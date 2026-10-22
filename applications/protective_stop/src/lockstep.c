/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include "app_settings.h"
#include "estop_gpio.h"
#include "estop_verdict.h"
#include "lockstep.h"
#include "pstop/pstop_msg.h"
#include "session.h"

LOG_MODULE_REGISTER(lockstep, LOG_LEVEL_INF);

#define SAMPLER_STACK_SIZE    2048
#define COMPARATOR_STACK_SIZE 4096
#define SAMPLER_PRIORITY      4
#define COMPARATOR_PRIORITY   5

static struct pstop_session sessions[PSTOP_MAX_MACHINES];
static bool slot_active[PSTOP_MAX_MACHINES];

static estop_state_t estop[ESTOP_CHANNELS];

/* Per-sampler encoded frames: [channel][slot][48]. The comparator memcmps
 * frames[0][slot] against frames[1][slot].
 */
static uint8_t frames[ESTOP_CHANNELS][PSTOP_MAX_MACHINES][PSTOP_MESSAGE_SIZE];

/* The comparator snapshots these BEFORE waking the samplers, so both samplers
 * encode the same timestamp and the same due-set. Without that the two
 * encodings differ in the stamp field and every tick looks like a mismatch.
 */
static uint64_t tick_now_ms;
static bool tick_due[PSTOP_MAX_MACHINES];

/* The role, snapshotted once per tick with the rest. The role can change at
 * runtime (and will over HTTP in slice 3); if each sampler read settings
 * itself, a change landing between their builds would split the encodings.
 */
static pstop_aux_role_t tick_role;

/* Each sampler's verdict this tick, for mismatch attribution. */
static uint8_t sampler_verdict[ESTOP_CHANNELS];

/* Uptime at which each sampler last published, for latency attribution. */
static atomic_t sampler_done_ms[ESTOP_CHANNELS];

/* Rebond and reconfigure requests from other threads, one bit per slot,
 * consumed by begin_tick().
 */
static atomic_t rebond_requests;
static atomic_t reconfigure_requests;

static struct pstop_lockstep_diag diag;
static struct k_spinlock diag_lock;

#ifdef CONFIG_ZTEST
void (*pstop_lockstep_test_between_samplers)(void);
#endif

/* Which tick generation is in flight, and which generation each sampler last
 * completed. A counting semaphore cannot carry this: see the header comment
 * for why a stale completion must be caught by generation, not by counting.
 * atomic_t is load-bearing under CONFIG_SMP -- a plain store could be
 * reordered ahead of the frame writes in sampler_pass(), reintroducing the
 * torn-buffer read this stamp exists to prevent.
 */
static atomic_t tick_gen;
static atomic_t sampler_gen[ESTOP_CHANNELS];

static uint32_t mismatches;

/* Ticks where a sampler missed its generation and was never compared -- a
 * distinct fault from a genuine byte-comparison disagreement, and must stay
 * distinct so neither counter is muddied with the other's cause.
 */
static uint32_t missed_deadlines;

/* Ticks where the comparator was gated by estop_channels_primed() rather than
 * actually comparing anything -- a stuck-open or chattering channel can hold
 * this indefinitely, which otherwise looks identical to "nothing to report".
 */
static uint32_t gated_ticks;

static K_SEM_DEFINE(go_sem_0, 0, 1);
static K_SEM_DEFINE(go_sem_1, 0, 1);
static K_SEM_DEFINE(done_sem_0, 0, 1);
static K_SEM_DEFINE(done_sem_1, 0, 1);

static struct k_sem *const go_sem[ESTOP_CHANNELS] = {&go_sem_0, &go_sem_1};
static struct k_sem *const done_sem[ESTOP_CHANNELS] = {&done_sem_0, &done_sem_1};

/* (Re)build one slot's session from its settings: close whatever is open,
 * then open afresh if the slot is configured. A reconfigured slot starts from
 * IDLE with zeroed counters, bonding to the new machine on its next tick.
 * Runs only on the comparator's thread (or before it starts), so the sampler
 * builds never see a session change under them.
 */
