/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * The running build's identity, as /state.json and /api/health report it.
 */

#ifndef PROTECTIVE_STOP_FW_INFO_H
#define PROTECTIVE_STOP_FW_INFO_H

#include <zephyr/app_version.h>
#include <zephyr/sys/util.h>

#ifdef APP_BUILD_VERSION
#define FW_SHA STRINGIFY(APP_BUILD_VERSION)
#else
#define FW_SHA ""
#endif

#endif /* PROTECTIVE_STOP_FW_INFO_H */
