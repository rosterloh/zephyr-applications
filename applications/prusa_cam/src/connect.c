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

#include <zephyr/app_version.h>

#include "cam.h"
#include "cfg.h"
#include "connect.h"
#include "prusa_camera.h"

LOG_MODULE_REGISTER(connect, LOG_LEVEL_INF);

static K_SEM_DEFINE(kick, 0, 1);
static K_MUTEX_DEFINE(status_lock);
static struct connect_status status;
static bool info_sent;

static int send_info(const struct prusa_camera_auth *auth, const struct cfg *c)
{
	const struct prusa_camera_info info = {
		.name = CONFIG_NET_HOSTNAME,
		.trigger = PRUSA_CAMERA_THIRTY_SEC,
		.rotation = c->rotation * 90,
		.width = cfg_resolutions[c->resolution].width,
		.height = cfg_resolutions[c->resolution].height,
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

static void record(int result, bool uploaded)
{
	k_mutex_lock(&status_lock, K_FOREVER);
	status.last_ms = k_uptime_get();
	status.last_result = result;
	status.uploads += uploaded;
	k_mutex_unlock(&status_lock);
}

static void connect_thread(void *p1, void *p2, void *p3)
{
	struct cfg c;
	struct prusa_camera_auth auth = {.token = c.token, .fingerprint = cfg_fingerprint()};
	int ret;

	while (true) {
		cfg_get(&c);
		k_sem_take(&kick, K_SECONDS(c.interval_s));

		cfg_get(&c);
		if (c.token[0] == '\0') {
			continue;
		}

		if (!info_sent) {
			ret = send_info(&auth, &c);
			info_sent = (ret >= 200 && ret < 300);
			if (!info_sent) {
				LOG_WRN("Info -> %d", ret);
				record(ret, false);
				continue;
			}
		}

		ret = send_snapshot(&auth);
		record(ret, ret >= 200 && ret < 300);
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
	k_thread_start(connect_tid);
	return 0;
}

void connect_kick(void)
{
	info_sent = false;
	k_sem_give(&kick);
}

void connect_get_status(struct connect_status *out)
{
	k_mutex_lock(&status_lock, K_FOREVER);
	*out = status;
	k_mutex_unlock(&status_lock);
}
