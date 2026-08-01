/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * HID report descriptor shared by both HID transports (BLE HoG and USB HID):
 * a keyboard collection (report id 1, 8-byte report) and a consumer-control
 * collection (report id 2, one 16-bit usage).
 */

#ifndef BT_KEYS_REPORT_DESC_H_
#define BT_KEYS_REPORT_DESC_H_

#include <stddef.h>
#include <stdint.h>

#define REPORT_ID_KEYBOARD 0x01
#define REPORT_ID_CONSUMER 0x02

/* Keyboard input report: [modifiers][reserved][keycode x6]. */
#define HID_KEYBOARD_KEYS 6

extern const uint8_t hid_report_desc[];
extern const size_t hid_report_desc_len;

#endif /* BT_KEYS_REPORT_DESC_H_ */
