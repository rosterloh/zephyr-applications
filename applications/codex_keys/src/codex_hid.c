/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * HID-over-GATT service exposing a single vendor-defined collection: one
 * 63-byte input report and one 63-byte output report under report id 6. The
 * descriptor and framing mirror the Work Louder Codex Micro keyboard, which is
 * what ChatGPT Desktop probes for. See README.md for provenance.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "codex_hid.h"

LOG_MODULE_REGISTER(codex_hid, LOG_LEVEL_INF);

#define REPORT_ID       0x06
#define REPORT_BODY_LEN 63
#define PAYLOAD_MAX     (REPORT_BODY_LEN - 2)
#define MSG_TYPE_JSON   2

/* The reference firmware paces fragments to avoid overrunning the host. */
#define FRAGMENT_GAP K_MSEC(4)

enum {
	HIDS_REMOTE_WAKE = BIT(0),
	HIDS_NORMALLY_CONNECTABLE = BIT(1),
};

enum {
	HIDS_INPUT = 0x01,
	HIDS_OUTPUT = 0x02,
};

struct hids_info {
	uint16_t version;
	uint8_t code;
	uint8_t flags;
} __packed;

struct hids_report {
	uint8_t id;
	uint8_t type;
} __packed;

static struct hids_info info = {
	.version = 0x0111,
	.code = 0x00,
	.flags = HIDS_NORMALLY_CONNECTABLE,
};

static struct hids_report input_ref = {.id = REPORT_ID, .type = HIDS_INPUT};
static struct hids_report output_ref = {.id = REPORT_ID, .type = HIDS_OUTPUT};

/*
 * One vendor-defined application collection. On BLE the report id selects the
 * characteristic and is not part of the 63-byte report body.
 */
/* clang-format off */
static const uint8_t report_map[] = {
	0x06, 0x00, 0xFF,     /* Usage Page (Vendor Defined 0xFF00) */
	0x09, 0x01,           /* Usage (1) */
	0xA1, 0x01,           /* Collection (Application) */
	0x85, REPORT_ID,      /*  Report ID (6) */
	0x15, 0x00,           /*  Logical Minimum (0) */
	0x26, 0xFF, 0x00,     /*  Logical Maximum (255) */
	0x75, 0x08,           /*  Report Size (8) */
	0x95, 0x3F,           /*  Report Count (63) */
	0x09, 0x01,           /*  Usage (1) */
	0x81, 0x02,           /*  Input (Data,Var,Abs) */
	0x95, 0x3F,           /*  Report Count (63) */
	0x09, 0x02,           /*  Usage (2) */
	0x91, 0x02,           /*  Output (Data,Var,Abs) */
	0xC0,                 /* End Collection */
};
/* clang-format on */

static uint8_t input_notify;
static uint8_t protocol_mode = 0x01; /* report protocol */
static uint8_t ctrl_point;

/* Reassembly of fragmented host requests. Only touched from GATT writes. */
static char rx_buf[CODEX_JSON_MAX];
static size_t rx_len;

static ssize_t read_info(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			 uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data,
				 sizeof(struct hids_info));
}

static ssize_t read_report_map(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			       uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, report_map, sizeof(report_map));
}

static ssize_t read_report_ref(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			       uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data,
				 sizeof(struct hids_report));
}

static ssize_t read_protocol_mode(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
				  uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data,
				 sizeof(protocol_mode));
}

static ssize_t write_protocol_mode(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				   const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	if (offset != 0 || len != sizeof(protocol_mode)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	protocol_mode = *(const uint8_t *)buf;
	return len;
}

/* Reports are notify-only; a read just returns an empty value. */
static ssize_t read_empty(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			  uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, NULL, 0);
}

static void input_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	input_notify = (value == BT_GATT_CCC_NOTIFY) ? 1 : 0;
	LOG_INF("host %s input reports", input_notify ? "subscribed to" : "unsubscribed from");
}

/* True once rx_buf holds a complete top-level JSON object. */
static bool rx_object_complete(void)
{
	int depth = 0;
	bool in_string = false;

	for (size_t i = 0; i < rx_len; i++) {
		char c = rx_buf[i];

		if (in_string) {
			if (c == '\\') {
				i++;
			} else if (c == '"') {
				in_string = false;
			}
			continue;
		}

		if (c == '"') {
			in_string = true;
		} else if (c == '{') {
			depth++;
		} else if (c == '}' && --depth == 0) {
			return true;
		}
	}

	return false;
}

