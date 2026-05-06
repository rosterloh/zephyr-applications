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
#define MAX_MOTORS         8
#define TELEMETRY_PERIOD   K_MSEC(1000)

static int iface;
const static struct dxl_iface_param dxl_param = {
	.rx_timeout = 5000,
	.serial =
		{
			.baud = 115200,
			.parity = UART_CFG_PARITY_NONE,
		},
};
static uint8_t motor_ids[MAX_MOTORS];
static size_t motor_count;
static const struct device *const neoslider = DEVICE_DT_GET(DT_NODELABEL(neoslider));
static uint16_t last_slider = 0;
struct k_work_delayable inputs_scan_work;
struct k_work_delayable telemetry_work;
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

			if (motor_count > 0) {
				/* SYNC_WRITE: broadcast the same GOAL_POSITION to every
				 * motor on the bus in a single transaction. */
				uint32_t goals[MAX_MOTORS];

				for (size_t i = 0; i < motor_count; i++) {
					goals[i] = (uint32_t)slide_val * 4;
				}
				err = dxl_sync_write_u32(iface, GOAL_POSITION, motor_ids,
							 goals, motor_count);
				if (err != 0) {
					LOG_ERR("sync_write GOAL_POSITION failed. %d", err);
				}
			}
		}
	}

	k_work_reschedule(&inputs_scan_work, K_MSEC(300));
}

static void telemetry_fn(struct k_work *work)
{
	int err;

	if (motor_count == 0) {
		goto reschedule;
	}

	/* SYNC_READ: pull PRESENT_POSITION from every motor in one transaction. */
	uint32_t positions[MAX_MOTORS];
	int sync_errs[MAX_MOTORS];

	err = dxl_sync_read_u32(iface, PRESENT_POSITION, motor_ids, positions, sync_errs,
				motor_count);
	if (err == 0) {
		for (size_t i = 0; i < motor_count; i++) {
			LOG_INF("motor %u position=%u", motor_ids[i], positions[i]);
		}
	} else {
		LOG_WRN("sync_read PRESENT_POSITION rc=%d", err);
		for (size_t i = 0; i < motor_count; i++) {
			if (sync_errs[i] != 0) {
				LOG_WRN("  motor %u err=%d", motor_ids[i], sync_errs[i]);
			}
		}
	}

	/* BULK_READ: query a different register per entry in one transaction.
	 * Here we ask each motor for both its temperature and hardware error
	 * status — useful as a periodic health check. */
	struct dxl_bulk_read_entry req[MAX_MOTORS * 2];
	uint32_t bulk_vals[MAX_MOTORS * 2];
	int bulk_errs[MAX_MOTORS * 2];
	size_t n = 0;

	for (size_t i = 0; i < motor_count && n + 1 < ARRAY_SIZE(req); i++) {
		req[n++] = (struct dxl_bulk_read_entry){
			.id = motor_ids[i],
			.item = PRESENT_TEMPERATURE,
		};
		req[n++] = (struct dxl_bulk_read_entry){
			.id = motor_ids[i],
			.item = HARDWARE_ERROR_STATUS,
		};
	}

	err = dxl_bulk_read(iface, req, bulk_vals, bulk_errs, n);
	if (err == 0) {
		for (size_t i = 0; i < n; i++) {
			LOG_INF("motor %u item=%d val=%u", req[i].id, req[i].item,
				bulk_vals[i]);
		}
	} else {
		LOG_WRN("bulk_read rc=%d", err);
	}

reschedule:
	k_work_reschedule(&telemetry_work, TELEMETRY_PERIOD);
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

	/* Discover motors from devicetree and ping each one. */
	const size_t total = dxl_motor_count();

	for (size_t i = 0; i < total && motor_count < MAX_MOTORS; i++) {
		const struct dxl_motor *m = dxl_motor_get(i);

		if (m == NULL || m->iface != iface) {
			continue;
		}
		if (dxl_ping(iface, m->id) != 0) {
			LOG_WRN("Motor '%s' (id=%u) did not respond to ping",
				m->label ? m->label : "?", m->id);
			continue;
		}
		motor_ids[motor_count++] = m->id;
	}
	LOG_INF("Discovered %u dynamixel motor(s)", (unsigned)motor_count);

	if (motor_count > 0) {
		/* SYNC_WRITE: push TORQUE_ENABLE / OPERATING_MODE / TORQUE_ENABLE
		 * to every motor in three broadcast bursts instead of N*3 unicasts. */
		uint8_t off[MAX_MOTORS] = {0};
		uint8_t mode[MAX_MOTORS];
		uint8_t on[MAX_MOTORS];

		for (size_t i = 0; i < motor_count; i++) {
			mode[i] = DXL_OP_POSITION;
			on[i] = 1;
		}

		ret = dxl_sync_write_u8(iface, TORQUE_ENABLE, motor_ids, off, motor_count);
		ret |= dxl_sync_write_u8(iface, OPERATING_MODE, motor_ids, mode, motor_count);
		ret |= dxl_sync_write_u8(iface, TORQUE_ENABLE, motor_ids, on, motor_count);
		if (ret != 0) {
			LOG_ERR("Failed to configure motors. %d", ret);
		}

		/* BULK_WRITE: set PROFILE_VELOCITY and LED on each motor in one
		 * transaction, mixing two different registers per motor. */
		struct dxl_bulk_write_entry init_writes[MAX_MOTORS * 2];
		size_t n = 0;

		for (size_t i = 0; i < motor_count && n + 1 < ARRAY_SIZE(init_writes); i++) {
			init_writes[n++] = (struct dxl_bulk_write_entry){
				.id = motor_ids[i],
				.item = PROFILE_VELOCITY,
				.value = 100,
			};
			init_writes[n++] = (struct dxl_bulk_write_entry){
				.id = motor_ids[i],
				.item = LED,
				.value = 1,
			};
		}
		ret = dxl_bulk_write(iface, init_writes, n);
		if (ret != 0) {
			LOG_ERR("bulk_write init failed. %d", ret);
		}
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
	k_work_init_delayable(&telemetry_work, telemetry_fn);
	k_work_reschedule(&telemetry_work, TELEMETRY_PERIOD);

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