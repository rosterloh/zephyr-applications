/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PRUSA_CAM_CFG_H_
#define PRUSA_CAM_CFG_H_

#include <stdbool.h>
#include <stdint.h>

#define CFG_TOKEN_MAX       64
#define CFG_PASSWORD_MAX    32
#define CFG_NAME_MAX        32 /* Prusa Connect's limit */
#define CFG_PRINTER_KEY_MAX 32

struct cfg_resolution {
	const char *name;
	uint16_t width;
	uint16_t height;
};

struct cfg {
	char token[CFG_TOKEN_MAX + 1];
	char password[CFG_PASSWORD_MAX + 1];
	char name[CFG_NAME_MAX + 1]; /* shown in Prusa Connect */
	uint16_t interval_s;         /* 10, 30, 60 or 600: the service's trigger schemes */
	uint8_t resolution;          /* index into cfg_resolutions[] */
	uint8_t quality;             /* VIDEO_CID_JPEG_COMPRESSION_QUALITY */
	bool hflip;
	bool vflip;
	bool flash;
	uint8_t rotation; /* clockwise, 0..3 = 0/90/180/270 deg, for Connect and Exif */
	/* Host-order IPv4; ip == 0 means DHCP. Applied on the next WiFi connect. */
	uint32_t ip;
	uint32_t netmask;
	uint32_t gateway;
	uint32_t dns; /* 0: use the gateway */
	/* Sensor tuning: levels -2..2, the auto loops on or off */
	int8_t brightness;
	int8_t contrast;
	int8_t saturation;
	bool auto_exposure;
	bool auto_wb;
	bool auto_gain;
};

/* PrusaLink on the printer; stored apart from struct cfg so its stored size stays. */
struct cfg_printer {
	uint32_t ip;                       /* host order; 0: no printer, upload every interval */
	char key[CFG_PRINTER_KEY_MAX + 1]; /* X-Api-Key, letters and digits */
};

#define CFG_RESOLUTION_COUNT 8
/* Append only: the stored config keeps an index. */
extern const struct cfg_resolution cfg_resolutions[CFG_RESOLUTION_COUNT];

/* Load from settings (defaults for anything unset). */
int cfg_init(void);

/* Snapshot of the current configuration. */
void cfg_get(struct cfg *out);

/* Validate and persist; -EINVAL leaves the stored config untouched. */
int cfg_set(const struct cfg *in);

void cfg_printer_get(struct cfg_printer *out);

/* Validate and persist; -EINVAL leaves the stored value untouched. */
int cfg_printer_set(const struct cfg_printer *in);

/* Erase all settings (config, WiFi networks, keys) and reboot. Returns only on error. */
int cfg_factory_reset(void);

/* Index of a resolution by name, or -1. */
int cfg_resolution_find(const char *name);

/* Stable per-device id that Prusa Connect keys the camera on. */
const char *cfg_fingerprint(void);

/* Called after cfg_set() changes anything. */
void cfg_changed(void);

#endif /* PRUSA_CAM_CFG_H_ */
