/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * The comparator's contract: transmit only when both samplers produced
 * byte-identical frames, and stay silent until both channels have settled.
 *
 * These use the synchronous pstop_lockstep_tick() rather than the threads, so
 * the tests are deterministic. The threads are exercised by the live interop
 * run in Task 7.
 *
 * NOTE: these cases are deliberately ORDERED and share state.
 *
 * ztest emits each ZTEST() into an iterable section that the linker sorts with
 * SORT_BY_NAME (zephyr/linker/iterable_sections.h), so cases execute in
 * ALPHABETICAL order of their symbol name, not source order. The test_NN_
 * prefixes are therefore load-bearing: they are what makes the sequence below
 * run in the order it is written in. Do not remove them.
 *
 * The ordering is intentional rather than incidental. This suite models one
 * device across successive ticks -- unsettled at boot, then settled and
 * transmitting, then split, then healed, then a real stop-and-recover cycle
 * -- and that progression is the behaviour under test. Only test_01 requires
 * pristine state (sent == 0, channels unprimed); the rest settle and snapshot
 * their own baseline first.
 *
 * test_00 is the one exception to all of the above: it is a pure white-box
 * unit test of the generation-stamp completion check (see lockstep.h), and it
 * neither depends on nor disturbs the device-model progression -- any real
 * tick immediately overwrites whatever it pokes into the sampler generations.
 * It sorts before test_01 by construction, which is convenient but not
 * load-bearing the way test_01..test_04's ordering is.
 */

#include <string.h>

#include <zephyr/net/socket.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "app_settings.h"
#include "estop_gpio.h"
#include "lockstep.h"
#include "session.h"

#define FAKE_MACHINE_PORT  18890U
#define FAKE_MACHINE_HB_MS 1000U
#define FAKE_MACHINE_ID    0x01020304U

static int fake_machine_sock;

/* Every message type the fake machine has actually received, in order --
 * i.e. what really reached the wire, not what the remote intended to send.
 * test_05 decodes this to pin the arming gesture (STOP->OK) at the message
 * level, which byte-count assertions like test_04's cannot: a count only
 * proves something was sent, never what.
 */
#define RECORDED_MESSAGES_MAX 64
static uint8_t recorded_messages[RECORDED_MESSAGES_MAX];
static size_t recorded_count;

static void recorded_reset(void)
{
	recorded_count = 0U;
}

/* A minimal fake machine: it acks a BOND request with a heartbeat_timeout,
 * because pstop_session_poll() only reaches PSTOP_SESS_BONDED on a decoded
 * reply, and pstop_session_build() emits a BOND frame -- with no
 * verdict-derived field at all -- until then. Without this, both samplers'
 * frames stay byte-identical no matter what the switch does, and the
 * comparator can never be exercised. Called explicitly between ticks, never
 * on a thread, so the tests stay deterministic.
 */
static void fake_machine_service(void)
{
	uint8_t buf[PSTOP_MESSAGE_SIZE];
	uint8_t out[PSTOP_MESSAGE_SIZE];
	struct sockaddr_in from;
	socklen_t from_len = sizeof(from);
	pstop_msg_t req;
	pstop_msg_t reply;
	device_id_t machine_id;
	ssize_t n;

	n = zsock_recvfrom(fake_machine_sock, buf, sizeof(buf), ZSOCK_MSG_DONTWAIT,
			   (struct sockaddr *)&from, &from_len);
	if (n != (ssize_t)PSTOP_MESSAGE_SIZE) {
		return;
	}

	pstop_message_decode(&req, buf);

	if (recorded_count < RECORDED_MESSAGES_MAX) {
		recorded_messages[recorded_count++] = req.message;
	}

	device_id_set(&machine_id, FAKE_MACHINE_ID);
	pstop_create_ok_message(&reply, req.stamp, req.stamp, &machine_id, &req.id,
				req.counter + 1U, req.counter);
	reply.heartbeat_timeout = FAKE_MACHINE_HB_MS;

	pstop_message_encode(&reply, out);
	(void)zsock_sendto(fake_machine_sock, out, sizeof(out), 0, (struct sockaddr *)&from,
			   from_len);
}

static void *suite_setup(void)
{
	struct sockaddr_in fake_addr;

	zassert_ok(app_settings_init(), "settings");
	/* Point slot 0 at 127.0.0.1:18890, where fake_machine_service() above
	 * acks the BOND -- see its comment for why that ack is required.
	 */
	zassert_ok(
		app_settings_set_peer(0, 0x7F000001U, (uint16_t)FAKE_MACHINE_PORT, FAKE_MACHINE_ID),
		"peer");
	zassert_ok(app_settings_set_device_id(0xC0A80105U), "device id");
	zassert_ok(estop_gpio_init(), "gpio");
	zassert_ok(pstop_lockstep_init(), "lockstep");

	fake_machine_sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	zassert_true(fake_machine_sock >= 0, "fake machine socket");

	(void)memset(&fake_addr, 0, sizeof(fake_addr));
	fake_addr.sin_family = AF_INET;
	fake_addr.sin_port = htons((uint16_t)FAKE_MACHINE_PORT);
	fake_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	zassert_ok(zsock_bind(fake_machine_sock, (struct sockaddr *)&fake_addr, sizeof(fake_addr)),
		   "fake machine bind");

	return NULL;
}

