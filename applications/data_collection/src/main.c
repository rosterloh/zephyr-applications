/*
 * Copyright (c) 2026 Richard Osterloh
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

static struct net_mgmt_event_callback ipv4_cb;

static void on_ipv4_addr_add(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			     struct net_if *iface)
{
	if (mgmt_event != NET_EVENT_IPV4_ADDR_ADD) {
		return;
	}

	for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
		struct net_if_addr *if_addr = &iface->config.ip.ipv4->unicast[i].ipv4;
		char buf[NET_IPV4_ADDR_LEN];

		if (!if_addr->is_used || if_addr->addr_type != NET_ADDR_DHCP) {
			continue;
		}

		LOG_INF("IPv4 address: %s",
			net_addr_ntop(AF_INET, &if_addr->address.in_addr, buf, sizeof(buf)));
	}
}

int main(void)
{
	LOG_INF("data_collection starting (SMP/UDP OTA ready)");

	net_mgmt_init_event_callback(&ipv4_cb, on_ipv4_addr_add, NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&ipv4_cb);

	return 0;
}
