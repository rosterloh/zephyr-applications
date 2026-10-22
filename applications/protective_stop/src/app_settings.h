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

/* Self-claimed role, announced in every pstop frame and re-read by the
 * machine on every frame, so a change applies live. Defaults to stop-only.
 * Since upstream #130 the machine has no operator list: this claim alone
 * decides whether the remote may re-arm. Admission (may it bond at all) is
 * the machine's allow/denylist.
 */
bool app_settings_is_operator(void);
int app_settings_set_operator(bool op);

#endif /* PROTECTIVE_STOP_APP_SETTINGS_H */
