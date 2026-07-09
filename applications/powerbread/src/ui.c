/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ui.h"
#include "sampler.h"

#include <stdio.h>
#include <stdlib.h>

#include <lvgl.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(ui, LOG_LEVEL_INF);

#define REFRESH_PERIOD_MS 100

static atomic_t req_mode = ATOMIC_INIT(PB_MODE_DASH);
static atomic_t req_channel = ATOMIC_INIT(0);

static lv_obj_t *pages[PB_MODE_COUNT];
static lv_obj_t *dash_val[PB_NUM_CH][3];        /* V / mA / mW value labels */
static lv_obj_t *dash_unit[PB_NUM_CH][3];       /* unit labels; power flips mW<->W */
static enum pb_mode shown_mode = PB_MODE_COUNT; /* force first update */
static struct pb_snapshot snap;

static lv_obj_t *chart;
static lv_chart_series_t *chart_ser;
static lv_obj_t *chart_label;
static int32_t chart_buf[PB_CHART_POINTS];
static lv_obj_t *stats_label;

void pb_ui_set_mode(enum pb_mode mode)
{
	if (mode < PB_MODE_COUNT) {
		atomic_set(&req_mode, mode);
	}
}

void pb_ui_step_mode(int dir)
{
	int m = ((int)atomic_get(&req_mode) + dir + PB_MODE_COUNT) % PB_MODE_COUNT;

	atomic_set(&req_mode, m);
}

void pb_ui_set_channel(uint8_t ch)
{
	if (ch < PB_NUM_CH) {
		atomic_set(&req_channel, ch);
	}
}

uint8_t pb_ui_get_channel(void)
{
	return (uint8_t)atomic_get(&req_channel);
}

/* "12.34" style formatting without float printf: value scaled by 100.
 * The explicit sign prefix keeps -0.99..-0.01 from printing as positive
 * (integer division truncates -50/100 to 0).
 */
static void fmt_x100(char *buf, size_t len, int32_t x100)
{
	snprintf(buf, len, "%s%d.%02d", (x100 < 0 && x100 > -100) ? "-" : "", x100 / 100,
		 abs(x100 % 100));
}

/* one decimal place variant, value scaled by 10 */
static void fmt_x10(char *buf, size_t len, int32_t x10)
{
	snprintf(buf, len, "%s%d.%d", (x10 < 0 && x10 > -10) ? "-" : "", x10 / 10, abs(x10 % 10));
}

/* upstream PowerBread channel accents: cyan for A, pink for B */
static lv_color_t ch_color(uint8_t ch)
{
	return (ch == 0) ? lv_color_hex(0x00b8d4) : lv_color_hex(0xf0308c);
}

static void create_dash(lv_obj_t *parent)
{
	static const char *const units[3] = {"V", "mA", "mW"};

	for (int i = 0; i < PB_NUM_CH; i++) {
		int y0 = (i == 0) ? 2 : 82;
		lv_obj_t *chip = lv_label_create(parent);

		lv_label_set_text_fmt(chip, "Channel %c", 'A' + i);
		lv_obj_set_style_bg_color(chip, ch_color(i), 0);
		lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
		lv_obj_set_style_radius(chip, 2, 0);
		lv_obj_set_style_pad_hor(chip, 3, 0);
		lv_obj_set_style_pad_ver(chip, 1, 0);
		lv_obj_align(chip, LV_ALIGN_TOP_LEFT, 0, y0);

		for (int r = 0; r < 3; r++) {
			int ry = y0 + 16 + r * 17;

			dash_val[i][r] = lv_label_create(parent);
			lv_obj_set_style_text_font(dash_val[i][r], &lv_font_montserrat_16, 0);
			lv_label_set_text(dash_val[i][r], "--");
			lv_obj_align(dash_val[i][r], LV_ALIGN_TOP_LEFT, 0, ry);

			dash_unit[i][r] = lv_label_create(parent);
			lv_label_set_text(dash_unit[i][r], units[r]);
			lv_obj_set_style_text_color(dash_unit[i][r], lv_color_hex(0x9e9e9e), 0);
			lv_obj_align(dash_unit[i][r], LV_ALIGN_TOP_RIGHT, 0, ry + 4);
		}
	}
}

static void create_chart(lv_obj_t *parent)
{
	chart = lv_chart_create(parent);
	lv_obj_set_size(chart, lv_pct(100), 135);
	lv_obj_align(chart, LV_ALIGN_TOP_MID, 0, 0);
	lv_obj_set_style_bg_color(chart, lv_color_black(), 0);
	lv_obj_set_style_border_width(chart, 0, 0);
	lv_obj_set_style_line_color(chart, lv_color_hex(0x303030), LV_PART_MAIN);
	lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
	lv_chart_set_point_count(chart, PB_CHART_POINTS);
	lv_chart_set_div_line_count(chart, 4, 4);
	chart_ser = lv_chart_add_series(chart, ch_color(0), LV_CHART_AXIS_PRIMARY_Y);
	lv_chart_set_series_ext_y_array(chart, chart_ser, chart_buf);

	chart_label = lv_label_create(parent);
	lv_obj_align(chart_label, LV_ALIGN_BOTTOM_MID, 0, 0);
	lv_label_set_text(chart_label, "--");
}

