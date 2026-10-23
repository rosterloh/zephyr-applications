/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * The control plane without a socket: the request parsers in http_util.c,
 * then every route in api.c against the real settings store and comparator.
 * The live HTTP path (Zephyr's server on native_sim, driven by upstream's
 * tools/pstop_multi_machine_test.py) is covered in the app's README.
 *
 * Slot changes take effect on the comparator's next tick, so the route tests
 * run pstop_lockstep_tick() themselves between a request and the checks that
 * depend on it.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "api.h"
#include "app_settings.h"
#include "estop_gpio.h"
#include "health.h"
#include "http_util.h"
#include "leds.h"
#include "lockstep.h"
#include "session.h"
#include "verbose.h"

/* CONFIG_PROTECTIVE_STOP_ADMIN_PASSWORD is "s3cret" in this suite's Kconfig;
 * the admin_disabled variant in testcase.yaml empties it, as shipped.
 */
#define AUTH_OK         "Basic YWRtaW46czNjcmV0"     /* admin:s3cret */
#define AUTH_UPSTREAM   "Basic YWRtaW46bWljcm9saW5r" /* admin:microlink */
#define AUTH_LONGER     "Basic YWRtaW46czNjcmV0Mg==" /* admin:s3cret2 */
#define AUTH_SHORTER    "Basic YWRtaW46czNjcmU="     /* admin:s3cre */
#define AUTH_WRONG_USER "Basic cm9vdDpzM2NyZXQ="     /* root:s3cret */
#define AUTH_NO_COLON   "Basic YWRtaW5zM2NyZXQ="     /* admins3cret */
#define AUTH_EMPTY_PW   "Basic YWRtaW46"             /* admin: */

static bool admin_configured(void)
{
	return CONFIG_PROTECTIVE_STOP_ADMIN_PASSWORD[0] != '\0';
}

/* Cases that need a password skip in the admin_disabled variant. */
#define REQUIRE_ADMIN()                                                                            \
	do {                                                                                       \
		if (!admin_configured()) {                                                         \
			ztest_test_skip();                                                         \
		}                                                                                  \
	} while (false)

static char body[PSTOP_API_BODY_MAX];
static struct pstop_api_response rsp;
static uint64_t now_ms = 1000U;

static void tick(void)
{
	now_ms += PSTOP_TICK_MS;
	pstop_lockstep_tick(now_ms);
}

static void assert_body(const char *expected)
{
	zassert_equal(rsp.len, strlen(expected), "len %zu: %s", rsp.len, body);
	zassert_mem_equal(body, expected, rsp.len, "got %.*s", (int)rsp.len, body);
}

static bool body_has(const char *needle)
{
	body[MIN(rsp.len, sizeof(body) - 1U)] = '\0';
	return strstr(body, needle) != NULL;
}

/* ------------------------------------------------------------------ */
/* http_util                                                           */
/* ------------------------------------------------------------------ */

ZTEST(http_util, test_query_get)
{
	char v[8];

	zassert_ok(http_query_get("/p?a=1&slot=2&b=x", "slot", v, sizeof(v)));
	zassert_str_equal(v, "2");
	zassert_ok(http_query_get("/p?slot=3", "slot", v, sizeof(v)), "first pair");
	zassert_str_equal(v, "3");
	zassert_ok(http_query_get("/p?ip=", "ip", v, sizeof(v)), "empty value");
	zassert_str_equal(v, "");

	zassert_equal(http_query_get("/p", "slot", v, sizeof(v)), -ENOENT, "no query");
	zassert_equal(http_query_get("/p?", "slot", v, sizeof(v)), -ENOENT, "empty query");
	zassert_equal(http_query_get("/p?slots=1", "slot", v, sizeof(v)), -ENOENT,
		      "longer key must not match");
	zassert_equal(http_query_get("/p?xslot=1", "slot", v, sizeof(v)), -ENOENT,
		      "suffix key must not match");
	zassert_equal(http_query_get("/p?slot", "slot", v, sizeof(v)), -ENOENT, "no '='");

	zassert_equal(http_query_get("/p?ip=255.255.255.255", "ip", v, sizeof(v)), -ENOSPC);
	zassert_str_equal(v, "", "never truncated");

	zassert_equal(http_query_get(NULL, "a", v, sizeof(v)), -EINVAL);
	zassert_equal(http_query_get("/p?a=1", "a", v, 0U), -EINVAL);
}

ZTEST(http_util, test_query_flag)
{
	zassert_true(http_query_flag("/p?slot=1&clear=1", "clear"));
	zassert_false(http_query_flag("/p?clear=0", "clear"));
	zassert_false(http_query_flag("/p?clear=11", "clear"));
	zassert_false(http_query_flag("/p?clear=true", "clear"));
	zassert_false(http_query_flag("/p?clear", "clear"));
	zassert_false(http_query_flag("/p", "clear"));
}

