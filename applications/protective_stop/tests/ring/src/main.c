/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Every colour rule of the ring and the status pixel, as pure functions:
 * segment division for 1-4 configured slots, rotation wraparound, locate and
 * mismatch overriding the state colours, the purple persistence filter, and
 * the octet blink pattern.
 */

#include <string.h>

#include <zephyr/ztest.h>

#include "pstop/pstop_msg.h"
#include "ring.h"
#include "session.h"

/* 100 ms into an amber period, so a single-blink amber is lit. */
#define NOW ((80U * (RING_AMBER_ON_MS + RING_AMBER_OFF_MS + RING_AMBER_REST_MS)) + 100U)

static const struct ring_rgb OFF = {0, 0, 0};
static const struct ring_rgb WHITE = {RING_BRIGHTNESS / 2, RING_BRIGHTNESS / 2,
				      RING_BRIGHTNESS / 2};
static const struct ring_rgb PURPLE = {RING_BRIGHTNESS, 0, RING_BRIGHTNESS};
static const struct ring_rgb BLUE = {0, 0, RING_BRIGHTNESS};
static const struct ring_rgb LOCATE = {RING_BRIGHTNESS, RING_BRIGHTNESS, RING_BRIGHTNESS};

static struct ring_input in;
static struct ring_rgb px[RING_LEDS];

static bool eq(struct ring_rgb a, struct ring_rgb b)
{
	return (a.r == b.r) && (a.g == b.g) && (a.b == b.b);
}

static void before(void *f)
{
	ARG_UNUSED(f);
	(void)memset(&in, 0, sizeof(in));
	in.now_ms = NOW;
	in.uplink_up = true;
	(void)memset(px, 0xAA, sizeof(px));
}

static void set_slot(int slot, uint8_t state, uint8_t last_msg, uint64_t last_reply_ms)
{
	in.slots[slot].configured = true;
	in.slots[slot].state = state;
	in.slots[slot].last_msg = last_msg;
	in.slots[slot].last_reply_ms = last_reply_ms;
}

/* Dominant hue of a pixel, ignoring the liveness wave's scaling. */
static char hue(struct ring_rgb c)
{
	if ((c.r == 0U) && (c.g == 0U) && (c.b == 0U)) {
		return '.';
	}
	if ((c.r > 0U) && (c.g == 0U) && (c.b == 0U)) {
		return 'R';
	}
	if ((c.g > 0U) && (c.r == 0U) && (c.b == 0U)) {
		return 'G';
	}
	if ((c.b > 0U) && (c.r == 0U) && (c.g == 0U)) {
		return 'B';
	}
	if ((c.r > c.g) && (c.g > 0U) && (c.b == 0U)) {
		return 'A';
	}
	if ((c.r == c.b) && (c.g == 0U)) {
		return 'P';
	}
	if ((c.r == c.g) && (c.g == c.b)) {
		return 'W';
	}
	return '?';
}

static void hues(char out[RING_LEDS + 1])
{
	for (int i = 0; i < RING_LEDS; i++) {
		out[i] = hue(px[i]);
	}
	out[RING_LEDS] = '\0';
}

ZTEST(ring, test_no_machines_is_dim_white)
{
	zassert_equal(ring_frame(&in, px), RING_IDLE);
	for (int i = 0; i < RING_LEDS; i++) {
		zassert_true(eq(px[i], WHITE), "pixel %d", i);
	}
}

ZTEST(ring, test_one_machine_owns_the_whole_ring)
{
	char h[RING_LEDS + 1];

	set_slot(0, PSTOP_SESS_BONDED, PSTOP_MESSAGE_BOND, NOW - 10U);
	zassert_equal(ring_frame(&in, px), RING_BOND);
	hues(h);
	zassert_str_equal(h, "BBBBBBBBBBBBBBBB");
}

