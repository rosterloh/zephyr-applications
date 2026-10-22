/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PROTECTIVE_STOP_HTTP_SERVER_H
#define PROTECTIVE_STOP_HTTP_SERVER_H

/* Start the control plane on CONFIG_PROTECTIVE_STOP_HTTP_PORT. Call once
 * the sessions exist: the first request may arrive straight away.
 */
int pstop_http_start(void);

#endif /* PROTECTIVE_STOP_HTTP_SERVER_H */
