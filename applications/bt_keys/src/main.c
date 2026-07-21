/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * bt_keys - Adafruit NeoKey 1x4 turned into a Bluetooth HID keypad.
 *
 * The four seesaw-connected keys are scanned and reported over HID-over-GATT
 * (keyboard + consumer reports). Each key's NeoPixel lights while held. The
 * firmware is updatable over Bluetooth via MCUmgr SMP (see README).
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

#include "hog.h"
#include "keys.h"

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

#define SCAN_INTERVAL K_MSEC(20)

/* Seesaw GPIO pins the four keys are wired to (see board overlay). */
static const uint8_t key_pin[BT_KEYS_NUM_KEYS] = {4, 5, 6, 7};

#define KEYPAD_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(adafruit_seesaw_gpio)
#define STRIP_NODE  DT_COMPAT_GET_ANY_STATUS_OKAY(adafruit_seesaw_neopixel)

static const struct device *const keypad_dev = DEVICE_DT_GET(KEYPAD_NODE);
static const struct device *const strip_dev = DEVICE_DT_GET(STRIP_NODE);

/* Colour shown on a key's NeoPixel while it is held. */
static const struct led_rgb key_lit = {.r = 0, .g = 24, .b = 24};
static const struct led_rgb key_off = {.r = 0, .g = 0, .b = 0};

/* ---------- Bluetooth ---------- */

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_HIDS_VAL),
		      BT_UUID_16_ENCODE(BT_UUID_BAS_VAL)),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		LOG_ERR("Connection failed (err 0x%02x)", err);
		return;
	}

	LOG_INF("Connected");

	if (bt_conn_set_security(conn, BT_SECURITY_L2)) {
		LOG_WRN("Failed to set security");
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("Disconnected (reason 0x%02x)", reason);
}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	if (err) {
		LOG_WRN("Security failed: level %u err %d", level, err);
	} else {
		LOG_INF("Security changed: level %u", level);
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed,
};

static void bt_ready(int err)
{
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return;
	}

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		bt_keys_settings_register();
		settings_load();
	}

	bt_bas_set_battery_level(100);

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err) {
		LOG_ERR("Advertising failed to start (err %d)", err);
		return;
	}

	LOG_INF("Advertising as \"%s\"", CONFIG_BT_DEVICE_NAME);
}

/* ---------- Keypad ---------- */

static int keypad_init(void)
{
	if (!device_is_ready(keypad_dev)) {
		LOG_ERR("Keypad GPIO not ready");
		return -ENODEV;
	}

	for (int i = 0; i < BT_KEYS_NUM_KEYS; i++) {
		int err = gpio_pin_configure(keypad_dev, key_pin[i], GPIO_INPUT | GPIO_PULL_UP);

		if (err) {
			LOG_ERR("configure key pin %u: %d", key_pin[i], err);
			return err;
		}
	}

	return 0;
}

/* Returns a bitmask of pressed keys (bit i == key i), or negative on error. */
static int keypad_scan(void)
{
	gpio_port_value_t raw;
	int err = gpio_port_get_raw(keypad_dev, &raw);

	if (err) {
		return err;
	}

	int pressed = 0;

	for (int i = 0; i < BT_KEYS_NUM_KEYS; i++) {
		/* Keys are active-low with pull-ups: a 0 bit means pressed. */
		if (!(raw & BIT(key_pin[i]))) {
			pressed |= BIT(i);
		}
	}

	return pressed;
}

static void leds_update(int pressed_mask)
{
	struct led_rgb buf[BT_KEYS_NUM_KEYS];

	for (int i = 0; i < BT_KEYS_NUM_KEYS; i++) {
		buf[i] = (pressed_mask & BIT(i)) ? key_lit : key_off;
	}

	int err = led_strip_update_rgb(strip_dev, buf, BT_KEYS_NUM_KEYS);

	if (err) {
		LOG_WRN("led_strip update failed: %d", err);
	}
}

/* Translate the pressed mask into HID reports and notify on change. */
static void report_keys(int pressed_mask)
{
	static uint8_t prev_kbd[HOG_KEYBOARD_KEYS];
	static uint16_t prev_consumer;

	uint8_t kbd[HOG_KEYBOARD_KEYS] = {0};
	uint16_t consumer = 0;
	int kbd_idx = 0;

	for (int i = 0; i < BT_KEYS_NUM_KEYS; i++) {
		if (!(pressed_mask & BIT(i))) {
			continue;
		}

		if (bt_keys_map[i].type == BT_KEYS_TYPE_CONSUMER) {
			if (consumer == 0) {
				consumer = bt_keys_map[i].usage;
			}
		} else if (kbd_idx < HOG_KEYBOARD_KEYS) {
			kbd[kbd_idx++] = (uint8_t)bt_keys_map[i].usage;
		}
	}

	if (memcmp(kbd, prev_kbd, sizeof(kbd)) != 0) {
		memcpy(prev_kbd, kbd, sizeof(kbd));
		hog_notify_keyboard(0, kbd);
	}

	if (consumer != prev_consumer) {
		prev_consumer = consumer;
		hog_notify_consumer(consumer);
	}
}

int main(void)
{
	int err;

	LOG_INF("bt_keys starting");

	if (keypad_init()) {
		return 0;
	}

	if (!device_is_ready(strip_dev)) {
		LOG_ERR("LED strip not ready");
		return 0;
	}
	leds_update(0);

	err = bt_enable(bt_ready);
	if (err) {
		LOG_ERR("bt_enable failed (err %d)", err);
		return 0;
	}

	int prev_mask = 0;

	while (1) {
		int mask = keypad_scan();

		if (mask < 0) {
			LOG_WRN("keypad scan failed: %d", mask);
			k_sleep(SCAN_INTERVAL);
			continue;
		}

		if (mask != prev_mask) {
			report_keys(mask);
			leds_update(mask);
			prev_mask = mask;
		}

		k_sleep(SCAN_INTERVAL);
	}

	return 0;
}
