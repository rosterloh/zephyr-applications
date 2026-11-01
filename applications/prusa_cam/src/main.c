/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Prusa Connect camera: periodic JPEG snapshots from an OV2640 uploaded to
 * Prusa Connect, configured from the shell or a small web page.
 */

#include <stdlib.h>
#include <string.h>

#include <zephyr/dfu/mcuboot.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/mgmt/hawkbit/autohandler.h>
#include <zephyr/mgmt/hawkbit/hawkbit.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/shell/shell.h>

#include "app_img_confirm.h"
#include "app_watchdog.h"
#include "app_wifi.h"
#include "cam.h"
#include "cfg.h"
#include "connect.h"
#include "http_util.h"
#include "prusa_camera.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int web_init(void);

void cfg_changed(void)
{
	struct cfg c;

	cfg_get(&c);
	cam_apply(&c);
	connect_kick();
}

#define IP4(a) (a) >> 24, ((a) >> 16) & 0xFF, ((a) >> 8) & 0xFF, (a) & 0xFF

/* "dhcp" or "0" clears; otherwise A.B.C.D. */
static bool parse_ip(const char *val, uint32_t *out)
{
	if (strcmp(val, "dhcp") == 0 || strcmp(val, "0") == 0) {
		*out = 0;
		return true;
	}
	return http_parse_ipv4(val, out) == 0;
}

static int cmd_show(const struct shell *sh, size_t argc, char **argv)
{
	struct cfg c;
	struct connect_status st;

	cfg_get(&c);
	connect_get_status(&st);
	shell_print(sh, "name:        %s", c.name);
	shell_print(sh, "token:       %s", c.token[0] ? "set" : "not set");
	shell_print(sh, "password:    %s", c.password[0] ? "set" : "not set");
	shell_print(sh, "fingerprint: %s", cfg_fingerprint());
	shell_print(sh, "interval:    %u s", c.interval_s);
	shell_print(sh, "resolution:  %s", cfg_resolutions[c.resolution].name);
	shell_print(sh, "quality:     %u", c.quality);
	shell_print(sh, "hflip/vflip: %d/%d, flash: %d", c.hflip, c.vflip, c.flash);
	shell_print(sh, "rotation:    %u", c.rotation * 90);
	shell_print(sh, "brightness/contrast/saturation: %d/%d/%d", c.brightness, c.contrast,
		    c.saturation);
	shell_print(sh, "auto exposure/wb/gain: %d/%d/%d", c.auto_exposure, c.auto_wb, c.auto_gain);
	if (c.ip == 0) {
		shell_print(sh, "address:     DHCP");
	} else {
		shell_print(sh,
			    "address:     %u.%u.%u.%u/%u.%u.%u.%u gw %u.%u.%u.%u dns %u.%u.%u.%u",
			    IP4(c.ip), IP4(c.netmask), IP4(c.gateway), IP4(c.dns));
	}
	shell_print(sh, "uploads:     %u, last result %d (%s)%s", st.uploads, st.last_result,
		    st.last_ms ? prusa_camera_describe(st.last_result) : "none yet",
		    st.stopped ? ", stopped until the config changes" : "");
	memset(&c, 0, sizeof(c));
	return 0;
}

static int cmd_set(const struct shell *sh, size_t argc, char **argv)
{
	const char *key = argv[1];
	const char *val = argv[2];
	struct cfg c;
	int ret;

	cfg_get(&c);
	if (strcmp(key, "name") == 0 && strlen(val) <= CFG_NAME_MAX) {
		strcpy(c.name, val);
	} else if (strcmp(key, "token") == 0 && strlen(val) <= CFG_TOKEN_MAX) {
		strcpy(c.token, val);
	} else if (strcmp(key, "password") == 0 && strlen(val) <= CFG_PASSWORD_MAX) {
		strcpy(c.password, val);
	} else if (strcmp(key, "interval") == 0) {
		c.interval_s = CLAMP(strtol(val, NULL, 10), 0, UINT16_MAX);
	} else if (strcmp(key, "resolution") == 0 && cfg_resolution_find(val) >= 0) {
		c.resolution = cfg_resolution_find(val);
	} else if (strcmp(key, "quality") == 0) {
		c.quality = CLAMP(strtol(val, NULL, 10), 0, UINT8_MAX);
	} else if (strcmp(key, "hflip") == 0) {
		c.hflip = strtol(val, NULL, 10) != 0;
	} else if (strcmp(key, "vflip") == 0) {
		c.vflip = strtol(val, NULL, 10) != 0;
	} else if (strcmp(key, "flash") == 0) {
		c.flash = strtol(val, NULL, 10) != 0;
	} else if (strcmp(key, "rotation") == 0 && strtol(val, NULL, 10) % 90 == 0) {
		c.rotation = CLAMP(strtol(val, NULL, 10) / 90, 0, UINT8_MAX);
	} else if (strcmp(key, "brightness") == 0) {
		c.brightness = CLAMP(strtol(val, NULL, 10), INT8_MIN, INT8_MAX);
	} else if (strcmp(key, "contrast") == 0) {
		c.contrast = CLAMP(strtol(val, NULL, 10), INT8_MIN, INT8_MAX);
	} else if (strcmp(key, "saturation") == 0) {
		c.saturation = CLAMP(strtol(val, NULL, 10), INT8_MIN, INT8_MAX);
	} else if (strcmp(key, "auto_exposure") == 0) {
		c.auto_exposure = strtol(val, NULL, 10) != 0;
	} else if (strcmp(key, "auto_wb") == 0) {
		c.auto_wb = strtol(val, NULL, 10) != 0;
	} else if (strcmp(key, "auto_gain") == 0) {
		c.auto_gain = strtol(val, NULL, 10) != 0;
	} else if ((strcmp(key, "ip") == 0 && parse_ip(val, &c.ip)) ||
		   (strcmp(key, "netmask") == 0 && parse_ip(val, &c.netmask)) ||
		   (strcmp(key, "gateway") == 0 && parse_ip(val, &c.gateway)) ||
		   (strcmp(key, "dns") == 0 && parse_ip(val, &c.dns))) {
		/* parsed in place */
	} else {
		shell_error(sh, "bad key or value");
		return -EINVAL;
	}

	ret = cfg_set(&c);
	memset(&c, 0, sizeof(c));
	if (ret < 0) {
		shell_error(sh, "rejected (%d)", ret);
	}
	return ret;
}

