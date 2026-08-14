/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_watchdog, LOG_LEVEL_INF);

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/task_wdt/task_wdt.h>

#include "app_watchdog.h"

/* ros_driver/esp32 aliases the TIMG0 MWDT as watchdog0, so the task watchdog
 * is backed by real hardware via CONFIG_TASK_WDT_HW_FALLBACK: the kernel timer
 * catches a stalled task, the hardware resets the SoC if the kernel itself
 * hangs. Targets without a watchdog0 alias (native_sim) pass NULL to
 * task_wdt_init(), which is the documented software-only fallback — stall
 * detection still works, but nothing catches a wedged kernel. */
#if DT_NODE_HAS_STATUS(DT_ALIAS(watchdog0), okay)
#define APP_WDT_HW_DEV DEVICE_DT_GET(DT_ALIAS(watchdog0))
#else
#define APP_WDT_HW_DEV NULL
#endif

static bool wdt_ready;

static void wdt_timeout_cb(int channel_id, void *user_data)
{
	/* Synchronous logging so this reaches the console before the reset. */
	LOG_PANIC();
	LOG_ERR("Task watchdog timeout: '%s' (channel %d) stalled - rebooting",
		(const char *)user_data, channel_id);

	sys_reboot(SYS_REBOOT_COLD);
}

int app_watchdog_init(void)
{
	const struct device *hw_wdt = APP_WDT_HW_DEV;
	int ret;

	if (hw_wdt != NULL && !device_is_ready(hw_wdt)) {
		LOG_WRN("Hardware watchdog not ready; falling back to software only");
		hw_wdt = NULL;
	}

	ret = task_wdt_init(hw_wdt);
	if (ret != 0) {
		/* Deliberately loud: everything below degrades to a no-op from
		 * here, so the board boots and runs with no stall detection at
		 * all. That is the one failure this module must not report
		 * quietly. */
		LOG_ERR("*** task_wdt_init failed (%d) ***", ret);
		LOG_ERR("*** RUNNING UNPROTECTED: no stall detection, hangs will not reboot ***");
		return ret;
	}

	wdt_ready = true;
	LOG_INF("Task watchdog initialised (hardware backing: %s)", hw_wdt ? "yes" : "no");

	return 0;
}

int app_watchdog_register(const char *name, uint32_t timeout_ms)
{
	int ch;

	if (!wdt_ready) {
		return -EBUSY;
	}

	ch = task_wdt_add(timeout_ms, wdt_timeout_cb, (void *)name);
	if (ch < 0) {
		LOG_ERR("task_wdt_add('%s') failed (%d)", name, ch);
	} else {
		LOG_INF("Watchdog channel %d registered for '%s' (%u ms)", ch, name, timeout_ms);
	}

	return ch;
}

void app_watchdog_unregister(int channel)
{
	if (wdt_ready && channel >= 0) {
		task_wdt_delete(channel);
	}
}

void app_watchdog_feed(int channel)
{
	if (wdt_ready && channel >= 0) {
		task_wdt_feed(channel);
	}
}