ZTEST(lockstep, test_00_samplers_published_rejects_stale_generation)
{
	uint32_t gen;

	/* Manufacture the exact defect the generation stamp exists to catch: a
	 * sampler's last completion belongs to a stale generation, meaning it
	 * did not run for the tick under comparison. This is expressible as
	 * pure state -- no threads, no timing, and unlike a thread-timing test
	 * it does not self-heal on a uniprocessor.
	 */
	gen = pstop_lockstep_test_tick_gen();
	pstop_lockstep_test_set_sampler_gen(0, gen);
	pstop_lockstep_test_set_sampler_gen(1, gen);
	zassert_true(pstop_lockstep_test_samplers_published(),
		     "both channels at the current generation must publish");

	pstop_lockstep_test_set_sampler_gen(1, gen - 1U);
	zassert_false(pstop_lockstep_test_samplers_published(),
		      "a channel stuck on a stale generation must not publish");

	/* Leave both channels at the current generation: the next real tick
	 * overwrites this regardless, but there is no reason to leave it torn.
	 */
	pstop_lockstep_test_set_sampler_gen(1, gen);
}

ZTEST(lockstep, test_01_silent_until_both_channels_settle)
{
	const struct pstop_session *s;

	/* Both poles closed but not yet debounced: nothing may go out. The
	 * boot-priming STOP must never reach a machine, because a machine
	 * treats STOP->OK as the arming gesture and would arm with no operator
	 * action at all.
	 */
	estop_sim_set_pole(0, true);
	estop_sim_set_pole(1, true);

	pstop_lockstep_tick(1000ULL);

	s = pstop_lockstep_session(0);
	zassert_equal(s->sent, 0U, "nothing may be sent before both channels settle");
}

ZTEST(lockstep, test_02_sends_once_settled)
{
	const struct pstop_session *s;
	uint64_t now = 2000ULL;

	estop_sim_set_pole(0, true);
	estop_sim_set_pole(1, true);

	for (unsigned int i = 0U; i < 8U; i++) {
		pstop_lockstep_tick(now);
		now += 100ULL;
	}

	s = pstop_lockstep_session(0);
	zassert_true(s->sent > 0U, "a settled, agreeing pair must transmit");
}

ZTEST(lockstep, test_03_split_channels_silence_every_session)
{
	const struct pstop_session *s;
	uint32_t sent_before;
	uint32_t mismatches_before;
	uint64_t now = 5000ULL;

	/* Settle and bond first: the fake machine acks the BOND so the session
	 * reaches PSTOP_SESS_BONDED and its frames start carrying the real
	 * verdict, which a channel split can then diverge.
	 */
	estop_sim_set_pole(0, true);
	estop_sim_set_pole(1, true);
	for (unsigned int i = 0U; i < 8U; i++) {
		pstop_lockstep_tick(now);
		fake_machine_service();
		now += 100ULL;
	}

	s = pstop_lockstep_session(0);
	zassert_equal(s->state, PSTOP_SESS_BONDED, "must be bonded before a verdict can diverge");
	sent_before = s->sent;
	zassert_true(sent_before > 0U, "a bonded, agreeing pair must already have transmitted");

	/* A few more agreeing, bonded ticks: confirm the comparator keeps
	 * matching real verdict-bearing frames and keeps transmitting.
	 */
	for (unsigned int i = 0U; i < 5U; i++) {
		pstop_lockstep_tick(now);
		fake_machine_service();
		now += 100ULL;
	}
	zassert_true(s->sent > sent_before, "a bonded, agreeing pair must keep transmitting");
	sent_before = s->sent;
	mismatches_before = pstop_lockstep_mismatches();

	/* Now break ONE pole: the channels disagree, so nothing may go out and
	 * the machine must time out on heartbeat liveness.
	 */
	estop_sim_set_pole(1, false);
	for (unsigned int i = 0U; i < 10U; i++) {
		pstop_lockstep_tick(now);
		fake_machine_service();
		now += 100ULL;
	}

	zassert_equal(s->sent, sent_before, "a channel split must silence transmission");
	zassert_true(pstop_lockstep_mismatches() > mismatches_before,
		     "the mismatch must be counted");
}

