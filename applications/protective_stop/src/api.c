/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/app_version.h>
#include <zephyr/data/json.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "api.h"
#include "app_settings.h"
#include "crash.h"
#include "estop_gpio.h"
#include "fw_info.h"
#include "health.h"
#include "http_util.h"
#include "leds.h"
#include "lockstep.h"
#include "pstop/pstop_msg.h"
#include "pstop_aux_channel.h"
#include "pstop_log.h"
#include "session.h"
#include "uplink.h"
#include "verbose.h"
#include "wifi.h"

LOG_MODULE_REGISTER(pstop_api, PSTOP_LOG_LEVEL);

/* machine_app_runner's default machine_device_id, used when ?id= is absent. */
#define PSTOP_DEFAULT_MACHINE_ID 0x01020304U

/* Bounded appender: once the buffer is full every later append is a no-op
 * and the overflow is remembered, so a body is either complete or replaced by
 * an error -- never sent truncated.
 */
struct out {
	char *buf;
	size_t cap;
	size_t len;
	bool overflow;
};

static void out_printf(struct out *o, const char *fmt, ...)
{
	va_list ap;
	int n;

	if (o->overflow) {
		return;
	}

	va_start(ap, fmt);
	n = vsnprintf(o->buf + o->len, o->cap - o->len, fmt, ap);
	va_end(ap);

	if ((n < 0) || ((size_t)n >= (o->cap - o->len))) {
		o->overflow = true;
		return;
	}
	o->len += (size_t)n;
}

static void finish(struct out *o, int status, struct pstop_api_response *rsp)
{
	if (o->overflow) {
		LOG_ERR("response body overflowed %zu bytes", o->cap);
		o->len = 0U;
		o->overflow = false;
		out_printf(o, "{\"ok\":false,\"error\":\"response too large\"}");
		status = 500;
	}
	rsp->status = status;
	rsp->len = o->len;
}

static void reply(char *buf, size_t cap, struct pstop_api_response *rsp, int status,
		  const char *fmt, ...)
{
	struct out o = {.buf = buf, .cap = cap};
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, cap, fmt, ap);
	va_end(ap);

	if ((n < 0) || ((size_t)n >= cap)) {
		o.overflow = true;
	} else {
		o.len = (size_t)n;
	}
	finish(&o, status, rsp);
}

static void init_rsp(struct pstop_api_response *rsp)
{
	(void)memset(rsp, 0, sizeof(*rsp));
}

static bool admin_ok(const char *auth, char *buf, size_t cap, struct pstop_api_response *rsp)
{
	if (CONFIG_PROTECTIVE_STOP_ADMIN_PASSWORD[0] == '\0') {
		/* No password configured: no credentials can be right, and
		 * asking for them would only invite guessing.
		 */
		reply(buf, cap, rsp, 403,
		      "{\"ok\":false,\"error\":\"admin disabled: no password configured\"}");
		return false;
	}
	if (http_basic_auth_ok(auth, PSTOP_API_ADMIN_USER, CONFIG_PROTECTIVE_STOP_ADMIN_PASSWORD)) {
		return true;
	}
	reply(buf, cap, rsp, 401, "{\"ok\":false,\"error\":\"unauthorized\"}");
	rsp->auth_challenge = true;
	return false;
}

static const char *role_str(void)
{
	return pstop_aux_role_str(app_settings_is_operator() ? PSTOP_AUX_ROLE_OPERATOR
							     : PSTOP_AUX_ROLE_STOP_ONLY);
}

/* Upstream's pstop_mm_last[0]: the most recent comparator fault, packed as
 * kind<<28 | late_mask<<26 | slot<<24 | first_differing_byte<<16 (0xFF n/a) |
 * verdict0<<8 | verdict1.
 */
