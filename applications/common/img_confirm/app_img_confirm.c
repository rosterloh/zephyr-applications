/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/dfu/mcuboot.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include "app_img_confirm.h"
#include "app_watchdog.h"

LOG_MODULE_REGISTER(app_img_confirm, LOG_LEVEL_INF);

static atomic_t confirmed;
static int wdt_channel = -1;

void app_img_confirm_arm(void)
{
	if (boot_is_img_confirmed()) {
		atomic_set(&confirmed, 1);
		return;
	}

	wdt_channel = app_watchdog_register("img_confirm", CONFIG_APP_IMG_CONFIRM_TIMEOUT_MS);
	if (wdt_channel < 0) {
		LOG_ERR("Image on trial with no revert deadline (%d): it will not revert "
			"unless something else resets the board",
			wdt_channel);
	} else {
		LOG_WRN("Image on trial: reverts unless healthy within %d ms",
			CONFIG_APP_IMG_CONFIRM_TIMEOUT_MS);
	}
}

void app_img_confirm(void)
{
	int ret;

	if (!atomic_cas(&confirmed, 0, 1)) {
		return;
	}

	ret = boot_write_img_confirmed();
	if (ret != 0) {
		/* Deadline stays armed; a later healthy call retries. */
		LOG_ERR("Image confirm failed (%d)", ret);
		atomic_clear(&confirmed);
		return;
	}

	app_watchdog_unregister(wdt_channel);
	LOG_INF("Image healthy: confirmed");
}