ZTEST(http_util, test_parse_ipv4)
{
	uint32_t ip;

	zassert_ok(http_parse_ipv4("192.168.1.100", &ip));
	zassert_equal(ip, 0xC0A80164U, "host order");
	zassert_ok(http_parse_ipv4("0.0.0.0", &ip));
	zassert_equal(ip, 0U);
	zassert_ok(http_parse_ipv4("255.255.255.255", &ip));
	zassert_equal(ip, 0xFFFFFFFFU);

	static const char *const bad[] = {
		"",       "1.2.3",  "1.2.3.4.5", "256.1.1.1", "1.2.3.4 ",   "1..3.4",
		"1.2.3.", ".1.2.3", "a.b.c.d",   "1.2.3.-4",  "0001.1.1.1", "1.2.3.4x",
	};

	for (size_t i = 0; i < ARRAY_SIZE(bad); i++) {
		zassert_equal(http_parse_ipv4(bad[i], &ip), -EINVAL, "accepted \"%s\"", bad[i]);
	}
}

ZTEST(http_util, test_parse_u32)
{
	uint32_t v;

	zassert_ok(http_parse_u32("8890", &v));
	zassert_equal(v, 8890U);
	zassert_ok(http_parse_u32("0x01020304", &v));
	zassert_equal(v, 0x01020304U);
	zassert_ok(http_parse_u32("0XaBcD", &v));
	zassert_equal(v, 0xABCDU);
	zassert_ok(http_parse_u32("4294967295", &v));
	zassert_equal(v, UINT32_MAX);

	static const char *const bad[] = {
		"", "0x", "4294967296", "0x100000000", "-1", "12a", "0xg", " 1", "1 ",
	};

	for (size_t i = 0; i < ARRAY_SIZE(bad); i++) {
		zassert_equal(http_parse_u32(bad[i], &v), -EINVAL, "accepted \"%s\"", bad[i]);
	}
}

ZTEST(http_util, test_basic_auth)
{
	zassert_true(http_basic_auth_ok(AUTH_OK, "admin", "s3cret"));

	zassert_false(http_basic_auth_ok(AUTH_UPSTREAM, "admin", "s3cret"), "other password");
	zassert_false(http_basic_auth_ok(AUTH_LONGER, "admin", "s3cret"), "password + suffix");
	zassert_false(http_basic_auth_ok(AUTH_SHORTER, "admin", "s3cret"), "password prefix");
	zassert_false(http_basic_auth_ok(AUTH_WRONG_USER, "admin", "s3cret"), "other user");
	zassert_false(http_basic_auth_ok(AUTH_NO_COLON, "admin", "s3cret"), "no separator");
	zassert_false(http_basic_auth_ok("YWRtaW46czNjcmV0", "admin", "s3cret"), "no scheme");
	zassert_false(http_basic_auth_ok("Bearer YWRtaW46czNjcmV0", "admin", "s3cret"),
		      "other scheme");
	zassert_false(http_basic_auth_ok("Basic !!!!", "admin", "s3cret"), "not base64");
	zassert_false(http_basic_auth_ok("Basic ", "admin", "s3cret"), "empty credentials");
	zassert_false(http_basic_auth_ok("", "admin", "s3cret"), "empty header");
	zassert_false(http_basic_auth_ok(NULL, "admin", "s3cret"), "no header");
	zassert_false(http_basic_auth_ok(AUTH_EMPTY_PW, "admin", ""),
		      "an empty password never matches, not even \"admin:\"");
}

ZTEST_SUITE(http_util, NULL, NULL, NULL, NULL, NULL);

/* ------------------------------------------------------------------ */
/* api routes                                                          */
/* ------------------------------------------------------------------ */

static void *api_setup(void)
{
	zassert_ok(app_settings_init(), "settings");
	zassert_ok(app_settings_set_device_id(0xC0A80105U), "device id");
	zassert_ok(health_init(), "health");
	zassert_ok(estop_gpio_init(), "gpio");
	zassert_ok(pstop_lockstep_init(), "lockstep");
	return NULL;
}

/* Every case starts from an empty peer table, a stop-only role and an
 * unrotated ring with locate off: the flash file outlives a run.
 */
static void api_before(void *fixture)
{
	ARG_UNUSED(fixture);

	for (int slot = 0; slot < PSTOP_MAX_MACHINES; slot++) {
		(void)app_settings_clear_peer(slot);
		(void)pstop_lockstep_request_reconfigure(slot);
	}
	(void)app_settings_set_operator(false);
	(void)app_settings_set_ring_offset(0U);
	leds_locate(false);
	tick();
	(void)memset(body, 0, sizeof(body));
}

