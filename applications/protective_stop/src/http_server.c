/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * The control plane's HTTP glue: one Zephyr HTTP service, one dynamic
 * resource per route, each handing the request to api.c. Nothing here
 * decides anything; it moves bytes between the socket and the handlers.
 *
 * The HTTP server runs every callback on its own single thread, and a
 * response chunk is on the wire before the next callback runs, so one shared
 * response buffer serves every request. What spans callbacks (credentials, a
 * POST body, a stream position) is kept per client: see struct req_state.
 */

#include <string.h>
#include <strings.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/sys/reboot.h>

#include "api.h"
#include "crash.h"
#include "health.h"
#include "http_server.h"
#include "last_log.h"
#include "pstop_log.h"

LOG_MODULE_REGISTER(pstop_http, PSTOP_LOG_LEVEL);

#define RESTART_DELAY_MS 500

/* Largest request body taken (the WiFi list is the biggest, ~500 bytes). */
#define REQ_BODY_MAX 1024

HTTP_SERVER_REGISTER_HEADER_CAPTURE(capture_authorization, "Authorization");

static uint16_t http_port = CONFIG_PROTECTIVE_STOP_HTTP_PORT;
HTTP_SERVICE_DEFINE(pstop_http, NULL, &http_port, 2, 2, NULL, NULL, NULL);

static char body[PSTOP_API_BODY_MAX];

/* The admin page, gzipped at build time from src/admin.html. */
static const uint8_t admin_page_gz[] = {
#include "admin.html.gz.inc"
};

/* Per-client request state. One client's POST body can arrive over several
 * reads with another client's requests served in between, so nothing that
 * spans callbacks may be shared between clients.
 */
struct req_state {
	struct http_client_ctx *client; /* NULL = free */

	/* Copied out of the first callback: captured headers are only
	 * visible there.
	 */
	char auth[CONFIG_HTTP_SERVER_CAPTURE_HEADER_BUFFER_SIZE];

	/* A POST body, gathered across however many callbacks it takes. */
	char body[REQ_BODY_MAX];
	size_t len;
	bool overflow;

	/* An in-progress streamed response (the coredump). */
	bool streaming;
	size_t stream_off;
	size_t stream_size;
};

static struct req_state reqs[CONFIG_HTTP_SERVER_MAX_CLIENTS];

static const struct http_header auth_challenge[] = {
	{.name = "WWW-Authenticate", .value = "Basic realm=\"pstop\""},
};

static const struct http_header gzip_encoding[] = {
	{.name = "Content-Encoding", .value = "gzip"},
};

static void restart_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	LOG_WRN("restarting on admin request");
	(void)health_flush();
	sys_reboot(SYS_REBOOT_COLD);
}

static K_WORK_DELAYABLE_DEFINE(restart_work, restart_work_fn);

enum route {
	ROUTE_STATE,
	ROUTE_PSTOP_PEER,
	ROUTE_PSTOP_PEERS,
	ROUTE_ROLE,
	ROUTE_RING_OFFSET,
	ROUTE_RING_LED1,
	ROUTE_HEALTH,
	ROUTE_HEALTH_RESET,
	ROUTE_LAST_LOG,
	ROUTE_COREDUMP,
	ROUTE_ADMIN_PAGE,
	ROUTE_ADMIN_STATUS,
	ROUTE_ADMIN_RESTART,
	ROUTE_ADMIN_SETTINGS,
	ROUTE_ADMIN_WIFI,
	ROUTE_ADMIN_VERBOSE,
	ROUTE_ADMIN_VERBOSE_TOGGLE,
};

/* This client's request state, claiming a free slot on its first callback.
 * NULL only if every slot is held, which the server's own client limit
 * rules out.
 */
static struct req_state *req_for(struct http_client_ctx *client)
{
	struct req_state *free_slot = NULL;

	for (size_t i = 0; i < ARRAY_SIZE(reqs); i++) {
		if (reqs[i].client == client) {
			return &reqs[i];
		}
		if ((reqs[i].client == NULL) && (free_slot == NULL)) {
			free_slot = &reqs[i];
		}
	}
	if (free_slot != NULL) {
		(void)memset(free_slot, 0, sizeof(*free_slot));
		free_slot->client = client;
	}
	return free_slot;
}

