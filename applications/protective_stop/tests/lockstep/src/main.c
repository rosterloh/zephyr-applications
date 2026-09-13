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
 * transmitting, then split, then healed -- and that progression is the
 * behaviour under test. Only test_01 requires pristine state (sent == 0,
 * channels unprimed); the rest settle and snapshot their own baseline first.
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

ZTEST_SUITE(lockstep, NULL, suite_setup, NULL, NULL, NULL);
