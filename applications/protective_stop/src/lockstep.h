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
 *
 * Completion is tracked by a per-tick GENERATION STAMP, not by semaphore
 * counting. A counting semaphore cannot carry which tick a completion belongs
 * to: if a sampler misses its deadline, the comparator moves on, but the
 * sampler still finishes and gives its "done" semaphore -- crediting the
 * NEXT tick's wait with a completion nothing produced this tick's frame for.
 * On a uniprocessor this self-heals (the late sampler always finishes before
 * the comparator's own take, because it runs at a higher priority and never
 * blocks), which is exactly why it must not be trusted: under CONFIG_SMP
 * those incidental properties fail, and a stale frame could reach the wire.
 */

#ifndef PROTECTIVE_STOP_LOCKSTEP_H
#define PROTECTIVE_STOP_LOCKSTEP_H

#include <stdbool.h>
#include <stdint.h>

#include "session.h"

/* Stop-switch sampling cadence. A safety property: it never slows down, even
 * when the machine asks for a slower heartbeat.
 */
#define PSTOP_TICK_MS 100U

/* BOTH samplers must publish within this long of the tick, leaving the
 * comparator slack to drain replies and re-align. It is one absolute deadline
 * shared by both waits, not one per sampler: two sequential 80 ms waits would
 * let the second sampler publish up to 160 ms late and still transmit, and
 * would blame a both-late tick on the first sampler alone. A tick where either
 * sampler lands outside the window is tainted and sends nothing, counted
 * separately from a mismatch -- see pstop_lockstep_missed_deadlines().
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

/* Lifetime count of ticks where transmission was held back because the
 * stop-switch channels have not both primed yet.
 */
uint32_t pstop_lockstep_gated_ticks(void);

/* Lifetime count of ticks where a sampler missed its generation and was never
 * compared -- distinct from mismatches, which counts only a genuine
 * byte-comparison disagreement between the two encodings.
 */
uint32_t pstop_lockstep_missed_deadlines(void);

/* Read a slot's session, or NULL if the slot is out of range. */
const struct pstop_session *pstop_lockstep_session(int slot);

/* Ask the comparator to re-read a slot's peer from settings at the start of
 * its next tick: a changed slot re-opens and bonds afresh, a cleared one goes
 * idle. This is how the HTTP API applies peer changes live. Safe from any
 * thread. Returns 0, or -EINVAL for a slot out of range.
 */
int pstop_lockstep_request_reconfigure(int slot);

/* True if a slot has an open session (configured and bound). */
bool pstop_lockstep_slot_active(int slot);

/* A channel's latest loop-health reads: the drive-high phase read back high,
 * the drive-low phase read back low. Both true = the loop is closed. Upstream
 * reports these as e_hi<c>/e_lo<c>.
 */
void pstop_lockstep_loop_health(int channel, bool *high_ok, bool *low_ok);

/* A channel's verdict on the most recent tick (PSTOP_MESSAGE_OK or _STOP). */
uint8_t pstop_lockstep_last_verdict(int channel);

/* Ask the comparator to re-bond a slot at the start of its next tick. The only
 * way out of PSTOP_SESS_REJECTED short of a reboot. Safe from any thread: it
 * only sets a flag the comparator consumes. Returns 0, or -EINVAL for a slot
 * out of range.
 */
int pstop_lockstep_request_rebond(int slot);

/* What the comparator knew about its most recent faults. The two counters say
 * how often; this says which side and when, so a late sampler can be told
 * apart from two samplers that disagreed (upstream's pstop_mm_last).
 */
/* Which fault the diag record's most recent event was. */
enum pstop_lockstep_event {
	PSTOP_LOCKSTEP_EVENT_NONE = 0,
	PSTOP_LOCKSTEP_EVENT_TIMEOUT = 1, /* a sampler missed the window */
	PSTOP_LOCKSTEP_EVENT_CONTENT = 2, /* both published, frames differed */
};

struct pstop_lockstep_diag {
	/* Whichever of the two records below was written last. */
	uint8_t last_kind; /* enum pstop_lockstep_event */

	/* Last tick tainted by a sampler outside the publish window. */
	uint8_t late_mask;       /* bit c set = sampler c was late */
	uint32_t late_by_ms;     /* worst lateness past the deadline, if known */
	uint64_t late_uptime_ms; /* valid once missed_deadlines > 0 */

	/* Last tick where the two encodings disagreed. */
	uint8_t mismatch_slot;
	uint8_t mismatch_offset;     /* first differing byte in the 48-byte frame */
	uint8_t mismatch_verdict[2]; /* each sampler's verdict on that tick */
	uint64_t mismatch_uptime_ms; /* valid once mismatches > 0 */

	/* Worst observed notify-to-publish latency per sampler. */
	uint32_t latency_max_ms[2];
};

/* Copy out the comparator's fault attribution. Safe from any thread. */
void pstop_lockstep_diag_get(struct pstop_lockstep_diag *out);

#ifdef CONFIG_ZTEST
/* Test-only: white-box access to the generation-stamp completion check.
 *
 * A sampler's k_sem_give() landing a tick late must not be mistaken for
 * "this tick's frame is current" -- that stale-completion defect is invisible
 * to any thread-timing test (it self-heals on a uniprocessor and only
 * manifests under CONFIG_SMP), so it is pinned here as pure state instead:
 * manufacture a stale generation directly and assert the comparator refuses
 * to publish it.
 */
void pstop_lockstep_test_set_sampler_gen(int channel, uint32_t gen);
uint32_t pstop_lockstep_test_tick_gen(void);
bool pstop_lockstep_test_samplers_published(void);

/* Test-only: run one synchronous tick as if the samplers in late_mask had
 * published only after the window closed. Their frames are current, so only
 * the window check can refuse them.
 */
void pstop_lockstep_test_tick_late(uint64_t now_ms, uint8_t late_mask);

/* Test-only: called between the two samplers of a synchronous tick, to land a
 * change (a role flip) exactly where it would split their encodings.
 */
extern void (*pstop_lockstep_test_between_samplers)(void);
#endif /* CONFIG_ZTEST */

#endif /* PROTECTIVE_STOP_LOCKSTEP_H */
