/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_STATUS_H_
#define APP_STATUS_H_

#include <zephyr/sys/util.h>

/** Coarse application state, surfaced on the onboard WS2812. */
enum app_status {
	/** Booting, no network yet. */
	APP_STATUS_BOOTING,
	/** Ethernet link is up but no IPv4 address has been leased. */
	APP_STATUS_LINK_UP,
	/** DHCPv4 bound, hawkBit polling. */
	APP_STATUS_ONLINE,
	/** An update is being downloaded. */
	APP_STATUS_UPDATING,
	/** Something went wrong; see the log. */
	APP_STATUS_ERROR,
};

/**
 * @brief Set the status indicator.
 *
 * Safe to call before the LED strip device is ready and on builds without a
 * status LED, in which case it is a no-op.
 */
#ifdef CONFIG_APP_STATUS_LED
void app_status_set(enum app_status status);
#else
static inline void app_status_set(enum app_status status)
{
	ARG_UNUSED(status);
}
#endif

#endif /* APP_STATUS_H_ */
