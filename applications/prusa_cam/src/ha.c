/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Home Assistant over the ESPHome native API: the camera, upload status, WiFi
 * signal, the flash setting and a snapshot-now button. A low-priority thread polls the
 * uploader and config and publishes changes.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/esphome_api.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/base64.h>

#include "cam.h"
#include "cfg.h"
#include "connect.h"
#include "ha.h"

LOG_MODULE_REGISTER(ha, LOG_LEVEL_INF);

#define POLL_MS    1000
#define RSSI_POLLS 30

static void flash_set(struct esphome_entity *e, bool on)
{
	struct cfg c;

	cfg_get(&c);
	c.flash = on;
	if (cfg_set(&c) < 0) {
		on = !on;
	}
	memset(&c, 0, sizeof(c));
	(void)esphome_api_publish_bool(e, on);
}

static void snapshot_press(struct esphome_entity *e)
{
	ARG_UNUSED(e);
	connect_kick();
}

/* As the MJPEG stream: no flash, and the camera stays locked while the frame is sent. */
static int image_get(struct esphome_entity *e, const uint8_t **data, size_t *len)
{
	ARG_UNUSED(e);
	return cam_frame(data, len);
}

static void image_release(struct esphome_entity *e)
{
	ARG_UNUSED(e);
	cam_release();
}

ESPHOME_CAMERA_DEFINE(camera, .name = "Camera", .get_image = image_get,
		      .release_image = image_release);
ESPHOME_BINARY_SENSOR_DEFINE(upload_problem, .name = "Upload", .device_class = "problem");
ESPHOME_SENSOR_DEFINE(uploads, .name = "Uploads", .icon = "mdi:cloud-upload",
		      .state_class = ESPHOME_STATE_CLASS_TOTAL_INCREASING,
		      .entity_category = ESPHOME_ENTITY_CATEGORY_DIAGNOSTIC);
ESPHOME_SENSOR_DEFINE(wifi_signal, .name = "WiFi signal", .unit = "dBm",
		      .device_class = "signal_strength",
		      .state_class = ESPHOME_STATE_CLASS_MEASUREMENT,
		      .entity_category = ESPHOME_ENTITY_CATEGORY_DIAGNOSTIC);
ESPHOME_SWITCH_DEFINE(flash, .name = "Flash LED", .icon = "mdi:flash",
		      .entity_category = ESPHOME_ENTITY_CATEGORY_CONFIG, .on_switch = flash_set);
ESPHOME_BUTTON_DEFINE(snapshot, .name = "Upload snapshot", .icon = "mdi:camera",
		      .on_press = snapshot_press);

int ha_set_key(const char *b64)
{
	uint8_t key[32];
	size_t len = 0U;
	int ret;

	if (base64_decode(key, sizeof(key), &len, (const uint8_t *)b64, strlen(b64)) != 0 ||
	    len != sizeof(key)) {
		return -EINVAL;
	}
	ret = esphome_api_set_psk(key);
	memset(key, 0, sizeof(key));
	return ret;
}

static int rssi_get(void)
{
	struct net_if *iface = net_if_get_wifi_sta();
	struct wifi_iface_status st = {0};

	if (!IS_ENABLED(CONFIG_WIFI) || iface == NULL ||
	    net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &st, sizeof(st)) != 0 ||
	    st.state < WIFI_STATE_ASSOCIATED) {
		return 0;
	}
	return st.rssi;
}

static void ha_thread(void *p1, void *p2, void *p3)
{
	int last_problem = -1;
	uint32_t last_uploads = UINT32_MAX;
	int last_flash = -1;
	int last_rssi = 1;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	/* cfg_init() loads only "prusa"; the API key lives here. */
	(void)settings_load_subtree("esphome_api");

	for (uint32_t n = 0U;; n++) {
		struct connect_status st;
		struct cfg c;

		connect_get_status(&st);
		cfg_get(&c);

		/* Unknown in Home Assistant until the first attempt. */
		if (st.last_ms != 0) {
			int problem = st.stopped || st.last_result < 200 || st.last_result >= 300;

			if (problem != last_problem) {
				(void)esphome_api_publish_bool(&upload_problem, problem);
				last_problem = problem;
			}
		}
		if (st.uploads != last_uploads) {
			(void)esphome_api_publish_float(&uploads, (float)st.uploads);
			last_uploads = st.uploads;
		}
		if (c.flash != last_flash) {
			(void)esphome_api_publish_bool(&flash, c.flash);
			last_flash = c.flash;
		}
		if (IS_ENABLED(CONFIG_WIFI) && n % RSSI_POLLS == 0U) {
			int rssi = rssi_get();

			if (rssi != 0 && rssi != last_rssi) {
				(void)esphome_api_publish_float(&wifi_signal, (float)rssi);
				last_rssi = rssi;
			}
		}
		memset(&c, 0, sizeof(c)); /* holds the token and password */

		k_sleep(K_MSEC(POLL_MS));
	}
}

K_THREAD_DEFINE(prusa_ha, 1536, ha_thread, NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0,
		SYS_FOREVER_MS);

void ha_start(void)
{
	k_thread_start(prusa_ha);
}
