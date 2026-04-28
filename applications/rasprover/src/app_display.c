#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_display, LOG_LEVEL_DBG);

#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <lvgl.h>

#include <app_version.h>

#include "app_display.h"

static const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static bool initialised;

static lv_obj_t *power_label;
static lv_obj_t *status_label;

/* --- tick / work --------------------------------------------------------- */

static void display_tick_cb(struct k_work *work)
{
	lv_timer_handler();
}

K_WORK_DEFINE(display_tick_work, display_tick_cb);

#if IS_ENABLED(CONFIG_APP_DISPLAY_WORK_QUEUE_DEDICATED)
K_THREAD_STACK_DEFINE(display_work_stack_area,
		      CONFIG_APP_DISPLAY_DEDICATED_THREAD_STACK_SIZE);
static struct k_work_q display_work_q;
#endif

static struct k_work_q *app_display_work_q(void)
{
#if IS_ENABLED(CONFIG_APP_DISPLAY_WORK_QUEUE_DEDICATED)
	return &display_work_q;
#else
	return &k_sys_work_q;
#endif
}

static void display_timer_cb(struct k_timer *timer)
{
	k_work_submit_to_queue(app_display_work_q(), &display_tick_work);
}

K_TIMER_DEFINE(display_timer, display_timer_cb, NULL);

static void unblank_display_cb(struct k_work *work)
{
	display_blanking_off(display);
	k_timer_start(&display_timer, K_MSEC(10), K_MSEC(10));
}

K_WORK_DEFINE(unblank_work, unblank_display_cb);

#if IS_ENABLED(CONFIG_APP_DISPLAY_BLANK_ON_IDLE)
static void blank_display_cb(struct k_work *work)
{
	k_timer_stop(&display_timer);
	display_blanking_on(display);
}

K_WORK_DEFINE(blank_work, blank_display_cb);

void app_display_blank(void)
{
	k_work_submit_to_queue(app_display_work_q(), &blank_work);
}

void app_display_unblank(void)
{
	k_work_submit_to_queue(app_display_work_q(), &unblank_work);
}
#endif

/* --- public update ------------------------------------------------------- */

void app_display_update_power(double v, double i, double p)
{
	if (!initialised) {
		return;
	}

	char buf[32];

	snprintf(buf, sizeof(buf), "%.2fV  %.3fA  %.2fW", v, i, p);
	lv_label_set_text(power_label, buf);
}

/* --- screen build -------------------------------------------------------- */

static lv_obj_t *build_screen(void)
{
	lv_obj_t *scr = lv_obj_create(NULL);

	lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
	lv_obj_set_style_text_color(scr, lv_color_white(), LV_PART_MAIN);
	lv_obj_set_style_pad_all(scr, 0, LV_PART_MAIN);

	power_label = lv_label_create(scr);
	lv_label_set_text(power_label, "--.- V  -.--- A  -.- W");
	lv_obj_set_style_text_color(power_label, lv_color_white(), LV_PART_MAIN);
	lv_obj_align(power_label, LV_ALIGN_TOP_LEFT, 2, 0);

	status_label = lv_label_create(scr);
	lv_label_set_text(status_label, "Rasprover v" APP_VERSION_STRING);
	lv_obj_set_style_text_color(status_label,
				    lv_color_make(0x80, 0x80, 0x80), LV_PART_MAIN);
	lv_obj_align(status_label, LV_ALIGN_BOTTOM_LEFT, 2, 0);

	return scr;
}

/* --- initialise ---------------------------------------------------------- */

static void initialise_display_cb(struct k_work *work)
{
	if (!device_is_ready(display)) {
		LOG_ERR("Display device not ready");
		return;
	}

	lv_scr_load(build_screen());
	initialised = true;

	k_work_submit_to_queue(app_display_work_q(), &unblank_work);
	LOG_INF("Display initialised");
}

K_WORK_DEFINE(init_work, initialise_display_cb);

int app_display_init(void)
{
#if IS_ENABLED(CONFIG_APP_DISPLAY_WORK_QUEUE_DEDICATED)
	k_work_queue_start(&display_work_q, display_work_stack_area,
			   K_THREAD_STACK_SIZEOF(display_work_stack_area),
			   CONFIG_APP_DISPLAY_DEDICATED_THREAD_PRIORITY, NULL);
#endif
	k_work_submit_to_queue(app_display_work_q(), &init_work);
	return 0;
}
