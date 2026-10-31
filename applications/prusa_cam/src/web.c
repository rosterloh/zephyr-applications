/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Config page and API. Everything but the (secret-free) page itself needs
 * HTTP basic auth as "admin" with the password set from the shell; with no
 * password set the API stays closed.
 *
 * Each resource is served to one client at a time (the server's `holder`),
 * so per-resource static state is enough.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <zephyr/data/json.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/dns_sd.h>
#include <zephyr/net/http/service.h>

#include "cam.h"
#include "cfg.h"
#include "connect.h"
#include "exif.h"
#include "http_util.h"
#include "prusa_camera.h"

LOG_MODULE_REGISTER(web, LOG_LEVEL_INF);

#define USER     "admin"
#define BODY_MAX 512

HTTP_SERVER_REGISTER_HEADER_CAPTURE(capture_authorization, "Authorization");

static uint16_t http_port = 80;
HTTP_SERVICE_DEFINE(prusa_http, NULL, &http_port, CONFIG_HTTP_SERVER_MAX_CLIENTS, 2, NULL, NULL,
		    NULL);
DNS_SD_REGISTER_TCP_SERVICE(prusa_http_sd, CONFIG_NET_HOSTNAME, "_http", "local", DNS_SD_EMPTY_TXT,
			    80);

static const uint8_t index_gz[] = {
#include "index.html.gz.inc"
};

static const struct http_header gzip_hdr[] = {{.name = "Content-Encoding", .value = "gzip"}};
static const struct http_header auth_hdr[] = {
	{.name = "WWW-Authenticate", .value = "Basic realm=\"prusa_cam\""}};

struct route {
	char auth[CONFIG_HTTP_SERVER_CAPTURE_HEADER_BUFFER_SIZE];
	char body[BODY_MAX + 1];
	size_t len;
	bool overflow;
	bool cam_held;
	bool exif_sent;
	uint8_t exif[EXIF_HDR_LEN];
	const uint8_t *jpeg;
	size_t jpeg_len;
};

static char out[640];

static void capture(struct route *r, const struct http_request_ctx *req)
{
	for (size_t i = 0; req->headers != NULL && i < req->header_count; i++) {
		if (strcasecmp(req->headers[i].name, "Authorization") == 0) {
			strncpy(r->auth, req->headers[i].value, sizeof(r->auth) - 1);
		}
	}
	if (req->data_len > 0) {
		if (r->len + req->data_len > BODY_MAX) {
			r->overflow = true;
		} else {
			memcpy(&r->body[r->len], req->data, req->data_len);
			r->len += req->data_len;
		}
	}
}

static void reply(struct http_response_ctx *rsp, enum http_status st, const char *body)
{
	rsp->status = st;
	rsp->body = (const uint8_t *)body;
	rsp->body_len = strlen(body);
	rsp->final_chunk = true;
}

/* True if authorised; otherwise the response is filled in. */
static bool authorised(struct route *r, struct http_response_ctx *rsp)
{
	struct cfg c;
	bool ok;

	cfg_get(&c);
	if (c.password[0] == '\0') {
		reply(rsp, HTTP_403_FORBIDDEN,
		      "{\"error\":\"set a password: prusa set password\"}");
		return false;
	}
	ok = http_basic_auth_ok(r->auth, USER, c.password);
	memset(&c, 0, sizeof(c));
	if (!ok) {
		reply(rsp, HTTP_401_UNAUTHORIZED, "{\"error\":\"unauthorised\"}");
		rsp->headers = auth_hdr;
		rsp->header_count = ARRAY_SIZE(auth_hdr);
	}
	return ok;
}

/* "a.b.c.d", or "" for 0 (unset). */
static const char *ip_str(char buf[16], uint32_t ip)
{
	if (ip == 0) {
		buf[0] = '\0';
	} else {
		snprintf(buf, 16, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF,
			 ip & 0xFF);
	}
	return buf;
}

struct config_out {
	const char *name;
	bool token_set;
	const char *fingerprint;
	uint32_t interval;
	const char *resolution;
	uint32_t quality;
	bool hflip;
	bool vflip;
	bool flash;
	uint32_t rotation;
	const char *ip;
	const char *netmask;
	const char *gateway;
	const char *dns;
	int32_t brightness;
	int32_t contrast;
	int32_t saturation;
	bool auto_exposure;
	bool auto_wb;
	bool auto_gain;
};

static const struct json_obj_descr config_out_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct config_out, name, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct config_out, token_set, JSON_TOK_TRUE),
	JSON_OBJ_DESCR_PRIM(struct config_out, fingerprint, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct config_out, interval, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(struct config_out, resolution, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct config_out, quality, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(struct config_out, hflip, JSON_TOK_TRUE),
	JSON_OBJ_DESCR_PRIM(struct config_out, vflip, JSON_TOK_TRUE),
	JSON_OBJ_DESCR_PRIM(struct config_out, flash, JSON_TOK_TRUE),
	JSON_OBJ_DESCR_PRIM(struct config_out, rotation, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(struct config_out, ip, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct config_out, netmask, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct config_out, gateway, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct config_out, dns, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct config_out, brightness, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct config_out, contrast, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct config_out, saturation, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct config_out, auto_exposure, JSON_TOK_TRUE),
	JSON_OBJ_DESCR_PRIM(struct config_out, auto_wb, JSON_TOK_TRUE),
	JSON_OBJ_DESCR_PRIM(struct config_out, auto_gain, JSON_TOK_TRUE),
};

struct status_out {
	int64_t uptime_s;
	int64_t last_upload_s;
	int32_t last_result;
	const char *result;
	uint32_t uploads;
	bool stopped;
};

static const struct json_obj_descr status_out_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct status_out, uptime_s, JSON_TOK_INT64),
	JSON_OBJ_DESCR_PRIM(struct status_out, last_upload_s, JSON_TOK_INT64),
	JSON_OBJ_DESCR_PRIM(struct status_out, last_result, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct status_out, result, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct status_out, uploads, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(struct status_out, stopped, JSON_TOK_TRUE),
};

/* Encode into `out` and reply 200, or 500 if it does not fit. */
static void reply_json(struct http_response_ctx *rsp, const struct json_obj_descr *descr, size_t n,
		       const void *val)
{
	if (json_obj_encode_buf(descr, n, val, out, sizeof(out)) < 0) {
		reply(rsp, HTTP_500_INTERNAL_SERVER_ERROR, "{\"error\":\"encode\"}");
		return;
	}
	reply(rsp, HTTP_200_OK, out);
}

static void reply_config(struct http_response_ctx *rsp)
{
	char ip[16], mask[16], gw[16], dns[16];
	struct cfg c;
	struct config_out o;

	cfg_get(&c);
	o = (struct config_out){
		.name = c.name,
		.token_set = c.token[0] != '\0',
		.fingerprint = cfg_fingerprint(),
		.interval = c.interval_s,
		.resolution = cfg_resolutions[c.resolution].name,
		.quality = c.quality,
		.hflip = c.hflip,
		.vflip = c.vflip,
		.flash = c.flash,
		.rotation = c.rotation * 90,
		.ip = ip_str(ip, c.ip),
		.netmask = ip_str(mask, c.netmask),
		.gateway = ip_str(gw, c.gateway),
		.dns = ip_str(dns, c.dns),
		.brightness = c.brightness,
		.contrast = c.contrast,
		.saturation = c.saturation,
		.auto_exposure = c.auto_exposure,
		.auto_wb = c.auto_wb,
		.auto_gain = c.auto_gain,
	};
	reply_json(rsp, config_out_descr, ARRAY_SIZE(config_out_descr), &o);
	memset(&c, 0, sizeof(c)); /* after encoding: o.name points into c */
}

struct cfg_post {
	const char *token;
	const char *password;
	int32_t interval;
	const char *resolution;
	int32_t quality;
	bool hflip;
	bool vflip;
	bool flash;
	int32_t rotation;
	const char *ip;
	const char *netmask;
	const char *gateway;
	const char *dns;
	const char *name;
	int32_t brightness;
	int32_t contrast;
	int32_t saturation;
	bool auto_exposure;
	bool auto_wb;
	bool auto_gain;
};

static const struct json_obj_descr cfg_post_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct cfg_post, token, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct cfg_post, password, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct cfg_post, interval, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct cfg_post, resolution, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct cfg_post, quality, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct cfg_post, hflip, JSON_TOK_TRUE),
	JSON_OBJ_DESCR_PRIM(struct cfg_post, vflip, JSON_TOK_TRUE),
	JSON_OBJ_DESCR_PRIM(struct cfg_post, flash, JSON_TOK_TRUE),
	JSON_OBJ_DESCR_PRIM(struct cfg_post, rotation, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct cfg_post, ip, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct cfg_post, netmask, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct cfg_post, gateway, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct cfg_post, dns, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct cfg_post, name, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct cfg_post, brightness, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct cfg_post, contrast, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct cfg_post, saturation, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct cfg_post, auto_exposure, JSON_TOK_TRUE),
	JSON_OBJ_DESCR_PRIM(struct cfg_post, auto_wb, JSON_TOK_TRUE),
	JSON_OBJ_DESCR_PRIM(struct cfg_post, auto_gain, JSON_TOK_TRUE),
};

