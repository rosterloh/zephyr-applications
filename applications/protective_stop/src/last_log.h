/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * /api/last_log: the tail of the previous boot's log. A log backend copies
 * every line into a ring in memory that survives a reset (RTC slow memory on
 * the ESP32-S3: panics, watchdogs and sys_reboot() keep it, a power cycle
 * does not). At startup the backend snapshots what the last boot left there
 * before it writes anything of its own.
 */

#ifndef PROTECTIVE_STOP_LAST_LOG_H
#define PROTECTIVE_STOP_LAST_LOG_H

#include <stddef.h>

/* The previous boot's tail, oldest first, not NUL-terminated. Returns its
 * length; 0 when there was none (a cold boot, or the backend is disabled).
 */
size_t last_log_get(const char **data);

#endif /* PROTECTIVE_STOP_LAST_LOG_H */
