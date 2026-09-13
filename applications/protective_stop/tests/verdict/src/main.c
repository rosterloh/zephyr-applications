/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Truth table for the SIL-critical stop-switch verdict core.
 *
 * The loop is healthy only when THIS tick's drive-high echoed high AND this
 * tick's drive-low echoed low. Everything else -- cut wire, stuck-high short,
 * dead input -- is STOP. The open->STOP edge is never filtered; only the
 * release direction is debounced.
 */

#include <zephyr/ztest.h>

#include "estop_verdict.h"
#include "pstop/pstop_msg.h"

/* Feed n healthy ticks and return the last verdict. */
static uint8_t run_healthy(estop_state_t *st, int core_id, unsigned int n)
{
	uint8_t msg = PSTOP_MESSAGE_STOP;

	for (unsigned int i = 0U; i < n; i++) {
		msg = estop_decide(st, core_id, 1, 0);
	}
	return msg;
}

ZTEST(estop_verdict, test_closed_loop_reports_ok_after_debounce)
{
	estop_state_t st;

	estop_state_init(&st);

	/* Not yet debounced. */
	zassert_equal(estop_decide(&st, 0, 1, 0), PSTOP_MESSAGE_STOP, "tick 1");
	zassert_equal(estop_decide(&st, 0, 1, 0), PSTOP_MESSAGE_STOP, "tick 2");
	/* Third consecutive healthy tick satisfies LOOP_RECLOSE_DEBOUNCE_TICKS. */
	zassert_equal(estop_decide(&st, 0, 1, 0), PSTOP_MESSAGE_OK, "tick 3");
}

ZTEST(estop_verdict, test_open_loop_stops_immediately)
{
	estop_state_t st;

	estop_state_init(&st);
	zassert_equal(run_healthy(&st, 0, 5U), PSTOP_MESSAGE_OK, "should be armed");

	/* Cut wire / button pressed: drive high, read low. Single tick, no filter. */
	zassert_equal(estop_decide(&st, 0, 0, 0), PSTOP_MESSAGE_STOP, "open must stop at once");
}

ZTEST(estop_verdict, test_stuck_high_input_is_stop)
{
	estop_state_t st;

	estop_state_init(&st);
	/* IN shorted high: drive-low phase still reads 1. Never healthy. */
	for (unsigned int i = 0U; i < 10U; i++) {
		zassert_equal(estop_decide(&st, 0, 1, 1), PSTOP_MESSAGE_STOP,
			      "stuck-high must never report OK");
	}
}

ZTEST(estop_verdict, test_dead_input_is_stop)
{
	estop_state_t st;

	estop_state_init(&st);
	for (unsigned int i = 0U; i < 10U; i++) {
		zassert_equal(estop_decide(&st, 0, 0, 1), PSTOP_MESSAGE_STOP,
			      "inverted/dead input must never report OK");
	}
}

ZTEST(estop_verdict, test_release_is_debounced_but_stop_is_not)
{
	estop_state_t st;

	estop_state_init(&st);
	(void)run_healthy(&st, 0, 3U);

	zassert_equal(estop_decide(&st, 0, 0, 0), PSTOP_MESSAGE_STOP, "blip opens");
	/* One healthy tick is not enough to re-arm. */
	zassert_equal(estop_decide(&st, 0, 1, 0), PSTOP_MESSAGE_STOP, "reclose tick 1");
	zassert_equal(estop_decide(&st, 0, 1, 0), PSTOP_MESSAGE_STOP, "reclose tick 2");
	zassert_equal(estop_decide(&st, 0, 1, 0), PSTOP_MESSAGE_OK, "reclose tick 3");
}

ZTEST(estop_verdict, test_both_cores_agree_on_every_input)
{
	/* Core 0 forms the verdict arithmetically, core 1 by boolean. They must
	 * be identical for all four input combinations at every debounce depth,
	 * or the comparator would silence a healthy device. */
	for (int hi = 0; hi <= 1; hi++) {
		for (int lo = 0; lo <= 1; lo++) {
			estop_state_t a;
			estop_state_t b;

			estop_state_init(&a);
			estop_state_init(&b);

			for (unsigned int i = 0U; i < 6U; i++) {
				uint8_t va = estop_decide(&a, 0, hi, lo);
				uint8_t vb = estop_decide(&b, 1, hi, lo);

				zassert_equal(va, vb,
					      "core diversity diverged at hi=%d lo=%d tick=%u", hi,
					      lo, i);
			}
		}
	}
}

ZTEST(estop_verdict, test_not_primed_until_both_channels_settle)
{
	estop_state_t st[2];

	estop_state_init(&st[0]);
	estop_state_init(&st[1]);
	zassert_false(estop_channels_primed(st), "must not be primed at boot");

	/* Channel A settles via a full closed-debounce cycle. */
	(void)run_healthy(&st[0], 0, 3U);
	zassert_false(estop_channels_primed(st), "B has not settled");

	(void)run_healthy(&st[1], 1, 3U);
	zassert_true(estop_channels_primed(st), "both settled");
}

ZTEST(estop_verdict, test_button_held_at_boot_settles_via_open_streak)
{
	estop_state_t st[2];

	estop_state_init(&st[0]);
	estop_state_init(&st[1]);

	/* Button genuinely held down at power-on: never healthy. After
	 * LOOP_BOOT_OPEN_CONFIRM_TICKS consecutive opens the channel settles,
	 * so STOP flows instead of the device staying mute forever. */
	for (unsigned int i = 0U; i < LOOP_BOOT_OPEN_CONFIRM_TICKS; i++) {
		(void)estop_decide(&st[0], 0, 0, 0);
		(void)estop_decide(&st[1], 1, 0, 0);
	}
	zassert_true(estop_channels_primed(st), "held-open must settle");
}

ZTEST_SUITE(estop_verdict, NULL, NULL, NULL, NULL, NULL);
