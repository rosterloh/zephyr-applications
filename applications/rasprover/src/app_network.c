#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_network, LOG_LEVEL_INF);

#include <zephyr/kernel.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_mgmt.h>

#include "app_network.h"

/* Connection management lives in the wifi_connectivity module (conn_mgr
 * CONNECTIVITY_WIFI_MGMT implementation); this file only tracks L4 state
 * and blocks boot until the network is up or the timeout expires.
 */

#define NET_CONNECT_TIMEOUT K_SECONDS(30)

static K_SEM_DEFINE(l4_connected_sem, 0, 1);
static atomic_t l4_connected;
static struct net_mgmt_event_callback l4_cb;

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
		break;
	}
}

void app_net_connect(void)
{
	int err;

	net_mgmt_init_event_callback(&l4_cb, l4_event_handler,
				     NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED);
	net_mgmt_add_event_callback(&l4_cb);
	conn_mgr_mon_resend_status();

	err = conn_mgr_all_if_connect(true);
	if (err) {
		LOG_WRN("Failed to start network connection: %d", err);
	}

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