static uint32_t pack_mm_last(const struct pstop_lockstep_diag *d)
{
	if (d->last_kind == PSTOP_LOCKSTEP_EVENT_CONTENT) {
		return ((uint32_t)PSTOP_LOCKSTEP_EVENT_CONTENT << 28) |
		       ((uint32_t)(d->mismatch_slot & 0x3U) << 24) |
		       ((uint32_t)d->mismatch_offset << 16) |
		       ((uint32_t)d->mismatch_verdict[0] << 8) | (uint32_t)d->mismatch_verdict[1];
	}
	if (d->last_kind == PSTOP_LOCKSTEP_EVENT_TIMEOUT) {
		return ((uint32_t)PSTOP_LOCKSTEP_EVENT_TIMEOUT << 28) |
		       ((uint32_t)(d->late_mask & 0x3U) << 26) | (0xFFU << 16);
	}
	return 0U;
}

void pstop_api_state(char *buf, size_t cap, struct pstop_api_response *rsp)
{
	struct out o = {.buf = buf, .cap = cap};
	struct pstop_lockstep_diag diag;
	struct pstop_peer peer0;
	uint32_t sent = 0U;
	uint32_t replies = 0U;
	uint32_t send_fail = 0U;
	uint32_t rebonds = 0U;
	uint64_t last_reply_ms = 0U;
	uint8_t last_msg = PSTOP_MESSAGE_UNKNOWN;
	bool hi[ESTOP_CHANNELS];
	bool lo[ESTOP_CHANNELS];
	uint64_t last_event_ms;

	init_rsp(rsp);
	pstop_lockstep_diag_get(&diag);
	(void)app_settings_peer_get(0, &peer0);

	for (int c = 0; c < ESTOP_CHANNELS; c++) {
		pstop_lockstep_loop_health(c, &hi[c], &lo[c]);
	}

	/* The scalar pstop_* fields are upstream's single-machine view, kept
	 * for old tooling: counters summed over the active slots, and the
	 * last message from whichever machine replied most recently.
	 */
	for (int slot = 0; slot < PSTOP_MAX_MACHINES; slot++) {
		const struct pstop_session *s = pstop_lockstep_session(slot);

		if (!pstop_lockstep_slot_active(slot)) {
			continue;
		}
		sent += s->sent;
		replies += s->replies;
		send_fail += s->send_fail;
		rebonds += s->rebonds;
		if ((s->last_msg != PSTOP_MESSAGE_UNKNOWN) && (s->last_reply_ms >= last_reply_ms)) {
			last_reply_ms = s->last_reply_ms;
			last_msg = s->last_msg;
		}
	}

	last_event_ms = (diag.last_kind == PSTOP_LOCKSTEP_EVENT_CONTENT)   ? diag.mismatch_uptime_ms
			: (diag.last_kind == PSTOP_LOCKSTEP_EVENT_TIMEOUT) ? diag.late_uptime_ms
									   : 0U;

	out_printf(&o, "{\"uptime_ms\":%llu,\"fw_ver\":\"%s\",\"fw_sha\":\"%s\",",
		   (unsigned long long)k_uptime_get(), APP_VERSION_STRING, FW_SHA);
	out_printf(&o, "\"role\":\"%s\",\"device_id\":%u,", role_str(),
		   (unsigned int)app_settings_effective_device_id());
	out_printf(&o, "\"l0\":%u,\"l1\":%u,", pstop_lockstep_last_verdict(0),
		   pstop_lockstep_last_verdict(1));
	out_printf(&o, "\"e_hi0\":%d,\"e_lo0\":%d,\"e_hi1\":%d,\"e_lo1\":%d,", hi[0], lo[0], hi[1],
		   lo[1]);
	out_printf(&o, "\"pstop_peer_ip\":%u,\"pstop_peer_port\":%u,",
		   peer0.configured ? (unsigned int)peer0.ip : 0U,
		   peer0.configured ? (unsigned int)peer0.port : 0U);
	out_printf(&o,
		   "\"pstop_sent\":%u,\"pstop_replies\":%u,\"pstop_last_msg\":%u,"
		   "\"pstop_send_fail\":%u,\"pstop_rebonds\":%u,\"pstop_last_reply_ms\":%llu,",
		   sent, replies, last_msg, send_fail, rebonds, (unsigned long long)last_reply_ms);
	out_printf(&o,
		   "\"pstop_mismatch\":%u,\"pstop_mm_timeout\":%u,\"pstop_mm_content\":%u,"
		   "\"pstop_mm_last\":[%u,0,%llu],\"pstop_core_lat_max_ms\":[%u,%u],"
		   "\"pstop_gated_ticks\":%u,",
		   pstop_lockstep_mismatches() + pstop_lockstep_missed_deadlines(),
		   pstop_lockstep_missed_deadlines(), pstop_lockstep_mismatches(),
		   pack_mm_last(&diag), (unsigned long long)last_event_ms, diag.latency_max_ms[0],
		   diag.latency_max_ms[1], pstop_lockstep_gated_ticks());

	struct uplink_status up;

	uplink_status_get(&up);
	out_printf(&o,
		   "\"health\":%d,\"ring_offset\":%u,\"ring_led1\":%d,\"active_iface\":\"%s\","
		   "\"local_ip\":\"%u.%u.%u.%u\",\"wifi_idx\":%d,\"crash_present\":%d,",
		   health_level(), app_settings_ring_offset(), leds_locate_active() ? 1 : 0,
		   uplink_kind_str(up.kind), (up.ipv4 >> 24) & 0xFFU, (up.ipv4 >> 16) & 0xFFU,
		   (up.ipv4 >> 8) & 0xFFU, up.ipv4 & 0xFFU, pstop_wifi_active_idx(),
		   (crash_size() > 0U) ? 1 : 0);

	out_printf(&o, "\"pstop_machines\":[");
	for (int slot = 0; slot < PSTOP_MAX_MACHINES; slot++) {
		const struct pstop_session *s = pstop_lockstep_session(slot);
		struct pstop_peer peer;
		bool active = pstop_lockstep_slot_active(slot);

		(void)app_settings_peer_get(slot, &peer);

		/* Stable indices: every slot is listed, configured or not. */
		out_printf(&o,
			   "%s{\"cfg\":%d,\"ip\":%u,\"port\":%u,\"id\":%u,\"state\":%d,"
			   "\"sent\":%u,\"replies\":%u,\"send_fail\":%u,\"rebonds\":%u,"
			   "\"unbonds\":%u,\"hb_ms\":%u,\"last_msg\":%u,\"last_reply_ms\":%llu}",
			   (slot == 0) ? "" : ",", peer.configured ? 1 : 0,
			   peer.configured ? (unsigned int)peer.ip : 0U,
			   peer.configured ? (unsigned int)peer.port : 0U,
			   peer.configured ? (unsigned int)peer.id : 0U,
			   active ? (int)s->state : PSTOP_SESS_IDLE, active ? s->sent : 0U,
			   active ? s->replies : 0U, active ? s->send_fail : 0U,
			   active ? s->rebonds : 0U, active ? s->unbonds : 0U,
			   active ? s->hb_ms : 0U,
			   active ? s->last_msg : (unsigned int)PSTOP_MESSAGE_UNKNOWN,
			   active ? (unsigned long long)s->last_reply_ms : 0ULL);
	}
	out_printf(&o, "]}");

	finish(&o, 200, rsp);
}