ZTEST(api, test_state_has_what_upstream_tools_read)
{
	/* Keys read by tools/pstop_multi_machine_test.py, tools/hil/rig.py
	 * test_00 and the admin page, plus the lockstep attribution.
	 */
	static const char *const keys[] = {
		"\"uptime_ms\":",
		"\"fw_ver\":\"9.8.7\"",
		"\"fw_sha\":",
		"\"role\":\"stop_only\"",
		"\"device_id\":3232235781",
		"\"l0\":",
		"\"l1\":",
		"\"e_hi0\":",
		"\"e_lo0\":",
		"\"e_hi1\":",
		"\"e_lo1\":",
		"\"pstop_peer_ip\":",
		"\"pstop_peer_port\":",
		"\"pstop_sent\":",
		"\"pstop_replies\":",
		"\"pstop_last_msg\":",
		"\"pstop_send_fail\":",
		"\"pstop_rebonds\":",
		"\"pstop_mismatch\":",
		"\"pstop_mm_timeout\":",
		"\"pstop_mm_content\":",
		"\"pstop_mm_last\":[",
		"\"pstop_core_lat_max_ms\":[",
		"\"pstop_gated_ticks\":",
		"\"health\":0",
		"\"ring_offset\":",
		"\"ring_led1\":0",
		"\"active_iface\":\"",
		"\"local_ip\":\"",
		"\"wifi_idx\":-1",
		"\"crash_present\":0",
		"\"pstop_machines\":[",
	};

	pstop_api_state(body, sizeof(body), &rsp);

	zassert_equal(rsp.status, 200);
	zassert_false(rsp.auth_challenge);
	zassert_true(rsp.len > 0U && rsp.len < sizeof(body));
	zassert_equal(body[0], '{');
	zassert_equal(body[rsp.len - 1U], '}');
	for (size_t i = 0; i < ARRAY_SIZE(keys); i++) {
		zassert_true(body_has(keys[i]), "missing %s", keys[i]);
	}
}

ZTEST(api, test_state_lists_every_slot)
{
	const char *p;
	int count = 0;

	/* Stable indices: upstream's tooling addresses machines by slot
	 * number, so unconfigured slots are listed too.
	 */
	pstop_api_state(body, sizeof(body), &rsp);
	body[rsp.len] = '\0';
	for (p = strstr(body, "{\"cfg\":"); p != NULL; p = strstr(p + 1, "{\"cfg\":")) {
		count++;
	}
	zassert_equal(count, PSTOP_MAX_MACHINES);
}

ZTEST(api, test_state_overflow_is_an_error_not_a_truncation)
{
	char small[64];

	pstop_api_state(small, sizeof(small), &rsp);

	zassert_equal(rsp.status, 500);
	zassert_true(rsp.len < sizeof(small));
	zassert_mem_equal(small, "{\"ok\":false,\"error\":\"response too large\"}", rsp.len);
}

ZTEST(api, test_set_peer_slot)
{
	struct pstop_peer peer;

	pstop_api_pstop_peers("/api/pstop_peers?slot=2&ip=127.0.0.1&port=8892&id=0x0A0B0C0D", body,
			      sizeof(body), &rsp);
	zassert_equal(rsp.status, 200);
	assert_body("{\"ok\":true,\"slot\":2,\"ip\":\"127.0.0.1\",\"port\":8892,"
		    "\"id\":168496141}");

	zassert_ok(app_settings_peer_get(2, &peer));
	zassert_true(peer.configured);
	zassert_equal(peer.ip, 0x7F000001U);
	zassert_equal(peer.port, 8892U);
	zassert_equal(peer.id, 0x0A0B0C0DU);

	/* Applied by the comparator, not by the HTTP thread. */
	zassert_false(pstop_lockstep_slot_active(2), "before the tick");
	tick();
	zassert_true(pstop_lockstep_slot_active(2), "after the tick");

	pstop_api_state(body, sizeof(body), &rsp);
	zassert_true(body_has("{\"cfg\":1,\"ip\":2130706433,\"port\":8892,\"id\":168496141,"
			      "\"state\":1,"),
		     "slot 2 bonding in %s", body);
}

ZTEST(api, test_set_peer_default_id)
{
	struct pstop_peer peer;

	pstop_api_pstop_peers("/api/pstop_peers?slot=1&ip=10.0.0.2&port=8890", body, sizeof(body),
			      &rsp);
	zassert_equal(rsp.status, 200);
	zassert_ok(app_settings_peer_get(1, &peer));
	zassert_equal(peer.id, 0x01020304U, "machine_app_runner's default id");
}