static void req_release(struct http_client_ctx *client)
{
	for (size_t i = 0; i < ARRAY_SIZE(reqs); i++) {
		if (reqs[i].client == client) {
			/* Scrub the credentials and the body (it may carry a
			 * WiFi password) along with the claim.
			 */
			(void)memset(&reqs[i], 0, sizeof(reqs[i]));
		}
	}
}

static void capture_auth(struct req_state *r, const struct http_request_ctx *req)
{
	if (req->headers == NULL) {
		return;
	}

	r->auth[0] = '\0';
	for (size_t i = 0; i < req->header_count; i++) {
		if (strcasecmp(req->headers[i].name, "Authorization") == 0) {
			strncpy(r->auth, req->headers[i].value, sizeof(r->auth) - 1U);
			r->auth[sizeof(r->auth) - 1U] = '\0';
			break;
		}
	}
}

static void gather_body(struct req_state *r, const struct http_request_ctx *req)
{
	if ((req->data == NULL) || (req->data_len == 0U)) {
		return;
	}
	if ((r->len + req->data_len) > sizeof(r->body)) {
		r->overflow = true;
		return;
	}
	(void)memcpy(&r->body[r->len], req->data, req->data_len);
	r->len += req->data_len;
}

/* Next chunk of the coredump. The server calls back for the next one, with
 * nothing in between, until final_chunk.
 */
static int stream_coredump(struct req_state *r, struct http_response_ctx *rsp_ctx)
{
	size_t n = MIN(sizeof(body), r->stream_size - r->stream_off);
	int ret = crash_read(r->stream_off, (uint8_t *)body, n);

	if (ret <= 0) {
		/* Headers are gone already: all that is left is to end it. */
		LOG_ERR("coredump read at %zu failed: %d", r->stream_off, ret);
		rsp_ctx->final_chunk = true;
		return 0;
	}
	r->stream_off += (size_t)ret;
	rsp_ctx->body = (const uint8_t *)body;
	rsp_ctx->body_len = (size_t)ret;
	rsp_ctx->final_chunk = (r->stream_off >= r->stream_size);
	return 0;
}