/* Validate and apply one slot's ip/port/id, then have the comparator pick it
 * up. Shared by the legacy single-peer route and the peer table.
 */
static int set_slot(int slot, const char *url, uint32_t *ip, uint32_t *port, uint32_t *id,
		    const char **err)
{
	char val[24];
	int ret;

	if ((http_query_get(url, "ip", val, sizeof(val)) != 0) || (http_parse_ipv4(val, ip) != 0)) {
		*err = "bad or missing ip";
		return -EINVAL;
	}
	if ((http_query_get(url, "port", val, sizeof(val)) != 0) ||
	    (http_parse_u32(val, port) != 0) || (*port < 1U) || (*port > 65535U)) {
		*err = "bad or missing port";
		return -EINVAL;
	}

	*id = PSTOP_DEFAULT_MACHINE_ID;
	ret = http_query_get(url, "id", val, sizeof(val));
	if ((ret != -ENOENT) && ((ret != 0) || (http_parse_u32(val, id) != 0) || (*id == 0U))) {
		*err = "bad id";
		return -EINVAL;
	}

	ret = app_settings_set_peer(slot, *ip, (uint16_t)*port, *id);
	if (ret != 0) {
		*err = "settings write failed";
		return ret;
	}
	(void)pstop_lockstep_request_reconfigure(slot);
	LOG_INF("slot %d -> %u.%u.%u.%u:%u id %08x", slot, (*ip >> 24) & 0xFFU, (*ip >> 16) & 0xFFU,
		(*ip >> 8) & 0xFFU, *ip & 0xFFU, *port, *id);
	return 0;
}

