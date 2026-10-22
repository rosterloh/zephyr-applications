/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>  /* snprintf */
#include <stdlib.h> /* atoi */
#include <string.h>

#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include "app_settings.h"
#include "pstop_log.h"

LOG_MODULE_REGISTER(app_settings, PSTOP_LOG_LEVEL);

#define PSTOP_SETTINGS_ROOT "pstop"

/* Upstream's family byte for remote ids derived from the MAC. */
#define DERIVED_ID_PREFIX 0x01000000U
#define FALLBACK_ID       0xC0A80105U /* 192.168.1.5, slice 1-3's fixed id */

/* Guards peers[]: the HTTP thread writes slots while the comparator reads
 * them. device_id and is_operator are single words, read whole.
 */
static K_MUTEX_DEFINE(peers_lock);
static struct pstop_peer peers[PSTOP_MAX_MACHINES];
static uint32_t device_id;
static uint32_t derived_id;
static bool is_operator;
static uint8_t ring_offset;

static K_MUTEX_DEFINE(wifi_lock);
static struct pstop_wifi_net wifi[PSTOP_WIFI_MAX];

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

	if (settings_name_steq(name, "ring_off", &next) && !next) {
		if (len != sizeof(ring_offset)) {
			return -EINVAL;
		}
		return (read_cb(cb_arg, &ring_offset, sizeof(ring_offset)) < 0) ? -EIO : 0;
	}

	if (settings_name_steq(name, "wifi", &next) && next) {
		int idx = atoi(next);

		if ((idx < 0) || (idx >= PSTOP_WIFI_MAX) || (len != sizeof(wifi[idx]))) {
			return -EINVAL;
		}
		if (read_cb(cb_arg, &wifi[idx], sizeof(wifi[idx])) < 0) {
			return -EIO;
		}
		wifi[idx].ssid[PSTOP_WIFI_SSID_MAX] = '\0';
		wifi[idx].pass[PSTOP_WIFI_PASS_MAX] = '\0';
		return 0;
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

static uint32_t derive_device_id(void)
{
#ifdef CONFIG_HWINFO
	uint8_t mac[6];
	ssize_t n = hwinfo_get_device_id(mac, sizeof(mac));

	if (n >= 3) {
		return DERIVED_ID_PREFIX | ((uint32_t)mac[n - 3] << 16) |
		       ((uint32_t)mac[n - 2] << 8) | (uint32_t)mac[n - 1];
	}
#endif
	return FALLBACK_ID;
}

int app_settings_init(void)
{
	int ret;

	(void)memset(peers, 0, sizeof(peers));
	(void)memset(wifi, 0, sizeof(wifi));
	device_id = 0U;
	is_operator = false;
	ring_offset = 0U;
	derived_id = derive_device_id();

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

	if (ring_offset >= 16U) {
		ring_offset = 0U;
	}

	LOG_INF("settings loaded: device_id=%08x (%s) role=%s", app_settings_effective_device_id(),
		(device_id != 0U) ? "pinned" : "derived", is_operator ? "operator" : "stop_only");
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

uint32_t app_settings_effective_device_id(void)
{
	uint32_t id = device_id;

	return (id != 0U) ? id : derived_id;
}

uint8_t app_settings_ring_offset(void)
{
	return ring_offset;
}

int app_settings_set_ring_offset(uint8_t offset)
{
	int ret;

	if (offset >= 16U) {
		return -EINVAL;
	}

	ret = settings_save_one(PSTOP_SETTINGS_ROOT "/ring_off", &offset, sizeof(offset));
	if (ret != 0) {
		return ret;
	}

	ring_offset = offset;
	return 0;
}

int app_settings_wifi_get(int idx, struct pstop_wifi_net *out)
{
	if ((idx < 0) || (idx >= PSTOP_WIFI_MAX) || (out == NULL)) {
		return -EINVAL;
	}

	(void)k_mutex_lock(&wifi_lock, K_FOREVER);
	*out = wifi[idx];
	(void)k_mutex_unlock(&wifi_lock);
	return 0;
}

int app_settings_wifi_set_all(const struct pstop_wifi_net nets[PSTOP_WIFI_MAX])
{
	char key[32];
	int ret = 0;

	(void)k_mutex_lock(&wifi_lock, K_FOREVER);
	for (int i = 0; i < PSTOP_WIFI_MAX; i++) {
		(void)snprintf(key, sizeof(key), PSTOP_SETTINGS_ROOT "/wifi/%d", i);
		ret = settings_save_one(key, &nets[i], sizeof(nets[i]));
		if (ret != 0) {
			break;
		}
		wifi[i] = nets[i];
	}
	(void)k_mutex_unlock(&wifi_lock);
	return ret;
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