/* "" clears the address (DHCP / gateway as DNS); anything else must parse. */
static int parse_ip(const char *s, uint32_t *out)
{
	if (s[0] == '\0') {
		*out = 0;
		return 0;
	}
	return http_parse_ipv4(s, out);
}

/* Apply the fields present in a JSON body; 0, -EINVAL or a settings error. */
static int apply_post(char *body, size_t len)
{
	struct cfg_post p = {0};
	struct cfg c;
	int64_t got = json_obj_parse(body, len, cfg_post_descr, ARRAY_SIZE(cfg_post_descr), &p);
	int ret;

	if (got < 0) {
		return -EINVAL;
	}

	cfg_get(&c);
	if (got & BIT(0)) {
		if (strlen(p.token) > CFG_TOKEN_MAX) {
			return -EINVAL;
		}
		strcpy(c.token, p.token);
	}
	if (got & BIT(1)) {
		if (strlen(p.password) > CFG_PASSWORD_MAX) {
			return -EINVAL;
		}
		strcpy(c.password, p.password);
	}
	if (got & BIT(2)) {
		c.interval_s = CLAMP(p.interval, 0, UINT16_MAX);
	}
	if (got & BIT(3)) {
		int i = cfg_resolution_find(p.resolution);

		if (i < 0) {
			return -EINVAL;
		}
		c.resolution = i;
	}
	if (got & BIT(4)) {
		c.quality = CLAMP(p.quality, 0, UINT8_MAX);
	}
	if (got & BIT(5)) {
		c.hflip = p.hflip;
	}
	if (got & BIT(6)) {
		c.vflip = p.vflip;
	}
	if (got & BIT(7)) {
		c.flash = p.flash;
	}
	if (got & BIT(8)) {
		if (p.rotation % 90 != 0 || p.rotation < 0 || p.rotation > 270) {
			return -EINVAL;
		}
		c.rotation = p.rotation / 90;
	}
	if (((got & BIT(9)) && parse_ip(p.ip, &c.ip) < 0) ||
	    ((got & BIT(10)) && parse_ip(p.netmask, &c.netmask) < 0) ||
	    ((got & BIT(11)) && parse_ip(p.gateway, &c.gateway) < 0) ||
	    ((got & BIT(12)) && parse_ip(p.dns, &c.dns) < 0)) {
		memset(&c, 0, sizeof(c));
		return -EINVAL;
	}
	if (got & BIT(13)) {
		if (strlen(p.name) > CFG_NAME_MAX) {
			return -EINVAL;
		}
		strcpy(c.name, p.name);
	}
	/* Out-of-range levels are left for cfg_set() to reject. */
	if (got & BIT(14)) {
		c.brightness = CLAMP(p.brightness, INT8_MIN, INT8_MAX);
	}
	if (got & BIT(15)) {
		c.contrast = CLAMP(p.contrast, INT8_MIN, INT8_MAX);
	}
	if (got & BIT(16)) {
		c.saturation = CLAMP(p.saturation, INT8_MIN, INT8_MAX);
	}
	if (got & BIT(17)) {
		c.auto_exposure = p.auto_exposure;
	}
	if (got & BIT(18)) {
		c.auto_wb = p.auto_wb;
	}
	if (got & BIT(19)) {
		c.auto_gain = p.auto_gain;
	}
	ret = cfg_set(&c);
	memset(&c, 0, sizeof(c));
	return ret;
}

