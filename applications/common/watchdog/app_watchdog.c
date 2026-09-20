/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_watchdog, LOG_LEVEL_INF);

#include <errno.h>
#include <zephyr/debug/coredump.h>
#include <zephyr/device.h>
#include <zephyr/fatal_types.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/task_wdt/task_wdt.h>

#include "app_watchdog.h"

/* A board that aliases a hardware watchdog as watchdog0 gets the full
 * arrangement via CONFIG_TASK_WDT_HW_FALLBACK: the kernel timer catches a
 * stalled task, the hardware resets the SoC if the kernel itself hangs.
 * Targets without the alias (native_sim) pass NULL to task_wdt_init(), which
 * is the documented software-only fallback — stall detection still works, but
 * nothing catches a wedged kernel. */
#if DT_NODE_HAS_STATUS(DT_ALIAS(watchdog0), okay)
#define APP_WDT_HW_DEV DEVICE_DT_GET(DT_ALIAS(watchdog0))
#else
#define APP_WDT_HW_DEV NULL
#endif

static bool wdt_ready;

/* Runs in ISR context: task_wdt drives it from a k_timer expiry. Everything
 * here has to be safe from an interrupt and has to finish inside
 * CONFIG_TASK_WDT_HW_FALLBACK_DELAY, or the hardware watchdog resets the SoC
 * mid-way through. */
static void wdt_timeout_cb(int channel_id, void *user_data)
{
	/* Synchronous logging so this reaches the console before the reset. */
	LOG_PANIC();
	LOG_ERR("Task watchdog timeout: '%s' (channel %d) stalled - rebooting",
		(const char *)user_data, channel_id);

	/* A stall produces no fatal error, so nothing else would dump -- but
	 * only a backend that is safe from an interrupt may run here, and only
	 * the logging one is. It puts the log subsystem in panic mode itself and
	 * writes from a static buffer.
	 *
	 * The flash-partition backend is deliberately excluded even though both
	 * it and Zephyr's ESP32 flash driver degrade their semaphore waits to
	 * K_NO_WAIT in ISR context. Two things still break it there: the driver's
	 * synchronous write and erase are not IRAM-resident, so writing flash
	 * from an ISR on an XIP part risks executing from a disabled cache, and a
	 * sector erase does not fit inside CONFIG_TASK_WDT_HW_FALLBACK_DELAY
	 * (20 ms by default) before the hardware watchdog resets the SoC
	 * mid-dump. A handler meant to recover the board must not be the thing
	 * that hangs it.
	 *
	 * Dumping a stall to flash needs the write deferred to a high-priority
	 * thread this handler wakes, plus a fallback delay long enough for the
	 * erase. Worth doing if stall post-mortems are wanted; not done here.
	 *
	 * k_current_get() is also the thread the timer interrupted rather than
	 * the stalled one, which is typically blocked, so what survives a stall
	 * today is the channel name logged above -- and that is the diagnostic
	 * that actually identifies it.
	 */
	if (IS_ENABLED(CONFIG_DEBUG_COREDUMP_BACKEND_LOGGING)) {
		coredump(K_ERR_KERNEL_OOPS, NULL, k_current_get());
	}

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
