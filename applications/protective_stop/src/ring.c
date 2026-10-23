/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "pstop/pstop_msg.h"
#include "ring.h"
#include "session.h"

/* White lights all three channels, so it reads ~3x brighter than the
 * single-channel state colours: halve it.
 */
#define WHITE_LEVEL (RING_BRIGHTNESS / 2)

/* Amber is full red plus this much green (r > g), distinct from yellow. */
#define AMBER_GREEN_PCT 40U

/* Liveness wave: percent of the base colour by distance from the head. The
 * rest of the segment sits below base so the motion reads at a glance.
 */
static const uint8_t wave_pct[] = {170U, 135U, 105U, 80U, 65U};
#define WAVE_FLOOR_PCT 55U

static struct ring_rgb rgb(uint8_t r, uint8_t g, uint8_t b)
{
	return (struct ring_rgb){.r = r, .g = g, .b = b};
}

static uint8_t scale(uint8_t v, uint32_t pct)
{
	uint32_t s = ((uint32_t)v * pct) / 100U;

	return (uint8_t)((s > 255U) ? 255U : s);
}

/* On or off for an n-blink amber group at time now. */
static bool amber_on(uint64_t now_ms, uint32_t n)
{
	uint32_t group = RING_AMBER_ON_MS + RING_AMBER_OFF_MS;
	uint32_t period = (n * group) + RING_AMBER_REST_MS;
	uint32_t phase = (uint32_t)(now_ms % period);

	if (phase >= (n * group)) {
		return false;
	}
	return (phase % group) < RING_AMBER_ON_MS;
}

static void fill(struct ring_rgb logical[RING_LEDS], struct ring_rgb c)
{
	for (int i = 0; i < RING_LEDS; i++) {
		logical[i] = c;
	}
}

static enum ring_state compose(const struct ring_input *in, struct ring_rgb logical[RING_LEDS])
{
	int configured[PSTOP_MAX_MACHINES];
	int n = 0;
	enum ring_state worst = RING_IDLE;

	fill(logical, rgb(0, 0, 0));

	if (in->locate) {
		logical[0] = rgb(RING_BRIGHTNESS, RING_BRIGHTNESS, RING_BRIGHTNESS);
		return RING_LOCATE;
	}

	/* A sampler disagreement silences every session, so it is a device
	 * fault and owns the whole ring.
	 */
	if (in->mismatch) {
		fill(logical, rgb(RING_BRIGHTNESS, 0, RING_BRIGHTNESS));
		return RING_MISMATCH;
	}

	for (int slot = 0; slot < PSTOP_MAX_MACHINES; slot++) {
		if (in->slots[slot].configured) {
			configured[n++] = slot;
		}
	}

	if (n == 0) {
		fill(logical, rgb(WHITE_LEVEL, WHITE_LEVEL, WHITE_LEVEL));
		return RING_IDLE;
	}

	uint8_t amber = amber_on(in->now_ms, in->uplink_up ? 1U : 3U) ? RING_BRIGHTNESS : 0U;

	for (int j = 0; j < n; j++) {
		const struct ring_slot *s = &in->slots[configured[j]];
		bool fresh = (s->state == PSTOP_SESS_BONDED) && (s->last_reply_ms > 0U) &&
			     (in->now_ms >= s->last_reply_ms) &&
			     ((in->now_ms - s->last_reply_ms) < RING_LINK_FRESH_MS);
		struct ring_rgb c;
		enum ring_state st;
		bool wave = false;

		if (!fresh) {
			st = RING_UNREACHABLE;
			c = rgb(amber, scale(amber, AMBER_GREEN_PCT), 0);
		} else if (s->last_msg == PSTOP_MESSAGE_STOP) {
			st = RING_STOP;
			c = rgb(RING_BRIGHTNESS, 0, 0);
			wave = true;
		} else if (s->last_msg == PSTOP_MESSAGE_OK) {
			st = RING_OK;
			c = rgb(0, RING_BRIGHTNESS, 0);
			wave = true;
		} else {
			st = RING_BOND;
			c = rgb(0, 0, RING_BRIGHTNESS);
		}
		if (st > worst) {
			worst = st;
		}

		/* Even division in slot order; with 3 slots the segments are
		 * 5, 5 and 6 pixels.
		 */
		int start = (j * RING_LEDS) / n;
		int end = ((j + 1) * RING_LEDS) / n;

		for (int p = start; p < end; p++) {
			if (wave) {
				/* One wave chases the whole ring, so several
				 * segments share one coherent motion.
				 */
				int d = (((int)in->wave_head - p) + RING_LEDS) % RING_LEDS;
				uint32_t pct =
					(d < (int)sizeof(wave_pct)) ? wave_pct[d] : WAVE_FLOOR_PCT;

				logical[p] = rgb(scale(c.r, pct), scale(c.g, pct), scale(c.b, pct));
			} else {
				logical[p] = c;
			}
		}
	}

