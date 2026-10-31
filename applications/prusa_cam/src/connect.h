/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PRUSA_CAM_CONNECT_H_
#define PRUSA_CAM_CONNECT_H_

#include <stdint.h>

struct connect_status {
	int64_t last_ms;  /* uptime of the last attempt, 0 = none */
	int last_result;  /* HTTP status, or -errno */
	uint32_t uploads; /* successful snapshot uploads */
};

int connect_init(void);

/* Upload a snapshot now (and resend camera info) instead of waiting. */
void connect_kick(void);

void connect_get_status(struct connect_status *out);

#endif /* PRUSA_CAM_CONNECT_H_ */
