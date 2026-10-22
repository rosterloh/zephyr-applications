/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Lifetime wear and health counters (upstream's dcs_health.c): stop-switch
 * presses, loop disagreements, cumulative uptime, boots and reflashes, kept
 * in RAM and flushed to settings at most once a minute and at least every
 * ten. The stop switch on upstream's BOM is rated for 100,000 operations;
 * these counters say when a unit is due a new one. Plus a warning board any
 * module can raise a named warning on, surfaced at /api/health.
 *
 * Not a safety function: nothing here influences a verdict or a
 * transmission. It observes ticks the comparator has already decided.
 */

#ifndef PROTECTIVE_STOP_HEALTH_H
#define PROTECTIVE_STOP_HEALTH_H

#include <stdint.h>

#include "health_logic.h"

struct health_snapshot {
	struct health_counters counters; /* live values (may be ahead of flash) */
	int level;                       /* worst active warning */
	uint32_t button_rated_ops;
	uint32_t button_wear_pct;
	uint32_t flushes;     /* successful flushes this boot */
	uint32_t flush_fails; /* failed flushes this boot */
	uint32_t last_flush_age_s;
	struct health_board board;
};

/* Load the counters, count this boot (and a reflash, if the firmware changed)
 * and hook the comparator. After app_settings_init(), before
 * pstop_lockstep_start(). Returns 0 or -errno; a failed load starts from zero.
 */
int health_init(void);

/* Fold in the comparator's edges, refresh the built-in warnings and flush if
 * due. Call every few seconds.
 */
void health_poll(void);

/* Flush now, e.g. just before a controlled restart. Returns 0 or -errno. */
int health_flush(void);

/* Raise (or, with HEALTH_OK, clear) a named warning. */
int health_publish(const char *src, int level, const char *msg);

/* Worst level on the board. */
int health_level(void);

void health_snapshot_get(struct health_snapshot *out);

/* Zero the button counters (presses, mismatch_events) and bump button_swaps,
 * for a switch replacement. Flushes immediately.
 */
int health_reset_button(void);

/* Zero everything (refurbished unit). Flushes immediately. */
int health_reset_all(void);

#endif /* PROTECTIVE_STOP_HEALTH_H */
