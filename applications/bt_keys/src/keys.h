/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BT_KEYS_KEYS_H_
#define BT_KEYS_KEYS_H_

#include <stdint.h>

/* Number of physical keys on the NeoKey 1x4. */
#define BT_KEYS_NUM_KEYS 4

/*
 * A physical key sends either a standard keyboard usage (HID Keyboard/Keypad
 * page 0x07, 8-bit usage) or a consumer-control usage (HID Consumer page 0x0C,
 * 16-bit usage). The two are reported over different HID input reports, so a
 * key's type selects which report carries it.
 */
enum bt_keys_type {
	BT_KEYS_TYPE_KEYBOARD = 0,
	BT_KEYS_TYPE_CONSUMER = 1,
};

struct bt_keys_binding {
	uint8_t type;   /* enum bt_keys_type */
	uint16_t usage; /* HID usage code on the type's usage page */
};

/* Current bindings, index 0..3 matching physical keys 0..3 (left to right). */
extern struct bt_keys_binding bt_keys_map[BT_KEYS_NUM_KEYS];

/*
 * Register the settings handler and load any persisted bindings. Call after
 * settings_subsys_init()/before settings_load(). Falls back to the compiled-in
 * media-key defaults for any binding not present in storage.
 */
int bt_keys_settings_register(void);

/* Persist a single binding to storage. */
int bt_keys_set(uint8_t index, enum bt_keys_type type, uint16_t usage);

#endif /* BT_KEYS_KEYS_H_ */
