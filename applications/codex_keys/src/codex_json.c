/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "codex_json.h"

static const char *skip_ws(const char *p)
{
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
		p++;
	}

	return p;
}

/* Advance past a JSON string starting at the opening quote. NULL if unterminated. */
static const char *skip_string(const char *p)
{
	p++; /* opening quote */

	while (*p != '"') {
		if (*p == '\0') {
			return NULL;
		}
		if (*p == '\\' && p[1] != '\0') {
			p++;
		}
		p++;
	}

	return p + 1;
}

/* Advance past any JSON value. NULL if malformed or truncated. */
static const char *skip_value(const char *p)
{
	if (*p == '"') {
		return skip_string(p);
	}

	if (*p == '{' || *p == '[') {
		int depth = 0;

		do {
			if (*p == '"') {
				p = skip_string(p);
				if (p == NULL) {
					return NULL;
				}
				continue;
			}
			if (*p == '{' || *p == '[') {
				depth++;
			} else if (*p == '}' || *p == ']') {
				depth--;
			} else if (*p == '\0') {
				return NULL;
			}
			p++;
		} while (depth > 0);

		return p;
	}

	/* Number, true, false or null: runs until a structural character. */
	while (*p != '\0' && strchr(",}] \t\n\r", *p) == NULL) {
		p++;
	}

	return p;
}

const char *codex_json_member(const char *json, const char *key, size_t *len)
{
	const char *p = skip_ws(json);
	size_t key_len = strlen(key);

	if (*p != '{') {
		return NULL;
	}
	p++;

	while (true) {
		p = skip_ws(p);
		if (*p != '"') {
			return NULL; /* end of object, or malformed */
		}

		const char *name = p + 1;
		const char *name_end = skip_string(p);

		if (name_end == NULL) {
			return NULL;
		}

		p = skip_ws(name_end);
		if (*p != ':') {
			return NULL;
		}

		const char *value = skip_ws(p + 1);
		const char *value_end = skip_value(value);

		if (value_end == NULL) {
			return NULL;
		}

		size_t name_len = (size_t)(name_end - name) - 1;

		if (name_len == key_len && memcmp(name, key, key_len) == 0) {
			*len = (size_t)(value_end - value);
			return value;
		}

		p = skip_ws(value_end);
		if (*p != ',') {
			return NULL;
		}
		p++;
	}
}

bool codex_json_str_eq(const char *val, size_t len, const char *expect)
{
	size_t expect_len = strlen(expect);

	if (val == NULL || len != expect_len + 2 || val[0] != '"') {
		return false;
	}

	return memcmp(val + 1, expect, expect_len) == 0;
}
