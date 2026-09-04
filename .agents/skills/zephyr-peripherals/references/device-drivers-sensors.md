# Device Drivers: Sensors

## Sensor Drivers


### Table of Contents

1. [Sensor API Overview](#sensor-api-overview)
2. [Basic Sensor Driver](#basic-sensor-driver)
3. [Multi-Channel Sensors](#multi-channel-sensors)
4. [Sensor Triggers](#sensor-triggers)
5. [Sensor Attributes](#sensor-attributes)
6. [Sensor Emulators](#sensor-emulators)

---

### Sensor API Overview

The sensor subsystem provides a unified API for all sensor types.

#### Key Concepts

| Concept | Description |
|---------|-------------|
| Channel | Type of measurement (temp, accel, etc.) |
| Trigger | Event that notifies data ready |
| Attribute | Configurable sensor parameter |
| sensor_value | Fixed-point value (val1=int, val2=micro) |

#### sensor_value Structure

```c
struct sensor_value {
    int32_t val1;  /* Integer part */
    int32_t val2;  /* Fractional part (millionths) */
};

/* Examples:
 * 25.5 °C  → val1 = 25, val2 = 500000
 * -10.25   → val1 = -10, val2 = -250000
 * 1013.25 hPa → val1 = 1013, val2 = 250000
 */
```

#### Common Sensor Channels

| Channel | Unit | Description |
|---------|------|-------------|
| `SENSOR_CHAN_AMBIENT_TEMP` | °C | Ambient temperature |
| `SENSOR_CHAN_PRESS` | kPa | Atmospheric pressure |
| `SENSOR_CHAN_HUMIDITY` | % | Relative humidity |
| `SENSOR_CHAN_ACCEL_X/Y/Z` | m/s² | Acceleration |
| `SENSOR_CHAN_ACCEL_XYZ` | m/s² | All 3 axes |
| `SENSOR_CHAN_GYRO_X/Y/Z` | rad/s | Angular velocity |
| `SENSOR_CHAN_MAGN_X/Y/Z` | Gauss | Magnetic field |
| `SENSOR_CHAN_LIGHT` | lux | Ambient light |
| `SENSOR_CHAN_PROX` | - | Proximity |
| `SENSOR_CHAN_VOLTAGE` | V | Voltage |
| `SENSOR_CHAN_CURRENT` | A | Current |
| `SENSOR_CHAN_ALTITUDE` | m | Altitude |
| `SENSOR_CHAN_PM_1_0` | µg/m³ | PM1.0 particles |

---

### Basic Sensor Driver

#### Minimal Structure

```c
#define DT_DRV_COMPAT vendor_temp_sensor

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/i2c.h>

struct temp_sensor_config {
    struct i2c_dt_spec i2c;
};

struct temp_sensor_data {
    int16_t raw_temp;
};

/* Fetch data from hardware */
static int temp_sample_fetch(const struct device *dev,
                             enum sensor_channel chan)
{
    const struct temp_sensor_config *cfg = dev->config;
    struct temp_sensor_data *data = dev->data;
    uint8_t buf[2];
    int ret;

    if (chan != SENSOR_CHAN_ALL && chan != SENSOR_CHAN_AMBIENT_TEMP) {
        return -ENOTSUP;
    }

    /* Read from hardware */
    ret = i2c_read_dt(&cfg->i2c, buf, sizeof(buf));
    if (ret < 0) {
        return ret;
    }

    data->raw_temp = (buf[0] << 8) | buf[1];
    return 0;
}

/* Convert raw to sensor_value */
static int temp_channel_get(const struct device *dev,
                            enum sensor_channel chan,
                            struct sensor_value *val)
{
    struct temp_sensor_data *data = dev->data;

    if (chan != SENSOR_CHAN_AMBIENT_TEMP) {
        return -ENOTSUP;
    }

    /* Example: 0.0625 °C per LSB (12-bit resolution) */
    int32_t temp_mc = (data->raw_temp * 625) / 10;  /* millicelsius */

    val->val1 = temp_mc / 1000;
    val->val2 = (temp_mc % 1000) * 1000;

    return 0;
}

/* Driver API structure */
static DEVICE_API(sensor, temp_sensor_api) = {
    .sample_fetch = temp_sample_fetch,
    .channel_get = temp_channel_get,
};

/* Initialization */
static int temp_sensor_init(const struct device *dev)
{
    const struct temp_sensor_config *cfg = dev->config;

    if (!i2c_is_ready_dt(&cfg->i2c)) {
        return -ENODEV;
    }

    return 0;
}

/* Instantiation */
#define TEMP_SENSOR_DEFINE(inst)                                     \
    static struct temp_sensor_data temp_data_##inst;                 \
    static const struct temp_sensor_config temp_cfg_##inst = {       \
        .i2c = I2C_DT_SPEC_INST_GET(inst),                           \
    };                                                               \
    DEVICE_DT_INST_DEFINE(inst,                                      \
                          temp_sensor_init,                          \
                          NULL,                                      \
                          &temp_data_##inst,                         \
                          &temp_cfg_##inst,                          \
                          POST_KERNEL,                               \
                          CONFIG_SENSOR_INIT_PRIORITY,               \
                          &temp_sensor_api);

DT_INST_FOREACH_STATUS_OKAY(TEMP_SENSOR_DEFINE)
```

---

### Multi-Channel Sensors

For sensors providing multiple measurement types (e.g., BME280: temp + pressure + humidity).

```c
#define DT_DRV_COMPAT bosch_bme280

struct bme280_data {
    int32_t temp;       /* 0.01 °C units */
    uint32_t press;     /* Pa units */
    uint32_t humidity;  /* 0.01 % units */
};

static int bme280_sample_fetch(const struct device *dev,
                               enum sensor_channel chan)
{
    struct bme280_data *data = dev->data;
    const struct bme280_config *cfg = dev->config;
    uint8_t buf[8];

    /* Fetch all data in one burst read */
    /* (most environmental sensors benefit from burst reads) */
    i2c_burst_read_dt(&cfg->i2c, 0xF7, buf, sizeof(buf));

    /* Parse raw data (device-specific) */
    int32_t raw_temp = /* ... */;
    int32_t raw_press = /* ... */;
    int32_t raw_hum = /* ... */;

    /* Apply compensation (device-specific) */
    data->temp = compensate_temp(raw_temp);
    data->press = compensate_press(raw_press);
    data->humidity = compensate_hum(raw_hum);

    return 0;
}

static int bme280_channel_get(const struct device *dev,
                              enum sensor_channel chan,
                              struct sensor_value *val)
{
    struct bme280_data *data = dev->data;

    switch (chan) {
    case SENSOR_CHAN_AMBIENT_TEMP:
        /* data->temp is in 0.01 °C */
        val->val1 = data->temp / 100;
        val->val2 = (data->temp % 100) * 10000;
        return 0;

    case SENSOR_CHAN_PRESS:
        /* data->press is in Pa, sensor API expects kPa */
        val->val1 = data->press / 1000;
        val->val2 = (data->press % 1000) * 1000;
        return 0;

    case SENSOR_CHAN_HUMIDITY:
        /* data->humidity is in 0.01 % */
        val->val1 = data->humidity / 100;
        val->val2 = (data->humidity % 100) * 10000;
        return 0;

    default:
        return -ENOTSUP;
    }
}
```

#### XYZ Channels (IMU)

```c
struct imu_data {
    int16_t accel[3];  /* raw X, Y, Z */
    int16_t gyro[3];
};

static int imu_channel_get(const struct device *dev,
                           enum sensor_channel chan,
                           struct sensor_value *val)
{
    struct imu_data *data = dev->data;

    switch (chan) {
    case SENSOR_CHAN_ACCEL_X:
        return convert_accel(&data->accel[0], val);
    case SENSOR_CHAN_ACCEL_Y:
        return convert_accel(&data->accel[1], val);
    case SENSOR_CHAN_ACCEL_Z:
        return convert_accel(&data->accel[2], val);
    case SENSOR_CHAN_ACCEL_XYZ:
        /* val is an array of 3 sensor_values */
        convert_accel(&data->accel[0], &val[0]);
        convert_accel(&data->accel[1], &val[1]);
        convert_accel(&data->accel[2], &val[2]);
        return 0;
    case SENSOR_CHAN_GYRO_X:
    case SENSOR_CHAN_GYRO_Y:
    case SENSOR_CHAN_GYRO_Z:
    case SENSOR_CHAN_GYRO_XYZ:
        /* Similar pattern */
        break;
    default:
        return -ENOTSUP;
    }
}
```

---

### Sensor Triggers

Enable hardware interrupts for data-ready or threshold events.

#### Kconfig

```kconfig
config MY_SENSOR_TRIGGER
    bool "Enable trigger support"
    depends on MY_SENSOR
    select GPIO
    help
      Enable trigger support for the sensor.

config MY_SENSOR_TRIGGER_OWN_THREAD
    bool "Use own thread for triggers"
    depends on MY_SENSOR_TRIGGER
    help
      Use a dedicated thread for trigger processing.

config MY_SENSOR_TRIGGER_GLOBAL_THREAD
    bool "Use global thread for triggers"
    depends on MY_SENSOR_TRIGGER
    help
      Use the system work queue for trigger processing.
```

#### Trigger Implementation

```c
struct my_sensor_data {
    /* ... existing fields ... */

#ifdef CONFIG_MY_SENSOR_TRIGGER
    const struct device *dev;
    struct gpio_callback gpio_cb;
    sensor_trigger_handler_t handler;
    const struct sensor_trigger *trigger;

#ifdef CONFIG_MY_SENSOR_TRIGGER_OWN_THREAD
    K_THREAD_STACK_MEMBER(thread_stack, CONFIG_MY_SENSOR_THREAD_STACK_SIZE);
    struct k_thread thread;
    struct k_sem trig_sem;
#elif defined(CONFIG_MY_SENSOR_TRIGGER_GLOBAL_THREAD)
    struct k_work work;
#endif
#endif
};

#ifdef CONFIG_MY_SENSOR_TRIGGER

static void my_sensor_gpio_callback(const struct device *port,
                                    struct gpio_callback *cb,
                                    gpio_port_pins_t pins)
{
    struct my_sensor_data *data =
        CONTAINER_OF(cb, struct my_sensor_data, gpio_cb);

#ifdef CONFIG_MY_SENSOR_TRIGGER_OWN_THREAD
    k_sem_give(&data->trig_sem);
#elif defined(CONFIG_MY_SENSOR_TRIGGER_GLOBAL_THREAD)
    k_work_submit(&data->work);
#endif
}

static void my_sensor_handle_trigger(const struct device *dev)
{
    struct my_sensor_data *data = dev->data;

    if (data->handler) {
        data->handler(dev, data->trigger);
    }
}

#ifdef CONFIG_MY_SENSOR_TRIGGER_OWN_THREAD
static void my_sensor_thread_main(void *p1, void *p2, void *p3)
{
    const struct device *dev = p1;
    struct my_sensor_data *data = dev->data;

    while (1) {
        k_sem_take(&data->trig_sem, K_FOREVER);
        my_sensor_handle_trigger(dev);
    }
}
#elif defined(CONFIG_MY_SENSOR_TRIGGER_GLOBAL_THREAD)
static void my_sensor_work_handler(struct k_work *work)
{
    struct my_sensor_data *data =
        CONTAINER_OF(work, struct my_sensor_data, work);
    my_sensor_handle_trigger(data->dev);
}
#endif

static int my_sensor_trigger_set(const struct device *dev,
                                 const struct sensor_trigger *trig,
                                 sensor_trigger_handler_t handler)
{
    struct my_sensor_data *data = dev->data;
    const struct my_sensor_config *cfg = dev->config;

    if (trig->type != SENSOR_TRIG_DATA_READY) {
        return -ENOTSUP;
    }

    data->handler = handler;
    data->trigger = trig;

    if (handler) {
        /* Enable interrupt on device */
        /* write_reg(cfg, INT_ENABLE_REG, INT_DATA_READY); */
        gpio_pin_interrupt_configure_dt(&cfg->int_gpio,
                                        GPIO_INT_EDGE_TO_ACTIVE);
    } else {
        gpio_pin_interrupt_configure_dt(&cfg->int_gpio, GPIO_INT_DISABLE);
    }

    return 0;
}

static int my_sensor_trigger_init(const struct device *dev)
{
    struct my_sensor_data *data = dev->data;
    const struct my_sensor_config *cfg = dev->config;

    data->dev = dev;

    if (!gpio_is_ready_dt(&cfg->int_gpio)) {
        return -ENODEV;
    }

    gpio_pin_configure_dt(&cfg->int_gpio, GPIO_INPUT);

    gpio_init_callback(&data->gpio_cb, my_sensor_gpio_callback,
                       BIT(cfg->int_gpio.pin));
    gpio_add_callback(cfg->int_gpio.port, &data->gpio_cb);

#ifdef CONFIG_MY_SENSOR_TRIGGER_OWN_THREAD
    k_sem_init(&data->trig_sem, 0, K_SEM_MAX_LIMIT);
    k_thread_create(&data->thread, data->thread_stack,
                    CONFIG_MY_SENSOR_THREAD_STACK_SIZE,
                    my_sensor_thread_main, (void *)dev, NULL, NULL,
                    K_PRIO_COOP(CONFIG_MY_SENSOR_THREAD_PRIORITY),
                    0, K_NO_WAIT);
#elif defined(CONFIG_MY_SENSOR_TRIGGER_GLOBAL_THREAD)
    k_work_init(&data->work, my_sensor_work_handler);
#endif

    return 0;
}

#endif /* CONFIG_MY_SENSOR_TRIGGER */

/* Add to sensor_driver_api */
static DEVICE_API(sensor, my_sensor_api) = {
    .sample_fetch = my_sensor_sample_fetch,
    .channel_get = my_sensor_channel_get,
#ifdef CONFIG_MY_SENSOR_TRIGGER
    .trigger_set = my_sensor_trigger_set,
#endif
};
```

#### Common Trigger Types

| Trigger | Description |
|---------|-------------|
| `SENSOR_TRIG_DATA_READY` | New data available |
| `SENSOR_TRIG_THRESHOLD` | Threshold crossed |
| `SENSOR_TRIG_DELTA` | Value changed by delta |
| `SENSOR_TRIG_MOTION` | Motion detected |
| `SENSOR_TRIG_STATIONARY` | Device stationary |
| `SENSOR_TRIG_FREEFALL` | Freefall detected |
| `SENSOR_TRIG_TAP` | Single tap |
| `SENSOR_TRIG_DOUBLE_TAP` | Double tap |

---

### Sensor Attributes

Allow runtime configuration of sensor parameters.

```c
static int my_sensor_attr_set(const struct device *dev,
                              enum sensor_channel chan,
                              enum sensor_attribute attr,
                              const struct sensor_value *val)
{
    const struct my_sensor_config *cfg = dev->config;

    switch (attr) {
    case SENSOR_ATTR_SAMPLING_FREQUENCY:
        /* val->val1 = Hz */
        return set_odr(cfg, val->val1);

    case SENSOR_ATTR_FULL_SCALE:
        /* Set measurement range */
        return set_range(cfg, val->val1);

    case SENSOR_ATTR_OVERSAMPLING:
        return set_oversampling(cfg, val->val1);

    default:
        return -ENOTSUP;
    }
}

static int my_sensor_attr_get(const struct device *dev,
                              enum sensor_channel chan,
                              enum sensor_attribute attr,
                              struct sensor_value *val)
{
    struct my_sensor_data *data = dev->data;

    switch (attr) {
    case SENSOR_ATTR_SAMPLING_FREQUENCY:
        val->val1 = data->current_odr;
        val->val2 = 0;
        return 0;

    default:
        return -ENOTSUP;
    }
}

/* Add to driver API */
static DEVICE_API(sensor, my_sensor_api) = {
    .sample_fetch = my_sensor_sample_fetch,
    .channel_get = my_sensor_channel_get,
    .attr_set = my_sensor_attr_set,
    .attr_get = my_sensor_attr_get,
};
```

#### Common Attributes

| Attribute | Unit | Description |
|-----------|------|-------------|
| `SENSOR_ATTR_SAMPLING_FREQUENCY` | Hz | Output data rate |
| `SENSOR_ATTR_FULL_SCALE` | varies | Measurement range |
| `SENSOR_ATTR_OVERSAMPLING` | count | Oversampling factor |
| `SENSOR_ATTR_LOWER_THRESH` | varies | Lower threshold |
| `SENSOR_ATTR_UPPER_THRESH` | varies | Upper threshold |
| `SENSOR_ATTR_SLOPE_TH` | varies | Slope threshold |
| `SENSOR_ATTR_OFFSET` | varies | Calibration offset |
| `SENSOR_ATTR_CALIB_TARGET` | varies | Calibration target |

---

### Sensor Emulators

Enable testing without hardware using `CONFIG_EMUL=y`.

#### Emulator Definition

```c
/* In separate file: my_sensor_emul.c */

#define DT_DRV_COMPAT vendor_my_sensor

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c_emul.h>

struct my_sensor_emul_data {
    uint8_t regs[256];
    int16_t temp_value;
};

static int my_sensor_emul_transfer(const struct emul *target,
                                   struct i2c_msg *msgs,
                                   int num_msgs, int addr)
{
    struct my_sensor_emul_data *data = target->data;

    /* Handle I2C messages, return simulated values */
    for (int i = 0; i < num_msgs; i++) {
        if (msgs[i].flags & I2C_MSG_READ) {
            /* Return data from emulated registers */
        } else {
            /* Write to emulated registers */
        }
    }
    return 0;
}

/* Emulator backend API */
static int my_sensor_emul_set_temp(const struct emul *target, int32_t temp_mc)
{
    struct my_sensor_emul_data *data = target->data;
    /* Convert and store */
    data->temp_value = temp_mc / 10;  /* Example conversion */
    return 0;
}

/* Emulator backend structure (for tests to use) */
struct my_sensor_emul_api {
    int (*set_temp)(const struct emul *target, int32_t temp_mc);
};

static const struct my_sensor_emul_api emul_api = {
    .set_temp = my_sensor_emul_set_temp,
};

static const struct i2c_emul_api my_sensor_emul_i2c_api = {
    .transfer = my_sensor_emul_transfer,
};

static int my_sensor_emul_init(const struct emul *target,
                               const struct device *parent)
{
    return 0;
}

#define MY_SENSOR_EMUL_DEFINE(n)                                    \
    static struct my_sensor_emul_data my_sensor_emul_data_##n;      \
    EMUL_DT_INST_DEFINE(n, my_sensor_emul_init,                     \
                        &my_sensor_emul_data_##n,                   \
                        NULL,                                        \
                        &my_sensor_emul_i2c_api,                    \
                        &emul_api);

DT_INST_FOREACH_STATUS_OKAY(MY_SENSOR_EMUL_DEFINE)
```

#### Using Emulator in Tests

```c
#include <zephyr/ztest.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/emul.h>

static const struct device *sensor = DEVICE_DT_GET(DT_NODELABEL(my_sensor));
static const struct emul *emul = EMUL_DT_GET(DT_NODELABEL(my_sensor));

ZTEST(sensor_tests, test_temperature_reading)
{
    struct sensor_value val;
    const struct my_sensor_emul_api *api = emul->backend_api;

    /* Set emulated temperature to 25.5°C */
    api->set_temp(emul, 25500);

    /* Read from driver */
    zassert_ok(sensor_sample_fetch(sensor));
    zassert_ok(sensor_channel_get(sensor, SENSOR_CHAN_AMBIENT_TEMP, &val));

    /* Verify */
    zassert_equal(val.val1, 25);
    zassert_within(val.val2, 500000, 1000);
}

ZTEST_SUITE(sensor_tests, NULL, NULL, NULL, NULL, NULL);
```

#### Emulator Kconfig

```kconfig
config MY_SENSOR_EMUL
    bool "Emulator for my sensor"
    default y
    depends on EMUL
    depends on MY_SENSOR
```

#### Devicetree for Emulation

```dts
/* boards/native_sim.overlay */
&i2c0 {
    my_sensor: my-sensor@48 {
        compatible = "vendor,my-sensor";
        reg = <0x48>;
    };
};
```