static int clear_slot(int slot)
{
	int ret = app_settings_clear_peer(slot);

	if (ret == 0) {
		(void)pstop_lockstep_request_reconfigure(slot);
		LOG_INF("slot %d cleared", slot);
	}
	return ret;
}

void pstop_api_pstop_peer(const char *url, char *buf, size_t cap, struct pstop_api_response *rsp)
{
	uint32_t ip;
	uint32_t port;
	uint32_t id;
	const char *err = NULL;
	int ret;

	init_rsp(rsp);

	if (http_query_flag(url, "clear")) {
		ret = clear_slot(0);
		reply(buf, cap, rsp, (ret == 0) ? 200 : 500, "{\"ok\":%s,\"cleared\":true}",
		      (ret == 0) ? "true" : "false");
		return;
	}

	ret = set_slot(0, url, &ip, &port, &id, &err);
	if (ret != 0) {
		reply(buf, cap, rsp, (ret == -EINVAL) ? 400 : 500,
		      "{\"ok\":false,\"error\":\"%s\"}", err);
		return;
	}

	reply(buf, cap, rsp, 200, "{\"ok\":true,\"ip\":\"%u.%u.%u.%u\",\"port\":%u}",
	      (ip >> 24) & 0xFFU, (ip >> 16) & 0xFFU, (ip >> 8) & 0xFFU, ip & 0xFFU, port);
}

void pstop_api_pstop_peers(const char *url, char *buf, size_t cap, struct pstop_api_response *rsp)
{
	char val[8];
	uint32_t slot;
	uint32_t ip;
	uint32_t port;
	uint32_t id;
	const char *err = NULL;
	int ret;

	init_rsp(rsp);

	if ((http_query_get(url, "slot", val, sizeof(val)) != 0) ||
	    (http_parse_u32(val, &slot) != 0)) {
		reply(buf, cap, rsp, 400, "{\"ok\":false,\"error\":\"missing slot\"}");
		return;
	}
	if (slot >= PSTOP_MAX_MACHINES) {
		reply(buf, cap, rsp, 400, "{\"ok\":false,\"error\":\"slot out of range\"}");
		return;
	}

	if (http_query_flag(url, "rebond")) {
		/* The only way out of REJECTED short of a reboot. */
		(void)pstop_lockstep_request_rebond((int)slot);
		reply(buf, cap, rsp, 200, "{\"ok\":true,\"slot\":%u,\"rebond\":true}", slot);
		return;
	}

	if (http_query_flag(url, "clear")) {
		ret = clear_slot((int)slot);
		reply(buf, cap, rsp, (ret == 0) ? 200 : 500,
		      "{\"ok\":%s,\"slot\":%u,\"cleared\":true}", (ret == 0) ? "true" : "false",
		      slot);
		return;
	}

	ret = set_slot((int)slot, url, &ip, &port, &id, &err);
	if (ret != 0) {
		reply(buf, cap, rsp, (ret == -EINVAL) ? 400 : 500,
		      "{\"ok\":false,\"error\":\"%s\"}", err);
		return;
	}

	reply(buf, cap, rsp, 200,
	      "{\"ok\":true,\"slot\":%u,\"ip\":\"%u.%u.%u.%u\",\"port\":%u,\"id\":%u}", slot,
	      (ip >> 24) & 0xFFU, (ip >> 16) & 0xFFU, (ip >> 8) & 0xFFU, ip & 0xFFU, port, id);
}

