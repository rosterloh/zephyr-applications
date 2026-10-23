/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Persistent configuration: the machine peer table, this remote's role, its
 * device id, the LED-ring orientation and the WiFi networks.
 *
 * Settings-over-ZMS rather than raw ZMS: raw ZMS is a flat uint32 id -> bytes
 * store, so using it directly would mean inventing an id map and keeping it
 * stable across firmware versions forever. Settings gives string keys and
 * allocates ids itself. Upstream's NVS blobs are string-keyed too, so the
 * persistence model stays recognisable.
 */

#ifndef PROTECTIVE_STOP_APP_SETTINGS_H
#define PROTECTIVE_STOP_APP_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

/* One remote heartbeats up to this many machines independently. */
#define PSTOP_MAX_MACHINES 4

struct pstop_peer {
	uint32_t ip;   /* IPv4, host byte order */
	uint16_t port; /* machine's UDP port */
	uint32_t id;   /* the machine's pstop device id */
	bool configured;
};

/* Initialise the settings subsystem and load everything under "pstop/".
 * Returns 0 or -errno.
 */
int app_settings_init(void);

/* Copy a peer slot out. Returns 0, or -EINVAL for a slot out of range. Safe
 * from any thread: the HTTP API rewrites slots while the comparator reads
 * them, so callers get a consistent copy, never a pointer into the table.
 */
int app_settings_peer_get(int slot, struct pstop_peer *out);

/* Set and persist a peer slot. Returns 0 or -errno (-EINVAL for a bad slot). */
int app_settings_set_peer(int slot, uint32_t ip, uint16_t port, uint32_t id);

/* Empty and persist a peer slot. */
int app_settings_clear_peer(int slot);

/* This remote's pstop device id. Zero means "derive from the active uplink";
 * a non-zero stored value pins it. Pinning matters because the machine's
 * operator allowlist is keyed on this value, so an id that moves with DHCP
 * silently demotes the remote to stop-only.
 */
uint32_t app_settings_device_id(void);
int app_settings_set_device_id(uint32_t id);

/* The id this remote announces: the stored one if pinned, otherwise derived
 * from the chip's factory MAC like upstream's (0x01 then the MAC's low 24
 * bits), so it is stable across DHCP leases, reflashes and uplinks. Without a
 * hardware id source it falls back to 0xC0A80105.
 */
uint32_t app_settings_effective_device_id(void);

/* LED-ring rotation: which physical pixel is logical LED 1, 0..15. */
uint8_t app_settings_ring_offset(void);
int app_settings_set_ring_offset(uint8_t offset);

/* Stored WiFi networks, tried in order. An empty ssid is an empty entry. */
#define PSTOP_WIFI_MAX      4
#define PSTOP_WIFI_SSID_MAX 32
#define PSTOP_WIFI_PASS_MAX 64

struct pstop_wifi_net {
	char ssid[PSTOP_WIFI_SSID_MAX + 1];
	char pass[PSTOP_WIFI_PASS_MAX + 1];
};

int app_settings_wifi_get(int idx, struct pstop_wifi_net *out);

/* Replace the whole list. Returns 0 or -errno. */
int app_settings_wifi_set_all(const struct pstop_wifi_net nets[PSTOP_WIFI_MAX]);

/* Self-claimed role, announced in every pstop frame and re-read by the
 * machine on every frame, so a change applies live. Defaults to stop-only.
 * Since upstream #130 the machine has no operator list: this claim alone
 * decides whether the remote may re-arm. Admission (may it bond at all) is
 * the machine's allow/denylist.
 */
bool app_settings_is_operator(void);
int app_settings_set_operator(bool op);

#endif /* PROTECTIVE_STOP_APP_SETTINGS_H */
