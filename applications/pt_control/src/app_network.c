#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_network, LOG_LEVEL_INF);

#include <zephyr/kernel.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>

#include "app_display.h"
#include "app_network.h"

/* Connection management lives in the wifi_connectivity module (conn_mgr
 * CONNECTIVITY_WIFI_MGMT implementation); this file only tracks L4 state
 * and blocks boot until the network is up or the timeout expires.
 */

#define NET_CONNECT_TIMEOUT K_SECONDS(30)

static K_SEM_DEFINE(l4_connected_sem, 0, 1);
static atomic_t l4_connected;
static struct net_mgmt_event_callback l4_cb;
static struct net_mgmt_event_callback ipv4_cb;

static void l4_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			     struct net_if *iface)
{
	switch (mgmt_event) {
	case NET_EVENT_L4_CONNECTED:
		atomic_set(&l4_connected, 1);
		k_sem_give(&l4_connected_sem);
		break;
	case NET_EVENT_L4_DISCONNECTED:
		atomic_set(&l4_connected, 0);
		app_display_set_net(false, NULL);
		break;
	}
}

static void ipv4_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
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

		net_addr_ntop(AF_INET, &if_addr->address.in_addr, buf, sizeof(buf));
		LOG_INF("IPv4 address: %s", buf);
		app_display_set_net(true, buf);
	}
}

void app_net_connect(void)
{
	net_mgmt_init_event_callback(&l4_cb, l4_event_handler,
				     NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED);
	net_mgmt_add_event_callback(&l4_cb);

	net_mgmt_init_event_callback(&ipv4_cb, ipv4_event_handler, NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&ipv4_cb);

	/* Do NOT call conn_mgr_all_if_connect() here. The wifi_connectivity module
	 * owns the connection lifecycle: it sets NO_AUTO_CONNECT and drives a
	 * deferred connect from its own work queue to dodge the ESP32 boot race.
	 * Forcing an early connect makes the interface associate first, then the
	 * module's deferred attempt issues CONNECT_STORED while already connected,
	 * which the driver rejects and turns into a "Connection attempt failed (1)"
	 * reconnect loop every RECONNECT_DELAY. Just wait for L4 instead.
	 */
	conn_mgr_mon_resend_status();

	LOG_INF("Waiting for network connectivity");
	if (k_sem_take(&l4_connected_sem, NET_CONNECT_TIMEOUT)) {
		LOG_WRN("Network not ready; continuing without it "
			"(store credentials with 'wifi cred add')");
	}
}

bool app_net_ipv4_ready(void)
{
	return atomic_get(&l4_connected) != 0;
}
