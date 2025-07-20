#include <zephyr/init.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/usb/udc.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/bos.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(usb, LOG_LEVEL_INF);

#define APP_USB_VID       0x2f5d
#define APP_USB_PID       0x2202
#define APP_USB_MAX_POWER 250 // bMaxPower value in the sample configuration in 2 mA units

#if defined(CONFIG_USB_DEVICE_STACK_NEXT)
USBD_DEVICE_DEFINE(app_usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), APP_USB_VID, APP_USB_PID);
USBD_DESC_LANG_DEFINE(app_lang);
USBD_DESC_MANUFACTURER_DEFINE(app_mfr, "ROBOTIS");
USBD_DESC_PRODUCT_DEFINE(app_product, "OpenRB-150");
// C8A0685C5055344E312E3120FF0E2134
USBD_DESC_SERIAL_NUMBER_DEFINE(app_sn);
USBD_DESC_CONFIG_DEFINE(fs_cfg_desc, "FS Configuration");
USBD_CONFIGURATION_DEFINE(app_fs_config, USB_SCD_SELF_POWERED, APP_USB_MAX_POWER, &fs_cfg_desc);

static const struct usb_bos_capability_lpm bos_cap_lpm = {
	.bLength = sizeof(struct usb_bos_capability_lpm),
	.bDescriptorType = USB_DESC_DEVICE_CAPABILITY,
	.bDevCapabilityType = USB_BOS_CAPABILITY_EXTENSION,
	.bmAttributes = 0UL,
};
USBD_DESC_BOS_DEFINE(app_usbext, sizeof(bos_cap_lpm), &bos_cap_lpm);

/*
static void usb_msg_cb(struct usbd_context *const ctx, const struct usbd_msg *msg)
{
	LOG_INF("USBD message: %s", usbd_msg_type_string(msg->type));

	if (usbd_can_detect_vbus(ctx)) {
		if (msg->type == USBD_MSG_VBUS_READY) {
			if (usbd_enable(ctx)) {
				LOG_ERR("Failed to enable device support");
			}
		}

		if (msg->type == USBD_MSG_VBUS_REMOVED) {
			if (usbd_disable(ctx)) {
				LOG_ERR("Failed to disable device support");
			}
		}
	}

	if (msg->type == USBD_MSG_CDC_ACM_CONTROL_LINE_STATE) {
		uint32_t dtr = 0U;

		uart_line_ctrl_get(msg->dev, UART_LINE_CTRL_DTR, &dtr);
		if (dtr) {
			LOG_INF("Got DTR from USB");
		}
	}

	if (msg->type == USBD_MSG_CDC_ACM_LINE_CODING) {
		uint32_t baudrate = 0U;

		uart_line_ctrl_get(msg->dev, UART_LINE_CTRL_BAUD_RATE, &baudrate);
		if (baudrate) {
			LOG_INF("Baudrate %u", baudrate);
		}
	}
}
*/
static int app_usb_init(void)
{
	int err;

	err = usbd_add_descriptor(&app_usbd, &app_lang);
	err |= usbd_add_descriptor(&app_usbd, &app_mfr);
	err |= usbd_add_descriptor(&app_usbd, &app_product);
	err |= usbd_add_descriptor(&app_usbd, &app_sn);
	if (err) {
		LOG_ERR("Failed to initialise string descriptor (%d)", err);
		return err;
	}

	err = usbd_add_configuration(&app_usbd, USBD_SPEED_FS, &app_fs_config);
	if (err) {
		LOG_ERR("Failed to add Full-Speed configuration");
		return err;
	}

	err = usbd_register_all_classes(&app_usbd, USBD_SPEED_FS, 1);
	if (err) {
		LOG_ERR("Failed to add register classes");
		return err;
	}

	/*
	 * Class with multiple interfaces have an Interface Association Descriptor
	 * available, use an appropriate triple to indicate it.
	 */
	usbd_device_set_code_triple(&app_usbd, USBD_SPEED_FS, USB_BCC_MISCELLANEOUS, 0x02, 0x01);
	/*
		err = usbd_msg_register_cb(&app_usbd, &usb_msg_cb);
		if (err) {
			LOG_ERR("Failed to register message callback");
			return err;
		}
	*/
	(void)usbd_device_set_bcd_usb(&app_usbd, USBD_SPEED_FS, 0x0201);

	err = usbd_add_descriptor(&app_usbd, &app_usbext);
	if (err) {
		LOG_ERR("Failed to add USB 2.0 Extension Descriptor");
		return err;
	}

	err = usbd_init(&app_usbd);
	if (err) {
		LOG_ERR("Failed to initialise device support");
		return err;
	}

	// if (!usbd_can_detect_vbus(&app_usbd)) {
	err = usbd_enable(&app_usbd);
	if (err) {
		LOG_ERR("Failed to enable device support");
		return err;
	}
	// }

	return err;
}

SYS_INIT(app_usb_init, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY); // APPLICATION
#endif /* defined(CONFIG_USB_DEVICE_STACK_NEXT) */