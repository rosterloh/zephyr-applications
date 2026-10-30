/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * The image's GNU build ID (see build_id.cmake), for anything that leaves the
 * device and has to be matched to its zephyr.elf later.
 */

#ifndef APP_BUILD_ID_H
#define APP_BUILD_ID_H

#include <stddef.h>
#include <stdint.h>

#ifdef CONFIG_APP_BUILD_ID

/* Points *id at the raw ID and returns its length (20 for SHA-1), or 0 if the
 * image carries no note. */
size_t app_build_id_get(const uint8_t **id);

/* The ID as lowercase hex, NUL-terminated; "" if there is none. */
const char *app_build_id_str(void);

#else

static inline size_t app_build_id_get(const uint8_t **id)
{
	*id = NULL;
	return 0;
}

static inline const char *app_build_id_str(void)
{
	return "";
}

#endif /* CONFIG_APP_BUILD_ID */

#endif /* APP_BUILD_ID_H */
