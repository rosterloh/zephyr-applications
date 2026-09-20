/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stall detection on top of Zephyr's task watchdog, shared by the
 * applications in this repo.
 *
 * The failure this catches is a hang, not a fault: a thread stops making
 * progress while the kernel keeps running, so no fatal-error handler fires and
 * no coredump is written. A monitored task registers a channel and feeds it;
 * if it stops feeding, the stall is logged (with the channel's name) and the
 * board cold-reboots. Where the board has a `watchdog0` alias, the hardware
 * watchdog also resets the SoC if the kernel itself hangs.
 *
 * Coverage is per registered channel and nothing else. A thread that never
 * registers is invisible, and so is a hang inside a workqueue handler while
 * the registrant keeps feeding. Two shapes of use follow from that:
 *
 *   - a long-lived channel fed from a loop, for an application with a
 *     superloop (rasprover), and
 *   - a channel registered around one bounded operation and deleted when it
 *     completes, for an event-driven application with no loop to feed from
 *     (data_collection's capture path). A wedged operation then never reaches
 *     its delete and the timeout fires.
 *
 * With CONFIG_DEBUG_COREDUMP enabled, the timeout also emits a coredump before
 * rebooting, so a silent stall produces the same post-mortem a fault would.
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
 * "not registered" and retry. `name` must be a long-lived string: it is read
 * from the timeout handler, which runs in ISR context. */
int app_watchdog_register(const char *name, uint32_t timeout_ms);

/* Stop monitoring a channel, e.g. when a boot-phase channel is replaced by a
 * steady-state one, or when the operation a channel guarded has finished.
 * No-op for a negative channel or before init. */
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