void pstop_api_role(bool post, const char *url, const char *auth, char *buf, size_t cap,
		    struct pstop_api_response *rsp)
{
	char val[16];
	bool op;

	init_rsp(rsp);

	if (!admin_ok(auth, buf, cap, rsp)) {
		return;
	}

	if (post) {
		if (http_query_get(url, "role", val, sizeof(val)) != 0) {
			reply(buf, cap, rsp, 400, "{\"ok\":false,\"error\":\"missing role\"}");
			return;
		}
		if (strcmp(val, "operator") == 0) {
			op = true;
		} else if (strcmp(val, "stop_only") == 0) {
			op = false;
		} else {
			reply(buf, cap, rsp, 400,
			      "{\"ok\":false,\"error\":\"role must be stop_only or operator\"}");
			return;
		}

		/* Live: the comparator snapshots the role every tick, and the
		 * machine re-reads it from every frame.
		 */
		if (app_settings_set_operator(op) != 0) {
			reply(buf, cap, rsp, 500,
			      "{\"ok\":false,\"error\":\"settings write failed\"}");
			return;
		}
		LOG_INF("role -> %s", role_str());
		reply(buf, cap, rsp, 200, "{\"ok\":true,\"role\":\"%s\",\"applied\":true}",
		      role_str());
		return;
	}

	reply(buf, cap, rsp, 200, "{\"ok\":true,\"role\":\"%s\"}", role_str());
}

void pstop_api_admin_status(const char *auth, char *buf, size_t cap, struct pstop_api_response *rsp)
{
	int active = 0;

	init_rsp(rsp);

	if (!admin_ok(auth, buf, cap, rsp)) {
		return;
	}

	for (int slot = 0; slot < PSTOP_MAX_MACHINES; slot++) {
		if (pstop_lockstep_slot_active(slot)) {
			active++;
		}
	}

	reply(buf, cap, rsp, 200,
	      "{\"ok\":true,\"fw_ver\":\"%s\",\"fw_sha\":\"%s\",\"uptime_ms\":%llu,"
	      "\"device_id\":%u,\"role\":\"%s\",\"sessions\":%d}",
	      APP_VERSION_STRING, FW_SHA, (unsigned long long)k_uptime_get(),
	      (unsigned int)app_settings_effective_device_id(), role_str(), active);
}

void pstop_api_admin_restart(const char *auth, char *buf, size_t cap,
			     struct pstop_api_response *rsp)
{
	init_rsp(rsp);

	if (!admin_ok(auth, buf, cap, rsp)) {
		return;
	}

	reply(buf, cap, rsp, 200, "{\"ok\":true,\"message\":\"rebooting in 500 ms\"}");
	rsp->restart = true;
}

void pstop_api_ring_offset(const char *url, char *buf, size_t cap, struct pstop_api_response *rsp)
{
	char val[8];
	uint32_t n;

	init_rsp(rsp);

	if ((http_query_get(url, "n", val, sizeof(val)) != 0) || (http_parse_u32(val, &n) != 0) ||
	    (n > 15U)) {
		reply(buf, cap, rsp, 400, "{\"ok\":false,\"error\":\"n must be 0..15\"}");
		return;
	}
	if (app_settings_set_ring_offset((uint8_t)n) != 0) {
		reply(buf, cap, rsp, 500, "{\"ok\":false,\"error\":\"settings write failed\"}");
		return;
	}
	LOG_INF("ring offset -> %u", n);
	reply(buf, cap, rsp, 200, "{\"ok\":true,\"ring_offset\":%u}", n);
}

void pstop_api_ring_led1(const char *url, char *buf, size_t cap, struct pstop_api_response *rsp)
{
	char val[4];
	bool on;

	init_rsp(rsp);

	if ((http_query_get(url, "on", val, sizeof(val)) != 0) ||
	    ((strcmp(val, "0") != 0) && (strcmp(val, "1") != 0))) {
		reply(buf, cap, rsp, 400, "{\"ok\":false,\"error\":\"on must be 0 or 1\"}");
		return;
	}
	on = (val[0] == '1');
	leds_locate(on);
	LOG_INF("ring locate %s", on ? "on" : "off");
	reply(buf, cap, rsp, 200, "{\"ok\":true,\"ring_led1\":%d,\"ring_offset\":%u}", on ? 1 : 0,
	      app_settings_ring_offset());
}

