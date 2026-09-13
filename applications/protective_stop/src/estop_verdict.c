/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "estop_verdict.h"
#include "pstop/pstop_msg.h" /* PSTOP_MESSAGE_OK / PSTOP_MESSAGE_STOP */

void estop_state_init(estop_state_t *st)
{
	(void)memset(st, 0, sizeof(*st));
}

uint8_t estop_decide(estop_state_t *st, int core_id, int rb_hi, int rb_lo)
{
	/* The OK codeword is selected by the ARITHMETIC IMAGE of both fresh
	 * reads -- index 0 (OK) iff rb_hi==1 AND rb_lo==0, computed with no
	 * interpretable boolean. So OK cannot be produced by a stale/latched
	 * flag or by a fault in the health/debounce logic below: every check
	 * there is a STOP-ONLY override that may raise msg to STOP but can
	 * never lower it to OK.
	 */
	static const uint8_t k_estop_msg[2] = {PSTOP_MESSAGE_OK, PSTOP_MESSAGE_STOP};
	uint8_t msg;

	if (core_id == 0) {
		msg = k_estop_msg[(unsigned int)((rb_hi ^ 1) | rb_lo) & 1U];
	} else {
		msg = ((rb_hi == 1) && (rb_lo == 0)) ? PSTOP_MESSAGE_OK : PSTOP_MESSAGE_STOP;
	}

	st->high_ok = (rb_hi == 1);
	st->low_ok = (rb_lo == 0);
	st->primed_high = true;
	st->primed_low = true;

	const bool raw_closed = st->high_ok && st->low_ok;

	/* Asymmetric release debounce: open reports IMMEDIATELY, closed only
	 * after LOOP_RECLOSE_DEBOUNCE_TICKS consecutive healthy ticks.
	 */
	if (raw_closed) {
		if (st->closed_streak < (uint8_t)255U) {
			st->closed_streak++;
		}
		st->open_streak = 0U;
		if (st->closed_streak >= LOOP_RECLOSE_DEBOUNCE_TICKS) {
			st->settled = true;
		}
	} else {
		st->closed_streak = 0U;
		if (st->open_streak < (uint8_t)255U) {
			st->open_streak++;
		}
		if (st->open_streak >= LOOP_BOOT_OPEN_CONFIRM_TICKS) {
			st->settled = true; /* held open: STOP flows */
		}
	}

	/* STOP-ONLY override. msg is already STOP whenever this tick's live
	 * sample did not match the driven level, so a fault here cannot
	 * manufacture an OK.
	 */
	if (!(raw_closed && (st->closed_streak >= LOOP_RECLOSE_DEBOUNCE_TICKS))) {
		msg = PSTOP_MESSAGE_STOP;
	}

	return msg;
}

bool estop_channels_primed(const estop_state_t st[2])
{
	return st[0].primed_high && st[0].primed_low && st[1].primed_high && st[1].primed_low &&
	       st[0].settled && st[1].settled;
}
