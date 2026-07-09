/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sampler.h"
#include "ui.h"

#include <zephyr/input/input.h>

static void dial_cb(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	if (evt->type != INPUT_EV_KEY || evt->value != 1) {
		return;
	}

	switch (evt->code) {
	case INPUT_KEY_UP:
		pb_ui_step_mode(1);
		break;
	case INPUT_KEY_DOWN:
		pb_ui_step_mode(-1);
		break;
	case INPUT_KEY_A: /* dial short press */
		pb_ui_set_channel((pb_ui_get_channel() + 1) % PB_NUM_CH);
		break;
	case INPUT_KEY_B: /* dial long press */
		sampler_reset_stats();
		break;
	default:
		break;
	}
}

INPUT_CALLBACK_DEFINE(NULL, dial_cb, NULL);