ZTEST(lockstep, test_04_recovers_when_the_split_heals)
{
	const struct pstop_session *s;
	uint32_t sent_before;
	uint64_t now = 9000ULL;

	estop_sim_set_pole(0, true);
	estop_sim_set_pole(1, false);
	for (unsigned int i = 0U; i < 6U; i++) {
		pstop_lockstep_tick(now);
		fake_machine_service();
		now += 100ULL;
	}

	s = pstop_lockstep_session(0);
	zassert_equal(s->state, PSTOP_SESS_BONDED, "must be bonded before a verdict can diverge");
	sent_before = s->sent;

	estop_sim_set_pole(1, true);
	for (unsigned int i = 0U; i < 10U; i++) {
		pstop_lockstep_tick(now);
		fake_machine_service();
		now += 100ULL;
	}

	zassert_true(s->sent > sent_before, "transmission must resume once the channels agree");
}

/* Count STOP->OK transitions in the recorded sequence, and confirm no OK
 * precedes the first STOP. A transition is an OK immediately following a
 * STOP -- consecutive repeats of either message do not add one.
 */
static void assert_exactly_one_arming_edge(void)
{
	bool seen_stop = false;
	int transitions = 0;

	zassert_true(recorded_count > 0U, "the fake machine must have received something");

	for (size_t i = 0U; i < recorded_count; i++) {
		if (recorded_messages[i] == PSTOP_MESSAGE_STOP) {
			seen_stop = true;
		} else if (recorded_messages[i] == PSTOP_MESSAGE_OK) {
			zassert_true(seen_stop, "an OK must never reach the wire before a STOP");
			if ((i > 0U) && (recorded_messages[i - 1U] == PSTOP_MESSAGE_STOP)) {
				transitions++;
			}
		}
	}

	zassert_equal(transitions, 1, "exactly one STOP->OK arming edge must reach the wire");
}

/* test_04 leaves both channels agreeing and closed (transmitting OK). Both
 * are driven to STOP together here -- an agreed STOP, unlike test_03/04's
 * channel split, which never puts an agreed STOP on the wire at all (a
 * mismatch is silence, not a transmitted STOP). This is the scenario the
 * arming gesture actually depends on: a machine reads STOP->OK as "the
 * operator did something", so the wire must carry exactly one such edge
 * across a real stop-and-recover cycle, even with a mechanical bounce during
 * the reclose.
 *
 * A transmission only happens on a "due" tick -- once bonded, that is every
 * hb_ms/2 (500 ms = 5 ticks at the 100 ms step used throughout this file),
 * not every physical sample -- so a one-tick bounce cannot just be dropped in
 * anywhere and trusted to land on the wire. The physical debounce state
 * still advances every tick regardless of whether that tick transmits, so
 * the bounce is placed to land ON a due tick deliberately: that is the one
 * placement where a debounce bug is guaranteed to be visible on the wire,
 * and where correct debounce is guaranteed to still show STOP.
 */
ZTEST(lockstep, test_05_wire_carries_exactly_one_arming_edge)
{
	const struct pstop_session *s = pstop_lockstep_session(0);
	uint64_t now = 11000ULL; /* small step past test_04's end: no rebond */
	uint32_t sent_before;
	unsigned int i;

	recorded_reset();

	/* Agreed STOP, held until it actually reaches the wire (T). */
	estop_sim_set_pole(0, false);
	estop_sim_set_pole(1, false);
	sent_before = s->sent;
	for (i = 0U; i < 20U; i++) {
		pstop_lockstep_tick(now);
		fake_machine_service();
		now += 100ULL;
		if (s->sent > sent_before) {
			break;
		}
	}
	zassert_true(s->sent > sent_before, "an agreed STOP must reach the wire");

	/* T+1..T+4: still open, nothing changes. The next due tick is exactly
	 * T+5 -- nothing else resets last_send_ms in between.
	 */
	for (i = 0U; i < 4U; i++) {
		pstop_lockstep_tick(now);
		fake_machine_service();
		now += 100ULL;
	}

	/* T+5: the one-tick mechanical bounce, landing exactly on the due
	 * tick. The release debounce exists to absorb exactly this without
	 * producing a premature OK.
	 */
	estop_sim_set_pole(0, true);
	estop_sim_set_pole(1, true);
	pstop_lockstep_tick(now);
	fake_machine_service();
	now += 100ULL;
	estop_sim_set_pole(0, false);
	estop_sim_set_pole(1, false);

	/* T+6..T+14: held open well past the next due tick (T+10), so it
	 * samples an unambiguous STOP -- confirming the bounce did not latch.
	 */
	for (i = 0U; i < 9U; i++) {
		pstop_lockstep_tick(now);
		fake_machine_service();
		now += 100ULL;
	}

	/* T+15 onward: the real reclose, held closed continuously well past
	 * the debounce and the next due tick (T+20), so the wire settles on
	 * OK for good.
	 */
	estop_sim_set_pole(0, true);
	estop_sim_set_pole(1, true);
	for (i = 0U; i < 9U; i++) {
		pstop_lockstep_tick(now);
		fake_machine_service();
		now += 100ULL;
	}

	assert_exactly_one_arming_edge();
}

ZTEST_SUITE(lockstep, NULL, suite_setup, NULL, NULL, NULL);
