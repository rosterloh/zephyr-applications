/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BT_KEYS_HOG_H_
#define BT_KEYS_HOG_H_

#include <stdint.h>

#include "report_desc.h"

/* Register the HID Service. Call before settings_load() so CCCs restore. */
int hog_init(void);

/*
 * Notify the connected host of the current keyboard state. keycodes holds up
 * to HID_KEYBOARD_KEYS pressed HID keyboard usages (0 = empty slot).
 */
void hog_notify_keyboard(uint8_t modifiers, const uint8_t keycodes[HID_KEYBOARD_KEYS]);

/*
 * Notify the connected host of the current consumer-control state. usage is the
 * 16-bit consumer usage currently held, or 0 when released.
 */
void hog_notify_consumer(uint16_t usage);

#endif /* BT_KEYS_HOG_H_ */
