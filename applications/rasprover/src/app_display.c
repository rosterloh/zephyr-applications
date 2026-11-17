#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_display, LOG_LEVEL_DBG);

#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/kernel/dedicated_workq.h>
#include <lvgl.h>
#include <lvgl_zephyr.h>

#include <zephyr/app_version.h>

#include "app_display.h"

static const struct device *display = DEVICE_DT_GET_OR_NULL(DT_CHOSEN(zephyr_display));
static bool initialised;

static lv_obj_t *power_label;
static lv_obj_t *status_label;

/* --- tick / work --------------------------------------------------------- */

/* LVGL is not thread-safe and app_display_update_power() is called from the
 * main thread, so every LVGL call is taken under the module's mutex.
 */
static void display_tick_cb(struct k_work *work)
{
	lvgl_lock();
	lv_timer_handler();
	lvgl_unlock();
}

K_WORK_DEFINE(display_tick_work, display_tick_cb);

K_DEDICATED_WORK_QUEUE_DECLARE(app_display, APP_DISPLAY);
K_DEDICATED_WORK_QUEUE_DEFINE(app_display, APP_DISPLAY);

static void display_timer_cb(struct k_timer *timer)
{
	app_display_work_submit(&display_tick_work);
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
	app_display_work_submit(&blank_work);
}

void app_display_unblank(void)
{
	app_display_work_submit(&unblank_work);
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

	lvgl_lock();
	lv_label_set_text(power_label, buf);
	lvgl_unlock();
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
	lv_obj_set_style_text_color(status_label, lv_color_make(0x80, 0x80, 0x80), LV_PART_MAIN);
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

	lvgl_lock();
	lv_screen_load(build_screen());
	lvgl_unlock();
	initialised = true;

	app_display_work_submit(&unblank_work);
	LOG_INF("Display initialised");
}

K_WORK_DEFINE(init_work, initialise_display_cb);

int app_display_init(void)
{
	app_display_work_submit(&init_work);
	return 0;
}
