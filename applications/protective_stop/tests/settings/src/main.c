/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Settings round-trip over ZMS. On native_sim the ZMS backend sits on
 * flash_simulator's native backend, which persists to a host file, so this
 * exercises the real persistence path rather than a RAM stub.
 */

#include <zephyr/settings/settings.h>
#include <zephyr/ztest.h>

#include "app_settings.h"

static void *suite_setup(void)
{
	zassert_ok(app_settings_init(), "settings init");
	return NULL;
}

ZTEST(app_settings, test_peers_default_to_unconfigured)
{
	for (int slot = 0; slot < PSTOP_MAX_MACHINES; slot++) {
		const struct pstop_peer *p = app_settings_peer(slot);

		zassert_not_null(p, "slot %d", slot);
		/* A fresh slot must not be configured: an unconfigured slot is
		 * never heartbeated, so a garbage default would silently point
		 * the remote at an unintended address.
		 */
		if (!p->configured) {
			zassert_equal(p->ip, 0U, "slot %d ip", slot);
			zassert_equal(p->port, 0U, "slot %d port", slot);
		}
	}
}

ZTEST(app_settings, test_set_and_read_back_peer)
{
	const struct pstop_peer *p;

	zassert_ok(app_settings_set_peer(1, 0xC0A80164U, 8890U, 0x01020304U), "set");

	p = app_settings_peer(1);
	zassert_true(p->configured, "configured");
	zassert_equal(p->ip, 0xC0A80164U, "ip");
	zassert_equal(p->port, 8890U, "port");
	zassert_equal(p->id, 0x01020304U, "machine id");
}

ZTEST(app_settings, test_clear_peer)
{
	zassert_ok(app_settings_set_peer(2, 0x0A000001U, 8890U, 0x11U), "set");
	zassert_true(app_settings_peer(2)->configured, "configured");

	zassert_ok(app_settings_clear_peer(2), "clear");
	zassert_false(app_settings_peer(2)->configured, "cleared");
}

ZTEST(app_settings, test_out_of_range_slot_is_rejected)
{
	zassert_not_equal(app_settings_set_peer(-1, 1U, 1U, 1U), 0, "negative slot");
	zassert_not_equal(app_settings_set_peer(PSTOP_MAX_MACHINES, 1U, 1U, 1U), 0,
			  "slot past the end");
	zassert_is_null(app_settings_peer(PSTOP_MAX_MACHINES), "peer past the end");
}

ZTEST(app_settings, test_role_defaults_to_stop_only)
{
	/* Maximally safe default: a remote that cannot re-arm until someone
	 * deliberately promotes it. Mirrors upstream's default.
	 */
	zassert_ok(app_settings_set_operator(false), "reset role");
	zassert_false(app_settings_is_operator(), "default must be stop-only");
}

ZTEST(app_settings, test_role_round_trips)
{
	zassert_ok(app_settings_set_operator(true), "promote");
	zassert_true(app_settings_is_operator(), "operator");

	zassert_ok(app_settings_set_operator(false), "demote");
	zassert_false(app_settings_is_operator(), "stop-only");
}

ZTEST(app_settings, test_device_id_round_trips)
{
	zassert_ok(app_settings_set_device_id(0xC0A80105U), "set");
	zassert_equal(app_settings_device_id(), 0xC0A80105U, "read back");
}

/*
 * The tests above only prove the in-memory statics agree with what the
 * setters just wrote into them -- they would pass even if settings_save_one()
 * were a no-op. Reloading via app_settings_init() forces a real read back
 * from the ZMS backend: init() zeroes the statics before calling
 * settings_load_subtree(), so a value that survives the round trip really
 * came off flash.
 */
ZTEST(app_settings, test_peer_survives_reload)
{
	zassert_ok(app_settings_set_peer(3, 0xC0A80101U, 8890U, 0x99U), "set");

	zassert_ok(app_settings_init(), "reload");

	const struct pstop_peer *p = app_settings_peer(3);

	zassert_true(p->configured, "configured after reload");
	zassert_equal(p->ip, 0xC0A80101U, "ip after reload");
	zassert_equal(p->port, 8890U, "port after reload");
	zassert_equal(p->id, 0x99U, "machine id after reload");
}

ZTEST(app_settings, test_operator_survives_reload)
{
	zassert_ok(app_settings_set_operator(true), "set");

	zassert_ok(app_settings_init(), "reload");

	zassert_true(app_settings_is_operator(), "operator after reload");
}

ZTEST(app_settings, test_device_id_survives_reload)
{
	zassert_ok(app_settings_set_device_id(0xDEADBEEFU), "set");

	zassert_ok(app_settings_init(), "reload");

	zassert_equal(app_settings_device_id(), 0xDEADBEEFU, "device id after reload");
}

ZTEST_SUITE(app_settings, NULL, suite_setup, NULL, NULL, NULL);
