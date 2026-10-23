/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * The health counters' pure logic, ported case for case from upstream's
 * firmware/test/test_dcs_health_logic.c (blob codec, press and mismatch edge
 * detectors, wear thresholds, flush policy), plus the warning board and the
 * last_log ring.
 */

#include <string.h>

#include <zephyr/ztest.h>

#include "health_logic.h"
#include "log_ring.h"

#define OK   0x55U
#define STOP 0x92U

/* One tick as the comparator observer feeds it: channel 0, then 1. */
static struct health_edge_out tick(struct health_edge *st, uint32_t t, uint8_t v0, uint8_t v1)
{
	struct health_edge_out a = health_edge_step(st, 0, t, v0, STOP);
	struct health_edge_out b = health_edge_step(st, 1, t, v1, STOP);

	return (struct health_edge_out){.press = a.press || b.press,
					.mismatch = a.mismatch || b.mismatch};
}

ZTEST_SUITE(health_logic, NULL, NULL, NULL, NULL, NULL);

ZTEST(health_logic, test_codec_round_trip)
{
	struct health_counters c = {
		.presses = 0x01020304U,
		.mismatch_events = 7U,
		.uptime_s = 0xFFFFFFFFU,
		.boots = 41U,
		.flashes = 6U,
		.otas = 5U,
		.fw_sha = {0xde, 0xad, 0xbe, 0xef, 0x00, 0x11, 0x22, 0x33},
		.button_swaps = 2U,
	};
	struct health_counters d;
	uint8_t blob[HEALTH_BLOB_LEN];

	health_encode(&c, blob);
	zassert_equal(blob[0], HEALTH_BLOB_VER, "version byte");
	zassert_true((blob[1] == 0x01) && (blob[4] == 0x04), "u32 big-endian");

	(void)memset(&d, 0xAA, sizeof(d));
	zassert_true(health_decode(blob, sizeof(blob), &d), "decode");
	zassert_equal(d.presses, c.presses);
	zassert_equal(d.mismatch_events, c.mismatch_events);
	zassert_equal(d.uptime_s, c.uptime_s);
	zassert_equal(d.boots, c.boots);
	zassert_equal(d.flashes, c.flashes);
	zassert_equal(d.otas, c.otas);
	zassert_mem_equal(d.fw_sha, c.fw_sha, sizeof(c.fw_sha));
	zassert_equal(d.button_swaps, c.button_swaps);

	zassert_false(health_decode(blob, sizeof(blob) - 1U, &d), "short blob rejected");
	zassert_false(health_decode(NULL, sizeof(blob), &d), "NULL rejected");
	blob[0] = 99U;
	zassert_false(health_decode(blob, sizeof(blob), &d), "wrong version rejected");
}

ZTEST(health_logic, test_boot_priming_is_not_a_press)
{
	struct health_edge st = {0};
	struct health_edge_out a;
	struct health_edge_out b;

	for (uint32_t t = 1U; t <= 5U; t++) {
		struct health_edge_out o = tick(&st, t, STOP, STOP);

		zassert_false(o.press || o.mismatch, "priming STOP counted at tick %u", t);
	}
	(void)tick(&st, 6U, OK, OK);

	a = health_edge_step(&st, 0, 7U, STOP, STOP);
	b = health_edge_step(&st, 1, 7U, STOP, STOP);
	zassert_false(a.press || a.mismatch, "channel 0 alone (skewed tick): no verdict yet");
	zassert_true(b.press && !b.mismatch, "channel 1 aligns: press counted");

	zassert_false(tick(&st, 8U, STOP, STOP).press, "held: no press");
	zassert_false(tick(&st, 9U, OK, OK).press, "release: not a press");
}

ZTEST(health_logic, test_latched_at_boot_is_baseline)
{
	struct health_edge st = {0};
	int presses = 0;

	/* Channel 1 reports first this time. */
	presses += health_edge_step(&st, 1, 1U, STOP, STOP).press;
	presses += health_edge_step(&st, 0, 1U, STOP, STOP).press;
	zassert_equal(presses, 0, "latched at boot: baseline only");

	for (uint32_t i = 0U; i < 3U; i++) {
		uint32_t t = 10U + (10U * i);

		presses += tick(&st, t, OK, OK).press;
		presses += tick(&st, t + 1U, STOP, STOP).press;
		presses += tick(&st, t + 2U, STOP, STOP).press;
	}
	zassert_equal(presses, 3, "three release+press cycles");
}

