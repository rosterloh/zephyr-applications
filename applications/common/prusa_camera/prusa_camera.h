/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Client for the Prusa camera service API v1
 * (https://camera-service.prusa3d.com/docs/): describe the camera with
 * PUT /v1/camera-info, then upload JPEGs with PUT /v1/snapshot. Calls block
 * for up to CONFIG_PRUSA_CAMERA_TIMEOUT_MS and must not run concurrently.
 */

#ifndef APP_COMMON_PRUSA_CAMERA_H_
#define APP_COMMON_PRUSA_CAMERA_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PRUSA_CAMERA_TOKEN_LEN       20
#define PRUSA_CAMERA_NAME_MAX        32
#define PRUSA_CAMERA_FINGERPRINT_MIN 16
#define PRUSA_CAMERA_FINGERPRINT_MAX 64
#define PRUSA_CAMERA_SNAPSHOT_MAX    1000000

/* Upload cadences the service knows for non-LINK cameras. */
enum prusa_camera_trigger {
	PRUSA_CAMERA_TEN_SEC,
	PRUSA_CAMERA_THIRTY_SEC,
	PRUSA_CAMERA_SIXTY_SEC,
	PRUSA_CAMERA_TEN_MIN,
};

struct prusa_camera_auth {
	const char *token;       /* 20 alphanumerics from Prusa Connect */
	const char *fingerprint; /* 16..64 characters, stable for the device */
};

/* NULL or "" leaves an optional string out; width == 0 leaves out resolution. */
struct prusa_camera_info {
	const char *name; /* required, 1..32 */
	enum prusa_camera_trigger trigger;
	uint16_t rotation; /* degrees, as mounted */
	uint16_t width;
	uint16_t height;
	const char *wifi_mac;
	const char *wifi_ipv4;
	const char *wifi_ssid;
	const char *type;             /* required: "ESP32", ... */
	const char *firmware_version; /* required, 1..16 */
	const char *hardware_version; /* required, 1..32 */
	const char *manufacturer;
	const char *model;
};

/* What a caller should do after a result (an HTTP status or -errno). */
enum prusa_camera_next {
	PRUSA_CAMERA_NEXT_OK,
	PRUSA_CAMERA_NEXT_RETRY, /* network or server trouble: back off and retry */
	PRUSA_CAMERA_NEXT_WAIT,  /* rate limited: skip a cycle */
	PRUSA_CAMERA_NEXT_STOP,  /* the request is wrong: retrying it cannot help */
};

/*
 * Install the service's CA certificate. `user_agent` names the firmware and
 * its version ("prusa_cam/0.1.0"); the service requires it on every call.
 */
int prusa_camera_init(const char *user_agent);

/* Each returns the HTTP status, or -errno for a local or network failure. */
int prusa_camera_get_info(const struct prusa_camera_auth *auth);
int prusa_camera_put_info(const struct prusa_camera_auth *auth,
			  const struct prusa_camera_info *info);
int prusa_camera_put_snapshot(const struct prusa_camera_auth *auth, const uint8_t *jpeg,
			      size_t len);

/* Network-free helpers. */
int prusa_camera_info_encode(const struct prusa_camera_info *info, char *buf, size_t len);
enum prusa_camera_next prusa_camera_next(int result);
const char *prusa_camera_describe(int result);
bool prusa_camera_token_valid(const char *token);
uint32_t prusa_camera_trigger_seconds(enum prusa_camera_trigger t);
/* The trigger for an interval in seconds, or -EINVAL if there is none. */
int prusa_camera_trigger_from_seconds(uint32_t seconds);

#endif /* APP_COMMON_PRUSA_CAMERA_H_ */