void pstop_api_health(char *buf, size_t cap, struct pstop_api_response *rsp)
{
	static struct health_snapshot snap; /* ~1 KB: off the HTTP thread's stack */
	struct out o = {.buf = buf, .cap = cap};
	const struct health_counters *c = &snap.counters;
	uint64_t now = (uint64_t)k_uptime_get();

	init_rsp(rsp);
	health_snapshot_get(&snap);

	out_printf(&o,
		   "{\"ok\":true,\"level\":%d,\"presses\":%u,\"mismatch_events\":%u,"
		   "\"uptime_s\":%u,\"boots\":%u,\"flashes\":%u,\"otas\":%u,\"button_swaps\":%u,"
		   "\"button_rated_ops\":%u,\"button_wear_pct\":%u,\"fw_sha\":\"%s\","
		   "\"nvs_flushes\":%u,\"nvs_flush_fails\":%u,\"last_flush_age_s\":%u,"
		   "\"warnings\":[",
		   snap.level, c->presses, c->mismatch_events, c->uptime_s, c->boots, c->flashes,
		   c->otas, c->button_swaps, snap.button_rated_ops, snap.button_wear_pct, FW_SHA,
		   snap.flushes, snap.flush_fails, snap.last_flush_age_s);
	for (int i = 0; i < snap.board.n; i++) {
		const struct health_warning *w = &snap.board.w[i];

		/* Sources and messages are this firmware's own fixed text:
		 * nothing to escape.
		 */
		out_printf(&o,
			   "%s{\"src\":\"%s\",\"level\":%d,\"msg\":\"%s\",\"count\":%u,"
			   "\"age_s\":%u}",
			   (i == 0) ? "" : ",", w->src, w->level, w->msg, w->count,
			   (unsigned int)((now - w->first_ms) / MSEC_PER_SEC));
	}
	out_printf(&o, "],\"dropped\":%u}", snap.board.dropped);

	finish(&o, 200, rsp);
}

void pstop_api_health_reset(const char *url, const char *auth, char *buf, size_t cap,
			    struct pstop_api_response *rsp)
{
	char what[8];
	int ret;

	init_rsp(rsp);

	if (!admin_ok(auth, buf, cap, rsp)) {
		return;
	}
	if (!http_query_flag(url, "confirm")) {
		reply(buf, cap, rsp, 400, "{\"ok\":false,\"error\":\"confirm=1 required\"}");
		return;
	}
	if (http_query_get(url, "what", what, sizeof(what)) != 0) {
		what[0] = '\0';
	}
	if (strcmp(what, "button") == 0) {
		ret = health_reset_button();
	} else if (strcmp(what, "all") == 0) {
		ret = health_reset_all();
	} else {
		reply(buf, cap, rsp, 400,
		      "{\"ok\":false,\"error\":\"what must be button or all\"}");
		return;
	}

	reply(buf, cap, rsp, (ret == 0) ? 200 : 500, "{\"ok\":%s,\"reset\":\"%s\"}",
	      (ret == 0) ? "true" : "false", what);
}

void pstop_api_coredump(const char *auth, char *buf, size_t cap, struct pstop_api_response *rsp,
			size_t *size)
{
	init_rsp(rsp);
	*size = 0U;

	if (!admin_ok(auth, buf, cap, rsp)) {
		return;
	}

	*size = crash_size();
	if (*size == 0U) {
		reply(buf, cap, rsp, 404, "{\"ok\":false,\"error\":\"no coredump\"}");
		return;
	}
	rsp->status = 200;
}