ZTEST(ring, test_segments_divide_evenly_in_slot_order)
{
	char h[RING_LEDS + 1];

	/* Slots 0, 2, 3 configured; slot 1 not. Three segments of 5, 5, 6. */
	set_slot(0, PSTOP_SESS_BONDED, PSTOP_MESSAGE_STOP, NOW - 10U);
	set_slot(2, PSTOP_SESS_BONDED, PSTOP_MESSAGE_OK, NOW - 10U);
	set_slot(3, PSTOP_SESS_BONDED, PSTOP_MESSAGE_BOND, NOW - 10U);
	zassert_equal(ring_frame(&in, px), RING_STOP, "worst state wins");
	hues(h);
	zassert_str_equal(h, "RRRRRGGGGGBBBBBB");

	before(NULL);
	for (int s = 0; s < 4; s++) {
		set_slot(s, PSTOP_SESS_BONDED,
			 (s % 2 == 0) ? PSTOP_MESSAGE_BOND : PSTOP_MESSAGE_UNBOND, NOW - 10U);
	}
	(void)ring_frame(&in, px);
	hues(h);
	zassert_str_equal(h, "BBBBBBBBBBBBBBBB", "UNBOND shows as bond-blue");

	before(NULL);
	set_slot(1, PSTOP_SESS_BONDED, PSTOP_MESSAGE_OK, NOW - 10U);
	set_slot(3, PSTOP_SESS_BONDED, PSTOP_MESSAGE_STOP, NOW - 10U);
	(void)ring_frame(&in, px);
	hues(h);
	zassert_str_equal(h, "GGGGGGGGRRRRRRRR", "two slots = halves");
}

ZTEST(ring, test_stale_or_unbonded_is_amber)
{
	char h[RING_LEDS + 1];

	/* NOW is inside the first blink's on-phase. */
	zassert_equal(NOW % (RING_AMBER_ON_MS + RING_AMBER_OFF_MS + RING_AMBER_REST_MS), 100U);

	set_slot(0, PSTOP_SESS_BONDED, PSTOP_MESSAGE_OK, NOW - RING_LINK_FRESH_MS);
	set_slot(1, PSTOP_SESS_BONDING, PSTOP_MESSAGE_UNKNOWN, 0U);
	set_slot(2, PSTOP_SESS_REJECTED, PSTOP_MESSAGE_UNBOND, NOW - 10U);
	set_slot(3, PSTOP_SESS_BONDED, PSTOP_MESSAGE_OK, NOW + 5U); /* clock skew */
	zassert_equal(ring_frame(&in, px), RING_UNREACHABLE);
	hues(h);
	zassert_str_equal(h, "AAAAAAAAAAAAAAAA");

	/* In the rest gap the amber is dark. */
	in.now_ms = NOW + 500U;
	set_slot(0, PSTOP_SESS_BONDED, PSTOP_MESSAGE_OK, 1U);
	(void)ring_frame(&in, px);
	zassert_true(eq(px[0], OFF), "rest gap");
}

ZTEST(ring, test_amber_blink_count_names_the_layer)
{
	const uint32_t group = RING_AMBER_ON_MS + RING_AMBER_OFF_MS;
	int lit_uplink = 0;
	int lit_none = 0;

	set_slot(0, PSTOP_SESS_BONDING, PSTOP_MESSAGE_UNKNOWN, 0U);

	/* Count rising edges over one long period at 10 ms resolution. */
	for (int pass = 0; pass < 2; pass++) {
		bool up = (pass == 0);
		uint32_t period = ((up ? 1U : 3U) * group) + RING_AMBER_REST_MS;
		bool was = false;
		int edges = 0;

		in.uplink_up = up;
		for (uint32_t t = 0; t < period; t += 10U) {
			in.now_ms = (uint64_t)period * 1000U + t;
			(void)ring_frame(&in, px);
			bool on = !eq(px[0], OFF);

			if (on && !was) {
				edges++;
			}
			was = on;
		}
		if (up) {
			lit_uplink = edges;
		} else {
			lit_none = edges;
		}
	}
	zassert_equal(lit_uplink, 1, "machine silent, uplink fine");
	zassert_equal(lit_none, 3, "no uplink");
}