ZTEST(api, test_set_peer_rejects_bad_input)
{
	static const char *const bad[] = {
		"/api/pstop_peers",
		"/api/pstop_peers?ip=1.2.3.4&port=8890",
		"/api/pstop_peers?slot=4&ip=1.2.3.4&port=8890",
		"/api/pstop_peers?slot=-1&ip=1.2.3.4&port=8890",
		"/api/pstop_peers?slot=x&ip=1.2.3.4&port=8890",
		"/api/pstop_peers?slot=1&port=8890",
		"/api/pstop_peers?slot=1&ip=1.2.3&port=8890",
		"/api/pstop_peers?slot=1&ip=1.2.3.4",
		"/api/pstop_peers?slot=1&ip=1.2.3.4&port=0",
		"/api/pstop_peers?slot=1&ip=1.2.3.4&port=65536",
		"/api/pstop_peers?slot=1&ip=1.2.3.4&port=8890&id=0",
		"/api/pstop_peers?slot=1&ip=1.2.3.4&port=8890&id=0xZZ",
		"/api/pstop_peers?slot=1&ip=1.2.3.4&port=8890&id=",
	};
	struct pstop_peer peer;

	for (size_t i = 0; i < ARRAY_SIZE(bad); i++) {
		pstop_api_pstop_peers(bad[i], body, sizeof(body), &rsp);
		zassert_equal(rsp.status, 400, "%s -> %d", bad[i], rsp.status);
		zassert_true(body_has("\"ok\":false"), "%s", bad[i]);
	}

	/* None of them touched the table. */
	for (int slot = 0; slot < PSTOP_MAX_MACHINES; slot++) {
		zassert_ok(app_settings_peer_get(slot, &peer));
		zassert_false(peer.configured, "slot %d", slot);
	}
}

ZTEST(api, test_clear_peer_slot)
{
	struct pstop_peer peer;

	pstop_api_pstop_peers("/api/pstop_peers?slot=3&ip=127.0.0.1&port=8893", body, sizeof(body),
			      &rsp);
	tick();
	zassert_true(pstop_lockstep_slot_active(3));

	pstop_api_pstop_peers("/api/pstop_peers?slot=3&clear=1", body, sizeof(body), &rsp);
	zassert_equal(rsp.status, 200);
	assert_body("{\"ok\":true,\"slot\":3,\"cleared\":true}");
	zassert_ok(app_settings_peer_get(3, &peer));
	zassert_false(peer.configured);

	tick();
	zassert_false(pstop_lockstep_slot_active(3), "idle after the tick");
}

ZTEST(api, test_rebond)
{
	pstop_api_pstop_peers("/api/pstop_peers?slot=1&rebond=1", body, sizeof(body), &rsp);
	zassert_equal(rsp.status, 200);
	assert_body("{\"ok\":true,\"slot\":1,\"rebond\":true}");

	pstop_api_pstop_peers("/api/pstop_peers?slot=7&rebond=1", body, sizeof(body), &rsp);
	zassert_equal(rsp.status, 400, "range is checked before rebond");
}

ZTEST(api, test_legacy_single_peer_is_slot_0)
{
	struct pstop_peer peer;

	pstop_api_pstop_peer("/api/pstop_peer?ip=192.168.1.100&port=8890", body, sizeof(body),
			     &rsp);
	zassert_equal(rsp.status, 200);
	assert_body("{\"ok\":true,\"ip\":\"192.168.1.100\",\"port\":8890}");
	zassert_ok(app_settings_peer_get(0, &peer));
	zassert_true(peer.configured);
	zassert_equal(peer.ip, 0xC0A80164U);

	pstop_api_state(body, sizeof(body), &rsp);
	zassert_true(body_has("\"pstop_peer_ip\":3232235876,\"pstop_peer_port\":8890,"));

	pstop_api_pstop_peer("/api/pstop_peer?clear=1", body, sizeof(body), &rsp);
	zassert_equal(rsp.status, 200);
	zassert_ok(app_settings_peer_get(0, &peer));
	zassert_false(peer.configured);

	pstop_api_pstop_peer("/api/pstop_peer?ip=1.2.3.4", body, sizeof(body), &rsp);
	zassert_equal(rsp.status, 400);
}

ZTEST(api, test_role_needs_admin)
{
	REQUIRE_ADMIN();

	static const char *const bad[] = {
		NULL, "", AUTH_UPSTREAM, AUTH_WRONG_USER, AUTH_LONGER,
	};

	for (size_t i = 0; i < ARRAY_SIZE(bad); i++) {
		pstop_api_role(true, "/api/role?role=operator", bad[i], body, sizeof(body), &rsp);
		zassert_equal(rsp.status, 401, "auth %zu", i);
		zassert_true(rsp.auth_challenge, "auth %zu", i);
		pstop_api_role(false, "/api/role", bad[i], body, sizeof(body), &rsp);
		zassert_equal(rsp.status, 401, "GET auth %zu", i);
	}
	zassert_false(app_settings_is_operator(), "never promoted");
}