static void create_stats(lv_obj_t *parent)
{
	stats_label = lv_label_create(parent);
	lv_obj_align(stats_label, LV_ALIGN_TOP_LEFT, 0, 0);
	lv_label_set_text(stats_label, "--");
}

static void update_dash(void)
{
	for (int i = 0; i < PB_NUM_CH; i++) {
		char v[16], ma[16], mw[16];

		fmt_x100(v, sizeof(v), (int32_t)(snap.ch[i].v * 100.0f));
		fmt_x10(ma, sizeof(ma), (int32_t)(snap.ch[i].ma * 10.0f));
		if (snap.ch[i].mw >= 1000.0f) {
			/* upstream shows watts once past 1 W (e.g. "1.62 W") */
			fmt_x100(mw, sizeof(mw), (int32_t)(snap.ch[i].mw / 10.0f));
			lv_label_set_text(dash_unit[i][2], "W");
		} else {
			fmt_x10(mw, sizeof(mw), (int32_t)(snap.ch[i].mw * 10.0f));
			lv_label_set_text(dash_unit[i][2], "mW");
		}
		lv_label_set_text(dash_val[i][0], v);
		lv_label_set_text(dash_val[i][1], ma);
		lv_label_set_text(dash_val[i][2], mw);
	}
}

static void update_chart(void)
{
	uint8_t ch = pb_ui_get_channel();
	const struct pb_channel_stats *c = &snap.ch[ch];
	int32_t lo = INT32_MAX, hi = INT32_MIN;
	char ma[16];

	for (int p = 0; p < PB_CHART_POINTS; p++) {
		chart_buf[p] = c->chart[p];
		lo = MIN(lo, chart_buf[p]);
		hi = MAX(hi, chart_buf[p]);
	}
	/* pad the range so a flat line is not glued to an edge */
	if (hi - lo < 10) {
		hi = lo + 10;
	}
	lv_chart_set_series_color(chart, chart_ser, ch_color(ch));
	lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, lo, hi);
	lv_chart_refresh(chart);

	fmt_x100(ma, sizeof(ma), (int32_t)(c->ma * 100.0f));
	lv_label_set_text_fmt(chart_label, "%s mA", ma);
}

static void update_stats(void)
{
	const struct pb_channel_stats *c = &snap.ch[pb_ui_get_channel()];
	char lo[16], hi[16], avg[16], v[16], mah[16], mwh[16];

	fmt_x100(lo, sizeof(lo), (int32_t)(c->ma_min * 100.0f));
	fmt_x100(hi, sizeof(hi), (int32_t)(c->ma_max * 100.0f));
	fmt_x100(avg, sizeof(avg), (int32_t)(c->ma_avg * 100.0f));
	fmt_x100(v, sizeof(v), (int32_t)(c->v * 100.0f));
	fmt_x100(mah, sizeof(mah), (int32_t)(c->mah * 100.0f));
	fmt_x100(mwh, sizeof(mwh), (int32_t)(c->mwh * 100.0f));
	lv_label_set_text_fmt(stats_label, "V %s\nmin %s\nmax %s\navg %s\nmAh %s\nmWh %s", v, lo,
			      hi, avg, mah, mwh);
}

static void refresh_cb(lv_timer_t *timer)
{
	enum pb_mode mode = (enum pb_mode)atomic_get(&req_mode);

	ARG_UNUSED(timer);

	sampler_get(&snap);

	if (mode != shown_mode) {
		for (int i = 0; i < PB_MODE_COUNT; i++) {
			if (i == mode) {
				lv_obj_remove_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
			} else {
				lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
			}
		}
		shown_mode = mode;
	}

	switch (mode) {
	case PB_MODE_DASH:
		update_dash();
		break;
	case PB_MODE_CHART:
		update_chart();
		break;
	case PB_MODE_STATS:
		update_stats();
		break;
	default:
		break;
	}
}

int pb_ui_init(void)
{
	const struct device *disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	if (!device_is_ready(disp)) {
		LOG_ERR("display not ready");
		return -ENODEV;
	}

	lv_obj_t *scr = lv_screen_active();

	/* dark theme matching upstream PowerBread */
	lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
	lv_obj_set_style_text_color(scr, lv_color_white(), 0);
	lv_obj_set_style_pad_all(scr, 0, 0);
	lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

	for (int i = 0; i < PB_MODE_COUNT; i++) {
		pages[i] = lv_obj_create(scr);
		lv_obj_set_size(pages[i], lv_pct(100), lv_pct(100));
		lv_obj_align(pages[i], LV_ALIGN_BOTTOM_MID, 0, 0);
		lv_obj_set_style_bg_opa(pages[i], LV_OPA_TRANSP, 0);
		lv_obj_set_style_text_color(pages[i], lv_color_white(), 0);
		lv_obj_set_style_pad_all(pages[i], 2, 0);
		lv_obj_set_style_border_width(pages[i], 0, 0);
		lv_obj_set_style_radius(pages[i], 0, 0);
		lv_obj_set_scrollbar_mode(pages[i], LV_SCROLLBAR_MODE_OFF);
		lv_obj_remove_flag(pages[i], LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
	}

	create_dash(pages[PB_MODE_DASH]);
	create_chart(pages[PB_MODE_CHART]);
	create_stats(pages[PB_MODE_STATS]);

	lv_timer_create(refresh_cb, REFRESH_PERIOD_MS, NULL);

	display_blanking_off(disp);

	return 0;
}
