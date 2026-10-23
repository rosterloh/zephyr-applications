/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>

#include "app_settings.h"
#include "pstop_log.h"
#include "uplink.h"
#include "wifi.h"

LOG_MODULE_REGISTER(pstop_wifi, PSTOP_LOG_LEVEL);

#ifdef CONFIG_WIFI

#define POLL_MS            3000
#define CONNECT_TIMEOUT_MS 20000

static struct net_if *iface;
static atomic_t active_idx = ATOMIC_INIT(-1);
static atomic_t reload;
static int trying = -1; /* network of the attempt in flight */
static int64_t attempt_ms;
static int next_idx;

static void supervise(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(supervise_work, supervise);

static bool connected(void)
{
	struct wifi_iface_status st = {0};

	if (net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &st, sizeof(st)) != 0) {
		return false;
	}
	return st.state == WIFI_STATE_COMPLETED;
}

static void disconnect(void)
{
	(void)net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0);
	atomic_set(&active_idx, -1);
	trying = -1;
}

static void try_next(void)
{
	struct pstop_wifi_net net;
	struct wifi_connect_req_params p = {0};

	for (int n = 0; n < PSTOP_WIFI_MAX; n++) {
		int idx = (next_idx + n) % PSTOP_WIFI_MAX;

		if ((app_settings_wifi_get(idx, &net) != 0) || (net.ssid[0] == '\0')) {
			continue;
		}

		p.ssid = (const uint8_t *)net.ssid;
		p.ssid_length = (uint8_t)strlen(net.ssid);
		p.psk = (const uint8_t *)net.pass;
		p.psk_length = (uint8_t)strlen(net.pass);
		p.security = (p.psk_length > 0U) ? WIFI_SECURITY_TYPE_PSK : WIFI_SECURITY_TYPE_NONE;
		p.channel = WIFI_CHANNEL_ANY;
		p.band = WIFI_FREQ_BAND_2_4_GHZ;
		p.mfp = WIFI_MFP_OPTIONAL;
		p.timeout = SYS_FOREVER_MS;

		next_idx = (idx + 1) % PSTOP_WIFI_MAX;
		trying = idx;
		attempt_ms = k_uptime_get();
		LOG_INF("joining wifi %d \"%s\"", idx, net.ssid);
		if (net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &p, sizeof(p)) != 0) {
			LOG_WRN("wifi %d connect request refused", idx);
			trying = -1;
		}
		return;
	}
}

static void supervise(struct k_work *work)
{
	ARG_UNUSED(work);

	if (atomic_clear(&reload) != 0) {
		disconnect();
		next_idx = 0;
	}

	if (uplink_ethernet_ready()) {
		/* Ethernet wins: one path for the machine sessions. */
		if ((atomic_get(&active_idx) >= 0) || (trying >= 0)) {
			LOG_INF("ethernet up: leaving wifi");
			disconnect();
			next_idx = 0;
		}
	} else if (connected()) {
		if ((trying >= 0) && (atomic_get(&active_idx) != trying)) {
			atomic_set(&active_idx, trying);
			LOG_INF("wifi %d joined", trying);
		}
	} else {
		if (atomic_get(&active_idx) >= 0) {
			LOG_WRN("wifi %d lost", (int)atomic_get(&active_idx));
			atomic_set(&active_idx, -1);
			trying = -1;
		}
		if ((trying < 0) || ((k_uptime_get() - attempt_ms) > CONNECT_TIMEOUT_MS)) {
			if (trying >= 0) {
				(void)net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0);
				trying = -1;
			}
			try_next();
		}
	}

	(void)k_work_reschedule(&supervise_work, K_MSEC(POLL_MS));
}

int pstop_wifi_start(void)
{
	iface = net_if_get_first_wifi();
	if (iface == NULL) {
		return -ENODEV;
	}
	(void)k_work_reschedule(&supervise_work, K_NO_WAIT);
	return 0;
}

int pstop_wifi_active_idx(void)
{
	return (int)atomic_get(&active_idx);
}

void pstop_wifi_reload(void)
{
	atomic_set(&reload, 1);
	(void)k_work_reschedule(&supervise_work, K_NO_WAIT);
}

#else /* !CONFIG_WIFI */

int pstop_wifi_start(void)
{
	return -ENODEV;
}

int pstop_wifi_active_idx(void)
{
	return -1;
}

void pstop_wifi_reload(void)
{
}

#endif /* CONFIG_WIFI */
