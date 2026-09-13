/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Lockstep comparator: two samplers independently read one channel each and
 * encode a frame per machine slot; nothing is transmitted unless the two
 * encodings are byte-identical.
 *
 * Comparing ENCODED BYTES rather than verdicts is the point. One memcmp
 * covers the switch reading, the counter, the timestamp, the receiver id and
 * both message buffers -- and the CRC sits inside the compared region, so a
 * fault in CRC computation is caught too.
 *
 * Threads, not pinned cores: the safety property is two independent samplers
 * whose encodings must agree, which threads satisfy. This maps onto CONFIG_SMP
 * pinned cores on hardware with no change to the comparator.
 */

#ifndef PROTECTIVE_STOP_LOCKSTEP_H
#define PROTECTIVE_STOP_LOCKSTEP_H

#include <stdint.h>

#include "session.h"

/* Stop-switch sampling cadence. A safety property: it never slows down, even
 * when the machine asks for a slower heartbeat.
 */
#define PSTOP_TICK_MS 100U

/* Each sampler must publish within this long of the tick, leaving the
 * comparator slack to drain replies and re-align. A missed deadline is
 * treated as a mismatch.
 */
#define PSTOP_SAMPLER_DEADLINE_MS 80U

/* Open a session for every configured peer slot. Returns 0 or -errno. */
int pstop_lockstep_init(void);

/* Run exactly one tick synchronously on the calling thread: sample both
 * channels, build, compare, transmit, poll, run the watchdogs. Exposed so
 * tests can drive the comparator deterministically.
 */
void pstop_lockstep_tick(uint64_t now_ms);

/* Spawn the two sampler threads and the comparator thread. */
int pstop_lockstep_start(void);

/* Lifetime count of ticks where the two encodings disagreed. */
uint32_t pstop_lockstep_mismatches(void);

/* Read a slot's session, or NULL if the slot is out of range. */
const struct pstop_session *pstop_lockstep_session(int slot);

#endif /* PROTECTIVE_STOP_LOCKSTEP_H */
