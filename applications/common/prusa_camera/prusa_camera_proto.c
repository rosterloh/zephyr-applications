/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * The network-free half of the camera service client: the camera-info body,
 * and what the service's answers mean. Unit tested on native_sim.
 */

#include <ctype.h>
#include <errno.h>
#include <string.h>

#include <zephyr/data/json.h>
#include <zephyr/net/http/status.h>
#include <zephyr/sys/util.h>

#include "prusa_camera.h"

static const char *const trigger_names[] = {
	[PRUSA_CAMERA_TEN_SEC] = "TEN_SEC",
	[PRUSA_CAMERA_THIRTY_SEC] = "THIRTY_SEC",
	[PRUSA_CAMERA_SIXTY_SEC] = "SIXTY_SEC",
	[PRUSA_CAMERA_TEN_MIN] = "TEN_MIN",
};
static const uint32_t trigger_secs[] = {10, 30, 60, 600};

/* The body as the encoder sees it. */
struct res_json {
	uint32_t width;
	uint32_t height;
};
struct net_json {
	const char *wifi_mac;
	const char *wifi_ipv4;
	const char *wifi_ssid;
};
struct config_json {
	const char *name;
	const char *trigger_scheme;
	uint32_t rotation;
	struct res_json resolution;
	struct net_json network_info;
};
struct device_json {
	const char *type;
	const char *firmware_version;
	const char *hardware_version;
	const char *manufacturer;
	const char *model;
};
struct info_json {
	struct config_json config;
	const char *features[1];
	size_t features_len;
	struct device_json device;
};

static const struct json_obj_descr res_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct res_json, width, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(struct res_json, height, JSON_TOK_UINT),
};
static const struct json_obj_descr net_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct net_json, wifi_mac, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct net_json, wifi_ipv4, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct net_json, wifi_ssid, JSON_TOK_STRING),
};
/* Order matters below: resolution and network_info are the last two. */
static const struct json_obj_descr config_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct config_json, name, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct config_json, trigger_scheme, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct config_json, rotation, JSON_TOK_UINT),
	JSON_OBJ_DESCR_OBJECT(struct config_json, resolution, res_descr),
	JSON_OBJ_DESCR_OBJECT(struct config_json, network_info, net_descr),
};
static const struct json_obj_descr device_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct device_json, type, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct device_json, firmware_version, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct device_json, hardware_version, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct device_json, manufacturer, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct device_json, model, JSON_TOK_STRING),
};
static const struct json_obj_descr info_descr[] = {
	JSON_OBJ_DESCR_OBJECT(struct info_json, config, config_descr),
	JSON_OBJ_DESCR_ARRAY(struct info_json, features, 1, features_len, JSON_TOK_STRING),
	JSON_OBJ_DESCR_OBJECT(struct info_json, device, device_descr),
};

static bool set(const char *s)
{
	return s != NULL && s[0] != '\0';
}

/*
 * The service rejects unknown keys and the encoder cannot skip a field, so
 * copy only the descriptors of strings that are set (NULL would crash it).
 */
static size_t pick_strings(struct json_obj_descr *out, const struct json_obj_descr *all, size_t n,
			   const void *val)
{
	size_t k = 0;

	for (size_t i = 0; i < n; i++) {
		if (all[i].type == JSON_TOK_STRING &&
		    !set(*(const char *const *)((const char *)val + all[i].offset))) {
			continue;
		}
		out[k++] = all[i];
	}
	return k;
}