ZTEST(api, test_role_get_and_set)
{
	REQUIRE_ADMIN();

	pstop_api_role(false, "/api/role", AUTH_OK, body, sizeof(body), &rsp);
	zassert_equal(rsp.status, 200);
	assert_body("{\"ok\":true,\"role\":\"stop_only\"}");

	pstop_api_role(true, "/api/role?role=operator", AUTH_OK, body, sizeof(body), &rsp);
	zassert_equal(rsp.status, 200);
	assert_body("{\"ok\":true,\"role\":\"operator\",\"applied\":true}");
	zassert_true(app_settings_is_operator());

	pstop_api_state(body, sizeof(body), &rsp);
	zassert_true(body_has("\"role\":\"operator\""));

	pstop_api_role(true, "/api/role?role=stop_only", AUTH_OK, body, sizeof(body), &rsp);
	zassert_equal(rsp.status, 200);
	zassert_false(app_settings_is_operator());
}

ZTEST(api, test_role_rejects_unknown_roles)
{
	REQUIRE_ADMIN();

	pstop_api_role(true, "/api/role?role=admin", AUTH_OK, body, sizeof(body), &rsp);
	zassert_equal(rsp.status, 400);
	pstop_api_role(true, "/api/role", AUTH_OK, body, sizeof(body), &rsp);
	zassert_equal(rsp.status, 400);
	zassert_false(app_settings_is_operator());
}

ZTEST(api, test_admin_status)
{
	REQUIRE_ADMIN();

	pstop_api_admin_status(NULL, body, sizeof(body), &rsp);
	zassert_equal(rsp.status, 401);
	zassert_true(rsp.auth_challenge);

	pstop_api_admin_status(AUTH_OK, body, sizeof(body), &rsp);
	zassert_equal(rsp.status, 200);
	zassert_true(body_has("\"fw_ver\":\"9.8.7\""));
	zassert_true(body_has("\"sessions\":0"));
}

ZTEST(api, test_admin_restart)
{
	REQUIRE_ADMIN();

	pstop_api_admin_restart(AUTH_UPSTREAM, body, sizeof(body), &rsp);
	zassert_equal(rsp.status, 401);
	zassert_false(rsp.restart, "no restart without auth");

	pstop_api_admin_restart(AUTH_OK, body, sizeof(body), &rsp);
	zassert_equal(rsp.status, 200);
	zassert_true(rsp.restart);
}

ZTEST(api, test_state_reports_a_late_sampler)
{
	char expected[48];
	uint32_t before = pstop_lockstep_missed_deadlines();

	/* Channel 1 misses the tick's deadline: upstream packs that as
	 * kind=timeout(1)<<28 | late_mask<<26 | 0xFF<<16 (no byte offset).
	 */
	now_ms += PSTOP_TICK_MS;
	pstop_lockstep_test_tick_late(now_ms, BIT(1));
	zassert_equal(pstop_lockstep_missed_deadlines(), before + 1U);

	pstop_api_state(body, sizeof(body), &rsp);
	(void)snprintf(expected, sizeof(expected), "\"pstop_mm_last\":[%u,0,",
		       (unsigned int)((1U << 28) | (BIT(1) << 26) | (0xFFU << 16)));
	zassert_true(body_has(expected), "want %s in %s", expected, body);
	(void)snprintf(expected, sizeof(expected), "\"pstop_mm_timeout\":%u,", before + 1U);
	zassert_true(body_has(expected), "want %s", expected);
}

ZTEST(api, test_ring_offset)
{
	static const char *const bad[] = {
		"/api/ring_offset?n=16",
		"/api/ring_offset?n=-1",
		"/api/ring_offset?n=x",
		"/api/ring_offset",
	};

	pstop_api_ring_offset("/api/ring_offset?n=7", body, sizeof(body), &rsp);
	zassert_equal(rsp.status, 200);
	assert_body("{\"ok\":true,\"ring_offset\":7}");
	zassert_equal(app_settings_ring_offset(), 7U);

	pstop_api_state(body, sizeof(body), &rsp);
	zassert_true(body_has("\"ring_offset\":7,"));

	for (size_t i = 0; i < ARRAY_SIZE(bad); i++) {
		pstop_api_ring_offset(bad[i], body, sizeof(body), &rsp);
		zassert_equal(rsp.status, 400, "accepted %s", bad[i]);
	}
	zassert_equal(app_settings_ring_offset(), 7U, "unchanged by bad requests");

	pstop_api_ring_offset("/api/ring_offset?n=0", body, sizeof(body), &rsp);
	zassert_equal(rsp.status, 200);
}

