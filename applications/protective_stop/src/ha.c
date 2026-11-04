/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Home Assistant view of the remote over the ESPHome native API. Read-only:
 * nothing here reaches the stop path. A low-priority thread polls the
 * comparator's published state and pushes changes, so a slow or stalled
 * Home Assistant connection can only delay these updates, never a tick.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/esphome_api.h>
#include <zephyr/settings/settings.h>

#include "app_settings.h"
#include "health.h"
#include "lockstep.h"
#include "pstop/pstop_msg.h"
#include "pstop_log.h"
#include "session.h"

LOG_MODULE_DECLARE(pstop, PSTOP_LOG_LEVEL);

#define HA_POLL_MS 100

/* "safety" device class: on means unsafe, i.e. a stop is being sent. */
ESPHOME_BINARY_SENSOR_DEFINE(stop, .name = "Stop", .device_class = "safety");
ESPHOME_SENSOR_DEFINE(bonded_machines, .name = "Bonded machines", .icon = "mdi:robot-industrial",
		      .state_class = ESPHOME_STATE_CLASS_MEASUREMENT);
ESPHOME_SENSOR_DEFINE(lockstep_faults, .name = "Lockstep faults",
		      .state_class = ESPHOME_STATE_CLASS_TOTAL_INCREASING,
		      .entity_category = ESPHOME_ENTITY_CATEGORY_DIAGNOSTIC);
ESPHOME_BINARY_SENSOR_DEFINE(health_problem, .name = "Health", .device_class = "problem",
			     .entity_category = ESPHOME_ENTITY_CATEGORY_DIAGNOSTIC);

static void ha_thread(void *p1, void *p2, void *p3)
{
	int last_stop = -1;
	int last_bonded = -1;
	int64_t last_faults = -1;
	int last_problem = -1;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	/* app_settings loads only its own subtree; the API key lives here. */
	(void)settings_load_subtree("esphome_api");

	while (true) {
		/* Either channel deciding STOP means STOP goes on the wire. */
		int now_stop = (pstop_lockstep_last_verdict(0) != PSTOP_MESSAGE_OK) ||
			       (pstop_lockstep_last_verdict(1) != PSTOP_MESSAGE_OK);
		int bonded = 0;
		int64_t faults = (int64_t)pstop_lockstep_mismatches() +
				 (int64_t)pstop_lockstep_missed_deadlines();
		int problem = health_level() != HEALTH_OK;

		for (int slot = 0; slot < PSTOP_MAX_MACHINES; slot++) {
			if (pstop_lockstep_slot_active(slot) &&
			    (pstop_lockstep_session(slot)->state == PSTOP_SESS_BONDED)) {
				bonded++;
			}
		}

		if (now_stop != last_stop) {
			(void)esphome_api_publish_bool(&stop, now_stop);
			last_stop = now_stop;
		}
		if (bonded != last_bonded) {
			(void)esphome_api_publish_float(&bonded_machines, (float)bonded);
			last_bonded = bonded;
		}
		if (faults != last_faults) {
			(void)esphome_api_publish_float(&lockstep_faults, (float)faults);
			last_faults = faults;
		}
		if (problem != last_problem) {
			(void)esphome_api_publish_bool(&health_problem, problem);
			last_problem = problem;
		}

		k_sleep(K_MSEC(HA_POLL_MS));
	}
}

/* Lowest preemptible priority: below the samplers, comparator and uplinks. */
K_THREAD_DEFINE(pstop_ha, 1024, ha_thread, NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0,
		0);
