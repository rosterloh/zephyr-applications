# GPIO

## Overview

This skill provides guidance for implementing GPIO functionality in Zephyr RTOS applications. GPIO (General Purpose Input/Output) is fundamental to embedded systems for controlling LEDs, reading buttons, and interfacing with digital peripherals.

### API Selection Decision Tree

```
Need GPIO?
├── Single pin from devicetree?
│   └── Use gpio_dt_spec + _dt() functions (RECOMMENDED)
│       └── GPIO_DT_SPEC_GET(node, prop) → gpio_pin_configure_dt() → gpio_pin_set_dt()/gpio_pin_get_dt()
│
├── Multiple pins, same port?
│   └── Use gpio_port_*() functions for efficiency
│       └── gpio_port_set_masked() / gpio_port_get()
│
└── Runtime-determined pin?
    └── Use raw API with device + pin number
        └── DEVICE_DT_GET() → gpio_pin_configure() → gpio_pin_set()/gpio_pin_get()
```

### Getting Device Reference

#### From Devicetree (Preferred)

```c
/* For nodes with gpios property (e.g., gpio-leds, gpio-keys) */
#define LED_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);

/* Check device readiness before use */
if (!gpio_is_ready_dt(&led)) {
    return -ENODEV;
}
```

#### gpio_dt_spec Structure

```c
struct gpio_dt_spec {
    const struct device *port;  /* GPIO controller device */
    gpio_pin_t pin;             /* Pin number (0-31 typically) */
    gpio_dt_flags_t dt_flags;   /* Flags from devicetree (e.g., GPIO_ACTIVE_LOW) */
};
```

### Common Workflows

#### 1. Basic LED Blinky

```c
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#define LED_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);

int main(void)
{
    if (!gpio_is_ready_dt(&led)) {
        return -ENODEV;
    }

    int ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
    if (ret < 0) {
        return ret;
    }

    while (1) {
        gpio_pin_toggle_dt(&led);
        k_msleep(1000);
    }
    return 0;
}
```

#### 2. Button Input (Polled)

```c
#define BUTTON_NODE DT_ALIAS(sw0)
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(BUTTON_NODE, gpios);

int main(void)
{
    if (!gpio_is_ready_dt(&button)) {
        return -ENODEV;
    }

    gpio_pin_configure_dt(&button, GPIO_INPUT);

    while (1) {
        int val = gpio_pin_get_dt(&button);
        if (val > 0) {
            /* Button pressed (handles ACTIVE_LOW automatically) */
        }
        k_msleep(100);
    }
}
```

#### 3. Button with Interrupt

```c
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static struct gpio_callback button_cb_data;

void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    /* ISR context - keep short, use k_work for heavy processing */
    printk("Button pressed at %" PRIu32 "\n", k_cycle_get_32());
}

int main(void)
{
    if (!gpio_is_ready_dt(&button)) {
        return -ENODEV;
    }

    gpio_pin_configure_dt(&button, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);

    gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb_data);

    /* Main loop or other work */
    while (1) {
        k_msleep(1000);
    }
}
```

#### 4. Multiple Pins Configuration

```c
/* Define multiple GPIO specs */
static const struct gpio_dt_spec leds[] = {
    GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios),
};

int init_leds(void)
{
    for (int i = 0; i < ARRAY_SIZE(leds); i++) {
        if (!gpio_is_ready_dt(&leds[i])) {
            return -ENODEV;
        }
        int ret = gpio_pin_configure_dt(&leds[i], GPIO_OUTPUT_INACTIVE);
        if (ret < 0) {
            return ret;
        }
    }
    return 0;
}
```

#### 5. Bidirectional Pin (Input/Output switching)

```c
static const struct gpio_dt_spec data_pin = GPIO_DT_SPEC_GET(DT_NODELABEL(data_gpio), gpios);

void send_data(uint8_t bit)
{
    gpio_pin_configure_dt(&data_pin, GPIO_OUTPUT);
    gpio_pin_set_dt(&data_pin, bit);
}

int read_data(void)
{
    gpio_pin_configure_dt(&data_pin, GPIO_INPUT);
    return gpio_pin_get_dt(&data_pin);
}
```

### Configuration

#### Essential Kconfig

