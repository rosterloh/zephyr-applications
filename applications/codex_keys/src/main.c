/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * codex_keys - a Codex Micro compatible BLE controller built from 3x Adafruit
 * NeoKey 1x4 and an Adafruit I2C rotary encoder.
 *
 * The twelve keys cover the host's full key set (six agent keys, six command
 * keys) and the encoder provides the dial. The NeoPixels still only show a
 * local press colour; rendering the host's agent lighting state from
 * v.oai.thstatus is the next step.
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

#include "codex_rpc.h"

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

#define SCAN_INTERVAL K_MSEC(20)

#define KEYS_PER_PAD 4
#define NUM_PADS     3
#define NUM_KEYS     (NUM_PADS * KEYS_PER_PAD)

/* Seesaw GPIO pins the four keys of a NeoKey 1x4 are wired to. */
static const uint8_t key_pin[KEYS_PER_PAD] = {4, 5, 6, 7};

/* Encoder push switch, seesaw GPIO on the rotary encoder breakout. */
#define ENCODER_SWITCH_PIN 24

/*
 * Seesaw counts up when the shaft turns anticlockwise, and this encoder emits
 * one count per detent. Both are worth re-checking against the actual box:
 * flip the sign if the dial scrolls backwards, raise the divisor if one detent
 * produces several steps.
 */
#define ENCODER_CW_IS_NEGATIVE  1
#define ENCODER_COUNTS_PER_STEP 1

/* A fast spin must not monopolise the scan loop; each step is a BLE round. */
#define ENCODER_STEPS_PER_POLL 8

struct keypad {
	const struct device *gpio;
	const struct device *leds;
};

static const struct keypad keypads[NUM_PADS] = {
	{DEVICE_DT_GET(DT_NODELABEL(neokey0_gpio)), DEVICE_DT_GET(DT_NODELABEL(neokey0_leds))},
	{DEVICE_DT_GET(DT_NODELABEL(neokey1_gpio)), DEVICE_DT_GET(DT_NODELABEL(neokey1_leds))},
	{DEVICE_DT_GET(DT_NODELABEL(neokey2_gpio)), DEVICE_DT_GET(DT_NODELABEL(neokey2_leds))},
};

static const struct device *const encoder_dev = DEVICE_DT_GET(DT_NODELABEL(encoder));
static const struct device *const encoder_gpio = DEVICE_DT_GET(DT_NODELABEL(encoder_gpio));

/*
 * Physical key -> host key id. Keys 0-3 are the first NeoKey, 4-7 the second,
 * 8-11 the third. Agent keys carry their slot index; command keys use -1.
 * Rearrange to match how the strips ended up in the box.
 */
/* clang-format off */
static const struct {
	const char *id;
	int8_t agent;
} key_map[NUM_KEYS] = {
	{"AG00",   0}, {"AG01",   1}, {"AG02",  2}, {"AG03",  3},
	{"AG04",   4}, {"AG05",   5}, {"ACT06", -1}, {"ACT07", -1},
	{"ACT08", -1}, {"ACT09", -1}, {"ACT10", -1}, {"ACT12", -1},
};
/* clang-format on */

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
		settings_load();
	}

	bt_bas_set_battery_level(100);

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err) {
		LOG_ERR("Advertising failed to start (err %d)", err);
		return;
	}

	LOG_INF("Advertising as \"%s\" (%04x:%04x)", CONFIG_BT_DEVICE_NAME, CONFIG_BT_DIS_PNP_VID,
		CONFIG_BT_DIS_PNP_PID);
}

/* ---------- Keypads ---------- */

static int keypads_init(void)
{
	for (int pad = 0; pad < NUM_PADS; pad++) {
		if (!device_is_ready(keypads[pad].gpio) || !device_is_ready(keypads[pad].leds)) {
			LOG_ERR("Keypad %d not ready", pad);
			return -ENODEV;
		}

		for (int i = 0; i < KEYS_PER_PAD; i++) {
			int err = gpio_pin_configure(keypads[pad].gpio, key_pin[i],
						     GPIO_INPUT | GPIO_PULL_UP);

			if (err) {
				LOG_ERR("configure pad %d pin %u: %d", pad, key_pin[i], err);
				return err;
			}
		}
	}

	return 0;
}

/* Returns a bitmask of pressed keys (bit i == key_map[i]), or negative on error. */
static int keypads_scan(void)
{
	int pressed = 0;

	for (int pad = 0; pad < NUM_PADS; pad++) {
		gpio_port_value_t raw;
		int err = gpio_port_get_raw(keypads[pad].gpio, &raw);

		if (err) {
			return err;
		}

		for (int i = 0; i < KEYS_PER_PAD; i++) {
			/* Keys are active-low with pull-ups: a 0 bit means pressed. */
			if (!(raw & BIT(key_pin[i]))) {
				pressed |= BIT(pad * KEYS_PER_PAD + i);
			}
		}
	}

	return pressed;
}

