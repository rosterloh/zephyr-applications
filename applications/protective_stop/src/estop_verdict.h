/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Pure stop-switch decision core -- no HAL, no I/O, no kernel. Ported from
 * upstream firmware/main/estop_verdict.{c,h}, which was extracted from main.c
 * so the SIL-critical verdict logic is unit-testable to branch + MC/DC. That
 * reasoning holds here too, and gpio_emul additionally lets us test the HAL
 * glue in estop_gpio.c.
 */

#ifndef PROTECTIVE_STOP_ESTOP_VERDICT_H
#define PROTECTIVE_STOP_ESTOP_VERDICT_H

#include <stdbool.h>
#include <stdint.h>

/* Release-direction debounce: after ANY unhealthy read, this many consecutive
 * healthy ticks are required before the channel reports closed again. The
 * open->STOP edge stays SINGLE-TICK -- the stop path is never filtered. This
 * only extends how long a STOP episode lasts, so an EMC-induced blip produces
 * a >=300 ms episode instead of chattering.
 */
#define LOOP_RECLOSE_DEBOUNCE_TICKS 3U

/* Boot warm-up: the comparator sends NOTHING until each channel has settled --
 * either one full closed-debounce cycle, or this many CONSECUTIVE open reads
 * (switch genuinely held at boot, so STOP flows ~500 ms later, well before the
 * bond completes). Without the consecutive-open requirement, a first-sample
 * glitch would put a STOP->OK episode -- the arming gesture -- on the wire at
 * every power-on.
 */
#define LOOP_BOOT_OPEN_CONFIRM_TICKS 5U

/* Per-channel state, carried across ticks. One instance per sampler. */
typedef struct {
	bool high_ok;          /* most recent drive-high tick read IN==1 */
	bool low_ok;           /* most recent drive-low tick read IN==0 */
	bool primed_high;      /* a drive-high sample has been taken since boot */
	bool primed_low;       /* a drive-low sample has been taken since boot */
	uint8_t closed_streak; /* consecutive healthy ticks (release debounce) */
	uint8_t open_streak;   /* consecutive unhealthy ticks (boot warm-up) */
	bool settled;          /* warm-up done; see the two constants above */
} estop_state_t;

/* Zero a channel's state. Fail-safe: a zeroed state reports STOP. */
void estop_state_init(estop_state_t *st);

/* Decide this tick's pstop message byte from two FRESH both-phase reads
 * (rb_hi = readback after driving the loop HIGH, rb_lo = after driving it
 * LOW), updating health, debounce and priming. Returns PSTOP_MESSAGE_OK or
 * PSTOP_MESSAGE_STOP. Pure: no HAL, no globals.
 *
 * core_id selects a DIVERSE expression of the same decision: core 0 by
 * arithmetic image, core 1 by boolean. They are logically identical when
 * correct, so this guards against a common-mode misinterpretation of the
 * physical reads -- the two expressions are unlikely to misread rb_hi/rb_lo
 * the same wrong way. It does NOT mean a fault in either expression reaches
 * the comparator as a mismatch: the STOP-only override below fully
 * determines msg whenever the channel is not actually closed and settled, so
 * a bug that makes this expression emit OK when it should emit STOP is
 * masked toward STOP rather than surfaced as a lockstep divergence. Masking
 * toward STOP is the safe direction; it is just not the same thing as
 * detection.
 */
uint8_t estop_decide(estop_state_t *st, int core_id, int rb_hi, int rb_lo);

/* True once BOTH channels have sampled both phases AND settled. The
 * comparator holds off sending until then.
 */
bool estop_channels_primed(const estop_state_t st[2]);

#endif /* PROTECTIVE_STOP_ESTOP_VERDICT_H */
