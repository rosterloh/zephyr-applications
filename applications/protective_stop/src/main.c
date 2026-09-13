/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "pstop/config.h"
#include "pstop/time.h"

LOG_MODULE_REGISTER(pstop, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("protective_stop: pstop v%u, %u-byte frames, clock=%llu ms",
		(unsigned int)PSTOP_VERSION, (unsigned int)PSTOP_MESSAGE_SIZE,
		(unsigned long long)time_get_now());
	return 0;
}
