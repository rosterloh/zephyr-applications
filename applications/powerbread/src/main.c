/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ui.h"

#include <lvgl.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/app_version.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define REFRESH_LOOP_CAP_MS 50

int main(void)
{
	LOG_INF("PowerBread %s", APP_VERSION_STRING);

	if (pb_ui_init() != 0) {
		LOG_ERR("UI disabled; sampler and shell remain active");
		k_sleep(K_FOREVER);
	}

	while (true) {
		uint32_t delay = lv_timer_handler();

		k_msleep(MIN(delay, REFRESH_LOOP_CAP_MS));
	}
	return 0;
}
