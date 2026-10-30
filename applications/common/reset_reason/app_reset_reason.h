/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Why the board last reset, kept in a retention area across the reset and
 * logged once at the next boot (SYS_INIT, APPLICATION level).
 *
 * Writers mark a reset just before it happens: the task-watchdog timeout
 * records the stalled channel, and this module's fatal-error handler records
 * the fatal reason. A boot with no mark falls back to hwinfo's reset cause:
 * power-on, the reset pin and a plain sys_reboot() are expected; anything
 * else (hardware watchdog, brownout, CPU lockup) is an unexpected reset.
 * Marked watchdog/fatal resets and unexpected ones bump a counter that lives
 * until the next power cycle.
 */

#ifndef APP_RESET_REASON_H
#define APP_RESET_REASON_H

#include <zephyr/toolchain.h>

#ifdef CONFIG_APP_RESET_REASON

/* Mark the coming reset as a task-watchdog timeout on `channel`. Safe from
 * ISR context; `name` is copied, truncated to 15 characters. */
void app_reset_reason_watchdog(int channel, const char *name);

#else

static inline void app_reset_reason_watchdog(int channel, const char *name)
{
	ARG_UNUSED(channel);
	ARG_UNUSED(name);
}

#endif /* CONFIG_APP_RESET_REASON */

#endif /* APP_RESET_REASON_H */
