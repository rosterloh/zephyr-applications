/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PRUSA_CAM_PRUSALINK_H_
#define PRUSA_CAM_PRUSALINK_H_

#include <stdbool.h>
#include <stddef.h>

#include "cfg.h"

#define PRUSALINK_STATE_MAX sizeof("ATTENTION")

/*
 * GET /api/v1/status from the printer's PrusaLink and copy printer.state
 * (IDLE, BUSY, PRINTING, PAUSED, FINISHED, STOPPED, ERROR, ATTENTION, READY).
 * Returns 0, an HTTP status other than 200, or -errno.
 */
int prusalink_state(const struct cfg_printer *p, char state[PRUSALINK_STATE_MAX]);

/* A print is under way: PRINTING, PAUSED, or ATTENTION (waiting on the user mid-print). */
bool prusalink_printing(const char *state);

#endif /* PRUSA_CAM_PRUSALINK_H_ */