void pstop_api_admin_page(const char *auth, char *buf, size_t cap, struct pstop_api_response *rsp)
{
	init_rsp(rsp);

	if (admin_ok(auth, buf, cap, rsp)) {
		rsp->status = 200;
	}
}

struct settings_json {
	uint32_t ring_offset;
	uint32_t device_id;
};

static const struct json_obj_descr settings_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct settings_json, ring_offset, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(struct settings_json, device_id, JSON_TOK_UINT),
};

void pstop_api_admin_settings(bool post, char *body, size_t body_len, const char *auth, char *buf,
			      size_t cap, struct pstop_api_response *rsp)
{
	struct settings_json in = {0};
	int64_t fields;

	init_rsp(rsp);

	if (!admin_ok(auth, buf, cap, rsp)) {
		return;
	}

	if (post) {
		fields = json_obj_parse(body, body_len, settings_descr, ARRAY_SIZE(settings_descr),
					&in);
		if (fields <= 0) {
			reply(buf, cap, rsp, 400,
			      "{\"ok\":false,\"error\":\"expected a JSON object with ring_offset "
			      "and/or device_id\"}");
			return;
		}
		if (((fields & BIT(0)) != 0) && (in.ring_offset > 15U)) {
			reply(buf, cap, rsp, 400,
			      "{\"ok\":false,\"error\":\"ring_offset must be 0..15\"}");
			return;
		}
		if (((fields & BIT(0)) != 0) &&
		    (app_settings_set_ring_offset((uint8_t)in.ring_offset) != 0)) {
			reply(buf, cap, rsp, 500,
			      "{\"ok\":false,\"error\":\"settings write failed\"}");
			return;
		}
		if ((fields & BIT(1)) != 0) {
			if (app_settings_set_device_id(in.device_id) != 0) {
				reply(buf, cap, rsp, 500,
				      "{\"ok\":false,\"error\":\"settings write failed\"}");
				return;
			}
			/* Machines key the bond on our id: start every
			 * session afresh under the new one.
			 */
			for (int slot = 0; slot < PSTOP_MAX_MACHINES; slot++) {
				(void)pstop_lockstep_request_reconfigure(slot);
			}
			LOG_INF("device id -> %08x", app_settings_effective_device_id());
		}
	}

	reply(buf, cap, rsp, 200,
	      "{\"ok\":true,\"ring_offset\":%u,\"device_id\":%u,\"device_id_pinned\":%s}",
	      app_settings_ring_offset(), (unsigned int)app_settings_effective_device_id(),
	      (app_settings_device_id() != 0U) ? "true" : "false");
}

#define WIFI_MASK "********"

struct wifi_net_json {
	const char *ssid;
	const char *pass;
};

struct wifi_json {
	struct wifi_net_json networks[PSTOP_WIFI_MAX];
	size_t networks_len;
};

static const struct json_obj_descr wifi_net_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct wifi_net_json, ssid, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct wifi_net_json, pass, JSON_TOK_STRING),
};

static const struct json_obj_descr wifi_descr[] = {
	JSON_OBJ_DESCR_OBJ_ARRAY(struct wifi_json, networks, PSTOP_WIFI_MAX, networks_len,
				 wifi_net_descr, ARRAY_SIZE(wifi_net_descr)),
};

/* Stored strings are echoed into JSON unescaped, so refuse anything that
 * would need escaping. The JSON parser does not decode escapes either.
 */
static bool plain_string(const char *s)
{
	for (; *s != '\0'; s++) {
		if ((*s == '"') || (*s == '\\') || ((unsigned char)*s < 0x20U)) {
			return false;
		}
	}
	return true;
}

