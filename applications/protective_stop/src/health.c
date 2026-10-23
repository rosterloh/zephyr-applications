/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>

#include "fw_info.h"
#include "health.h"
#include "lockstep.h"
#include "pstop/pstop_msg.h"
#include "pstop_log.h"

LOG_MODULE_REGISTER(pstop_health, PSTOP_LOG_LEVEL);

/* The unit suites build without the app's Kconfig; they get upstream's
 * defaults.
 */
#ifndef CONFIG_PROTECTIVE_STOP_BUTTON_RATED_OPS
#define CONFIG_PROTECTIVE_STOP_BUTTON_RATED_OPS       100000
#define CONFIG_PROTECTIVE_STOP_BUTTON_WARN_PCT        80
#define CONFIG_PROTECTIVE_STOP_MISMATCH_WARN_MIN      50
#define CONFIG_PROTECTIVE_STOP_MISMATCH_WARN_PER_1000 20
#define CONFIG_PROTECTIVE_STOP_HEALTH_FLUSH_MIN_S     60
#define CONFIG_PROTECTIVE_STOP_HEALTH_FLUSH_MAX_S     600
#endif

#define HEALTH_SETTINGS_KEY "pstop_health/counters"

/* Guards everything below except the edge state and the two deltas, which
 * belong to the comparator thread's observer: it must never wait on the
 * HTTP thread, so it only bumps atomics.
 */
static K_MUTEX_DEFINE(lock);
static struct health_counters counters;
static struct health_board board;
static bool dirty;
static uint32_t uptime_at_boot_s;
static int64_t last_flush_ms;
static uint32_t flushes;
static uint32_t flush_fails;

static struct health_edge edge;
static atomic_t press_delta;
static atomic_t mismatch_delta;

static uint8_t loaded_blob[HEALTH_BLOB_LEN];
static bool loaded;

static int health_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	const char *next;

	if (settings_name_steq(name, "counters", &next) && !next) {
		if (len != sizeof(loaded_blob)) {
			return -EINVAL;
		}
		if (read_cb(cb_arg, loaded_blob, sizeof(loaded_blob)) < 0) {
			return -EIO;
		}
		loaded = true;
		return 0;
	}
	return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(pstop_health, "pstop_health", NULL, health_settings_set, NULL, NULL);

/* FNV-1a over the version and build strings: enough to tell one build
 * from the next, which is all "flashes" needs.
 */
static void fw_identity(uint8_t out[8])
{
	static const char id[] = APP_VERSION_STRING " " FW_SHA;
	uint64_t h = 0xCBF29CE484222325ULL;

	for (size_t i = 0; i < (sizeof(id) - 1U); i++) {
		h ^= (uint8_t)id[i];
		h *= 0x100000001B3ULL;
	}
	for (int i = 0; i < 8; i++) {
		out[i] = (uint8_t)(h >> (56 - (8 * i)));
	}
}

static void observe(uint32_t tick, uint8_t verdict0, uint8_t verdict1)
{
	struct health_edge_out e;

	(void)health_edge_step(&edge, 0, tick, verdict0, PSTOP_MESSAGE_STOP);
	e = health_edge_step(&edge, 1, tick, verdict1, PSTOP_MESSAGE_STOP);
	if (e.press) {
		(void)atomic_inc(&press_delta);
	}
	if (e.mismatch) {
		(void)atomic_inc(&mismatch_delta);
	}
}

static uint32_t uptime_s(void)
{
	return (uint32_t)(k_uptime_get() / MSEC_PER_SEC);
}

/* Callers hold lock. */
static void fold_locked(void)
{
	uint32_t p = (uint32_t)atomic_clear(&press_delta);
	uint32_t m = (uint32_t)atomic_clear(&mismatch_delta);

	if ((p != 0U) || (m != 0U)) {
		counters.presses += p;
		counters.mismatch_events += m;
		dirty = true;
	}
	counters.uptime_s = uptime_at_boot_s + uptime_s();
}

static int flush_locked(void)
{
	uint8_t blob[HEALTH_BLOB_LEN];
	int ret;

	fold_locked();
	health_encode(&counters, blob);
	ret = settings_save_one(HEALTH_SETTINGS_KEY, blob, sizeof(blob));
	last_flush_ms = k_uptime_get();
	if (ret != 0) {
		flush_fails++;
		LOG_WRN("health flush failed: %d", ret);
		return ret;
	}
	flushes++;
	dirty = false;
	return 0;
}

static void evaluate_locked(void)
{
	char msg[HEALTH_MSG_LEN];
	uint32_t pct;
	int level;
	uint64_t now = (uint64_t)k_uptime_get();

	level = health_button_level(counters.presses, CONFIG_PROTECTIVE_STOP_BUTTON_RATED_OPS,
				    CONFIG_PROTECTIVE_STOP_BUTTON_WARN_PCT, &pct);
	(void)snprintf(msg, sizeof(msg), "%u%% of rated %u ops", pct,
		       (unsigned int)CONFIG_PROTECTIVE_STOP_BUTTON_RATED_OPS);
	(void)health_board_publish(&board, "button_wear", level, msg, now);

	level = health_mismatch_level(counters.mismatch_events, counters.presses,
				      CONFIG_PROTECTIVE_STOP_MISMATCH_WARN_MIN,
				      CONFIG_PROTECTIVE_STOP_MISMATCH_WARN_PER_1000);
	(void)snprintf(msg, sizeof(msg), "%u loop mismatches in %u presses",
		       counters.mismatch_events, counters.presses);
	(void)health_board_publish(&board, "loop_mismatch", level, msg, now);
}

