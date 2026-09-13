/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Session timing and framing rules. These are the ones that silently break
 * interop when wrong: send period, counter decimation, and a rebond watchdog
 * that must never fire before the machine's own bond-drop timeout.
 */

#include <string.h>

#include <zephyr/ztest.h>

#include "app_settings.h"
#include "pstop/pstop_msg.h"
#include "session.h"

static struct pstop_session sess;
static const struct pstop_peer peer = {
	.ip = 0x7F000001U, /* 127.0.0.1 */
	.port = 8890U,
	.id = 0x01020304U,
	.configured = true,
};

static void *suite_setup(void)
{
	zassert_ok(app_settings_init(), "settings init");
	return NULL;
}

static void test_before(void *fixture)
{
	ARG_UNUSED(fixture);
	(void)memset(&sess, 0, sizeof(sess));
	zassert_ok(pstop_session_open(&sess, 0, &peer), "open");
}

static void test_after(void *fixture)
{
	ARG_UNUSED(fixture);
	pstop_session_close(&sess);
}

ZTEST(pstop_session, test_send_period_defaults_to_floor_before_first_reply)
{
	/* No reply yet, so no machine-advertised heartbeat. Until one arrives
	 * we transmit at the fast end rather than guessing slow.
	 */
	zassert_equal(pstop_session_send_period_ms(&sess), 100U, "pre-reply period");
}

ZTEST(pstop_session, test_send_period_is_half_the_advertised_heartbeat)
{
	sess.hb_ms = 1000U; /* what upstream's machine_app advertises */
	zassert_equal(pstop_session_send_period_ms(&sess), 500U, "half of 1000");

	sess.hb_ms = 400U;
	zassert_equal(pstop_session_send_period_ms(&sess), 200U, "half of 400");
}

ZTEST(pstop_session, test_send_period_is_clamped)
{
	sess.hb_ms = 10U; /* absurdly fast */
	zassert_equal(pstop_session_send_period_ms(&sess), 100U, "clamped to the 10 Hz tick");

	sess.hb_ms = 60000U; /* absurdly slow */
	zassert_equal(pstop_session_send_period_ms(&sess), 1000U, "clamped to 1 s");
}

ZTEST(pstop_session, test_counter_advances_only_on_commit)
{
	uint8_t a[PSTOP_MESSAGE_SIZE];
	uint8_t b[PSTOP_MESSAGE_SIZE];

	/* Building twice without committing must produce identical bytes --
	 * this is exactly what the two lockstep samplers do, and if build()
	 * advanced the counter they would never match.
	 */
	pstop_session_build(&sess, PSTOP_MESSAGE_OK, 5000ULL, a);
	pstop_session_build(&sess, PSTOP_MESSAGE_OK, 5000ULL, b);
	zassert_mem_equal(a, b, PSTOP_MESSAGE_SIZE, "two builds must agree");
}

ZTEST(pstop_session, test_committed_sends_have_contiguous_counters)
{
	uint8_t buf[PSTOP_MESSAGE_SIZE];
	pstop_msg_t msg;
	uint32_t first;

	pstop_session_build(&sess, PSTOP_MESSAGE_OK, 5000ULL, buf);
	pstop_message_decode(&msg, buf);
	first = msg.counter;
	(void)pstop_session_commit_send(&sess, buf, 5000ULL);

	pstop_session_build(&sess, PSTOP_MESSAGE_OK, 5500ULL, buf);
	pstop_message_decode(&msg, buf);

	/* Contiguous, NOT +5 for the five 100 ms ticks that elapsed. The
	 * machine rejects gaps beyond max_lost_messages + 1.
	 */
	zassert_equal(msg.counter, first + 1U, "counter must advance by exactly one per send");
}

ZTEST(pstop_session, test_first_message_is_a_bond)
{
	uint8_t buf[PSTOP_MESSAGE_SIZE];
	pstop_msg_t msg;

	pstop_session_build(&sess, PSTOP_MESSAGE_OK, 1000ULL, buf);
	pstop_message_decode(&msg, buf);

	zassert_equal(msg.message, PSTOP_MESSAGE_BOND,
		      "an unbonded session must bond before it heartbeats");
}

ZTEST(pstop_session, test_bonded_session_carries_the_verdict)
{
	uint8_t buf[PSTOP_MESSAGE_SIZE];
	pstop_msg_t msg;

	sess.state = PSTOP_SESS_BONDED;

	pstop_session_build(&sess, PSTOP_MESSAGE_STOP, 1000ULL, buf);
	pstop_message_decode(&msg, buf);
	zassert_equal(msg.message, PSTOP_MESSAGE_STOP, "verdict must reach the wire");

	pstop_session_build(&sess, PSTOP_MESSAGE_OK, 1000ULL, buf);
	pstop_message_decode(&msg, buf);
	zassert_equal(msg.message, PSTOP_MESSAGE_OK, "verdict must reach the wire");
}

ZTEST(pstop_session, test_addressing_matches_the_peer)
{
	uint8_t buf[PSTOP_MESSAGE_SIZE];
	pstop_msg_t msg;

	(void)app_settings_set_device_id(0xC0A80105U);
	pstop_session_build(&sess, PSTOP_MESSAGE_OK, 1000ULL, buf);
	pstop_message_decode(&msg, buf);

	zassert_equal(msg.receiver_id.data, peer.id, "receiver must be the machine id");
	zassert_equal(msg.id.data, 0xC0A80105U, "sender must be our device id");
}

ZTEST(pstop_session, test_rebond_watchdog_exceeds_the_machine_timeout)
{
	/* The machine drops a bond after hb_ms * max_missed. Our watchdog must
	 * fire LATER, or a sub-timeout reply blip causes a nuisance rebond
	 * while the machine still holds the bond.
	 */
	sess.hb_ms = 400U;
	zassert_true(pstop_session_rebond_after_ms(&sess) > (400U * 5U),
		     "watchdog must outlast the machine's 400*5 ms bond timeout");

	sess.hb_ms = 1000U;
	zassert_true(pstop_session_rebond_after_ms(&sess) > (1000U * 5U),
		     "watchdog must outlast the machine's 1000*5 ms bond timeout");
}

ZTEST(pstop_session, test_rebond_watchdog_has_a_floor)
{
	sess.hb_ms = 0U; /* no reply yet, nothing advertised */
	zassert_true(pstop_session_rebond_after_ms(&sess) >= 2500U,
		     "pre-reply watchdog must still have a sane floor");
}

ZTEST_SUITE(pstop_session, NULL, suite_setup, test_before, test_after, NULL);