```kconfig
CONFIG_GPIO=y                    # Enable GPIO driver subsystem (required)
# CONFIG_GPIO_LOG_LEVEL_DBG=y    # Enable for debugging
```

#### Devicetree Examples

**LED definition:**
```dts
/ {
    aliases {
        led0 = &green_led;
    };

    leds {
        compatible = "gpio-leds";
        green_led: led_0 {
            gpios = <&gpio0 13 GPIO_ACTIVE_LOW>;
            label = "Green LED";
        };
    };
};
```

**Button definition:**
```dts
/ {
    aliases {
        sw0 = &user_button;
    };

    buttons {
        compatible = "gpio-keys";
        user_button: button_0 {
            gpios = <&gpio0 11 (GPIO_PULL_UP | GPIO_ACTIVE_LOW)>;
            label = "User Button";
        };
    };
};
```

**Generic GPIO:**
```dts
/ {
    my_gpios {
        compatible = "gpio-leds";  /* Reuse for any GPIO */
        data_gpio: data {
            gpios = <&gpio0 5 GPIO_ACTIVE_HIGH>;
        };
    };
};
```

### GPIO Flags Reference

| Flag | Value | Description |
|------|-------|-------------|
| `GPIO_INPUT` | 0x1 | Pin configured as input |
| `GPIO_OUTPUT` | 0x2 | Pin configured as output |
| `GPIO_OUTPUT_INACTIVE` | 0x2 | Output initially inactive |
| `GPIO_OUTPUT_ACTIVE` | 0x3 | Output initially active |
| `GPIO_ACTIVE_LOW` | 0x10 | Logical active = physical low |
| `GPIO_PULL_UP` | 0x100 | Enable internal pull-up |
| `GPIO_PULL_DOWN` | 0x200 | Enable internal pull-down |

#### Interrupt Flags

| Flag | Description |
|------|-------------|
| `GPIO_INT_EDGE_RISING` | Trigger on rising edge |
| `GPIO_INT_EDGE_FALLING` | Trigger on falling edge |
| `GPIO_INT_EDGE_BOTH` | Trigger on both edges |
| `GPIO_INT_EDGE_TO_ACTIVE` | Edge toward active (respects ACTIVE_LOW) |
| `GPIO_INT_EDGE_TO_INACTIVE` | Edge toward inactive |
| `GPIO_INT_LEVEL_ACTIVE` | Level active interrupt |

### Interrupt Handling Pattern

#### With Work Queue (Recommended for complex handling)

```c
static struct k_work button_work;
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

void button_work_handler(struct k_work *work)
{
    /* Safe to do complex operations here */
    printk("Button handled in work queue\n");
}

void button_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    k_work_submit(&button_work);
}

int main(void)
{
    k_work_init(&button_work, button_work_handler);

    gpio_pin_configure_dt(&button, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);

    static struct gpio_callback cb;
    gpio_init_callback(&cb, button_isr, BIT(button.pin));
    gpio_add_callback(button.port, &cb);

    return 0;
}
```

#### Debouncing with Delayed Work

```c
static struct k_work_delayable debounce_work;
#define DEBOUNCE_MS 50

void debounce_handler(struct k_work *work)
{
    int val = gpio_pin_get_dt(&button);
    if (val > 0) {
        /* Confirmed button press */
    }
}

void button_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    k_work_reschedule(&debounce_work, K_MSEC(DEBOUNCE_MS));
}
```

### Error Handling

All GPIO functions return negative errno on failure:

```c
int ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
if (ret < 0) {
    LOG_ERR("Failed to configure GPIO: %d", ret);
    return ret;
}
```

Common errors:
- `-ENODEV`: Device not ready or not found
- `-ENOTSUP`: Configuration not supported by hardware
- `-EINVAL`: Invalid pin number or flags

### Troubleshooting

| Issue | Check |
|-------|-------|
| `gpio_is_ready_dt()` returns false | Verify devicetree node exists, GPIO controller enabled in Kconfig |
| Pin doesn't toggle | Check ACTIVE_LOW flag matches hardware, verify pin not used elsewhere |
| Interrupt not firing | Confirm `gpio_pin_interrupt_configure_dt()` called, check callback registered |
| Wrong logic level | Active-low LEDs need `GPIO_ACTIVE_LOW` in devicetree |
| Multiple callbacks not working | Each callback struct must be unique, use BIT(pin) correctly |