ZTEST(health_logic, test_mismatch_edges)
{
	struct health_edge st = {0};
	struct health_edge_out o;

	(void)tick(&st, 0U, OK, OK);

	o = tick(&st, 1U, STOP, OK);
	zassert_true(o.mismatch && !o.press, "one loop open: mismatch, not press");
	o = tick(&st, 2U, STOP, OK);
	zassert_false(o.mismatch, "sustained disagreement: one event");
	o = tick(&st, 3U, OK, OK);
	zassert_false(o.mismatch || o.press, "recovered");
	o = tick(&st, 4U, OK, STOP);
	zassert_true(o.mismatch, "other loop open: a new event");
	o = tick(&st, 5U, STOP, STOP);
	zassert_true(o.press && !o.mismatch, "second loop follows: one press");

	o = health_edge_step(&st, 2, 6U, STOP, STOP);
	zassert_false(o.press || o.mismatch, "bad channel ignored");
}

ZTEST(health_logic, test_button_wear)
{
	uint32_t pct = 123U;

	zassert_equal(health_button_level(0U, 100000U, 80U, &pct), HEALTH_OK);
	zassert_equal(pct, 0U);
	zassert_equal(health_button_level(79999U, 100000U, 80U, &pct), HEALTH_OK);
	zassert_equal(pct, 79U);
	zassert_equal(health_button_level(80000U, 100000U, 80U, &pct), HEALTH_WARN);
	zassert_equal(pct, 80U);
	zassert_equal(health_button_level(100000U, 100000U, 80U, &pct), HEALTH_CRIT);
	zassert_equal(pct, 100U);
	zassert_equal(health_button_level(5000000U, 100000U, 80U, &pct), HEALTH_CRIT);
	zassert_equal(pct, 999U, "percentage clamps at 999");
	zassert_equal(health_button_level(1000U, 0U, 80U, &pct), HEALTH_OK, "rating 0: disabled");
	zassert_equal(pct, 0U);
	zassert_equal(health_button_level(90000U, 100000U, 0U, &pct), HEALTH_OK,
		      "warn_pct 0: no warning below 100%");
	zassert_equal(health_button_level(UINT32_MAX, 100000U, 80U, NULL), HEALTH_CRIT,
		      "no overflow at UINT32_MAX");
}

ZTEST(health_logic, test_mismatch_rate)
{
	zassert_equal(health_mismatch_level(49U, 10U, 50U, 20U), HEALTH_OK, "below min_events");
	zassert_equal(health_mismatch_level(50U, 10U, 50U, 20U), HEALTH_WARN,
		      "50 on 10 presses (basis 1000)");
	zassert_equal(health_mismatch_level(50U, 5000U, 50U, 20U), HEALTH_OK, "allowed 100");
	zassert_equal(health_mismatch_level(101U, 5000U, 50U, 20U), HEALTH_WARN, "101 > 100");
	zassert_equal(health_mismatch_level(100U, 5000U, 50U, 20U), HEALTH_OK, "100 == allowed");
	zassert_equal(health_mismatch_level(1000000U, 0U, 50U, 0U), HEALTH_OK, "per_1000 0: off");
}

ZTEST(health_logic, test_flush_policy)
{
	zassert_false(health_flush_due(false, 0U, 60U, 600U), "clean, fresh");
	zassert_false(health_flush_due(true, 59U, 60U, 600U), "dirty, 59 s");
	zassert_true(health_flush_due(true, 60U, 60U, 600U), "dirty, 60 s");
	zassert_false(health_flush_due(false, 599U, 60U, 600U), "clean, 599 s");
	zassert_true(health_flush_due(false, 600U, 60U, 600U), "clean, 600 s: uptime");
	zassert_false(health_flush_due(true, 600U, 3600U, 600U), "min > max: throttle wins");
	zassert_false(health_flush_due(false, 3599U, 3600U, 600U), "min > max: 3599 s");
	zassert_true(health_flush_due(false, 3600U, 3600U, 600U), "min > max: 3600 s");
}

