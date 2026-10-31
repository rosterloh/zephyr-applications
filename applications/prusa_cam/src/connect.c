/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Prusa camera service API v1 (https://camera-service.prusa3d.com/docs/):
 * PUT /v1/camera-info (JSON) once and after each config change, then
 * PUT /v1/snapshot (JPEG, at most 1 MB and 6 a minute), each with Token,
 * Fingerprint and User-Agent headers, over HTTPS.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/atomic.h>

#include <zephyr/app_version.h>

#include "cam.h"
#include "cfg.h"
#include "connect.h"
#include "prusa_camera.h"

LOG_MODULE_REGISTER(connect, LOG_LEVEL_INF);

#define BACKOFF_MAX_S 600

enum {
	INFO_STALE,
	TEST_REQUESTED
};

static K_SEM_DEFINE(kick, 0, 1);
static K_MUTEX_DEFINE(status_lock);
static struct connect_status status;
static atomic_t flags = ATOMIC_INIT(BIT(INFO_STALE));
static struct net_mgmt_event_callback addr_cb;

/* network_info for camera-info, filled from the WiFi station interface. */
static struct {
	char mac[sizeof("00:00:00:00:00:00")];
	char ipv4[NET_IPV4_ADDR_LEN];
	char ssid[WIFI_SSID_MAX_LEN + 1];
} net;

static void net_info_update(void)
{
	struct net_if *iface = net_if_get_wifi_sta();
	struct wifi_iface_status st = {0};
	struct net_linkaddr *ll;
	struct net_in_addr *a;

	memset(&net, 0, sizeof(net));
	if (iface == NULL) {
		return;
	}
	ll = net_if_get_link_addr(iface);
	if (ll != NULL && ll->len == 6) {
		snprintk(net.mac, sizeof(net.mac), "%02x:%02x:%02x:%02x:%02x:%02x", ll->addr[0],
			 ll->addr[1], ll->addr[2], ll->addr[3], ll->addr[4], ll->addr[5]);
	}
	a = net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);
	if (a != NULL) {
		net_addr_ntop(NET_AF_INET, a, net.ipv4, sizeof(net.ipv4));
	}
	if (net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &st, sizeof(st)) == 0 &&
	    st.state >= WIFI_STATE_ASSOCIATED) {
		memcpy(net.ssid, st.ssid, MIN(st.ssid_len, WIFI_SSID_MAX_LEN));
	}
}

/*
 * A new address goes into camera-info, so Connect shows where the camera is.
 * Try now too: after an outage the backoff may have grown to 10 minutes.
 */
static void addr_event(struct net_mgmt_event_callback *cb, uint64_t event, struct net_if *iface)
{
	atomic_set_bit(&flags, INFO_STALE);
	k_sem_give(&kick);
}

static int send_info(const struct prusa_camera_auth *auth, const struct cfg *c)
{
	net_info_update();

	const struct prusa_camera_info info = {
		.name = c->name,
		.trigger = prusa_camera_trigger_from_seconds(c->interval_s),
		.rotation = c->rotation * 90,
		.width = cfg_resolutions[c->resolution].width,
		.height = cfg_resolutions[c->resolution].height,
		.wifi_mac = net.mac,
		.wifi_ipv4 = net.ipv4,
		.wifi_ssid = net.ssid,
		.type = "ESP32",
		.firmware_version = APP_VERSION_STRING,
		.hardware_version = "AI-Thinker ESP32-CAM",
		.manufacturer = "AI-Thinker",
		.model = "ESP32-CAM",
	};

	return prusa_camera_put_info(auth, &info);
}

static int send_snapshot(const struct prusa_camera_auth *auth)
{
	const uint8_t *jpeg;
	size_t len;
	int ret = cam_snapshot(&jpeg, &len);

	if (ret < 0) {
		return ret;
	}
	ret = prusa_camera_put_snapshot(auth, jpeg, len);
	cam_release();
	LOG_INF("Snapshot %u B -> %d", len, ret);
	return ret;
}

