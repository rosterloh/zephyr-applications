/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * HAL glue tests: does driving both phases through a simulated DPST pole
 * produce the right verdict, and does a single-pole fault split the channels?
 */

#include <zephyr/ztest.h>

#include "estop_gpio.h"
#include "estop_verdict.h"
#include "pstop/pstop_msg.h"

static void *suite_setup(void)
{
	zassert_ok(estop_gpio_init(), "gpio init");
	return NULL;
}

ZTEST(estop_gpio, test_closed_pole_arms_after_debounce)
{
	estop_state_t st;
	uint8_t msg = PSTOP_MESSAGE_STOP;

	estop_state_init(&st);
	estop_sim_set_pole(0, true);

	for (unsigned int i = 0U; i < LOOP_RECLOSE_DEBOUNCE_TICKS; i++) {
		msg = estop_channel_sample(0, &st);
	}
	zassert_equal(msg, PSTOP_MESSAGE_OK, "closed pole must arm");
}

ZTEST(estop_gpio, test_open_pole_stops)
{
	estop_state_t st;

	estop_state_init(&st);
	estop_sim_set_pole(0, true);
	for (unsigned int i = 0U; i < LOOP_RECLOSE_DEBOUNCE_TICKS; i++) {
		(void)estop_channel_sample(0, &st);
	}

	estop_sim_set_pole(0, false);
	zassert_equal(estop_channel_sample(0, &st), PSTOP_MESSAGE_STOP,
		      "open pole must stop on the next tick");
}

ZTEST(estop_gpio, test_single_pole_fault_splits_the_channels)
{
	estop_state_t st[2];
	uint8_t v0;
	uint8_t v1;

	estop_state_init(&st[0]);
	estop_state_init(&st[1]);

	/* Pole A intact, pole B broken: exactly the fault lockstep exists for. */
	estop_sim_set_pole(0, true);
	estop_sim_set_pole(1, false);

	for (unsigned int i = 0U; i < LOOP_RECLOSE_DEBOUNCE_TICKS; i++) {
		v0 = estop_channel_sample(0, &st[0]);
		v1 = estop_channel_sample(1, &st[1]);
	}

	zassert_equal(v0, PSTOP_MESSAGE_OK, "healthy channel reports OK");
	zassert_equal(v1, PSTOP_MESSAGE_STOP, "broken channel reports STOP");
	zassert_not_equal(v0, v1, "the channels must disagree, silencing the comparator");
}

ZTEST(estop_gpio, test_channels_are_independent)
{
	estop_state_t st[2];

	estop_state_init(&st[0]);
	estop_state_init(&st[1]);
	estop_sim_set_pole(0, true);
	estop_sim_set_pole(1, true);

	for (unsigned int i = 0U; i < LOOP_RECLOSE_DEBOUNCE_TICKS; i++) {
		(void)estop_channel_sample(0, &st[0]);
		(void)estop_channel_sample(1, &st[1]);
	}

	/* Sampling channel 0 must not disturb channel 1's pins. */
	estop_sim_set_pole(0, false);
	(void)estop_channel_sample(0, &st[0]);
	zassert_equal(estop_channel_sample(1, &st[1]), PSTOP_MESSAGE_OK,
		      "channel 1 must be unaffected by channel 0");
}

ZTEST_SUITE(estop_gpio, NULL, suite_setup, NULL, NULL, NULL);
