# Device Drivers

## Overview

Expert guidance on Zephyr's device driver model, creating custom drivers, using existing driver APIs, and implementing bus-specific device patterns.

### Table of Contents

1. [Quick Start](#quick-start)
2. [Core Concepts](#core-concepts)
3. [Using Existing Drivers](#using-existing-drivers)
4. [Creating Custom Drivers](#creating-custom-drivers)
5. [Bus-Specific Patterns](#bus-specific-patterns)
6. [Sensor Subsystem](#sensor-subsystem)
7. [Testing Drivers](#testing-drivers)
8. [Troubleshooting](#troubleshooting)

---

### Quick Start

#### Using an Existing Driver

```c
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

/* Get device from devicetree */
const struct device *gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));

/* Check device is ready */
if (!device_is_ready(gpio_dev)) {
    printk("GPIO device not ready\n");
    return -ENODEV;
}

/* Use driver API */
gpio_pin_configure(gpio_dev, 13, GPIO_OUTPUT_ACTIVE);
gpio_pin_set(gpio_dev, 13, 1);
```

#### Minimal Custom Driver Structure

```c
#define DT_DRV_COMPAT vendor_mydevice

struct mydevice_config {
    /* Immutable config from devicetree */
};

struct mydevice_data {
    /* Runtime mutable state */
};

static int mydevice_init(const struct device *dev)
{
    return 0;
}

#define MYDEVICE_INIT(inst)                                     \
    static struct mydevice_data mydevice_data_##inst;           \
    static const struct mydevice_config mydevice_config_##inst; \
    DEVICE_DT_INST_DEFINE(inst, mydevice_init, NULL,            \
                          &mydevice_data_##inst,                \
                          &mydevice_config_##inst,              \
                          POST_KERNEL,                          \
                          CONFIG_KERNEL_INIT_PRIORITY_DEVICE,   \
                          NULL);

DT_INST_FOREACH_STATUS_OKAY(MYDEVICE_INIT)
```

---

### Core Concepts

#### Device Model Overview

Zephyr's device model provides:
- **Static device definitions** at compile time via devicetree
- **Lazy initialization** based on initialization levels
- **API abstraction** through driver API structures
- **Power management** integration

For complete device model details: See [driver-model.md](#driver-model)

#### Key Components

| Component | Purpose | Example |
|-----------|---------|---------|
| `struct device` | Runtime device handle | `const struct device *dev` |
| Config structure | Immutable HW config | Base address, IRQ, pins |
| Data structure | Mutable runtime state | Buffers, locks, counters |
| Driver API | Subsystem operations | `gpio_driver_api`, `i2c_driver_api` |

#### Initialization Levels

Devices initialize in order:

| Level | Typical Use |
|-------|-------------|
| `EARLY` | Architecture-specific, before main memory |
| `PRE_KERNEL_1` | Drivers without dependencies |
| `PRE_KERNEL_2` | Drivers depending on PRE_KERNEL_1 |
| `POST_KERNEL` | Most drivers (default) |
| `APPLICATION` | Application-specific init |

---

### Using Existing Drivers

#### Device Acquisition Pattern

```c
/* Method 1: From node label (most common) */
const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(my_sensor));

/* Method 2: From alias */
const struct device *dev = DEVICE_DT_GET(DT_ALIAS(led0));

/* Method 3: From chosen node */
const struct device *dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

/* Method 4: From compatible (first match) */
const struct device *dev = DEVICE_DT_GET_ANY(bosch_bme280);

/* ALWAYS check readiness */
if (!device_is_ready(dev)) {
    return -ENODEV;
}
```

#### Common Driver APIs

| Subsystem | Header | Key Functions |
|-----------|--------|---------------|
| GPIO | `<zephyr/drivers/gpio.h>` | `gpio_pin_configure()`, `gpio_pin_set()`, `gpio_pin_get()` |
| I2C | `<zephyr/drivers/i2c.h>` | `i2c_write()`, `i2c_read()`, `i2c_transfer()` |
| SPI | `<zephyr/drivers/spi.h>` | `spi_transceive()`, `spi_write()`, `spi_read()` |
| UART | `<zephyr/drivers/uart.h>` | `uart_poll_out()`, `uart_irq_rx_enable()` |
| ADC | `<zephyr/drivers/adc.h>` | `adc_channel_setup()`, `adc_read()` |
| PWM | `<zephyr/drivers/pwm.h>` | `pwm_set_pulse_dt()` |
| Sensor | `<zephyr/drivers/sensor.h>` | `sensor_sample_fetch()`, `sensor_channel_get()` |

For detailed API usage: See [driver-apis.md](#driver-apis)

---

### Creating Custom Drivers

#### Driver File Structure

```
drivers/mydriver/
├── CMakeLists.txt          # Build integration
├── Kconfig                 # Configuration options
├── mydriver.c              # Driver implementation
└── dts/bindings/           # Devicetree bindings (optional location)
    └── vendor,mydriver.yaml
```

#### Step-by-Step Guide

1. **Define DT binding** (YAML)
2. **Create Kconfig** for driver
3. **Implement driver** with DEVICE_DT_INST_DEFINE
4. **Add devicetree node** to board/overlay
5. **Enable in prj.conf**

For complete walkthrough: See [driver-creation.md](#driver-creation)

#### Critical Macros

```c
/* MUST define before includes */
#define DT_DRV_COMPAT vendor_device_name

/* Get instance count */
#define NUM_INSTANCES DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT)

/* Access instance properties */
DT_INST_PROP(inst, property_name)
DT_INST_REG_ADDR(inst)
DT_INST_IRQ(inst, irq)

/* Define device for each instance */
DEVICE_DT_INST_DEFINE(inst, init_fn, pm, data, config, level, prio, api);

/* Iterate all instances */
DT_INST_FOREACH_STATUS_OKAY(MACRO_NAME)
```

---

### Bus-Specific Patterns

#### I2C Device Driver

```c
#define DT_DRV_COMPAT vendor_i2c_sensor

struct sensor_config {
    struct i2c_dt_spec i2c;
};

static int sensor_init(const struct device *dev)
{
    const struct sensor_config *cfg = dev->config;

    if (!i2c_is_ready_dt(&cfg->i2c)) {
        return -ENODEV;
    }
    /* Read chip ID, configure, etc. */
    return 0;
}

#define SENSOR_INIT(inst)                                       \
    static const struct sensor_config sensor_config_##inst = {  \
        .i2c = I2C_DT_SPEC_INST_GET(inst),                      \
    };                                                          \
    DEVICE_DT_INST_DEFINE(inst, sensor_init, NULL, NULL,        \
                          &sensor_config_##inst, POST_KERNEL,   \
                          CONFIG_SENSOR_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(SENSOR_INIT)
```

#### SPI Device Driver

```c
#define DT_DRV_COMPAT vendor_spi_device

struct device_config {
    struct spi_dt_spec spi;
};

static int device_init(const struct device *dev)
{
    const struct device_config *cfg = dev->config;

    if (!spi_is_ready_dt(&cfg->spi)) {
        return -ENODEV;
    }
    return 0;
}

#define DEVICE_INIT(inst)                                       \
    static const struct device_config device_config_##inst = {  \
        .spi = SPI_DT_SPEC_INST_GET(inst,                       \
                   SPI_WORD_SET(8) | SPI_TRANSFER_MSB, 0),      \
    };                                                          \
    DEVICE_DT_INST_DEFINE(inst, device_init, NULL, NULL,        \
                          &device_config_##inst, POST_KERNEL,   \
                          CONFIG_SPI_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(DEVICE_INIT)
```

For GPIO, UART, and more patterns: See [bus-drivers.md](#bus-drivers)

---

### Sensor Subsystem

Sensors use a standardized API with channels and triggers.

#### Implementing a Sensor Driver

```c
#define DT_DRV_COMPAT vendor_temp_sensor

static int sensor_sample_fetch(const struct device *dev,
                               enum sensor_channel chan)
{
    /* Read raw data from hardware */
    return 0;
}

static int sensor_channel_get(const struct device *dev,
                              enum sensor_channel chan,
                              struct sensor_value *val)
{
    if (chan != SENSOR_CHAN_AMBIENT_TEMP) {
        return -ENOTSUP;
    }
    /* Convert to sensor_value (val1=integer, val2=micro) */
    val->val1 = 25;
    val->val2 = 500000;  /* 25.5 degrees */
    return 0;
}

static DEVICE_API(sensor, sensor_api) = {
    .sample_fetch = sensor_sample_fetch,
    .channel_get = sensor_channel_get,
};
```

> **Always declare API instances with `DEVICE_API(class, name)`.** As of
> Zephyr 4.5 this is **mandatory** for every upstream driver class, including
> out-of-tree drivers. The macro places the struct in the class's iterable
> section, and `DEVICE_API_GET()` now asserts that the API actually belongs to
> the requested class. The old form —
> `static const struct sensor_driver_api foo = {...}` — still compiles but the
> device fails that assert at runtime.
>
> If your out-of-tree class *extends* an upstream one (embeds the upstream API
> struct as its **first member**), also register the relationship so
> `DEVICE_API_GET()` for the parent class succeeds:
>
> ```c
> struct my_sensor_driver_api {
>     struct sensor_driver_api sensor;   /* must be first */
>     int (*my_extra_op)(const struct device *dev);
> };
>
> DEVICE_API_EXTENDS(my_sensor, sensor, sensor);
> ```

For triggers, emulators, and more: See [sensor-drivers.md](#sensor-drivers)

---

### Testing Drivers

#### Ztest for Drivers

```c
#include <zephyr/ztest.h>
#include <zephyr/device.h>

ZTEST(driver_tests, test_device_ready)
{
    const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(test_device));
    zassert_true(device_is_ready(dev), "Device not ready");
}

ZTEST_SUITE(driver_tests, NULL, NULL, NULL, NULL, NULL);
```

#### Using Emulators

Enable emulation in `prj.conf`:
```
CONFIG_EMUL=y
CONFIG_I2C_EMUL=y
```

For test fixtures, fakes, and more: See [driver-testing.md](#driver-testing)

---

### Troubleshooting

| Error | Cause | Solution |
|-------|-------|----------|
| `device_is_ready()` returns false | Device not initialized | Check `status = "okay"`, Kconfig enabled |
| `__device_dts_ord_N` linker error | Driver not linked | Enable driver Kconfig, check CMakeLists.txt |
| `DT_N_...` undefined | Missing DT node | Add node to devicetree/overlay |
| Init returns `-ENODEV` | Bus not ready | Check parent bus device, init order |
| Wrong init order | Dependency not ready | Use correct init level/priority |

#### Debugging Checklist

1. **Check devicetree**: `builds/zephyr/zephyr.dts`
2. **Check generated macros**: `builds/zephyr/include/generated/devicetree_generated.h`
3. **Check Kconfig**: `builds/zephyr/.config`
4. **Check init order**: Add printk in init function
5. **Check bus parent**: Ensure parent device initializes first

---

### Resource Locations

For finding drivers, examples, and bindings in Zephyr: See [locations.md](#locations)

### Complete Examples

For full working examples (GPIO LED, I2C sensor, SPI device, sensor driver): See [examples.md](#examples)

### Related Skills

- **zephyr-devicetree**: DT syntax, bindings, overlays
- **zephyr-kconfig**: Driver configuration options
- **zephyr-kernel-synchronization**: Locks in interrupt-driven drivers

## Driver Apis

### Table of Contents

1. [Getting Device Handles](#getting-device-handles)
2. [GPIO API](#gpio-api)
3. [I2C API](#i2c-api)
4. [SPI API](#spi-api)
5. [UART API](#uart-api)
6. [ADC API](#adc-api)
7. [PWM API](#pwm-api)
8. [Sensor API](#sensor-api)

---

### Getting Device Handles

#### From Devicetree Node Label

```c
#include <zephyr/device.h>

/* Most common pattern */
const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));

/* ALWAYS check readiness before use */
if (!device_is_ready(dev)) {
    printk("Device not ready\n");
    return -ENODEV;
}
```

#### Other Methods

```c
/* From alias (defined in DT) */
const struct device *led = DEVICE_DT_GET(DT_ALIAS(led0));

/* From chosen node */
const struct device *console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

/* First device with compatible */
const struct device *sensor = DEVICE_DT_GET_ANY(bosch_bme280);

/* From devicetree path */
const struct device *spi = DEVICE_DT_GET(DT_PATH(soc, spi_40003000));
```

---

### GPIO API

**Header**: `<zephyr/drivers/gpio.h>`
**Kconfig**: `CONFIG_GPIO=y`

#### Using gpio_dt_spec (Recommended)

```c
#include <zephyr/drivers/gpio.h>

/* Define from devicetree */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

int main(void)
{
    int ret;

    /* Check device ready */
    if (!gpio_is_ready_dt(&led)) {
        return -ENODEV;
    }

    /* Configure as output */
    ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
    if (ret < 0) {
        return ret;
    }

    /* Set pin high */
    gpio_pin_set_dt(&led, 1);

    /* Toggle pin */
    gpio_pin_toggle_dt(&led);

    /* Configure as input with interrupt */
    gpio_pin_configure_dt(&button, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);

    return 0;
}
```

#### GPIO Interrupt Callback

```c
static struct gpio_callback button_cb_data;

void button_pressed(const struct device *dev, struct gpio_callback *cb,
                    uint32_t pins)
{
    printk("Button pressed at %" PRIu32 "\n", pins);
}

int setup_button_interrupt(void)
{
    gpio_pin_configure_dt(&button, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);

    gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb_data);

    return 0;
}
```

#### GPIO Flags

| Flag | Description |
|------|-------------|
| `GPIO_OUTPUT` | Configure as output |
| `GPIO_INPUT` | Configure as input |
| `GPIO_OUTPUT_ACTIVE` | Output, initially active |
| `GPIO_OUTPUT_INACTIVE` | Output, initially inactive |
| `GPIO_PULL_UP` | Enable pull-up |
| `GPIO_PULL_DOWN` | Enable pull-down |
| `GPIO_ACTIVE_LOW` | Active low polarity |
| `GPIO_INT_EDGE_RISING` | Interrupt on rising edge |
| `GPIO_INT_EDGE_FALLING` | Interrupt on falling edge |
| `GPIO_INT_EDGE_BOTH` | Interrupt on both edges |

---

### I2C API

**Header**: `<zephyr/drivers/i2c.h>`
**Kconfig**: `CONFIG_I2C=y`

#### Using i2c_dt_spec (Recommended)

```c
#include <zephyr/drivers/i2c.h>

/* From devicetree node with reg property */
static const struct i2c_dt_spec dev_i2c = I2C_DT_SPEC_GET(DT_NODELABEL(my_sensor));

int main(void)
{
    uint8_t reg_addr = 0x00;
    uint8_t data[4];

    if (!i2c_is_ready_dt(&dev_i2c)) {
        return -ENODEV;
    }

    /* Write single byte */
    uint8_t val = 0x42;
    i2c_write_dt(&dev_i2c, &val, 1);

    /* Read into buffer */
    i2c_read_dt(&dev_i2c, data, sizeof(data));

    /* Write register then read (common pattern) */
    i2c_write_read_dt(&dev_i2c, &reg_addr, 1, data, sizeof(data));

    return 0;
}
```

#### Register Access Pattern

```c
/* Read register */
static int read_reg(const struct i2c_dt_spec *i2c, uint8_t reg, uint8_t *val)
{
    return i2c_write_read_dt(i2c, &reg, 1, val, 1);
}

/* Write register */
static int write_reg(const struct i2c_dt_spec *i2c, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_write_dt(i2c, buf, sizeof(buf));
}

/* Read multiple registers */
static int read_regs(const struct i2c_dt_spec *i2c, uint8_t start_reg,
                     uint8_t *buf, size_t len)
{
    return i2c_write_read_dt(i2c, &start_reg, 1, buf, len);
}
```

#### Burst/Multi-Message Transfer

```c
struct i2c_msg msgs[2];
uint8_t reg = 0x10;
uint8_t data[6];

/* First message: write register address */
msgs[0].buf = &reg;
msgs[0].len = 1;
msgs[0].flags = I2C_MSG_WRITE;

/* Second message: read data */
msgs[1].buf = data;
msgs[1].len = sizeof(data);
msgs[1].flags = I2C_MSG_READ | I2C_MSG_STOP;

i2c_transfer_dt(&dev_i2c, msgs, 2);
```

---

### SPI API

**Header**: `<zephyr/drivers/spi.h>`
**Kconfig**: `CONFIG_SPI=y`

#### Using spi_dt_spec (Recommended)

```c
#include <zephyr/drivers/spi.h>

/* From devicetree - includes bus, CS GPIO, and config */
static const struct spi_dt_spec spi_dev = SPI_DT_SPEC_GET(
    DT_NODELABEL(my_spi_device),
    SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
    0  /* delay */
);

int main(void)
{
    uint8_t tx_buf[4] = {0x01, 0x02, 0x03, 0x04};
    uint8_t rx_buf[4];

    if (!spi_is_ready_dt(&spi_dev)) {
        return -ENODEV;
    }

    /* Set up buffers */
    struct spi_buf tx = {.buf = tx_buf, .len = sizeof(tx_buf)};
    struct spi_buf rx = {.buf = rx_buf, .len = sizeof(rx_buf)};
    struct spi_buf_set tx_set = {.buffers = &tx, .count = 1};
    struct spi_buf_set rx_set = {.buffers = &rx, .count = 1};

    /* Full duplex transfer */
    spi_transceive_dt(&spi_dev, &tx_set, &rx_set);

    /* Write only */
    spi_write_dt(&spi_dev, &tx_set);

    /* Read only */
    spi_read_dt(&spi_dev, &rx_set);

    return 0;
}
```

#### Register Access Over SPI

```c
/* Read register (typical: send reg addr with read bit, then read) */
static int spi_read_reg(const struct spi_dt_spec *spi, uint8_t reg,
                        uint8_t *val)
{
    uint8_t tx = reg | 0x80;  /* Set read bit */
    uint8_t rx[2];

    struct spi_buf tx_buf = {.buf = &tx, .len = 1};
    struct spi_buf rx_buf = {.buf = rx, .len = 2};
    struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1};
    struct spi_buf_set rx_set = {.buffers = &rx_buf, .count = 1};

    int ret = spi_transceive_dt(spi, &tx_set, &rx_set);
    *val = rx[1];
    return ret;
}

/* Write register */
static int spi_write_reg(const struct spi_dt_spec *spi, uint8_t reg,
                         uint8_t val)
{
    uint8_t tx[2] = {reg & 0x7F, val};  /* Clear read bit */
    struct spi_buf buf = {.buf = tx, .len = 2};
    struct spi_buf_set buf_set = {.buffers = &buf, .count = 1};

    return spi_write_dt(spi, &buf_set);
}
```

#### SPI Configuration Flags

| Flag | Description |
|------|-------------|
| `SPI_WORD_SET(n)` | Word size in bits (8, 16, etc.) |
| `SPI_TRANSFER_MSB` | MSB first (most common) |
| `SPI_TRANSFER_LSB` | LSB first |
| `SPI_MODE_CPOL` | Clock polarity high |
| `SPI_MODE_CPHA` | Clock phase: sample on trailing edge |
| `SPI_MODE_GET(n)` | SPI mode 0-3 |

---

### UART API

**Header**: `<zephyr/drivers/uart.h>`
**Kconfig**: `CONFIG_SERIAL=y`

#### Polling API

```c
#include <zephyr/drivers/uart.h>

const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(uart0));

/* Transmit */
void uart_send(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        uart_poll_out(uart, data[i]);
    }
}

/* Receive (blocking) */
int uart_receive_byte(uint8_t *c)
{
    return uart_poll_in(uart, c);  /* Returns 0 on success, -1 if no data */
}
```

#### Interrupt-Driven API

```c
#include <zephyr/drivers/uart.h>

const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(uart0));

static uint8_t rx_buf[64];
static volatile size_t rx_count = 0;

void uart_isr(const struct device *dev, void *user_data)
{
    uart_irq_update(dev);

    if (uart_irq_rx_ready(dev)) {
        uint8_t c;
        while (uart_fifo_read(dev, &c, 1) > 0) {
            if (rx_count < sizeof(rx_buf)) {
                rx_buf[rx_count++] = c;
            }
        }
    }
}

int setup_uart_irq(void)
{
    if (!device_is_ready(uart)) {
        return -ENODEV;
    }

    uart_irq_callback_set(uart, uart_isr);
    uart_irq_rx_enable(uart);

    return 0;
}
```

#### Async API (DMA-based)

Enable with `CONFIG_UART_ASYNC_API=y`:

```c
static uint8_t rx_buf[256];

void uart_async_callback(const struct device *dev,
                         struct uart_event *evt,
                         void *user_data)
{
    switch (evt->type) {
    case UART_RX_RDY:
        /* Data received: evt->data.rx.buf, evt->data.rx.len */
        break;
    case UART_TX_DONE:
        /* Transmission complete */
        break;
    case UART_RX_DISABLED:
        /* Re-enable RX */
        uart_rx_enable(dev, rx_buf, sizeof(rx_buf), SYS_FOREVER_US);
        break;
    default:
        break;
    }
}

int setup_uart_async(void)
{
    uart_callback_set(uart, uart_async_callback, NULL);
    uart_rx_enable(uart, rx_buf, sizeof(rx_buf), SYS_FOREVER_US);
    return 0;
}
```

---

### ADC API

**Header**: `<zephyr/drivers/adc.h>`
**Kconfig**: `CONFIG_ADC=y`

#### Basic ADC Reading

```c
#include <zephyr/drivers/adc.h>

/* Define channel from devicetree */
static const struct adc_dt_spec adc_channel = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));

int main(void)
{
    int16_t buf;
    int ret;

    if (!adc_is_ready_dt(&adc_channel)) {
        return -ENODEV;
    }

    /* Configure channel */
    ret = adc_channel_setup_dt(&adc_channel);
    if (ret < 0) {
        return ret;
    }

    /* Create sequence */
    struct adc_sequence sequence = {
        .buffer = &buf,
        .buffer_size = sizeof(buf),
    };
    adc_sequence_init_dt(&adc_channel, &sequence);

    /* Read ADC */
    ret = adc_read_dt(&adc_channel, &sequence);
    if (ret < 0) {
        return ret;
    }

    /* Convert to millivolts */
    int32_t mv = buf;
    adc_raw_to_millivolts_dt(&adc_channel, &mv);
    printk("ADC: %d mV\n", mv);

    return 0;
}
```

#### Devicetree for ADC

```dts
/ {
    zephyr,user {
        io-channels = <&adc 0>;  /* ADC channel 0 */
    };
};

&adc {
    #address-cells = <1>;
    #size-cells = <0>;
    status = "okay";

    channel@0 {
        reg = <0>;
        zephyr,gain = "ADC_GAIN_1_6";
        zephyr,reference = "ADC_REF_INTERNAL";
        zephyr,acquisition-time = <ADC_ACQ_TIME_DEFAULT>;
        zephyr,resolution = <12>;
    };
};
```

---

### PWM API

**Header**: `<zephyr/drivers/pwm.h>`
**Kconfig**: `CONFIG_PWM=y`

#### Basic PWM Control

```c
#include <zephyr/drivers/pwm.h>

static const struct pwm_dt_spec pwm_led = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led0));

int main(void)
{
    if (!pwm_is_ready_dt(&pwm_led)) {
        return -ENODEV;
    }

    /* Set 50% duty cycle at 1kHz */
    uint32_t period_ns = 1000000U;  /* 1ms = 1kHz */
    uint32_t pulse_ns = period_ns / 2;

    pwm_set_pulse_dt(&pwm_led, pulse_ns);

    /* Or set with explicit period */
    pwm_set_dt(&pwm_led, period_ns, pulse_ns);

    return 0;
}
```

#### Devicetree for PWM

```dts
/ {
    aliases {
        pwm-led0 = &pwm_led0;
    };

    pwmleds {
        compatible = "pwm-leds";
        pwm_led0: pwm_led_0 {
            pwms = <&pwm0 0 PWM_MSEC(20) PWM_POLARITY_NORMAL>;
        };
    };
};
```

---

### Sensor API

**Header**: `<zephyr/drivers/sensor.h>`
**Kconfig**: `CONFIG_SENSOR=y`

#### Reading Sensor Data

```c
#include <zephyr/drivers/sensor.h>

const struct device *sensor = DEVICE_DT_GET_ANY(bosch_bme280);

int main(void)
{
    struct sensor_value temp, press, humidity;

    if (!device_is_ready(sensor)) {
        return -ENODEV;
    }

    while (1) {
        /* Fetch all channels */
        sensor_sample_fetch(sensor);

        /* Get specific channels */
        sensor_channel_get(sensor, SENSOR_CHAN_AMBIENT_TEMP, &temp);
        sensor_channel_get(sensor, SENSOR_CHAN_PRESS, &press);
        sensor_channel_get(sensor, SENSOR_CHAN_HUMIDITY, &humidity);

        /* sensor_value: val1 = integer part, val2 = fractional (micro) */
        printk("Temp: %d.%06d C\n", temp.val1, temp.val2);
        printk("Press: %d.%06d kPa\n", press.val1, press.val2);
        printk("Humidity: %d.%06d %%\n", humidity.val1, humidity.val2);

        k_sleep(K_SECONDS(1));
    }
}
```

#### Sensor Triggers

```c
static void trigger_handler(const struct device *dev,
                            const struct sensor_trigger *trigger)
{
    struct sensor_value val;
    sensor_sample_fetch(dev);
    sensor_channel_get(dev, SENSOR_CHAN_ACCEL_XYZ, &val);
    printk("Motion detected!\n");
}

int setup_trigger(void)
{
    struct sensor_trigger trig = {
        .type = SENSOR_TRIG_MOTION,
        .chan = SENSOR_CHAN_ACCEL_XYZ,
    };

    return sensor_trigger_set(sensor, &trig, trigger_handler);
}
```

#### Common Sensor Channels

| Channel | Description |
|---------|-------------|
| `SENSOR_CHAN_AMBIENT_TEMP` | Temperature (°C) |
| `SENSOR_CHAN_PRESS` | Pressure (kPa) |
| `SENSOR_CHAN_HUMIDITY` | Relative humidity (%) |
| `SENSOR_CHAN_ACCEL_X/Y/Z` | Acceleration (m/s²) |
| `SENSOR_CHAN_GYRO_X/Y/Z` | Angular velocity (rad/s) |
| `SENSOR_CHAN_LIGHT` | Light intensity (lux) |
| `SENSOR_CHAN_VOLTAGE` | Voltage (V) |
| `SENSOR_CHAN_CURRENT` | Current (A) |

## Driver Creation

### Table of Contents

1. [Overview](#overview)
2. [Step 1: Devicetree Binding](#step-1-devicetree-binding)
3. [Step 2: Kconfig](#step-2-kconfig)
4. [Step 3: Driver Implementation](#step-3-driver-implementation)
5. [Step 4: CMake Integration](#step-4-cmake-integration)
6. [Step 5: Devicetree Node](#step-5-devicetree-node)
7. [Step 6: Application Configuration](#step-6-application-configuration)
8. [Complete Example](#complete-example)

---

### Overview

Creating a Zephyr driver involves these files:

```
my_project/
├── drivers/
│   └── mydriver/
│       ├── CMakeLists.txt
│       ├── Kconfig
│       └── mydriver.c
├── dts/bindings/
│   └── vendor,mydriver.yaml
├── boards/
│   └── myboard.overlay     # Or app overlay
├── prj.conf
└── CMakeLists.txt          # Top-level
```

---

### Step 1: Devicetree Binding

Create `dts/bindings/vendor,mydriver.yaml`:

```yaml
description: Vendor MyDriver device

compatible: "vendor,mydriver"

include: base.yaml

properties:
  reg:
    required: true
    description: I2C address or register base

  int-gpios:
    type: phandle-array
    description: Interrupt GPIO

  sample-rate:
    type: int
    default: 100
    description: Sample rate in Hz

  mode:
    type: string
    enum:
      - "low-power"
      - "normal"
      - "high-performance"
    default: "normal"
    description: Operating mode
```

#### Common Binding Includes

| Include | For | Provides |
|---------|-----|----------|
| `base.yaml` | All devices | `status`, `compatible`, `label` |
| `i2c-device.yaml` | I2C devices | `reg` (I2C address) |
| `spi-device.yaml` | SPI devices | `reg`, `spi-max-frequency` |
| `gpio-controller.yaml` | GPIO controllers | `gpio-cells`, `#gpio-cells` |

#### For I2C Device

```yaml
description: I2C sensor

compatible: "vendor,i2c-sensor"

include: [i2c-device.yaml]

properties:
  int-gpios:
    type: phandle-array
```

#### For SPI Device

```yaml
description: SPI device

compatible: "vendor,spi-device"

include: [spi-device.yaml]

properties:
  reset-gpios:
    type: phandle-array
```

---

### Step 2: Kconfig

Create `drivers/mydriver/Kconfig`:

```kconfig
# Mydriver configuration

menuconfig MYDRIVER
    bool "My custom driver"
    default y
    depends on I2C
    help
      Enable support for the vendor mydriver device.

if MYDRIVER

config MYDRIVER_INIT_PRIORITY
    int "Init priority"
    default 90
    help
      Device driver initialization priority.

config MYDRIVER_TRIGGER
    bool "Enable trigger support"
    help
      Enable interrupt-based trigger support.

config MYDRIVER_LOG_LEVEL
    int "Log level"
    default 3
    range 0 4
    help
      Log level for mydriver (0=OFF, 4=DEBUG).

endif # MYDRIVER
```

#### Kconfig Best Practices

1. Use `menuconfig` for main driver option
2. Add `depends on` for required subsystems
3. Nest options under `if MYDRIVER` block
4. Provide sensible defaults
5. Add `help` text for all options

---

### Step 3: Driver Implementation

Create `drivers/mydriver/mydriver.c`:

```c
#define DT_DRV_COMPAT vendor_mydriver

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(mydriver, CONFIG_MYDRIVER_LOG_LEVEL);

/* Configuration structure - stored in flash */
struct mydriver_config {
    struct i2c_dt_spec i2c;
    struct gpio_dt_spec int_gpio;
    uint32_t sample_rate;
};

/* Runtime data - stored in RAM */
struct mydriver_data {
    struct k_sem lock;
    uint8_t last_reading;
    bool initialized;
};

/* Driver API functions */
static int mydriver_read(const struct device *dev, uint8_t *value)
{
    const struct mydriver_config *cfg = dev->config;
    struct mydriver_data *data = dev->data;
    int ret;

    k_sem_take(&data->lock, K_FOREVER);

    ret = i2c_read_dt(&cfg->i2c, value, 1);
    if (ret == 0) {
        data->last_reading = *value;
    }

    k_sem_give(&data->lock);
    return ret;
}

static int mydriver_write(const struct device *dev, uint8_t value)
{
    const struct mydriver_config *cfg = dev->config;
    struct mydriver_data *data = dev->data;
    int ret;

    k_sem_take(&data->lock, K_FOREVER);
    ret = i2c_write_dt(&cfg->i2c, &value, 1);
    k_sem_give(&data->lock);

    return ret;
}

/* Define API structure (optional, for subsystem integration).
 * Name it `<class>_driver_api` so DEVICE_API() works.
 */
struct mydriver_driver_api {
    int (*read)(const struct device *dev, uint8_t *value);
    int (*write)(const struct device *dev, uint8_t value);
};

static DEVICE_API(mydriver, mydriver_api_funcs) = {
    .read = mydriver_read,
    .write = mydriver_write,
};

/* Initialization function */
static int mydriver_init(const struct device *dev)
{
    const struct mydriver_config *cfg = dev->config;
    struct mydriver_data *data = dev->data;
    int ret;

    LOG_DBG("Initializing %s", dev->name);

    /* Check I2C bus is ready */
    if (!i2c_is_ready_dt(&cfg->i2c)) {
        LOG_ERR("I2C bus not ready");
        return -ENODEV;
    }

    /* Configure interrupt GPIO if present */
    if (cfg->int_gpio.port != NULL) {
        if (!gpio_is_ready_dt(&cfg->int_gpio)) {
            LOG_ERR("Interrupt GPIO not ready");
            return -ENODEV;
        }

        ret = gpio_pin_configure_dt(&cfg->int_gpio, GPIO_INPUT);
        if (ret < 0) {
            LOG_ERR("Failed to configure interrupt GPIO");
            return ret;
        }
    }

    /* Initialize synchronization */
    k_sem_init(&data->lock, 1, 1);

    /* Verify device communication (e.g., read chip ID) */
    uint8_t chip_id;
    ret = i2c_read_dt(&cfg->i2c, &chip_id, 1);
    if (ret < 0) {
        LOG_ERR("Failed to read chip ID");
        return ret;
    }

    LOG_INF("Device %s initialized, chip ID: 0x%02x", dev->name, chip_id);
    data->initialized = true;

    return 0;
}

/* Device instantiation macro */
#define MYDRIVER_DEFINE(inst)                                              \
    static struct mydriver_data mydriver_data_##inst;                      \
                                                                           \
    static const struct mydriver_config mydriver_config_##inst = {         \
        .i2c = I2C_DT_SPEC_INST_GET(inst),                                 \
        .int_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, int_gpios, {0}),        \
        .sample_rate = DT_INST_PROP(inst, sample_rate),                    \
    };                                                                     \
                                                                           \
    DEVICE_DT_INST_DEFINE(inst,                                            \
                          mydriver_init,                                   \
                          NULL,                                            \
                          &mydriver_data_##inst,                           \
                          &mydriver_config_##inst,                         \
                          POST_KERNEL,                                     \
                          CONFIG_MYDRIVER_INIT_PRIORITY,                   \
                          &mydriver_api_funcs);

/* Instantiate for all enabled nodes */
DT_INST_FOREACH_STATUS_OKAY(MYDRIVER_DEFINE)
```

#### Key Patterns

1. **`DT_DRV_COMPAT`**: Must match `compatible` with underscores
2. **Config initialization**: Use `DT_INST_*` macros in config struct
3. **Check dependencies**: Verify bus/GPIO ready before use
4. **Logging**: Use `LOG_MODULE_REGISTER` with Kconfig level
5. **Thread safety**: Use k_sem/k_mutex for shared state

---

### Step 4: CMake Integration

#### Driver CMakeLists.txt

Create `drivers/mydriver/CMakeLists.txt`:

```cmake
# Only build if Kconfig is enabled
zephyr_library_sources_ifdef(CONFIG_MYDRIVER mydriver.c)
```

#### Top-Level CMakeLists.txt

In your application's `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20.0)

# Add custom drivers BEFORE find_package
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/drivers/mydriver)

# Add custom bindings
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(my_app)

target_sources(app PRIVATE src/main.c)
```

#### Alternative: Kconfig include

Add to application's `Kconfig`:

```kconfig
menu "Custom Drivers"

rsource "drivers/mydriver/Kconfig"

endmenu
```

---

### Step 5: Devicetree Node

Add to board overlay or `boards/myboard.overlay`:

```dts
&i2c0 {
    status = "okay";

    mydevice: mydriver@48 {
        compatible = "vendor,mydriver";
        reg = <0x48>;
        int-gpios = <&gpio0 15 GPIO_ACTIVE_LOW>;
        sample-rate = <200>;
    };
};
```

---

### Step 6: Application Configuration

#### prj.conf

```ini
CONFIG_I2C=y
CONFIG_GPIO=y
CONFIG_MYDRIVER=y
CONFIG_MYDRIVER_INIT_PRIORITY=90
CONFIG_LOG=y
CONFIG_MYDRIVER_LOG_LEVEL=4
```

#### Using the Driver in Application

```c
#include <zephyr/device.h>

/* Get device handle */
const struct device *mydev = DEVICE_DT_GET(DT_NODELABEL(mydevice));

int main(void)
{
    if (!device_is_ready(mydev)) {
        printk("Device not ready\n");
        return -1;
    }

    /* Access driver API */
    const struct mydriver_api *api = mydev->api;
    uint8_t value;
    api->read(mydev, &value);

    return 0;
}
```

---

### Complete Example

#### Directory Structure

```
my_app/
├── CMakeLists.txt
├── prj.conf
├── src/
│   └── main.c
├── boards/
│   └── nrf52dk_nrf52832.overlay
├── drivers/
│   └── temp_sensor/
│       ├── CMakeLists.txt
│       ├── Kconfig
│       └── temp_sensor.c
└── dts/bindings/
    └── acme,temp-sensor.yaml
```

#### Binding: `dts/bindings/acme,temp-sensor.yaml`

```yaml
description: ACME temperature sensor

compatible: "acme,temp-sensor"

include: [i2c-device.yaml]

properties:
  resolution:
    type: int
    default: 12
    enum: [9, 10, 11, 12]
    description: ADC resolution in bits
```

#### Kconfig: `drivers/temp_sensor/Kconfig`

```kconfig
config ACME_TEMP_SENSOR
    bool "ACME temperature sensor driver"
    default y
    depends on I2C
    select SENSOR

config ACME_TEMP_SENSOR_INIT_PRIORITY
    int "Init priority"
    default 90
    depends on ACME_TEMP_SENSOR
```

#### Driver: `drivers/temp_sensor/temp_sensor.c`

```c
#define DT_DRV_COMPAT acme_temp_sensor

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>

struct temp_sensor_config {
    struct i2c_dt_spec i2c;
    uint8_t resolution;
};

struct temp_sensor_data {
    int32_t temp_raw;
};

static int temp_sensor_sample_fetch(const struct device *dev,
                                    enum sensor_channel chan)
{
    const struct temp_sensor_config *cfg = dev->config;
    struct temp_sensor_data *data = dev->data;
    uint8_t buf[2];
    int ret;

    ret = i2c_read_dt(&cfg->i2c, buf, sizeof(buf));
    if (ret < 0) {
        return ret;
    }

    data->temp_raw = (buf[0] << 8) | buf[1];
    return 0;
}

static int temp_sensor_channel_get(const struct device *dev,
                                   enum sensor_channel chan,
                                   struct sensor_value *val)
{
    struct temp_sensor_data *data = dev->data;

    if (chan != SENSOR_CHAN_AMBIENT_TEMP) {
        return -ENOTSUP;
    }

    /* Convert raw to celsius (example: 0.0625 per LSB for 12-bit) */
    int32_t temp_mc = (data->temp_raw * 625) / 10;  /* millicelsius */
    val->val1 = temp_mc / 1000;
    val->val2 = (temp_mc % 1000) * 1000;

    return 0;
}

static DEVICE_API(sensor, temp_sensor_api) = {
    .sample_fetch = temp_sensor_sample_fetch,
    .channel_get = temp_sensor_channel_get,
};

static int temp_sensor_init(const struct device *dev)
{
    const struct temp_sensor_config *cfg = dev->config;

    if (!i2c_is_ready_dt(&cfg->i2c)) {
        return -ENODEV;
    }

    /* Configure resolution register */
    uint8_t config = cfg->resolution << 5;
    return i2c_write_dt(&cfg->i2c, &config, 1);
}

#define TEMP_SENSOR_DEFINE(inst)                                    \
    static struct temp_sensor_data temp_sensor_data_##inst;         \
    static const struct temp_sensor_config temp_sensor_cfg_##inst = { \
        .i2c = I2C_DT_SPEC_INST_GET(inst),                          \
        .resolution = DT_INST_PROP(inst, resolution),               \
    };                                                              \
    DEVICE_DT_INST_DEFINE(inst,                                     \
                          temp_sensor_init,                         \
                          NULL,                                     \
                          &temp_sensor_data_##inst,                 \
                          &temp_sensor_cfg_##inst,                  \
                          POST_KERNEL,                              \
                          CONFIG_ACME_TEMP_SENSOR_INIT_PRIORITY,    \
                          &temp_sensor_api);

DT_INST_FOREACH_STATUS_OKAY(TEMP_SENSOR_DEFINE)
```

#### CMakeLists.txt (driver)

```cmake
zephyr_library_sources_ifdef(CONFIG_ACME_TEMP_SENSOR temp_sensor.c)
```

#### Overlay

```dts
&i2c0 {
    temp_sensor: temp-sensor@48 {
        compatible = "acme,temp-sensor";
        reg = <0x48>;
        resolution = <12>;
    };
};
```

#### prj.conf

```ini
CONFIG_I2C=y
CONFIG_SENSOR=y
CONFIG_ACME_TEMP_SENSOR=y
```

#### Application

```c
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

const struct device *temp = DEVICE_DT_GET(DT_NODELABEL(temp_sensor));

int main(void)
{
    struct sensor_value val;

    if (!device_is_ready(temp)) {
        return -1;
    }

    while (1) {
        sensor_sample_fetch(temp);
        sensor_channel_get(temp, SENSOR_CHAN_AMBIENT_TEMP, &val);
        printk("Temp: %d.%06d C\n", val.val1, val.val2);
        k_sleep(K_SECONDS(1));
    }
}
```

## Driver Model

### Table of Contents

1. [Device Structure](#device-structure)
2. [Initialization Levels and Priorities](#initialization-levels-and-priorities)
3. [Device Definition Macros](#device-definition-macros)
4. [Power Management](#power-management)
5. [Device Dependencies](#device-dependencies)

---

### Device Structure

#### The `struct device`

Every device in Zephyr is represented by a `struct device`:

```c
struct device {
    const char *name;           /* Device name (from DT or static) */
    const void *config;         /* Immutable configuration */
    const void *api;            /* Driver API function pointers */
    void *data;                 /* Mutable runtime data */
    /* Plus internal fields for PM, init state, etc. */
};
```

#### Config vs Data

| Aspect | Config Structure | Data Structure |
|--------|------------------|----------------|
| Mutability | `const` - immutable | Mutable at runtime |
| Storage | Flash (ROM) | RAM |
| Contents | HW addresses, pins, IRQs | State, buffers, locks |
| Initialization | Compile-time | Runtime in init function |

#### Example Structures

```c
/* Configuration - stored in flash */
struct mydriver_config {
    uint32_t base_addr;              /* Register base address */
    uint32_t irq_num;                /* IRQ number */
    const struct gpio_dt_spec reset; /* GPIO for reset pin */
    uint32_t clock_freq;             /* Clock frequency */
};

/* Runtime data - stored in RAM */
struct mydriver_data {
    struct k_sem lock;               /* Synchronization */
    uint8_t rx_buffer[256];          /* Receive buffer */
    volatile bool transfer_done;     /* Transfer complete flag */
    uint32_t error_count;            /* Error statistics */
};
```

---

### Initialization Levels and Priorities

#### Initialization Levels

Devices initialize in a specific order based on levels:

| Level | Value | Description | Use Case |
|-------|-------|-------------|----------|
| `EARLY` | 0 | Before standard initialization | Architecture early init |
| `PRE_KERNEL_1` | 1 | No kernel services available | Interrupt controllers, clocks |
| `PRE_KERNEL_2` | 2 | After PRE_KERNEL_1 | Drivers depending on PRE_KERNEL_1 |
| `POST_KERNEL` | 3 | Kernel services available | Most drivers (recommended default) |
| `APPLICATION` | 4 | After kernel, before main() | Application-specific devices |

#### Initialization Priority

Within each level, devices initialize by priority (0-99, lower = earlier):

```c
/* Priority constants in Kconfig */
CONFIG_KERNEL_INIT_PRIORITY_DEFAULT=40
CONFIG_KERNEL_INIT_PRIORITY_DEVICE=50
CONFIG_I2C_INIT_PRIORITY=50
CONFIG_GPIO_INIT_PRIORITY=40
CONFIG_SENSOR_INIT_PRIORITY=90
```

#### Choosing Level and Priority

```
Initialization Order:

PRE_KERNEL_1     PRE_KERNEL_2        POST_KERNEL           APPLICATION
    │                 │                   │                     │
    ▼                 ▼                   ▼                     ▼
┌────────┐       ┌────────┐         ┌────────┐             ┌────────┐
│ Clock  │       │  Bus   │         │ Device │             │  App   │
│ GPIO   │──────▶│  I2C   │────────▶│ Sensor │────────────▶│ Custom │
│ IRQ    │       │  SPI   │         │ Display│             │        │
└────────┘       └────────┘         └────────┘             └────────┘
```

**Rules:**
1. Clocks, GPIO controllers, interrupt controllers → `PRE_KERNEL_1`
2. Bus controllers (I2C, SPI, UART) → `PRE_KERNEL_2` or early `POST_KERNEL`
3. Bus devices (sensors, displays) → `POST_KERNEL`
4. Within a level, parent devices need lower priority than children

---

### Device Definition Macros

#### DEVICE_DT_DEFINE

For defining a device from a specific devicetree node:

```c
DEVICE_DT_DEFINE(node_id,       /* DT node identifier */
                 init_fn,        /* Initialization function */
                 pm,             /* PM device pointer (or NULL) */
                 data,           /* Pointer to data structure */
                 config,         /* Pointer to config structure */
                 level,          /* Initialization level */
                 prio,           /* Initialization priority */
                 api);           /* Driver API pointer */
```

#### DEVICE_DT_INST_DEFINE

For multi-instance drivers using `DT_DRV_COMPAT`:

```c
#define DT_DRV_COMPAT vendor_device

#define MYDRIVER_INIT(inst)                                       \
    static struct mydriver_data data_##inst;                      \
    static const struct mydriver_config config_##inst = {         \
        .base_addr = DT_INST_REG_ADDR(inst),                      \
        .irq_num = DT_INST_IRQN(inst),                            \
    };                                                            \
    DEVICE_DT_INST_DEFINE(inst,                                   \
                          mydriver_init,                          \
                          NULL,                                   \
                          &data_##inst,                           \
                          &config_##inst,                         \
                          POST_KERNEL,                            \
                          CONFIG_MYDRIVER_INIT_PRIORITY,          \
                          &mydriver_api);

DT_INST_FOREACH_STATUS_OKAY(MYDRIVER_INIT)
```

#### DEVICE_DEFINE

For non-devicetree devices (rare, avoid if possible):

```c
DEVICE_DEFINE(name,             /* C identifier for device */
              "device_name",    /* Device name string */
              init_fn,
              pm,
              data,
              config,
              level,
              prio,
              api);
```

---

### Power Management

#### PM Device Structure

Enable with `CONFIG_PM_DEVICE=y`:

```c
#include <zephyr/pm/device.h>

/* PM action callback */
static int mydriver_pm_action(const struct device *dev,
                              enum pm_device_action action)
{
    switch (action) {
    case PM_DEVICE_ACTION_SUSPEND:
        /* Save state, disable HW */
        return 0;
    case PM_DEVICE_ACTION_RESUME:
        /* Restore state, enable HW */
        return 0;
    default:
        return -ENOTSUP;
    }
}

/* Define PM device */
PM_DEVICE_DT_INST_DEFINE(inst, mydriver_pm_action);

/* Use in DEVICE_DT_INST_DEFINE */
DEVICE_DT_INST_DEFINE(inst,
                      mydriver_init,
                      PM_DEVICE_DT_INST_GET(inst),  /* PM pointer */
                      &data,
                      &config,
                      POST_KERNEL,
                      CONFIG_MYDRIVER_INIT_PRIORITY,
                      &mydriver_api);
```

#### PM Device Actions

| Action | Description |
|--------|-------------|
| `PM_DEVICE_ACTION_SUSPEND` | Device going to low power |
| `PM_DEVICE_ACTION_RESUME` | Device returning from low power |
| `PM_DEVICE_ACTION_TURN_OFF` | Device being turned off |
| `PM_DEVICE_ACTION_TURN_ON` | Device being turned on |

#### Application PM Control

```c
#include <zephyr/pm/device.h>

/* Suspend a device */
pm_device_action_run(dev, PM_DEVICE_ACTION_SUSPEND);

/* Resume a device */
pm_device_action_run(dev, PM_DEVICE_ACTION_RESUME);

/* Check device state */
enum pm_device_state state;
pm_device_state_get(dev, &state);
```

---

### Device Dependencies

#### Implicit Dependencies via Devicetree

Zephyr automatically handles dependencies based on devicetree hierarchy:

```dts
/* Parent bus initializes before child device */
&i2c0 {
    status = "okay";

    sensor@48 {
        compatible = "vendor,sensor";
        reg = <0x48>;
        /* Implicitly depends on i2c0 */
    };
};
```

#### Checking Parent Readiness

```c
static int sensor_init(const struct device *dev)
{
    const struct sensor_config *cfg = dev->config;

    /* Check bus is ready before using */
    if (!i2c_is_ready_dt(&cfg->i2c)) {
        return -ENODEV;
    }

    /* Now safe to use I2C */
    return 0;
}
```

#### Manual Dependency Handling

For complex dependencies not captured in devicetree:

```c
static int mydriver_init(const struct device *dev)
{
    /* Get dependency device */
    const struct device *clock_dev = DEVICE_DT_GET(DT_NODELABEL(clock0));

    if (!device_is_ready(clock_dev)) {
        /* Dependency not ready - this shouldn't happen if
         * init levels/priorities are correct */
        return -ENODEV;
    }

    /* Use clock device */
    return 0;
}
```

#### Debugging Init Order

```c
/* Add to your init function */
static int mydriver_init(const struct device *dev)
{
    printk("[INIT] %s initializing\n", dev->name);
    /* ... */
    printk("[INIT] %s complete: %d\n", dev->name, ret);
    return ret;
}
```

Use `CONFIG_BOOT_BANNER=y` and add printk to see actual init order.

## Driver Testing

### Table of Contents

1. [Testing Overview](#testing-overview)
2. [Ztest for Drivers](#ztest-for-drivers)
3. [Emulators](#emulators)
4. [Fake Devices](#fake-devices)
5. [Test Fixtures](#test-fixtures)
6. [Running Tests](#running-tests)

---

### Testing Overview

#### Testing Approaches

| Approach | Use Case | Platform |
|----------|----------|----------|
| Unit tests with emulators | Comprehensive driver testing | native_sim |
| Integration tests | Test with real hardware | Target board |
| Fake/stub devices | Test application code | Any |
| Twister | Automated test execution | CI/CD |

#### Project Structure for Tests

```
my_driver/
├── CMakeLists.txt
├── Kconfig
├── my_driver.c
└── tests/
    ├── CMakeLists.txt
    ├── prj.conf
    ├── testcase.yaml
    ├── boards/
    │   └── native_sim.overlay
    └── src/
        └── main.c
```

---

### Ztest for Drivers

#### Basic Driver Test

```c
#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

static const struct device *gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));

ZTEST(driver_tests, test_device_ready)
{
    zassert_true(device_is_ready(gpio_dev), "GPIO device not ready");
}

ZTEST(driver_tests, test_gpio_output)
{
    int ret;

    ret = gpio_pin_configure(gpio_dev, 0, GPIO_OUTPUT);
    zassert_ok(ret, "Failed to configure GPIO");

    ret = gpio_pin_set(gpio_dev, 0, 1);
    zassert_ok(ret, "Failed to set GPIO high");

    ret = gpio_pin_set(gpio_dev, 0, 0);
    zassert_ok(ret, "Failed to set GPIO low");
}

ZTEST_SUITE(driver_tests, NULL, NULL, NULL, NULL, NULL);
```

#### Testing Sensor Drivers

```c
#include <zephyr/ztest.h>
#include <zephyr/drivers/sensor.h>

static const struct device *sensor = DEVICE_DT_GET(DT_NODELABEL(temp_sensor));

ZTEST(sensor_tests, test_sensor_ready)
{
    zassert_true(device_is_ready(sensor), "Sensor not ready");
}

ZTEST(sensor_tests, test_sample_fetch)
{
    int ret = sensor_sample_fetch(sensor);
    zassert_ok(ret, "Failed to fetch sample");
}

ZTEST(sensor_tests, test_channel_get)
{
    struct sensor_value val;
    int ret;

    ret = sensor_sample_fetch(sensor);
    zassert_ok(ret);

    ret = sensor_channel_get(sensor, SENSOR_CHAN_AMBIENT_TEMP, &val);
    zassert_ok(ret, "Failed to get channel");

    /* Check reasonable range (-40 to +85°C for typical sensors) */
    zassert_between_inclusive(val.val1, -40, 85,
                              "Temperature out of range: %d", val.val1);
}

ZTEST(sensor_tests, test_unsupported_channel)
{
    struct sensor_value val;
    int ret;

    ret = sensor_sample_fetch(sensor);
    zassert_ok(ret);

    ret = sensor_channel_get(sensor, SENSOR_CHAN_GYRO_X, &val);
    zassert_equal(ret, -ENOTSUP, "Should not support gyro channel");
}

ZTEST_SUITE(sensor_tests, NULL, NULL, NULL, NULL, NULL);
```

#### Testing I2C Drivers

```c
#include <zephyr/ztest.h>
#include <zephyr/drivers/i2c.h>

static const struct i2c_dt_spec dev_i2c = I2C_DT_SPEC_GET(DT_NODELABEL(my_device));

ZTEST(i2c_tests, test_bus_ready)
{
    zassert_true(i2c_is_ready_dt(&dev_i2c), "I2C bus not ready");
}

ZTEST(i2c_tests, test_read_chip_id)
{
    uint8_t chip_id;
    uint8_t reg = 0x00;  /* Chip ID register */
    int ret;

    ret = i2c_write_read_dt(&dev_i2c, &reg, 1, &chip_id, 1);
    zassert_ok(ret, "Failed to read chip ID");
    zassert_equal(chip_id, 0x5A, "Unexpected chip ID: 0x%02x", chip_id);
}

ZTEST_SUITE(i2c_tests, NULL, NULL, NULL, NULL, NULL);
```

---

### Emulators

Enable hardware-free testing on native_sim.

#### Kconfig for Emulation

```kconfig
# prj.conf for tests
CONFIG_EMUL=y
CONFIG_I2C_EMUL=y
CONFIG_SPI_EMUL=y
CONFIG_GPIO_EMUL=y
```

#### Devicetree for Emulation

```dts
/* boards/native_sim.overlay */
/ {
    aliases {
        my-sensor = &test_sensor;
    };
};

&i2c0 {
    status = "okay";

    test_sensor: sensor@48 {
        compatible = "vendor,my-sensor";
        reg = <0x48>;
    };
};
```

#### Using Emulator Backend in Tests

```c
#include <zephyr/ztest.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/emul.h>

/* Get device and emulator */
static const struct device *sensor = DEVICE_DT_GET(DT_NODELABEL(test_sensor));
static const struct emul *emul = EMUL_DT_GET(DT_NODELABEL(test_sensor));

/* Emulator backend API (defined in emulator) */
struct my_sensor_emul_backend {
    void (*set_temperature)(const struct emul *target, int32_t temp_milli_c);
    void (*set_error)(const struct emul *target, bool inject_error);
};

ZTEST(emul_tests, test_read_emulated_value)
{
    const struct my_sensor_emul_backend *backend = emul->backend_api;
    struct sensor_value val;

    /* Set emulator to return 25.5°C */
    backend->set_temperature(emul, 25500);

    /* Read through driver */
    zassert_ok(sensor_sample_fetch(sensor));
    zassert_ok(sensor_channel_get(sensor, SENSOR_CHAN_AMBIENT_TEMP, &val));

    /* Verify driver correctly interprets emulated data */
    zassert_equal(val.val1, 25, "Expected 25, got %d", val.val1);
    zassert_within(val.val2, 500000, 1000, "Fractional mismatch");
}

ZTEST(emul_tests, test_error_handling)
{
    const struct my_sensor_emul_backend *backend = emul->backend_api;

    /* Inject error */
    backend->set_error(emul, true);

    /* Verify driver handles error correctly */
    int ret = sensor_sample_fetch(sensor);
    zassert_not_ok(ret, "Should fail with error injected");

    /* Clear error */
    backend->set_error(emul, false);
    ret = sensor_sample_fetch(sensor);
    zassert_ok(ret, "Should succeed after clearing error");
}

ZTEST_SUITE(emul_tests, NULL, NULL, NULL, NULL, NULL);
```

#### GPIO Emulator

```c
#include <zephyr/drivers/gpio/gpio_emul.h>

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

ZTEST(gpio_emul_tests, test_gpio_output)
{
    gpio_flags_t flags;
    int val;

    /* Configure as output */
    gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

    /* Get emulated pin state */
    gpio_emul_output_get(led.port, led.pin, &val);
    zassert_equal(val, 0, "Should start inactive");

    /* Set high */
    gpio_pin_set_dt(&led, 1);
    gpio_emul_output_get(led.port, led.pin, &val);
    zassert_equal(val, 1, "Should be high");
}

ZTEST(gpio_emul_tests, test_gpio_input)
{
    static const struct gpio_dt_spec button =
        GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
    int val;

    gpio_pin_configure_dt(&button, GPIO_INPUT);

    /* Inject input value */
    gpio_emul_input_set(button.port, button.pin, 1);

    /* Read through driver */
    val = gpio_pin_get_dt(&button);
    zassert_equal(val, 1, "Should read injected value");
}

ZTEST_SUITE(gpio_emul_tests, NULL, NULL, NULL, NULL, NULL);
```

---

### Fake Devices

For testing application code without full driver emulation.

#### Creating a Fake Driver

```c
/* fake_sensor.c */
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

static int32_t fake_temp_value = 25000;  /* 25.0°C in millicelsius */

void fake_sensor_set_temp(int32_t temp_mc)
{
    fake_temp_value = temp_mc;
}

static int fake_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
    return 0;
}

static int fake_channel_get(const struct device *dev,
                            enum sensor_channel chan,
                            struct sensor_value *val)
{
    if (chan != SENSOR_CHAN_AMBIENT_TEMP) {
        return -ENOTSUP;
    }

    val->val1 = fake_temp_value / 1000;
    val->val2 = (fake_temp_value % 1000) * 1000;
    return 0;
}

static DEVICE_API(sensor, fake_sensor_api) = {
    .sample_fetch = fake_sample_fetch,
    .channel_get = fake_channel_get,
};

static int fake_sensor_init(const struct device *dev)
{
    return 0;
}

DEVICE_DEFINE(fake_sensor, "FAKE_SENSOR", fake_sensor_init, NULL,
              NULL, NULL, POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,
              &fake_sensor_api);
```

#### Using in Tests

```c
#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

/* Declare fake setter (from fake_sensor.c) */
void fake_sensor_set_temp(int32_t temp_mc);

ZTEST(app_tests, test_temp_alarm)
{
    const struct device *sensor = device_get_binding("FAKE_SENSOR");
    bool alarm_triggered = false;

    /* Test normal temperature */
    fake_sensor_set_temp(25000);  /* 25°C */
    alarm_triggered = check_temp_alarm(sensor);
    zassert_false(alarm_triggered, "No alarm at 25°C");

    /* Test high temperature */
    fake_sensor_set_temp(85000);  /* 85°C */
    alarm_triggered = check_temp_alarm(sensor);
    zassert_true(alarm_triggered, "Alarm should trigger at 85°C");
}

ZTEST_SUITE(app_tests, NULL, NULL, NULL, NULL, NULL);
```

---

### Test Fixtures

Setup and teardown for tests.

#### Suite-Level Fixtures

```c
static const struct device *dev;

static void *suite_setup(void)
{
    dev = DEVICE_DT_GET(DT_NODELABEL(my_device));
    zassert_true(device_is_ready(dev), "Device not ready");

    /* Return state that can be passed to tests */
    return (void *)dev;
}

static void suite_teardown(void *fixture)
{
    /* Cleanup after all tests */
}

ZTEST_SUITE(driver_tests, NULL, suite_setup, NULL, NULL, suite_teardown);
```

#### Per-Test Fixtures

```c
struct test_fixture {
    const struct device *dev;
    uint8_t original_config;
};

static void before_each(void *fixture)
{
    struct test_fixture *f = fixture;

    /* Reset device to known state before each test */
    f->dev = DEVICE_DT_GET(DT_NODELABEL(my_device));
    /* Save original config */
    /* read_config(f->dev, &f->original_config); */
}

static void after_each(void *fixture)
{
    struct test_fixture *f = fixture;

    /* Restore original config after each test */
    /* write_config(f->dev, f->original_config); */
}

static void *suite_setup(void)
{
    static struct test_fixture fixture;
    return &fixture;
}

ZTEST_SUITE(driver_tests, NULL, suite_setup, before_each, after_each, NULL);

ZTEST_F(driver_tests, test_with_fixture)
{
    /* Access fixture via 'fixture' pointer */
    zassert_true(device_is_ready(fixture->dev));
}
```

---

### Running Tests

#### testcase.yaml

```yaml
tests:
  drivers.my_driver:
    tags: driver sensor
    platform_allow:
      - native_sim
      - nrf52840dk_nrf52840
    integration_platforms:
      - native_sim
    extra_configs:
      - CONFIG_MY_DRIVER=y
      - CONFIG_EMUL=y
```

#### CMakeLists.txt for Tests

```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(my_driver_test)

target_sources(app PRIVATE src/main.c)

# Include driver source if not built as module
# target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../my_driver.c)
```

#### prj.conf for Tests

```ini
CONFIG_ZTEST=y
CONFIG_LOG=y

# For native_sim
CONFIG_EMUL=y
CONFIG_I2C_EMUL=y
CONFIG_GPIO_EMUL=y

# Driver under test
CONFIG_MY_DRIVER=y
CONFIG_MY_DRIVER_LOG_LEVEL=4
```

#### Running with Twister

```bash
# Run all driver tests
west twister -T tests/drivers/

# Run specific test
west twister -T tests/drivers/my_driver/

# Run on native_sim only
west twister -T tests/drivers/ -p native_sim

# Verbose output
west twister -T tests/drivers/ -v

# Generate coverage report
west twister -T tests/drivers/ --coverage
```

#### Running Locally

```bash
# Build for native_sim
west build -b native_sim tests/drivers/my_driver/

# Run
./build/zephyr/zephyr.exe

# Or with west
west build -b native_sim tests/drivers/my_driver/ -t run
```

#### Test Output

```
*** Booting Zephyr OS build v3.x.x ***
Running TESTSUITE driver_tests
================================================================
START - test_device_ready
 PASS - test_device_ready in 0.001 seconds
================================================================
START - test_sample_fetch
 PASS - test_sample_fetch in 0.003 seconds
================================================================
START - test_channel_get
 PASS - test_channel_get in 0.002 seconds
================================================================
TESTSUITE driver_tests succeeded
------ TESTSUITE SUMMARY ------
SUITE PASS - 100.00% [driver_tests]: pass = 3, fail = 0, skip = 0
```

## Examples

### Table of Contents

1. [GPIO LED Driver](#gpio-led-driver)
2. [I2C Temperature Sensor](#i2c-temperature-sensor)
3. [SPI Flash Driver](#spi-flash-driver)
4. [UART GPS Module](#uart-gps-module)
5. [Complete Sensor with Triggers](#complete-sensor-with-triggers)

---

### GPIO LED Driver

Complete example of a simple GPIO-controlled LED driver.

#### Files

```
led_driver/
├── CMakeLists.txt
├── Kconfig
├── led_driver.c
└── dts/bindings/
    └── custom,led.yaml
```

#### Binding: `custom,led.yaml`

```yaml
description: Custom LED driver

compatible: "custom,led"

include: base.yaml

properties:
  led-gpios:
    type: phandle-array
    required: true
    description: GPIO connected to LED

  default-brightness:
    type: int
    default: 100
    description: Default brightness percentage (0-100)
```

#### Kconfig

```kconfig
config CUSTOM_LED
    bool "Custom LED driver"
    default y
    depends on GPIO
    help
      Enable custom LED driver.

config CUSTOM_LED_INIT_PRIORITY
    int "Initialization priority"
    default 60
    depends on CUSTOM_LED
```

#### Driver: `led_driver.c`

```c
#define DT_DRV_COMPAT custom_led

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(custom_led, CONFIG_LED_LOG_LEVEL);

struct led_config {
    struct gpio_dt_spec gpio;
    uint8_t default_brightness;
};

struct led_data {
    bool is_on;
    uint8_t brightness;
};

/* LED API */
static int led_on(const struct device *dev)
{
    const struct led_config *cfg = dev->config;
    struct led_data *data = dev->data;

    gpio_pin_set_dt(&cfg->gpio, 1);
    data->is_on = true;
    return 0;
}

static int led_off(const struct device *dev)
{
    const struct led_config *cfg = dev->config;
    struct led_data *data = dev->data;

    gpio_pin_set_dt(&cfg->gpio, 0);
    data->is_on = false;
    return 0;
}

static int led_toggle(const struct device *dev)
{
    struct led_data *data = dev->data;

    if (data->is_on) {
        return led_off(dev);
    } else {
        return led_on(dev);
    }
}

static int led_set_brightness(const struct device *dev, uint8_t brightness)
{
    const struct led_config *cfg = dev->config;
    struct led_data *data = dev->data;

    /* Simple on/off for GPIO LED */
    data->brightness = brightness;
    if (brightness > 0) {
        gpio_pin_set_dt(&cfg->gpio, 1);
        data->is_on = true;
    } else {
        gpio_pin_set_dt(&cfg->gpio, 0);
        data->is_on = false;
    }
    return 0;
}

/* Standard LED driver API */
static DEVICE_API(led, led_api) = {
    .on = led_on,
    .off = led_off,
    .set_brightness = led_set_brightness,
};

static int led_init(const struct device *dev)
{
    const struct led_config *cfg = dev->config;
    struct led_data *data = dev->data;

    if (!gpio_is_ready_dt(&cfg->gpio)) {
        LOG_ERR("GPIO device not ready");
        return -ENODEV;
    }

    int ret = gpio_pin_configure_dt(&cfg->gpio, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        LOG_ERR("Failed to configure GPIO: %d", ret);
        return ret;
    }

    data->is_on = false;
    data->brightness = cfg->default_brightness;

    LOG_INF("LED %s initialized", dev->name);
    return 0;
}

#define LED_DEFINE(inst)                                               \
    static struct led_data led_data_##inst;                            \
    static const struct led_config led_config_##inst = {               \
        .gpio = GPIO_DT_SPEC_INST_GET(inst, led_gpios),                \
        .default_brightness = DT_INST_PROP(inst, default_brightness),  \
    };                                                                 \
    DEVICE_DT_INST_DEFINE(inst, led_init, NULL,                        \
                          &led_data_##inst, &led_config_##inst,        \
                          POST_KERNEL, CONFIG_CUSTOM_LED_INIT_PRIORITY,\
                          &led_api);

DT_INST_FOREACH_STATUS_OKAY(LED_DEFINE)
```

#### Overlay

```dts
/ {
    my_led: led_0 {
        compatible = "custom,led";
        led-gpios = <&gpio0 13 GPIO_ACTIVE_HIGH>;
        default-brightness = <100>;
    };
};
```

#### Application Usage

```c
#include <zephyr/drivers/led.h>

const struct device *led = DEVICE_DT_GET(DT_NODELABEL(my_led));

int main(void)
{
    if (!device_is_ready(led)) {
        return -1;
    }

    while (1) {
        led_on(led, 0);
        k_sleep(K_MSEC(500));
        led_off(led, 0);
        k_sleep(K_MSEC(500));
    }
}
```

---

### I2C Temperature Sensor

Complete I2C temperature sensor driver with sensor API integration.

#### Files

```
temp_sensor/
├── CMakeLists.txt
├── Kconfig
├── temp_sensor.c
└── dts/bindings/
    └── acme,tmp101.yaml
```

#### Binding: `acme,tmp101.yaml`

```yaml
description: ACME TMP101 Temperature Sensor

compatible: "acme,tmp101"

include: [i2c-device.yaml]

properties:
  alert-gpios:
    type: phandle-array
    description: Alert/interrupt GPIO

  resolution:
    type: int
    default: 12
    enum: [9, 10, 11, 12]
    description: ADC resolution in bits
```

#### Kconfig

```kconfig
config ACME_TMP101
    bool "ACME TMP101 temperature sensor"
    default y
    depends on I2C
    select SENSOR
    help
      Enable ACME TMP101 temperature sensor driver.

config ACME_TMP101_INIT_PRIORITY
    int "Init priority"
    default 90
    depends on ACME_TMP101
```

#### Driver: `temp_sensor.c`

```c
#define DT_DRV_COMPAT acme_tmp101

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(tmp101, CONFIG_SENSOR_LOG_LEVEL);

/* Register addresses */
#define REG_TEMP        0x00
#define REG_CONFIG      0x01
#define REG_TLOW        0x02
#define REG_THIGH       0x03

/* Configuration bits */
#define CONFIG_OS       BIT(7)  /* One-shot */
#define CONFIG_RES_MASK (BIT(6) | BIT(5))
#define CONFIG_RES_SHIFT 5

struct tmp101_config {
    struct i2c_dt_spec i2c;
    struct gpio_dt_spec alert;
    uint8_t resolution;
};

struct tmp101_data {
    int16_t temp_raw;
};

/* Helper functions */
static int tmp101_read_reg(const struct i2c_dt_spec *i2c,
                           uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_write_read_dt(i2c, &reg, 1, data, len);
}

static int tmp101_write_reg(const struct i2c_dt_spec *i2c,
                            uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = {reg, value};
    return i2c_write_dt(i2c, buf, sizeof(buf));
}

/* Sensor API */
static int tmp101_sample_fetch(const struct device *dev,
                               enum sensor_channel chan)
{
    const struct tmp101_config *cfg = dev->config;
    struct tmp101_data *data = dev->data;
    uint8_t buf[2];
    int ret;

    if (chan != SENSOR_CHAN_ALL && chan != SENSOR_CHAN_AMBIENT_TEMP) {
        return -ENOTSUP;
    }

    ret = tmp101_read_reg(&cfg->i2c, REG_TEMP, buf, sizeof(buf));
    if (ret < 0) {
        LOG_ERR("Failed to read temperature: %d", ret);
        return ret;
    }

    /* Temperature is in 2's complement, MSB first */
    data->temp_raw = (buf[0] << 8) | buf[1];

    /* Right-justify based on resolution */
    data->temp_raw >>= (16 - cfg->resolution);

    return 0;
}

static int tmp101_channel_get(const struct device *dev,
                              enum sensor_channel chan,
                              struct sensor_value *val)
{
    const struct tmp101_config *cfg = dev->config;
    struct tmp101_data *data = dev->data;

    if (chan != SENSOR_CHAN_AMBIENT_TEMP) {
        return -ENOTSUP;
    }

    /* Calculate temperature based on resolution
     * 12-bit: 0.0625°C per LSB
     * 11-bit: 0.125°C per LSB
     * 10-bit: 0.25°C per LSB
     * 9-bit:  0.5°C per LSB
     */
    int32_t lsb_uc;  /* Micro-celsius per LSB */
    switch (cfg->resolution) {
    case 9:  lsb_uc = 500000; break;
    case 10: lsb_uc = 250000; break;
    case 11: lsb_uc = 125000; break;
    case 12:
    default: lsb_uc = 62500; break;
    }

    int64_t temp_uc = (int64_t)data->temp_raw * lsb_uc;
    val->val1 = temp_uc / 1000000;
    val->val2 = temp_uc % 1000000;

    return 0;
}

static DEVICE_API(sensor, tmp101_api) = {
    .sample_fetch = tmp101_sample_fetch,
    .channel_get = tmp101_channel_get,
};

static int tmp101_init(const struct device *dev)
{
    const struct tmp101_config *cfg = dev->config;
    uint8_t config;
    int ret;

    if (!i2c_is_ready_dt(&cfg->i2c)) {
        LOG_ERR("I2C bus not ready");
        return -ENODEV;
    }

    /* Read current config */
    ret = tmp101_read_reg(&cfg->i2c, REG_CONFIG, &config, 1);
    if (ret < 0) {
        LOG_ERR("Failed to read config: %d", ret);
        return ret;
    }

    /* Set resolution */
    config &= ~CONFIG_RES_MASK;
    config |= ((cfg->resolution - 9) << CONFIG_RES_SHIFT);

    ret = tmp101_write_reg(&cfg->i2c, REG_CONFIG, config);
    if (ret < 0) {
        LOG_ERR("Failed to write config: %d", ret);
        return ret;
    }

    LOG_INF("TMP101 %s initialized (resolution: %d-bit)",
            dev->name, cfg->resolution);

    return 0;
}

#define TMP101_DEFINE(inst)                                             \
    static struct tmp101_data tmp101_data_##inst;                       \
    static const struct tmp101_config tmp101_config_##inst = {          \
        .i2c = I2C_DT_SPEC_INST_GET(inst),                              \
        .alert = GPIO_DT_SPEC_INST_GET_OR(inst, alert_gpios, {0}),      \
        .resolution = DT_INST_PROP(inst, resolution),                   \
    };                                                                  \
    DEVICE_DT_INST_DEFINE(inst, tmp101_init, NULL,                      \
                          &tmp101_data_##inst,                          \
                          &tmp101_config_##inst,                        \
                          POST_KERNEL,                                  \
                          CONFIG_ACME_TMP101_INIT_PRIORITY,             \
                          &tmp101_api);

DT_INST_FOREACH_STATUS_OKAY(TMP101_DEFINE)
```

#### Overlay

```dts
&i2c0 {
    status = "okay";

    tmp101: temperature@48 {
        compatible = "acme,tmp101";
        reg = <0x48>;
        resolution = <12>;
    };
};
```

#### Application Usage

```c
#include <zephyr/drivers/sensor.h>

const struct device *temp = DEVICE_DT_GET(DT_NODELABEL(tmp101));

int main(void)
{
    struct sensor_value val;

    if (!device_is_ready(temp)) {
        return -1;
    }

    while (1) {
        sensor_sample_fetch(temp);
        sensor_channel_get(temp, SENSOR_CHAN_AMBIENT_TEMP, &val);
        printk("Temperature: %d.%06d C\n", val.val1, val.val2);
        k_sleep(K_SECONDS(1));
    }
}
```

---

### SPI Flash Driver

Complete SPI NOR flash driver example.

#### Files

```
spi_flash/
├── CMakeLists.txt
├── Kconfig
├── spi_flash.c
└── dts/bindings/
    └── acme,spi-flash.yaml
```

#### Binding: `acme,spi-flash.yaml`

```yaml
description: ACME SPI NOR Flash

compatible: "acme,spi-flash"

include: [spi-device.yaml]

properties:
  size:
    type: int
    required: true
    description: Flash size in bytes

  page-size:
    type: int
    default: 256
    description: Page size for programming

  sector-size:
    type: int
    default: 4096
    description: Sector size for erase

  jedec-id:
    type: uint8-array
    description: Expected JEDEC ID bytes

  wp-gpios:
    type: phandle-array
    description: Write protect GPIO

  hold-gpios:
    type: phandle-array
    description: Hold GPIO
```

#### Driver: `spi_flash.c`

```c
#define DT_DRV_COMPAT acme_spi_flash

#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(acme_flash, CONFIG_FLASH_LOG_LEVEL);

/* Flash commands */
#define CMD_READ_ID      0x9F
#define CMD_READ_STATUS  0x05
#define CMD_WRITE_ENABLE 0x06
#define CMD_READ         0x03
#define CMD_PAGE_PROGRAM 0x02
#define CMD_SECTOR_ERASE 0x20
#define CMD_CHIP_ERASE   0xC7

#define STATUS_WIP       BIT(0)  /* Write in progress */
#define STATUS_WEL       BIT(1)  /* Write enable latch */

struct flash_config {
    struct spi_dt_spec spi;
    struct gpio_dt_spec wp;
    uint32_t size;
    uint16_t page_size;
    uint16_t sector_size;
    uint8_t jedec_id[3];
};

struct flash_data {
    struct k_sem lock;
};

/* SPI transaction helpers */
static int flash_read_status(const struct spi_dt_spec *spi, uint8_t *status)
{
    uint8_t cmd = CMD_READ_STATUS;
    uint8_t rx[2];

    struct spi_buf tx_buf = {.buf = &cmd, .len = 1};
    struct spi_buf rx_buf = {.buf = rx, .len = 2};
    struct spi_buf_set tx = {.buffers = &tx_buf, .count = 1};
    struct spi_buf_set rx_set = {.buffers = &rx_buf, .count = 1};

    int ret = spi_transceive_dt(spi, &tx, &rx_set);
    if (ret == 0) {
        *status = rx[1];
    }
    return ret;
}

static int flash_wait_ready(const struct spi_dt_spec *spi, k_timeout_t timeout)
{
    int64_t end = k_uptime_get() + k_ticks_to_ms_floor64(timeout.ticks);
    uint8_t status;

    do {
        int ret = flash_read_status(spi, &status);
        if (ret < 0) {
            return ret;
        }
        if (!(status & STATUS_WIP)) {
            return 0;
        }
        k_sleep(K_MSEC(1));
    } while (k_uptime_get() < end);

    return -ETIMEDOUT;
}

static int flash_write_enable(const struct spi_dt_spec *spi)
{
    uint8_t cmd = CMD_WRITE_ENABLE;
    struct spi_buf buf = {.buf = &cmd, .len = 1};
    struct spi_buf_set tx = {.buffers = &buf, .count = 1};
    return spi_write_dt(spi, &tx);
}

/* Flash API implementation */
static int flash_read(const struct device *dev, off_t offset,
                      void *data, size_t len)
{
    const struct flash_config *cfg = dev->config;
    struct flash_data *drv = dev->data;
    uint8_t cmd[4] = {
        CMD_READ,
        (offset >> 16) & 0xFF,
        (offset >> 8) & 0xFF,
        offset & 0xFF,
    };
    int ret;

    if (offset + len > cfg->size) {
        return -EINVAL;
    }

    struct spi_buf tx_buf = {.buf = cmd, .len = sizeof(cmd)};
    struct spi_buf rx_bufs[] = {
        {.buf = NULL, .len = sizeof(cmd)},  /* Dummy for command */
        {.buf = data, .len = len},
    };
    struct spi_buf_set tx = {.buffers = &tx_buf, .count = 1};
    struct spi_buf_set rx = {.buffers = rx_bufs, .count = 2};

    k_sem_take(&drv->lock, K_FOREVER);
    ret = spi_transceive_dt(&cfg->spi, &tx, &rx);
    k_sem_give(&drv->lock);

    return ret;
}

static int flash_write(const struct device *dev, off_t offset,
                       const void *data, size_t len)
{
    const struct flash_config *cfg = dev->config;
    struct flash_data *drv = dev->data;
    const uint8_t *src = data;
    int ret;

    if (offset + len > cfg->size) {
        return -EINVAL;
    }

    k_sem_take(&drv->lock, K_FOREVER);

    while (len > 0) {
        /* Calculate bytes to write in current page */
        size_t page_offset = offset % cfg->page_size;
        size_t write_len = MIN(len, cfg->page_size - page_offset);

        /* Enable writes */
        ret = flash_write_enable(&cfg->spi);
        if (ret < 0) {
            goto out;
        }

        /* Send page program command */
        uint8_t cmd[4] = {
            CMD_PAGE_PROGRAM,
            (offset >> 16) & 0xFF,
            (offset >> 8) & 0xFF,
            offset & 0xFF,
        };

        struct spi_buf tx_bufs[] = {
            {.buf = cmd, .len = sizeof(cmd)},
            {.buf = (void *)src, .len = write_len},
        };
        struct spi_buf_set tx = {.buffers = tx_bufs, .count = 2};

        ret = spi_write_dt(&cfg->spi, &tx);
        if (ret < 0) {
            goto out;
        }

        /* Wait for write complete */
        ret = flash_wait_ready(&cfg->spi, K_MSEC(10));
        if (ret < 0) {
            goto out;
        }

        offset += write_len;
        src += write_len;
        len -= write_len;
    }

    ret = 0;

out:
    k_sem_give(&drv->lock);
    return ret;
}

static int flash_erase(const struct device *dev, off_t offset, size_t size)
{
    const struct flash_config *cfg = dev->config;
    struct flash_data *drv = dev->data;
    int ret;

    if ((offset % cfg->sector_size) || (size % cfg->sector_size)) {
        return -EINVAL;
    }

    k_sem_take(&drv->lock, K_FOREVER);

    while (size > 0) {
        ret = flash_write_enable(&cfg->spi);
        if (ret < 0) {
            goto out;
        }

        uint8_t cmd[4] = {
            CMD_SECTOR_ERASE,
            (offset >> 16) & 0xFF,
            (offset >> 8) & 0xFF,
            offset & 0xFF,
        };

        struct spi_buf buf = {.buf = cmd, .len = sizeof(cmd)};
        struct spi_buf_set tx = {.buffers = &buf, .count = 1};

        ret = spi_write_dt(&cfg->spi, &tx);
        if (ret < 0) {
            goto out;
        }

        ret = flash_wait_ready(&cfg->spi, K_SECONDS(1));
        if (ret < 0) {
            goto out;
        }

        offset += cfg->sector_size;
        size -= cfg->sector_size;
    }

    ret = 0;

out:
    k_sem_give(&drv->lock);
    return ret;
}

static const struct flash_parameters *flash_get_parameters(
    const struct device *dev)
{
    static const struct flash_parameters params = {
        .write_block_size = 1,
        .erase_value = 0xFF,
    };
    return &params;
}

static DEVICE_API(flash, flash_api) = {
    .read = flash_read,
    .write = flash_write,
    .erase = flash_erase,
    .get_parameters = flash_get_parameters,
};

static int flash_init(const struct device *dev)
{
    const struct flash_config *cfg = dev->config;
    struct flash_data *drv = dev->data;
    uint8_t jedec_id[3];
    int ret;

    if (!spi_is_ready_dt(&cfg->spi)) {
        LOG_ERR("SPI bus not ready");
        return -ENODEV;
    }

    k_sem_init(&drv->lock, 1, 1);

    /* Read JEDEC ID */
    uint8_t cmd = CMD_READ_ID;
    struct spi_buf tx_buf = {.buf = &cmd, .len = 1};
    struct spi_buf rx_bufs[] = {
        {.buf = NULL, .len = 1},
        {.buf = jedec_id, .len = 3},
    };
    struct spi_buf_set tx = {.buffers = &tx_buf, .count = 1};
    struct spi_buf_set rx = {.buffers = rx_bufs, .count = 2};

    ret = spi_transceive_dt(&cfg->spi, &tx, &rx);
    if (ret < 0) {
        LOG_ERR("Failed to read JEDEC ID: %d", ret);
        return ret;
    }

    LOG_INF("Flash %s: JEDEC ID %02x %02x %02x, size %u bytes",
            dev->name, jedec_id[0], jedec_id[1], jedec_id[2], cfg->size);

    return 0;
}

#define FLASH_DEFINE(inst)                                              \
    static struct flash_data flash_data_##inst;                         \
    static const struct flash_config flash_config_##inst = {            \
        .spi = SPI_DT_SPEC_INST_GET(inst,                               \
                   SPI_WORD_SET(8) | SPI_TRANSFER_MSB, 0),              \
        .wp = GPIO_DT_SPEC_INST_GET_OR(inst, wp_gpios, {0}),            \
        .size = DT_INST_PROP(inst, size),                               \
        .page_size = DT_INST_PROP(inst, page_size),                     \
        .sector_size = DT_INST_PROP(inst, sector_size),                 \
    };                                                                  \
    DEVICE_DT_INST_DEFINE(inst, flash_init, NULL,                       \
                          &flash_data_##inst,                           \
                          &flash_config_##inst,                         \
                          POST_KERNEL,                                  \
                          CONFIG_FLASH_INIT_PRIORITY,                   \
                          &flash_api);

DT_INST_FOREACH_STATUS_OKAY(FLASH_DEFINE)
```

#### Overlay

```dts
&spi1 {
    status = "okay";
    cs-gpios = <&gpio0 25 GPIO_ACTIVE_LOW>;

    flash0: flash@0 {
        compatible = "acme,spi-flash";
        reg = <0>;
        spi-max-frequency = <8000000>;
        size = <0x100000>;  /* 1MB */
        page-size = <256>;
        sector-size = <4096>;
    };
};
```

#### Application Usage

```c
#include <zephyr/drivers/flash.h>

const struct device *flash = DEVICE_DT_GET(DT_NODELABEL(flash0));

int main(void)
{
    uint8_t data[256];

    if (!device_is_ready(flash)) {
        return -1;
    }

    /* Erase sector */
    flash_erase(flash, 0, 4096);

    /* Write data */
    memset(data, 0xAA, sizeof(data));
    flash_write(flash, 0, data, sizeof(data));

    /* Read back */
    memset(data, 0, sizeof(data));
    flash_read(flash, 0, data, sizeof(data));

    return 0;
}
```

---

### UART GPS Module

Complete UART-based GPS module driver.

#### Binding: `acme,uart-gps.yaml`

```yaml
description: ACME UART GPS Module

compatible: "acme,uart-gps"

include: [uart-device.yaml]

properties:
  reset-gpios:
    type: phandle-array
    description: Hardware reset GPIO

  enable-gpios:
    type: phandle-array
    description: Enable/power GPIO

  pps-gpios:
    type: phandle-array
    description: Pulse per second GPIO
```

#### Driver: `uart_gps.c`

```c
#define DT_DRV_COMPAT acme_uart_gps

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(uart_gps, CONFIG_GPS_LOG_LEVEL);

#define RX_BUFFER_SIZE 256
#define NMEA_MAX_LEN   82

typedef void (*gps_nmea_callback_t)(const struct device *dev,
                                    const char *sentence);

struct gps_config {
    const struct device *uart;
    struct gpio_dt_spec reset;
    struct gpio_dt_spec enable;
    struct gpio_dt_spec pps;
};

struct gps_data {
    uint8_t rx_buf[RX_BUFFER_SIZE];
    size_t rx_pos;
    char nmea_sentence[NMEA_MAX_LEN + 1];
    gps_nmea_callback_t callback;
    bool enabled;
};

/* UART ISR */
static void gps_uart_isr(const struct device *uart, void *user_data)
{
    const struct device *dev = user_data;
    struct gps_data *data = dev->data;

    uart_irq_update(uart);

    while (uart_irq_rx_ready(uart)) {
        uint8_t c;

        if (uart_fifo_read(uart, &c, 1) != 1) {
            break;
        }

        /* Start of NMEA sentence */
        if (c == '$') {
            data->rx_pos = 0;
        }

        /* Store character */
        if (data->rx_pos < RX_BUFFER_SIZE - 1) {
            data->rx_buf[data->rx_pos++] = c;
        }

        /* End of NMEA sentence */
        if (c == '\n') {
            data->rx_buf[data->rx_pos] = '\0';

            /* Copy to callback buffer */
            if (data->callback && data->rx_pos > 0) {
                memcpy(data->nmea_sentence, data->rx_buf,
                       MIN(data->rx_pos + 1, NMEA_MAX_LEN));
                data->nmea_sentence[NMEA_MAX_LEN] = '\0';
                data->callback(dev, data->nmea_sentence);
            }

            data->rx_pos = 0;
        }
    }
}

/* GPS API */
static int gps_enable(const struct device *dev)
{
    const struct gps_config *cfg = dev->config;
    struct gps_data *data = dev->data;

    if (cfg->enable.port) {
        gpio_pin_set_dt(&cfg->enable, 1);
        k_sleep(K_MSEC(100));
    }

    uart_irq_rx_enable(cfg->uart);
    data->enabled = true;

    LOG_INF("GPS enabled");
    return 0;
}

static int gps_disable(const struct device *dev)
{
    const struct gps_config *cfg = dev->config;
    struct gps_data *data = dev->data;

    uart_irq_rx_disable(cfg->uart);

    if (cfg->enable.port) {
        gpio_pin_set_dt(&cfg->enable, 0);
    }

    data->enabled = false;
    return 0;
}

static int gps_reset(const struct device *dev)
{
    const struct gps_config *cfg = dev->config;

    if (!cfg->reset.port) {
        return -ENOTSUP;
    }

    gpio_pin_set_dt(&cfg->reset, 1);
    k_sleep(K_MSEC(100));
    gpio_pin_set_dt(&cfg->reset, 0);
    k_sleep(K_MSEC(500));

    return 0;
}

static int gps_set_callback(const struct device *dev,
                            gps_nmea_callback_t callback)
{
    struct gps_data *data = dev->data;
    data->callback = callback;
    return 0;
}

static int gps_send_command(const struct device *dev, const char *cmd)
{
    const struct gps_config *cfg = dev->config;

    for (size_t i = 0; cmd[i] != '\0'; i++) {
        uart_poll_out(cfg->uart, cmd[i]);
    }
    uart_poll_out(cfg->uart, '\r');
    uart_poll_out(cfg->uart, '\n');

    return 0;
}

struct gps_driver_api {
    int (*enable)(const struct device *dev);
    int (*disable)(const struct device *dev);
    int (*reset)(const struct device *dev);
    int (*set_callback)(const struct device *dev, gps_nmea_callback_t cb);
    int (*send_command)(const struct device *dev, const char *cmd);
};

/* DEVICE_API(gps, ...) expands the class name to `gps_driver_api`, so a
 * custom class works as long as you follow the `<class>_driver_api` naming.
 */
static DEVICE_API(gps, gps_api) = {
    .enable = gps_enable,
    .disable = gps_disable,
    .reset = gps_reset,
    .set_callback = gps_set_callback,
    .send_command = gps_send_command,
};

static int gps_init(const struct device *dev)
{
    const struct gps_config *cfg = dev->config;
    struct gps_data *data = dev->data;

    if (!device_is_ready(cfg->uart)) {
        LOG_ERR("UART not ready");
        return -ENODEV;
    }

    /* Configure GPIOs */
    if (cfg->reset.port && gpio_is_ready_dt(&cfg->reset)) {
        gpio_pin_configure_dt(&cfg->reset, GPIO_OUTPUT_INACTIVE);
    }

    if (cfg->enable.port && gpio_is_ready_dt(&cfg->enable)) {
        gpio_pin_configure_dt(&cfg->enable, GPIO_OUTPUT_INACTIVE);
    }

    if (cfg->pps.port && gpio_is_ready_dt(&cfg->pps)) {
        gpio_pin_configure_dt(&cfg->pps, GPIO_INPUT);
    }

    data->rx_pos = 0;
    data->enabled = false;

    /* Set up UART ISR */
    uart_irq_callback_user_data_set(cfg->uart, gps_uart_isr, (void *)dev);

    LOG_INF("GPS %s initialized", dev->name);
    return 0;
}

#define GPS_DEFINE(inst)                                                \
    static struct gps_data gps_data_##inst;                             \
    static const struct gps_config gps_config_##inst = {                \
        .uart = DEVICE_DT_GET(DT_INST_BUS(inst)),                       \
        .reset = GPIO_DT_SPEC_INST_GET_OR(inst, reset_gpios, {0}),      \
        .enable = GPIO_DT_SPEC_INST_GET_OR(inst, enable_gpios, {0}),    \
        .pps = GPIO_DT_SPEC_INST_GET_OR(inst, pps_gpios, {0}),          \
    };                                                                  \
    DEVICE_DT_INST_DEFINE(inst, gps_init, NULL,                         \
                          &gps_data_##inst,                             \
                          &gps_config_##inst,                           \
                          POST_KERNEL,                                  \
                          CONFIG_GPS_INIT_PRIORITY,                     \
                          &gps_api);

DT_INST_FOREACH_STATUS_OKAY(GPS_DEFINE)
```

#### Overlay

```dts
&uart1 {
    status = "okay";
    current-speed = <9600>;

    gps: gps {
        compatible = "acme,uart-gps";
        reset-gpios = <&gpio0 10 GPIO_ACTIVE_HIGH>;
        enable-gpios = <&gpio0 11 GPIO_ACTIVE_HIGH>;
    };
};
```

---

### Complete Sensor with Triggers

See [sensor-drivers.md](sensor-drivers.md) for a complete sensor driver with interrupt-based triggers.

## Locations

### Table of Contents

1. [Driver Source Code](#driver-source-code)
2. [Devicetree Bindings](#devicetree-bindings)
3. [Samples and Examples](#samples-and-examples)
4. [Tests](#tests)
5. [Documentation](#documentation)

---

### Driver Source Code

#### Main Driver Directories

| Path | Contents |
|------|----------|
| `zephyr/drivers/` | All driver implementations |
| `zephyr/drivers/gpio/` | GPIO drivers |
| `zephyr/drivers/i2c/` | I2C bus drivers |
| `zephyr/drivers/spi/` | SPI bus drivers |
| `zephyr/drivers/sensor/` | Sensor drivers |
| `zephyr/drivers/serial/` | UART/Serial drivers |
| `zephyr/drivers/adc/` | ADC drivers |
| `zephyr/drivers/pwm/` | PWM drivers |
| `zephyr/drivers/flash/` | Flash memory drivers |
| `zephyr/drivers/display/` | Display drivers |
| `zephyr/drivers/led/` | LED drivers |
| `zephyr/drivers/kscan/` | Keyboard scan drivers |
| `zephyr/drivers/rtc/` | Real-time clock drivers |
| `zephyr/drivers/watchdog/` | Watchdog drivers |
| `zephyr/drivers/counter/` | Counter/Timer drivers |
| `zephyr/drivers/can/` | CAN bus drivers |
| `zephyr/drivers/bluetooth/` | Bluetooth HCI drivers |
| `zephyr/drivers/wifi/` | WiFi drivers |
| `zephyr/drivers/ethernet/` | Ethernet drivers |

#### Sensor Driver Subdirectories

| Path | Sensor Type |
|------|-------------|
| `drivers/sensor/adi/` | Analog Devices sensors |
| `drivers/sensor/bosch/` | Bosch sensors (BME280, BMP388, etc.) |
| `drivers/sensor/st/` | STMicroelectronics sensors (LIS2DH, LSM6DSO, etc.) |
| `drivers/sensor/ti/` | Texas Instruments sensors |
| `drivers/sensor/invensense/` | InvenSense/TDK sensors (MPU6050, ICM42688, etc.) |
| `drivers/sensor/sensirion/` | Sensirion sensors (SHT4x, SCD4x, etc.) |
| `drivers/sensor/asahi_kasei/` | AKM sensors (AK09918, etc.) |

#### Header Files

| Path | Contents |
|------|----------|
| `zephyr/include/zephyr/drivers/` | Public driver API headers |
| `zephyr/include/zephyr/drivers/gpio.h` | GPIO API |
| `zephyr/include/zephyr/drivers/i2c.h` | I2C API |
| `zephyr/include/zephyr/drivers/spi.h` | SPI API |
| `zephyr/include/zephyr/drivers/sensor.h` | Sensor API |
| `zephyr/include/zephyr/drivers/uart.h` | UART API |

---

### Devicetree Bindings

#### Binding Locations

| Path | Contents |
|------|----------|
| `zephyr/dts/bindings/` | All devicetree bindings |
| `zephyr/dts/bindings/gpio/` | GPIO controller bindings |
| `zephyr/dts/bindings/i2c/` | I2C controller bindings |
| `zephyr/dts/bindings/spi/` | SPI controller bindings |
| `zephyr/dts/bindings/sensor/` | Sensor device bindings |
| `zephyr/dts/bindings/serial/` | UART bindings |
| `zephyr/dts/bindings/base/` | Base bindings (base.yaml, i2c-device.yaml, etc.) |

#### Common Base Bindings

| File | Use For |
|------|---------|
| `base/base.yaml` | All devices (provides status, compatible) |
| `base/i2c-device.yaml` | I2C slave devices |
| `base/spi-device.yaml` | SPI slave devices |
| `base/uart-device.yaml` | UART child devices |
| `gpio/gpio-controller.yaml` | GPIO controllers |

#### Finding Bindings by Compatible

```bash
# Search for a specific compatible string
find $ZEPHYR_BASE/dts/bindings -name "*.yaml" | xargs grep -l "compatible.*bme280"

# List all sensor bindings
ls $ZEPHYR_BASE/dts/bindings/sensor/
```

---

### Samples and Examples

#### Driver Samples

| Path | Description |
|------|-------------|
| `zephyr/samples/basic/blinky/` | Basic GPIO LED example |
| `zephyr/samples/basic/button/` | GPIO button with interrupt |
| `zephyr/samples/drivers/` | All driver samples |
| `zephyr/samples/drivers/gpio/` | GPIO-specific samples |
| `zephyr/samples/drivers/i2c_scanner/` | I2C bus scanner |
| `zephyr/samples/drivers/spi_flash/` | SPI flash usage |
| `zephyr/samples/drivers/adc/` | ADC reading samples |
| `zephyr/samples/drivers/pwm/` | PWM control samples |
| `zephyr/samples/drivers/uart/` | UART samples |

#### Sensor Samples

| Path | Description |
|------|-------------|
| `zephyr/samples/sensor/` | All sensor samples |
| `zephyr/samples/sensor/bme280/` | BME280 temperature/pressure/humidity |
| `zephyr/samples/sensor/bme680/` | BME680 air quality |
| `zephyr/samples/sensor/lis2dh/` | LIS2DH accelerometer |
| `zephyr/samples/sensor/lsm6dso/` | LSM6DSO IMU |
| `zephyr/samples/sensor/thermometer/` | Generic temperature sensor |
| `zephyr/samples/sensor/accel_polling/` | Generic accelerometer polling |

#### Application-Level Samples

| Path | Description |
|------|-------------|
| `zephyr/samples/boards/` | Board-specific examples |
| `zephyr/samples/bluetooth/peripheral_hr/` | BLE with sensor data |
| `zephyr/samples/net/sockets/echo_server/` | Network with driver usage |

---

### Tests

#### Driver Tests

| Path | Contents |
|------|----------|
| `zephyr/tests/drivers/` | All driver tests |
| `zephyr/tests/drivers/gpio/` | GPIO driver tests |
| `zephyr/tests/drivers/i2c/` | I2C driver tests |
| `zephyr/tests/drivers/spi/` | SPI driver tests |
| `zephyr/tests/drivers/sensor/` | Sensor driver tests |
| `zephyr/tests/drivers/uart/` | UART driver tests |
| `zephyr/tests/drivers/adc/` | ADC driver tests |
| `zephyr/tests/drivers/flash/` | Flash driver tests |
| `zephyr/tests/drivers/build_all/` | Build smoke tests |

#### Emulator Tests

| Path | Contents |
|------|----------|
| `zephyr/tests/drivers/gpio/gpio_emul/` | GPIO emulator tests |
| `zephyr/tests/drivers/i2c/i2c_emul/` | I2C emulator tests |
| `zephyr/tests/drivers/spi/spi_emul/` | SPI emulator tests |

#### Running Driver Tests

```bash
# Run all driver tests on native_sim
west twister -T tests/drivers/ -p native_sim

# Run specific driver test
west twister -T tests/drivers/sensor/bme280/

# Run with hardware (e.g., nRF52840 DK)
west twister -T tests/drivers/gpio/ -p nrf52840dk_nrf52840 --device-testing
```

---

### Documentation

#### Driver Documentation

| Path | Contents |
|------|----------|
| `zephyr/doc/hardware/` | Hardware and driver docs |
| `zephyr/doc/hardware/peripherals/` | Peripheral driver guides |
| `zephyr/doc/hardware/peripherals/gpio.rst` | GPIO documentation |
| `zephyr/doc/hardware/peripherals/i2c.rst` | I2C documentation |
| `zephyr/doc/hardware/peripherals/spi.rst` | SPI documentation |
| `zephyr/doc/hardware/peripherals/sensor.rst` | Sensor subsystem docs |
| `zephyr/doc/hardware/porting/` | Porting guides |

#### Online Documentation

| Resource | URL |
|----------|-----|
| Zephyr Docs | https://docs.zephyrproject.org/latest/ |
| Driver API Reference | https://docs.zephyrproject.org/latest/doxygen/html/group__io__interfaces.html |
| Devicetree Guide | https://docs.zephyrproject.org/latest/build/dts/ |
| Sensor API | https://docs.zephyrproject.org/latest/doxygen/html/group__sensor__interface.html |

---

### Quick Reference Commands

```bash
# Find driver by compatible string
grep -r "DT_DRV_COMPAT.*bme280" $ZEPHYR_BASE/drivers/

# Find all drivers for a chip vendor
ls $ZEPHYR_BASE/drivers/sensor/bosch/

# Find binding for a device
find $ZEPHYR_BASE/dts/bindings -name "*bme280*"

# Find sample for a driver
find $ZEPHYR_BASE/samples -name "*bme280*" -type d

# Find tests for a driver
find $ZEPHYR_BASE/tests/drivers -name "*bme280*" -type d

# List all sensor drivers
ls $ZEPHYR_BASE/drivers/sensor/

# Search for driver API usage in samples
grep -r "sensor_sample_fetch" $ZEPHYR_BASE/samples/

# Find Kconfig for a driver
find $ZEPHYR_BASE/drivers -name "Kconfig*" | xargs grep -l "BME280"
```

---

### Out-of-Tree Driver Locations

For custom drivers in your project:

```
my_project/
├── drivers/
│   └── my_driver/
│       ├── CMakeLists.txt
│       ├── Kconfig
│       └── my_driver.c
├── dts/
│   └── bindings/
│       └── vendor,my-device.yaml
├── boards/
│   └── my_board.overlay
├── CMakeLists.txt
└── prj.conf
```

#### CMakeLists.txt Integration

```cmake
# Add before find_package(Zephyr)
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/drivers/my_driver)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
```