static bool slot_apply_config(int slot)
{
	struct pstop_peer peer;
	int ret;

	if (slot_active[slot]) {
		pstop_session_close(&sessions[slot]);
	}
	slot_active[slot] = false;
	sessions[slot].sock = -1;

	if ((app_settings_peer_get(slot, &peer) != 0) || !peer.configured) {
		return false;
	}

	ret = pstop_session_open(&sessions[slot], slot, &peer);
	if (ret != 0) {
		LOG_ERR("slot %d open failed: %d", slot, ret);
		return false;
	}

	slot_active[slot] = true;
	return true;
}

int pstop_lockstep_init(void)
{
	int opened = 0;

	for (int c = 0; c < ESTOP_CHANNELS; c++) {
		estop_state_init(&estop[c]);
	}

	for (int slot = 0; slot < PSTOP_MAX_MACHINES; slot++) {
		slot_active[slot] = false;
		if (slot_apply_config(slot)) {
			opened++;
		}
	}

	if (opened == 0) {
		/* Total muteness: the most complete form of the failure this
		 * design exists to make visible, and counted by neither
		 * mismatches nor gated_ticks. An unprovisioned remote booting
		 * is legitimate (slice 3's HTTP layer is how it gets
		 * configured), so this stays a warning, not a return failure.
		 */
		LOG_WRN("lockstep ready: 0 session(s) -- nothing is configured, nothing will "
			"transmit");
	} else {
		LOG_INF("lockstep ready: %d session(s)", opened);
	}
	return 0;
}

/* Sample one channel and encode every due slot's frame into that channel's
 * buffer. Runs on the sampler thread, or inline from pstop_lockstep_tick().
 * Writes sampler_gen[channel] LAST, so that under CONFIG_SMP no reader can
 * observe the generation stamp before the frame writes it guards.
 */
static void sampler_pass(int channel, uint32_t gen)
{
	uint8_t verdict = estop_channel_sample(channel, &estop[channel]);

	sampler_verdict[channel] = verdict;

	for (int slot = 0; slot < PSTOP_MAX_MACHINES; slot++) {
		if (slot_active[slot] && tick_due[slot]) {
			pstop_session_build(&sessions[slot], verdict, tick_role, tick_now_ms,
					    frames[channel][slot]);
		}
	}

	atomic_set(&sampler_done_ms[channel], (atomic_val_t)k_uptime_get_32());
	atomic_set(&sampler_gen[channel], (atomic_val_t)gen);
}

/* True iff every sampler's last completion belongs to generation `gen`. A
 * sampler stuck on a stale (older) generation means its frame was not
 * produced for this tick, so it must not be trusted for comparison.
 */
static bool samplers_published(uint32_t gen)
{
	for (int c = 0; c < ESTOP_CHANNELS; c++) {
		if ((uint32_t)atomic_get(&sampler_gen[c]) != gen) {
			return false;
		}
	}
	return true;
}

/* Attribute a content mismatch: which slot, where the frames first differ, and
 * what each sampler read. The scan is bounded by the memcmp that already
 * failed, so it always finds a byte.
 */
static void record_mismatch(int slot)
{
	k_spinlock_key_t key;
	uint8_t offset = 0U;

	while ((offset < PSTOP_MESSAGE_SIZE) &&
	       (frames[0][slot][offset] == frames[1][slot][offset])) {
		offset++;
	}

	key = k_spin_lock(&diag_lock);
	diag.mismatch_slot = (uint8_t)slot;
	diag.mismatch_offset = offset;
	diag.mismatch_verdict[0] = sampler_verdict[0];
	diag.mismatch_verdict[1] = sampler_verdict[1];
	diag.mismatch_uptime_ms = (uint64_t)k_uptime_get();
	diag.last_kind = PSTOP_LOCKSTEP_EVENT_CONTENT;
	k_spin_unlock(&diag_lock, key);

	LOG_WRN("lockstep mismatch: slot %d byte %u, verdicts %02x/%02x", slot, offset,
		sampler_verdict[0], sampler_verdict[1]);
}

/* Attribute a tainted tick to the sampler(s) that missed the window. */
static void record_late(uint8_t late_mask, uint32_t late_by_ms)
{
	k_spinlock_key_t key = k_spin_lock(&diag_lock);

	diag.late_mask = late_mask;
	diag.late_by_ms = late_by_ms;
	diag.late_uptime_ms = (uint64_t)k_uptime_get();
	diag.last_kind = PSTOP_LOCKSTEP_EVENT_TIMEOUT;
	k_spin_unlock(&diag_lock, key);
}

