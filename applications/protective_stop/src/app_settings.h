/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Persistent configuration: the machine peer table, this remote's role, and
 * its device id.
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

/* Read a peer slot. Returns NULL if slot is out of range. The returned
 * pointer stays valid for the process lifetime.
 */
const struct pstop_peer *app_settings_peer(int slot);

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

/* Self-claimed role, announced in every pstop frame. Defaults to stop-only:
 * the machine ANDs this claim with its own operator allowlist, so claiming
 * operator is necessary but not sufficient to re-arm.
 */
bool app_settings_is_operator(void);
int app_settings_set_operator(bool op);

#endif /* PROTECTIVE_STOP_APP_SETTINGS_H */