static int route_handler(struct http_client_ctx *client, enum http_transaction_status status,
			 const struct http_request_ctx *req, struct http_response_ctx *rsp_ctx,
			 void *user_data)
{
	const char *url = (const char *)client->url_buffer;
	bool post = (client->method == HTTP_POST);
	enum route route = (enum route)POINTER_TO_INT(user_data);
	struct pstop_api_response rsp;
	const char *rsp_body = body;
	struct req_state *r;

	if ((status == HTTP_SERVER_TRANSACTION_ABORTED) ||
	    (status == HTTP_SERVER_TRANSACTION_COMPLETE)) {
		req_release(client);
		return 0;
	}

	r = req_for(client);
	if (r == NULL) {
		return -ENOMEM;
	}

	if (r->streaming) {
		return stream_coredump(r, rsp_ctx);
	}

	capture_auth(r, req);
	gather_body(r, req);

	/* Answer once the request is fully read. */
	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	LOG_DBG("%s %s", post ? "POST" : "GET", url);

	if (r->overflow) {
		(void)memset(&rsp, 0, sizeof(rsp));
		rsp.status = 413;
		rsp.len = (size_t)snprintk(body, sizeof(body),
					   "{\"ok\":false,\"error\":\"body too large\"}");
		goto respond;
	}

	switch (route) {
	case ROUTE_STATE:
		pstop_api_state(body, sizeof(body), &rsp);
		break;
	case ROUTE_PSTOP_PEER:
		pstop_api_pstop_peer(url, body, sizeof(body), &rsp);
		break;
	case ROUTE_PSTOP_PEERS:
		pstop_api_pstop_peers(url, body, sizeof(body), &rsp);
		break;
	case ROUTE_ROLE:
		pstop_api_role(post, url, r->auth, body, sizeof(body), &rsp);
		break;
	case ROUTE_RING_OFFSET:
		pstop_api_ring_offset(url, body, sizeof(body), &rsp);
		break;
	case ROUTE_RING_LED1:
		pstop_api_ring_led1(url, body, sizeof(body), &rsp);
		break;
	case ROUTE_HEALTH:
		pstop_api_health(body, sizeof(body), &rsp);
		break;
	case ROUTE_HEALTH_RESET:
		pstop_api_health_reset(url, r->auth, body, sizeof(body), &rsp);
		break;
	case ROUTE_LAST_LOG:
		(void)memset(&rsp, 0, sizeof(rsp));
		rsp.status = 200;
		rsp.len = last_log_get(&rsp_body);
		break;
	case ROUTE_COREDUMP:
		pstop_api_coredump(r->auth, body, sizeof(body), &rsp, &r->stream_size);
		if (rsp.status == 200) {
			r->streaming = true;
			r->stream_off = 0U;
			return stream_coredump(r, rsp_ctx);
		}
		break;
	case ROUTE_ADMIN_PAGE:
		pstop_api_admin_page(r->auth, body, sizeof(body), &rsp);
		if (rsp.status == 200) {
			rsp_ctx->headers = gzip_encoding;
			rsp_ctx->header_count = ARRAY_SIZE(gzip_encoding);
			rsp_ctx->body = admin_page_gz;
			rsp_ctx->body_len = sizeof(admin_page_gz);
			rsp_ctx->final_chunk = true;
			return 0;
		}
		break;
	case ROUTE_ADMIN_STATUS:
		pstop_api_admin_status(r->auth, body, sizeof(body), &rsp);
		break;
	case ROUTE_ADMIN_RESTART:
		pstop_api_admin_restart(r->auth, body, sizeof(body), &rsp);
		break;
	case ROUTE_ADMIN_SETTINGS:
		pstop_api_admin_settings(post, r->body, r->len, r->auth, body, sizeof(body), &rsp);
		break;
	case ROUTE_ADMIN_WIFI:
		pstop_api_admin_wifi(post, r->body, r->len, r->auth, body, sizeof(body), &rsp);
		break;
	case ROUTE_ADMIN_VERBOSE:
		pstop_api_admin_verbose(false, r->auth, body, sizeof(body), &rsp);
		break;
	case ROUTE_ADMIN_VERBOSE_TOGGLE:
		pstop_api_admin_verbose(true, r->auth, body, sizeof(body), &rsp);
		break;
	default:
		return -EINVAL;
	}

respond:
	r->auth[0] = '\0';

	rsp_ctx->status = (enum http_status)rsp.status;
	if (rsp.auth_challenge) {
		rsp_ctx->headers = auth_challenge;
		rsp_ctx->header_count = ARRAY_SIZE(auth_challenge);
	}
	rsp_ctx->body = (const uint8_t *)rsp_body;
	rsp_ctx->body_len = rsp.len;
	rsp_ctx->final_chunk = true;

	if (rsp.restart) {
		/* Long enough for this response to reach the client. */
		(void)k_work_schedule(&restart_work, K_MSEC(RESTART_DELAY_MS));
	}

	return 0;
}

#define ROUTE_DETAIL(_name, _methods, _route, _type)                                               \
	static struct http_resource_detail_dynamic _name = {                                       \
		.common =                                                                          \
			{                                                                          \
				.type = HTTP_RESOURCE_TYPE_DYNAMIC,                                \
				.bitmask_of_supported_http_methods = (_methods),                   \
				.content_type = (_type),                                           \
			},                                                                         \
		.cb = route_handler,                                                               \
		.user_data = INT_TO_POINTER(_route),                                               \
	}

#define JSON "application/json"
#define GET  BIT(HTTP_GET)
#define POST BIT(HTTP_POST)

