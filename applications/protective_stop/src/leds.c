/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app_settings.h"
#include "leds.h"
#include "lockstep.h"
#include "pstop_log.h"
#include "ring.h"
#include "uplink.h"

LOG_MODULE_REGISTER(pstop_leds, PSTOP_LOG_LEVEL);

#define LEDS_STACK_SIZE 1536
#define LEDS_PRIORITY   K_LOWEST_APPLICATION_THREAD_PRIO

#if DT_NODE_EXISTS(DT_ALIAS(pstop_ring)) && defined(CONFIG_LED_STRIP)
#define HAVE_RING 1
static const struct device *const ring_dev = DEVICE_DT_GET(DT_ALIAS(pstop_ring));
BUILD_ASSERT(DT_PROP(DT_ALIAS(pstop_ring), chain_length) == RING_LEDS,
	     "the pstop-ring strip must be 16 pixels long");
#endif

#if DT_NODE_EXISTS(DT_ALIAS(led_strip)) && defined(CONFIG_LED_STRIP)
#define HAVE_PIXEL 1
static const struct device *const pixel_dev = DEVICE_DT_GET(DT_ALIAS(led_strip));
#endif

static struct k_spinlock locate_lock;
static int64_t locate_until_ms;

static const char *state_name(enum ring_state st)
{
	switch (st) {
	case RING_IDLE:
		return "idle (white)";
	case RING_UNREACHABLE:
		return "unreachable (amber)";
	case RING_BOND:
		return "bonded (blue)";
	case RING_OK:
		return "ok (green)";
	case RING_STOP:
		return "stop (red)";
	case RING_MISMATCH:
		return "sampler mismatch (purple)";
	case RING_LOCATE:
		return "locate (LED 1)";
	default:
		return "?";
	}
}

void leds_locate(bool on)
{
	k_spinlock_key_t key = k_spin_lock(&locate_lock);

	locate_until_ms = on ? (k_uptime_get() + RING_LOCATE_TIMEOUT_MS) : 0;
	k_spin_unlock(&locate_lock, key);
}

bool leds_locate_active(void)
{
	k_spinlock_key_t key = k_spin_lock(&locate_lock);
	bool on = k_uptime_get() < locate_until_ms;

	k_spin_unlock(&locate_lock, key);
	return on;
}

static void collect(struct ring_input *in, struct ring_mismatch *mm, uint8_t *wave_head)
{
	struct uplink_status up;

	in->now_ms = (uint64_t)k_uptime_get();

	for (int slot = 0; slot < PSTOP_MAX_MACHINES; slot++) {
		const struct pstop_session *s = pstop_lockstep_session(slot);
		struct pstop_peer peer;
		struct ring_slot *rs = &in->slots[slot];

		(void)app_settings_peer_get(slot, &peer);
		rs->configured = peer.configured;
		if (pstop_lockstep_slot_active(slot)) {
			/* Read without the comparator's cooperation: a torn
			 * read costs one frame of the wrong colour, nothing
			 * more.
			 */
			rs->state = (uint8_t)s->state;
			rs->last_msg = s->last_msg;
			rs->last_reply_ms = s->last_reply_ms;
		} else {
			rs->state = PSTOP_SESS_IDLE;
			rs->last_msg = PSTOP_MESSAGE_UNKNOWN;
			rs->last_reply_ms = 0U;
		}
	}

	uplink_status_get(&up);
	in->uplink_up = (up.kind != UPLINK_NONE);
	in->mismatch = ring_mismatch_update(mm, pstop_lockstep_mismatches(), in->now_ms);
	in->locate = leds_locate_active();
	in->offset = app_settings_ring_offset();
	in->wave_head = *wave_head;
	*wave_head = (uint8_t)((*wave_head + 1U) % RING_LEDS);
}

#ifdef HAVE_PIXEL
static bool pixel_ok;

static enum status_iface status_iface_of(enum uplink_kind kind)
{
	switch (kind) {
	case UPLINK_ETHERNET:
	case UPLINK_HOST:
		return STATUS_IFACE_ETHERNET;
	case UPLINK_WIFI:
		return STATUS_IFACE_WIFI;
	default:
		return STATUS_IFACE_NONE;
	}
}
#endif

#if defined(HAVE_RING) || defined(HAVE_PIXEL)
static struct led_rgb to_led(struct ring_rgb c)
{
	return (struct led_rgb){.r = c.r, .g = c.g, .b = c.b};
}
#endif

#ifdef HAVE_PIXEL
/* The uplink's last octet, blinked in the uplink's colour; a red strobe
 * while the samplers disagree.
 */
static void paint_pixel(const struct ring_input *in)
{
	struct uplink_status up;
	enum status_iface iface;
	uint8_t octet;
	uint32_t cycle;
	struct led_rgb pixel;

	if (!pixel_ok) {
		return;
	}

	uplink_status_get(&up);
	iface = status_iface_of(up.kind);
	octet = (uint8_t)(up.ipv4 & 0xFFU);
	cycle = status_cycle_ms(iface, octet, in->mismatch);
	pixel = to_led(status_pixel(iface, octet, in->mismatch, (uint32_t)(in->now_ms % cycle)));
	(void)led_strip_update_rgb(pixel_dev, &pixel, 1);
}
#endif

static void leds_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	struct ring_mismatch mm;
	struct ring_input in = {0};
	struct ring_rgb px[RING_LEDS];
	enum ring_state last = (enum ring_state) - 1;
	uint8_t wave_head = 0U;

	ring_mismatch_init(&mm, pstop_lockstep_mismatches());

	while (true) {
		collect(&in, &mm, &wave_head);

		enum ring_state st = ring_frame(&in, px);

		if (st != last) {
			LOG_INF("ring: %s", state_name(st));
			last = st;
		}

#ifdef HAVE_RING
		struct led_rgb ring_px[RING_LEDS];

		for (int i = 0; i < RING_LEDS; i++) {
			ring_px[i] = to_led(px[i]);
		}
		(void)led_strip_update_rgb(ring_dev, ring_px, RING_LEDS);
#endif

#ifdef HAVE_PIXEL
		paint_pixel(&in);
#endif
		k_sleep(K_MSEC(RING_FRAME_MS));
	}
}

K_THREAD_STACK_DEFINE(leds_stack, LEDS_STACK_SIZE);
static struct k_thread leds_thread_data;

int leds_start(void)
{
#ifdef HAVE_RING
	if (!device_is_ready(ring_dev)) {
		LOG_ERR("LED ring not ready");
		return -ENODEV;
	}
#endif
#ifdef HAVE_PIXEL
	pixel_ok = device_is_ready(pixel_dev);
	if (!pixel_ok) {
		LOG_WRN("status pixel not ready");
	}
#endif
	(void)k_thread_create(&leds_thread_data, leds_stack, K_THREAD_STACK_SIZEOF(leds_stack),
			      leds_thread, NULL, NULL, NULL, LEDS_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&leds_thread_data, "pstop_leds");
	return 0;
}
