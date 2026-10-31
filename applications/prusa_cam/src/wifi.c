/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Station mode from networks stored with `wifi cred add`; retry after a
 * failed or dropped connection.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_credentials.h>
#include <zephyr/net/wifi_mgmt.h>

#include "app_wifi.h"

LOG_MODULE_REGISTER(app_wifi, LOG_LEVEL_INF);

#define RETRY K_SECONDS(10)

static struct net_mgmt_event_callback wifi_cb;

static void connect_work_fn(struct k_work *work)
{
	struct net_if *iface = net_if_get_wifi_sta();

	if (iface == NULL || wifi_credentials_is_empty()) {
		LOG_INF("No stored network: wifi cred add -s <ssid> -k 1 -p <psk>");
		return;
	}
	if (net_mgmt(NET_REQUEST_WIFI_CONNECT_STORED, iface, NULL, 0) < 0) {
		LOG_WRN("Connect request failed, retrying");
		k_work_reschedule((struct k_work_delayable *)work, RETRY);
	}
}

static K_WORK_DELAYABLE_DEFINE(connect_work, connect_work_fn);

static void wifi_event(struct net_mgmt_event_callback *cb, uint64_t event, struct net_if *iface)
{
	const struct wifi_status *st = cb->info;

	if (event == NET_EVENT_WIFI_CONNECT_RESULT && st->status == 0) {
		LOG_INF("Connected");
		return;
	}
	/* Failed connect or disconnect: try again later. */
	k_work_reschedule(&connect_work, RETRY);
}

int app_wifi_init(void)
{
	net_mgmt_init_event_callback(&wifi_cb, wifi_event,
				     NET_EVENT_WIFI_CONNECT_RESULT |
					     NET_EVENT_WIFI_DISCONNECT_RESULT);
	net_mgmt_add_event_callback(&wifi_cb);
	k_work_reschedule(&connect_work, K_NO_WAIT);
	return 0;
}

void app_wifi_reconnect(void)
{
	k_work_reschedule(&connect_work, K_NO_WAIT);
}
