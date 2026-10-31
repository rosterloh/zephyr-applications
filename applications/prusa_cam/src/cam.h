/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PRUSA_CAM_CAM_H_
#define PRUSA_CAM_CAM_H_

#include <stddef.h>
#include <stdint.h>

#include "cfg.h"

int cam_init(void);

/* Apply resolution, quality and flips; restarts the stream. */
int cam_apply(const struct cfg *c);

/*
 * Take a fresh JPEG. On success the frame stays valid, and the camera
 * locked, until cam_release().
 */
int cam_snapshot(const uint8_t **jpeg, size_t *len);
/* As cam_snapshot(), never with the flash LED: for the MJPEG stream. */
int cam_frame(const uint8_t **jpeg, size_t *len);
void cam_release(void);

#endif /* PRUSA_CAM_CAM_H_ */
