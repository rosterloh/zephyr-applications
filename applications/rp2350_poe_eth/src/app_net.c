/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/ethernet.h>

#include "app_net.h"
#include "app_status.h"

LOG_MODULE_REGISTER(app_net, CONFIG_APP_LOG_LEVEL);

#define NET_EVENTS (NET_EVENT_IPV4_ADDR_ADD | NET_EVENT_IF_UP | NET_EVENT_IF_DOWN)

static struct net_mgmt_event_callback mgmt_cb;
static K_SEM_DEFINE(got_address, 0, 1);

static void net_event_handler(struct net_mgmt_event_callback *cb, uint64_t event,
			      struct net_if *iface)
{
	ARG_UNUSED(iface);

	switch (event) {
	case NET_EVENT_IF_UP:
		LOG_INF("Ethernet link up");
		app_status_set(APP_STATUS_LINK_UP);
		break;

	case NET_EVENT_IF_DOWN:
		LOG_WRN("Ethernet link down");
		app_status_set(APP_STATUS_ERROR);
		break;

	case NET_EVENT_IPV4_ADDR_ADD: {
		char buf[NET_IPV4_ADDR_LEN];
		const struct net_if_addr *if_addr = cb->info;

		if (if_addr == NULL) {
			break;
		}

		LOG_INF("IPv4 address: %s",
			net_addr_ntop(AF_INET, &if_addr->address.in_addr, buf, sizeof(buf)));
		app_status_set(APP_STATUS_ONLINE);
		k_sem_give(&got_address);
		break;
	}

	default:
		break;
	}
}

int app_net_init(void)
{
	struct net_if *iface = net_if_get_first_by_type(&NET_L2_GET_NAME(ETHERNET));

	if (iface == NULL) {
		LOG_ERR("No Ethernet interface found");
		return -ENODEV;
	}

	net_mgmt_init_event_callback(&mgmt_cb, net_event_handler, NET_EVENTS);
	net_mgmt_add_event_callback(&mgmt_cb);

	if (!net_if_is_admin_up(iface)) {
		int ret = net_if_up(iface);

		if (ret < 0 && ret != -EALREADY) {
			LOG_ERR("Failed to bring interface up: %d", ret);
			return ret;
		}
	}

	LOG_INF("Starting DHCPv4");
	net_dhcpv4_start(iface);

	return 0;
}

int app_net_wait_for_address(k_timeout_t timeout)
{
	if (k_sem_take(&got_address, timeout) != 0) {
		LOG_ERR("Timed out waiting for a DHCPv4 lease");
		return -ETIMEDOUT;
	}

	return 0;
}