ZTEST(api, test_ring_led1_locate)
{
	pstop_api_ring_led1("/api/ring_led1?on=1", body, sizeof(body), &rsp);
	zassert_equal(rsp.status, 200);
	zassert_true(leds_locate_active());
	pstop_api_state(body, sizeof(body), &rsp);
	zassert_true(body_has("\"ring_led1\":1,"));

	pstop_api_ring_led1("/api/ring_led1?on=2", body, sizeof(body), &rsp);
	zassert_equal(rsp.status, 400);
	zassert_true(leds_locate_active(), "a bad request changes nothing");

	pstop_api_ring_led1("/api/ring_led1?on=0", body, sizeof(body), &rsp);
	zassert_equal(rsp.status, 200);
	zassert_false(leds_locate_active());
}

static void ticks_with_poles(bool a, bool b, int n)
{
	estop_sim_set_pole(0, a);
	estop_sim_set_pole(1, b);
	for (int i = 0; i < n; i++) {
		tick();
	}
}

ZTEST(api, test_health_counts_presses_and_mismatches)
{
	struct health_snapshot snap;
	uint32_t presses;
	uint32_t mismatches;
	char expected[48];

	ticks_with_poles(true, true, 10);
	health_snapshot_get(&snap);
	presses = snap.counters.presses;
	mismatches = snap.counters.mismatch_events;

	/* Both poles open: one press, however long it is held. */
	ticks_with_poles(false, false, 10);
	ticks_with_poles(true, true, 10);
	health_snapshot_get(&snap);
	zassert_equal(snap.counters.presses, presses + 1U, "one press");
	zassert_equal(snap.counters.mismatch_events, mismatches);

	/* One pole open: a loop disagreement, not a press. */
	ticks_with_poles(false, true, 10);
	ticks_with_poles(true, true, 10);
	health_snapshot_get(&snap);
	zassert_equal(snap.counters.presses, presses + 1U, "not a press");
	zassert_equal(snap.counters.mismatch_events, mismatches + 1U, "one mismatch event");

	pstop_api_health(body, sizeof(body), &rsp);
	zassert_equal(rsp.status, 200);
	zassert_equal(body[rsp.len - 1U], '}');
	(void)snprintf(expected, sizeof(expected), "\"presses\":%u,", presses + 1U);
	zassert_true(body_has(expected), "want %s in %s", expected, body);
	zassert_true(body_has("\"button_rated_ops\":100000,"));
	zassert_true(body_has("\"warnings\":[]"));
	zassert_true(body_has("\"boots\":"));
}

ZTEST(api, test_health_warning_board_is_reported)
{
	zassert_ok(health_publish("test_src", HEALTH_WARN, "something odd"));
	pstop_api_health(body, sizeof(body), &rsp);
	zassert_true(body_has("\"level\":1,"));
	zassert_true(body_has("{\"src\":\"test_src\",\"level\":1,\"msg\":\"something odd\","
			      "\"count\":1,\"age_s\":"));
	pstop_api_state(body, sizeof(body), &rsp);
	zassert_true(body_has("\"health\":1,"));

	zassert_ok(health_publish("test_src", HEALTH_OK, ""));
	pstop_api_health(body, sizeof(body), &rsp);
	zassert_true(body_has("\"level\":0,"));
}

ZTEST(api, test_health_reset)
{
	struct health_snapshot snap;
	uint8_t swaps;

	REQUIRE_ADMIN();

	ticks_with_poles(true, true, 10);
	ticks_with_poles(false, false, 10);
	ticks_with_poles(true, true, 10);
	health_snapshot_get(&snap);
	zassert_true(snap.counters.presses > 0U);
	swaps = snap.counters.button_swaps;

	pstop_api_health_reset("/api/health/reset?what=button&confirm=1", NULL, body, sizeof(body),
			       &rsp);
	zassert_equal(rsp.status, 401);
	pstop_api_health_reset("/api/health/reset?what=button", AUTH_OK, body, sizeof(body), &rsp);
	zassert_equal(rsp.status, 400, "confirm=1 is required");
	pstop_api_health_reset("/api/health/reset?what=disk&confirm=1", AUTH_OK, body, sizeof(body),
			       &rsp);
	zassert_equal(rsp.status, 400);
	health_snapshot_get(&snap);
	zassert_true(snap.counters.presses > 0U, "nothing reset yet");

	pstop_api_health_reset("/api/health/reset?what=button&confirm=1", AUTH_OK, body,
			       sizeof(body), &rsp);
	zassert_equal(rsp.status, 200);
	health_snapshot_get(&snap);
	zassert_equal(snap.counters.presses, 0U);
	zassert_equal(snap.counters.mismatch_events, 0U);
	zassert_equal(snap.counters.button_swaps, swaps + 1U, "a switch swap is counted");
	zassert_true(snap.counters.boots > 0U, "the rest is kept");

	pstop_api_health_reset("/api/health/reset?what=all&confirm=1", AUTH_OK, body, sizeof(body),
			       &rsp);
	zassert_equal(rsp.status, 200);
	health_snapshot_get(&snap);
	zassert_equal(snap.counters.boots, 0U);
	zassert_equal(snap.counters.button_swaps, 0U);
}

