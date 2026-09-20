/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#ifdef CONFIG_GPIO_EMUL
#include <zephyr/drivers/gpio/gpio_emul.h>
#endif

#include "estop_gpio.h"

LOG_MODULE_REGISTER(estop_gpio, LOG_LEVEL_INF);

#define ZUSER DT_PATH(zephyr_user)

/* Propagation through the wire and the switch contacts. */
#define ESTOP_SETTLE_US 10

static const struct gpio_dt_spec estop_out[ESTOP_CHANNELS] = {
	GPIO_DT_SPEC_GET(ZUSER, estop_a_out_gpios),
	GPIO_DT_SPEC_GET(ZUSER, estop_b_out_gpios),
};

static const struct gpio_dt_spec estop_in[ESTOP_CHANNELS] = {
	GPIO_DT_SPEC_GET(ZUSER, estop_a_in_gpios),
	GPIO_DT_SPEC_GET(ZUSER, estop_b_in_gpios),
};

#ifdef CONFIG_GPIO_EMUL
static bool sim_pole_closed[ESTOP_CHANNELS];

void estop_sim_set_pole(int ch, bool closed)
{
	if ((ch >= 0) && (ch < ESTOP_CHANNELS)) {
		sim_pole_closed[ch] = closed;
	}
}

/* Model the wire: a closed pole conducts the driven level to the sense pin, an
 * open pole leaves it at the pull-down's 0.
 */
static void sim_propagate(int ch, int driven)
{
	(void)gpio_emul_input_set_dt(&estop_in[ch], sim_pole_closed[ch] ? driven : 0);
}
#else
#define sim_propagate(ch, driven) ((void)0)
#endif

int estop_gpio_init(void)
{
	for (int c = 0; c < ESTOP_CHANNELS; c++) {
		int ret;

		if (!gpio_is_ready_dt(&estop_out[c]) || !gpio_is_ready_dt(&estop_in[c])) {
			LOG_ERR("channel %d gpio not ready", c);
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&estop_out[c], GPIO_OUTPUT_INACTIVE);
		if (ret != 0) {
			LOG_ERR("channel %d out configure: %d", c, ret);
			return ret;
		}

		/* Pull DOWN: an open loop or a cut wire reads 0 = STOP. */
		ret = gpio_pin_configure_dt(&estop_in[c], GPIO_INPUT | GPIO_PULL_DOWN);
		if (ret != 0) {
			LOG_ERR("channel %d in configure: %d", c, ret);
			return ret;
		}
	}

	LOG_INF("stop-switch loopback ready: %d channels", ESTOP_CHANNELS);
	return 0;
}

uint8_t estop_channel_sample(int core_id, estop_state_t *st)
{
	int rb_hi;
	int rb_lo;

	/* Sample BOTH loop phases THIS tick. A healthy closed loop conducts as
	 * driven, so rb_hi==1 AND rb_lo==0. Driving only one phase would let a
	 * stuck-high sense pin masquerade as a closed loop.
	 */
	(void)gpio_pin_set_dt(&estop_out[core_id], 1);
	sim_propagate(core_id, 1);
	k_busy_wait(ESTOP_SETTLE_US);
	rb_hi = gpio_pin_get_dt(&estop_in[core_id]);

	(void)gpio_pin_set_dt(&estop_out[core_id], 0);
	sim_propagate(core_id, 0);
	k_busy_wait(ESTOP_SETTLE_US);
	rb_lo = gpio_pin_get_dt(&estop_in[core_id]);

	/* A read error must never read as a healthy level. */
	if (rb_hi < 0) {
		rb_hi = 0;
	}
	if (rb_lo < 0) {
		rb_lo = 1;
	}

	return estop_decide(st, core_id, rb_hi, rb_lo);
}
