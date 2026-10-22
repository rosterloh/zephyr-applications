/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * The 16-LED status ring, as pure functions: given the link state of every
 * machine slot, compute the 16 pixels. No HAL, no clock, no globals, so every
 * colour rule is unit-tested on native_sim; leds.c only pushes the result.
 *
 * Mirrors upstream's dcs_pstop_ring.c. The ring is divided evenly among the
 * configured slots (one machine = the whole ring), in slot order from LED 1,
 * and each segment shows its machine's link state:
 *
 *   WHITE   no machine configured at all (whole ring, half brightness)
 *   AMBER   configured but no fresh reply; blinks N times, N = how deep the
 *           break is (1 = the machine is silent but our uplink is up,
 *           3 = no uplink at all)
 *   BLUE    bonded, the machine's last reply was BOND/UNBOND
 *   GREEN   the machine's last reply was OK (cleared to run)
 *   RED     the machine's last reply was STOP
 *   PURPLE  the two lockstep samplers persistently disagree; a device fault,
 *           so it overrides every segment
 *
 * OK and STOP segments carry a travelling brightness wave so a steady colour
 * still reads as alive. Locate mode (provisioning) lights only LED 1 white.
 * Frames are composed in logical order and rotated at the end, because the
 * ring can be installed in any of 16 orientations.
 */

#ifndef PROTECTIVE_STOP_RING_H
#define PROTECTIVE_STOP_RING_H

#include <stdbool.h>
#include <stdint.h>

#include "app_settings.h"

#define RING_LEDS 16

/* Per-channel level of the state colours: visible, low current and glare. */
#define RING_BRIGHTNESS 40

/* A reply older than this means the link is down (a rebond takes ~1.5 s). */
#define RING_LINK_FRESH_MS 2000U

/* One repaint every this long: fast enough for crisp amber blink edges and
 * a smooth liveness wave (one revolution ~1.1 s).
 */
#define RING_FRAME_MS 70U

/* Purple needs the mismatch counter to have advanced within each of the last
 * this-many 250 ms windows (~750 ms of continuous disagreement: a sustained
 * fault advances it every 100 ms tick). Momentary blips never flash the ring;
 * they stay visible in /state.json's pstop_mismatch. Once triggered it holds
 * this long after the last advance, so a persistent fault paints solid.
 */
#define RING_MISMATCH_WINDOW_MS      250U
#define RING_MISMATCH_PERSIST_FRAMES 3U
#define RING_MISMATCH_HOLD_MS        300U

/* Amber blink timing: N blinks, then a rest. */
#define RING_AMBER_ON_MS   160U
#define RING_AMBER_OFF_MS  200U
#define RING_AMBER_REST_MS 900U

/* Locate mode expires on its own so it cannot mask the safety colours. */
#define RING_LOCATE_TIMEOUT_MS (5U * 60U * 1000U)

struct ring_rgb {
	uint8_t r;
	uint8_t g;
	uint8_t b;
};

struct ring_slot {
	bool configured;
	uint8_t state;          /* PSTOP_SESS_* */
	uint8_t last_msg;       /* PSTOP_MESSAGE_* of the last reply */
	uint64_t last_reply_ms; /* 0 = never */
};

struct ring_input {
	uint64_t now_ms;
	struct ring_slot slots[PSTOP_MAX_MACHINES];
	bool mismatch;     /* from ring_mismatch_update() */
	bool locate;       /* locate mode active */
	bool uplink_up;    /* false: amber blinks 3, not 1 */
	uint8_t wave_head; /* liveness wave position, advanced by the caller */
	uint8_t offset;    /* physical index of logical LED 1, 0..15 */
};

/* What the ring is showing, worst first, for logging transitions. */
enum ring_state {
	RING_IDLE = 0,
	RING_UNREACHABLE,
	RING_BOND,
	RING_OK,
	RING_STOP,
	RING_MISMATCH,
	RING_LOCATE,
};

/* Compute one frame in PHYSICAL order (rotation applied). Returns the worst
 * state on the ring.
 */
enum ring_state ring_frame(const struct ring_input *in, struct ring_rgb out[RING_LEDS]);

/* Persistence filter for purple: feed it the lockstep mismatch counter on
 * every frame; it answers whether to paint the fault.
 */
struct ring_mismatch {
	uint32_t last_count;
	uint32_t streak;
	uint64_t window_start_ms;
	bool advanced; /* the counter moved in the current window */
	uint64_t until_ms;
};

void ring_mismatch_init(struct ring_mismatch *m, uint32_t count);
bool ring_mismatch_update(struct ring_mismatch *m, uint32_t count, uint64_t now_ms);

/* The onboard status pixel (upstream dcs_rgb.c): blinks the last octet of
 * the uplink's IPv4 address digit by digit (.109 = 1 blink, a long blink for
 * the 0, then 9 blinks), in a colour naming the uplink: blue Ethernet,
 * yellow WiFi. Off with no uplink. A device fault replaces it with a rapid
 * red strobe.
 */
enum status_iface {
	STATUS_IFACE_NONE = 0,
	STATUS_IFACE_ETHERNET,
	STATUS_IFACE_WIFI,
};

#define STATUS_BRIGHTNESS   48
#define STATUS_ON_MS        180U
#define STATUS_OFF_MS       220U
#define STATUS_ZERO_MS      550U /* a long blink is the digit 0 */
#define STATUS_DIGIT_GAP_MS 550U
#define STATUS_CYCLE_GAP_MS 1500U
#define STATUS_STROBE_MS    120U

/* Length of one full pattern; the caller wraps its clock at this. */
uint32_t status_cycle_ms(enum status_iface iface, uint8_t octet, bool fault);

/* The pixel at t_ms into the cycle (t_ms < status_cycle_ms()). */
struct ring_rgb status_pixel(enum status_iface iface, uint8_t octet, bool fault, uint32_t t_ms);

#endif /* PROTECTIVE_STOP_RING_H */