static const char *wifi_from_json(const struct wifi_json *in, struct pstop_wifi_net *nets)
{
	struct pstop_wifi_net old;

	(void)memset(nets, 0, sizeof(struct pstop_wifi_net) * PSTOP_WIFI_MAX);

	for (size_t i = 0; i < in->networks_len; i++) {
		const char *ssid = in->networks[i].ssid;
		const char *pass = (in->networks[i].pass != NULL) ? in->networks[i].pass : "";

		if ((ssid == NULL) || (ssid[0] == '\0') || (strlen(ssid) > PSTOP_WIFI_SSID_MAX) ||
		    !plain_string(ssid)) {
			return "ssid must be 1..32 plain characters";
		}
		(void)strcpy(nets[i].ssid, ssid);

		if (strcmp(pass, WIFI_MASK) == 0) {
			/* Keep the password stored for this ssid, if any. */
			for (int j = 0; j < PSTOP_WIFI_MAX; j++) {
				if ((app_settings_wifi_get(j, &old) == 0) &&
				    (strcmp(old.ssid, ssid) == 0)) {
					(void)strcpy(nets[i].pass, old.pass);
					break;
				}
			}
			continue;
		}
		if ((pass[0] != '\0') &&
		    ((strlen(pass) < 8U) || (strlen(pass) > PSTOP_WIFI_PASS_MAX) ||
		     !plain_string(pass))) {
			return "pass must be empty (open network) or 8..64 plain characters";
		}
		(void)strcpy(nets[i].pass, pass);
	}
	return NULL;
}

void pstop_api_admin_wifi(bool post, char *body, size_t body_len, const char *auth, char *buf,
			  size_t cap, struct pstop_api_response *rsp)
{
	struct out o = {.buf = buf, .cap = cap};
	struct pstop_wifi_net net;
	bool first = true;

	init_rsp(rsp);

	if (!admin_ok(auth, buf, cap, rsp)) {
		return;
	}

	if (post) {
		static struct pstop_wifi_net nets[PSTOP_WIFI_MAX];
		struct wifi_json in = {0};
		const char *err;

		if (json_obj_parse(body, body_len, wifi_descr, ARRAY_SIZE(wifi_descr), &in) !=
		    BIT(0)) {
			reply(buf, cap, rsp, 400,
			      "{\"ok\":false,\"error\":\"expected {\\\"networks\\\":[{\\\"ssid\\\","
			      "\\\"pass\\\"}]}, at most 4\"}");
			return;
		}
		err = wifi_from_json(&in, nets);
		if (err != NULL) {
			reply(buf, cap, rsp, 400, "{\"ok\":false,\"error\":\"%s\"}", err);
			return;
		}
		if (app_settings_wifi_set_all(nets) != 0) {
			reply(buf, cap, rsp, 500,
			      "{\"ok\":false,\"error\":\"settings write failed\"}");
			return;
		}
		LOG_INF("wifi: %u network(s) stored", (unsigned int)in.networks_len);
		pstop_wifi_reload();
	}

	out_printf(&o, "{\"ok\":true,\"networks\":[");
	for (int i = 0; i < PSTOP_WIFI_MAX; i++) {
		if ((app_settings_wifi_get(i, &net) != 0) || (net.ssid[0] == '\0')) {
			continue;
		}
		out_printf(&o, "%s{\"ssid\":\"%s\",\"pass\":\"%s\"}", first ? "" : ",", net.ssid,
			   (net.pass[0] != '\0') ? WIFI_MASK : "");
		first = false;
	}
	out_printf(&o, "],\"active_idx\":%d}", pstop_wifi_active_idx());
	finish(&o, 200, rsp);
}

void pstop_api_admin_verbose(bool toggle, const char *auth, char *buf, size_t cap,
			     struct pstop_api_response *rsp)
{
	init_rsp(rsp);

	if (!admin_ok(auth, buf, cap, rsp)) {
		return;
	}

	if (toggle) {
		if (pstop_verbose_set(!pstop_verbose_get()) != 0) {
			reply(buf, cap, rsp, 501,
			      "{\"ok\":false,\"error\":\"verbose logging not built in\"}");
			return;
		}
		LOG_INF("verbose logging %s", pstop_verbose_get() ? "on" : "off");
		reply(buf, cap, rsp, 200, "{\"ok\":true,\"verbose\":%s}",
		      pstop_verbose_get() ? "true" : "false");
		return;
	}

	reply(buf, cap, rsp, 200, "{\"verbose\":%s}", pstop_verbose_get() ? "true" : "false");
}
