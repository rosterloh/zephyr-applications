/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>
#include <strings.h>

#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

#include "cfg.h"
#include "prusa_camera.h"

LOG_MODULE_REGISTER(cfg, LOG_LEVEL_INF);

const struct cfg_resolution cfg_resolutions[] = {
	{"QVGA", 320, 240}, {"VGA", 640, 480},    {"SVGA", 800, 600},
	{"XGA", 1024, 768}, {"SXGA", 1280, 1024}, {"UXGA", 1600, 1200},
};
const uint8_t cfg_resolution_count = ARRAY_SIZE(cfg_resolutions);

static struct cfg cfg = {
	.name = CONFIG_NET_HOSTNAME,
	.interval_s = 30,
	.resolution = 5, /* UXGA: the service suggests 1280x720..1920x1080 */
	.quality = 80,
	.auto_exposure = true,
	.auto_wb = true,
	.auto_gain = true,
};
static K_MUTEX_DEFINE(cfg_lock);
/* The service wants 16..64 characters; the MAC alone is 12 hex digits. */
#define FP_PREFIX "zephyrcam"
#define FP_ID_MAX ((40 - (sizeof(FP_PREFIX) - 1)) / 2)
static char fingerprint[sizeof(FP_PREFIX) + 2 * FP_ID_MAX];

static int cfg_load(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	struct cfg tmp = {0};
	ssize_t n;

	if (strcmp(key, "v1") != 0) {
		return 0;
	}
	n = read_cb(cb_arg, &tmp, sizeof(tmp));
	if (n != sizeof(tmp)) {
		LOG_WRN("Stored config has the wrong size, using defaults");
		return 0;
	}
	tmp.token[CFG_TOKEN_MAX] = '\0';
	tmp.password[CFG_PASSWORD_MAX] = '\0';
	tmp.name[CFG_NAME_MAX] = '\0';
	cfg = tmp;
	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(prusa, "prusa", NULL, cfg_load, NULL, NULL);

int cfg_init(void)
{
	uint8_t id[FP_ID_MAX];
	ssize_t n = hwinfo_get_device_id(id, sizeof(id));
	int ret;

	if (n <= 0) {
		LOG_ERR("No device id (%d)", (int)n);
		return n < 0 ? (int)n : -EIO;
	}
	strcpy(fingerprint, FP_PREFIX);
	bin2hex(id, n, fingerprint + strlen(FP_PREFIX), sizeof(fingerprint) - strlen(FP_PREFIX));

	ret = settings_subsys_init();
	if (ret < 0) {
		return ret;
	}
	return settings_load_subtree("prusa");
}

void cfg_get(struct cfg *out)
{
	k_mutex_lock(&cfg_lock, K_FOREVER);
	*out = cfg;
	k_mutex_unlock(&cfg_lock);
}

int cfg_set(const struct cfg *in)
{
	struct cfg tmp = *in;
	int ret;

	if (prusa_camera_trigger_from_seconds(tmp.interval_s) < 0 ||
	    (tmp.token[0] != '\0' && !prusa_camera_token_valid(tmp.token)) || tmp.name[0] == '\0' ||
	    strnlen(tmp.name, sizeof(tmp.name)) > CFG_NAME_MAX ||
	    !IN_RANGE(tmp.brightness, -2, 2) || !IN_RANGE(tmp.contrast, -2, 2) ||
	    !IN_RANGE(tmp.saturation, -2, 2) || tmp.resolution >= cfg_resolution_count ||
	    tmp.quality > 100 || tmp.rotation > 3 ||
	    (tmp.ip != 0 && (tmp.netmask == 0 || tmp.gateway == 0)) ||
	    strnlen(tmp.token, sizeof(tmp.token)) > CFG_TOKEN_MAX ||
	    strnlen(tmp.password, sizeof(tmp.password)) > CFG_PASSWORD_MAX) {
		return -EINVAL;
	}

	k_mutex_lock(&cfg_lock, K_FOREVER);
	ret = settings_save_one("prusa/v1", &tmp, sizeof(tmp));
	if (ret == 0) {
		cfg = tmp;
	}
	k_mutex_unlock(&cfg_lock);

	if (ret == 0) {
		cfg_changed();
	}
	return ret;
}

int cfg_resolution_find(const char *name)
{
	for (int i = 0; i < cfg_resolution_count; i++) {
		if (strcasecmp(name, cfg_resolutions[i].name) == 0) {
			return i;
		}
	}
	return -1;
}

const char *cfg_fingerprint(void)
{
	return fingerprint;
}
