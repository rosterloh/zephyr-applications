/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Thin HAL glue for the dual-channel stop-switch loopback. All decision logic
 * lives in estop_verdict.c; this file only drives pins and reads them back.
 */

#ifndef PROTECTIVE_STOP_ESTOP_GPIO_H
#define PROTECTIVE_STOP_ESTOP_GPIO_H

#include <stdbool.h>
#include <stdint.h>

#include "estop_verdict.h"

#define ESTOP_CHANNELS 2

/* Configure both channels' pins. Drive pins as outputs held low, sense pins as
 * inputs pulled DOWN so an open loop reads 0 = STOP. Returns 0 or -errno.
 */
int estop_gpio_init(void);

/* Drive channel `core_id`'s loop high, read the echo, drive it low, read
 * again, then hand both reads to estop_decide(). Called exactly ONCE per tick
 * per channel; the returned verdict is reused for every machine slot so all
 * sessions carry the same tick verdict.
 */
uint8_t estop_channel_sample(int core_id, estop_state_t *st);

#ifdef CONFIG_GPIO_EMUL
/* Model one pole of the DPST switch: closed conducts the driven level to the
 * sense pin, open leaves it pulled down. native_sim has no wire, so the
 * loopback has to be modelled explicitly -- there is nothing to test
 * otherwise. Not compiled on hardware.
 */
void estop_sim_set_pole(int ch, bool closed);
#endif

#endif /* PROTECTIVE_STOP_ESTOP_GPIO_H */
