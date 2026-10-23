/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/linker/section_tags.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_backend_std.h>
#include <zephyr/logging/log_output.h>

#include "last_log.h"
#include "log_ring.h"

/* RTC slow memory is the one RAM on the ESP32-S3 the ROM bootloader leaves
 * alone across a reset. Elsewhere, plain no-init RAM.
 */
#if defined(CONFIG_SOC_SERIES_ESP32S3)
#define LAST_LOG_NOINIT __attribute__((section(".rtc_noinit")))
#else
#define LAST_LOG_NOINIT __noinit
#endif

static struct log_ring ring LAST_LOG_NOINIT;
static char snapshot[LOG_RING_SIZE];
static size_t snapshot_len;

static uint8_t line_buf[128];

static int ring_out(uint8_t *data, size_t length, void *ctx)
{
	ARG_UNUSED(ctx);
	log_ring_write(&ring, (const char *)data, length);
	return (int)length;
}

LOG_OUTPUT_DEFINE(last_log_output, ring_out, line_buf, sizeof(line_buf));

static void process(const struct log_backend *const backend, union log_msg_generic *msg)
{
	ARG_UNUSED(backend);
	log_output_msg_process(&last_log_output, &msg->log,
			       LOG_OUTPUT_FLAG_LEVEL | LOG_OUTPUT_FLAG_TIMESTAMP |
				       LOG_OUTPUT_FLAG_CRLF_LFONLY);
}

static void init(const struct log_backend *const backend)
{
	ARG_UNUSED(backend);
	/* Before anything of this boot lands in the ring. */
	snapshot_len = log_ring_copy(&ring, snapshot, sizeof(snapshot));
	log_ring_reset(&ring);
}

static void panic(const struct log_backend *const backend)
{
	ARG_UNUSED(backend);
	log_output_flush(&last_log_output);
}

static void dropped(const struct log_backend *const backend, uint32_t cnt)
{
	ARG_UNUSED(backend);
	log_output_dropped_process(&last_log_output, cnt);
}

static const struct log_backend_api last_log_api = {
	.process = process,
	.panic = panic,
	.init = init,
	.dropped = dropped,
};

LOG_BACKEND_DEFINE(last_log_backend, last_log_api, true);

size_t last_log_get(const char **data)
{
	*data = snapshot;
	return snapshot_len;
}
