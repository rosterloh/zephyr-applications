/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Station mode from networks stored with `wifi cred add`; retry after a
 * failed or dropped connection. Addressing is DHCP unless a static IPv4
 * address is configured (CONFIG_WIFI_STA_AUTO_DHCPV4 is off for that).
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/dns_resolve.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_credentials.h>
#include <zephyr/net/wifi_mgmt.h>

#include "app_wifi.h"
#include "cfg.h"

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

static struct net_in_addr in4(uint32_t host_order)
{
	return (struct net_in_addr){.s_addr = net_htonl(host_order)};
}

/*
 * shortcut: a changed address applies on the next connect (or reboot); an
 * old static address is not removed when switching back to DHCP until then.
 */
static void ipv4_up(struct net_if *iface)
{
	struct cfg c;
	struct net_in_addr addr, mask, gw, dns;
	char dns_str[NET_IPV4_ADDR_LEN];
	const char *servers[] = {dns_str, NULL};

	cfg_get(&c);
	if (c.ip == 0) {
		net_dhcpv4_start(iface);
		return;
	}
	addr = in4(c.ip);
	mask = in4(c.netmask);
	gw = in4(c.gateway);
	dns = in4(c.dns != 0 ? c.dns : c.gateway);
	memset(&c, 0, sizeof(c));

	if (net_if_ipv4_addr_add(iface, &addr, NET_ADDR_MANUAL, 0) == NULL) {
		LOG_ERR("Static address rejected");
		return;
	}
	net_if_ipv4_set_netmask_by_addr(iface, &addr, &mask);
	net_if_ipv4_set_gw(iface, &gw);
	net_addr_ntop(NET_AF_INET, &dns, dns_str, sizeof(dns_str));
	if (dns_resolve_reconfigure(dns_resolve_get_default(), servers, NULL, DNS_SOURCE_MANUAL) <
	    0) {
		LOG_WRN("DNS server %s not set", dns_str);
	}
	LOG_INF("Static address set");
}

static void wifi_event(struct net_mgmt_event_callback *cb, uint64_t event, struct net_if *iface)
{
	const struct wifi_status *st = cb->info;

	if (event == NET_EVENT_WIFI_CONNECT_RESULT && st->status == 0) {
		LOG_INF("Connected");
		ipv4_up(iface);
		return;
	}
	/* Failed connect or disconnect: try again later. */
	net_dhcpv4_stop(iface);
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
