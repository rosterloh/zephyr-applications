/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>  /* snprintf */
#include <stdlib.h> /* atoi */
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include "app_settings.h"

LOG_MODULE_REGISTER(app_settings, LOG_LEVEL_INF);

#define PSTOP_SETTINGS_ROOT "pstop"

static struct pstop_peer peers[PSTOP_MAX_MACHINES];
static uint32_t device_id;
static bool is_operator;

static int pstop_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	const char *next;

	if (settings_name_steq(name, "device_id", &next) && !next) {
		if (len != sizeof(device_id)) {
			return -EINVAL;
		}
		return (read_cb(cb_arg, &device_id, sizeof(device_id)) < 0) ? -EIO : 0;
	}

	if (settings_name_steq(name, "operator", &next) && !next) {
		if (len != sizeof(is_operator)) {
			return -EINVAL;
		}
		return (read_cb(cb_arg, &is_operator, sizeof(is_operator)) < 0) ? -EIO : 0;
	}

	if (settings_name_steq(name, "peers", &next) && next) {
		/* next is "<slot>" */
		int slot = atoi(next);

		if ((slot < 0) || (slot >= PSTOP_MAX_MACHINES)) {
			return -EINVAL;
		}
		if (len != sizeof(peers[slot])) {
			return -EINVAL;
		}
		return (read_cb(cb_arg, &peers[slot], sizeof(peers[slot])) < 0) ? -EIO : 0;
	}

	return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(pstop, PSTOP_SETTINGS_ROOT, NULL, pstop_settings_set, NULL, NULL);

int app_settings_init(void)
{
	int ret;

	(void)memset(peers, 0, sizeof(peers));
	device_id = 0U;
	is_operator = false;

	ret = settings_subsys_init();
	if (ret != 0) {
		LOG_ERR("settings_subsys_init: %d", ret);
		return ret;
	}

	ret = settings_load_subtree(PSTOP_SETTINGS_ROOT);
	if (ret != 0) {
		LOG_ERR("settings_load_subtree: %d", ret);
		return ret;
	}

	LOG_INF("settings loaded: device_id=%08x role=%s", device_id,
		is_operator ? "operator" : "stop_only");
	return 0;
}

const struct pstop_peer *app_settings_peer(int slot)
{
	if ((slot < 0) || (slot >= PSTOP_MAX_MACHINES)) {
		return NULL;
	}
	return &peers[slot];
}

static int save_peer(int slot)
{
	char key[32];

	(void)snprintf(key, sizeof(key), PSTOP_SETTINGS_ROOT "/peers/%d", slot);
	return settings_save_one(key, &peers[slot], sizeof(peers[slot]));
}

int app_settings_set_peer(int slot, uint32_t ip, uint16_t port, uint32_t id)
{
	if ((slot < 0) || (slot >= PSTOP_MAX_MACHINES)) {
		return -EINVAL;
	}

	peers[slot].ip = ip;
	peers[slot].port = port;
	peers[slot].id = id;
	peers[slot].configured = true;

	return save_peer(slot);
}

int app_settings_clear_peer(int slot)
{
	if ((slot < 0) || (slot >= PSTOP_MAX_MACHINES)) {
		return -EINVAL;
	}

	(void)memset(&peers[slot], 0, sizeof(peers[slot]));
	return save_peer(slot);
}

uint32_t app_settings_device_id(void)
{
	return device_id;
}

int app_settings_set_device_id(uint32_t id)
{
	device_id = id;
	return settings_save_one(PSTOP_SETTINGS_ROOT "/device_id", &device_id, sizeof(device_id));
}

bool app_settings_is_operator(void)
{
	return is_operator;
}

int app_settings_set_operator(bool op)
{
	is_operator = op;
	return settings_save_one(PSTOP_SETTINGS_ROOT "/operator", &is_operator,
				 sizeof(is_operator));
}
