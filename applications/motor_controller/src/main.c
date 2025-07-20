#include <zephyr/kernel.h>
#include <zephyr/drivers/display.h>
#include <drivers/dynamixel.h>
#include <drivers/seesaw.h>
#include <app_version.h>
#include <lvgl.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

/* https://learn.adafruit.com/adafruit-neoslider/pinouts */
#define NEOSLIDER_NEOPIN   14
#define NEOSLIDER_ANALOGIN 18
#define DXL_DEV_NAME       DEVICE_DT_NAME(DT_INST(0, robotis_dynamixel))

static int iface;
const static struct dxl_iface_param dxl_param = {
	.rx_timeout = 5000,
	.serial =
		{
			.baud = 115200,
			.parity = UART_CFG_PARITY_NONE,
		},
};
static uint8_t motor_id = 1;
static const struct device *const neoslider = DEVICE_DT_GET(DT_NODELABEL(neoslider));
static uint16_t last_slider = 0;
struct k_work_delayable inputs_scan_work;
char slide_str[11] = {0};
static lv_obj_t *slide_label;
static lv_obj_t *slider;

static void inputs_scan_fn(struct k_work *work)
{
	int err;
	uint16_t slide_val;

	err = seesaw_read_analog(neoslider, NEOSLIDER_ANALOGIN, &slide_val);
	if (err == 0) {
		if (slide_val != last_slider) {
			lv_slider_set_value(slider, slide_val, LV_ANIM_OFF);
			sprintf(slide_str, "%d", slide_val);
			lv_label_set_text(slide_label, slide_str);
			last_slider = slide_val;

			err = dxl_write(iface, motor_id, GOAL_POSITION, slide_val * 4);
			if (err != 0) {
				LOG_ERR("Failed to write goal position. %d", err);
			}
		}
	}

	k_work_reschedule(&inputs_scan_work, K_MSEC(300));
}

int main(void)
{
	int ret;
	const struct device *display_dev;
	// struct display_capabilities capabilities;

	LOG_INF("Motor Controller Application %s", APP_VERSION_STRING);

	iface = dxl_iface_get_by_name(DXL_DEV_NAME);
	if (iface < 0) {
		LOG_ERR("Failed to get dynamixel interface %s", DXL_DEV_NAME);
		return -ENOMEM;
	}

	if (dxl_init(iface, dxl_param)) {
		LOG_ERR("Dynamixel initialisation failed");
		return -ENODEV;
	}

	ret = dxl_ping(iface, motor_id);
	if (ret == 0) {
		ret = dxl_write(iface, motor_id, TORQUE_ENABLE, 0);
		ret |= dxl_write(iface, motor_id, OPERATING_MODE, DXL_OP_POSITION);
		ret |= dxl_write(iface, motor_id, TORQUE_ENABLE, 1);
		if (ret != 0) {
			LOG_ERR("Failed to configure motor. %d", ret);
		}
	} else {
		LOG_WRN("Failed to ping motor with ID %d", motor_id);
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

	// display_get_capabilities(display_dev, &capabilities);
	// lv_disp_set_rotation(NULL, LV_DISP_ROT_90);

	// lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), LV_PART_MAIN |
	// LV_STATE_DEFAULT);

	slider = lv_slider_create(lv_scr_act());
	// lv_obj_set_pos(slider, 45, 90);
	// lv_obj_set_size(slider, 150, 10);
	lv_obj_center(slider);
	lv_slider_set_range(slider, 0, 1024);
	lv_slider_set_value(slider, 25, LV_ANIM_OFF);
	// lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
	lv_obj_refresh_ext_draw_size(slider);

	slide_label = lv_label_create(lv_scr_act());
	// lv_obj_set_style_text_color(lv_scr_act(), lv_color_hex(0xffffff), LV_PART_MAIN);
	lv_obj_align_to(slide_label, slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
	// lv_obj_align(slide_label, LV_ALIGN_BOTTOM_MID, 0, 0);

	lv_task_handler();
	display_blanking_off(display_dev);

	while (1) {
		lv_task_handler();
		k_sleep(K_MSEC(10));
	}

	return 0;
}