ZTEST(api, test_coredump_none_on_native_sim)
{
	size_t size = 1U;

	REQUIRE_ADMIN();

	pstop_api_coredump(NULL, body, sizeof(body), &rsp, &size);
	zassert_equal(rsp.status, 401, "a coredump is a RAM snapshot: admin only");
	zassert_equal(size, 0U);

	pstop_api_coredump(AUTH_OK, body, sizeof(body), &rsp, &size);
	zassert_equal(rsp.status, 404);
	zassert_equal(size, 0U);
}

ZTEST(api, test_admin_page_gate)
{
	REQUIRE_ADMIN();

	pstop_api_admin_page(NULL, body, sizeof(body), &rsp);
	zassert_equal(rsp.status, 401);
	zassert_true(rsp.auth_challenge, "lets the browser ask for credentials");

	pstop_api_admin_page(AUTH_OK, body, sizeof(body), &rsp);
	zassert_equal(rsp.status, 200);
}

static void post_settings(const char *json)
{
	static char req[128];

	(void)strncpy(req, json, sizeof(req) - 1U);
	pstop_api_admin_settings(true, req, strlen(req), AUTH_OK, body, sizeof(body), &rsp);
}

ZTEST(api, test_admin_settings)
{
	REQUIRE_ADMIN();

	pstop_api_admin_settings(false, NULL, 0U, NULL, body, sizeof(body), &rsp);
	zassert_equal(rsp.status, 401);

	pstop_api_admin_settings(false, NULL, 0U, AUTH_OK, body, sizeof(body), &rsp);
	zassert_equal(rsp.status, 200);
	assert_body("{\"ok\":true,\"ring_offset\":0,\"device_id\":3232235781,"
		    "\"device_id_pinned\":true}");

	post_settings("{\"ring_offset\":12}");
	zassert_equal(rsp.status, 200);
	zassert_equal(app_settings_ring_offset(), 12U);

	post_settings("{\"ring_offset\":16}");
	zassert_equal(rsp.status, 400);
	post_settings("not json");
	zassert_equal(rsp.status, 400);
	post_settings("{\"colour\":\"red\"}");
	zassert_equal(rsp.status, 400, "no known field");
	zassert_equal(app_settings_ring_offset(), 12U);

	/* 0 unpins: the id comes from the hardware again. */
	post_settings("{\"device_id\":0}");
	zassert_equal(rsp.status, 200);
	zassert_true(body_has("\"device_id_pinned\":false"));
	zassert_equal(app_settings_device_id(), 0U);
	zassert_not_equal(app_settings_effective_device_id(), 0U, "derived, never 0");

	post_settings("{\"device_id\":4294967295,\"ring_offset\":0}");
	zassert_equal(rsp.status, 200);
	zassert_equal(app_settings_effective_device_id(), UINT32_MAX, "full 32-bit range");
	zassert_equal(app_settings_ring_offset(), 0U);

	zassert_ok(app_settings_set_device_id(0xC0A80105U), "restore for the other cases");
}

static void post_wifi(const char *json)
{
	static char req[512];

	(void)strncpy(req, json, sizeof(req) - 1U);
	pstop_api_admin_wifi(true, req, strlen(req), AUTH_OK, body, sizeof(body), &rsp);
}

