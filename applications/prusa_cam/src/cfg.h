/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PRUSA_CAM_CFG_H_
#define PRUSA_CAM_CFG_H_

#include <stdbool.h>
#include <stdint.h>

#define CFG_TOKEN_MAX    64
#define CFG_PASSWORD_MAX 32

struct cfg_resolution {
	const char *name;
	uint16_t width;
	uint16_t height;
};

struct cfg {
	char token[CFG_TOKEN_MAX + 1];
	char password[CFG_PASSWORD_MAX + 1];
	uint16_t interval_s;
	uint8_t resolution; /* index into cfg_resolutions[] */
	uint8_t quality;    /* VIDEO_CID_JPEG_COMPRESSION_QUALITY */
	bool hflip;
	bool vflip;
	bool flash;
};

extern const struct cfg_resolution cfg_resolutions[];
extern const uint8_t cfg_resolution_count;

#define CFG_INTERVAL_MIN 10
#define CFG_INTERVAL_MAX 3600

/* Load from settings (defaults for anything unset). */
int cfg_init(void);

/* Snapshot of the current configuration. */
void cfg_get(struct cfg *out);

/* Validate and persist; -EINVAL leaves the stored config untouched. */
int cfg_set(const struct cfg *in);

/* Index of a resolution by name, or -1. */
int cfg_resolution_find(const char *name);

/* Stable per-device id that Prusa Connect keys the camera on. */
const char *cfg_fingerprint(void);

/* Called after cfg_set() changes anything. */
void cfg_changed(void);

#endif /* PRUSA_CAM_CFG_H_ */
