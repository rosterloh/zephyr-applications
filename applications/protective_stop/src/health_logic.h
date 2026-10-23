/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Pure logic behind the lifetime health counters: the press / mismatch edge
 * detectors, the persisted blob codec, the wear thresholds, the flush policy
 * and the warning board. No kernel, no I/O, so tests/health unit-tests it on
 * native_sim; health.c holds the locking, persistence and HTTP view.
 *
 * Ported from upstream's dcs_health_logic.c; the blob layout is the same, so
 * a counter dump reads alike on either firmware.
 */

#ifndef PROTECTIVE_STOP_HEALTH_LOGIC_H
#define PROTECTIVE_STOP_HEALTH_LOGIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Persisted counters. Survive reboot and reflash; lost on a flash erase. */
struct health_counters {
	uint32_t presses;         /* both channels OK->STOP rising edges */
	uint32_t mismatch_events; /* same-tick channel disagreement rising edges */
	uint32_t uptime_s;        /* cumulative powered seconds */
	uint32_t boots;           /* every boot, never auto-cleared */
	uint32_t flashes;         /* boots where the firmware identity changed */
	uint32_t otas;            /* boots of an OTA-delivered image */
	uint8_t fw_sha[8];        /* identity of the last firmware seen */
	uint8_t button_swaps;     /* admin button-counter resets (switch replaced) */
};

/* Blob layout v1: ver(1) + 6 x u32 BE (24) + sha(8) + swaps(1) = 34 bytes. */
#define HEALTH_BLOB_VER 1U
#define HEALTH_BLOB_LEN 34U

void health_encode(const struct health_counters *c, uint8_t out[HEALTH_BLOB_LEN]);

/* False on a bad length or version: the caller keeps zeroed counters (a "new
 * unit"), never a hard failure.
 */
bool health_decode(const uint8_t *blob, size_t len, struct health_counters *out);

/* Edge detector over the two lockstep channels' per-tick verdicts, one call
 * per channel per tick. It only evaluates once both channels have reported
 * the SAME tick, and the first aligned observation sets a baseline without
 * counting, so a switch already open at power-on (or the boot-priming STOP)
 * is never counted as a press.
 */
struct health_edge {
	uint8_t seen; /* bit per channel: has reported at least once */
	bool primed;
	bool stop_both;
	bool mismatch;
	uint32_t tick[2];
	uint8_t verdict[2];
};

struct health_edge_out {
	bool press;    /* both-STOP rising edge this call */
	bool mismatch; /* same-tick disagreement rising edge this call */
};

/* stop_code is the STOP verdict (PSTOP_MESSAGE_STOP); anything else is "not
 * STOP".
 */
struct health_edge_out health_edge_step(struct health_edge *st, int channel, uint32_t tick,
					uint8_t verdict, uint8_t stop_code);

/* Wear levels: 0 ok, 1 warn, 2 critical. */
#define HEALTH_OK   0
#define HEALTH_WARN 1
#define HEALTH_CRIT 2

/* Percent of rated operations consumed; warn at warn_pct, critical at 100.
 * pct_out (optional) receives the clamped 0..999 percentage. rated_ops 0
 * disables the rating.
 */
int health_button_level(uint32_t presses, uint32_t rated_ops, uint32_t warn_pct, uint32_t *pct_out);

/* Warn once mismatch_events >= min_events AND the rate exceeds per_1000 per
 * 1000 presses (presses floored at 1000, so a new unit with a handful of
 * presses cannot trip it).
 */
int health_mismatch_level(uint32_t mismatch_events, uint32_t presses, uint32_t min_events,
			  uint32_t per_1000);

/* Flush when dirty and at least min_interval_s since the last one, and always
 * after max_interval_s. A max below min is stretched up to min.
 */
bool health_flush_due(bool dirty, uint32_t since_last_flush_s, uint32_t min_interval_s,
		      uint32_t max_interval_s);

/* The warning board: one entry per source, upserted. */
#define HEALTH_MAX_WARNINGS 8
#define HEALTH_SRC_LEN      16 /* incl. NUL */
#define HEALTH_MSG_LEN      64 /* incl. NUL */

struct health_warning {
	char src[HEALTH_SRC_LEN];
	char msg[HEALTH_MSG_LEN];
	int level;
	uint32_t count;    /* publishes since first raised */
	uint64_t first_ms; /* uptime when first raised */
	uint64_t last_ms;  /* uptime of the latest publish */
};

struct health_board {
	int n;
	uint32_t dropped; /* publishes refused: table full */
	struct health_warning w[HEALTH_MAX_WARNINGS];
};

/* Raise or update src's warning; level HEALTH_OK clears it. Returns 0, or
 * -ENOMEM when the table is full (counted in dropped).
 */
int health_board_publish(struct health_board *b, const char *src, int level, const char *msg,
			 uint64_t now_ms);
void health_board_clear(struct health_board *b, const char *src);

/* Worst level on the board. */
int health_board_level(const struct health_board *b);

#endif /* PROTECTIVE_STOP_HEALTH_LOGIC_H */
