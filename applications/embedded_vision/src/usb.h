#ifndef __APP_USB_H__
#define __APP_USB_H__

#include <stdint.h>
#include <zephyr/usb/usbd.h>

struct usbd_context *app_usbd_init_device(usbd_msg_cb_t msg_cb);

/*
 * This function is similar to app_usbd_init_device(), but does not
 * initialise the device. It allows the application to set additional features,
 * such as additional descriptors.
 */
struct usbd_context *app_usbd_setup_device(usbd_msg_cb_t msg_cb);

#endif /* __APP_USB_H__ */