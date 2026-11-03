/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * HID-over-GATT (HoG) on Zephyr's HID Service with two input reports: a
 * boot-style keyboard report (report id 1) and a consumer-control report
 * (report id 2), so keys can map to either usage page.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/bluetooth/services/hids.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include "hog.h"

LOG_MODULE_REGISTER(hog, LOG_LEVEL_INF);

int hog_init(void)
{
	static const struct bt_hids_cb cb = {0};
	const struct bt_hids_register_param param = {
		.bcd_hid = 0x0111,
		.flags = BT_HIDS_INFO_FLAG_NORMALLY_CONNECTABLE,
		.report_map = hid_report_desc,
		.report_map_len = hid_report_desc_len,
		.input_report_ids = {REPORT_ID_KEYBOARD, REPORT_ID_CONSUMER},
		.cb = &cb,
	};

	return bt_hids_register(&param);
}

static void send(uint8_t id, const uint8_t *report, uint16_t len)
{
	int err = bt_hids_send_report(NULL, id, report, len, NULL, NULL);

	/* -ENOTCONN: no host subscribed yet. */
	if (err && err != -ENOTCONN) {
		LOG_WRN("report %u notify failed: %d", id, err);
	}
}

void hog_notify_keyboard(uint8_t modifiers, const uint8_t keycodes[HID_KEYBOARD_KEYS])
{
	uint8_t report[2 + HID_KEYBOARD_KEYS] = {modifiers, 0};

	memcpy(&report[2], keycodes, HID_KEYBOARD_KEYS);
	send(REPORT_ID_KEYBOARD, report, sizeof(report));
}

void hog_notify_consumer(uint16_t usage)
{
	uint8_t report[2];

	sys_put_le16(usage, report);
	send(REPORT_ID_CONSUMER, report, sizeof(report));
}