static void rx_append(const char *payload, size_t len)
{
	/* A fresh top-level object means an earlier fragment was dropped. */
	if (rx_len > 0 && len >= 9 && memcmp(payload, "{\"method\"", 9) == 0) {
		LOG_WRN("resynchronising, discarded %u stale bytes", (unsigned int)rx_len);
		rx_len = 0;
	}

	if (rx_len == 0) {
		while (len > 0 && *payload != '{') {
			payload++;
			len--;
		}
	}

	if (len == 0) {
		return;
	}

	if (rx_len + len >= sizeof(rx_buf)) {
		LOG_WRN("request exceeds %zu bytes, dropping", sizeof(rx_buf));
		rx_len = 0;
		return;
	}

	memcpy(rx_buf + rx_len, payload, len);
	rx_len += len;
	rx_buf[rx_len] = '\0';

	if (rx_object_complete()) {
		LOG_INF("rx: %s", rx_buf);
		codex_rpc_handle(rx_buf);
		rx_len = 0;
	}
}

static ssize_t write_output_report(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				   const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	const uint8_t *data = buf;

	if (offset != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	LOG_HEXDUMP_DBG(data, len, "output report");

	/* HOGP strips the report id; also accept hosts that forward it. */
	size_t body = (len >= 3 && data[0] == REPORT_ID) ? 1 : 0;

	if (len < body + 2 || data[body] != MSG_TYPE_JSON) {
		LOG_WRN("ignoring output report (len %u, type %u)", len,
			len > body ? data[body] : 0);
		return len;
	}

	size_t payload_len = MIN(data[body + 1], PAYLOAD_MAX);

	if (len < body + 2 + payload_len) {
		LOG_WRN("truncated fragment: claims %u bytes, got %u", (unsigned int)payload_len,
			len);
		return len;
	}

	rx_append((const char *)(data + body + 2), payload_len);
	return len;
}

static ssize_t write_ctrl_point(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	uint8_t *value = attr->user_data;

	if (offset + len > sizeof(ctrl_point)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	memcpy(value + offset, buf, len);
	return len;
}

/* HID access requires an encrypted link. */
#define HOG_PERM_READ  BT_GATT_PERM_READ_ENCRYPT
#define HOG_PERM_WRITE BT_GATT_PERM_WRITE_ENCRYPT

BT_GATT_SERVICE_DEFINE(
	codex_hid_svc, BT_GATT_PRIMARY_SERVICE(BT_UUID_HIDS),
	BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_INFO, BT_GATT_CHRC_READ, BT_GATT_PERM_READ, read_info,
			       NULL, &info),
	BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT_MAP, BT_GATT_CHRC_READ, BT_GATT_PERM_READ,
			       read_report_map, NULL, NULL),
	BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_PROTOCOL_MODE,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, read_protocol_mode,
			       write_protocol_mode, &protocol_mode),
	/* Vendor input report - value attribute at index 8. */
	BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       HOG_PERM_READ, read_empty, NULL, NULL),
	BT_GATT_CCC(input_ccc_changed, HOG_PERM_READ | HOG_PERM_WRITE),
	BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ, read_report_ref, NULL,
			   &input_ref),
	/* Vendor output report. */
	BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE |
				       BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       HOG_PERM_READ | HOG_PERM_WRITE, read_empty, write_output_report,
			       NULL),
	BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ, read_report_ref, NULL,
			   &output_ref),
	BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_CTRL_POINT, BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_WRITE, NULL, write_ctrl_point, &ctrl_point), );

/* Value-attribute index into codex_hid_svc.attrs (see layout comment above). */
#define ATTR_INPUT_REPORT 8

/* Key events and RPC replies are sent from different threads. */
static K_MUTEX_DEFINE(tx_lock);
static char framed[CODEX_JSON_MAX];

int codex_hid_send(const char *json)
{
	int ret = 0;

	if (!input_notify) {
		return -ENOTCONN;
	}

	k_mutex_lock(&tx_lock, K_FOREVER);

	int n = snprintf(framed, sizeof(framed), "%s\n", json);

	if (n < 0 || n >= (int)sizeof(framed)) {
		k_mutex_unlock(&tx_lock);
		return -EMSGSIZE;
	}

	LOG_INF("tx: %s", json);

	for (int offset = 0; offset < n; offset += PAYLOAD_MAX) {
		uint8_t report[REPORT_BODY_LEN] = {0};
		size_t chunk = MIN((size_t)(n - offset), PAYLOAD_MAX);

		report[0] = MSG_TYPE_JSON;
		report[1] = chunk;
		memcpy(&report[2], framed + offset, chunk);

		ret = bt_gatt_notify(NULL, &codex_hid_svc.attrs[ATTR_INPUT_REPORT], report,
				     sizeof(report));
		if (ret) {
			LOG_WRN("notify failed: %d", ret);
			break;
		}

		k_sleep(FRAGMENT_GAP);
	}

	k_mutex_unlock(&tx_lock);
	return ret;
}
