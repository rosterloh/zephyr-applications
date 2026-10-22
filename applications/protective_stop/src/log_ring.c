/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "log_ring.h"

static uint32_t check_of(const struct log_ring *r)
{
	return ~(r->magic ^ r->head ^ r->wrapped);
}

void log_ring_reset(struct log_ring *r)
{
	r->magic = LOG_RING_MAGIC;
	r->head = 0U;
	r->wrapped = 0U;
	r->check = check_of(r);
}

bool log_ring_valid(const struct log_ring *r)
{
	return (r->magic == LOG_RING_MAGIC) && (r->head < LOG_RING_SIZE) && (r->wrapped <= 1U) &&
	       (r->check == check_of(r));
}

void log_ring_write(struct log_ring *r, const char *data, size_t len)
{
	uint32_t head = r->head;
	uint32_t wrapped = r->wrapped;

	/* Only the tail fits anyway. */
	if (len > LOG_RING_SIZE) {
		data += len - LOG_RING_SIZE;
		len = LOG_RING_SIZE;
	}

	while (len > 0U) {
		size_t n = LOG_RING_SIZE - head;

		if (n > len) {
			n = len;
		}
		(void)memcpy(&r->buf[head], data, n);
		data += n;
		len -= n;
		head += (uint32_t)n;
		if (head == LOG_RING_SIZE) {
			head = 0U;
			wrapped = 1U;
		}
	}

	r->head = head;
	r->wrapped = wrapped;
	r->check = check_of(r);
}

size_t log_ring_copy(const struct log_ring *r, char *out, size_t cap)
{
	size_t total;
	size_t start;
	size_t first;

	if (!log_ring_valid(r) || (cap == 0U)) {
		return 0U;
	}

	total = (r->wrapped != 0U) ? LOG_RING_SIZE : r->head;
	start = (r->wrapped != 0U) ? r->head : 0U;

	/* Keep the newest cap bytes. */
	if (total > cap) {
		start = (start + (total - cap)) % LOG_RING_SIZE;
		total = cap;
	}

	first = LOG_RING_SIZE - start;
	if (first > total) {
		first = total;
	}
	(void)memcpy(out, &r->buf[start], first);
	(void)memcpy(out + first, &r->buf[0], total - first);
	return total;
}
