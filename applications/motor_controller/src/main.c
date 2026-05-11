#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/display.h>
#include <zephyr/actuator/actuator.h>
#include <zephyr/actuator/actuator_group.h>
#include <drivers/seesaw.h>
#include <app_version.h>
#include <lvgl.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#define NEOSLIDER_NEOPIN   14
#define NEOSLIDER_ANALOGIN 18

#define MOTOR0 DEVICE_DT_GET(DT_NODELABEL(motor0))

ACTUATOR_GROUP_DEFINE(arm, MOTOR0);

static const struct device *const neoslider = DEVICE_DT_GET(DT_NODELABEL(neoslider));
static uint16_t last_slider = UINT16_MAX;
static struct k_work_delayable inputs_scan_work;
static char slide_str[11];
static lv_obj_t *slide_label;
static lv_obj_t *slider;

static void on_feedback(const struct device *dev, const struct actuator_feedback *fb, void *ud)
{
	ARG_UNUSED(ud);
	LOG_INF("%s: pos=%.3f vel=%.3f temp=%.0f flags=0x%08x", dev->name, (double)fb->position,
		(double)fb->velocity, (double)fb->temperature, fb->fault_flags);
}

static void inputs_scan_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	uint16_t slide_val;
	int err = seesaw_read_analog(neoslider, NEOSLIDER_ANALOGIN, &slide_val);
	if (err == 0 && slide_val != last_slider) {
		lv_slider_set_value(slider, slide_val, LV_ANIM_OFF);
		snprintf(slide_str, sizeof(slide_str), "%u", slide_val);
		lv_label_set_text(slide_label, slide_str);
		last_slider = slide_val;

		/* Map slider [0..1023] to [-pi..+pi] rad. */
		float rad = ((float)slide_val / 1023.0f) * 2.0f * (float)M_PI - (float)M_PI;
		float goals[1] = {rad};
		(void)actuator_group_set_position(&arm, goals);
	}
	k_work_reschedule(&inputs_scan_work, K_MSEC(300));
}

int main(void)
{
	int ret;
	const struct device *display_dev;

	LOG_INF("Motor Controller Application %s", APP_VERSION_STRING);

	for (size_t i = 0; i < arm.n; i++) {
		if (!device_is_ready(arm.devs[i])) {
			LOG_ERR("actuator %s not ready", arm.devs[i]->name);
			return -ENODEV;
		}
		(void)actuator_register_feedback_cb(arm.devs[i], on_feedback, NULL);
	}

	ret = actuator_group_enable(&arm);
	if (ret) {
		LOG_ERR("group enable: %d", ret);
		return ret;
	}

	if (!device_is_ready(neoslider)) {
		LOG_ERR("Error: neoslider not ready");
		return 0;
	}

	ret = seesaw_neopixel_setup(neoslider, NEO_GRB + NEO_KHZ800, 4, NEOSLIDER_NEOPIN);
	ret |= seesaw_neopixel_set_brightness(neoslider, 40);
	if (ret != 0) {
		LOG_ERR("Error %d: failed to setup neoslider leds", ret);
		return 0;
	}

	k_work_init_delayable(&inputs_scan_work, inputs_scan_fn);
	k_work_reschedule(&inputs_scan_work, K_NO_WAIT);

	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Device %s not found", display_dev->name);
	}

	slider = lv_slider_create(lv_scr_act());
	lv_obj_center(slider);
	lv_slider_set_range(slider, 0, 1023);
	lv_slider_set_value(slider, 25, LV_ANIM_OFF);
	lv_obj_refresh_ext_draw_size(slider);

	slide_label = lv_label_create(lv_scr_act());
	lv_obj_align_to(slide_label, slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

	lv_task_handler();
	display_blanking_off(display_dev);

	while (1) {
		lv_task_handler();
		k_sleep(K_MSEC(10));
	}
	return 0;
}
