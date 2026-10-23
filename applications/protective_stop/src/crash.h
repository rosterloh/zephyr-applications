/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * The coredump a fatal error leaves in the flash coredump partition, for
 * /api/coredump and /state.json's crash_present. It persists until the next
 * crash overwrites it. Builds without a flash coredump backend (native_sim)
 * never have one.
 */

#ifndef PROTECTIVE_STOP_CRASH_H
#define PROTECTIVE_STOP_CRASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Size of the stored dump in bytes, 0 if there is none. */
size_t crash_size(void);

/* Copy up to len bytes of the stored dump from offset. Returns the count
 * copied or -errno.
 */
int crash_read(size_t offset, uint8_t *buf, size_t len);

#endif /* PROTECTIVE_STOP_CRASH_H */
