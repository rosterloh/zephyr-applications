/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include "health_logic.h"

static void put_u32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

static uint32_t get_u32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
	       (uint32_t)p[3];
}

void health_encode(const struct health_counters *c, uint8_t out[HEALTH_BLOB_LEN])
{
	out[0] = (uint8_t)HEALTH_BLOB_VER;
	put_u32(&out[1], c->presses);
	put_u32(&out[5], c->mismatch_events);
	put_u32(&out[9], c->uptime_s);
	put_u32(&out[13], c->boots);
	put_u32(&out[17], c->flashes);
	put_u32(&out[21], c->otas);
	(void)memcpy(&out[25], c->fw_sha, sizeof(c->fw_sha));
	out[33] = c->button_swaps;
}

bool health_decode(const uint8_t *blob, size_t len, struct health_counters *out)
{
	if ((blob == NULL) || (out == NULL) || (len != HEALTH_BLOB_LEN) ||
	    (blob[0] != (uint8_t)HEALTH_BLOB_VER)) {
		return false;
	}
	out->presses = get_u32(&blob[1]);
	out->mismatch_events = get_u32(&blob[5]);
	out->uptime_s = get_u32(&blob[9]);
	out->boots = get_u32(&blob[13]);
	out->flashes = get_u32(&blob[17]);
	out->otas = get_u32(&blob[21]);
	(void)memcpy(out->fw_sha, &blob[25], sizeof(out->fw_sha));
	out->button_swaps = blob[33];
	return true;
}

struct health_edge_out health_edge_step(struct health_edge *st, int channel, uint32_t tick,
					uint8_t verdict, uint8_t stop_code)
{
	struct health_edge_out out = {false, false};

	if ((channel < 0) || (channel > 1)) {
		return out;
	}
	st->tick[channel] = tick;
	st->verdict[channel] = verdict;
	st->seen |= (channel == 0) ? 1U : 2U;

	/* Only a same-tick comparison means anything: wait for the partner. */
	if ((st->seen != 3U) || (st->tick[0] != st->tick[1])) {
		return out;
	}

	bool s0 = (st->verdict[0] == stop_code);
	bool s1 = (st->verdict[1] == stop_code);
	bool stop_both = s0 && s1;
	bool mismatch = (s0 != s1);

	if (st->primed) {
		out.press = stop_both && !st->stop_both;
		out.mismatch = mismatch && !st->mismatch;
	}
	st->primed = true;
	st->stop_both = stop_both;
	st->mismatch = mismatch;
	return out;
}

int health_button_level(uint32_t presses, uint32_t rated_ops, uint32_t warn_pct, uint32_t *pct_out)
{
	uint32_t pct = 0U;

	if (rated_ops > 0U) {
		/* 64-bit: presses * 100 overflows 32 bits past ~42M. */
		uint64_t p = ((uint64_t)presses * 100U) / rated_ops;

		pct = (p > 999U) ? 999U : (uint32_t)p;
	}
	if (pct_out != NULL) {
		*pct_out = pct;
	}
	if (rated_ops == 0U) {
		return HEALTH_OK;
	}
	if (pct >= 100U) {
		return HEALTH_CRIT;
	}
	if ((warn_pct > 0U) && (pct >= warn_pct)) {
		return HEALTH_WARN;
	}
	return HEALTH_OK;
}

int health_mismatch_level(uint32_t mismatch_events, uint32_t presses, uint32_t min_events,
			  uint32_t per_1000)
{
	if ((per_1000 == 0U) || (mismatch_events < min_events)) {
		return HEALTH_OK;
	}

	uint64_t basis = (presses < 1000U) ? 1000ULL : (uint64_t)presses;
	uint64_t allowed = (basis * per_1000) / 1000U;

	return ((uint64_t)mismatch_events > allowed) ? HEALTH_WARN : HEALTH_OK;
}

bool health_flush_due(bool dirty, uint32_t since_last_flush_s, uint32_t min_interval_s,
		      uint32_t max_interval_s)
{
	if (max_interval_s < min_interval_s) {
		max_interval_s = min_interval_s;
	}
	if (since_last_flush_s >= max_interval_s) {
		return true;
	}
	return dirty && (since_last_flush_s >= min_interval_s);
}

static int find(const struct health_board *b, const char *src)
{
	for (int i = 0; i < b->n; i++) {
		if (strncmp(b->w[i].src, src, HEALTH_SRC_LEN) == 0) {
			return i;
		}
	}
	return -1;
}

static void copy_str(char *dst, const char *src, size_t cap)
{
	(void)strncpy(dst, (src != NULL) ? src : "", cap - 1U);
	dst[cap - 1U] = '\0';
}

int health_board_publish(struct health_board *b, const char *src, int level, const char *msg,
			 uint64_t now_ms)
{
	if (level == HEALTH_OK) {
		health_board_clear(b, src);
		return 0;
	}

	int i = find(b, src);

	if (i < 0) {
		if (b->n >= HEALTH_MAX_WARNINGS) {
			b->dropped++;
			return -ENOMEM;
		}
		i = b->n++;
		(void)memset(&b->w[i], 0, sizeof(b->w[i]));
		copy_str(b->w[i].src, src, sizeof(b->w[i].src));
		b->w[i].first_ms = now_ms;
	}
	copy_str(b->w[i].msg, msg, sizeof(b->w[i].msg));
	b->w[i].level = level;
	b->w[i].count++;
	b->w[i].last_ms = now_ms;
	return 0;
}

void health_board_clear(struct health_board *b, const char *src)
{
	int i = find(b, src);

	if (i < 0) {
		return;
	}
	for (int j = i; j < (b->n - 1); j++) {
		b->w[j] = b->w[j + 1];
	}
	b->n--;
}

int health_board_level(const struct health_board *b)
{
	int worst = HEALTH_OK;

	for (int i = 0; i < b->n; i++) {
		if (b->w[i].level > worst) {
			worst = b->w[i].level;
		}
	}
	return worst;
}
