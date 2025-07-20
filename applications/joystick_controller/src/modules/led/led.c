#include <zephyr/logging/log.h>
#include <zephyr/init.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/device.h>
#include <zephyr/zbus/zbus.h>
#include <inttypes.h>

#include "message_channel.h"

LOG_MODULE_REGISTER(led, CONFIG_LED_MODULE_LOG_LEVEL);

#define STRIP_NODE DT_ALIAS(led_strip)
#if DT_NODE_HAS_PROP(DT_ALIAS(led_strip), chain_length)
#define STRIP_NUM_PIXELS DT_PROP(DT_ALIAS(led_strip), chain_length)
#else
#error Unable to determine length of LED strip
#endif

#define RGB(_r, _g, _b) {.r = (_r), .g = (_g), .b = (_b)}

static const struct led_rgb colors[] = {
	RGB(0x0f, 0x00, 0x00), /* red */
	RGB(0x00, 0x0f, 0x00), /* green */
	RGB(0x00, 0x00, 0x0f), /* blue */
};
static struct led_rgb pixels[STRIP_NUM_PIXELS];

static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);

ZBUS_SUBSCRIBER_DEFINE(led_subscriber, 4);
ZBUS_CHAN_ADD_OBS(led_ch, led_subscriber, DEFAULT_OBS_PRIO);

static enum sys_states read_sys_msg(void)
{
	int ret;
	enum sys_states msg;

	ret = zbus_chan_read(&led_ch, &msg, K_NO_WAIT);
	if (ret != 0) {
		LOG_ERR("zbus_chan_read, error: %d", ret);
		return false;
	}

	return msg;
}

/* LED Task Function */
static void led_fn(void)
{
	const struct zbus_channel *chan;
	enum sys_states sys_state;
	int ret;

	if (!device_is_ready(strip)) {
		LOG_ERR("LED device %s not ready", strip->name);
		return;
	}
	memset(&pixels, 0x00, sizeof(pixels));

	/* Boot to State Sleep */
	sys_state = SYS_SLEEP;

	LOG_INF("LED module started");
	while (1) {
		switch (sys_state) {
		case SYS_SLEEP:
			memset(&pixels, 0x00, sizeof(pixels));
			led_strip_update_rgb(strip, pixels, STRIP_NUM_PIXELS);
			LOG_DBG("LED off");
			/* Nothing to do, blocking wait to save energy */
			ret = zbus_sub_wait(&led_subscriber, &chan, K_FOREVER);
			break;
		case SYS_STANDBY:
			memcpy(&pixels[0], &colors[0], sizeof(struct led_rgb));
			led_strip_update_rgb(strip, pixels, STRIP_NUM_PIXELS);
			LOG_DBG("LED on");
			k_sleep(K_MSEC(50));
			memset(&pixels, 0x00, sizeof(pixels));
			led_strip_update_rgb(strip, pixels, STRIP_NUM_PIXELS);
			LOG_DBG("LED off");
			k_sleep(K_MSEC(500));
			/* Blinking led, blocking wait not possible */
			ret = zbus_sub_wait(&led_subscriber, &chan, K_NO_WAIT);
			break;
		default:
			break;
		}
		if (ret == 0) {
			/* Short wait for the system to wake up from sleep */
			k_sleep(K_MSEC(1));
			sys_state = read_sys_msg();
		}
	}
}

K_THREAD_DEFINE(led_task, CONFIG_LED_MODULE_THREAD_STACK_SIZE, led_fn, NULL, NULL, NULL,
		CONFIG_LED_MODULE_THREAD_STACK_PRIO, 0, 0);