/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CODEX_HID_H_
#define CODEX_HID_H_

#include <stddef.h>

/*
 * Vendor-defined HID-over-GATT transport: usage page 0xFF00, report id 6, one
 * 63-byte input report and one 63-byte output report. JSON messages are framed
 * as [type=2][len][payload] and fragmented across reports.
 */

/* Longest JSON message that can be sent or reassembled, including the
 * terminating newline the host's framing expects.
 */
#define CODEX_JSON_MAX 512

/* Frame, fragment and notify `json`. Sleeps between fragments, so it must be
 * called from a thread. Returns 0, -ENOTCONN if the host is not subscribed, or
 * -EMSGSIZE if the message exceeds CODEX_JSON_MAX.
 */
int codex_hid_send(const char *json);

/* Provided by the application: called with one reassembled, NUL-terminated
 * JSON object received from the host.
 */
void codex_rpc_handle(const char *json);

#endif /* CODEX_HID_H_ */