/* Fold one sampler's notify-to-publish latency into the running worst. */
static void record_latency(int channel, uint32_t latency_ms)
{
	k_spinlock_key_t key = k_spin_lock(&diag_lock);

	if (latency_ms > diag.latency_max_ms[channel]) {
		diag.latency_max_ms[channel] = latency_ms;
	}
	k_spin_unlock(&diag_lock, key);
}

/* Compare every due slot's pair of encodings, then transmit only if ALL of
 * them agreed. A fault that reached one slot's buffer had no respect for slot
 * boundaries, so a single mismatch silences every session at once -- and
 * which slots transmit must not depend on loop order, so nothing is sent
 * until every slot has been checked.
 */
static void comparator_pass(void)
{
	bool all_agreed = true;

	/* Hold everything back until both channels have settled. The
	 * boot-priming STOP must never reach a machine: a machine reads
	 * STOP->OK as the arming gesture and would arm with no operator action.
	 */
	if (!estop_channels_primed(estop)) {
		gated_ticks++;
		return;
	}

	for (int slot = 0; slot < PSTOP_MAX_MACHINES; slot++) {
		if (!slot_active[slot] || !tick_due[slot]) {
			continue;
		}
		if (memcmp(frames[0][slot], frames[1][slot], PSTOP_MESSAGE_SIZE) != 0) {
			all_agreed = false;
			record_mismatch(slot);
			break;
		}
	}

	if (!all_agreed) {
		/* Silence is the safe action: every bonded machine
		 * heartbeat-times-out and stops.
		 */
		mismatches++;
		return;
	}

	for (int slot = 0; slot < PSTOP_MAX_MACHINES; slot++) {
		if (slot_active[slot] && tick_due[slot]) {
			(void)pstop_session_commit_send(&sessions[slot], frames[0][slot],
							tick_now_ms);
		}
	}
}

/* Snapshot this tick's shared inputs and mint its generation. Shared by the
 * synchronous and threaded paths so they cannot silently diverge again.
 */
static uint32_t begin_tick(uint64_t now_ms)
{
	uint32_t gen;

	/* Snapshot before minting the generation: atomic_add() is the release
	 * that publishes this snapshot, so a sampler that observes the new
	 * generation is guaranteed to see these writes, not a stale snapshot
	 * from the prior tick.
	 */
	atomic_val_t reconfigure = atomic_clear(&reconfigure_requests);
	atomic_val_t rebond = atomic_clear(&rebond_requests);

	for (int slot = 0; slot < PSTOP_MAX_MACHINES; slot++) {
		if ((reconfigure & BIT(slot)) != 0) {
			LOG_INF("slot %d reconfigured: %s", slot,
				slot_apply_config(slot) ? "active" : "idle");
		}
	}

	for (int slot = 0; slot < PSTOP_MAX_MACHINES; slot++) {
		if (slot_active[slot] && ((rebond & BIT(slot)) != 0)) {
			LOG_INF("slot %d rebond requested (state %d)", slot, sessions[slot].state);
			pstop_session_rebond(&sessions[slot]);
		}
	}

	tick_now_ms = now_ms;
	tick_role = app_settings_is_operator() ? PSTOP_AUX_ROLE_OPERATOR : PSTOP_AUX_ROLE_STOP_ONLY;
	for (int slot = 0; slot < PSTOP_MAX_MACHINES; slot++) {
		tick_due[slot] = slot_active[slot] && pstop_session_due(&sessions[slot], now_ms);
	}

	gen = (uint32_t)atomic_add(&tick_gen, 1) + 1U;
	return gen;
}

/* Compare-or-decline, then poll and watchdog every active session. Shared by
 * the synchronous and threaded paths: the only difference between them is how
 * the samplers ran, not what happens once they have (or have not).
 *
 * late_mask names the samplers that did not publish inside the window. A late
 * frame can still carry the current generation, so the window and the
 * generation are both required: the generation proves which tick a frame is
 * for, the window proves it was fresh enough to send.
 */