static int index_cb(struct http_client_ctx *client, enum http_transaction_status status,
		    const struct http_request_ctx *req, struct http_response_ctx *rsp, void *ud)
{
	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}
	rsp->headers = gzip_hdr;
	rsp->header_count = ARRAY_SIZE(gzip_hdr);
	rsp->body = index_gz;
	rsp->body_len = sizeof(index_gz);
	rsp->final_chunk = true;
	return 0;
}

static struct route config_route;

static int config_cb(struct http_client_ctx *client, enum http_transaction_status status,
		     const struct http_request_ctx *req, struct http_response_ctx *rsp, void *ud)
{
	struct route *r = &config_route;
	int ret;

	if (status == HTTP_SERVER_TRANSACTION_ABORTED ||
	    status == HTTP_SERVER_TRANSACTION_COMPLETE) {
		memset(r, 0, sizeof(*r)); /* the body may carry the token or password */
		return 0;
	}
	capture(r, req);
	if (status != HTTP_SERVER_REQUEST_DATA_FINAL || !authorised(r, rsp)) {
		return 0;
	}

	if (client->method == HTTP_POST) {
		ret = r->overflow ? -E2BIG : apply_post(r->body, r->len);
		if (ret < 0) {
			reply(rsp, HTTP_400_BAD_REQUEST, "{\"error\":\"invalid config\"}");
			return 0;
		}
	}
	reply_config(rsp);
	return 0;
}

static struct route status_route;