ZTEST(health_logic, test_warning_board)
{
	struct health_board b = {0};
	char src[HEALTH_SRC_LEN];

	zassert_ok(health_board_publish(&b, "button_wear", HEALTH_WARN, "80%", 1000U));
	zassert_ok(health_board_publish(&b, "button_wear", HEALTH_CRIT, "100%", 2000U));
	zassert_equal(b.n, 1, "upsert: one entry per source");
	zassert_equal(b.w[0].count, 2U);
	zassert_equal(b.w[0].first_ms, 1000U, "first raised kept");
	zassert_equal(b.w[0].last_ms, 2000U);
	zassert_str_equal(b.w[0].msg, "100%");
	zassert_equal(health_board_level(&b), HEALTH_CRIT);

	zassert_ok(health_board_publish(&b, "loop_mismatch", HEALTH_WARN, "x", 3000U));
	zassert_ok(health_board_publish(&b, "button_wear", HEALTH_OK, "", 4000U));
	zassert_equal(b.n, 1, "level OK clears the source");
	zassert_str_equal(b.w[0].src, "loop_mismatch");
	zassert_equal(health_board_level(&b), HEALTH_WARN);

	health_board_clear(&b, "loop_mismatch");
	health_board_clear(&b, "never_raised");
	zassert_equal(b.n, 0);
	zassert_equal(health_board_level(&b), HEALTH_OK);

	for (int i = 0; i < HEALTH_MAX_WARNINGS; i++) {
		(void)snprintf(src, sizeof(src), "src%d", i);
		zassert_ok(health_board_publish(&b, src, HEALTH_WARN, "m", 0U));
	}
	zassert_equal(health_board_publish(&b, "one_more", HEALTH_WARN, "m", 0U), -ENOMEM);
	zassert_equal(b.dropped, 1U, "a full table counts the refusal");
	zassert_ok(health_board_publish(&b, "src3", HEALTH_CRIT, "m", 0U),
		   "an existing source still updates when full");
}

ZTEST_SUITE(log_ring, NULL, NULL, NULL, NULL, NULL);

static struct log_ring ring;
static char out[LOG_RING_SIZE + 16];

ZTEST(log_ring, test_garbage_reads_empty)
{
	(void)memset(&ring, 0xA5, sizeof(ring));
	zassert_false(log_ring_valid(&ring), "power-on garbage");
	zassert_equal(log_ring_copy(&ring, out, sizeof(out)), 0U);

	log_ring_reset(&ring);
	zassert_true(log_ring_valid(&ring));
	zassert_equal(log_ring_copy(&ring, out, sizeof(out)), 0U, "valid but empty");

	ring.head = 5U; /* torn: header no longer checks out */
	zassert_false(log_ring_valid(&ring));
}

ZTEST(log_ring, test_write_and_copy)
{
	log_ring_reset(&ring);
	log_ring_write(&ring, "hello ", 6U);
	log_ring_write(&ring, "world\n", 6U);
	zassert_equal(log_ring_copy(&ring, out, sizeof(out)), 12U);
	zassert_mem_equal(out, "hello world\n", 12U);

	zassert_equal(log_ring_copy(&ring, out, 6U), 6U, "cap keeps the newest");
	zassert_mem_equal(out, "world\n", 6U);
}

ZTEST(log_ring, test_wrap_keeps_newest_in_order)
{
	char line[100];
	size_t n;

	log_ring_reset(&ring);
	/* 100 lines of 100 bytes: wraps the 4 KiB ring twice over. */
	for (int i = 0; i < 100; i++) {
		(void)memset(line, 'a' + (i % 26), sizeof(line));
		line[sizeof(line) - 1U] = '\n';
		log_ring_write(&ring, line, sizeof(line));
	}
	zassert_true(log_ring_valid(&ring));

	n = log_ring_copy(&ring, out, sizeof(out));
	zassert_equal(n, LOG_RING_SIZE, "full ring");
	/* The newest line (i = 99, 'v') is last and intact. */
	zassert_equal(out[n - 1U], '\n');
	zassert_equal(out[n - 2U], 'a' + (99 % 26));
	zassert_equal(out[n - 100U], 'a' + (99 % 26));
	zassert_equal(out[n - 101U], '\n', "line boundary before it");
	zassert_equal(out[n - 102U], 'a' + (98 % 26), "the line before is older");
}

ZTEST(log_ring, test_oversized_write_keeps_tail)
{
	static char big[LOG_RING_SIZE + 10];

	for (size_t i = 0; i < sizeof(big); i++) {
		big[i] = (char)('0' + (i % 10));
	}
	log_ring_reset(&ring);
	log_ring_write(&ring, big, sizeof(big));
	zassert_equal(log_ring_copy(&ring, out, sizeof(out)), LOG_RING_SIZE);
	zassert_mem_equal(out, &big[10], LOG_RING_SIZE, "the last 4 KiB, in order");
}