int prusa_camera_info_encode(const struct prusa_camera_info *info, char *buf, size_t len)
{
	struct json_obj_descr net[ARRAY_SIZE(net_descr)];
	struct json_obj_descr dev[ARRAY_SIZE(device_descr)];
	struct json_obj_descr cfg[ARRAY_SIZE(config_descr)];
	struct json_obj_descr top[ARRAY_SIZE(info_descr)];
	size_t n_cfg = 3; /* name, trigger_scheme, rotation */
	struct info_json v = {
		.config = {.name = info->name,
			   .rotation = info->rotation,
			   .resolution = {info->width, info->height},
			   .network_info = {info->wifi_mac, info->wifi_ipv4, info->wifi_ssid}},
		.features = {"snapshot"},
		.features_len = 1,
		.device = {info->type, info->firmware_version, info->hardware_version,
			   info->manufacturer, info->model},
	};
	size_t n_net;

	if (!set(info->name) || strlen(info->name) > PRUSA_CAMERA_NAME_MAX || !set(info->type) ||
	    !set(info->firmware_version) || strlen(info->firmware_version) > 16 ||
	    !set(info->hardware_version) || strlen(info->hardware_version) > 32 ||
	    info->trigger >= ARRAY_SIZE(trigger_names) ||
	    (info->width == 0) != (info->height == 0)) {
		return -EINVAL;
	}
	v.config.trigger_scheme = trigger_names[info->trigger];

	memcpy(cfg, config_descr, sizeof(cfg));
	if (info->width != 0) {
		cfg[n_cfg++] = config_descr[3];
	}
	n_net = pick_strings(net, net_descr, ARRAY_SIZE(net_descr), &v.config.network_info);
	if (n_net > 0) {
		cfg[n_cfg] = config_descr[4];
		cfg[n_cfg].object.sub_descr = net;
		cfg[n_cfg].object.sub_descr_len = n_net;
		n_cfg++;
	}

	memcpy(top, info_descr, sizeof(top));
	top[0].object.sub_descr = cfg;
	top[0].object.sub_descr_len = n_cfg;
	top[2].object.sub_descr = dev;
	top[2].object.sub_descr_len =
		pick_strings(dev, device_descr, ARRAY_SIZE(device_descr), &v.device);

	return json_obj_encode_buf(top, ARRAY_SIZE(top), &v, buf, len);
}

enum prusa_camera_next prusa_camera_next(int result)
{
	if (result >= HTTP_200_OK && result < HTTP_300_MULTIPLE_CHOICES) {
		return PRUSA_CAMERA_NEXT_OK;
	}
	if (result == HTTP_429_TOO_MANY_REQUESTS || result == HTTP_503_SERVICE_UNAVAILABLE) {
		return PRUSA_CAMERA_NEXT_WAIT;
	}
	/* Refused locally: what the service would have rejected. */
	if (result == -EINVAL || result == -EMSGSIZE) {
		return PRUSA_CAMERA_NEXT_STOP;
	}
	/* -errno, or a 5xx: the network or the service. 4xx: our request. */
	if (result < 0 || result >= HTTP_500_INTERNAL_SERVER_ERROR) {
		return PRUSA_CAMERA_NEXT_RETRY;
	}
	return PRUSA_CAMERA_NEXT_STOP;
}

const char *prusa_camera_describe(int result)
{
	switch (result) {
	case -EINVAL:
		return "token must be 20 letters/digits (or bad camera info)";
	case -EMSGSIZE:
	case HTTP_413_PAYLOAD_TOO_LARGE:
		return "image over 1 MB";
	case HTTP_400_BAD_REQUEST:
		return "bad request";
	case HTTP_401_UNAUTHORIZED:
		return "token or fingerprint missing";
	case HTTP_403_FORBIDDEN:
		return "unknown token, fingerprint mismatch, or first call later than 15 min "
		       "after creating the camera";
	case HTTP_411_LENGTH_REQUIRED:
		return "Content-Length missing";
	case HTTP_415_UNSUPPORTED_MEDIA_TYPE:
		return "not a JPEG";
	case HTTP_422_UNPROCESSABLE_ENTITY:
		return "rejected (token must be 20 characters)";
	case HTTP_429_TOO_MANY_REQUESTS:
	case HTTP_503_SERVICE_UNAVAILABLE:
		return "rate limited";
	default:
		break;
	}
	if (result >= HTTP_200_OK && result < HTTP_300_MULTIPLE_CHOICES) {
		return "ok";
	}
	if (result >= HTTP_500_INTERNAL_SERVER_ERROR) {
		return "server error";
	}
	return result < 0 ? "network or TLS error" : "unexpected status";
}

bool prusa_camera_token_valid(const char *token)
{
	size_t n = strlen(token);

	if (n != PRUSA_CAMERA_TOKEN_LEN) {
		return false;
	}
	for (size_t i = 0; i < n; i++) {
		if (!isalnum((unsigned char)token[i])) {
			return false;
		}
	}
	return true;
}

uint32_t prusa_camera_trigger_seconds(enum prusa_camera_trigger t)
{
	return t < ARRAY_SIZE(trigger_secs) ? trigger_secs[t]
					    : trigger_secs[PRUSA_CAMERA_THIRTY_SEC];
}

int prusa_camera_trigger_from_seconds(uint32_t seconds)
{
	for (size_t i = 0; i < ARRAY_SIZE(trigger_secs); i++) {
		if (trigger_secs[i] == seconds) {
			return i;
		}
	}
	return -EINVAL;
}
