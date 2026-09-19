/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * One pstop session: the socket, bond state machine, counters and reply-loss
 * watchdog for a single machine slot. One socket per machine keeps the reply
 * path trivially demultiplexed and stops a dead machine from stalling the
 * heartbeats to the others.
 *
 * Build/commit split: the two lockstep samplers both call
 * pstop_session_build(), which must be side-effect free so their encodings are
 * byte-identical. Only the comparator calls pstop_session_commit_send(), which
 * advances the counter.
 */

#ifndef PROTECTIVE_STOP_SESSION_H
#define PROTECTIVE_STOP_SESSION_H

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/net/socket.h>

#include "app_settings.h"
#include "pstop/protocol_data.h"
#include "pstop/pstop_msg.h"

/* Local UDP source ports: slot i binds PSTOP_LOCAL_PORT + i. */
#define PSTOP_LOCAL_PORT 8891

/* Transmit every heartbeat_timeout/2, clamped to this window. The lower bound
 * is the 10 Hz lockstep tick: sampling is a safety property and never slows.
 */
#define PSTOP_MIN_SEND_PERIOD_MS 100U
#define PSTOP_MAX_SEND_PERIOD_MS 1000U

/* One BOND in flight at a time. Long spacing matters because the protocol
 * rejects duplicate counters with MSG_LOST once a client is registered.
 */
#define PSTOP_BOND_RETRY_MS 5000U

/* Reply-loss watchdog floor, covering the pre-first-reply state. The real
 * threshold is derived per session so it always outlasts the machine's own
 * bond-drop timeout -- see pstop_session_rebond_after_ms().
 */
#define PSTOP_REBOND_FLOOR_MS 2500U

/* The machine's missed-heartbeat multiplier is NOT on the wire (replies carry
 * only heartbeat_timeout), so this is a hard-coded coupling to machn's
 * MACHN_MAX_MISSED_HEARTBEATS. If that changes upstream, this must change too.
 */
#define PSTOP_REBOND_MACHINE_MAX_MISSED 5U
#define PSTOP_REBOND_JITTER_MARGIN_MS   500U

enum pstop_sess_state {
	PSTOP_SESS_IDLE = 0,
	PSTOP_SESS_BONDING = 1,
	PSTOP_SESS_BONDED = 2,
};

struct pstop_session {
	int slot;
	int sock;
	struct sockaddr_in peer_addr;
	uint32_t peer_id;

	protocol_data_t proto; /* pstop_c's per-peer counters and stamps */
	enum pstop_sess_state state;

	uint32_t hb_ms; /* heartbeat window advertised by the machine */

	uint64_t last_send_ms;
	uint64_t last_reply_ms;
	uint64_t last_bond_ms;

	/* Telemetry, surfaced by /state.json in slice 3. */
	uint32_t sent;
	uint32_t replies;
	uint32_t send_fail;
	uint32_t rebonds;
	uint8_t last_msg;
};

/* Bind a socket on PSTOP_LOCAL_PORT + slot and point the session at peer.
 * Returns 0 or -errno. Safe to call on a zeroed struct.
 */
int pstop_session_open(struct pstop_session *s, int slot, const struct pstop_peer *peer);

void pstop_session_close(struct pstop_session *s);

/* heartbeat_timeout/2, clamped. Before the first reply hb_ms is 0 and this
 * returns the floor -- transmit fast rather than guess slow.
 */
uint32_t pstop_session_send_period_ms(const struct pstop_session *s);

/* How long without a reply before this session re-bonds. Always greater than
 * the machine's own hb_ms * max_missed bond-drop timeout. hb_ms is adopted
 * unclamped from the wire, so this widens to uint64_t rather than clamping it
 * -- a 32-bit accumulator would wrap and could undercut the machine's own
 * timeout.
 */
uint64_t pstop_session_rebond_after_ms(const struct pstop_session *s);

/* Is this session due to transmit on this tick? */
bool pstop_session_due(const struct pstop_session *s, uint64_t now_ms);

/* Encode this tick's 48-byte frame into out48. SIDE-EFFECT FREE: calling it
 * twice with the same arguments must produce identical bytes, because both
 * lockstep samplers call it and the comparator byte-compares the results.
 */
void pstop_session_build(struct pstop_session *s, uint8_t verdict, uint64_t now_ms, uint8_t *out48);

/* Transmit a frame the comparator has already validated, then advance the
 * counter. Counter advances ONLY here, so the machine sees contiguous
 * counters despite send decimation. Returns 0 or -errno.
 */
int pstop_session_commit_send(struct pstop_session *s, const uint8_t *buf48, uint64_t now_ms);

/* Drain any pending replies (non-blocking) and adopt the machine's advertised
 * heartbeat window.
 */
void pstop_session_poll(struct pstop_session *s, uint64_t now_ms);

/* Re-bond if the machine has gone quiet for longer than the derived window. */
void pstop_session_watchdog(struct pstop_session *s, uint64_t now_ms);

#endif /* PROTECTIVE_STOP_SESSION_H */
