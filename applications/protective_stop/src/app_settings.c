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

/* Guards peers[]: the HTTP thread writes slots while the comparator reads
 * them. device_id and is_operator are single words, read whole.
 */
static K_MUTEX_DEFINE(peers_lock);
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

int app_settings_peer_get(int slot, struct pstop_peer *out)
{
	if ((slot < 0) || (slot >= PSTOP_MAX_MACHINES) || (out == NULL)) {
		return -EINVAL;
	}

	(void)k_mutex_lock(&peers_lock, K_FOREVER);
	*out = peers[slot];
	(void)k_mutex_unlock(&peers_lock);
	return 0;
}

static int save_peer(int slot, const struct pstop_peer *peer)
{
	char key[32];

	(void)snprintf(key, sizeof(key), PSTOP_SETTINGS_ROOT "/peers/%d", slot);
	return settings_save_one(key, peer, sizeof(*peer));
}

int app_settings_set_peer(int slot, uint32_t ip, uint16_t port, uint32_t id)
{
	struct pstop_peer peer;
	int ret;

	if ((slot < 0) || (slot >= PSTOP_MAX_MACHINES)) {
		return -EINVAL;
	}

	(void)memset(&peer, 0, sizeof(peer));
	peer.ip = ip;
	peer.port = port;
	peer.id = id;
	peer.configured = true;

	(void)k_mutex_lock(&peers_lock, K_FOREVER);
	ret = save_peer(slot, &peer);
	if (ret == 0) {
		peers[slot] = peer;
	}
	(void)k_mutex_unlock(&peers_lock);
	return ret;
}

int app_settings_clear_peer(int slot)
{
	struct pstop_peer peer;
	int ret;

	if ((slot < 0) || (slot >= PSTOP_MAX_MACHINES)) {
		return -EINVAL;
	}

	(void)memset(&peer, 0, sizeof(peer));

	(void)k_mutex_lock(&peers_lock, K_FOREVER);
	ret = save_peer(slot, &peer);
	if (ret == 0) {
		peers[slot] = peer;
	}
	(void)k_mutex_unlock(&peers_lock);
	return ret;
}

uint32_t app_settings_device_id(void)
{
	return device_id;
}

int app_settings_set_device_id(uint32_t id)
{
	int ret;

	ret = settings_save_one(PSTOP_SETTINGS_ROOT "/device_id", &id, sizeof(id));
	if (ret != 0) {
		return ret;
	}

	device_id = id;
	return 0;
}

bool app_settings_is_operator(void)
{
	return is_operator;
}

int app_settings_set_operator(bool op)
{
	int ret;

	ret = settings_save_one(PSTOP_SETTINGS_ROOT "/operator", &op, sizeof(op));
	if (ret != 0) {
		return ret;
	}

	is_operator = op;
	return 0;
}