static int cmd_test(const struct shell *sh, size_t argc, char **argv)
{
	connect_test();
	shell_print(sh, "test queued; see `prusa show` in a few seconds");
	return 0;
}

static int cmd_snap(const struct shell *sh, size_t argc, char **argv)
{
	connect_kick();
	shell_print(sh, "upload queued");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	prusa_cmds,
	SHELL_CMD_ARG(show, NULL, "Show configuration and upload status", cmd_show, 1, 0),
	SHELL_CMD_ARG(
		set, NULL,
		"Set <name|token|password|interval|resolution|quality|hflip|vflip|flash|\n"
		"     rotation|brightness|contrast|saturation|auto_exposure|auto_wb|auto_gain|\n"
		"     ip|netmask|gateway|dns> <value>\n"
		"token: 20 letters/digits; interval: 10 30 60 600\n"
		"resolution: QVGA VGA SVGA XGA SXGA UXGA; rotation: 0 90 180 270\n"
		"brightness/contrast/saturation: -2..2; auto_*: 0 or 1\n"
		"ip: A.B.C.D or dhcp; set netmask and gateway first; applies on reconnect",
		cmd_set, 3, 0),
	SHELL_CMD_ARG(test, NULL, "Check token and fingerprint with the service", cmd_test, 1, 0),
	SHELL_CMD_ARG(snap, NULL, "Upload a snapshot now", cmd_snap, 1, 0), SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(prusa, &prusa_cmds, "Prusa Connect camera", NULL);

/* Only on a confirmed image: the autohandler reboots an unconfirmed one, which
 * would cut the img_confirm deadline short. A confirmed image starts it even
 * with a broken camera, so OTA can fix it.
 */
static void hawkbit_start_fn(struct k_work *work)
{
	static bool started;
	int ret;

	if (!IS_ENABLED(CONFIG_HAWKBIT) || started || !boot_is_img_confirmed()) {
		return;
	}
	ret = hawkbit_init();
	if (ret < 0) {
		LOG_ERR("hawkBit init failed (%d)", ret);
		return;
	}
	started = true;
	hawkbit_autohandler(true);
}

static K_WORK_DEFINE(hawkbit_start, hawkbit_start_fn);

static bool cam_ok;
static struct net_mgmt_event_callback ipv4_cb;

/* Healthy = a frame from the camera and an IPv4 address. */
static void ipv4_added(struct net_mgmt_event_callback *cb, uint64_t event, struct net_if *iface)
{
	if (cam_ok) {
		app_img_confirm();
	}
	k_work_submit(&hawkbit_start);
}

static bool cam_check(void)
{
	const uint8_t *jpeg;
	size_t len;

	if (cam_frame(&jpeg, &len) < 0) {
		return false;
	}
	cam_release();
	return true;
}

int main(void)
{
	int ret;

	app_watchdog_init();
	app_img_confirm_arm();

	ret = cfg_init();
	if (ret < 0) {
		LOG_ERR("Config init failed (%d)", ret);
		return ret;
	}
	LOG_INF("Fingerprint %s", cfg_fingerprint());

	ret = cam_init();
	if (ret < 0) {
		LOG_ERR("Camera init failed (%d)", ret); /* keep going: the shell still works */
	} else {
		cam_ok = cam_check();
	}
	if (!cam_ok) {
		LOG_ERR("No frame from the camera; a trial image will be reverted");
	}

	ret = connect_init();
	if (ret < 0) {
		LOG_ERR("Connect init failed (%d)", ret);
		return ret;
	}

	ret = web_init();
	if (ret < 0) {
		LOG_ERR("HTTP server failed (%d)", ret);
	}

	net_mgmt_init_event_callback(&ipv4_cb, ipv4_added, NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&ipv4_cb);
	k_work_submit(&hawkbit_start);

	return app_wifi_init();
}