/* Only pushes pixels for pads whose keys actually changed. */
static void leds_update(int pressed_mask, int prev_mask)
{
	for (int pad = 0; pad < NUM_PADS; pad++) {
		int shift = pad * KEYS_PER_PAD;
		int mask = (BIT(KEYS_PER_PAD) - 1) << shift;

		if ((pressed_mask & mask) == (prev_mask & mask)) {
			continue;
		}

		struct led_rgb buf[KEYS_PER_PAD];

		for (int i = 0; i < KEYS_PER_PAD; i++) {
			buf[i] = (pressed_mask & BIT(shift + i)) ? key_lit : key_off;
		}

		int err = led_strip_update_rgb(keypads[pad].leds, buf, KEYS_PER_PAD);

		if (err) {
			LOG_WRN("pad %d led update failed: %d", pad, err);
		}
	}
}

static void report_keys(int pressed_mask, int prev_mask)
{
	for (int i = 0; i < NUM_KEYS; i++) {
		int now = pressed_mask & BIT(i);

		if (now == (prev_mask & BIT(i))) {
			continue;
		}

		codex_rpc_send_key(key_map[i].id, now ? CODEX_ACT_PRESS : CODEX_ACT_RELEASE,
				   key_map[i].agent);
	}
}

/* ---------- Rotary encoder ---------- */

static int32_t encoder_last;
static bool encoder_synced;

/* Turn accumulated counts into ENC_CW / ENC_CC step events. */
static void encoder_poll(void)
{
	struct sensor_value val;

	if (sensor_sample_fetch(encoder_dev) < 0 ||
	    sensor_channel_get(encoder_dev, SENSOR_CHAN_ROTATION, &val) < 0) {
		return;
	}

	if (!encoder_synced) {
		encoder_last = val.val1;
		encoder_synced = true;
		return;
	}

	int32_t delta = val.val1 - encoder_last;
	int steps = delta / ENCODER_COUNTS_PER_STEP;

	if (steps == 0) {
		return;
	}

	/* Keep the sub-step remainder so slow turns are not lost. */
	encoder_last += steps * ENCODER_COUNTS_PER_STEP;

	bool clockwise = ENCODER_CW_IS_NEGATIVE ? (steps < 0) : (steps > 0);
	int count = (steps < 0) ? -steps : steps;

	if (count > ENCODER_STEPS_PER_POLL) {
		LOG_WRN("dropping %d encoder steps", count - ENCODER_STEPS_PER_POLL);
		count = ENCODER_STEPS_PER_POLL;
	}

	while (count--) {
		codex_rpc_send_key(clockwise ? "ENC_CW" : "ENC_CC", CODEX_ACT_STEP, -1);
	}
}

static void encoder_switch_poll(void)
{
	static bool was_pressed;

	int val = gpio_pin_get_raw(encoder_gpio, ENCODER_SWITCH_PIN);

	if (val < 0) {
		return;
	}

	bool pressed = (val == 0); /* active-low */

	if (pressed != was_pressed) {
		was_pressed = pressed;
		codex_rpc_send_key("ENC", pressed ? CODEX_ACT_PRESS : CODEX_ACT_RELEASE, -1);
	}
}

static int encoder_init(void)
{
	if (!device_is_ready(encoder_dev) || !device_is_ready(encoder_gpio)) {
		LOG_ERR("Encoder not ready");
		return -ENODEV;
	}

	int err = gpio_pin_configure(encoder_gpio, ENCODER_SWITCH_PIN, GPIO_INPUT | GPIO_PULL_UP);

	if (err) {
		LOG_ERR("configure encoder switch: %d", err);
	}

	return err;
}

int main(void)
{
	int err;

	LOG_INF("codex_keys starting: %d keys + encoder", NUM_KEYS);

	if (keypads_init() || encoder_init()) {
		return 0;
	}

	/* Pretend every key was held so all three strips get blanked once. */
	leds_update(0, BIT(NUM_KEYS) - 1);

	err = bt_enable(bt_ready);
	if (err) {
		LOG_ERR("bt_enable failed (err %d)", err);
		return 0;
	}

	int prev_mask = 0;

	while (1) {
		int mask = keypads_scan();

		if (mask < 0) {
			LOG_WRN("keypad scan failed: %d", mask);
		} else if (mask != prev_mask) {
			leds_update(mask, prev_mask);
			report_keys(mask, prev_mask);
			prev_mask = mask;
		}

		encoder_poll();
		encoder_switch_poll();

		k_sleep(SCAN_INTERVAL);
	}

	return 0;
}