static int status_cb(struct http_client_ctx *client, enum http_transaction_status status,
		     const struct http_request_ctx *req, struct http_response_ctx *rsp, void *ud)
{
	struct route *r = &status_route;
	struct connect_status st;
	struct status_out so;

	if (status == HTTP_SERVER_TRANSACTION_ABORTED ||
	    status == HTTP_SERVER_TRANSACTION_COMPLETE) {
		memset(r, 0, sizeof(*r));
		return 0;
	}
	capture(r, req);
	if (status != HTTP_SERVER_REQUEST_DATA_FINAL || !authorised(r, rsp)) {
		return 0;
	}

	connect_get_status(&st);
	so = (struct status_out){
		.uptime_s = k_uptime_get() / 1000,
		.last_upload_s = st.last_ms / 1000,
		.last_result = st.last_result,
		.result = st.last_ms ? prusa_camera_describe(st.last_result) : "",
		.uploads = st.uploads,
		.stopped = st.stopped,
	};
	reply_json(rsp, status_out_descr, ARRAY_SIZE(status_out_descr), &so);
	return 0;
}

static struct route test_route;

static int test_cb(struct http_client_ctx *client, enum http_transaction_status status,
		   const struct http_request_ctx *req, struct http_response_ctx *rsp, void *ud)
{
	struct route *r = &test_route;

	if (status == HTTP_SERVER_TRANSACTION_ABORTED ||
	    status == HTTP_SERVER_TRANSACTION_COMPLETE) {
		memset(r, 0, sizeof(*r));
		return 0;
	}
	capture(r, req);
	if (status != HTTP_SERVER_REQUEST_DATA_FINAL || !authorised(r, rsp)) {
		return 0;
	}
	connect_test(); /* the result shows in /api/status */
	reply(rsp, HTTP_202_ACCEPTED, "{\"queued\":true}");
	return 0;
}

static struct route snapshot_route;

static int snapshot_cb(struct http_client_ctx *client, enum http_transaction_status status,
		       const struct http_request_ctx *req, struct http_response_ctx *rsp, void *ud)
{
	struct route *r = &snapshot_route;
	struct cfg c;

	if (status == HTTP_SERVER_TRANSACTION_ABORTED ||
	    status == HTTP_SERVER_TRANSACTION_COMPLETE) {
		if (r->cam_held) {
			cam_release();
		}
		memset(r, 0, sizeof(*r));
		return 0;
	}
	if (r->exif_sent) {
		/* Second chunk of a rotated frame: everything after its SOI. */
		rsp->body = r->jpeg + 2;
		rsp->body_len = r->jpeg_len - 2;
		rsp->final_chunk = true;
		return 0;
	}
	capture(r, req);
	if (status != HTTP_SERVER_REQUEST_DATA_FINAL || !authorised(r, rsp)) {
		return 0;
	}

	if (cam_snapshot(&r->jpeg, &r->jpeg_len) < 0) {
		reply(rsp, HTTP_503_SERVICE_UNAVAILABLE, "no frame");
		return 0;
	}
	/* The frame is sent from the camera buffer; it is released on COMPLETE/ABORTED. */
	r->cam_held = true;
	cfg_get(&c);
	if (exif_wanted(r->jpeg, r->jpeg_len, c.rotation)) {
		exif_header(r->exif, exif_orientation(c.rotation));
		r->exif_sent = true;
		rsp->body = r->exif;
		rsp->body_len = sizeof(r->exif);
	} else {
		rsp->body = r->jpeg;
		rsp->body_len = r->jpeg_len;
		rsp->final_chunk = true;
	}
	memset(&c, 0, sizeof(c));
	return 0;
}

#define DYN(_name, _methods, _type, _cb)                                                           \
	static struct http_resource_detail_dynamic _name = {                                       \
		.common = {.type = HTTP_RESOURCE_TYPE_DYNAMIC,                                     \
			   .bitmask_of_supported_http_methods = (_methods),                        \
			   .content_type = (_type)},                                               \
		.cb = (_cb),                                                                       \
	}

DYN(index_detail, BIT(HTTP_GET), "text/html", index_cb);
DYN(config_detail, BIT(HTTP_GET) | BIT(HTTP_POST), "application/json", config_cb);
DYN(status_detail, BIT(HTTP_GET), "application/json", status_cb);
DYN(snapshot_detail, BIT(HTTP_GET), "image/jpeg", snapshot_cb);
DYN(test_detail, BIT(HTTP_POST), "application/json", test_cb);

HTTP_RESOURCE_DEFINE(index_res, prusa_http, "/", &index_detail);
HTTP_RESOURCE_DEFINE(config_res, prusa_http, "/api/config", &config_detail);
HTTP_RESOURCE_DEFINE(status_res, prusa_http, "/api/status", &status_detail);
HTTP_RESOURCE_DEFINE(snapshot_res, prusa_http, "/snapshot.jpg", &snapshot_detail);
HTTP_RESOURCE_DEFINE(test_res, prusa_http, "/api/test", &test_detail);

int web_init(void)
{
	return http_server_start();
}
