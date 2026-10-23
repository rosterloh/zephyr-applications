/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/logging/log_ctrl.h>

#include "pstop_log.h"
#include "verbose.h"

static bool verbose;

#if defined(CONFIG_PROTECTIVE_STOP_VERBOSE) && defined(CONFIG_LOG_RUNTIME_FILTERING)

/* This app's modules: everything registered from src/. */
static bool ours(const char *name)
{
	static const char *const prefixes[] = {"pstop", "app_settings", "lockstep", "estop_gpio"};

	for (size_t i = 0; i < ARRAY_SIZE(prefixes); i++) {
		if (strncmp(name, prefixes[i], strlen(prefixes[i])) == 0) {
			return true;
		}
	}
	return false;
}

static void apply(bool on)
{
	uint32_t n = log_src_cnt_get(Z_LOG_LOCAL_DOMAIN_ID);

	for (uint32_t i = 0; i < n; i++) {
		const char *name = log_source_name_get(Z_LOG_LOCAL_DOMAIN_ID, i);

		if ((name != NULL) && ours(name)) {
			(void)log_filter_set(NULL, Z_LOG_LOCAL_DOMAIN_ID, (int16_t)i,
					     on ? LOG_LEVEL_DBG : LOG_LEVEL_INF);
		}
	}
	verbose = on;
}

void pstop_verbose_init(void)
{
	apply(false);
}

int pstop_verbose_set(bool on)
{
	apply(on);
	return 0;
}

#else

void pstop_verbose_init(void)
{
}

int pstop_verbose_set(bool on)
{
	return on ? -ENOTSUP : 0;
}

#endif

bool pstop_verbose_get(void)
{
	return verbose;
}