static void run_tick_body(uint64_t now_ms, uint32_t gen, uint8_t late_mask, uint32_t late_by_ms)
{
	for (int c = 0; c < ESTOP_CHANNELS; c++) {
		if ((uint32_t)atomic_get(&sampler_gen[c]) != gen) {
			late_mask |= BIT(c);
		}
	}

	if ((late_mask == 0U) && samplers_published(gen)) {
		comparator_pass();
	} else {
		LOG_WRN("sampler(s) %#x missed tick %u", late_mask, gen);
		missed_deadlines++;
		record_late(late_mask, late_by_ms);
	}

	for (int slot = 0; slot < PSTOP_MAX_MACHINES; slot++) {
		if (slot_active[slot]) {
			pstop_session_poll(&sessions[slot], now_ms);
			pstop_session_watchdog(&sessions[slot], now_ms);
		}
	}
}

static void tick_sync(uint64_t now_ms, uint8_t late_mask)
{
	uint32_t gen = begin_tick(now_ms);

	for (int c = 0; c < ESTOP_CHANNELS; c++) {
		sampler_pass(c, gen);
#ifdef CONFIG_ZTEST
		if ((c == 0) && (pstop_lockstep_test_between_samplers != NULL)) {
			pstop_lockstep_test_between_samplers();
		}
#endif
	}

	run_tick_body(now_ms, gen, late_mask, 0U);
}

void pstop_lockstep_tick(uint64_t now_ms)
{
	tick_sync(now_ms, 0U);
}

uint32_t pstop_lockstep_mismatches(void)
{
	return mismatches;
}

uint32_t pstop_lockstep_gated_ticks(void)
{
	return gated_ticks;
}

uint32_t pstop_lockstep_missed_deadlines(void)
{
	return missed_deadlines;
}

const struct pstop_session *pstop_lockstep_session(int slot)
{
	if ((slot < 0) || (slot >= PSTOP_MAX_MACHINES)) {
		return NULL;
	}
	return &sessions[slot];
}

int pstop_lockstep_request_reconfigure(int slot)
{
	if ((slot < 0) || (slot >= PSTOP_MAX_MACHINES)) {
		return -EINVAL;
	}
	(void)atomic_or(&reconfigure_requests, (atomic_val_t)BIT(slot));
	return 0;
}

bool pstop_lockstep_slot_active(int slot)
{
	return (slot >= 0) && (slot < PSTOP_MAX_MACHINES) && slot_active[slot];
}

void pstop_lockstep_loop_health(int channel, bool *high_ok, bool *low_ok)
{
	if ((channel < 0) || (channel >= ESTOP_CHANNELS)) {
		*high_ok = false;
		*low_ok = false;
		return;
	}
	*high_ok = estop[channel].high_ok;
	*low_ok = estop[channel].low_ok;
}

uint8_t pstop_lockstep_last_verdict(int channel)
{
	if ((channel < 0) || (channel >= ESTOP_CHANNELS)) {
		return PSTOP_MESSAGE_STOP;
	}
	return sampler_verdict[channel];
}

int pstop_lockstep_request_rebond(int slot)
{
	if ((slot < 0) || (slot >= PSTOP_MAX_MACHINES)) {
		return -EINVAL;
	}
	(void)atomic_or(&rebond_requests, (atomic_val_t)BIT(slot));
	return 0;
}

void pstop_lockstep_diag_get(struct pstop_lockstep_diag *out)
{
	k_spinlock_key_t key = k_spin_lock(&diag_lock);

	*out = diag;
	k_spin_unlock(&diag_lock, key);
}

static void sampler_thread(void *p1, void *p2, void *p3)
{
	int channel = (int)(intptr_t)p1;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (;;) {
		k_sem_take(go_sem[channel], K_FOREVER);
		/* The generation was minted by begin_tick() before this thread
		 * was woken, so it is already current.
		 */
		sampler_pass(channel, (uint32_t)atomic_get(&tick_gen));
		k_sem_give(done_sem[channel]);
	}
}

