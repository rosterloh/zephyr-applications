/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/byteorder.h>

#include "pstop_log.h"
#include "uplink.h"

LOG_MODULE_REGISTER(pstop_uplink, PSTOP_LOG_LEVEL);

#ifdef CONFIG_NET_NATIVE

static struct net_if *eth_iface;
static struct net_if *wifi_iface;

static bool is_wifi(struct net_if *iface)
{
#ifdef CONFIG_WIFI
	return net_if_is_wifi(iface);
#else
	ARG_UNUSED(iface);
	return false;
#endif
}

static void classify(struct net_if *iface, void *user_data)
{
	ARG_UNUSED(user_data);

	if (is_wifi(iface)) {
		if (wifi_iface == NULL) {
			wifi_iface = iface;
		}
	} else if (net_if_l2(iface) == &NET_L2_GET_NAME(ETHERNET)) {
		if (eth_iface == NULL) {
			eth_iface = iface;
		}
	} else {
		return;
	}

#ifdef CONFIG_NET_DHCPV4
	net_dhcpv4_start(iface);
#endif
}

/* An interface's leased (or static) IPv4 address, host order, or 0. */
static uint32_t iface_ipv4(struct net_if *iface)
{
	struct in_addr *addr;

	if ((iface == NULL) || !net_if_is_up(iface)) {
		return 0U;
	}
	addr = net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);
	return (addr != NULL) ? sys_be32_to_cpu(addr->s_addr) : 0U;
}

int uplink_init(void)
{
	net_if_foreach(classify, NULL);
	LOG_INF("uplinks: ethernet %s, wifi %s", (eth_iface != NULL) ? "yes" : "no",
		(wifi_iface != NULL) ? "yes" : "no");
	return ((eth_iface == NULL) && (wifi_iface == NULL)) ? -ENODEV : 0;
}

bool uplink_ethernet_ready(void)
{
	return iface_ipv4(eth_iface) != 0U;
}

void uplink_status_get(struct uplink_status *out)
{
	uint32_t ip = iface_ipv4(eth_iface);

	if (ip != 0U) {
		out->kind = UPLINK_ETHERNET;
		out->ipv4 = ip;
		return;
	}

	ip = iface_ipv4(wifi_iface);
	if (ip != 0U) {
		out->kind = UPLINK_WIFI;
		out->ipv4 = ip;
		return;
	}

	out->kind = UPLINK_NONE;
	out->ipv4 = 0U;
}

#else /* !CONFIG_NET_NATIVE */

int uplink_init(void)
{
	return 0;
}

bool uplink_ethernet_ready(void)
{
	return false;
}

void uplink_status_get(struct uplink_status *out)
{
	out->kind = UPLINK_HOST;
	out->ipv4 = 0x7F000001U;
}

#endif /* CONFIG_NET_NATIVE */

const char *uplink_kind_str(enum uplink_kind kind)
{
	switch (kind) {
	case UPLINK_ETHERNET:
		return "eth";
	case UPLINK_WIFI:
		return "wifi";
	case UPLINK_HOST:
		return "host";
	default:
		return "none";
	}
}
