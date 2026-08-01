/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BT_KEYS_USB_HID_H_
#define BT_KEYS_USB_HID_H_

#include <stdint.h>

#include "report_desc.h"

/*
 * Bring up the USB device stack and the HID class instance. Only built when
 * CONFIG_USB_DEVICE_STACK_NEXT is enabled (currently the esp32s3 board target
 * only - esp32c3 has no USB device controller for a HID class).
 */
int bt_keys_usb_hid_init(void);

/* Same semantics as hog_notify_keyboard(), for the USB HID transport. */
void usb_hid_notify_keyboard(uint8_t modifiers, const uint8_t keycodes[HID_KEYBOARD_KEYS]);

/* Same semantics as hog_notify_consumer(), for the USB HID transport. */
void usb_hid_notify_consumer(uint16_t usage);

#endif /* BT_KEYS_USB_HID_H_ */
