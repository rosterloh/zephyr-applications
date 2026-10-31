/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Prusa camera service API v1 (https://camera-service.prusa3d.com/docs/):
 * PUT /v1/camera-info (JSON) once and after each config change, then
 * PUT /v1/snapshot (JPEG, at most 1 MB and 6 a minute), each with Token,
 * Fingerprint and User-Agent headers, over HTTPS.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/data/json.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/client.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>

#include <zephyr/app_version.h>

#include "cam.h"
#include "cfg.h"
#include "connect.h"

LOG_MODULE_REGISTER(connect, LOG_LEVEL_INF);

#define HOST       CONFIG_PRUSA_CAM_CONNECT_HOST
#define CA_TAG     1
#define TIMEOUT_MS 10000

static const uint8_t ca_cert[] = {
#include "isrg_root_x1.der.inc"
};

static K_SEM_DEFINE(kick, 0, 1);
static K_MUTEX_DEFINE(status_lock);
static struct connect_status status;
static uint8_t recv_buf[512];
static bool info_sent;

static int on_response(struct http_response *rsp, enum http_final_call final, void *user_data)
{
	if (final == HTTP_DATA_FINAL) {
		*(int *)user_data = rsp->http_status_code;
	}
	return 0;
}

static int tls_connect(void)
{
	static const sec_tag_t tags[] = {CA_TAG};
	struct zsock_addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
	struct zsock_addrinfo *res;
	int sock;
	int ret;

	ret = zsock_getaddrinfo(HOST, "443", &hints, &res);
	if (ret != 0) {
		LOG_WRN("DNS %s failed (%d)", HOST, ret);
		return -EHOSTUNREACH;
	}

	sock = zsock_socket(res->ai_family, SOCK_STREAM, IPPROTO_TLS_1_2);
	if (sock < 0) {
		ret = -errno;
		goto out;
	}
	if (zsock_setsockopt(sock, SOL_TLS, TLS_SEC_TAG_LIST, tags, sizeof(tags)) < 0 ||
	    zsock_setsockopt(sock, SOL_TLS, TLS_HOSTNAME, HOST, sizeof(HOST)) < 0 ||
	    zsock_connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
		ret = -errno;
		zsock_close(sock);
		goto out;
	}
	ret = sock;
out:
	zsock_freeaddrinfo(res);
	return ret;
}

/* PUT `body` to `path`; returns the HTTP status or -errno. */
static int put(const char *path, const char *type, const uint8_t *body, size_t len)
{
	static char token_hdr[sizeof("Token: \r\n") + CFG_TOKEN_MAX];
	static char fp_hdr[96];
	static const char ua_hdr[] = "User-Agent: prusa_cam/" APP_VERSION_STRING "\r\n";
	const char *headers[] = {token_hdr, fp_hdr, ua_hdr, NULL};
	struct cfg c;
	int http_status = -EIO;
	int sock;
	int ret;

	cfg_get(&c);
	snprintf(token_hdr, sizeof(token_hdr), "Token: %s\r\n", c.token);
	snprintf(fp_hdr, sizeof(fp_hdr), "Fingerprint: %s\r\n", cfg_fingerprint());

	sock = tls_connect();
	if (sock < 0) {
		return sock;
	}

	struct http_request req = {
		.method = HTTP_PUT,
		.url = path,
		.host = HOST,
		.protocol = "HTTP/1.1",
		.content_type_value = type,
		.header_fields = headers,
		.payload = (const char *)body,
		.payload_len = len,
		.response = on_response,
		.recv_buf = recv_buf,
		.recv_buf_len = sizeof(recv_buf),
	};

	ret = http_client_req(sock, &req, TIMEOUT_MS, &http_status);
	zsock_close(sock);
	memset(token_hdr, 0, sizeof(token_hdr));

	return ret < 0 ? ret : http_status;
}

/* PUT /v1/camera-info body; the service rejects unknown keys. */
struct info_resolution {
	uint32_t width;
	uint32_t height;
};
struct info_config {
	const char *name;
	uint32_t rotation;
	struct info_resolution resolution;
};
struct info_device {
	const char *type;
	const char *firmware_version;
	const char *hardware_version;
	const char *manufacturer;
	const char *model;
};
struct info {
	struct info_config config;
	struct info_device device;
};

static const struct json_obj_descr info_resolution_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct info_resolution, width, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(struct info_resolution, height, JSON_TOK_UINT),
};
static const struct json_obj_descr info_config_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct info_config, name, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct info_config, rotation, JSON_TOK_UINT),
	JSON_OBJ_DESCR_OBJECT(struct info_config, resolution, info_resolution_descr),
};
static const struct json_obj_descr info_device_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct info_device, type, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct info_device, firmware_version, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct info_device, hardware_version, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct info_device, manufacturer, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct info_device, model, JSON_TOK_STRING),
};
static const struct json_obj_descr info_descr[] = {
	JSON_OBJ_DESCR_OBJECT(struct info, config, info_config_descr),
	JSON_OBJ_DESCR_OBJECT(struct info, device, info_device_descr),
};

static int send_info(void)
{
	static char json[320];
	struct cfg c;
	struct info info = {
		.config = {.name = CONFIG_NET_HOSTNAME},
		.device = {.type = "ESP32",
			   .firmware_version = APP_VERSION_STRING,
			   .hardware_version = "AI-Thinker ESP32-CAM",
			   .manufacturer = "AI-Thinker",
			   .model = "ESP32-CAM"},
	};
	int ret;

	cfg_get(&c);
	info.config.resolution.width = cfg_resolutions[c.resolution].width;
	info.config.resolution.height = cfg_resolutions[c.resolution].height;
	info.config.rotation = c.rotation * 90;
	memset(&c, 0, sizeof(c));

	ret = json_obj_encode_buf(info_descr, ARRAY_SIZE(info_descr), &info, json, sizeof(json));
	if (ret < 0) {
		return ret;
	}
	return put("/v1/camera-info", "application/json", json, strlen(json));
}

static int send_snapshot(void)
{
	const uint8_t *jpeg;
	size_t len;
	int ret = cam_snapshot(&jpeg, &len);

	if (ret < 0) {
		return ret;
	}
	ret = put("/v1/snapshot", "image/jpeg", jpeg, len);
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
	int ret;

	while (true) {
		cfg_get(&c);
		k_sem_take(&kick, K_SECONDS(c.interval_s));

		cfg_get(&c);
		if (c.token[0] == '\0') {
			continue;
		}

		if (!info_sent) {
			ret = send_info();
			info_sent = (ret >= 200 && ret < 300);
			if (!info_sent) {
				LOG_WRN("Info -> %d", ret);
				record(ret, false);
				continue;
			}
		}

		ret = send_snapshot();
		record(ret, ret >= 200 && ret < 300);
	}
}

K_THREAD_DEFINE(connect_tid, CONFIG_PRUSA_CAM_CONNECT_STACK_SIZE, connect_thread, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, SYS_FOREVER_MS);

int connect_init(void)
{
	int ret =
		tls_credential_add(CA_TAG, TLS_CREDENTIAL_CA_CERTIFICATE, ca_cert, sizeof(ca_cert));

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
