/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * HID-over-GATT on Zephyr's HID Service, with a single vendor-defined
 * collection: one 63-byte input report and one 63-byte output report under
 * report id 6. The
 * descriptor and framing mirror the Work Louder Codex Micro keyboard, which is
 * what ChatGPT Desktop probes for. See README.md for provenance.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/bluetooth/services/hids.h>
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

/* Reassembly of fragmented host requests. Only touched from GATT writes. */
static char rx_buf[CODEX_JSON_MAX];
static size_t rx_len;

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

static void set_report(struct bt_conn *conn, enum bt_hids_report_type type, uint8_t id,
		       const uint8_t *data, uint16_t len)
{
	if (type != BT_HIDS_REPORT_TYPE_OUTPUT || id != REPORT_ID) {
		return;
	}

	LOG_HEXDUMP_DBG(data, len, "output report");

	/* HOGP strips the report id; also accept hosts that forward it. */
	size_t body = (len >= 3 && data[0] == REPORT_ID) ? 1 : 0;

	if (len < body + 2 || data[body] != MSG_TYPE_JSON) {
		LOG_WRN("ignoring output report (len %u, type %u)", len,
			len > body ? data[body] : 0);
		return;
	}

	size_t payload_len = MIN(data[body + 1], PAYLOAD_MAX);

	if (len < body + 2 + payload_len) {
		LOG_WRN("truncated fragment: claims %u bytes, got %u", (unsigned int)payload_len,
			len);
		return;
	}

	rx_append((const char *)(data + body + 2), payload_len);
}

int codex_hid_init(void)
{
	static const struct bt_hids_cb cb = {.set_report = set_report};
	const struct bt_hids_register_param param = {
		.bcd_hid = 0x0111,
		.flags = BT_HIDS_INFO_FLAG_NORMALLY_CONNECTABLE,
		.report_map = report_map,
		.report_map_len = sizeof(report_map),
		.input_report_ids = {REPORT_ID},
		.output_report_ids = {REPORT_ID},
		.cb = &cb,
	};

	return bt_hids_register(&param);
}

/* Key events and RPC replies are sent from different threads. */
static K_MUTEX_DEFINE(tx_lock);
static char framed[CODEX_JSON_MAX];

int codex_hid_send(const char *json)
{
	int ret = 0;

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

		ret = bt_hids_send_report(NULL, REPORT_ID, report, sizeof(report), NULL, NULL);
		if (ret) {
			/* -ENOTCONN: no host subscribed yet. */
			if (ret != -ENOTCONN) {
				LOG_WRN("notify failed: %d", ret);
			}
			break;
		}

		k_sleep(FRAGMENT_GAP);
	}

	k_mutex_unlock(&tx_lock);
	return ret;
}