### References

- [#api](#api) - Complete GPIO API function reference
- [#devicetree](#devicetree) - Devicetree bindings and properties
- [#kconfig](#kconfig) - All GPIO Kconfig options

## Api

Complete reference for Zephyr GPIO driver API functions.

### Core Structures

#### gpio_dt_spec

Container for GPIO pin information from devicetree.

```c
struct gpio_dt_spec {
    const struct device *port;  /* GPIO controller device pointer */
    gpio_pin_t pin;             /* Pin number (0-31 typical) */
    gpio_dt_flags_t dt_flags;   /* Flags from devicetree */
};
```

#### gpio_callback

Callback structure for interrupt handling.

```c
struct gpio_callback {
    sys_snode_t node;                                           /* Linked list node */
    gpio_callback_handler_t handler;                            /* Callback function */
    gpio_port_pins_t pin_mask;                                  /* Pins triggering callback */
};

/* Callback handler signature */
typedef void (*gpio_callback_handler_t)(const struct device *port,
                                        struct gpio_callback *cb,
                                        gpio_port_pins_t pins);
```

### Devicetree Macros

#### GPIO_DT_SPEC_GET

Get gpio_dt_spec from devicetree node property.

```c
GPIO_DT_SPEC_GET(node_id, prop)
GPIO_DT_SPEC_GET_BY_IDX(node_id, prop, idx)  /* For phandle arrays */
GPIO_DT_SPEC_GET_OR(node_id, prop, default_value)
```

**Example:**
```c
#define LED_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);
```

#### GPIO_DT_SPEC_INST_GET

Get gpio_dt_spec using instance number (for drivers).

```c
GPIO_DT_SPEC_INST_GET(inst, prop)
GPIO_DT_SPEC_INST_GET_BY_IDX(inst, prop, idx)
```

### Pin Configuration

#### gpio_pin_configure_dt

Configure pin using devicetree spec.

```c
int gpio_pin_configure_dt(const struct gpio_dt_spec *spec, gpio_flags_t extra_flags);
```

**Parameters:**
- `spec`: GPIO specification from devicetree
- `extra_flags`: Additional flags to OR with dt_flags

**Returns:** 0 on success, negative errno on failure

**Example:**
```c
gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
gpio_pin_configure_dt(&button, GPIO_INPUT);
```

#### gpio_pin_configure

Configure pin using raw device/pin.

```c
int gpio_pin_configure(const struct device *port, gpio_pin_t pin, gpio_flags_t flags);
```

### Pin Read/Write

#### gpio_pin_get_dt / gpio_pin_get

Read logical pin value (respects ACTIVE_LOW).

```c
int gpio_pin_get_dt(const struct gpio_dt_spec *spec);
int gpio_pin_get(const struct device *port, gpio_pin_t pin);
```

**Returns:**
- 0: Inactive (low for ACTIVE_HIGH, high for ACTIVE_LOW)
- 1: Active
- Negative: Error

#### gpio_pin_get_raw

Read physical pin value (ignores ACTIVE_LOW).

```c
int gpio_pin_get_raw(const struct device *port, gpio_pin_t pin);
```

**Returns:** 0 or 1 for physical level, negative on error

#### gpio_pin_set_dt / gpio_pin_set

Set logical pin value (respects ACTIVE_LOW).

```c
int gpio_pin_set_dt(const struct gpio_dt_spec *spec, int value);
int gpio_pin_set(const struct device *port, gpio_pin_t pin, int value);
```

**Parameters:**
- `value`: 0 for inactive, non-zero for active

#### gpio_pin_set_raw

Set physical pin value (ignores ACTIVE_LOW).

```c
int gpio_pin_set_raw(const struct device *port, gpio_pin_t pin, int value);
```

#### gpio_pin_toggle_dt / gpio_pin_toggle

Toggle pin output.

```c
int gpio_pin_toggle_dt(const struct gpio_dt_spec *spec);
int gpio_pin_toggle(const struct device *port, gpio_pin_t pin);
```

### Port Operations

For multi-pin operations on same port.

#### gpio_port_get_raw

Read all pins on port.

```c
int gpio_port_get_raw(const struct device *port, gpio_port_value_t *value);
```

#### gpio_port_set_masked_raw

Set multiple pins with mask.

```c
int gpio_port_set_masked_raw(const struct device *port,
                             gpio_port_pins_t mask,
                             gpio_port_value_t value);
```

#### gpio_port_set_bits_raw / gpio_port_clear_bits_raw

Set or clear specific bits.

```c
int gpio_port_set_bits_raw(const struct device *port, gpio_port_pins_t pins);
int gpio_port_clear_bits_raw(const struct device *port, gpio_port_pins_t pins);
```

#### gpio_port_toggle_bits

Toggle multiple pins.

```c
int gpio_port_toggle_bits(const struct device *port, gpio_port_pins_t pins);
```

### Interrupt Configuration

#### gpio_pin_interrupt_configure_dt

Configure interrupt using devicetree spec.

```c
int gpio_pin_interrupt_configure_dt(const struct gpio_dt_spec *spec, gpio_flags_t flags);
```

**Common flag combinations:**
- `GPIO_INT_EDGE_TO_ACTIVE`: Interrupt on transition to active state
- `GPIO_INT_EDGE_TO_INACTIVE`: Interrupt on transition to inactive state
- `GPIO_INT_EDGE_BOTH`: Interrupt on any transition
- `GPIO_INT_LEVEL_ACTIVE`: Level-triggered when active
- `GPIO_INT_DISABLE`: Disable interrupt

#### gpio_pin_interrupt_configure

Configure interrupt using raw device/pin.

```c
int gpio_pin_interrupt_configure(const struct device *port,
                                 gpio_pin_t pin,
                                 gpio_flags_t flags);
```

### Callback Management

#### gpio_init_callback

Initialize callback structure.

```c
void gpio_init_callback(struct gpio_callback *callback,
                        gpio_callback_handler_t handler,
                        gpio_port_pins_t pin_mask);
```

**Parameters:**
- `callback`: Callback struct to initialize
- `handler`: Function to call on interrupt
- `pin_mask`: Bitmask of pins (use `BIT(pin)`)

**Example:**
```c
static struct gpio_callback button_cb;
gpio_init_callback(&button_cb, button_handler, BIT(button.pin));
```

#### gpio_add_callback

Register callback with GPIO port.

```c
int gpio_add_callback(const struct device *port, struct gpio_callback *callback);
```

#### gpio_remove_callback

Unregister callback.

```c
int gpio_remove_callback(const struct device *port, struct gpio_callback *callback);
```

### Device Readiness

#### gpio_is_ready_dt

Check if GPIO device is ready.

```c
bool gpio_is_ready_dt(const struct gpio_dt_spec *spec);
```

**Example:**
```c
if (!gpio_is_ready_dt(&led)) {
    return -ENODEV;
}
```

### Configuration Flags

#### Direction Flags

| Flag | Value | Description |
|------|-------|-------------|
| `GPIO_INPUT` | (1 << 0) | Configure as input |
| `GPIO_OUTPUT` | (1 << 1) | Configure as output |
| `GPIO_OUTPUT_INACTIVE` | GPIO_OUTPUT | Output, initial inactive |
| `GPIO_OUTPUT_ACTIVE` | GPIO_OUTPUT \| GPIO_OUTPUT_INIT_HIGH | Output, initial active |
| `GPIO_OUTPUT_LOW` | GPIO_OUTPUT \| GPIO_OUTPUT_INIT_LOW | Output, initial low |
| `GPIO_OUTPUT_HIGH` | GPIO_OUTPUT \| GPIO_OUTPUT_INIT_HIGH | Output, initial high |

#### Active Level Flags

| Flag | Value | Description |
|------|-------|-------------|
| `GPIO_ACTIVE_LOW` | (1 << 4) | Active = physical low |
| `GPIO_ACTIVE_HIGH` | 0 | Active = physical high (default) |

#### Pull Resistor Flags

| Flag | Value | Description |
|------|-------|-------------|
| `GPIO_PULL_UP` | (1 << 8) | Enable pull-up |
| `GPIO_PULL_DOWN` | (1 << 9) | Enable pull-down |

#### Drive Mode Flags

| Flag | Description |
|------|-------------|
| `GPIO_OPEN_DRAIN` | Open-drain output |
| `GPIO_OPEN_SOURCE` | Open-source output |

#### Interrupt Trigger Flags

| Flag | Description |
|------|-------------|
| `GPIO_INT_DISABLE` | Disable interrupt |
| `GPIO_INT_EDGE_RISING` | Rising edge trigger |
| `GPIO_INT_EDGE_FALLING` | Falling edge trigger |
| `GPIO_INT_EDGE_BOTH` | Both edges trigger |
| `GPIO_INT_LEVEL_LOW` | Low level trigger |
| `GPIO_INT_LEVEL_HIGH` | High level trigger |
| `GPIO_INT_EDGE_TO_ACTIVE` | Edge to active (respects ACTIVE_LOW) |
| `GPIO_INT_EDGE_TO_INACTIVE` | Edge to inactive |
| `GPIO_INT_LEVEL_ACTIVE` | Level active |
| `GPIO_INT_LEVEL_INACTIVE` | Level inactive |

### Error Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| -ENODEV | Device not found or not ready |
| -ENOTSUP | Operation not supported |
| -EINVAL | Invalid argument (pin number, flags) |
| -EIO | I/O error |
| -EBUSY | Resource busy |

## Devicetree

Complete reference for GPIO devicetree bindings and properties.

### GPIO Controller Binding

Base binding: `dts/bindings/gpio/gpio-controller.yaml`

#### Required Properties

| Property | Type | Description |
|----------|------|-------------|
| `gpio-controller` | boolean | Marks node as GPIO controller |
| `#gpio-cells` | int | Number of cells in GPIO specifier (usually 2) |

#### Optional Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `ngpios` | int | 32 | Number of in-use GPIO slots. Set when only first N GPIOs (0...N-1) are available. |
| `gpio-reserved-ranges` | array | - | Unusable GPIO offsets as tuples (start, size). Example: `<3 2>, <10 1>` marks offsets 3, 4, 10 unavailable. |
| `gpio-line-names` | string-array | - | Names for GPIO lines (documentation/debugging). |

#### Controller Example

```dts
gpio0: gpio@50000000 {
    compatible = "nordic,nrf-gpio";
    reg = <0x50000000 0x200>;
    gpio-controller;
    #gpio-cells = <2>;
    ngpios = <16>;
    gpio-line-names = "LED1", "LED2", "BUTTON1", "", "", "", "", "",
                      "", "", "", "", "", "", "", "";
    status = "okay";
};

gpio1: gpio@50000300 {
    compatible = "nordic,nrf-gpio";
    reg = <0x50000300 0x200>;
    gpio-controller;
    #gpio-cells = <2>;
    ngpios = <16>;
    gpio-reserved-ranges = <12 4>;  /* Pins 12-15 not usable */
};
```

### GPIO Specifier Format

Standard 2-cell format: `<&controller pin flags>`

```dts
gpios = <&gpio0 13 GPIO_ACTIVE_LOW>;
/*       ^      ^  ^
         |      |  flags (from dt-bindings/gpio/gpio.h)
         |      pin number
         phandle to controller
*/
```

### gpio-leds Binding

Path: `dts/bindings/led/gpio-leds.yaml`

#### Properties

| Property | Type | Required | Description |
|----------|------|----------|-------------|
| `compatible` | string | yes | Must be "gpio-leds" |
| `gpios` | phandle-array | yes | GPIO specifier |
| `label` | string | no | Human-readable name |

#### Example

```dts
/ {
    aliases {
        led0 = &green_led;
        led1 = &red_led;
    };

    leds {
        compatible = "gpio-leds";

        green_led: led_0 {
            gpios = <&gpio0 13 GPIO_ACTIVE_LOW>;
            label = "Green LED";
        };

        red_led: led_1 {
            gpios = <&gpio0 14 GPIO_ACTIVE_LOW>;
            label = "Red LED";
        };
    };
};
```

#### Usage in Code

```c
#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
```

### gpio-keys Binding

Path: `dts/bindings/input/gpio-keys.yaml`

#### Properties

| Property | Type | Required | Description |
|----------|------|----------|-------------|
| `compatible` | string | yes | Must be "gpio-keys" |
| `debounce-interval-ms` | int | no | Debounce interval (default: 30) |
| `polling-mode` | boolean | no | Poll instead of using interrupts |
| `gpios` | phandle-array | yes | GPIO specifier |
| `label` | string | no | Human-readable name |
| `zephyr,code` | int | no | Input event code |

#### Example

```dts
/ {
    aliases {
        sw0 = &button0;
    };

    buttons {
        compatible = "gpio-keys";
        debounce-interval-ms = <50>;

        button0: button_0 {
            gpios = <&gpio0 11 (GPIO_PULL_UP | GPIO_ACTIVE_LOW)>;
            label = "User Button";
            zephyr,code = <INPUT_KEY_0>;
        };
    };
};
```

#### Usage in Code

```c
#define SW0_NODE DT_ALIAS(sw0)
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(SW0_NODE, gpios);
```

### GPIO Hogs

Requires: `CONFIG_GPIO_HOGS=y`

Auto-configure GPIOs at boot without application code.

#### Hog Properties

| Property | Type | Description |
|----------|------|-------------|
| `gpio-hog` | boolean | Marks node as GPIO hog |
| `gpios` | array | GPIO specifiers to hog |
| `input` | boolean | Configure as input |
| `output-low` | boolean | Configure as output LOW |
| `output-high` | boolean | Configure as output HIGH |
| `line-name` | string | Optional descriptive name |

#### Example

```dts
&gpio0 {
    mux-hog {
        gpio-hog;
        gpios = <10 GPIO_ACTIVE_HIGH>, <11 GPIO_ACTIVE_HIGH>;
        output-high;
        line-name = "MUX_SEL0", "MUX_SEL1";
    };

    power-enable {
        gpio-hog;
        gpios = <5 GPIO_ACTIVE_HIGH>;
        output-low;
        line-name = "POWER_EN";
    };
};
```

### GPIO Flags (dt-bindings)

Include: `<zephyr/dt-bindings/gpio/gpio.h>`

#### Active Level

| Flag | Value | Description |
|------|-------|-------------|
| `GPIO_ACTIVE_LOW` | 1 | Logical active = physical low |
| `GPIO_ACTIVE_HIGH` | 0 | Logical active = physical high |

#### Pull Configuration

| Flag | Value | Description |
|------|-------|-------------|
| `GPIO_PULL_UP` | 16 | Enable internal pull-up |
| `GPIO_PULL_DOWN` | 32 | Enable internal pull-down |

#### Drive Mode

| Flag | Value | Description |
|------|-------|-------------|
| `GPIO_OPEN_DRAIN` | 64 | Open-drain output |
| `GPIO_OPEN_SOURCE` | 128 | Open-source output |

#### Combining Flags

```dts
/* Button with pull-up, active-low */
gpios = <&gpio0 11 (GPIO_PULL_UP | GPIO_ACTIVE_LOW)>;

/* Open-drain output */
gpios = <&gpio0 5 (GPIO_OPEN_DRAIN | GPIO_ACTIVE_LOW)>;
```

### Generic GPIO Node Pattern

For arbitrary GPIO usage (not LED/button specific):

```dts
/ {
    /* Can reuse gpio-leds compatible for any GPIO output */
    custom_gpios {
        compatible = "gpio-leds";

        my_output: output_pin {
            gpios = <&gpio0 5 GPIO_ACTIVE_HIGH>;
            label = "Data Output";
        };
    };
};
```

### Accessing GPIO in Code

#### By Alias

```c
#define LED_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);
```

#### By Node Label

```c
static const struct gpio_dt_spec pin =
    GPIO_DT_SPEC_GET(DT_NODELABEL(my_output), gpios);
```

#### By Path

```c
static const struct gpio_dt_spec pin =
    GPIO_DT_SPEC_GET(DT_PATH(leds, led_0), gpios);
```

#### Checking Node Existence

```c
#if DT_NODE_EXISTS(DT_ALIAS(led0))
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
#endif
```

### Board Overlay Examples

#### Adding LED to Custom Board

```dts
/* boards/my_board.overlay */
/ {
    aliases {
        led0 = &status_led;
    };

    leds {
        compatible = "gpio-leds";
        status_led: led_status {
            gpios = <&gpio0 17 GPIO_ACTIVE_HIGH>;
            label = "Status LED";
        };
    };
};
```

#### Overriding Existing Pin

```dts
&green_led {
    gpios = <&gpio1 5 GPIO_ACTIVE_LOW>;
};
```

#### Disabling a Node

```dts
&unused_led {
    status = "disabled";
};
```

### Multiple GPIOs in One Property

```dts
my_device {
    control-gpios = <&gpio0 5 GPIO_ACTIVE_HIGH>,
                    <&gpio0 6 GPIO_ACTIVE_HIGH>,
                    <&gpio1 2 GPIO_ACTIVE_LOW>;
};
```

```c
/* Access by index */
static const struct gpio_dt_spec ctrl[] = {
    GPIO_DT_SPEC_GET_BY_IDX(DT_NODELABEL(my_device), control_gpios, 0),
    GPIO_DT_SPEC_GET_BY_IDX(DT_NODELABEL(my_device), control_gpios, 1),
    GPIO_DT_SPEC_GET_BY_IDX(DT_NODELABEL(my_device), control_gpios, 2),
};
```

### Optional GPIO

```c
static const struct gpio_dt_spec optional_pin =
    GPIO_DT_SPEC_GET_OR(DT_NODELABEL(my_node), optional_gpios, {0});

if (optional_pin.port != NULL) {
    /* GPIO is defined */
}
```

### GPIO Nexus Binding

Path: `dts/bindings/gpio/gpio-nexus.yaml`

For GPIO mapping/redirection between controllers.

| Property | Type | Description |
|----------|------|-------------|
| `gpio-map` | compound | GPIO mapping entries |
| `gpio-map-mask` | array | Mask for matching specifiers |
| `gpio-map-pass-thru` | array | Flags to pass through |
| `#gpio-cells` | int | Cells in GPIO specifiers |

#### Example

```dts
gpio_mux: gpio-mux {
    compatible = "gpio-nexus";
    #gpio-cells = <2>;

    gpio-map =
        <0 0 &gpio0 1 0>,
        <1 0 &gpio0 2 0>,
        <2 0 &gpio1 5 0>;

    gpio-map-mask = <0xF 0x0>;
    gpio-map-pass-thru = <0x0 0x7>;
};

/* Usage: reference the mux instead of individual GPIO controllers */
led {
    gpios = <&gpio_mux 0 GPIO_ACTIVE_HIGH>;  /* Maps to &gpio0 1 */
};
```

## Kconfig

Complete reference for GPIO-related Kconfig options.

### Core Options

#### CONFIG_GPIO

Enable GPIO driver subsystem.

```kconfig
CONFIG_GPIO=y
```

**Type:** bool
**Default:** n
**Required:** Yes, for any GPIO usage

#### CONFIG_GPIO_INIT_PRIORITY

GPIO device initialization priority.

```kconfig
CONFIG_GPIO_INIT_PRIORITY=40
```

**Type:** int
**Default:** 40
**Range:** 0-99

Higher priority (lower number) initializes earlier. Default 40 runs after basic bus drivers.

### Logging Options

#### CONFIG_GPIO_LOG_LEVEL

Set GPIO subsystem log level.

```kconfig
# Options: LOG_LEVEL_NONE, LOG_LEVEL_ERR, LOG_LEVEL_WRN, LOG_LEVEL_INF, LOG_LEVEL_DBG
CONFIG_GPIO_LOG_LEVEL_DBG=y
```

**Shortcuts:**
```kconfig
CONFIG_GPIO_LOG_LEVEL_OFF=y   # No logging
CONFIG_GPIO_LOG_LEVEL_ERR=y   # Errors only
CONFIG_GPIO_LOG_LEVEL_WRN=y   # Warnings and errors
CONFIG_GPIO_LOG_LEVEL_INF=y   # Info, warnings, errors
CONFIG_GPIO_LOG_LEVEL_DBG=y   # All messages including debug
```

### Shell Commands

#### CONFIG_GPIO_SHELL

Enable GPIO shell commands for debugging.

```kconfig
CONFIG_GPIO_SHELL=y
CONFIG_SHELL=y  # Required dependency
```

**Type:** bool
**Default:** n

**Available shell commands:**
```
gpio conf <device> <pin> <mode>   # Configure pin
gpio get <device> <pin>           # Read pin
gpio set <device> <pin> <value>   # Write pin
gpio blink <device> <pin>         # Toggle pin
```

### Hardware-Specific Options

#### Nordic nRF

```kconfig
CONFIG_GPIO_NRF=y           # Nordic GPIO driver
CONFIG_NRF_GPIO_MISC=y      # Extra GPIO features
```

#### STM32

```kconfig
CONFIG_GPIO_STM32=y         # STM32 GPIO driver
```

#### ESP32

```kconfig
CONFIG_GPIO_ESP32=y         # ESP32 GPIO driver
```

#### NXP

```kconfig
CONFIG_GPIO_MCUX=y          # NXP Kinetis GPIO
CONFIG_GPIO_MCUX_LPC=y      # NXP LPC GPIO
CONFIG_GPIO_MCUX_IGPIO=y    # NXP i.MX GPIO
```

### GPIO Expanders

#### I2C GPIO Expanders

```kconfig
CONFIG_GPIO_PCA95XX=y       # NXP PCA95xx series (PCA9535, PCA9555, etc.)
CONFIG_GPIO_PCAL6524=y      # NXP PCAL6524
CONFIG_GPIO_MCP23S17=y      # Microchip MCP23017/MCP23S17
CONFIG_GPIO_SX1509B=y       # Semtech SX1509B
```

#### SPI GPIO Expanders

```kconfig
CONFIG_GPIO_MCP23SXX=y      # Microchip MCP23Sxx SPI series
```

#### Interrupt Support for Expanders

```kconfig
CONFIG_GPIO_PCA95XX_INTERRUPT=y  # Enable interrupt support
```

### Debug Options

#### CONFIG_GPIO_HOGS

Enable GPIO hogs (auto-configured GPIOs from devicetree).

```kconfig
CONFIG_GPIO_HOGS=y
```

Allows devicetree to specify GPIOs that should be configured at boot:

```dts
&gpio0 {
    gpio-hog-example {
        gpio-hog;
        gpios = <13 GPIO_ACTIVE_HIGH>;
        output-high;
        line-name = "power-enable";
    };
};
```

#### CONFIG_GPIO_GET_DIRECTION

Enable `gpio_pin_get_direction()` API.

```kconfig
CONFIG_GPIO_GET_DIRECTION=y
```

**Type:** bool
**Default:** n

Enables querying pin direction at runtime:
```c
int dir = gpio_pin_get_direction(port, pin);
/* Returns GPIO_INPUT, GPIO_OUTPUT, or 0 */
```

#### CONFIG_GPIO_GET_CONFIG

Enable `gpio_pin_get_config()` API.

```kconfig
CONFIG_GPIO_GET_CONFIG=y
```

Enables querying full pin configuration at runtime.

### Typical Configurations

#### Minimal Application

```kconfig
CONFIG_GPIO=y
```

#### Development/Debug

```kconfig
CONFIG_GPIO=y
CONFIG_GPIO_LOG_LEVEL_DBG=y
CONFIG_GPIO_SHELL=y
CONFIG_SHELL=y
```

#### With I2C GPIO Expander

```kconfig
CONFIG_GPIO=y
CONFIG_I2C=y
CONFIG_GPIO_PCA95XX=y
CONFIG_GPIO_PCA95XX_INTERRUPT=y
```

#### Low-Power Application

```kconfig
CONFIG_GPIO=y
CONFIG_GPIO_LOG_LEVEL_OFF=y
# Disable unused drivers
CONFIG_GPIO_SHELL=n
```

### Defconfig Examples

#### prj.conf for LED Blinky

```kconfig
# Basic GPIO support
CONFIG_GPIO=y
```

#### prj.conf for Button with Interrupt

```kconfig
CONFIG_GPIO=y
CONFIG_LOG=y
CONFIG_GPIO_LOG_LEVEL_INF=y
```

#### prj.conf for GPIO Shell Testing

```kconfig
CONFIG_GPIO=y
CONFIG_SHELL=y
CONFIG_GPIO_SHELL=y
CONFIG_SHELL_BACKEND_SERIAL=y
CONFIG_UART_CONSOLE=y
```

### Dependencies

GPIO typically requires:

```kconfig
# For interrupt-driven GPIO
CONFIG_EXTI=y               # External interrupt controller (STM32)

# For GPIO expanders
CONFIG_I2C=y                # I2C bus support
CONFIG_SPI=y                # SPI bus support
```

### Verification

Check enabled GPIO options:

```bash
# In build directory
cat zephyr/.config | grep GPIO
```

Check available GPIO devices:

```bash
# In Zephyr shell
device list
gpio conf gpio@50000000 0 in  # Try configuring pin
```
