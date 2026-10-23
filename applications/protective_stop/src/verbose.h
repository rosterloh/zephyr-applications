/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Runtime verbose logging (upstream's /admin/api/verbose): flips this app's
 * modules between info and debug without a rebuild. Debug adds a line per
 * machine reply, per HTTP request and per WiFi supervisor decision. Not
 * persisted: every boot starts quiet.
 */

#ifndef PROTECTIVE_STOP_VERBOSE_H
#define PROTECTIVE_STOP_VERBOSE_H

#include <stdbool.h>

/* Apply the quiet default. Call first thing in main(). */
void pstop_verbose_init(void);

bool pstop_verbose_get(void);

/* Returns 0, or -ENOTSUP when verbose logging is not built in. */
int pstop_verbose_set(bool on);

#endif /* PROTECTIVE_STOP_VERBOSE_H */
