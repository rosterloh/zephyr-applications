/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr port of pstop_c's time_get_now().
 *
 * Upstream's pstop_c/pstop/src/pstop/time.c implements this only for
 * __linux__ and returns 0 everywhere else, which would silently break every
 * heartbeat and timeout. pstop_c.cmake excludes that file so only this
 * definition is linked.
 *
 * Units: pstop expects milliseconds. k_uptime_get() returns milliseconds
 * since boot from the monotonic kernel clock -- no RTC or NTP
 * discontinuities, which is exactly what a timeout clock needs.
 */

#include <zephyr/kernel.h>

#include "pstop/time.h"

uint64_t time_get_now(void)
{
	return (uint64_t)k_uptime_get();
}
