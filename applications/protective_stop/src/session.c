/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/byteorder.h>

#include "pstop_aux_channel.h"
#include "session.h"

LOG_MODULE_REGISTER(pstop_session, LOG_LEVEL_INF);

int pstop_session_open(struct pstop_session *s, int slot, const struct pstop_peer *peer)
{
	struct sockaddr_in local;
	int sock;
	int ret;

	if (s != NULL) {
		s->sock = -1;
	}

	if ((s == NULL) || (peer == NULL) || (slot < 0) || (slot >= PSTOP_MAX_MACHINES)) {
		return -EINVAL;
	}

	sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) {
		LOG_ERR("slot %d socket: %d", slot, errno);
		return -errno;
	}

	(void)memset(&local, 0, sizeof(local));
	local.sin_family = AF_INET;
	local.sin_port = htons((uint16_t)(PSTOP_LOCAL_PORT + slot));
	local.sin_addr.s_addr = htonl(INADDR_ANY);

	ret = zsock_bind(sock, (struct sockaddr *)&local, sizeof(local));
	if (ret < 0) {
		LOG_ERR("slot %d bind %u: %d", slot, PSTOP_LOCAL_PORT + slot, errno);
		(void)zsock_close(sock);
		return -errno;
	}

	s->slot = slot;
	s->sock = sock;
	s->peer_id = peer->id;
	s->state = PSTOP_SESS_IDLE;
	s->hb_ms = 0U;
	s->last_send_ms = 0U;
	s->last_reply_ms = 0U;
	s->last_bond_ms = 0U;
	s->sent = 0U;
	s->replies = 0U;
	s->send_fail = 0U;
	s->rebonds = 0U;
	s->last_msg = PSTOP_MESSAGE_UNKNOWN;

	(void)memset(&s->peer_addr, 0, sizeof(s->peer_addr));
	s->peer_addr.sin_family = AF_INET;
	s->peer_addr.sin_port = htons(peer->port);
	s->peer_addr.sin_addr.s_addr = htonl(peer->ip);

	protocol_data_init(&s->proto);
	device_id_set(&s->proto.remote_id, peer->id);

	LOG_INF("slot %d bound :%u -> machine %08x", slot, PSTOP_LOCAL_PORT + slot, peer->id);
	return 0;
}

void pstop_session_close(struct pstop_session *s)
{
	if ((s != NULL) && (s->sock >= 0)) {
		(void)zsock_close(s->sock);
		s->sock = -1;
		s->state = PSTOP_SESS_IDLE;
	}
}

uint32_t pstop_session_send_period_ms(const struct pstop_session *s)
{
	uint32_t period;

	/* No reply yet: transmit at the fast end rather than guessing slow. */
	if (s->hb_ms == 0U) {
		return PSTOP_MIN_SEND_PERIOD_MS;
	}

	/* Half the machine's window gives 2x margin, so ordinary jitter can
	 * never cost the machine a whole heartbeat.
	 */
	period = s->hb_ms / 2U;

	if (period < PSTOP_MIN_SEND_PERIOD_MS) {
		period = PSTOP_MIN_SEND_PERIOD_MS;
	}
	if (period > PSTOP_MAX_SEND_PERIOD_MS) {
		period = PSTOP_MAX_SEND_PERIOD_MS;
	}
	return period;
}

uint64_t pstop_session_rebond_after_ms(const struct pstop_session *s)
{
	uint64_t derived = ((uint64_t)s->hb_ms * (uint64_t)PSTOP_REBOND_MACHINE_MAX_MISSED) +
			   PSTOP_REBOND_JITTER_MARGIN_MS;

	return (derived < PSTOP_REBOND_FLOOR_MS) ? PSTOP_REBOND_FLOOR_MS : derived;
}

bool pstop_session_due(const struct pstop_session *s, uint64_t now_ms)
{
	if (s->state == PSTOP_SESS_BONDING) {
		return (now_ms - s->last_bond_ms) >= PSTOP_BOND_RETRY_MS;
	}
	if (s->state == PSTOP_SESS_IDLE) {
		return true;
	}
	return (now_ms - s->last_send_ms) >= pstop_session_send_period_ms(s);
}