ZTEST(api, test_admin_wifi)
{
	struct pstop_wifi_net net;

	REQUIRE_ADMIN();

	pstop_api_admin_wifi(false, NULL, 0U, NULL, body, sizeof(body), &rsp);
	zassert_equal(rsp.status, 401);

	post_wifi("{\"networks\":[{\"ssid\":\"home\",\"pass\":\"supersecret\"},"
		  "{\"ssid\":\"guest\",\"pass\":\"\"}]}");
	zassert_equal(rsp.status, 200);
	assert_body("{\"ok\":true,\"networks\":[{\"ssid\":\"home\",\"pass\":\"********\"},"
		    "{\"ssid\":\"guest\",\"pass\":\"\"}],\"active_idx\":-1}");
	zassert_ok(app_settings_wifi_get(0, &net));
	zassert_str_equal(net.pass, "supersecret", "stored, never echoed");

	/* The mask keeps the password stored for that ssid, wherever it
	 * moves in the list.
	 */
	post_wifi("{\"networks\":[{\"ssid\":\"office\",\"pass\":\"12345678\"},"
		  "{\"ssid\":\"home\",\"pass\":\"********\"}]}");
	zassert_equal(rsp.status, 200);
	zassert_ok(app_settings_wifi_get(1, &net));
	zassert_str_equal(net.ssid, "home");
	zassert_str_equal(net.pass, "supersecret");
	zassert_ok(app_settings_wifi_get(2, &net));
	zassert_str_equal(net.ssid, "", "the old list is replaced, not merged");

	static const char *const bad[] = {
		"{\"networks\":[{\"ssid\":\"\",\"pass\":\"\"}]}",
		"{\"networks\":[{\"pass\":\"12345678\"}]}",
		"{\"networks\":[{\"ssid\":\"q\\\"uote\",\"pass\":\"\"}]}",
		"{\"networks\":[{\"ssid\":\"short\",\"pass\":\"1234567\"}]}",
		"{\"networks\":[{\"ssid\":\"123456789012345678901234567890123\",\"pass\":\"\"}]}",
		"{\"networks\":[{\"ssid\":\"a\"},{\"ssid\":\"b\"},{\"ssid\":\"c\"},{\"ssid\":\"d\"}"
		","
		"{\"ssid\":\"e\"}]}",
		"{\"ssid\":\"home\"}",
		"[]",
	};

	for (size_t i = 0; i < ARRAY_SIZE(bad); i++) {
		post_wifi(bad[i]);
		zassert_equal(rsp.status, 400, "accepted %s", bad[i]);
	}
	zassert_ok(app_settings_wifi_get(0, &net));
	zassert_str_equal(net.ssid, "office", "bad requests change nothing");

	post_wifi("{\"networks\":[]}");
	zassert_equal(rsp.status, 200, "an empty list forgets every network");
	assert_body("{\"ok\":true,\"networks\":[],\"active_idx\":-1}");
}

ZTEST(api, test_admin_verbose)
{
	REQUIRE_ADMIN();

	pstop_api_admin_verbose(false, NULL, body, sizeof(body), &rsp);
	zassert_equal(rsp.status, 401);

	pstop_api_admin_verbose(false, AUTH_OK, body, sizeof(body), &rsp);
	assert_body("{\"verbose\":false}");

	pstop_api_admin_verbose(true, AUTH_OK, body, sizeof(body), &rsp);
	zassert_equal(rsp.status, 200);
	assert_body("{\"ok\":true,\"verbose\":true}");
	zassert_true(pstop_verbose_get());

	pstop_api_admin_verbose(true, AUTH_OK, body, sizeof(body), &rsp);
	assert_body("{\"ok\":true,\"verbose\":false}");
}

ZTEST(api, test_admin_disabled_without_a_password)
{
	static const char *const auths[] = {
		NULL, "", AUTH_EMPTY_PW, AUTH_OK, AUTH_UPSTREAM,
	};
	size_t size;

	if (admin_configured()) {
		ztest_test_skip();
	}

	/* 403, not 401: no credentials can succeed, so none are asked for. */
	for (size_t i = 0; i < ARRAY_SIZE(auths); i++) {
		pstop_api_role(true, "/api/role?role=operator", auths[i], body, sizeof(body), &rsp);
		zassert_equal(rsp.status, 403, "role, auth %zu", i);
		zassert_false(rsp.auth_challenge, "role, auth %zu", i);

		pstop_api_admin_status(auths[i], body, sizeof(body), &rsp);
		zassert_equal(rsp.status, 403, "status, auth %zu", i);

		pstop_api_admin_restart(auths[i], body, sizeof(body), &rsp);
		zassert_equal(rsp.status, 403, "restart, auth %zu", i);
		zassert_false(rsp.restart, "restart, auth %zu", i);

		pstop_api_health_reset("/api/health/reset?what=all&confirm=1", auths[i], body,
				       sizeof(body), &rsp);
		zassert_equal(rsp.status, 403, "health reset, auth %zu", i);

		pstop_api_coredump(auths[i], body, sizeof(body), &rsp, &size);
		zassert_equal(rsp.status, 403, "coredump, auth %zu", i);
		zassert_equal(size, 0U, "coredump, auth %zu", i);

		pstop_api_admin_page(auths[i], body, sizeof(body), &rsp);
		zassert_equal(rsp.status, 403, "page, auth %zu", i);

		pstop_api_admin_settings(false, NULL, 0U, auths[i], body, sizeof(body), &rsp);
		zassert_equal(rsp.status, 403, "settings, auth %zu", i);

		pstop_api_admin_wifi(false, NULL, 0U, auths[i], body, sizeof(body), &rsp);
		zassert_equal(rsp.status, 403, "wifi, auth %zu", i);

		pstop_api_admin_verbose(true, auths[i], body, sizeof(body), &rsp);
		zassert_equal(rsp.status, 403, "verbose, auth %zu", i);
	}
	zassert_false(pstop_verbose_get(), "never toggled");
	zassert_false(app_settings_is_operator(), "never promoted");
}

ZTEST_SUITE(api, NULL, api_setup, api_before, NULL, NULL);
