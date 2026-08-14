/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stall detection on top of Zephyr's task watchdog. rasprover's observed
 * failure modes are hangs (display init, WiFi connect) rather than CPU
 * faults, and a fatal-error handler catches neither. A monitored task
 * registers a channel and feeds it from its loop; if it stops feeding, the
 * stall is logged and the board cold-reboots. Where a hardware watchdog is
 * available it also resets the SoC if the kernel itself hangs.
 *
 * Coverage is per registered thread, and main() is the only registrant, so
 * only hangs in main's own call path are detected -- app_net_connect() among
 * them. A hang inside a workqueue handler (the LVGL display work, which
 * app_display_init() only submits) leaves main running and feeding, and a
 * pre-main driver init hang predates the first feed. Catching either needs a
 * channel registered and fed by that thread.
 */

#ifndef APP_WATCHDOG_H
#define APP_WATCHDOG_H

#include <errno.h>
#include <stdint.h>
#include <zephyr/toolchain.h>

#ifdef CONFIG_APP_WATCHDOG

/* Initialise the task watchdog. Call once, as early in main() as possible:
 * the boot sequence itself is what needs covering. Returns 0 on success,
 * negative on error, in which case register/feed degrade to no-ops. */
int app_watchdog_init(void);

/* Register a monitored task. Returns a channel id (>= 0), or negative if the
 * watchdog is unavailable or not initialised yet — callers may treat that as
 * "not registered" and retry. `name` must be a long-lived string (it is used
 * in the timeout log). */
int app_watchdog_register(const char *name, uint32_t timeout_ms);

/* Stop monitoring a channel, e.g. when a boot-phase channel is replaced by a
 * steady-state one. No-op for a negative channel or before init. */
void app_watchdog_unregister(int channel);

/* Feed a registered channel. No-op for a negative channel or before init. */
void app_watchdog_feed(int channel);

#else /* Not built: degrade to no-ops so call sites stay unguarded. */

static inline int app_watchdog_init(void)
{
	return -ENOTSUP;
}

static inline int app_watchdog_register(const char *name, uint32_t timeout_ms)
{
	ARG_UNUSED(name);
	ARG_UNUSED(timeout_ms);
	return -ENOTSUP;
}

static inline void app_watchdog_unregister(int channel)
{
	ARG_UNUSED(channel);
}

static inline void app_watchdog_feed(int channel)
{
	ARG_UNUSED(channel);
}

#endif /* CONFIG_APP_WATCHDOG */

#endif /* APP_WATCHDOG_H */
