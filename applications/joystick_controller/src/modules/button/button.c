#include <zephyr/logging/log.h>
#include <zephyr/init.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>
#include <zephyr/zbus/zbus.h>

#include <inttypes.h>

#include "message_channel.h"

LOG_MODULE_REGISTER(button, CONFIG_BUTTON_MODULE_LOG_LEVEL);

#define DEBOUNCE_DELAY_MS 10
#define SW0_NODE          DT_ALIAS(sw0)
#if !DT_NODE_HAS_STATUS(SW0_NODE, okay)
#error "Unsupported board: sw0 devicetree alias is not defined"
#endif
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios, {0});
static struct gpio_callback button_cb_data;

static struct k_work_delayable send_event_work;

#define NEOKEY_NODE DT_NODELABEL(neokey_1x4)
#if DT_NODE_HAS_STATUS(NEOKEY_NODE, okay)
#include <drivers/seesaw.h>

static const struct device *const neokey = DEVICE_DT_GET(NEOKEY_NODE);
static uint8_t last_buttons = 0;
struct k_work_delayable neokey_scan_work;

static void neokey_scan_fn(struct k_work *work)
{
	int err;
	enum sys_events msg;
	uint32_t buttons = 0;

	err = seesaw_read_digital(neokey, NEOKEY_1X4_BUTTONMASK, &buttons);
	if (err == 0) {
		buttons ^= NEOKEY_1X4_BUTTONMASK;
		buttons &= NEOKEY_1X4_BUTTONMASK;
		buttons >>= NEOKEY_1X4_BUTTONA;

		uint8_t just_pressed = (buttons ^ last_buttons) & buttons;
		uint8_t just_released = (buttons ^ last_buttons) & ~buttons;
		if (just_pressed | just_released) {
			LOG_DBG("Pressed 0x%x, Released 0x%x", just_pressed, just_released);
			for (int b = 0; b < 4; b++) {
				if (just_pressed & BIT(b)) {
					switch (b) {
					case 0:
						seesaw_neopixel_set_colour(neokey, b, 0xFF, 0, 0,
									   0);
						break;

					case 1:
						seesaw_neopixel_set_colour(neokey, b, 0, 0xFF, 0,
									   0);
						break;

					case 2:
						seesaw_neopixel_set_colour(neokey, b, 0, 0, 0xFF,
									   0);
						break;

					case 3:
						seesaw_neopixel_set_colour(neokey, b, 0xFF, 0, 0xFF,
									   0);
						break;

					default:
						seesaw_neopixel_set_colour(neokey, b, 0, 0, 0, 0);
						break;
					}

					msg = BUTTON_A_PRESSED + b;
					err = zbus_chan_pub(&button_ch, &msg, K_SECONDS(1));
					if (err) {
						LOG_ERR("zbus_chan_pub, error: %d", err);
					}
				}

				if (just_released & BIT(b)) {
					seesaw_neopixel_set_colour(neokey, b, 0, 0, 0, 0);
					msg = BUTTON_A_RELEASED + b;
					err = zbus_chan_pub(&button_ch, &msg, K_SECONDS(1));
					if (err) {
						LOG_ERR("zbus_chan_pub, error: %d", err);
					}
				}
			}
			seesaw_neopixel_show(neokey);
		}
		last_buttons = buttons;
	} else {
		LOG_WRN("Failed to read NeoKey (%d)", err);
	}
	k_work_reschedule(&neokey_scan_work, K_MSEC(30));
}
#endif

/* System workqueue context */
static void send_event_work_handler(struct k_work *work)
{
	int err;
	enum sys_events msg;

	/* Due to button bouncing, check if button was pressed or released */
	if (gpio_pin_get(button.port, button.pin) == 0) {
		return;
	}

	msg = SYS_BUTTON_PRESSED;
	LOG_INF("Button pressed, sent zbus event");
	err = zbus_chan_pub(&button_ch, &msg, K_SECONDS(1));
	if (err) {
		LOG_ERR("zbus_chan_pub, error: %d", err);
	}
}

/* Interrupt context */
static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	static int64_t last_press_time;
	int64_t now = k_uptime_get();

	if (now - last_press_time > DEBOUNCE_DELAY_MS) {
		last_press_time = now;
		k_work_schedule(&send_event_work, K_MSEC(DEBOUNCE_DELAY_MS / 2));
	}
}

static int init(void)
{
	int ret;

	if (!gpio_is_ready_dt(&button)) {
		LOG_ERR("Error: button device %s is not ready", button.port->name);
		return 0;
	}

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret != 0) {
		LOG_ERR("Error %d: failed to configure %s pin %d", ret, button.port->name,
			button.pin);
		return 0;
	}

	ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		LOG_ERR("Error %d: failed to configure interrupt on %s pin %d", ret,
			button.port->name, button.pin);
		return 0;
	}

	gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb_data);
	LOG_INF("Set up button at %s pin %d", button.port->name, button.pin);

	k_work_init_delayable(&send_event_work, send_event_work_handler);
#if DT_NODE_HAS_STATUS(NEOKEY_NODE, okay)
	if (!device_is_ready(neokey)) {
		LOG_ERR("Error: neokey not ready");
		return 0;
	}

	ret = seesaw_write_pin_mode(neokey, NEOKEY_1X4_BUTTONMASK, SEESAW_INPUT_PULLUP);
	if (ret != 0) {
		LOG_ERR("Error %d: failed to configure neokey gpios", ret);
		return 0;
	}
	ret = seesaw_gpio_interrupts(neokey, NEOKEY_1X4_BUTTONMASK, 1);
	if (ret != 0) {
		LOG_ERR("Error %d: failed to enable neokey interrupts", ret);
		return 0;
	}
	ret = seesaw_neopixel_setup(neokey, NEO_GRB + NEO_KHZ800, NEOKEY_1X4_KEYS,
				    NEOKEY_1X4_NEOPIN);
	ret |= seesaw_neopixel_set_brightness(neokey, 40);
	if (ret != 0) {
		LOG_ERR("Error %d: failed to setup neokey leds", ret);
		return 0;
	}

	k_work_init_delayable(&neokey_scan_work, neokey_scan_fn);
	k_work_reschedule(&neokey_scan_work, K_NO_WAIT);
#endif
	return 0;
}

SYS_INIT(init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
