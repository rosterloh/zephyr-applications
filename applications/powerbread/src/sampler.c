/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sampler.h"

#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_REGISTER(sampler, LOG_LEVEL_INF);

/*
 * Private attribute of the in-tree INA3221 driver
 * (deps/zephyr/drivers/sensor/ti/ina3221/ina3221.h). Selects which of the
 * three hardware channels channel_get() reports; val1 is 1-based.
 */
#define SENSOR_ATTR_INA3221_SELECTED_CHANNEL (SENSOR_ATTR_PRIV_START + 1)

#define SAMPLE_PERIOD_MS 10
#define CHART_DECIMATION 5 /* 100 Hz / 5 = 20 Hz chart rate */

struct channel_acc {
	float v, ma, mw;
	float v_min, v_max;
	float ma_min, ma_max;
	double ma_sum;
	uint32_t n;
	double mah, mwh;
	int32_t chart[PB_CHART_POINTS];
	uint16_t head; /* next write index == oldest sample */
};

static const struct device *const ina = DEVICE_DT_GET_ONE(ti_ina3221);
static struct channel_acc acc[PB_NUM_CH];
static bool sensor_ok;
static bool stream_on;
static K_MUTEX_DEFINE(lock);

static void reset_channel(struct channel_acc *c)
{
	c->v_min = 1e9f;
	c->ma_min = 1e9f;
	c->v_max = -1e9f;
	c->ma_max = -1e9f;
	c->ma_sum = 0.0;
	c->n = 0;
	c->mah = 0.0;
	c->mwh = 0.0;
}

void sampler_reset_stats(void)
{
	k_mutex_lock(&lock, K_FOREVER);
	for (int i = 0; i < PB_NUM_CH; i++) {
		reset_channel(&acc[i]);
	}
	k_mutex_unlock(&lock);
}

void sampler_stream_set(bool enable)
{
	stream_on = enable;
}

bool sampler_stream_get(void)
{
	return stream_on;
}

void sampler_get(struct pb_snapshot *out)
{
	k_mutex_lock(&lock, K_FOREVER);
	out->sensor_ok = sensor_ok;
	out->t_ms = k_uptime_get_32();
	for (int i = 0; i < PB_NUM_CH; i++) {
		const struct channel_acc *c = &acc[i];
		struct pb_channel_stats *o = &out->ch[i];

		o->v = c->v;
		o->ma = c->ma;
		o->mw = c->mw;
		o->v_min = (c->n > 0) ? c->v_min : 0.0f;
		o->v_max = (c->n > 0) ? c->v_max : 0.0f;
		o->ma_min = (c->n > 0) ? c->ma_min : 0.0f;
		o->ma_max = (c->n > 0) ? c->ma_max : 0.0f;
		o->ma_avg = (c->n > 0) ? (float)(c->ma_sum / c->n) : 0.0f;
		o->mah = (float)c->mah;
		o->mwh = (float)c->mwh;
		for (int p = 0; p < PB_CHART_POINTS; p++) {
			o->chart[p] = c->chart[(c->head + p) % PB_CHART_POINTS];
		}
	}
	k_mutex_unlock(&lock);
}

static int read_channels(float *v, float *ma, float *mw)
{
	struct sensor_value val;
	int ret;

	ret = sensor_sample_fetch(ina);
	if (ret != 0) {
		return ret;
	}

	for (int i = 0; i < PB_NUM_CH; i++) {
		struct sensor_value sel = {.val1 = i + 1, .val2 = 0};

		ret = sensor_attr_set(ina, SENSOR_CHAN_ALL, SENSOR_ATTR_INA3221_SELECTED_CHANNEL,
				      &sel);
		if (ret != 0) {
			return ret;
		}
		ret = sensor_channel_get(ina, SENSOR_CHAN_VOLTAGE, &val);
		if (ret != 0) {
			return ret;
		}
		v[i] = sensor_value_to_float(&val);
		ret = sensor_channel_get(ina, SENSOR_CHAN_CURRENT, &val);
		if (ret != 0) {
			return ret;
		}
		ma[i] = sensor_value_to_float(&val) * 1000.0f;
		ret = sensor_channel_get(ina, SENSOR_CHAN_POWER, &val);
		if (ret != 0) {
			return ret;
		}
		mw[i] = sensor_value_to_float(&val) * 1000.0f;
	}
	return 0;
}

static void stream_csv(uint32_t t_ms, const float *v, const float *ma, const float *mw)
{
	for (int i = 0; i < PB_NUM_CH; i++) {
		int32_t mv = (int32_t)(v[i] * 1000.0f);
		int32_t ma_x10 = (int32_t)(ma[i] * 10.0f);
		int32_t mw_x10 = (int32_t)(mw[i] * 10.0f);

		printk("%u,%d,%d,%s%d.%d,%s%d.%d\n", t_ms, i + 1, mv,
		       (ma_x10 < 0 && ma_x10 > -10) ? "-" : "", ma_x10 / 10, abs(ma_x10 % 10),
		       (mw_x10 < 0 && mw_x10 > -10) ? "-" : "", mw_x10 / 10, abs(mw_x10 % 10));
	}
}

static void sampler_thread(void *a, void *b, void *c)
{
	float v[PB_NUM_CH], ma[PB_NUM_CH], mw[PB_NUM_CH];
	unsigned int decim = 0;
	int64_t last_t = 0;
	unsigned int errs = 0;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	sampler_reset_stats();

	if (!device_is_ready(ina)) {
		LOG_ERR("INA3221 not ready");
		return;
	}

	last_t = k_uptime_get();

	while (true) {
		int64_t t0 = k_uptime_get();
		int ret = read_channels(v, ma, mw);

		if (ret == 0) {
			int64_t now = k_uptime_get();
			double dt_h = (double)(now - last_t) / 3600000.0;
			bool push;

			last_t = now;
			decim++;
			push = (decim >= CHART_DECIMATION);
			if (push) {
				decim = 0;
			}

			k_mutex_lock(&lock, K_FOREVER);
			sensor_ok = true;
			for (int i = 0; i < PB_NUM_CH; i++) {
				struct channel_acc *ch = &acc[i];

				ch->v = v[i];
				ch->ma = ma[i];
				ch->mw = mw[i];
				ch->v_min = MIN(ch->v_min, v[i]);
				ch->v_max = MAX(ch->v_max, v[i]);
				ch->ma_min = MIN(ch->ma_min, ma[i]);
				ch->ma_max = MAX(ch->ma_max, ma[i]);
				ch->ma_sum += ma[i];
				ch->n++;
				ch->mah += ma[i] * dt_h;
				ch->mwh += mw[i] * dt_h;
				if (push) {
					ch->chart[ch->head] = (int32_t)ma[i];
					ch->head = (ch->head + 1) % PB_CHART_POINTS;
				}
			}
			k_mutex_unlock(&lock);

			if (stream_on) {
				stream_csv((uint32_t)now, v, ma, mw);
			}
			errs = 0;
		} else {
			k_mutex_lock(&lock, K_FOREVER);
			sensor_ok = false;
			k_mutex_unlock(&lock);
			errs++;
			if (errs == 1 || (errs % 500) == 0) {
				LOG_WRN("INA3221 read failed: %d", ret);
			}
		}

		int64_t elapsed = k_uptime_get() - t0;

		if (elapsed < SAMPLE_PERIOD_MS) {
			k_msleep(SAMPLE_PERIOD_MS - (int32_t)elapsed);
		}
	}
}

K_THREAD_DEFINE(pb_sampler, 2048, sampler_thread, NULL, NULL, NULL, 5, 0, 0);
