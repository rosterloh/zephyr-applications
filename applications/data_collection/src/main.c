/*
 * Copyright (c) 2026 Richard Osterloh
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/dfu/mcuboot.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/mgmt/hawkbit/hawkbit.h>
#include <zephyr/mgmt/hawkbit/autohandler.h>

#include "app_img_confirm.h"
#include "app_watchdog.h"
#include "cam_mgmt.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Generous: DHCPv4 and the camera's bring-up both sit inside this window, and
 * a boot that is merely slow must not be mistaken for one that is stuck. */
#define BOOT_WDT_TIMEOUT_MS 60000

/* Under BOOT_WDT_TIMEOUT_MS. NET_CONFIG_NEED_IPV4 already waited for the lease
 * before main(), so this only covers a slow DHCP server. */
#define IPV4_WAIT_TIMEOUT K_SECONDS(30)

static K_SEM_DEFINE(ipv4_up, 0, 1);

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
		k_sem_give(&ipv4_up);
	}
}

static bool ipv4_ready(void)
{
	return net_if_ipv4_get_global_addr(net_if_get_default(), NET_ADDR_PREFERRED) != NULL ||
	       k_sem_take(&ipv4_up, IPV4_WAIT_TIMEOUT) == 0;
}

/* After the health check, and only on a confirmed image: the autohandler
 * reboots an unconfirmed one, which would cut the img_confirm deadline short.
 * A confirmed image starts it even with a broken camera, so OTA can fix it. */
static void start_hawkbit(void)
{
#if defined(CONFIG_HAWKBIT)
	if (!boot_is_img_confirmed()) {
		LOG_WRN("Image not confirmed; hawkBit not started");
		return;
	}

	int ret = hawkbit_init();

	if (ret < 0) {
		LOG_ERR("hawkBit init failed (%d)", ret);
		return;
	}

	hawkbit_autohandler(true);
#endif
}

int main(void)
{
	const struct device *cam = DEVICE_DT_GET(DT_CHOSEN(zephyr_camera));
	int wdt_channel;

	LOG_INF("data_collection starting (SMP/UDP management ready)");

	/* Cover the boot path. Everything below can block on hardware -- the
	 * camera's I2C bring-up, and the first capture through a CSI receiver
	 * that is known to wedge (rosterloh/zephyr-drivers#43). The channel is
	 * deleted before main() returns rather than left registered: from that
	 * point the app is event driven, there is no loop left to feed from, and
	 * a channel nobody feeds is a reboot timer. Steady-state coverage is
	 * cam_mgmt_capture()'s own channel, registered per capture. */
	app_watchdog_init();
	wdt_channel = app_watchdog_register("boot", BOOT_WDT_TIMEOUT_MS);

	/* Healthy = an IPv4 lease and a first frame. */
	app_img_confirm_arm();

	net_mgmt_init_event_callback(&ipv4_cb, on_ipv4_addr_add, NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&ipv4_cb);

	app_watchdog_feed(wdt_channel);

	if (!device_is_ready(cam)) {
		LOG_ERR("Camera %s not ready", cam->name);
		goto out;
	}

#if defined(CONFIG_APP_CAM_MGMT)
	if (cam_mgmt_capture(cam) != 0) {
		goto out;
	}
	LOG_INF("Camera capture OK; pull it with the SMP camera group (0x1000) or "
		"capture again from the `video` shell");
#endif

	app_watchdog_feed(wdt_channel);
	if (ipv4_ready()) {
		app_img_confirm();
	} else {
		LOG_ERR("No IPv4 lease");
	}

out:
	app_watchdog_unregister(wdt_channel);
	start_hawkbit();

	return 0;
}