void pstop_session_build(struct pstop_session *s, uint8_t verdict, uint64_t now_ms, uint8_t *out48)
{
	pstop_msg_t msg;
	device_id_t me;
	uint8_t type;
	pstop_aux_role_t role;

	device_id_set(&me, app_settings_device_id());

	/* An unbonded session must bond before it can heartbeat. */
	type = (s->state == PSTOP_SESS_BONDED) ? verdict : PSTOP_MESSAGE_BOND;

	if (type == PSTOP_MESSAGE_BOND) {
		/* A bond establishes the counter baseline, so it carries no
		 * received stamp or counter.
		 */
		pstop_create_bond_message(&msg, now_ms, &me, &s->proto.remote_id,
					  s->proto.msg_counter + 1U);
	} else {
		pstop_create_generic_message(&msg, type, now_ms, s->proto.last_received_stamp, &me,
					     &s->proto.remote_id, s->proto.msg_counter + 1U,
					     s->proto.last_received_counter);
	}

	/* The role is latched by the machine at bond time, so it must be
	 * carried on the BOND frame too -- there is no later chance to
	 * correct it.
	 */
	role = app_settings_is_operator() ? PSTOP_AUX_ROLE_OPERATOR : PSTOP_AUX_ROLE_STOP_ONLY;
	pstop_aux_encode_role(&msg, role);

	pstop_message_encode(&msg, out48);
}

int pstop_session_commit_send(struct pstop_session *s, const uint8_t *buf48, uint64_t now_ms)
{
	ssize_t n;

	n = zsock_sendto(s->sock, buf48, PSTOP_MESSAGE_SIZE, 0, (struct sockaddr *)&s->peer_addr,
			 sizeof(s->peer_addr));
	if (n != (ssize_t)PSTOP_MESSAGE_SIZE) {
		s->send_fail++;
		LOG_WRN("slot %d send failed: %d", s->slot, errno);
		return -errno;
	}

	/* Counter advances ONLY on a transmitting tick, so the machine sees
	 * contiguous counters despite send decimation. protocol.c rejects gaps
	 * larger than max_lost_messages + 1.
	 */
	s->proto.msg_counter++;
	s->last_send_ms = now_ms;
	s->sent++;

	if (s->state == PSTOP_SESS_IDLE) {
		s->state = PSTOP_SESS_BONDING;
		s->last_bond_ms = now_ms;
	} else if (s->state == PSTOP_SESS_BONDING) {
		s->last_bond_ms = now_ms;
	} else {
		/* bonded; nothing further */
	}

	return 0;
}

void pstop_session_poll(struct pstop_session *s, uint64_t now_ms)
{
	uint8_t buf[PSTOP_MESSAGE_SIZE];
	pstop_msg_t msg;
	struct sockaddr_in from;
	socklen_t from_len;
	ssize_t n;

	for (;;) {
		from_len = sizeof(from);
		n = zsock_recvfrom(s->sock, buf, sizeof(buf), ZSOCK_MSG_DONTWAIT,
				   (struct sockaddr *)&from, &from_len);
		if (n != (ssize_t)PSTOP_MESSAGE_SIZE) {
			return;
		}

		/* A stray sender must not be able to feed counters into a
		 * safety session: only the bonded peer's address is trusted.
		 */
		if ((from.sin_addr.s_addr != s->peer_addr.sin_addr.s_addr) ||
		    (from.sin_port != s->peer_addr.sin_port)) {
			LOG_WRN("slot %d reply from unexpected source", s->slot);
			continue;
		}

		pstop_message_decode(&msg, buf);

		/* A frame whose checksum does not verify tells us nothing; drop
		 * it rather than adopting its counters.
		 */
		if (msg.checksum != msg.calculated_checksum) {
			LOG_WRN("slot %d bad checksum", s->slot);
			continue;
		}

		s->proto.last_received_counter = msg.counter;
		s->proto.last_received_stamp = msg.stamp;
		s->last_reply_ms = now_ms;
		s->last_msg = msg.message;
		s->replies++;

		/* The machine governs the rate: it advertises its per-operator
		 * window in every reply, including the bond ack. A reply
		 * carrying 0 (a reject path) changes nothing.
		 */
		if (msg.heartbeat_timeout != 0U) {
			s->hb_ms = msg.heartbeat_timeout;
		}

		if (s->state != PSTOP_SESS_BONDED) {
			LOG_INF("slot %d bonded (hb=%u ms)", s->slot, s->hb_ms);
			s->state = PSTOP_SESS_BONDED;
		}
	}
}

void pstop_session_watchdog(struct pstop_session *s, uint64_t now_ms)
{
	if (s->state != PSTOP_SESS_BONDED) {
		return;
	}

	if ((now_ms - s->last_reply_ms) < pstop_session_rebond_after_ms(s)) {
		return;
	}

	/* The link has desynced and will not recover on its own. Re-bond this
	 * session only; the others are untouched.
	 */
	LOG_WRN("slot %d silent for %u ms, re-bonding", s->slot,
		(unsigned int)(now_ms - s->last_reply_ms));
	s->state = PSTOP_SESS_IDLE;
	s->hb_ms = 0U;
	s->rebonds++;
	protocol_data_init(&s->proto);
	device_id_set(&s->proto.remote_id, s->peer_id);
}