ROUTE_DETAIL(state_detail, GET, ROUTE_STATE, JSON);
ROUTE_DETAIL(pstop_peer_detail, POST, ROUTE_PSTOP_PEER, JSON);
ROUTE_DETAIL(pstop_peers_detail, POST, ROUTE_PSTOP_PEERS, JSON);
ROUTE_DETAIL(role_detail, GET | POST, ROUTE_ROLE, JSON);
ROUTE_DETAIL(ring_offset_detail, POST, ROUTE_RING_OFFSET, JSON);
ROUTE_DETAIL(ring_led1_detail, POST, ROUTE_RING_LED1, JSON);
ROUTE_DETAIL(health_detail, GET, ROUTE_HEALTH, JSON);
ROUTE_DETAIL(health_reset_detail, POST, ROUTE_HEALTH_RESET, JSON);
ROUTE_DETAIL(last_log_detail, GET, ROUTE_LAST_LOG, "text/plain");
ROUTE_DETAIL(coredump_detail, GET, ROUTE_COREDUMP, "application/octet-stream");
ROUTE_DETAIL(admin_page_detail, GET, ROUTE_ADMIN_PAGE, "text/html");
ROUTE_DETAIL(admin_status_detail, GET, ROUTE_ADMIN_STATUS, JSON);
ROUTE_DETAIL(admin_restart_detail, POST, ROUTE_ADMIN_RESTART, JSON);
ROUTE_DETAIL(admin_settings_detail, GET | POST, ROUTE_ADMIN_SETTINGS, JSON);
ROUTE_DETAIL(admin_wifi_detail, GET | POST, ROUTE_ADMIN_WIFI, JSON);
ROUTE_DETAIL(admin_verbose_detail, GET, ROUTE_ADMIN_VERBOSE, JSON);
ROUTE_DETAIL(admin_verbose_toggle_detail, POST, ROUTE_ADMIN_VERBOSE_TOGGLE, JSON);

HTTP_RESOURCE_DEFINE(state_res, pstop_http, "/state.json", &state_detail);
HTTP_RESOURCE_DEFINE(pstop_peer_res, pstop_http, "/api/pstop_peer", &pstop_peer_detail);
HTTP_RESOURCE_DEFINE(pstop_peers_res, pstop_http, "/api/pstop_peers", &pstop_peers_detail);
HTTP_RESOURCE_DEFINE(role_res, pstop_http, "/api/role", &role_detail);
HTTP_RESOURCE_DEFINE(ring_offset_res, pstop_http, "/api/ring_offset", &ring_offset_detail);
HTTP_RESOURCE_DEFINE(ring_led1_res, pstop_http, "/api/ring_led1", &ring_led1_detail);
HTTP_RESOURCE_DEFINE(health_res, pstop_http, "/api/health", &health_detail);
HTTP_RESOURCE_DEFINE(health_reset_res, pstop_http, "/api/health/reset", &health_reset_detail);
HTTP_RESOURCE_DEFINE(last_log_res, pstop_http, "/api/last_log", &last_log_detail);
HTTP_RESOURCE_DEFINE(coredump_res, pstop_http, "/api/coredump", &coredump_detail);
HTTP_RESOURCE_DEFINE(admin_page_res, pstop_http, "/admin/", &admin_page_detail);
HTTP_RESOURCE_DEFINE(admin_status_res, pstop_http, "/admin/api/status", &admin_status_detail);
HTTP_RESOURCE_DEFINE(admin_restart_res, pstop_http, "/admin/api/restart", &admin_restart_detail);
HTTP_RESOURCE_DEFINE(admin_settings_res, pstop_http, "/admin/api/settings", &admin_settings_detail);
HTTP_RESOURCE_DEFINE(admin_wifi_res, pstop_http, "/admin/api/wifi", &admin_wifi_detail);
HTTP_RESOURCE_DEFINE(admin_verbose_res, pstop_http, "/admin/api/verbose", &admin_verbose_detail);
HTTP_RESOURCE_DEFINE(admin_verbose_toggle_res, pstop_http, "/admin/api/verbose/toggle",
		     &admin_verbose_toggle_detail);

int pstop_http_start(void)
{
	int ret;

	if (CONFIG_PROTECTIVE_STOP_ADMIN_PASSWORD[0] == '\0') {
		LOG_WRN("admin routes disabled: CONFIG_PROTECTIVE_STOP_ADMIN_PASSWORD is not set");
	}

	ret = http_server_start();
	if (ret != 0) {
		LOG_ERR("http server start failed: %d", ret);
		return ret;
	}

	LOG_INF("control plane on port %u", http_port);
	return 0;
}
