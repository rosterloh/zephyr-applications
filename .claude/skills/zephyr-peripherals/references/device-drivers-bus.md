# Device Drivers: Bus Drivers

## Bus Drivers


### Table of Contents

1. [GPIO Device Driver](#gpio-device-driver)
2. [I2C Device Driver](#i2c-device-driver)
3. [SPI Device Driver](#spi-device-driver)
4. [UART Device Driver](#uart-device-driver)
5. [Mixed Bus Driver](#mixed-bus-driver)

---

### GPIO Device Driver

Drivers for simple GPIO-based devices (LEDs, relays, buttons).

#### Devicetree Binding

```yaml
# dts/bindings/vendor,gpio-relay.yaml
description: GPIO-controlled relay

compatible: "vendor,gpio-relay"

include: base.yaml

properties:
  control-gpios:
    type: phandle-array
    required: true
    description: GPIO for relay control

  active-time-ms:
    type: int
    default: 0
    description: Minimum active time in milliseconds
```

#### Driver Implementation

```c
#define DT_DRV_COMPAT vendor_gpio_relay

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

struct gpio_relay_config {
    struct gpio_dt_spec control;
    uint32_t active_time_ms;
};

struct gpio_relay_data {
    bool is_on;
};

/* Driver API */
static int gpio_relay_on(const struct device *dev)
{
    const struct gpio_relay_config *cfg = dev->config;
    struct gpio_relay_data *data = dev->data;

    gpio_pin_set_dt(&cfg->control, 1);
    data->is_on = true;
    return 0;
}

static int gpio_relay_off(const struct device *dev)
{
    const struct gpio_relay_config *cfg = dev->config;
    struct gpio_relay_data *data = dev->data;

    if (cfg->active_time_ms > 0 && data->is_on) {
        k_sleep(K_MSEC(cfg->active_time_ms));
    }

    gpio_pin_set_dt(&cfg->control, 0);
    data->is_on = false;
    return 0;
}

static bool gpio_relay_is_on(const struct device *dev)
{
    struct gpio_relay_data *data = dev->data;
    return data->is_on;
}

struct gpio_relay_api {
    int (*on)(const struct device *dev);
    int (*off)(const struct device *dev);
    bool (*is_on)(const struct device *dev);
};

static const struct gpio_relay_api relay_api = {
    .on = gpio_relay_on,
    .off = gpio_relay_off,
    .is_on = gpio_relay_is_on,
};

/* Initialization */
static int gpio_relay_init(const struct device *dev)
{
    const struct gpio_relay_config *cfg = dev->config;

    if (!gpio_is_ready_dt(&cfg->control)) {
        return -ENODEV;
    }

    return gpio_pin_configure_dt(&cfg->control, GPIO_OUTPUT_INACTIVE);
}

/* Instantiation */
#define GPIO_RELAY_DEFINE(inst)                                      \
    static struct gpio_relay_data relay_data_##inst;                 \
    static const struct gpio_relay_config relay_config_##inst = {    \
        .control = GPIO_DT_SPEC_INST_GET(inst, control_gpios),       \
        .active_time_ms = DT_INST_PROP(inst, active_time_ms),        \
    };                                                               \
    DEVICE_DT_INST_DEFINE(inst,                                      \
                          gpio_relay_init,                           \
                          NULL,                                      \
                          &relay_data_##inst,                        \
                          &relay_config_##inst,                      \
                          POST_KERNEL,                               \
                          CONFIG_GPIO_INIT_PRIORITY,                 \
                          &relay_api);

DT_INST_FOREACH_STATUS_OKAY(GPIO_RELAY_DEFINE)
```

#### Devicetree Usage

```dts
/ {
    relay0: relay_0 {
        compatible = "vendor,gpio-relay";
        control-gpios = <&gpio0 10 GPIO_ACTIVE_HIGH>;
        active-time-ms = <100>;
    };
};
```

---

### I2C Device Driver

Complete pattern for I2C device drivers.

#### Devicetree Binding

```yaml
# dts/bindings/vendor,i2c-sensor.yaml
description: Vendor I2C Sensor

compatible: "vendor,i2c-sensor"

include: [i2c-device.yaml]

properties:
  int-gpios:
    type: phandle-array
    description: Optional interrupt GPIO

  measurement-mode:
    type: string
    enum:
      - "continuous"
      - "single-shot"
    default: "continuous"
```

#### Driver Implementation

```c
#define DT_DRV_COMPAT vendor_i2c_sensor

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(i2c_sensor, CONFIG_SENSOR_LOG_LEVEL);

/* Register definitions */
#define REG_CHIP_ID     0x00
#define REG_CONFIG      0x01
#define REG_DATA        0x10
#define EXPECTED_CHIP_ID 0x5A

struct i2c_sensor_config {
    struct i2c_dt_spec i2c;
    struct gpio_dt_spec int_gpio;
    bool continuous_mode;
};

struct i2c_sensor_data {
    struct k_sem lock;
    int16_t raw_value;

#ifdef CONFIG_I2C_SENSOR_TRIGGER
    struct gpio_callback gpio_cb;
    const struct device *dev;
    sensor_trigger_handler_t handler;
    const struct sensor_trigger *trigger;
#endif
};

/* Register access helpers */
static int read_reg(const struct i2c_dt_spec *i2c, uint8_t reg, uint8_t *val)
{
    return i2c_write_read_dt(i2c, &reg, 1, val, 1);
}

static int write_reg(const struct i2c_dt_spec *i2c, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_write_dt(i2c, buf, sizeof(buf));
}

static int read_data(const struct i2c_dt_spec *i2c, int16_t *data)
{
    uint8_t buf[2];
    uint8_t reg = REG_DATA;
    int ret;

    ret = i2c_write_read_dt(i2c, &reg, 1, buf, sizeof(buf));
    if (ret == 0) {
        *data = (buf[0] << 8) | buf[1];
    }
    return ret;
}

/* Sensor API implementation */
static int sensor_sample_fetch(const struct device *dev,
                               enum sensor_channel chan)
{
    const struct i2c_sensor_config *cfg = dev->config;
    struct i2c_sensor_data *data = dev->data;
    int ret;

    if (chan != SENSOR_CHAN_ALL && chan != SENSOR_CHAN_AMBIENT_TEMP) {
        return -ENOTSUP;
    }

    k_sem_take(&data->lock, K_FOREVER);
    ret = read_data(&cfg->i2c, &data->raw_value);
    k_sem_give(&data->lock);

    return ret;
}

static int sensor_channel_get(const struct device *dev,
                              enum sensor_channel chan,
                              struct sensor_value *val)
{
    struct i2c_sensor_data *data = dev->data;

    if (chan != SENSOR_CHAN_AMBIENT_TEMP) {
        return -ENOTSUP;
    }

    /* Convert raw to temperature (example: 0.01 C per LSB) */
    int32_t temp_mc = data->raw_value * 10;  /* millicelsius */
    val->val1 = temp_mc / 1000;
    val->val2 = (temp_mc % 1000) * 1000;

    return 0;
}

static const struct sensor_driver_api sensor_api = {
    .sample_fetch = sensor_sample_fetch,
    .channel_get = sensor_channel_get,
};

/* Initialization */
static int i2c_sensor_init(const struct device *dev)
{
    const struct i2c_sensor_config *cfg = dev->config;
    struct i2c_sensor_data *data = dev->data;
    uint8_t chip_id;
    int ret;

    /* Check I2C bus ready */
    if (!i2c_is_ready_dt(&cfg->i2c)) {
        LOG_ERR("I2C bus not ready");
        return -ENODEV;
    }

    k_sem_init(&data->lock, 1, 1);

    /* Verify chip ID */
    ret = read_reg(&cfg->i2c, REG_CHIP_ID, &chip_id);
    if (ret < 0) {
        LOG_ERR("Failed to read chip ID");
        return ret;
    }

    if (chip_id != EXPECTED_CHIP_ID) {
        LOG_ERR("Invalid chip ID: 0x%02x (expected 0x%02x)",
                chip_id, EXPECTED_CHIP_ID);
        return -ENODEV;
    }

    /* Configure device */
    uint8_t config = cfg->continuous_mode ? 0x01 : 0x00;
    ret = write_reg(&cfg->i2c, REG_CONFIG, config);
    if (ret < 0) {
        LOG_ERR("Failed to configure device");
        return ret;
    }

    LOG_INF("Device %s initialized", dev->name);
    return 0;
}

/* Instantiation */
#define I2C_SENSOR_DEFINE(inst)                                              \
    static struct i2c_sensor_data i2c_sensor_data_##inst;                    \
                                                                             \
    static const struct i2c_sensor_config i2c_sensor_config_##inst = {       \
        .i2c = I2C_DT_SPEC_INST_GET(inst),                                   \
        .int_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, int_gpios, {0}),          \
        .continuous_mode = DT_INST_ENUM_IDX(inst, measurement_mode) == 0,    \
    };                                                                       \
                                                                             \
    DEVICE_DT_INST_DEFINE(inst,                                              \
                          i2c_sensor_init,                                   \
                          NULL,                                              \
                          &i2c_sensor_data_##inst,                           \
                          &i2c_sensor_config_##inst,                         \
                          POST_KERNEL,                                       \
                          CONFIG_SENSOR_INIT_PRIORITY,                       \
                          &sensor_api);

DT_INST_FOREACH_STATUS_OKAY(I2C_SENSOR_DEFINE)
```

#### Key I2C Macros

| Macro | Purpose |
|-------|---------|
| `I2C_DT_SPEC_INST_GET(inst)` | Get i2c_dt_spec from instance |
| `i2c_is_ready_dt(&spec)` | Check bus ready |
| `i2c_read_dt(&spec, buf, len)` | Read from device |
| `i2c_write_dt(&spec, buf, len)` | Write to device |
| `i2c_write_read_dt(&spec, wr, wr_len, rd, rd_len)` | Write then read |

---

### SPI Device Driver

Complete pattern for SPI device drivers.

#### Devicetree Binding

```yaml
# dts/bindings/vendor,spi-flash.yaml
description: Vendor SPI Flash

compatible: "vendor,spi-flash"

include: [spi-device.yaml]

properties:
  size:
    type: int
    required: true
    description: Flash size in bytes

  page-size:
    type: int
    default: 256
    description: Page size in bytes

  wp-gpios:
    type: phandle-array
    description: Write protect GPIO
```

#### Driver Implementation

```c
#define DT_DRV_COMPAT vendor_spi_flash

#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(spi_flash, CONFIG_FLASH_LOG_LEVEL);

/* Flash commands */
#define CMD_READ_ID     0x9F
#define CMD_READ        0x03
#define CMD_WRITE_EN    0x06
#define CMD_PAGE_PROG   0x02
#define CMD_STATUS      0x05

struct spi_flash_config {
    struct spi_dt_spec spi;
    struct gpio_dt_spec wp;
    uint32_t size;
    uint16_t page_size;
};

struct spi_flash_data {
    struct k_sem lock;
};

/* SPI transfer helpers */
static int flash_read_id(const struct spi_dt_spec *spi, uint8_t *id, size_t len)
{
    uint8_t cmd = CMD_READ_ID;

    struct spi_buf tx_buf = {.buf = &cmd, .len = 1};
    struct spi_buf rx_bufs[] = {
        {.buf = NULL, .len = 1},  /* Skip command echo */
        {.buf = id, .len = len},
    };
    struct spi_buf_set tx = {.buffers = &tx_buf, .count = 1};
    struct spi_buf_set rx = {.buffers = rx_bufs, .count = 2};

    return spi_transceive_dt(spi, &tx, &rx);
}

static int flash_read_status(const struct spi_dt_spec *spi, uint8_t *status)
{
    uint8_t cmd = CMD_STATUS;
    uint8_t rx_buf[2];

    struct spi_buf tx_buf = {.buf = &cmd, .len = 1};
    struct spi_buf rx_buf_desc = {.buf = rx_buf, .len = 2};
    struct spi_buf_set tx = {.buffers = &tx_buf, .count = 1};
    struct spi_buf_set rx = {.buffers = &rx_buf_desc, .count = 1};

    int ret = spi_transceive_dt(spi, &tx, &rx);
    *status = rx_buf[1];
    return ret;
}

static int flash_write_enable(const struct spi_dt_spec *spi)
{
    uint8_t cmd = CMD_WRITE_EN;
    struct spi_buf buf = {.buf = &cmd, .len = 1};
    struct spi_buf_set tx = {.buffers = &buf, .count = 1};

    return spi_write_dt(spi, &tx);
}

static int flash_wait_ready(const struct spi_dt_spec *spi, k_timeout_t timeout)
{
    uint8_t status;
    int64_t end = k_uptime_get() + k_ticks_to_ms_floor64(timeout.ticks);

    do {
        flash_read_status(spi, &status);
        if (!(status & 0x01)) {  /* WIP bit */
            return 0;
        }
        k_sleep(K_MSEC(1));
    } while (k_uptime_get() < end);

    return -ETIMEDOUT;
}

/* Flash API */
static int flash_read(const struct device *dev, off_t offset, void *data,
                      size_t len)
{
    const struct spi_flash_config *cfg = dev->config;
    struct spi_flash_data *drv_data = dev->data;
    uint8_t cmd[4] = {
        CMD_READ,
        (offset >> 16) & 0xFF,
        (offset >> 8) & 0xFF,
        offset & 0xFF,
    };

    struct spi_buf tx_buf = {.buf = cmd, .len = sizeof(cmd)};
    struct spi_buf rx_bufs[] = {
        {.buf = NULL, .len = sizeof(cmd)},
        {.buf = data, .len = len},
    };
    struct spi_buf_set tx = {.buffers = &tx_buf, .count = 1};
    struct spi_buf_set rx = {.buffers = rx_bufs, .count = 2};

    k_sem_take(&drv_data->lock, K_FOREVER);
    int ret = spi_transceive_dt(&cfg->spi, &tx, &rx);
    k_sem_give(&drv_data->lock);

    return ret;
}

/* Initialization */
static int spi_flash_init(const struct device *dev)
{
    const struct spi_flash_config *cfg = dev->config;
    struct spi_flash_data *data = dev->data;
    uint8_t id[3];
    int ret;

    if (!spi_is_ready_dt(&cfg->spi)) {
        LOG_ERR("SPI bus not ready");
        return -ENODEV;
    }

    /* Configure WP GPIO if present */
    if (cfg->wp.port != NULL) {
        if (!gpio_is_ready_dt(&cfg->wp)) {
            return -ENODEV;
        }
        gpio_pin_configure_dt(&cfg->wp, GPIO_OUTPUT_ACTIVE);
    }

    k_sem_init(&data->lock, 1, 1);

    /* Read and verify JEDEC ID */
    ret = flash_read_id(&cfg->spi, id, sizeof(id));
    if (ret < 0) {
        LOG_ERR("Failed to read flash ID");
        return ret;
    }

    LOG_INF("Flash ID: %02x %02x %02x, size: %u bytes",
            id[0], id[1], id[2], cfg->size);

    return 0;
}

/* Instantiation */
#define SPI_FLASH_DEFINE(inst)                                          \
    static struct spi_flash_data spi_flash_data_##inst;                 \
                                                                        \
    static const struct spi_flash_config spi_flash_config_##inst = {    \
        .spi = SPI_DT_SPEC_INST_GET(inst,                               \
                   SPI_WORD_SET(8) | SPI_TRANSFER_MSB, 0),              \
        .wp = GPIO_DT_SPEC_INST_GET_OR(inst, wp_gpios, {0}),            \
        .size = DT_INST_PROP(inst, size),                               \
        .page_size = DT_INST_PROP(inst, page_size),                     \
    };                                                                  \
                                                                        \
    DEVICE_DT_INST_DEFINE(inst,                                         \
                          spi_flash_init,                               \
                          NULL,                                         \
                          &spi_flash_data_##inst,                       \
                          &spi_flash_config_##inst,                     \
                          POST_KERNEL,                                  \
                          CONFIG_FLASH_INIT_PRIORITY,                   \
                          NULL);

DT_INST_FOREACH_STATUS_OKAY(SPI_FLASH_DEFINE)
```

#### Key SPI Macros

| Macro | Purpose |
|-------|---------|
| `SPI_DT_SPEC_INST_GET(inst, op, delay)` | Get spi_dt_spec |
| `spi_is_ready_dt(&spec)` | Check bus ready |
| `spi_transceive_dt(&spec, tx, rx)` | Full duplex |
| `spi_write_dt(&spec, tx)` | Write only |
| `spi_read_dt(&spec, rx)` | Read only |

---

### UART Device Driver

Pattern for UART-based device drivers (e.g., GPS, modem).

#### Devicetree Binding

```yaml
# dts/bindings/vendor,uart-gps.yaml
description: UART GPS Module

compatible: "vendor,uart-gps"

include: [uart-device.yaml]

properties:
  reset-gpios:
    type: phandle-array
    description: Optional reset GPIO

  pps-gpios:
    type: phandle-array
    description: PPS (pulse per second) GPIO
```

#### Driver Implementation

```c
#define DT_DRV_COMPAT vendor_uart_gps

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(uart_gps, CONFIG_GPS_LOG_LEVEL);

#define RX_BUF_SIZE 256

struct uart_gps_config {
    const struct device *uart;
    struct gpio_dt_spec reset;
    struct gpio_dt_spec pps;
};

struct uart_gps_data {
    uint8_t rx_buf[RX_BUF_SIZE];
    size_t rx_pos;
    struct k_sem rx_sem;
    gps_callback_t callback;
    void *user_data;
};

/* UART ISR callback */
static void uart_gps_isr(const struct device *uart, void *user_data)
{
    const struct device *dev = user_data;
    struct uart_gps_data *data = dev->data;

    if (!uart_irq_update(uart)) {
        return;
    }

    if (uart_irq_rx_ready(uart)) {
        uint8_t c;
        while (uart_fifo_read(uart, &c, 1) > 0) {
            if (data->rx_pos < RX_BUF_SIZE - 1) {
                data->rx_buf[data->rx_pos++] = c;
            }

            /* Check for NMEA sentence end */
            if (c == '\n') {
                data->rx_buf[data->rx_pos] = '\0';

                if (data->callback) {
                    data->callback(dev, (char *)data->rx_buf,
                                   data->rx_pos, data->user_data);
                }

                data->rx_pos = 0;
            }
        }
    }
}

/* GPS API */
static int gps_start(const struct device *dev)
{
    const struct uart_gps_config *cfg = dev->config;

    uart_irq_rx_enable(cfg->uart);
    return 0;
}

static int gps_stop(const struct device *dev)
{
    const struct uart_gps_config *cfg = dev->config;

    uart_irq_rx_disable(cfg->uart);
    return 0;
}

static int gps_send_command(const struct device *dev, const char *cmd)
{
    const struct uart_gps_config *cfg = dev->config;

    for (size_t i = 0; cmd[i] != '\0'; i++) {
        uart_poll_out(cfg->uart, cmd[i]);
    }
    uart_poll_out(cfg->uart, '\r');
    uart_poll_out(cfg->uart, '\n');

    return 0;
}

static int gps_set_callback(const struct device *dev, gps_callback_t cb,
                            void *user_data)
{
    struct uart_gps_data *data = dev->data;

    data->callback = cb;
    data->user_data = user_data;
    return 0;
}

static int gps_reset(const struct device *dev)
{
    const struct uart_gps_config *cfg = dev->config;

    if (cfg->reset.port != NULL) {
        gpio_pin_set_dt(&cfg->reset, 1);
        k_sleep(K_MSEC(100));
        gpio_pin_set_dt(&cfg->reset, 0);
        k_sleep(K_MSEC(500));
    }
    return 0;
}

struct gps_driver_api {
    int (*start)(const struct device *dev);
    int (*stop)(const struct device *dev);
    int (*send_command)(const struct device *dev, const char *cmd);
    int (*set_callback)(const struct device *dev, gps_callback_t cb,
                        void *user_data);
    int (*reset)(const struct device *dev);
};

static const struct gps_driver_api gps_api = {
    .start = gps_start,
    .stop = gps_stop,
    .send_command = gps_send_command,
    .set_callback = gps_set_callback,
    .reset = gps_reset,
};

/* Initialization */
static int uart_gps_init(const struct device *dev)
{
    const struct uart_gps_config *cfg = dev->config;
    struct uart_gps_data *data = dev->data;

    if (!device_is_ready(cfg->uart)) {
        LOG_ERR("UART device not ready");
        return -ENODEV;
    }

    /* Configure reset GPIO */
    if (cfg->reset.port != NULL) {
        if (!gpio_is_ready_dt(&cfg->reset)) {
            return -ENODEV;
        }
        gpio_pin_configure_dt(&cfg->reset, GPIO_OUTPUT_INACTIVE);
    }

    /* Configure PPS GPIO */
    if (cfg->pps.port != NULL) {
        if (!gpio_is_ready_dt(&cfg->pps)) {
            return -ENODEV;
        }
        gpio_pin_configure_dt(&cfg->pps, GPIO_INPUT);
    }

    k_sem_init(&data->rx_sem, 0, 1);
    data->rx_pos = 0;

    /* Set up UART interrupt */
    uart_irq_callback_user_data_set(cfg->uart, uart_gps_isr, (void *)dev);

    LOG_INF("GPS %s initialized", dev->name);
    return 0;
}

/* Instantiation */
#define UART_GPS_DEFINE(inst)                                           \
    static struct uart_gps_data uart_gps_data_##inst;                   \
                                                                        \
    static const struct uart_gps_config uart_gps_config_##inst = {      \
        .uart = DEVICE_DT_GET(DT_INST_BUS(inst)),                       \
        .reset = GPIO_DT_SPEC_INST_GET_OR(inst, reset_gpios, {0}),      \
        .pps = GPIO_DT_SPEC_INST_GET_OR(inst, pps_gpios, {0}),          \
    };                                                                  \
                                                                        \
    DEVICE_DT_INST_DEFINE(inst,                                         \
                          uart_gps_init,                                \
                          NULL,                                         \
                          &uart_gps_data_##inst,                        \
                          &uart_gps_config_##inst,                      \
                          POST_KERNEL,                                  \
                          CONFIG_GPS_INIT_PRIORITY,                     \
                          &gps_api);

DT_INST_FOREACH_STATUS_OKAY(UART_GPS_DEFINE)
```

#### Devicetree Usage

```dts
&uart1 {
    status = "okay";
    current-speed = <9600>;

    gps: gps {
        compatible = "vendor,uart-gps";
        reset-gpios = <&gpio0 5 GPIO_ACTIVE_HIGH>;
        pps-gpios = <&gpio0 6 GPIO_ACTIVE_HIGH>;
    };
};
```

---

### Mixed Bus Driver

Drivers that use multiple buses (e.g., I2C + interrupt GPIO).

#### Pattern

```c
#define DT_DRV_COMPAT vendor_touchscreen

struct touchscreen_config {
    struct i2c_dt_spec i2c;
    struct gpio_dt_spec int_gpio;
    struct gpio_dt_spec reset_gpio;
    uint16_t x_max;
    uint16_t y_max;
};

struct touchscreen_data {
    struct k_work work;
    struct gpio_callback gpio_cb;
    const struct device *dev;
    touch_callback_t callback;
};

static void touchscreen_gpio_callback(const struct device *port,
                                      struct gpio_callback *cb,
                                      gpio_port_pins_t pins)
{
    struct touchscreen_data *data =
        CONTAINER_OF(cb, struct touchscreen_data, gpio_cb);

    /* Defer I2C read to work queue (can't do I2C in ISR) */
    k_work_submit(&data->work);
}

static void touchscreen_work_handler(struct k_work *work)
{
    struct touchscreen_data *data =
        CONTAINER_OF(work, struct touchscreen_data, work);
    const struct device *dev = data->dev;
    const struct touchscreen_config *cfg = dev->config;

    /* Read touch data over I2C */
    uint8_t buf[6];
    uint8_t reg = 0x00;
    i2c_write_read_dt(&cfg->i2c, &reg, 1, buf, sizeof(buf));

    /* Process and invoke callback */
    if (data->callback) {
        uint16_t x = (buf[1] << 8) | buf[2];
        uint16_t y = (buf[3] << 8) | buf[4];
        data->callback(dev, x, y, buf[0]);
    }
}

static int touchscreen_init(const struct device *dev)
{
    const struct touchscreen_config *cfg = dev->config;
    struct touchscreen_data *data = dev->data;

    /* Check all buses ready */
    if (!i2c_is_ready_dt(&cfg->i2c)) {
        return -ENODEV;
    }

    if (!gpio_is_ready_dt(&cfg->int_gpio)) {
        return -ENODEV;
    }

    /* Store device reference for work handler */
    data->dev = dev;

    /* Initialize work */
    k_work_init(&data->work, touchscreen_work_handler);

    /* Configure interrupt GPIO */
    gpio_pin_configure_dt(&cfg->int_gpio, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&cfg->int_gpio, GPIO_INT_EDGE_TO_ACTIVE);
    gpio_init_callback(&data->gpio_cb, touchscreen_gpio_callback,
                       BIT(cfg->int_gpio.pin));
    gpio_add_callback(cfg->int_gpio.port, &data->gpio_cb);

    /* Reset device if GPIO present */
    if (cfg->reset_gpio.port != NULL) {
        gpio_pin_configure_dt(&cfg->reset_gpio, GPIO_OUTPUT_ACTIVE);
        k_sleep(K_MSEC(10));
        gpio_pin_set_dt(&cfg->reset_gpio, 0);
        k_sleep(K_MSEC(50));
    }

    return 0;
}
```

#### Key Pattern: ISR + Work Queue

1. GPIO interrupt fires in ISR context
2. ISR submits work to system work queue
3. Work handler runs in thread context (can do I2C/SPI)
4. Invoke user callback with data
