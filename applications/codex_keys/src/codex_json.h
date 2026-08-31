/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Just enough JSON to dispatch a flat JSON-RPC object. Deliberately free of
 * Zephyr dependencies so it can be host-compiled by tests/codex_keys/.
 */

#ifndef CODEX_JSON_H_
#define CODEX_JSON_H_

#include <stdbool.h>
#include <stddef.h>

/*
 * Look up `key` among the *top-level* members of the JSON object `json` and
 * return a pointer into `json` at the raw text of its value, with the length
 * written to `len`. Nested members are skipped, so the "id" inside a params
 * array cannot shadow the request id.
 *
 * The value is returned verbatim (quotes included for strings, no unescaping)
 * which is exactly what a response needs in order to echo an id back. Returns
 * NULL if the key is absent or the object is malformed.
 */
const char *codex_json_member(const char *json, const char *key, size_t *len);

/* True if the raw value at `val` is the JSON string "expect". */
bool codex_json_str_eq(const char *val, size_t len, const char *expect);

#endif /* CODEX_JSON_H_ */
