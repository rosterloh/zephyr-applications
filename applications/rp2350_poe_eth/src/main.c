/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Bring-up / evaluation firmware for the Waveshare RP2350-POE-ETH.
 *
 * Exercises the parts of the board that matter for a fielded PoE device:
 * the W6300 Ethernet controller, DHCPv4, MCUboot-backed hawkBit OTA and the
 * onboard status LED.
 */

#include <zephyr/kernel.h>
#include <zephyr/app_version.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/logging/log.h>
#include <zephyr/mgmt/hawkbit/autohandler.h>
#include <zephyr/mgmt/hawkbit/config.h>
#include <zephyr/mgmt/hawkbit/event.h>
#include <zephyr/mgmt/hawkbit/hawkbit.h>
#include <zephyr/sys/reboot.h>

#include "app_net.h"
#include "app_status.h"

LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

#define NET_READY_TIMEOUT K_SECONDS(CONFIG_APP_NET_READY_TIMEOUT_SEC)

static const struct gpio_dt_spec poe_detect = GPIO_DT_SPEC_GET(DT_NODELABEL(poe_detect), gpios);
static const struct gpio_dt_spec usb_detect = GPIO_DT_SPEC_GET(DT_NODELABEL(usb_detect), gpios);

static void log_power_source(void)
{
	int poe, usb;

	if (!gpio_is_ready_dt(&poe_detect) || !gpio_is_ready_dt(&usb_detect)) {
		LOG_WRN("Power sense GPIOs not ready");
		return;
	}

	if (gpio_pin_configure_dt(&poe_detect, GPIO_INPUT) < 0 ||
	    gpio_pin_configure_dt(&usb_detect, GPIO_INPUT) < 0) {
		LOG_WRN("Failed to configure power sense GPIOs");
		return;
	}

	poe = gpio_pin_get_dt(&poe_detect);
	usb = gpio_pin_get_dt(&usb_detect);

	LOG_INF("Power source: PoE=%s USB=%s", poe == 1 ? "yes" : "no", usb == 1 ? "yes" : "no");
}

#ifdef CONFIG_HAWKBIT_EVENT_CALLBACKS
static void hawkbit_event_handler(struct hawkbit_event_callback *cb, enum hawkbit_event_type event)
{
	ARG_UNUSED(cb);

	switch (event) {
	case HAWKBIT_EVENT_START_DOWNLOAD:
		LOG_INF("hawkBit: download started");
		app_status_set(APP_STATUS_UPDATING);
		break;

	case HAWKBIT_EVENT_END_DOWNLOAD:
		LOG_INF("hawkBit: download finished");
		break;

	case HAWKBIT_EVENT_UPDATE_DOWNLOADED:
		LOG_INF("hawkBit: update staged in slot 1, rebooting to apply");
		break;

	case HAWKBIT_EVENT_NO_UPDATE:
		LOG_DBG("hawkBit: no update available");
		app_status_set(APP_STATUS_ONLINE);
		break;

	case HAWKBIT_EVENT_CONFIRMED_CURRENT_IMAGE:
		LOG_INF("hawkBit: running image confirmed");
		break;

	case HAWKBIT_EVENT_ERROR_NETWORKING:
		LOG_ERR("hawkBit: networking error");
		app_status_set(APP_STATUS_ERROR);
		break;

	case HAWKBIT_EVENT_ERROR_DOWNLOAD:
		LOG_ERR("hawkBit: download error");
		app_status_set(APP_STATUS_ERROR);
		break;

	case HAWKBIT_EVENT_ERROR:
		LOG_ERR("hawkBit: generic error");
		app_status_set(APP_STATUS_ERROR);
		break;

	default:
		break;
	}
}

static HAWKBIT_EVENT_CREATE_CALLBACK(hb_cb_start_dl, hawkbit_event_handler,
				     HAWKBIT_EVENT_START_DOWNLOAD);
static HAWKBIT_EVENT_CREATE_CALLBACK(hb_cb_end_dl, hawkbit_event_handler,
				     HAWKBIT_EVENT_END_DOWNLOAD);
static HAWKBIT_EVENT_CREATE_CALLBACK(hb_cb_downloaded, hawkbit_event_handler,
				     HAWKBIT_EVENT_UPDATE_DOWNLOADED);
static HAWKBIT_EVENT_CREATE_CALLBACK(hb_cb_no_update, hawkbit_event_handler,
				     HAWKBIT_EVENT_NO_UPDATE);
static HAWKBIT_EVENT_CREATE_CALLBACK(hb_cb_confirmed, hawkbit_event_handler,
				     HAWKBIT_EVENT_CONFIRMED_CURRENT_IMAGE);
static HAWKBIT_EVENT_CREATE_CALLBACK(hb_cb_err_net, hawkbit_event_handler,
				     HAWKBIT_EVENT_ERROR_NETWORKING);
static HAWKBIT_EVENT_CREATE_CALLBACK(hb_cb_err_dl, hawkbit_event_handler,
				     HAWKBIT_EVENT_ERROR_DOWNLOAD);
static HAWKBIT_EVENT_CREATE_CALLBACK(hb_cb_err, hawkbit_event_handler, HAWKBIT_EVENT_ERROR);

static void hawkbit_register_callbacks(void)
{
	hawkbit_event_add_callback(&hb_cb_start_dl);
	hawkbit_event_add_callback(&hb_cb_end_dl);
	hawkbit_event_add_callback(&hb_cb_downloaded);
	hawkbit_event_add_callback(&hb_cb_no_update);
	hawkbit_event_add_callback(&hb_cb_confirmed);
	hawkbit_event_add_callback(&hb_cb_err_net);
	hawkbit_event_add_callback(&hb_cb_err_dl);
	hawkbit_event_add_callback(&hb_cb_err);
}
#else
static void hawkbit_register_callbacks(void)
{
}
#endif /* CONFIG_HAWKBIT_EVENT_CALLBACKS */

int main(void)
{
	int ret;

	app_status_set(APP_STATUS_BOOTING);

	LOG_INF("Waveshare RP2350-POE-ETH test application");
	LOG_INF("Firmware version " APP_VERSION_STRING ", built " __DATE__ " " __TIME__);

	log_power_source();

	ret = app_net_init();
	if (ret < 0) {
		app_status_set(APP_STATUS_ERROR);
		return ret;
	}

	ret = app_net_wait_for_address(NET_READY_TIMEOUT);
	if (ret < 0) {
		/*
		 * Keep going anyway: the autohandler retries, and the shell is
		 * still usable for diagnosing the link.
		 */
		app_status_set(APP_STATUS_ERROR);
	}

	hawkbit_register_callbacks();

	ret = hawkbit_init();
	if (ret < 0) {
		LOG_ERR("Failed to init hawkBit: %d", ret);
		app_status_set(APP_STATUS_ERROR);
		return ret;
	}

	LOG_INF("hawkBit polling %s:%d every %d minute(s)", CONFIG_HAWKBIT_SERVER,
		CONFIG_HAWKBIT_PORT, CONFIG_HAWKBIT_POLL_INTERVAL);

	hawkbit_autohandler(true);

	return 0;
}
