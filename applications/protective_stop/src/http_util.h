/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Request parsing for the control plane: query strings, dotted-quad
 * addresses, numbers and HTTP Basic credentials. Pure functions over strings,
 * so every edge case is unit-tested without a socket.
 */

#ifndef PROTECTIVE_STOP_HTTP_UTIL_H
#define PROTECTIVE_STOP_HTTP_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Copy the value of `key` from the query string of `url` ("/path?a=1&b=2")
 * into out, NUL-terminated. Values are taken verbatim, without
 * percent-decoding: every parameter this API takes is a number, an address
 * or a fixed word. Returns 0, -ENOENT if the key is absent, or -ENOSPC if the
 * value does not fit (out is then empty, never truncated).
 */
int http_query_get(const char *url, const char *key, char *out, size_t out_len);

/* True if `key` is present and its value is exactly "1". */
bool http_query_flag(const char *url, const char *key);

/* Parse "A.B.C.D" into a host-order IPv4 address. Rejects anything else:
 * out-of-range octets, missing or extra octets, trailing characters.
 * Returns 0 or -EINVAL.
 */
int http_parse_ipv4(const char *s, uint32_t *out);

/* Parse an unsigned 32-bit number, accepting decimal or a 0x-prefixed hex
 * form (upstream's ?id= is written 0x01020304). The whole string must be
 * consumed. Returns 0 or -EINVAL.
 */
int http_parse_u32(const char *s, uint32_t *out);

/* Check an Authorization header value ("Basic <base64 user:password>")
 * against the expected credentials. NULL or malformed input is a rejection.
 * The password comparison does not stop at the first differing byte.
 */
bool http_basic_auth_ok(const char *header, const char *user, const char *password);

#endif /* PROTECTIVE_STOP_HTTP_UTIL_H */