ZTEST(ring, test_wave_keeps_ok_alive)
{
	set_slot(0, PSTOP_SESS_BONDED, PSTOP_MESSAGE_OK, NOW - 10U);
	in.wave_head = 5;
	(void)ring_frame(&in, px);

	zassert_true(px[5].g > RING_BRIGHTNESS, "head brighter than base");
	zassert_true(px[4].g < px[5].g, "tail fades");
	zassert_true(px[12].g < RING_BRIGHTNESS, "far pixels below base");
	zassert_equal(px[6].g, px[12].g, "ahead of the head is floor");

	in.wave_head = 6;
	(void)ring_frame(&in, px);
	zassert_true(px[6].g > RING_BRIGHTNESS, "the head moves");
}

ZTEST(ring, test_bond_does_not_wave)
{
	set_slot(0, PSTOP_SESS_BONDED, PSTOP_MESSAGE_BOND, NOW - 10U);
	in.wave_head = 3;
	(void)ring_frame(&in, px);
	for (int i = 0; i < RING_LEDS; i++) {
		zassert_true(eq(px[i], BLUE), "pixel %d", i);
	}
}

ZTEST(ring, test_mismatch_overrides_everything_but_locate)
{
	set_slot(0, PSTOP_SESS_BONDED, PSTOP_MESSAGE_OK, NOW - 10U);
	in.mismatch = true;
	zassert_equal(ring_frame(&in, px), RING_MISMATCH);
	for (int i = 0; i < RING_LEDS; i++) {
		zassert_true(eq(px[i], PURPLE), "pixel %d", i);
	}

	in.locate = true;
	zassert_equal(ring_frame(&in, px), RING_LOCATE);
}

ZTEST(ring, test_locate_lights_only_led1_rotated)
{
	set_slot(0, PSTOP_SESS_BONDED, PSTOP_MESSAGE_OK, NOW - 10U);
	in.locate = true;

	for (uint8_t off = 0; off < RING_LEDS + 2U; off++) {
		in.offset = off;
		zassert_equal(ring_frame(&in, px), RING_LOCATE);
		for (int i = 0; i < RING_LEDS; i++) {
			bool want = (i == (off % RING_LEDS));

			zassert_true(eq(px[i], want ? LOCATE : OFF), "offset %u pixel %d", off, i);
		}
	}
}

ZTEST(ring, test_rotation_wraps)
{
	char h[RING_LEDS + 1];

	set_slot(0, PSTOP_SESS_BONDED, PSTOP_MESSAGE_STOP, NOW - 10U);
	set_slot(1, PSTOP_SESS_BONDED, PSTOP_MESSAGE_BOND, NOW - 10U);

	in.offset = 3;
	(void)ring_frame(&in, px);
	hues(h);
	zassert_str_equal(h, "BBBRRRRRRRRBBBBB");

	in.offset = 15;
	(void)ring_frame(&in, px);
	hues(h);
	zassert_str_equal(h, "RRRRRRRBBBBBBBBR");
}

ZTEST(ring, test_mismatch_blip_does_not_paint)
{
	struct ring_mismatch m;
	uint64_t t = 1000U;

	ring_mismatch_init(&m, 0U);
	/* One disagreeing tick, then quiet. */
	zassert_false(ring_mismatch_update(&m, 1U, t));
	for (int i = 0; i < 40; i++) {
		t += RING_FRAME_MS;
		zassert_false(ring_mismatch_update(&m, 1U, t), "frame %d", i);
	}
}

