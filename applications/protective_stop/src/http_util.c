/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/sys/base64.h>

#include "http_util.h"

int http_query_get(const char *url, const char *key, char *out, size_t out_len)
{
	const char *p;
	size_t key_len;

	if ((url == NULL) || (key == NULL) || (out == NULL) || (out_len == 0U)) {
		return -EINVAL;
	}
	out[0] = '\0';

	p = strchr(url, '?');
	if (p == NULL) {
		return -ENOENT;
	}
	p++;

	key_len = strlen(key);

	while (*p != '\0') {
		const char *end = strchr(p, '&');
		size_t pair_len = (end != NULL) ? (size_t)(end - p) : strlen(p);

		if ((pair_len > key_len) && (strncmp(p, key, key_len) == 0) &&
		    (p[key_len] == '=')) {
			const char *val = p + key_len + 1;
			size_t val_len = pair_len - key_len - 1U;

			if (val_len >= out_len) {
				return -ENOSPC;
			}
			(void)memcpy(out, val, val_len);
			out[val_len] = '\0';
			return 0;
		}

		if (end == NULL) {
			break;
		}
		p = end + 1;
	}

	return -ENOENT;
}

bool http_query_flag(const char *url, const char *key)
{
	char val[4];

	return (http_query_get(url, key, val, sizeof(val)) == 0) && (strcmp(val, "1") == 0);
}

int http_parse_ipv4(const char *s, uint32_t *out)
{
	uint32_t ip = 0U;

	if ((s == NULL) || (out == NULL)) {
		return -EINVAL;
	}

	for (int octet = 0; octet < 4; octet++) {
		uint32_t v = 0U;
		int digits = 0;

		while ((*s >= '0') && (*s <= '9')) {
			v = (v * 10U) + (uint32_t)(*s - '0');
			if ((v > 255U) || (++digits > 3)) {
				return -EINVAL;
			}
			s++;
		}
		if (digits == 0) {
			return -EINVAL;
		}

		ip = (ip << 8) | v;

		if (octet < 3) {
			if (*s != '.') {
				return -EINVAL;
			}
			s++;
		}
	}

	if (*s != '\0') {
		return -EINVAL;
	}

	*out = ip;
	return 0;
}

static int hex_digit(char c)
{
	if ((c >= '0') && (c <= '9')) {
		return c - '0';
	}
	if ((c >= 'a') && (c <= 'f')) {
		return c - 'a' + 10;
	}
	if ((c >= 'A') && (c <= 'F')) {
		return c - 'A' + 10;
	}
	return -1;
}

int http_parse_u32(const char *s, uint32_t *out)
{
	uint64_t v = 0U;
	uint32_t base = 10U;

	if ((s == NULL) || (out == NULL) || (*s == '\0')) {
		return -EINVAL;
	}

	if ((s[0] == '0') && ((s[1] == 'x') || (s[1] == 'X'))) {
		base = 16U;
		s += 2;
		if (*s == '\0') {
			return -EINVAL;
		}
	}

	for (; *s != '\0'; s++) {
		int d = hex_digit(*s);

		if ((d < 0) || ((uint32_t)d >= base)) {
			return -EINVAL;
		}
		v = (v * base) + (uint64_t)d;
		if (v > UINT32_MAX) {
			return -EINVAL;
		}
	}

	*out = (uint32_t)v;
	return 0;
}

bool http_basic_auth_ok(const char *header, const char *user, const char *password)
{
	static const char scheme[] = "Basic ";
	uint8_t decoded[128];
	size_t decoded_len;
	size_t user_len;
	size_t pw_len;
	uint8_t diff;

	if ((header == NULL) || (user == NULL) || (password == NULL)) {
		return false;
	}
	if (strncmp(header, scheme, sizeof(scheme) - 1U) != 0) {
		return false;
	}
	header += sizeof(scheme) - 1U;

	if (base64_decode(decoded, sizeof(decoded) - 1U, &decoded_len, (const uint8_t *)header,
			  strlen(header)) != 0) {
		return false;
	}

	user_len = strlen(user);
	pw_len = strlen(password);

	if ((decoded_len != (user_len + 1U + pw_len)) || (memcmp(decoded, user, user_len) != 0) ||
	    (decoded[user_len] != ':')) {
		return false;
	}

	diff = 0U;
	for (size_t i = 0U; i < pw_len; i++) {
		diff |= decoded[user_len + 1U + i] ^ (uint8_t)password[i];
	}
	return diff == 0U;
}