static void comparator_thread(void *p1, void *p2, void *p3)
{
	int64_t next = k_uptime_get();

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (;;) {
		uint64_t now_ms;
		uint32_t gen;
		int64_t deadline;
		uint8_t late_mask;
		uint32_t late_by_ms;

		next += PSTOP_TICK_MS;

		/* Snapshot the tick's shared inputs and mint its generation
		 * BEFORE waking the samplers, so both encode identical stamps
		 * and the same due-set, and so a late completion from a prior
		 * tick can never be mistaken for this one's.
		 */
		now_ms = (uint64_t)k_uptime_get();
		gen = begin_tick(now_ms);
		deadline = (int64_t)now_ms + PSTOP_SAMPLER_DEADLINE_MS;
		late_mask = 0U;
		late_by_ms = 0U;

		/* Drop a give left over from a sampler that finished after the
		 * previous tick's window, so it cannot satisfy this tick's take.
		 */
		for (int c = 0; c < ESTOP_CHANNELS; c++) {
			k_sem_reset(done_sem[c]);
		}

		for (int c = 0; c < ESTOP_CHANNELS; c++) {
			k_sem_give(go_sem[c]);
		}

		/* One absolute deadline for both takes: the second sampler
		 * gets whatever the first left, never a fresh 80 ms. Both
		 * takes always run, so a give that is already pending is
		 * consumed even when the window has closed. A timed-out take
		 * taints the tick even if the sampler publishes before
		 * run_tick_body() looks: a frame that missed the window is not
		 * fresh, whatever its generation says.
		 */
		for (int c = 0; c < ESTOP_CHANNELS; c++) {
			if (k_sem_take(done_sem[c], K_TIMEOUT_ABS_MS(deadline)) != 0) {
				late_mask |= BIT(c);
			} else {
				record_latency(c, (uint32_t)atomic_get(&sampler_done_ms[c]) -
							  (uint32_t)now_ms);
			}
		}

		if (late_mask != 0U) {
			int64_t over = k_uptime_get() - deadline;

			late_by_ms = (over > 0) ? (uint32_t)over : 0U;
		}

		run_tick_body(now_ms, gen, late_mask, late_by_ms);

		k_sleep(K_TIMEOUT_ABS_MS(next));
	}
}

K_THREAD_STACK_DEFINE(sampler_stack_0, SAMPLER_STACK_SIZE);
K_THREAD_STACK_DEFINE(sampler_stack_1, SAMPLER_STACK_SIZE);
K_THREAD_STACK_DEFINE(comparator_stack, COMPARATOR_STACK_SIZE);

static struct k_thread sampler_thread_data[ESTOP_CHANNELS];
static struct k_thread comparator_thread_data;

int pstop_lockstep_start(void)
{
	(void)k_thread_create(&sampler_thread_data[0], sampler_stack_0, SAMPLER_STACK_SIZE,
			      sampler_thread, (void *)(intptr_t)0, NULL, NULL, SAMPLER_PRIORITY, 0,
			      K_NO_WAIT);
	(void)k_thread_name_set(&sampler_thread_data[0], "pstop_ch_a");

	(void)k_thread_create(&sampler_thread_data[1], sampler_stack_1, SAMPLER_STACK_SIZE,
			      sampler_thread, (void *)(intptr_t)1, NULL, NULL, SAMPLER_PRIORITY, 0,
			      K_NO_WAIT);
	(void)k_thread_name_set(&sampler_thread_data[1], "pstop_ch_b");

	(void)k_thread_create(&comparator_thread_data, comparator_stack, COMPARATOR_STACK_SIZE,
			      comparator_thread, NULL, NULL, NULL, COMPARATOR_PRIORITY, 0,
			      K_NO_WAIT);
	(void)k_thread_name_set(&comparator_thread_data, "pstop_cmp");

	LOG_INF("lockstep threads started");
	return 0;
}

#ifdef CONFIG_ZTEST
void pstop_lockstep_test_set_sampler_gen(int channel, uint32_t gen)
{
	atomic_set(&sampler_gen[channel], (atomic_val_t)gen);
}

uint32_t pstop_lockstep_test_tick_gen(void)
{
	return (uint32_t)atomic_get(&tick_gen);
}

bool pstop_lockstep_test_samplers_published(void)
{
	return samplers_published((uint32_t)atomic_get(&tick_gen));
}

void pstop_lockstep_test_tick_late(uint64_t now_ms, uint8_t late_mask)
{
	tick_sync(now_ms, late_mask);
}
#endif /* CONFIG_ZTEST */
