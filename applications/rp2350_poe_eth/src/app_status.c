/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/logging/log.h>

#include "app_status.h"

LOG_MODULE_REGISTER(app_status, CONFIG_APP_LOG_LEVEL);

static const struct device *const strip = DEVICE_DT_GET(DT_ALIAS(led_strip));

static const struct led_rgb status_colours[] = {
	[APP_STATUS_BOOTING] = {.r = 0x10, .g = 0x08, .b = 0x00},
	[APP_STATUS_LINK_UP] = {.r = 0x00, .g = 0x00, .b = 0x20},
	[APP_STATUS_ONLINE] = {.r = 0x00, .g = 0x20, .b = 0x00},
	[APP_STATUS_UPDATING] = {.r = 0x10, .g = 0x00, .b = 0x20},
	[APP_STATUS_ERROR] = {.r = 0x20, .g = 0x00, .b = 0x00},
};

void app_status_set(enum app_status status)
{
	struct led_rgb pixel;
	int ret;

	if (status >= ARRAY_SIZE(status_colours)) {
		return;
	}

	if (!device_is_ready(strip)) {
		LOG_DBG("status LED not ready, ignoring state %d", status);
		return;
	}

	pixel = status_colours[status];

	ret = led_strip_update_rgb(strip, &pixel, 1);
	if (ret < 0) {
		LOG_WRN("failed to update status LED: %d", ret);
	}
}
