#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
// #include <zephyr/drivers/rtc.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/device_runtime.h>
// #include <zephyr/zbus/zbus.h>

// #include <time.h>
// #include <zephyr/posix/time.h>
// #include <zephyr/sys/timeutil.h>

LOG_MODULE_REGISTER(hw_module, LOG_LEVEL_DBG);

// static const struct device *rtc_dev = DEVICE_DT_GET(DT_ALIAS(rtc));
const struct device *const gpio_keys_dev = DEVICE_DT_GET(DT_NODELABEL(gpiokeys));
static const struct pwm_dt_spec pwm_led0 = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led0));
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

K_SEM_DEFINE(sem_hw_inited, 0, 1);
K_SEM_DEFINE(sem_hw_thread_start, 0, 1);

// ZBUS_CHAN_DECLARE(sys_time_chan);
// ZBUS_CHAN_DECLARE(temp_chan);

static void gpio_keys_cb_handler(struct input_event *evt, void *user_data)
{
	if (evt->value == 1) {
		switch (evt->code) {
		case INPUT_KEY_0:
			LOG_INF("Key 0 Pressed");
			pwm_set_pulse_dt(&pwm_led0, pwm_led0.period);
			// k_sem_give(&sem_crown_key_pressed);
			break;
		case INPUT_KEY_1:
			LOG_INF("Key 1 Pressed");
			pwm_set_pulse_dt(&pwm_led0, 0);
			// sys_reboot(SYS_REBOOT_COLD);
			break;
		case INPUT_KEY_2:
			LOG_INF("Key 2 Pressed");
			break;
		case INPUT_KEY_3:
			LOG_INF("Key 3 Pressed");
			break;
		default:
			break;
		}
	}
}
/*
double read_temp_f(void)
{
	struct sensor_value temp_sample;

	sensor_sample_fetch(max30208_dev);
	sensor_channel_get(max30208_dev, SENSOR_CHAN_AMBIENT_TEMP, &temp_sample);
	double temp_c = (double)temp_sample.val1 * 0.005;
	double temp_f = (temp_c * 1.8) + 32.0;
	return temp_f;
}

void hw_rtc_set_device_time(uint8_t m_sec, uint8_t m_min, uint8_t m_hour, uint8_t m_day,
			    uint8_t m_month, uint8_t m_year)
{
	struct rtc_time time_set;

	time_set.tm_sec = m_sec;
	time_set.tm_min = m_min;
	time_set.tm_hour = m_hour;
	time_set.tm_mday = m_day;
	time_set.tm_mon = m_month;
	time_set.tm_year = m_year;

	int ret = rtc_set_time(rtc_dev, &time_set);
}
*/
void hw_init(void)
{
	int ret = 0;
	// static struct rtc_time curr_time;

	if (!gpio_is_ready_dt(&led2)) {
		return;
	}

	ret = gpio_pin_configure_dt(&led2, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		return;
	}

	if (!pwm_is_ready_dt(&pwm_led0)) {
		LOG_ERR("Error: PWM device %s is not ready", pwm_led0.dev->name);
		return;
	}
	pwm_set_pulse_dt(&pwm_led0, 0);

	// rtc_get_time(rtc_dev, &curr_time);
	// LOG_INF("RTC time: %d:%d:%d %d/%d/%d", curr_time.tm_hour, curr_time.tm_min,
	// 	curr_time.tm_sec, curr_time.tm_mon, curr_time.tm_mday, curr_time.tm_year);

	pm_device_runtime_get(gpio_keys_dev);

	INPUT_CALLBACK_DEFINE(gpio_keys_dev, gpio_keys_cb_handler, NULL);

	k_sem_give(&sem_hw_inited);

	LOG_INF("HW Init complete");

	k_sem_give(&sem_hw_thread_start);
}
/*
struct tm hw_get_sys_time(void)
{
	return sys_tm_time;
}

int64_t hw_get_sys_time_ts(void)
{
	int64_t sys_time_ts = timeutil_timegm64(&sys_tm_time);
	return sys_time_ts;
}
*/
void hw_thread(void)
{
	// struct rtc_time rtc_sys_time;
	// double _temp_f = 0.0;
	int ret;

	k_sem_take(&sem_hw_thread_start, K_FOREVER);
	LOG_INF("HW Thread starting");

	for (;;) {
		// Read and publish time
		// ret = rtc_get_time(rtc_dev, &rtc_sys_time);
		// if (ret < 0) {
		// 	LOG_ERR("Failed to get RTC time");
		// }
		// sys_tm_time = *rtc_time_to_tm(&rtc_sys_time);
		// zbus_chan_pub(&sys_time_chan, &sys_tm_time, K_SECONDS(1));

		// Read and publish temperature
		// _temp_f = read_temp_f();

		// zbus_chan_pub(&temp_chan, &temp, K_SECONDS(1));

		ret = gpio_pin_toggle_dt(&led2);
		k_sleep(K_MSEC(500));
	}
}

#define HW_THREAD_STACKSIZE 4096
#define HW_THREAD_PRIORITY  7

K_THREAD_DEFINE(hw_thread_id, HW_THREAD_STACKSIZE, hw_thread, NULL, NULL, NULL, HW_THREAD_PRIORITY,
		0, 0);
