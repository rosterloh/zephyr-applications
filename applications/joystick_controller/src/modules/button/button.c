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

#define NEOKEY_GPIO_NODE DT_NODELABEL(neokey_gpio)
#define NEOKEY_LED_NODE  DT_NODELABEL(neokey_leds)
#if DT_NODE_HAS_STATUS(NEOKEY_GPIO_NODE, okay) && DT_NODE_HAS_STATUS(NEOKEY_LED_NODE, okay)
#include <zephyr/drivers/led_strip.h>

#define NEOKEY_BTN_A 4
#define NEOKEY_BTN_B 5
#define NEOKEY_BTN_C 6
#define NEOKEY_BTN_D 7
#define NEOKEY_BTN_MASK                                                                            \
	(BIT(NEOKEY_BTN_A) | BIT(NEOKEY_BTN_B) | BIT(NEOKEY_BTN_C) | BIT(NEOKEY_BTN_D))
#define NEOKEY_NUM_LEDS 4

static const struct device *const neokey_gpio = DEVICE_DT_GET(NEOKEY_GPIO_NODE);
static const struct device *const neokey_leds = DEVICE_DT_GET(NEOKEY_LED_NODE);
static uint8_t last_buttons;
static struct k_work_delayable neokey_scan_work;
static struct led_rgb neokey_pixels[NEOKEY_NUM_LEDS];

static void neokey_set_pixel(int idx, uint8_t r, uint8_t g, uint8_t b)
{
	neokey_pixels[idx].r = r;
	neokey_pixels[idx].g = g;
	neokey_pixels[idx].b = b;
}

static void neokey_scan_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	gpio_port_value_t raw;

	if (gpio_port_get(neokey_gpio, &raw) != 0) {
		k_work_reschedule(&neokey_scan_work, K_MSEC(30));
		return;
	}
	/* Active-low: invert and mask. */
	uint8_t pressed = (uint8_t)((~raw & NEOKEY_BTN_MASK) >> NEOKEY_BTN_A);
	uint8_t just_pressed = (pressed ^ last_buttons) & pressed;
	uint8_t just_released = (pressed ^ last_buttons) & ~pressed;

	if (just_pressed | just_released) {
		LOG_DBG("Pressed 0x%x, Released 0x%x", just_pressed, just_released);
		for (int b = 0; b < NEOKEY_NUM_LEDS; b++) {
			if (just_pressed & BIT(b)) {
				switch (b) {
				case 0:
					neokey_set_pixel(b, 0xFF, 0x00, 0x00);
					break;
				case 1:
					neokey_set_pixel(b, 0x00, 0xFF, 0x00);
					break;
				case 2:
					neokey_set_pixel(b, 0x00, 0x00, 0xFF);
					break;
				case 3:
					neokey_set_pixel(b, 0xFF, 0x00, 0xFF);
					break;
				default:
					neokey_set_pixel(b, 0x00, 0x00, 0x00);
					break;
				}

				enum sys_events msg = SYS_BUTTON_PRESSED;
				int err = zbus_chan_pub(&button_ch, &msg, K_SECONDS(1));

				if (err) {
					LOG_ERR("zbus_chan_pub, error: %d", err);
				}
			}

			if (just_released & BIT(b)) {
				neokey_set_pixel(b, 0x00, 0x00, 0x00);
			}
		}
		(void)led_strip_update_rgb(neokey_leds, neokey_pixels, ARRAY_SIZE(neokey_pixels));
	}
	last_buttons = pressed;
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
#if DT_NODE_HAS_STATUS(NEOKEY_GPIO_NODE, okay) && DT_NODE_HAS_STATUS(NEOKEY_LED_NODE, okay)
	if (!device_is_ready(neokey_gpio) || !device_is_ready(neokey_leds)) {
		LOG_ERR("Error: neokey devices not ready");
		return 0;
	}

	for (int pin = NEOKEY_BTN_A; pin <= NEOKEY_BTN_D; pin++) {
		ret = gpio_pin_configure(neokey_gpio, pin, GPIO_INPUT | GPIO_PULL_UP);
		if (ret != 0) {
			LOG_ERR("Error %d: failed to configure neokey pin %d", ret, pin);
			return 0;
		}
	}

	/* Clear all neopixels at startup. */
	(void)led_strip_update_rgb(neokey_leds, neokey_pixels, ARRAY_SIZE(neokey_pixels));

	k_work_init_delayable(&neokey_scan_work, neokey_scan_fn);
	k_work_reschedule(&neokey_scan_work, K_NO_WAIT);
#endif
	return 0;
}

SYS_INIT(init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
