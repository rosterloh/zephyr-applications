/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * protective_stop: a Zephyr pstop REMOTE.
 *
 * Init order matters. Settings first (the peer table decides which sessions
 * exist), then the health counters (they hook the comparator before it
 * starts), the stop-switch GPIOs (so the first sampler tick reads real pins),
 * then the sessions, and only then the threads. The uplinks, LEDs and
 * control plane come last: the remote stops machines without any of them.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app_settings.h"
#include "estop_gpio.h"
#include "health.h"
#include "http_server.h"
#include "leds.h"
#include "lockstep.h"
#include "pstop/config.h"
#include "pstop_log.h"
#include "uplink.h"
#include "verbose.h"
#include "wifi.h"

LOG_MODULE_REGISTER(pstop, PSTOP_LOG_LEVEL);

/* Fallback peer, applied only when nothing has been provisioned yet. Matches
 * upstream's machine_app defaults so a fresh native_sim build talks to it
 * without any configuration. POST /api/pstop_peers overrides it.
 */
#define DEFAULT_MACHINE_IP   0x7F000001U /* 127.0.0.1 */
#define DEFAULT_MACHINE_PORT 8890U
#define DEFAULT_MACHINE_ID   0x01020304U

/* Lockstep health counters are otherwise silent between /state.json polls.
 * Logging on change here is enough to see a stuck-gate or a mismatch spike
 * without polling. The same period paces the wear counters' flushes.
 */
#define COUNTER_LOG_PERIOD_MS 5000U

int main(void)
{
	int ret;

	pstop_verbose_init();

	LOG_INF("protective_stop remote: pstop v%u, %u-byte frames", (unsigned int)PSTOP_VERSION,
		(unsigned int)PSTOP_MESSAGE_SIZE);

	ret = app_settings_init();
	if (ret != 0) {
		LOG_ERR("settings init failed: %d", ret);
		return ret;
	}

	struct pstop_peer peer0;

	(void)app_settings_peer_get(0, &peer0);
	if (!peer0.configured) {
		LOG_WRN("no peer provisioned; using the built-in default");
		(void)app_settings_set_peer(0, DEFAULT_MACHINE_IP, DEFAULT_MACHINE_PORT,
					    DEFAULT_MACHINE_ID);
	}

	LOG_INF("device id %08x (%s)", app_settings_effective_device_id(),
		(app_settings_device_id() != 0U) ? "pinned" : "from the MAC");

	/* Wear counting is not a safety function: a failed load starts from
	 * zero and the remote runs on.
	 */
	(void)health_init();

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

	/* None of these gate the stop path: a remote with no uplink or no
	 * LEDs still drives its sessions, which simply never bond.
	 */
	if (uplink_init() != 0) {
		LOG_WRN("no network interfaces");
	}
	if (pstop_wifi_start() == 0) {
		LOG_INF("wifi station started");
	}
	if (leds_start() != 0) {
		LOG_WRN("LEDs unavailable");
	}

	if (IS_ENABLED(CONFIG_PROTECTIVE_STOP_HTTP)) {
		/* A control plane that fails to start leaves the remote stopping
		 * machines as before, just unprovisionable: keep running.
		 */
		(void)pstop_http_start();
	}

	uint32_t last_mismatches = 0;
	uint32_t last_gated_ticks = 0;
	uint32_t last_missed_deadlines = 0;

	while (true) {
		k_sleep(K_MSEC(COUNTER_LOG_PERIOD_MS));
		health_poll();

		uint32_t mismatches = pstop_lockstep_mismatches();
		uint32_t gated_ticks = pstop_lockstep_gated_ticks();
		uint32_t missed_deadlines = pstop_lockstep_missed_deadlines();

		if ((mismatches != last_mismatches) || (gated_ticks != last_gated_ticks) ||
		    (missed_deadlines != last_missed_deadlines)) {
			struct pstop_lockstep_diag diag;

			pstop_lockstep_diag_get(&diag);
			LOG_INF("lockstep health: mismatches=%u gated_ticks=%u missed_deadlines=%u "
				"last_late=%#x worst_latency=%u/%u ms",
				mismatches, gated_ticks, missed_deadlines, diag.late_mask,
				diag.latency_max_ms[0], diag.latency_max_ms[1]);
			last_mismatches = mismatches;
			last_gated_ticks = gated_ticks;
			last_missed_deadlines = missed_deadlines;
		}
	}
}
