/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/debug/coredump.h>

#include "crash.h"

#ifdef CONFIG_DEBUG_COREDUMP_BACKEND_FLASH_PARTITION

size_t crash_size(void)
{
	int size;

	if (coredump_query(COREDUMP_QUERY_HAS_STORED_DUMP, NULL) != 1) {
		return 0U;
	}
	size = coredump_query(COREDUMP_QUERY_GET_STORED_DUMP_SIZE, NULL);
	return (size > 0) ? (size_t)size : 0U;
}

int crash_read(size_t offset, uint8_t *buf, size_t len)
{
	struct coredump_cmd_copy_arg arg = {
		.offset = (off_t)offset,
		.buffer = buf,
		.length = len,
	};

	return coredump_cmd(COREDUMP_CMD_COPY_STORED_DUMP, &arg);
}

#else

size_t crash_size(void)
{
	return 0U;
}

int crash_read(size_t offset, uint8_t *buf, size_t len)
{
	ARG_UNUSED(offset);
	ARG_UNUSED(buf);
	ARG_UNUSED(len);
	return -ENOTSUP;
}

#endif
