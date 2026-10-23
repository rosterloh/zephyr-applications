/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Drives the 16-LED status ring and the onboard status pixel from ring.c's
 * pure frames, one repaint every RING_FRAME_MS. Either strip may be absent
 * (native_sim has neither): the frames are still computed and the ring's
 * state changes logged, so the display logic runs on every build.
 */

#ifndef PROTECTIVE_STOP_LEDS_H
#define PROTECTIVE_STOP_LEDS_H

#include <stdbool.h>

/* Start the repaint thread. Returns 0 or -errno. */
int leds_start(void);

/* Locate mode: light only logical LED 1, white, so an installer can check
 * the ring offset. Expires on its own after RING_LOCATE_TIMEOUT_MS.
 */
void leds_locate(bool on);
bool leds_locate_active(void);

#endif /* PROTECTIVE_STOP_LEDS_H */
