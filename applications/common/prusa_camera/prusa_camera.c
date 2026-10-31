/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * HTTPS transport for the camera service client. One TLS connection per
 * request: uploads are 10 s or more apart, too far apart to keep one open.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/net/http/client.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>

#include "prusa_camera.h"

LOG_MODULE_REGISTER(prusa_camera, CONFIG_PRUSA_CAMERA_LOG_LEVEL);

#define HOST CONFIG_PRUSA_CAMERA_HOST

static const uint8_t ca_cert[] = {
#include "prusa_camera_ca.der.inc"
};

static uint8_t recv_buf[512];
static char ua_hdr[64];
static char info_json[512];

static int on_response(struct http_response *rsp, enum http_final_call final, void *user_data)
{
	if (final == HTTP_DATA_FINAL) {
		*(int *)user_data = rsp->http_status_code;
	}
	return 0;
}

static int tls_connect(void)
{
	static const sec_tag_t tags[] = {CONFIG_PRUSA_CAMERA_CA_TAG};
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

static int request(const struct prusa_camera_auth *auth, enum http_method method, const char *path,
		   const char *type, const uint8_t *body, size_t len)
{
	static char token_hdr[sizeof("Token: \r\n") + PRUSA_CAMERA_TOKEN_LEN];
	static char fp_hdr[sizeof("Fingerprint: \r\n") + PRUSA_CAMERA_FINGERPRINT_MAX];
	const char *headers[] = {token_hdr, fp_hdr, ua_hdr, NULL};
	int http_status = -EIO;
	int sock;
	int ret;

	/* Checked here so a bad value is never sent: the first call binds it. */
	if (ua_hdr[0] == '\0' || !prusa_camera_token_valid(auth->token) ||
	    strlen(auth->fingerprint) < PRUSA_CAMERA_FINGERPRINT_MIN ||
	    strlen(auth->fingerprint) > PRUSA_CAMERA_FINGERPRINT_MAX) {
		return -EINVAL;
	}
	snprintf(token_hdr, sizeof(token_hdr), "Token: %s\r\n", auth->token);
	snprintf(fp_hdr, sizeof(fp_hdr), "Fingerprint: %s\r\n", auth->fingerprint);

	sock = tls_connect();
	if (sock < 0) {
		ret = sock;
		goto out;
	}

	struct http_request req = {
		.method = method,
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

	ret = http_client_req(sock, &req, CONFIG_PRUSA_CAMERA_TIMEOUT_MS, &http_status);
	zsock_close(sock);
	if (ret >= 0) {
		ret = http_status;
	}
out:
	memset(token_hdr, 0, sizeof(token_hdr));
	return ret;
}

int prusa_camera_get_info(const struct prusa_camera_auth *auth)
{
	return request(auth, HTTP_GET, "/v1/camera-info", NULL, NULL, 0);
}

int prusa_camera_put_info(const struct prusa_camera_auth *auth,
			  const struct prusa_camera_info *info)
{
	int ret = prusa_camera_info_encode(info, info_json, sizeof(info_json));

	if (ret < 0) {
		return ret;
	}
	return request(auth, HTTP_PUT, "/v1/camera-info", "application/json",
		       (const uint8_t *)info_json, strlen(info_json));
}

int prusa_camera_put_snapshot(const struct prusa_camera_auth *auth, const uint8_t *jpeg, size_t len)
{
	if (len == 0 || len > PRUSA_CAMERA_SNAPSHOT_MAX) {
		return -EMSGSIZE; /* the service would answer 413 */
	}
	return request(auth, HTTP_PUT, "/v1/snapshot", "image/jpeg", jpeg, len);
}

int prusa_camera_init(const char *user_agent)
{
	if (snprintf(ua_hdr, sizeof(ua_hdr), "User-Agent: %s\r\n", user_agent) >= sizeof(ua_hdr)) {
		return -EINVAL;
	}
	return tls_credential_add(CONFIG_PRUSA_CAMERA_CA_TAG, TLS_CREDENTIAL_CA_CERTIFICATE,
				  ca_cert, sizeof(ca_cert));
}
