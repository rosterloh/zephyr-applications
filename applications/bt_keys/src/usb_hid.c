/*
 * Copyright (c) 2026 Richard Osterloh
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * USB HID transport: same two reports (keyboard + consumer) as the BLE HoG
 * service, sent over a single HID class instance instead of GATT notifications.
 */

#include <string.h>

#include <zephyr/drivers/usb/udc.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/usb/class/usbd_hid.h>
#include <zephyr/usb/usbd.h>

#include <zephyr/logging/log.h>

#include "usb_hid.h"

LOG_MODULE_REGISTER(usb_hid, LOG_LEVEL_INF);

#define APP_USB_VID 0x2fe3
#define APP_USB_PID 0x0002

static const struct device *const hid_dev = DEVICE_DT_GET(DT_NODELABEL(usb_hid));

static bool hid_ready;

static void hid_iface_ready(const struct device *dev, const bool ready)
{
	ARG_UNUSED(dev);
	hid_ready = ready;
}

static const struct hid_device_ops hid_ops = {
	.iface_ready = hid_iface_ready,
};

USBD_DEVICE_DEFINE(bt_keys_usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), APP_USB_VID,
		   APP_USB_PID);
USBD_DESC_LANG_DEFINE(usbd_lang);
USBD_DESC_MANUFACTURER_DEFINE(usbd_mfr, "rosterloh");
USBD_DESC_PRODUCT_DEFINE(usbd_product, "bt_keys");
USBD_DESC_SERIAL_NUMBER_DEFINE(usbd_sn);
USBD_DESC_CONFIG_DEFINE(usbd_fs_cfg_desc, "FS Configuration");
USBD_CONFIGURATION_DEFINE(usbd_fs_config, USB_SCD_SELF_POWERED, 100, &usbd_fs_cfg_desc);

int bt_keys_usb_hid_init(void)
{
	int err;

	err = hid_device_register(hid_dev, hid_report_desc, hid_report_desc_len, &hid_ops);
	if (err) {
		LOG_ERR("hid_device_register failed (%d)", err);
		return err;
	}

	err = usbd_add_descriptor(&bt_keys_usbd, &usbd_lang);
	err |= usbd_add_descriptor(&bt_keys_usbd, &usbd_mfr);
	err |= usbd_add_descriptor(&bt_keys_usbd, &usbd_product);
	err |= usbd_add_descriptor(&bt_keys_usbd, &usbd_sn);
	if (err) {
		LOG_ERR("failed to add USB string descriptors (%d)", err);
		return err;
	}

	err = usbd_add_configuration(&bt_keys_usbd, USBD_SPEED_FS, &usbd_fs_config);
	if (err) {
		LOG_ERR("usbd_add_configuration failed (%d)", err);
		return err;
	}

	err = usbd_register_all_classes(&bt_keys_usbd, USBD_SPEED_FS, 1, NULL);
	if (err) {
		LOG_ERR("usbd_register_all_classes failed (%d)", err);
		return err;
	}

	err = usbd_init(&bt_keys_usbd);
	if (err) {
		LOG_ERR("usbd_init failed (%d)", err);
		return err;
	}

	err = usbd_enable(&bt_keys_usbd);
	if (err) {
		LOG_ERR("usbd_enable failed (%d)", err);
		return err;
	}

	LOG_INF("USB HID ready");
	return 0;
}

void usb_hid_notify_keyboard(uint8_t modifiers, const uint8_t keycodes[HID_KEYBOARD_KEYS])
{
	if (!hid_ready) {
		return;
	}

	uint8_t report[1 + 2 + HID_KEYBOARD_KEYS] = {REPORT_ID_KEYBOARD, modifiers, 0};

	memcpy(&report[3], keycodes, HID_KEYBOARD_KEYS);

	int err = hid_device_submit_report(hid_dev, sizeof(report), report);

	if (err) {
		LOG_WRN("keyboard report submit failed: %d", err);
	}
}

void usb_hid_notify_consumer(uint16_t usage)
{
	if (!hid_ready) {
		return;
	}

	uint8_t report[3] = {REPORT_ID_CONSUMER};

	sys_put_le16(usage, &report[1]);

	int err = hid_device_submit_report(hid_dev, sizeof(report), report);

	if (err) {
		LOG_WRN("consumer report submit failed: %d", err);
	}
}
