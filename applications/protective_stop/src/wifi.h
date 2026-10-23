/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * WiFi station: joins the first reachable network from the stored list
 * (app_settings_wifi_*), and stays off it while Ethernet holds a lease. A
 * poll every few seconds rather than an event chain: a station that drops
 * is simply found disconnected on the next pass and moves on down the list.
 *
 * Builds without WiFi (native_sim) keep the list API but never connect.
 */

#ifndef PROTECTIVE_STOP_WIFI_H
#define PROTECTIVE_STOP_WIFI_H

/* Start the station supervisor. Returns 0, or -ENODEV without WiFi. */
int pstop_wifi_start(void);

/* Index of the network currently joined, or -1. */
int pstop_wifi_active_idx(void);

/* The stored list changed: drop the current network and start again from
 * the top.
 */
void pstop_wifi_reload(void);

#endif /* PROTECTIVE_STOP_WIFI_H */