static void record(int result, bool uploaded, bool stopped)
{
	k_mutex_lock(&status_lock, K_FOREVER);
	status.last_ms = k_uptime_get();
	status.last_result = result;
	status.uploads += uploaded;
	status.stopped = stopped;
	k_mutex_unlock(&status_lock);
}

/*
 * One step of the upload loop: a requested connection test, camera-info when
 * it is stale, else a snapshot. Returns what the service said to do next.
 */
static enum prusa_camera_next step(const struct prusa_camera_auth *auth, const struct cfg *c,
				   bool *stopped)
{
	static const char *const what[] = {"test", "camera-info", "snapshot"};
	enum {
		TEST,
		INFO,
		SNAPSHOT
	} kind;
	enum prusa_camera_next next;
	int ret;

	if (atomic_test_and_clear_bit(&flags, TEST_REQUESTED)) {
		kind = TEST;
		ret = prusa_camera_get_info(auth);
	} else if (*stopped) {
		return PRUSA_CAMERA_NEXT_STOP;
	} else if (atomic_test_bit(&flags, INFO_STALE)) {
		kind = INFO;
		ret = send_info(auth, c);
	} else {
		kind = SNAPSHOT;
		ret = send_snapshot(auth);
	}

	next = prusa_camera_next(ret);
	if (kind == INFO && next == PRUSA_CAMERA_NEXT_OK) {
		atomic_clear_bit(&flags, INFO_STALE);
	}
	/* A passing test clears a stop: the token may have been fixed in Connect. */
	*stopped = next == PRUSA_CAMERA_NEXT_STOP;
	if (next != PRUSA_CAMERA_NEXT_OK) {
		LOG_WRN("%s -> %d: %s", what[kind], ret, prusa_camera_describe(ret));
	} else if (kind == TEST) {
		LOG_INF("Connection test passed");
	}
	record(ret, kind == SNAPSHOT && next == PRUSA_CAMERA_NEXT_OK, *stopped);
	return next;
}

static void connect_thread(void *p1, void *p2, void *p3)
{
	struct cfg c;
	struct prusa_camera_auth auth = {.token = c.token, .fingerprint = cfg_fingerprint()};
	uint32_t fails = 0;
	bool stopped = false;

	while (true) {
		uint32_t wait;

		cfg_get(&c);
		/* Back off on network or server trouble, doubling up to 10 minutes. */
		wait = MIN((uint32_t)c.interval_s << MIN(fails, 6), BACKOFF_MAX_S);
		wait = MAX(wait, c.interval_s);
		if (k_sem_take(&kick, K_SECONDS(wait)) == 0) {
			stopped = atomic_test_bit(&flags, TEST_REQUESTED) ? stopped : false;
		}

		cfg_get(&c);
		if (c.token[0] == '\0') {
			continue;
		}

		switch (step(&auth, &c, &stopped)) {
		case PRUSA_CAMERA_NEXT_RETRY:
			fails++;
			break;
		case PRUSA_CAMERA_NEXT_OK:
			fails = 0;
			break;
		default: /* WAIT: keep the cadence; STOP: wait for a config change */
			break;
		}
	}
}

K_THREAD_DEFINE(connect_tid, CONFIG_PRUSA_CAM_CONNECT_STACK_SIZE, connect_thread, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, SYS_FOREVER_MS);

int connect_init(void)
{
	int ret = prusa_camera_init("prusa_cam/" APP_VERSION_STRING);

	if (ret < 0) {
		return ret;
	}
	net_mgmt_init_event_callback(&addr_cb, addr_event, NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&addr_cb);
	k_thread_start(connect_tid);
	return 0;
}

void connect_kick(void)
{
	atomic_set_bit(&flags, INFO_STALE);
	k_sem_give(&kick);
}

void connect_test(void)
{
	atomic_set_bit(&flags, TEST_REQUESTED);
	k_sem_give(&kick);
}

void connect_get_status(struct connect_status *out)
{
	k_mutex_lock(&status_lock, K_FOREVER);
	*out = status;
	k_mutex_unlock(&status_lock);
}
