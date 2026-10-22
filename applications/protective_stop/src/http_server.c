/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * The control plane's HTTP glue: one Zephyr HTTP service, one dynamic
 * resource per route, each handing the request to api.c. Nothing here
 * decides anything; it moves bytes between the socket and the handlers.
 *
 * The HTTP server runs every callback on its own single thread, so the one
 * shared body buffer and the captured Authorization header below are never
 * touched by two requests at once.
 */

#include <string.h>
#include <strings.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/sys/reboot.h>

#include "api.h"
#include "http_server.h"

LOG_MODULE_REGISTER(pstop_http, LOG_LEVEL_INF);

/* Upstream's admin password, published in its docs: fine on a bench, not
 * on a deployed remote.
 */
#define PUBLIC_DEFAULT_PASSWORD "microlink"

#define RESTART_DELAY_MS 500

HTTP_SERVER_REGISTER_HEADER_CAPTURE(capture_authorization, "Authorization");

static uint16_t http_port = CONFIG_PROTECTIVE_STOP_HTTP_PORT;
HTTP_SERVICE_DEFINE(pstop_http, NULL, &http_port, 2, 2, NULL, NULL, NULL);

static char body[PSTOP_API_BODY_MAX];

/* Copied out of the first callback of each request: captured headers are
 * only visible there.
 */
static char auth[CONFIG_HTTP_SERVER_CAPTURE_HEADER_BUFFER_SIZE];

static const struct http_header auth_challenge[] = {
	{.name = "WWW-Authenticate", .value = "Basic realm=\"pstop\""},
};

static void restart_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	LOG_WRN("restarting on admin request");
	sys_reboot(SYS_REBOOT_COLD);
}

static K_WORK_DELAYABLE_DEFINE(restart_work, restart_work_fn);

enum route {
	ROUTE_STATE,
	ROUTE_PSTOP_PEER,
	ROUTE_PSTOP_PEERS,
	ROUTE_ROLE,
	ROUTE_ADMIN_STATUS,
	ROUTE_ADMIN_RESTART,
};

static void capture_auth(const struct http_request_ctx *req)
{
	if (req->headers == NULL) {
		return;
	}

	auth[0] = '\0';
	for (size_t i = 0; i < req->header_count; i++) {
		if (strcasecmp(req->headers[i].name, "Authorization") == 0) {
			strncpy(auth, req->headers[i].value, sizeof(auth) - 1U);
			auth[sizeof(auth) - 1U] = '\0';
			break;
		}
	}
}

static int route_handler(struct http_client_ctx *client, enum http_transaction_status status,
			 const struct http_request_ctx *req, struct http_response_ctx *rsp_ctx,
			 void *user_data)
{
	const char *url = (const char *)client->url_buffer;
	bool post = (client->method == HTTP_POST);
	struct pstop_api_response rsp;

	if ((status == HTTP_SERVER_TRANSACTION_ABORTED) ||
	    (status == HTTP_SERVER_TRANSACTION_COMPLETE)) {
		auth[0] = '\0';
		return 0;
	}

	capture_auth(req);

	/* Every route takes its arguments from the query string; any body is
	 * ignored. Answer once the request is fully read.
	 */
	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	switch ((enum route)POINTER_TO_INT(user_data)) {
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
		pstop_api_role(post, url, auth, body, sizeof(body), &rsp);
		break;
	case ROUTE_ADMIN_STATUS:
		pstop_api_admin_status(auth, body, sizeof(body), &rsp);
		break;
	case ROUTE_ADMIN_RESTART:
		pstop_api_admin_restart(auth, body, sizeof(body), &rsp);
		break;
	default:
		return -EINVAL;
	}

	auth[0] = '\0';

	rsp_ctx->status = (enum http_status)rsp.status;
	if (rsp.auth_challenge) {
		rsp_ctx->headers = auth_challenge;
		rsp_ctx->header_count = ARRAY_SIZE(auth_challenge);
	}
	rsp_ctx->body = (const uint8_t *)body;
	rsp_ctx->body_len = rsp.len;
	rsp_ctx->final_chunk = true;

	if (rsp.restart) {
		/* Long enough for this response to reach the client. */
		(void)k_work_schedule(&restart_work, K_MSEC(RESTART_DELAY_MS));
	}

	return 0;
}

#define ROUTE_DETAIL(_name, _methods, _route)                                                      \
	static struct http_resource_detail_dynamic _name = {                                       \
		.common =                                                                          \
			{                                                                          \
				.type = HTTP_RESOURCE_TYPE_DYNAMIC,                                \
				.bitmask_of_supported_http_methods = (_methods),                   \
				.content_type = "application/json",                                \
			},                                                                         \
		.cb = route_handler,                                                               \
		.user_data = INT_TO_POINTER(_route),                                               \
	}

ROUTE_DETAIL(state_detail, BIT(HTTP_GET), ROUTE_STATE);
ROUTE_DETAIL(pstop_peer_detail, BIT(HTTP_POST), ROUTE_PSTOP_PEER);
ROUTE_DETAIL(pstop_peers_detail, BIT(HTTP_POST), ROUTE_PSTOP_PEERS);
ROUTE_DETAIL(role_detail, BIT(HTTP_GET) | BIT(HTTP_POST), ROUTE_ROLE);
ROUTE_DETAIL(admin_status_detail, BIT(HTTP_GET), ROUTE_ADMIN_STATUS);
ROUTE_DETAIL(admin_restart_detail, BIT(HTTP_POST), ROUTE_ADMIN_RESTART);

HTTP_RESOURCE_DEFINE(state_res, pstop_http, "/state.json", &state_detail);
HTTP_RESOURCE_DEFINE(pstop_peer_res, pstop_http, "/api/pstop_peer", &pstop_peer_detail);
HTTP_RESOURCE_DEFINE(pstop_peers_res, pstop_http, "/api/pstop_peers", &pstop_peers_detail);
HTTP_RESOURCE_DEFINE(role_res, pstop_http, "/api/role", &role_detail);
HTTP_RESOURCE_DEFINE(admin_status_res, pstop_http, "/admin/api/status", &admin_status_detail);
HTTP_RESOURCE_DEFINE(admin_restart_res, pstop_http, "/admin/api/restart", &admin_restart_detail);

int pstop_http_start(void)
{
	int ret;

	if (strcmp(CONFIG_PROTECTIVE_STOP_ADMIN_PASSWORD, PUBLIC_DEFAULT_PASSWORD) == 0) {
		LOG_WRN("admin password is the public default; set "
			"CONFIG_PROTECTIVE_STOP_ADMIN_PASSWORD before deploying");
	}

	ret = http_server_start();
	if (ret != 0) {
		LOG_ERR("http server start failed: %d", ret);
		return ret;
	}

	LOG_INF("control plane on port %u", http_port);
	return 0;
}