int health_init(void)
{
	uint8_t fw[8];
	int ret;

	(void)memset(&counters, 0, sizeof(counters));
	(void)memset(&board, 0, sizeof(board));
	(void)memset(&edge, 0, sizeof(edge));
	atomic_clear(&press_delta);
	atomic_clear(&mismatch_delta);
	flushes = 0U;
	flush_fails = 0U;
	loaded = false;

	ret = settings_load_subtree("pstop_health");
	if (ret != 0) {
		LOG_WRN("health counters not loaded (%d): starting from zero", ret);
	}
	if (loaded && !health_decode(loaded_blob, sizeof(loaded_blob), &counters)) {
		LOG_WRN("health counters unreadable: starting from zero");
		(void)memset(&counters, 0, sizeof(counters));
	}

	(void)k_mutex_lock(&lock, K_FOREVER);
	counters.boots++;
	fw_identity(fw);
	if (memcmp(fw, counters.fw_sha, sizeof(fw)) != 0) {
		counters.flashes++;
		(void)memcpy(counters.fw_sha, fw, sizeof(fw));
	}
	uptime_at_boot_s = counters.uptime_s - uptime_s();
	evaluate_locked();
	ret = flush_locked();
	(void)k_mutex_unlock(&lock);

	LOG_INF("health: boot %u, %u presses, %u mismatch events, %u flashes", counters.boots,
		counters.presses, counters.mismatch_events, counters.flashes);

	pstop_lockstep_set_tick_observer(observe);
	return ret;
}

void health_poll(void)
{
	(void)k_mutex_lock(&lock, K_FOREVER);
	fold_locked();
	evaluate_locked();
	if (health_flush_due(dirty, (uint32_t)((k_uptime_get() - last_flush_ms) / MSEC_PER_SEC),
			     CONFIG_PROTECTIVE_STOP_HEALTH_FLUSH_MIN_S,
			     CONFIG_PROTECTIVE_STOP_HEALTH_FLUSH_MAX_S)) {
		(void)flush_locked();
	}
	(void)k_mutex_unlock(&lock);
}

int health_flush(void)
{
	int ret;

	(void)k_mutex_lock(&lock, K_FOREVER);
	ret = flush_locked();
	(void)k_mutex_unlock(&lock);
	return ret;
}

int health_publish(const char *src, int level, const char *msg)
{
	int ret;

	(void)k_mutex_lock(&lock, K_FOREVER);
	ret = health_board_publish(&board, src, level, msg, (uint64_t)k_uptime_get());
	(void)k_mutex_unlock(&lock);
	return ret;
}

int health_level(void)
{
	int level;

	(void)k_mutex_lock(&lock, K_FOREVER);
	level = health_board_level(&board);
	(void)k_mutex_unlock(&lock);
	return level;
}

void health_snapshot_get(struct health_snapshot *out)
{
	(void)k_mutex_lock(&lock, K_FOREVER);
	fold_locked();
	out->counters = counters;
	out->board = board;
	out->level = health_board_level(&board);
	out->button_rated_ops = CONFIG_PROTECTIVE_STOP_BUTTON_RATED_OPS;
	(void)health_button_level(counters.presses, CONFIG_PROTECTIVE_STOP_BUTTON_RATED_OPS,
				  CONFIG_PROTECTIVE_STOP_BUTTON_WARN_PCT, &out->button_wear_pct);
	out->flushes = flushes;
	out->flush_fails = flush_fails;
	out->last_flush_age_s = (uint32_t)((k_uptime_get() - last_flush_ms) / MSEC_PER_SEC);
	(void)k_mutex_unlock(&lock);
}

int health_reset_button(void)
{
	int ret;

	(void)k_mutex_lock(&lock, K_FOREVER);
	fold_locked();
	counters.presses = 0U;
	counters.mismatch_events = 0U;
	if (counters.button_swaps < UINT8_MAX) {
		counters.button_swaps++;
	}
	evaluate_locked();
	ret = flush_locked();
	(void)k_mutex_unlock(&lock);
	LOG_INF("health: button counters reset (swap %u)", counters.button_swaps);
	return ret;
}

int health_reset_all(void)
{
	uint8_t fw[8];
	int ret;

	(void)k_mutex_lock(&lock, K_FOREVER);
	(void)atomic_clear(&press_delta);
	(void)atomic_clear(&mismatch_delta);
	(void)memset(&counters, 0, sizeof(counters));
	/* Keep recognising this build, or the next boot counts a reflash. */
	fw_identity(fw);
	(void)memcpy(counters.fw_sha, fw, sizeof(fw));
	uptime_at_boot_s = 0U - uptime_s();
	evaluate_locked();
	ret = flush_locked();
	(void)k_mutex_unlock(&lock);
	LOG_INF("health: all counters reset");
	return ret;
}
