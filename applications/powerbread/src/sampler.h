/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_SAMPLER_H_
#define APP_SAMPLER_H_

#include <stdbool.h>
#include <stdint.h>

#define PB_NUM_CH       2
#define PB_CHART_POINTS 160

struct pb_channel_stats {
	float v;  /* latest bus voltage [V] */
	float ma; /* latest current [mA] */
	float mw; /* latest power [mW] */
	float v_min, v_max;
	float ma_min, ma_max;
	float ma_avg;
	float mah;                      /* accumulated charge [mAh] since reset */
	float mwh;                      /* accumulated energy [mWh] since reset */
	int32_t chart[PB_CHART_POINTS]; /* current [mA], oldest first, 20 Hz */
};

struct pb_snapshot {
	bool sensor_ok;
	uint32_t t_ms;
	struct pb_channel_stats ch[PB_NUM_CH];
};

void sampler_get(struct pb_snapshot *out);
void sampler_reset_stats(void);
void sampler_stream_set(bool enable);
bool sampler_stream_get(void);

#endif /* APP_SAMPLER_H_ */
