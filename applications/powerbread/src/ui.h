/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_UI_H_
#define APP_UI_H_

#include <stdint.h>

enum pb_mode {
	PB_MODE_DASH,
	PB_MODE_CHART,
	PB_MODE_STATS,
	PB_MODE_COUNT,
};

int pb_ui_init(void);
void pb_ui_set_mode(enum pb_mode mode);
enum pb_mode pb_ui_get_mode(void);
void pb_ui_step_mode(int dir);
void pb_ui_set_channel(uint8_t ch);
uint8_t pb_ui_get_channel(void);

#endif /* APP_UI_H_ */
