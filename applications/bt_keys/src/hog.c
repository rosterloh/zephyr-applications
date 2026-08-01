/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * HID-over-GATT (HoG) service exposing two input reports: a standard boot-style
 * keyboard report (report id 1) and a consumer-control report (report id 2).
 * Based on Zephyr's peripheral_hids sample, extended with the second report so
 * keys can be mapped to either the keyboard or consumer usage page.
 */

#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include "hog.h"

LOG_MODULE_REGISTER(hog, LOG_LEVEL_INF);

enum {
	HIDS_REMOTE_WAKE = BIT(0),
	HIDS_NORMALLY_CONNECTABLE = BIT(1),
};

enum {
	HIDS_INPUT = 0x01,
	HIDS_OUTPUT = 0x02,
	HIDS_FEATURE = 0x03,
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

static struct hids_report keyboard_ref = {
	.id = REPORT_ID_KEYBOARD,
	.type = HIDS_INPUT,
};

static struct hids_report consumer_ref = {
	.id = REPORT_ID_CONSUMER,
	.type = HIDS_INPUT,
};

static uint8_t keyboard_notify;
static uint8_t consumer_notify;
static uint8_t ctrl_point;

static ssize_t read_info(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			 uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data,
				 sizeof(struct hids_info));
}

static ssize_t read_report_map(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			       uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, hid_report_desc,
				 hid_report_desc_len);
}

static ssize_t read_report(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			   uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data,
				 sizeof(struct hids_report));
}

static ssize_t read_input_report(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
				 uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, NULL, 0);
}

static void keyboard_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	keyboard_notify = (value == BT_GATT_CCC_NOTIFY) ? 1 : 0;
}

static void consumer_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	consumer_notify = (value == BT_GATT_CCC_NOTIFY) ? 1 : 0;
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
	hog_svc, BT_GATT_PRIMARY_SERVICE(BT_UUID_HIDS),
	BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_INFO, BT_GATT_CHRC_READ, BT_GATT_PERM_READ, read_info,
			       NULL, &info),
	BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT_MAP, BT_GATT_CHRC_READ, BT_GATT_PERM_READ,
			       read_report_map, NULL, NULL),
	/* Keyboard input report - value attribute at index 6. */
	BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       HOG_PERM_READ, read_input_report, NULL, NULL),
	BT_GATT_CCC(keyboard_ccc_changed, HOG_PERM_READ | HOG_PERM_WRITE),
	BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ, read_report, NULL,
			   &keyboard_ref),
	/* Consumer input report - value attribute at index 10. */
	BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       HOG_PERM_READ, read_input_report, NULL, NULL),
	BT_GATT_CCC(consumer_ccc_changed, HOG_PERM_READ | HOG_PERM_WRITE),
	BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ, read_report, NULL,
			   &consumer_ref),
	BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_CTRL_POINT, BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_WRITE, NULL, write_ctrl_point, &ctrl_point), );

/* Value-attribute indices into hog_svc.attrs (see layout comments above). */
#define ATTR_KEYBOARD_REPORT 6
#define ATTR_CONSUMER_REPORT 10

void hog_notify_keyboard(uint8_t modifiers, const uint8_t keycodes[HID_KEYBOARD_KEYS])
{
	if (!keyboard_notify) {
		return;
	}

	uint8_t report[2 + HID_KEYBOARD_KEYS] = {modifiers, 0};

	memcpy(&report[2], keycodes, HID_KEYBOARD_KEYS);

	int err =
		bt_gatt_notify(NULL, &hog_svc.attrs[ATTR_KEYBOARD_REPORT], report, sizeof(report));

	if (err) {
		LOG_WRN("keyboard notify failed: %d", err);
	}
}

void hog_notify_consumer(uint16_t usage)
{
	if (!consumer_notify) {
		return;
	}

	uint8_t report[2];

	sys_put_le16(usage, report);

	int err =
		bt_gatt_notify(NULL, &hog_svc.attrs[ATTR_CONSUMER_REPORT], report, sizeof(report));

	if (err) {
		LOG_WRN("consumer notify failed: %d", err);
	}
}