	return worst;
}

enum ring_state ring_frame(const struct ring_input *in, struct ring_rgb out[RING_LEDS])
{
	struct ring_rgb logical[RING_LEDS];
	enum ring_state st = compose(in, logical);

	/* Physical pixel (logical + offset) mod 16, in WS2812 data order. */
	for (int i = 0; i < RING_LEDS; i++) {
		out[(i + (in->offset % RING_LEDS)) % RING_LEDS] = logical[i];
	}
	return st;
}

void ring_mismatch_init(struct ring_mismatch *m, uint32_t count)
{
	(void)memset(m, 0, sizeof(*m));
	m->last_count = count;
}

bool ring_mismatch_update(struct ring_mismatch *m, uint32_t count, uint64_t now_ms)
{
	if (count != m->last_count) {
		m->last_count = count;
		m->advanced = true;
		if (m->streak >= RING_MISMATCH_PERSIST_FRAMES) {
			m->until_ms = now_ms + RING_MISMATCH_HOLD_MS;
		}
	}

	if ((now_ms - m->window_start_ms) >= RING_MISMATCH_WINDOW_MS) {
		if (m->advanced) {
			if (m->streak < RING_MISMATCH_PERSIST_FRAMES) {
				m->streak++;
			}
			if (m->streak >= RING_MISMATCH_PERSIST_FRAMES) {
				m->until_ms = now_ms + RING_MISMATCH_HOLD_MS;
			}
		} else {
			m->streak = 0U;
		}
		m->advanced = false;
		m->window_start_ms = now_ms;
	}

	return now_ms < m->until_ms;
}

static struct ring_rgb iface_colour(enum status_iface iface)
{
	switch (iface) {
	case STATUS_IFACE_ETHERNET:
		return rgb(0, 0, STATUS_BRIGHTNESS);
	case STATUS_IFACE_WIFI:
		return rgb(STATUS_BRIGHTNESS, STATUS_BRIGHTNESS, 0);
	default:
		return rgb(0, 0, 0);
	}
}

/* Walk one cycle of the pattern. With t_ms == UINT32_MAX it only measures
 * the cycle; otherwise it stops at t_ms and reports whether the LED is lit.
 * One walker for both keeps the length and the pattern from drifting apart.
 */
static uint32_t walk(enum status_iface iface, uint8_t octet, bool fault, uint32_t t_ms, bool *lit)
{
	uint32_t at = 0U;
	uint8_t digits[3];
	int nd = 0;

	*lit = false;

#define STEP(on, ms)                                                                               \
	do {                                                                                       \
		if ((t_ms != UINT32_MAX) && (t_ms < (at + (ms)))) {                                \
			*lit = (on);                                                               \
			return at;                                                                 \
		}                                                                                  \
		at += (ms);                                                                        \
	} while (false)

	if (fault) {
		STEP(true, STATUS_STROBE_MS);
		STEP(false, STATUS_STROBE_MS);
		return at;
	}

	if (iface == STATUS_IFACE_NONE) {
		STEP(false, 1000U);
		return at;
	}

	if (octet >= 100U) {
		digits[nd++] = octet / 100U;
	}
	if (octet >= 10U) {
		digits[nd++] = (octet / 10U) % 10U;
	}
	digits[nd++] = octet % 10U;

	for (int d = 0; d < nd; d++) {
		if (d > 0) {
			STEP(false, STATUS_DIGIT_GAP_MS);
		}
		if (digits[d] == 0U) {
			STEP(true, STATUS_ZERO_MS);
		} else {
			for (uint8_t b = 0; b < digits[d]; b++) {
				if (b > 0U) {
					STEP(false, STATUS_OFF_MS);
				}
				STEP(true, STATUS_ON_MS);
			}
		}
	}
	STEP(false, STATUS_CYCLE_GAP_MS);

#undef STEP
	return at;
}

uint32_t status_cycle_ms(enum status_iface iface, uint8_t octet, bool fault)
{
	bool lit;

	return walk(iface, octet, fault, UINT32_MAX, &lit);
}

struct ring_rgb status_pixel(enum status_iface iface, uint8_t octet, bool fault, uint32_t t_ms)
{
	bool lit;

	(void)walk(iface, octet, fault, t_ms, &lit);
	if (!lit) {
		return rgb(0, 0, 0);
	}
	return fault ? rgb(STATUS_BRIGHTNESS, 0, 0) : iface_colour(iface);
}
