# USB device stack (`usbd`, "device_next")

Zephyr has **two** USB device stacks. Everything below is the newer
`usbd` stack (`CONFIG_USB_DEVICE_STACK_NEXT`), which is what new code should
use and what `<zephyr/usb/usbd.h>` declares.

| Stack | Kconfig | Header | Status |
|-------|---------|--------|--------|
| device_next (`usbd_*`) | `CONFIG_USB_DEVICE_STACK_NEXT` | `<zephyr/usb/usbd.h>` | current; use this |
| legacy (`usb_*`) | `CONFIG_USB_DEVICE_STACK` | `<zephyr/usb/usb_device.h>` | maintenance only; parts are `__deprecated` |

The two are mutually exclusive and share no API. **Check which one an app
selects before copying code into it** — see the trap at the bottom, which is a
live example from this repo.

## Contents

1. [Minimum working device](#minimum)
2. [Descriptors](#descriptors)
3. [Registering classes](#classes)
4. [Lifecycle and messages](#lifecycle)
5. [Writing a class (4.5 control-transfer rules)](#writing-a-class)
6. [Kconfig](#kconfig)
7. [Traps](#traps)

## <a name="minimum"></a>Minimum working device

The stack needs a UDC (USB device controller) node, a device context, at least
one configuration, and the string descriptors the host asks for at enumeration.

```c
#include <zephyr/usb/usbd.h>
#include <zephyr/drivers/usb/udc.h>

#define APP_USB_VID 0x2fe3
#define APP_USB_PID 0x0001

/* Bind the context to the board's UDC node (conventionally `zephyr_udc0`) */
USBD_DEVICE_DEFINE(app_usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
                   APP_USB_VID, APP_USB_PID);

USBD_DESC_LANG_DEFINE(app_lang);
USBD_DESC_MANUFACTURER_DEFINE(app_mfr, "Acme");
USBD_DESC_PRODUCT_DEFINE(app_product, "Widget");
USBD_DESC_SERIAL_NUMBER_DEFINE(app_sn);      /* derived from hwinfo */
USBD_DESC_CONFIG_DEFINE(fs_cfg_desc, "FS Configuration");

/* bmAttributes, bMaxPower (in 2 mA units), config string descriptor */
USBD_CONFIGURATION_DEFINE(app_fs_config, USB_SCD_SELF_POWERED, 250, &fs_cfg_desc);

static int app_usb_init(void)
{
    int err;

    /* Descriptors first — order matters, index 0 must be the language desc */
    err = usbd_add_descriptor(&app_usbd, &app_lang);
    err |= usbd_add_descriptor(&app_usbd, &app_mfr);
    err |= usbd_add_descriptor(&app_usbd, &app_product);
    err |= usbd_add_descriptor(&app_usbd, &app_sn);
    if (err) {
        return err;
    }

    /* cfg value (bConfigurationValue) is 1-based, not 0 */
    err = usbd_add_configuration(&app_usbd, USBD_SPEED_FS, &app_fs_config);
    if (err) {
        return err;
    }

    /* 4th arg is a NULL-terminated blocklist of class names to skip.
     * Pass NULL to register every class built into the image.
     */
    err = usbd_register_all_classes(&app_usbd, USBD_SPEED_FS, 1, NULL);
    if (err) {
        return err;
    }

    err = usbd_init(&app_usbd);
    if (err) {
        return err;
    }

    return usbd_enable(&app_usbd);
}
```

`usbd_init()` validates and finalises the descriptor set; `usbd_enable()`
attaches to the bus. They are separate so you can register a message callback
in between.

## <a name="descriptors"></a>Descriptors

| Macro | Purpose |
|-------|---------|
| `USBD_DESC_LANG_DEFINE(name)` | String descriptor index 0 (language IDs). Required. |
| `USBD_DESC_MANUFACTURER_DEFINE(name, str)` | iManufacturer |
| `USBD_DESC_PRODUCT_DEFINE(name, str)` | iProduct |
| `USBD_DESC_SERIAL_NUMBER_DEFINE(name)` | iSerialNumber, generated from `hwinfo` |
| `USBD_DESC_CONFIG_DEFINE(name, str)` | Per-configuration string |
| `USBD_DESC_STRING_DEFINE(name, str, utype)` | Arbitrary string descriptor |
| `USBD_DESC_BOS_DEFINE(name, len, subset)` | Binary Object Store capability (needs `CONFIG_USBD_BOS_SUPPORT`) |

Runtime overrides, useful when IDs come from settings rather than build config:
`usbd_device_set_vid()`, `usbd_device_set_pid()`, `usbd_device_set_bcd_usb()`,
`usbd_device_set_bcd_device()`, `usbd_device_set_code_triple()`.

Set the code triple to `USB_BCC_MISCELLANEOUS / 0x02 / 0x01` when a class spans
multiple interfaces so the host reads the Interface Association Descriptor.

## <a name="classes"></a>Registering classes

`usbd_register_all_classes()` picks up every class compiled into the image.
For finer control register individually by the name the class declared:

```c
err = usbd_register_class(&app_usbd, "cdc_acm_0", USBD_SPEED_FS, 1);
```

Or exclude specific ones while taking the rest:

```c
static const char *const blocklist[] = { "cdc_acm_0", NULL };  /* NULL-terminated */

err = usbd_register_all_classes(&app_usbd, USBD_SPEED_FS, 1, blocklist);
```

In-tree classes and their Kconfig:

| Class | Kconfig |
|-------|---------|
| CDC ACM (serial) | `CONFIG_USBD_CDC_ACM_CLASS` |
| CDC ECM / NCM (ethernet) | `CONFIG_USBD_CDC_ECM_CLASS` / `CONFIG_USBD_CDC_NCM_CLASS` |
| HID | `CONFIG_USBD_HID_SUPPORT` |
| Mass storage | `CONFIG_USBD_MSC_CLASS` |
| Audio class 2 | `CONFIG_USBD_AUDIO2_CLASS` |
| DFU | `CONFIG_USBD_DFU` (+ `CONFIG_USBD_DFU_FLASH` for the flash backend) |
| Bluetooth HCI | `CONFIG_USBD_BT_HCI` |

For a high-speed capable controller, add a second configuration and register
the classes against it too:

```c
if (usbd_caps_speed(&app_usbd) == USBD_SPEED_HS) {
    usbd_add_configuration(&app_usbd, USBD_SPEED_HS, &app_hs_config);
    usbd_register_all_classes(&app_usbd, USBD_SPEED_HS, 1, NULL);
}
```

## <a name="lifecycle"></a>Lifecycle and messages

Register a callback between `usbd_init()` and `usbd_enable()` to observe bus
events:

```c
static void usb_msg_cb(struct usbd_context *const ctx, const struct usbd_msg *msg)
{
    switch (msg->type) {
    case USBD_MSG_CONFIGURATION:      /* host selected a configuration */
    case USBD_MSG_RESET:
    case USBD_MSG_SUSPEND:
    case USBD_MSG_RESUME:
    case USBD_MSG_VBUS_READY:
    case USBD_MSG_VBUS_REMOVED:
    case USBD_MSG_UDC_ERROR:
    case USBD_MSG_STACK_ERROR:
    case USBD_MSG_CDC_ACM_LINE_CODING:
    case USBD_MSG_CDC_ACM_CONTROL_LINE_STATE:
    case USBD_MSG_DFU_APP_DETACH:
    case USBD_MSG_DFU_DOWNLOAD_COMPLETED:
        break;
    default:
        break;
    }
}

usbd_msg_register_cb(&app_usbd, usb_msg_cb);
```

Use `usbd_msg_type_string(msg->type)` for a printable name.

**Callback context depends on Kconfig.** With `CONFIG_USBD_MSG_DEFERRED_MODE`
the callback runs from a workqueue; without it, it runs in the USB stack's
own context, where blocking is not allowed. Don't do filesystem or long work
there unless deferred mode is on.

On a bus-powered board, wait for `USBD_MSG_VBUS_READY` before `usbd_enable()`
rather than enabling unconditionally at boot.

Other lifecycle calls: `usbd_disable()`, `usbd_shutdown()`,
`usbd_wakeup_request()` (remote wakeup, needs the RWUP config attribute),
`usbd_bus_speed()` (negotiated) vs `usbd_caps_speed()` (controller capability).

## <a name="writing-a-class"></a>Writing a class — 4.5 control-transfer rules

Zephyr 4.5 changed the control-transfer callbacks in `struct usbd_class_api` in
ways that silently break out-of-tree classes. If you maintain one, these are
mandatory:

**1. `control_to_host` must allocate the data-stage buffer itself.**
It now returns `struct net_buf *`. Previously the stack pre-allocated a buffer
sized from the host's `wLength`; making the handler allocate means worst-case
memory depends on your code, not on a value the host controls.

```c
static struct net_buf *my_control_to_host(struct usbd_class_data *const c_data,
                                          const struct usb_setup_packet *const setup)
{
    /* Allocate only what you will actually return */
    struct net_buf *buf = usbd_ep_buf_alloc(c_data, MIN(setup->wLength, sizeof(my_data)));

    if (buf == NULL) {
        return NULL;   /* stack will STALL */
    }
    net_buf_add_mem(buf, my_data, MIN(setup->wLength, sizeof(my_data)));
    return buf;
}
```

**2. `control_to_dev` is now called with `buf == NULL` before the data stage.**
For host-to-device transfers with a data stage, the stack calls you once with
NULL so you can STALL before any data is received, then again with the data.
Handle the NULL case explicitly:

```c
static int my_control_to_dev(struct usbd_class_data *const c_data,
                             const struct usb_setup_packet *const setup,
                             const struct net_buf *const buf)
{
    if (buf == NULL) {
        /* Pre-data-stage: validate setup, return an error to STALL */
        return setup->wLength <= sizeof(my_data) ? 0 : -ENOTSUP;
    }
    memcpy(my_data, buf->data, MIN(buf->len, sizeof(my_data)));
    return 0;
}
```

**3. Return error codes directly; don't signal via `errno`.**
Setting `errno` to indicate a protocol error is deprecated as of 4.5.

**4. `DEVICE_API` applies to USB host controllers too.** The host-side API
struct was renamed `uhc_api` → `uhc_driver_api` and must use
`DEVICE_API(uhc, ...)`.

## <a name="kconfig"></a>Kconfig

```kconfig
CONFIG_USB_DEVICE_STACK_NEXT=y     # the usbd stack (NOT USB_DEVICE_STACK)
CONFIG_USBD_CDC_ACM_CLASS=y

# Message callback from a workqueue instead of stack context
CONFIG_USBD_MSG_DEFERRED_MODE=y

# CDC ACM tuning
CONFIG_USBD_CDC_ACM_BUF_POOL_SIZE=2048
CONFIG_USBD_CDC_ACM_STACK_SIZE=1024

# Needed for USBD_DESC_BOS_DEFINE
CONFIG_USBD_BOS_SUPPORT=y

# Serial number derived from the SoC's unique ID
CONFIG_HWINFO=y
```

A UDC node must exist and be enabled in devicetree. Most boards provide
`zephyr_udc0` as a nodelabel; if `DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0))`
fails to compile, the board has no USB device controller enabled.

For a console over USB CDC ACM, prefer the `cdc-acm-console` snippet
(`west build -S cdc-acm-console`) over hand-wiring the chosen node.

## <a name="traps"></a>Traps

- **Two stacks, one header name.** `CONFIG_USB_DEVICE_STACK` (legacy) and
  `CONFIG_USB_DEVICE_STACK_NEXT` are different APIs. Code guarded by
  `#if defined(CONFIG_USB_DEVICE_STACK_NEXT)` compiles out silently on a legacy
  build, so a stale `usbd_*` call inside that guard produces **no error until
  someone switches stacks**. Real example in this repo:
  `applications/motor_controller/src/usb.c` calls
  `usbd_register_all_classes(&app_usbd, USBD_SPEED_FS, 1)` with the pre-4.x
  three-argument form; the app builds today only because it selects
  `CONFIG_USB_DEVICE_STACK=y` and the block is compiled out. Grep for the guard
  before assuming USB code in an app is live.
- **`bConfigurationValue` is 1-based.** Passing `0` as the `cfg` argument to
  `usbd_add_configuration()`/`usbd_register_*` is not "the first configuration",
  it's the "unconfigured" value and enumeration will misbehave.
- **Language descriptor must be added first.** String descriptor index 0 is
  reserved for the language list; adding `USBD_DESC_LANG_DEFINE` after the
  others yields wrong indices in the device descriptor.
- **`bMaxPower` is in 2 mA units.** `250` means 500 mA, not 250 mA.
- **Enumeration fails with no log.** Usually a missing descriptor
  (`usbd_add_descriptor` return value ignored) or a UDC that never saw VBUS.
  `CONFIG_USBD_LOG_LEVEL_DBG=y` plus the host's `dmesg`/`lsusb -v` is the
  fastest path; see `../../zephyr-debugging/references/troubleshooting.md`.
