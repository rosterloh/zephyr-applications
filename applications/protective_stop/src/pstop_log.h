/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Compile-time log level of this app's modules. With verbose logging built
 * in, debug lines are compiled and filtered out at runtime until
 * /admin/api/verbose/toggle lets them through (verbose.c).
 */

#ifndef PROTECTIVE_STOP_PSTOP_LOG_H
#define PROTECTIVE_STOP_PSTOP_LOG_H

#include <zephyr/logging/log.h>

#ifdef CONFIG_PROTECTIVE_STOP_VERBOSE
#define PSTOP_LOG_LEVEL LOG_LEVEL_DBG
#else
#define PSTOP_LOG_LEVEL LOG_LEVEL_INF
#endif

#endif /* PROTECTIVE_STOP_PSTOP_LOG_H */
