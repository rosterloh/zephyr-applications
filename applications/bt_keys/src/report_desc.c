/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "report_desc.h"

/* clang-format off */
const uint8_t hid_report_desc[] = {
	/* Keyboard */
	0x05, 0x01,               /* Usage Page (Generic Desktop) */
	0x09, 0x06,               /* Usage (Keyboard) */
	0xA1, 0x01,               /* Collection (Application) */
	0x85, REPORT_ID_KEYBOARD, /*  Report ID (1) */
	0x05, 0x07,               /*  Usage Page (Keyboard/Keypad) */
	0x19, 0xE0,               /*  Usage Minimum (Left Control) */
	0x29, 0xE7,               /*  Usage Maximum (Right GUI) */
	0x15, 0x00,               /*  Logical Minimum (0) */
	0x25, 0x01,               /*  Logical Maximum (1) */
	0x75, 0x01,               /*  Report Size (1) */
	0x95, 0x08,               /*  Report Count (8) */
	0x81, 0x02,               /*  Input (Data,Var,Abs) - modifier byte */
	0x95, 0x01,               /*  Report Count (1) */
	0x75, 0x08,               /*  Report Size (8) */
	0x81, 0x03,               /*  Input (Const) - reserved byte */
	0x95, 0x06,               /*  Report Count (6) */
	0x75, 0x08,               /*  Report Size (8) */
	0x15, 0x00,               /*  Logical Minimum (0) */
	0x25, 0xFF,               /*  Logical Maximum (255) */
	0x05, 0x07,               /*  Usage Page (Keyboard/Keypad) */
	0x19, 0x00,               /*  Usage Minimum (0) */
	0x29, 0xFF,               /*  Usage Maximum (255) */
	0x81, 0x00,               /*  Input (Data,Array) - keycodes */
	0xC0,                     /* End Collection */

	/* Consumer control */
	0x05, 0x0C,               /* Usage Page (Consumer) */
	0x09, 0x01,               /* Usage (Consumer Control) */
	0xA1, 0x01,               /* Collection (Application) */
	0x85, REPORT_ID_CONSUMER, /*  Report ID (2) */
	0x15, 0x00,               /*  Logical Minimum (0) */
	0x26, 0xFF, 0x03,         /*  Logical Maximum (0x3FF) */
	0x19, 0x00,               /*  Usage Minimum (0) */
	0x2A, 0xFF, 0x03,         /*  Usage Maximum (0x3FF) */
	0x75, 0x10,               /*  Report Size (16) */
	0x95, 0x01,               /*  Report Count (1) */
	0x81, 0x00,               /*  Input (Data,Array) */
	0xC0,                     /* End Collection */
};
/* clang-format on */

const size_t hid_report_desc_len = sizeof(hid_report_desc);