ZTEST(ring, test_mismatch_persisting_paints_then_clears)
{
	struct ring_mismatch m;
	uint32_t count = 0U;
	uint64_t t = 1000U;
	uint64_t first_purple = 0U;
	uint64_t cleared = 0U;

	ring_mismatch_init(&m, 0U);

	/* A sustained fault: the counter advances on every 100 ms tick. Frames
	 * every 70 ms see it advance most of the time.
	 */
	for (uint64_t now = t; now < t + 2000U; now += 10U) {
		if ((now % 100U) == 0U) {
			count++;
		}
		if ((now % RING_FRAME_MS) == 0U) {
			if (ring_mismatch_update(&m, count, now) && (first_purple == 0U)) {
				first_purple = now;
			}
		}
	}
	zassert_true(first_purple > 0U, "persistent fault paints");
	zassert_true(first_purple - t >= 500U, "not on the first blips (%llu ms)",
		     (unsigned long long)(first_purple - t));
	zassert_true(first_purple - t <= 1100U, "within ~1 s (%llu ms)",
		     (unsigned long long)(first_purple - t));

	/* Fault ends: purple clears within a few frames. */
	t += 2000U;
	for (uint64_t now = t; now < t + 2000U; now += RING_FRAME_MS) {
		if (!ring_mismatch_update(&m, count, now)) {
			cleared = now;
			break;
		}
	}
	zassert_true(cleared > 0U && (cleared - t) <= RING_MISMATCH_HOLD_MS + RING_FRAME_MS,
		     "clears promptly");
}

ZTEST_SUITE(ring, NULL, NULL, before, NULL, NULL);

/* ------------------------------------------------------------------ */

/* Count lit runs over one cycle and the length of each, in 10 ms steps. */
static int runs(enum status_iface iface, uint8_t octet, bool fault, uint32_t lens[16],
		struct ring_rgb *colour)
{
	uint32_t cycle = status_cycle_ms(iface, octet, fault);
	int n = 0;
	bool was = false;

	for (uint32_t t = 0; t < cycle; t += 10U) {
		struct ring_rgb c = status_pixel(iface, octet, fault, t);
		bool on = !eq(c, OFF);

		if (on) {
			*colour = c;
			if (!was) {
				lens[n++] = 0U;
			}
			lens[n - 1] += 10U;
		}
		was = on;
	}
	return n;
}

ZTEST(status_pixel, test_octet_blinks_digit_by_digit)
{
	uint32_t lens[16];
	struct ring_rgb c;

	/* .109 = 1 short, one long (the 0), 9 short. */
	zassert_equal(runs(STATUS_IFACE_ETHERNET, 109U, false, lens, &c), 11);
	zassert_equal(lens[0], STATUS_ON_MS);
	zassert_equal(lens[1], STATUS_ZERO_MS);
	for (int i = 2; i < 11; i++) {
		zassert_equal(lens[i], STATUS_ON_MS, "blink %d", i);
	}
	zassert_true(c.b > 0U && c.r == 0U && c.g == 0U, "ethernet is blue");

	zassert_equal(runs(STATUS_IFACE_WIFI, 5U, false, lens, &c), 5);
	zassert_true(c.r > 0U && c.r == c.g && c.b == 0U, "wifi is yellow");

	zassert_equal(runs(STATUS_IFACE_WIFI, 20U, false, lens, &c), 3, "2 then a long 0");
	zassert_equal(lens[2], STATUS_ZERO_MS);
}

ZTEST(status_pixel, test_off_without_uplink)
{
	uint32_t lens[16];
	struct ring_rgb c;

	zassert_equal(runs(STATUS_IFACE_NONE, 42U, false, lens, &c), 0);
	zassert_true(status_cycle_ms(STATUS_IFACE_NONE, 0U, false) > 0U, "cycle never zero");
}

ZTEST(status_pixel, test_fault_strobes_red)
{
	uint32_t lens[16];
	struct ring_rgb c;

	zassert_equal(runs(STATUS_IFACE_ETHERNET, 109U, true, lens, &c), 1);
	zassert_equal(lens[0], STATUS_STROBE_MS);
	zassert_true(c.r > 0U && c.g == 0U && c.b == 0U, "red");
	zassert_equal(status_cycle_ms(STATUS_IFACE_NONE, 0U, true), 2U * STATUS_STROBE_MS,
		      "strobes with no uplink too");
}

ZTEST_SUITE(status_pixel, NULL, NULL, NULL, NULL, NULL);
