/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * The control plane's request handling, route by route, kept apart from the
 * HTTP server so ztest can drive every route without a socket. Each handler
 * takes the request URL (path plus query) and, where the route needs it, the
 * Authorization header; writes a JSON body into buf; and returns the HTTP
 * status. Routes, query parameters and response bodies follow upstream's
 * docs/guides/http_api.md, so upstream's tooling drives this remote
 * unmodified. Fields this remote has no source for are left out of
 * /state.json rather than reported as zero.
 */

#ifndef PROTECTIVE_STOP_API_H
#define PROTECTIVE_STOP_API_H

#include <stdbool.h>
#include <stddef.h>

/* The admin user, as upstream's Basic auth expects it. The password is
 * CONFIG_PROTECTIVE_STOP_ADMIN_PASSWORD.
 */
#define PSTOP_API_ADMIN_USER "admin"

/* Big enough for /state.json with all four slots configured. */
#define PSTOP_API_BODY_MAX 2048

struct pstop_api_response {
	int status;          /* HTTP status code */
	size_t len;          /* bytes written to the body buffer */
	bool auth_challenge; /* a 401: send WWW-Authenticate */
	bool restart;        /* reboot once this response is on the wire */
};

/* GET /state.json -- telemetry snapshot. */
void pstop_api_state(char *buf, size_t cap, struct pstop_api_response *rsp);

/* POST /api/pstop_peer?ip=A.B.C.D&port=N | ?clear=1 -- legacy slot 0. */
void pstop_api_pstop_peer(const char *url, char *buf, size_t cap, struct pstop_api_response *rsp);

/* POST /api/pstop_peers?slot=N&ip=A.B.C.D&port=P[&id=HEX] | &clear=1 |
 * &rebond=1 -- the multi-machine peer table. Changes apply within one tick.
 */
void pstop_api_pstop_peers(const char *url, char *buf, size_t cap, struct pstop_api_response *rsp);

/* GET /api/role, POST /api/role?role=stop_only|operator -- admin. */
void pstop_api_role(bool post, const char *url, const char *auth, char *buf, size_t cap,
		    struct pstop_api_response *rsp);

/* GET /admin/api/status -- admin. */
void pstop_api_admin_status(const char *auth, char *buf, size_t cap,
			    struct pstop_api_response *rsp);

/* POST /admin/api/restart -- admin. Sets rsp->restart; the caller reboots. */
void pstop_api_admin_restart(const char *auth, char *buf, size_t cap,
			     struct pstop_api_response *rsp);

/* POST /api/ring_offset?n=N -- which physical pixel is LED 1 (0..15). */
void pstop_api_ring_offset(const char *url, char *buf, size_t cap, struct pstop_api_response *rsp);

/* POST /api/ring_led1?on=0|1 -- locate mode, auto-expiring after 5 min. */
void pstop_api_ring_led1(const char *url, char *buf, size_t cap, struct pstop_api_response *rsp);

/* GET /api/health -- lifetime wear counters and the warning board. */
void pstop_api_health(char *buf, size_t cap, struct pstop_api_response *rsp);

/* POST /api/health/reset?what=button|all&confirm=1 -- admin. */
void pstop_api_health_reset(const char *url, const char *auth, char *buf, size_t cap,
			    struct pstop_api_response *rsp);

/* GET /api/coredump -- admin. On 200, *size is the dump's length and the
 * caller streams it with crash_read(); otherwise buf holds the error body.
 */
void pstop_api_coredump(const char *auth, char *buf, size_t cap, struct pstop_api_response *rsp,
			size_t *size);

/* GET /admin/ -- admin. Only the gate: on 200 the caller serves the page. */
void pstop_api_admin_page(const char *auth, char *buf, size_t cap, struct pstop_api_response *rsp);

/* GET|POST /admin/api/settings -- admin. POST takes a JSON body with any of
 * "ring_offset" (0..15) and "device_id" (0 = derive from the MAC). body is
 * parsed in place.
 */
void pstop_api_admin_settings(bool post, char *body, size_t body_len, const char *auth, char *buf,
			      size_t cap, struct pstop_api_response *rsp);

/* GET|POST /admin/api/wifi -- admin. GET lists the stored networks with
 * passwords masked; POST replaces the list from {"networks":[{"ssid","pass"}]}
 * where a pass of "********" keeps that network's stored password. body is
 * parsed in place.
 */
void pstop_api_admin_wifi(bool post, char *body, size_t body_len, const char *auth, char *buf,
			  size_t cap, struct pstop_api_response *rsp);

/* GET /admin/api/verbose, POST /admin/api/verbose/toggle -- admin. */
void pstop_api_admin_verbose(bool toggle, const char *auth, char *buf, size_t cap,
			     struct pstop_api_response *rsp);

#endif /* PROTECTIVE_STOP_API_H */
