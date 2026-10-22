/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Which network this remote is on. Upstream prefers Ethernet over WiFi, and
 * so does this: both interfaces run DHCP, and while the W5500 holds a lease
 * WiFi is kept disconnected (wifi.c), so the machine sessions never straddle
 * two paths. Pull the cable and WiFi takes over; plug it back in and
 * Ethernet wins again.
 *
 * native_sim has no interfaces of its own (offloaded sockets go straight to
 * the host), so it reports the host as an uplink that is always up.
 */

#ifndef PROTECTIVE_STOP_UPLINK_H
#define PROTECTIVE_STOP_UPLINK_H

#include <stdbool.h>
#include <stdint.h>

enum uplink_kind {
	UPLINK_NONE = 0,
	UPLINK_ETHERNET,
	UPLINK_WIFI,
	UPLINK_HOST, /* native_sim */
};

struct uplink_status {
	enum uplink_kind kind;
	uint32_t ipv4; /* host byte order, 0 if none */
};

/* Start DHCP on every interface. Returns 0 or -errno. */
int uplink_init(void);

/* The active uplink, computed now. Safe from any thread. */
void uplink_status_get(struct uplink_status *out);

/* True while Ethernet has a link and a lease. */
bool uplink_ethernet_ready(void);

/* "eth", "wifi", "host" or "none", as /state.json's active_iface. */
const char *uplink_kind_str(enum uplink_kind kind);

#endif /* PROTECTIVE_STOP_UPLINK_H */
