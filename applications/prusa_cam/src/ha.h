/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PRUSA_CAM_HA_H_
#define PRUSA_CAM_HA_H_

/* Set the Home Assistant API encryption key from its base64 form (32 bytes) and
 * save it. -EINVAL for a malformed key, else esphome_api_set_psk()'s result.
 */
int ha_set_key(const char *b64);

/* Start publishing; after cfg_init() and connect_init(). */
void ha_start(void);

#endif /* PRUSA_CAM_HA_H_ */
