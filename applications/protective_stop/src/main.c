/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * protective_stop: a Zephyr pstop REMOTE.
 *
 * Init order matters. Settings first (the peer table decides which sessions
 * exist), then the stop-switch GPIOs (so the first sampler tick reads real
 * pins), then the sessions, and only then the threads.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app_settings.h"
#include "estop_gpio.h"
#include "lockstep.h"
#include "pstop/config.h"

LOG_MODULE_REGISTER(pstop, LOG_LEVEL_INF);

/* Fallback peer, applied only when nothing has been provisioned yet. Matches
 * upstream's machine_app defaults so a fresh native_sim build talks to it
 * without any configuration. Slice 3's POST /api/pstop_peers replaces this.
 */
#define DEFAULT_MACHINE_IP   0x7F000001U /* 127.0.0.1 */
#define DEFAULT_MACHINE_PORT 8890U
#define DEFAULT_MACHINE_ID   0x01020304U
#define DEFAULT_DEVICE_ID    0xC0A80105U /* 192.168.1.5 */

/* Lockstep health counters are otherwise silent between /state.json polls
 * (slice 3). Logging on change here is enough to see a stuck-gate or a
 * mismatch spike without polling.
 */
#define COUNTER_LOG_PERIOD_MS 5000U

int main(void)
{
	int ret;

	LOG_INF("protective_stop remote: pstop v%u, %u-byte frames", (unsigned int)PSTOP_VERSION,
		(unsigned int)PSTOP_MESSAGE_SIZE);

	ret = app_settings_init();
	if (ret != 0) {
		LOG_ERR("settings init failed: %d", ret);
		return ret;
	}

	if (app_settings_peer(0)->configured == false) {
		LOG_WRN("no peer provisioned; using the built-in default");
		(void)app_settings_set_peer(0, DEFAULT_MACHINE_IP, DEFAULT_MACHINE_PORT,
					    DEFAULT_MACHINE_ID);
	}

	if (app_settings_device_id() == 0U) {
		/* Slice 4 derives this from the active uplink. Until then a
		 * pinned id keeps the machine's operator allowlist stable.
		 */
		LOG_WRN("no device id provisioned; using the built-in default");
		(void)app_settings_set_device_id(DEFAULT_DEVICE_ID);
	}

	ret = estop_gpio_init();
	if (ret != 0) {
		LOG_ERR("stop-switch gpio init failed: %d", ret);
		return ret;
	}

	ret = pstop_lockstep_init();
	if (ret != 0) {
		LOG_ERR("lockstep init failed: %d", ret);
		return ret;
	}

	ret = pstop_lockstep_start();
	if (ret != 0) {
		return ret;
	}

	uint32_t last_mismatches = 0;
	uint32_t last_gated_ticks = 0;

	while (true) {
		k_sleep(K_MSEC(COUNTER_LOG_PERIOD_MS));

		uint32_t mismatches = pstop_lockstep_mismatches();
		uint32_t gated_ticks = pstop_lockstep_gated_ticks();

		if ((mismatches != last_mismatches) || (gated_ticks != last_gated_ticks)) {
			LOG_INF("lockstep health: mismatches=%u gated_ticks=%u", mismatches,
				gated_ticks);
			last_mismatches = mismatches;
			last_gated_ticks = gated_ticks;
		}
	}
}
