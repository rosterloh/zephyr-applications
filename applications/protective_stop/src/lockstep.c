/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

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

int pstop_lockstep_init(void)
{
	int opened = 0;

	for (int c = 0; c < ESTOP_CHANNELS; c++) {
		estop_state_init(&estop[c]);
	}

	for (int slot = 0; slot < PSTOP_MAX_MACHINES; slot++) {
		const struct pstop_peer *peer = app_settings_peer(slot);
		int ret;

		slot_active[slot] = false;
		sessions[slot].sock = -1;

		if ((peer == NULL) || !peer->configured) {
			continue;
		}

		ret = pstop_session_open(&sessions[slot], slot, peer);
		if (ret != 0) {
			LOG_ERR("slot %d open failed: %d", slot, ret);
			continue;
		}

		slot_active[slot] = true;
		opened++;
	}

	LOG_INF("lockstep ready: %d session(s)", opened);
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

	for (int slot = 0; slot < PSTOP_MAX_MACHINES; slot++) {
		if (slot_active[slot] && tick_due[slot]) {
			pstop_session_build(&sessions[slot], verdict, tick_now_ms,
					    frames[channel][slot]);
		}
	}

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
	uint32_t gen = (uint32_t)atomic_add(&tick_gen, 1) + 1U;

	tick_now_ms = now_ms;
	for (int slot = 0; slot < PSTOP_MAX_MACHINES; slot++) {
		tick_due[slot] = slot_active[slot] && pstop_session_due(&sessions[slot], now_ms);
	}
	return gen;
}

/* Compare-or-decline, then poll and watchdog every active session. Shared by
 * the synchronous and threaded paths: the only difference between them is how
 * the samplers ran, not what happens once they have (or have not).
 */
static void run_tick_body(uint64_t now_ms, uint32_t gen)
{
	if (samplers_published(gen)) {
		comparator_pass();
	} else {
		LOG_WRN("sampler(s) missed tick %u", gen);
		mismatches++;
	}

	for (int slot = 0; slot < PSTOP_MAX_MACHINES; slot++) {
		if (slot_active[slot]) {
			pstop_session_poll(&sessions[slot], now_ms);
			pstop_session_watchdog(&sessions[slot], now_ms);
		}
	}
}

void pstop_lockstep_tick(uint64_t now_ms)
{
	uint32_t gen = begin_tick(now_ms);

	for (int c = 0; c < ESTOP_CHANNELS; c++) {
		sampler_pass(c, gen);
	}

	run_tick_body(now_ms, gen);
}

uint32_t pstop_lockstep_mismatches(void)
{
	return mismatches;
}

const struct pstop_session *pstop_lockstep_session(int slot)
{
	if ((slot < 0) || (slot >= PSTOP_MAX_MACHINES)) {
		return NULL;
	}
	return &sessions[slot];
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

		next += PSTOP_TICK_MS;

		/* Snapshot the tick's shared inputs and mint its generation
		 * BEFORE waking the samplers, so both encode identical stamps
		 * and the same due-set, and so a late completion from a prior
		 * tick can never be mistaken for this one's.
		 */
		now_ms = (uint64_t)k_uptime_get();
		gen = begin_tick(now_ms);

		for (int c = 0; c < ESTOP_CHANNELS; c++) {
			k_sem_give(go_sem[c]);
		}

		/* The semaphore only carries wakeup here, not correctness: a
		 * timed-out take just means don't wait longer, not that the
		 * sampler's own generation stamp (checked in run_tick_body()
		 * via samplers_published()) is untrustworthy. A sampler that
		 * gives late still cannot make its stale frame look current.
		 */
		for (int c = 0; c < ESTOP_CHANNELS; c++) {
			if (k_sem_take(done_sem[c], K_MSEC(PSTOP_SAMPLER_DEADLINE_MS)) != 0) {
				LOG_WRN("sampler %d missed its deadline", c);
			}
		}

		run_tick_body(now_ms, gen);

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
#endif /* CONFIG_ZTEST */
