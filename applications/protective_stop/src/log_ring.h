/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * The byte ring behind /api/last_log (upstream's panic_log.c): every log
 * line is appended to a ring that lives in memory the next boot does not
 * clear, so after a crash, a watchdog or an admin restart the previous
 * boot's tail can still be read. Pure functions over the struct, so the
 * wrap, validation and copy-out rules are unit-tested; last_log.c places it
 * and feeds it.
 *
 * Uninitialised memory is the starting point on every cold boot, so nothing
 * here trusts the header: a bad magic or out-of-range head reads as empty.
 */

#ifndef PROTECTIVE_STOP_LOG_RING_H
#define PROTECTIVE_STOP_LOG_RING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LOG_RING_MAGIC 0x4A07BCDEU
#define LOG_RING_SIZE  4096U

struct log_ring {
	uint32_t magic;
	uint32_t head;    /* next write offset, < LOG_RING_SIZE */
	uint32_t wrapped; /* nonzero once the ring has wrapped */
	uint32_t check;   /* ~(magic ^ head ^ wrapped): catches a torn header */
	char buf[LOG_RING_SIZE];
};

/* Start empty. */
void log_ring_reset(struct log_ring *r);

/* True if the header is self-consistent, i.e. this is a ring a previous boot
 * wrote rather than power-on garbage.
 */
bool log_ring_valid(const struct log_ring *r);

void log_ring_write(struct log_ring *r, const char *data, size_t len);

/* Copy the ring out oldest-first into out (no NUL added). Returns the byte
 * count: 0 for an invalid ring. At most cap bytes, keeping the newest.
 */
size_t log_ring_copy(const struct log_ring *r, char *out, size_t cap);

#endif /* PROTECTIVE_STOP_LOG_RING_H */
