/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_reset_reason, LOG_LEVEL_INF);

#include <string.h>
#include <zephyr/debug/coredump.h>
#include <zephyr/device.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/fatal.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/retention/retention.h>
#include <zephyr/sys/reboot.h>

#include "app_reset_reason.h"

/* Bump when struct rr_record changes, so an old layout reads as no record. */
#define RR_VERSION 1

/* hwinfo causes that are a normal way to come up rather than a failure. */
#define RR_EXPECTED_CAUSES (RESET_POR | RESET_PIN | RESET_SOFTWARE | RESET_LOW_POWER_WAKE)

enum rr_mark {
	RR_NONE,
	RR_WATCHDOG,
	RR_FATAL,
};

struct rr_record {
	uint8_t version;
	uint8_t mark;
	uint8_t wdt_channel;
	uint8_t reserved;
	uint16_t crash_count;
	uint16_t fatal_reason;
	char wdt_name[16];
};

static const struct device *const area = DEVICE_DT_GET(DT_NODELABEL(app_reset_reason));

/* Unexpected resets since power-on, as of this boot. Writers carry it forward. */
static uint16_t crash_count;

/* Called from ISR and fatal contexts: a few bytes, no locks, no logging. */
static void rr_write(uint8_t mark, uint8_t wdt_channel, uint16_t fatal_reason, const char *name)
{
	struct rr_record rec = {
		.version = RR_VERSION,
		.mark = mark,
		.wdt_channel = wdt_channel,
		.crash_count = crash_count,
		.fatal_reason = fatal_reason,
	};

	if (name != NULL) {
		strncpy(rec.wdt_name, name, sizeof(rec.wdt_name) - 1);
	}

	if (device_is_ready(area)) {
		(void)retention_write(area, 0, (const uint8_t *)&rec, sizeof(rec));
	}
}

void app_reset_reason_watchdog(int channel, const char *name)
{
	rr_write(RR_WATCHDOG, (uint8_t)channel, 0, name);
}

/* Replaces the kernel's weak default so the fault is marked before the reset.
 * Runs after coredump(), so a flash dump is already written by now. */
void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	ARG_UNUSED(esf);

	rr_write(RR_FATAL, 0, (uint16_t)reason, NULL);

	LOG_PANIC();
	if (IS_ENABLED(CONFIG_RESET_ON_FATAL_ERROR)) {
		sys_reboot(SYS_REBOOT_WARM);
	}
	k_fatal_halt(reason);
}

static bool coredump_stored(void)
{
	return IS_ENABLED(CONFIG_DEBUG_COREDUMP_BACKEND_FLASH_PARTITION) &&
	       coredump_query(COREDUMP_QUERY_HAS_STORED_DUMP, NULL) == 1;
}

static int app_reset_reason_boot(void)
{
	struct rr_record rec = {0};
	uint32_t cause = 0;
	int ret;

	if (!device_is_ready(area)) {
		LOG_ERR("Retention area not ready; reset reasons will not be recorded");
		return 0;
	}

	ret = hwinfo_get_reset_cause(&cause);
	(void)hwinfo_clear_reset_cause();

	/* After a power-on the area holds whatever the RAM came up with; a
	 * prefix and CRC that happen to match are still not a record. */
	if (!(cause & RESET_POR) && retention_is_valid(area) == 1 &&
	    retention_read(area, 0, (uint8_t *)&rec, sizeof(rec)) == 0 &&
	    rec.version == RR_VERSION) {
		crash_count = rec.crash_count;
	} else {
		rec.mark = RR_NONE;
	}

	switch (rec.mark) {
	case RR_WATCHDOG:
		crash_count++;
		rec.wdt_name[sizeof(rec.wdt_name) - 1] = '\0';
		LOG_ERR("Last reset: task watchdog, '%s' (channel %u) stalled", rec.wdt_name,
			rec.wdt_channel);
		break;
	case RR_FATAL:
		crash_count++;
		LOG_ERR("Last reset: fatal error %u, %s", rec.fatal_reason,
			coredump_stored() ? "coredump stored" : "no coredump");
		break;
	default:
		if (ret < 0 || cause == 0) {
			LOG_WRN("Last reset: cause unknown (%d)", ret);
		} else if (cause & RESET_POR) {
			LOG_INF("Last reset: power-on");
		} else if (cause & ~RR_EXPECTED_CAUSES) {
			crash_count++;
			LOG_ERR("Last reset was not requested (hwinfo cause 0x%x)", cause);
		} else {
			LOG_INF("Last reset: requested (hwinfo cause 0x%x)", cause);
		}
		break;
	}

	if (crash_count > 0) {
		LOG_WRN("%u unexpected resets since power-on", crash_count);
	}

	/* Clear the mark so the next boot cannot report it again. */
	rr_write(RR_NONE, 0, 0, NULL);

	return 0;
}

SYS_INIT(app_reset_reason_boot, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
