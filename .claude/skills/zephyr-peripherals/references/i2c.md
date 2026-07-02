# I2C

## Overview

Expert guidance for Zephyr's I2C driver subsystem covering synchronous and asynchronous transfer APIs, target/slave mode, devicetree configuration, and common usage patterns.

### Table of Contents

1. [API Selection](#api-selection)
2. [Getting Device Reference](#getting-device-reference)
3. [Common Workflows](#common-workflows)
4. [Configuration](#configuration)
5. [Target Mode](#target-mode)
6. [Error Handling](#error-handling)
7. [Troubleshooting](#troubleshooting)

---

### API Selection

Zephyr provides multiple I2C access methods. Choose based on requirements:

| API | Kconfig | Use Case | Blocking? |
|-----|---------|----------|-----------|
| **Synchronous** | (default) | Simple transfers, blocking until complete | Yes |
| **Async Callback** | `CONFIG_I2C_CALLBACK` | Non-blocking with completion callback | No |
| **RTIO** | `CONFIG_I2C_RTIO` | Real-time I/O subsystem integration | No |
| **Target Mode** | `CONFIG_I2C_TARGET` | Act as I2C peripheral/slave device | N/A |

#### Decision Tree

```
Simple blocking transfer? -> Synchronous (i2c_transfer, i2c_write_read)
Register read/write? -> Helpers (i2c_reg_read_byte, i2c_reg_write_byte)
Need non-blocking? -> Async callback (i2c_transfer_cb)
RTIO integration? -> RTIO API (i2c_rtio_copy)
Respond to controller? -> Target mode (i2c_target_register)
SMBus protocol? -> SMBus API (smbus_byte_data_read, etc.)
```

---

### Getting Device Reference

#### From Devicetree (Preferred - for I2C Devices)

Use `I2C_DT_SPEC_GET` to get a complete I2C specification including bus and address:

```c
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>

/* Define the I2C device from devicetree */
#define MY_I2C_DEVICE DT_NODELABEL(my_sensor)

static const struct i2c_dt_spec i2c_dev = I2C_DT_SPEC_GET(MY_I2C_DEVICE);

/* Runtime check (in main or init) */
if (!i2c_is_ready_dt(&i2c_dev)) {
    printk("I2C device not ready\n");
    return -ENODEV;
}
```

#### Direct Controller Access

```c
/* Get I2C controller directly */
const struct device *i2c = DEVICE_DT_GET(DT_NODELABEL(i2c0));

if (!device_is_ready(i2c)) {
    return -ENODEV;
}

/* Use with explicit address */
#define SENSOR_ADDR 0x48
ret = i2c_read(i2c, buf, sizeof(buf), SENSOR_ADDR);
```

#### From Device Node on Bus

```c
/* Device defined as child of I2C controller in DTS */
#define CODEC_NODE DT_NODELABEL(wm8731)

const struct device *i2c_bus = DEVICE_DT_GET(DT_BUS(CODEC_NODE));
uint16_t addr = DT_REG_ADDR(CODEC_NODE);
```

---

### Common Workflows

#### 1. Simple Write

```c
#include <zephyr/drivers/i2c.h>

uint8_t data[] = {0x01, 0x02, 0x03};

/* Using i2c_dt_spec */
int ret = i2c_write_dt(&i2c_dev, data, sizeof(data));
if (ret < 0) {
    printk("I2C write error: %d\n", ret);
}

/* Using device + address */
ret = i2c_write(i2c, data, sizeof(data), SENSOR_ADDR);
```

#### 2. Simple Read

```c
uint8_t rx_buf[4];

int ret = i2c_read_dt(&i2c_dev, rx_buf, sizeof(rx_buf));
if (ret < 0) {
    printk("I2C read error: %d\n", ret);
}
```

#### 3. Write-Then-Read (Most Common Pattern)

Combined transaction with RESTART condition - typical for register access:

```c
uint8_t reg_addr = 0x00;  /* Register to read */
uint8_t rx_data[2];

int ret = i2c_write_read_dt(&i2c_dev, &reg_addr, 1, rx_data, sizeof(rx_data));
if (ret < 0) {
    printk("I2C write-read error: %d\n", ret);
}
```

#### 4. Register Access Helpers

Convenient functions for single-byte register operations:

```c
uint8_t value;

/* Read register */
ret = i2c_reg_read_byte_dt(&i2c_dev, 0x0F, &value);  /* Read WHO_AM_I */

/* Write register */
ret = i2c_reg_write_byte_dt(&i2c_dev, 0x20, 0x47);  /* Write CTRL_REG1 */

/* Read-modify-write with mask */
ret = i2c_reg_update_byte_dt(&i2c_dev, 0x20, 0x07, 0x05);  /* Update bits 0-2 */
```

#### 5. Burst Read/Write (Multi-byte Register Access)

```c
uint8_t data[6];

/* Read 6 bytes starting from register 0x28 */
ret = i2c_burst_read_dt(&i2c_dev, 0x28, data, sizeof(data));

/* Write 4 bytes starting from register 0x20 */
uint8_t config[] = {0x47, 0x00, 0x00, 0x88};
ret = i2c_burst_write_dt(&i2c_dev, 0x20, config, sizeof(config));
```

#### 6. Multi-Message Transfer (Scatter-Gather)

For complex transactions requiring multiple operations in one bus lock:

```c
struct i2c_msg msgs[2];

uint8_t cmd = 0x9F;  /* Command byte */
uint8_t rx_buf[4];

/* First message: write command */
msgs[0].buf = &cmd;
msgs[0].len = 1;
msgs[0].flags = I2C_MSG_WRITE;

/* Second message: read response */
msgs[1].buf = rx_buf;
msgs[1].len = sizeof(rx_buf);
msgs[1].flags = I2C_MSG_RESTART | I2C_MSG_READ | I2C_MSG_STOP;

int ret = i2c_transfer_dt(&i2c_dev, msgs, ARRAY_SIZE(msgs));
if (ret < 0) {
    printk("I2C transfer error: %d\n", ret);
}
```

#### 7. Async Transfer with Callback

Requires `CONFIG_I2C_CALLBACK=y`:

```c
#include <zephyr/drivers/i2c.h>

static volatile bool transfer_done;
static int transfer_result;

void i2c_callback(const struct device *dev, int result, void *userdata)
{
    transfer_result = result;
    transfer_done = true;
}

int async_transfer(void)
{
    struct i2c_msg msgs[1];
    uint8_t data[] = {0x01, 0x02};

    msgs[0].buf = data;
    msgs[0].len = sizeof(data);
    msgs[0].flags = I2C_MSG_WRITE | I2C_MSG_STOP;

    transfer_done = false;

    int ret = i2c_transfer_cb(i2c_dev.bus, msgs, 1, i2c_dev.addr,
                              i2c_callback, NULL);
    if (ret < 0) {
        return ret;
    }

    /* Do other work while transfer is in progress */

    /* Wait for completion */
    while (!transfer_done) {
        k_yield();
    }

    return transfer_result;
}
```

- **Full async API details**: See [#api](#api)

---

### Configuration

#### Kconfig Essentials

```kconfig
CONFIG_I2C=y                    # Enable I2C driver subsystem
CONFIG_I2C_CALLBACK=y           # Enable async callback API
CONFIG_I2C_RTIO=y               # Enable RTIO API (experimental)
CONFIG_I2C_TARGET=y             # Enable target/slave mode
CONFIG_I2C_SHELL=y              # Enable I2C shell for debugging
CONFIG_I2C_DUMP_MESSAGES=y      # Log all I2C transactions (debug)
```

- **Full Kconfig reference**: See [#kconfig](#kconfig)

#### Devicetree Essentials

##### I2C Controller

```dts
&i2c0 {
    status = "okay";
    pinctrl-0 = <&i2c0_default>;
    pinctrl-names = "default";
    clock-frequency = <I2C_BITRATE_FAST>;  /* 400 kHz */

    my_sensor: sensor@48 {
        compatible = "vendor,sensor";
        reg = <0x48>;                       /* 7-bit I2C address */
    };
};
```

##### Speed Constants (clock-frequency)

| Constant | Speed |
|----------|-------|
| `I2C_BITRATE_STANDARD` | 100 kHz |
| `I2C_BITRATE_FAST` | 400 kHz |
| `I2C_BITRATE_FAST_PLUS` | 1 MHz |
| `I2C_BITRATE_HIGH` | 3.4 MHz |
| `I2C_BITRATE_ULTRA` | 5 MHz |

- **Full devicetree reference**: See [#devicetree](#devicetree)

#### Message Flags

Common flags for `i2c_msg.flags`:

| Flag | Description |
|------|-------------|
| `I2C_MSG_WRITE` | Write operation (0) |
| `I2C_MSG_READ` | Read operation |
| `I2C_MSG_STOP` | Send STOP after this message |
| `I2C_MSG_RESTART` | Send RESTART before this message |
| `I2C_MSG_ADDR_10_BITS` | Use 10-bit addressing |

---

### Target Mode

I2C target (slave) mode allows the device to respond to an external controller.

#### Target Callbacks Setup

```c
#include <zephyr/drivers/i2c.h>

static uint8_t rx_buffer[32];
static uint8_t tx_buffer[32];
static size_t rx_index;

/* Called when controller initiates write */
static int target_write_requested(struct i2c_target_config *cfg)
{
    rx_index = 0;
    return 0;
}

/* Called for each byte received */
static int target_write_received(struct i2c_target_config *cfg, uint8_t val)
{
    if (rx_index < sizeof(rx_buffer)) {
        rx_buffer[rx_index++] = val;
    }
    return 0;
}

/* Called when controller initiates read */
static int target_read_requested(struct i2c_target_config *cfg, uint8_t *val)
{
    *val = tx_buffer[0];
    return 0;
}

/* Called after each byte sent to controller */
static int target_read_processed(struct i2c_target_config *cfg, uint8_t *val)
{
    *val = tx_buffer[1];  /* Next byte to send */
    return 0;
}

/* Called on STOP condition */
static int target_stop(struct i2c_target_config *cfg)
{
    /* Process received data */
    return 0;
}

static struct i2c_target_callbacks target_callbacks = {
    .write_requested = target_write_requested,
    .write_received = target_write_received,
    .read_requested = target_read_requested,
    .read_processed = target_read_processed,
    .stop = target_stop,
};

static struct i2c_target_config target_cfg = {
    .address = 0x60,
    .callbacks = &target_callbacks,
};

/* Register as target */
const struct device *i2c_bus = DEVICE_DT_GET(DT_NODELABEL(i2c0));

if (i2c_target_register(i2c_bus, &target_cfg) < 0) {
    printk("Failed to register I2C target\n");
}
```

#### Using EEPROM Target Driver

Zephyr provides a virtual EEPROM target driver:

```dts
&i2c0 {
    eeprom0: eeprom@52 {
        compatible = "zephyr,i2c-target-eeprom";
        reg = <0x52>;
        size = <256>;
    };
};
```

```c
const struct device *eeprom = DEVICE_DT_GET(DT_NODELABEL(eeprom0));

if (i2c_target_driver_register(eeprom) < 0) {
    printk("Failed to register EEPROM target\n");
}
```

---

### Error Handling

#### Return Values

| Value | Meaning |
|-------|---------|
| `0` | Success |
| `-ENOTSUP` | Operation not supported |
| `-EINVAL` | Invalid parameter |
| `-EIO` | I/O error (NACK, bus error) |
| `-EBUSY` | Bus busy |
| `-ETIMEDOUT` | Transfer timeout |

#### Robust Error Handling Pattern

```c
int read_sensor_data(const struct i2c_dt_spec *dev, uint8_t reg, uint8_t *data, size_t len)
{
    int ret;
    int retries = 3;

    while (retries--) {
        ret = i2c_burst_read_dt(dev, reg, data, len);
        if (ret == 0) {
            return 0;
        }
        if (ret == -ETIMEDOUT || ret == -EIO) {
            /* Try bus recovery */
            i2c_recover_bus(dev->bus);
            k_msleep(10);
            continue;
        }
        return ret;  /* Non-recoverable error */
    }
    return -EIO;
}
```

#### Bus Recovery

```c
/* Attempt to recover stuck bus (9 clock pulses) */
int ret = i2c_recover_bus(i2c_dev.bus);
if (ret < 0) {
    printk("Bus recovery failed: %d\n", ret);
}
```

---

### Troubleshooting

#### Quick Reference

| Issue | Likely Cause | Fix |
|-------|--------------|-----|
| No ACK / -EIO | Wrong address, device not powered | Verify address (7-bit), check power |
| Bus stuck | Device holding SDA low | Call `i2c_recover_bus()` |
| Timeout | Clock stretching too long | Increase timeout config |
| Garbled data | Speed too high | Reduce `clock-frequency` |
| Device not found | Wrong controller | Verify DT node hierarchy |
| `-ENOTSUP` | Missing driver feature | Check Kconfig options |

#### Debug Checklist

1. **Device ready?** Check `i2c_is_ready_dt(&spec)` or `device_is_ready(dev)`
2. **Address correct?** I2C uses 7-bit addresses (0x00-0x7F)
3. **Pins configured?** Verify pinctrl in devicetree
4. **Pull-ups present?** I2C requires pull-up resistors on SDA/SCL
5. **Speed valid?** Start with `I2C_BITRATE_STANDARD` (100kHz)
6. **Power sequence?** Some devices need power-on delay

#### I2C Shell Commands

Enable with `CONFIG_I2C_SHELL=y`:

```shell
# Scan for devices on bus
i2c scan i2c@40003000

# Read register
i2c read_byte i2c@40003000 0x48 0x00

# Write register
i2c write_byte i2c@40003000 0x48 0x20 0x47

# Recover bus
i2c recover i2c@40003000
```

---

### References

- [#api](#api) - Full API function reference with async and RTIO
- [#devicetree](#devicetree) - Devicetree properties and examples
- [#kconfig](#kconfig) - All I2C Kconfig options

### Source Locations

Key files in Zephyr source tree:
- `include/zephyr/drivers/i2c.h` - Public API header
- `include/zephyr/drivers/smbus.h` - SMBus API header
- `drivers/i2c/` - Driver implementations
- `dts/bindings/i2c/i2c-controller.yaml` - Controller DTS binding
- `dts/bindings/i2c/i2c-device.yaml` - Device DTS binding
- `samples/drivers/i2c/` - Sample applications

## Api

Complete API reference for Zephyr I2C driver subsystem.

### Table of Contents

1. [Data Structures](#data-structures)
2. [Macros and Flags](#macros-and-flags)
3. [Synchronous API](#synchronous-api)
4. [Async API](#async-api)
5. [Target Mode API](#target-mode-api)
6. [RTIO API](#rtio-api)
7. [SMBus API](#smbus-api)

---

### Data Structures

#### struct i2c_dt_spec

Devicetree-resolved I2C device specification.

```c
struct i2c_dt_spec {
    const struct device *bus;  /* I2C bus device */
    uint16_t addr;             /* Device address */
};
```

**Initialization macros:**
- `I2C_DT_SPEC_GET(node_id)` - Get spec from DT node
- `I2C_DT_SPEC_GET_BY_IDX(node_id, idx)` - Get spec by index
- `I2C_DT_SPEC_INST_GET(inst)` - Get spec from instance

#### struct i2c_msg

Single I2C message for transfers.

```c
struct i2c_msg {
    uint8_t *buf;    /* Data buffer */
    uint32_t len;    /* Buffer length in bytes */
    uint8_t flags;   /* Message flags (I2C_MSG_*) */
};
```

#### struct i2c_target_config

Configuration for I2C target (slave) mode.

```c
struct i2c_target_config {
    sys_snode_t node;
    uint8_t flags;
    uint16_t address;
    const struct i2c_target_callbacks *callbacks;
};
```

#### struct i2c_target_callbacks

Callback functions for target mode operations.

```c
struct i2c_target_callbacks {
    i2c_target_write_requested_cb_t write_requested;
    i2c_target_read_requested_cb_t read_requested;
    i2c_target_write_received_cb_t write_received;
    i2c_target_read_processed_cb_t read_processed;
    i2c_target_stop_cb_t stop;
    i2c_target_error_cb_t error;
    /* Buffer mode (CONFIG_I2C_TARGET_BUFFER_MODE) */
    i2c_target_buf_write_received_cb_t buf_write_received;
    i2c_target_buf_read_requested_cb_t buf_read_requested;
};
```

---

### Macros and Flags

#### Speed Configuration

| Macro | Value | Description |
|-------|-------|-------------|
| `I2C_SPEED_STANDARD` | 0x1 | 100 kHz |
| `I2C_SPEED_FAST` | 0x2 | 400 kHz |
| `I2C_SPEED_FAST_PLUS` | 0x3 | 1 MHz |
| `I2C_SPEED_HIGH` | 0x4 | 3.4 MHz |
| `I2C_SPEED_ULTRA` | 0x5 | 5 MHz |
| `I2C_SPEED_DT` | 0x6 | Use devicetree value |

**Speed macros:**
- `I2C_SPEED_SET(speed)` - Set speed in config
- `I2C_SPEED_GET(cfg)` - Get speed from config

#### Message Flags

| Flag | Value | Description |
|------|-------|-------------|
| `I2C_MSG_WRITE` | 0 | Write operation |
| `I2C_MSG_READ` | BIT(0) | Read operation |
| `I2C_MSG_STOP` | BIT(1) | Send STOP after message |
| `I2C_MSG_RESTART` | BIT(2) | Send RESTART before message |
| `I2C_MSG_ADDR_10_BITS` | BIT(3) | Use 10-bit addressing |

#### Mode Configuration

| Flag | Description |
|------|-------------|
| `I2C_MODE_CONTROLLER` | Controller (master) mode |

#### Target Flags

| Flag | Description |
|------|-------------|
| `I2C_TARGET_FLAGS_ADDR_10_BITS` | 10-bit target address |

#### Error Reasons

```c
enum i2c_error_reason {
    I2C_ERROR_TIMEOUT,
    I2C_ERROR_ARBITRATION,
    I2C_ERROR_SIZE,
    I2C_ERROR_DMA,
    I2C_ERROR_GENERIC
};
```

---

### Synchronous API

#### Configuration

```c
int i2c_configure(const struct device *dev, uint32_t dev_config);
```
Configure I2C controller speed and mode.

```c
int i2c_get_config(const struct device *dev, uint32_t *dev_config);
```
Get current controller configuration.

#### Core Transfer

```c
int i2c_transfer(const struct device *dev, struct i2c_msg *msgs,
                 uint8_t num_msgs, uint16_t addr);
```
Generic transfer with scatter-gather support.

```c
int i2c_transfer_dt(const struct i2c_dt_spec *spec, struct i2c_msg *msgs,
                    uint8_t num_msgs);
```
Transfer using devicetree spec (address from spec).

#### Simple Read/Write

```c
int i2c_write(const struct device *dev, const uint8_t *buf,
              uint32_t num_bytes, uint16_t addr);
int i2c_write_dt(const struct i2c_dt_spec *spec, const uint8_t *buf,
                 uint32_t num_bytes);
```
Single write transaction.

```c
int i2c_read(const struct device *dev, uint8_t *buf,
             uint32_t num_bytes, uint16_t addr);
int i2c_read_dt(const struct i2c_dt_spec *spec, uint8_t *buf,
                uint32_t num_bytes);
```
Single read transaction.

```c
int i2c_write_read(const struct device *dev, uint16_t addr,
                   const void *write_buf, size_t num_write,
                   void *read_buf, size_t num_read);
int i2c_write_read_dt(const struct i2c_dt_spec *spec,
                      const void *write_buf, size_t num_write,
                      void *read_buf, size_t num_read);
```
Combined write-then-read with RESTART.

#### Burst Access (Register-Based)

```c
int i2c_burst_read(const struct device *dev, uint16_t dev_addr,
                   uint8_t start_addr, uint8_t *buf, uint32_t num_bytes);
int i2c_burst_read_dt(const struct i2c_dt_spec *spec,
                      uint8_t start_addr, uint8_t *buf, uint32_t num_bytes);
```
Read multiple bytes from register address.

```c
int i2c_burst_write(const struct device *dev, uint16_t dev_addr,
                    uint8_t start_addr, const uint8_t *buf, uint32_t num_bytes);
int i2c_burst_write_dt(const struct i2c_dt_spec *spec,
                       uint8_t start_addr, const uint8_t *buf, uint32_t num_bytes);
```
Write multiple bytes to register address.

#### Single Register Access

```c
int i2c_reg_read_byte(const struct device *dev, uint16_t dev_addr,
                      uint8_t reg_addr, uint8_t *value);
int i2c_reg_read_byte_dt(const struct i2c_dt_spec *spec,
                         uint8_t reg_addr, uint8_t *value);
```
Read single byte from register.

```c
int i2c_reg_write_byte(const struct device *dev, uint16_t dev_addr,
                       uint8_t reg_addr, uint8_t value);
int i2c_reg_write_byte_dt(const struct i2c_dt_spec *spec,
                          uint8_t reg_addr, uint8_t value);
```
Write single byte to register.

```c
int i2c_reg_update_byte(const struct device *dev, uint8_t dev_addr,
                        uint8_t reg_addr, uint8_t mask, uint8_t value);
int i2c_reg_update_byte_dt(const struct i2c_dt_spec *spec,
                           uint8_t reg_addr, uint8_t mask, uint8_t value);
```
Read-modify-write with mask.

#### Utility Functions

```c
bool i2c_is_ready_dt(const struct i2c_dt_spec *spec);
```
Check if I2C device is ready.

```c
int i2c_recover_bus(const struct device *dev);
```
Attempt bus recovery (9 clock pulses).

```c
void i2c_dump_msgs(const struct device *dev, const struct i2c_msg *msgs,
                   uint8_t num_msgs, uint16_t addr);
```
Debug dump of I2C messages.

---

### Async API

Requires `CONFIG_I2C_CALLBACK=y`.

#### Callback Type

```c
typedef void (*i2c_callback_t)(const struct device *dev, int result, void *data);
```

#### Transfer Functions

```c
int i2c_transfer_cb(const struct device *dev, struct i2c_msg *msgs,
                    uint8_t num_msgs, uint16_t addr,
                    i2c_callback_t cb, void *userdata);
int i2c_transfer_cb_dt(const struct i2c_dt_spec *spec, struct i2c_msg *msgs,
                       uint8_t num_msgs, i2c_callback_t cb, void *userdata);
```
Async transfer with callback on completion.

```c
int i2c_write_read_cb(const struct device *dev, struct i2c_msg *msgs,
                      uint8_t num_msgs, uint16_t addr,
                      const void *write_buf, size_t num_write,
                      void *read_buf, size_t num_read,
                      i2c_callback_t cb, void *userdata);
int i2c_write_read_cb_dt(const struct i2c_dt_spec *spec, struct i2c_msg *msgs,
                         uint8_t num_msgs, const void *write_buf, size_t num_write,
                         void *read_buf, size_t num_read,
                         i2c_callback_t cb, void *userdata);
```
Async write-read with callback.

#### Signal-Based Async

Requires `CONFIG_POLL=y`:

```c
int i2c_transfer_signal(const struct device *dev, struct i2c_msg *msgs,
                        uint8_t num_msgs, uint16_t addr,
                        struct k_poll_signal *sig);
```
Async transfer with k_poll_signal notification.

---

### Target Mode API

Requires `CONFIG_I2C_TARGET=y`.

#### Registration

```c
int i2c_target_register(const struct device *dev,
                        struct i2c_target_config *cfg);
```
Register target device with callbacks on controller.

```c
int i2c_target_unregister(const struct device *dev,
                          struct i2c_target_config *cfg);
```
Unregister target device.

#### Driver Registration

For target drivers (e.g., EEPROM emulation):

```c
int i2c_target_driver_register(const struct device *dev);
int i2c_target_driver_unregister(const struct device *dev);
```

#### Callback Signatures

```c
/* Write request from controller */
typedef int (*i2c_target_write_requested_cb_t)(struct i2c_target_config *config);

/* Byte received from controller */
typedef int (*i2c_target_write_received_cb_t)(struct i2c_target_config *config,
                                               uint8_t val);

/* Read request from controller */
typedef int (*i2c_target_read_requested_cb_t)(struct i2c_target_config *config,
                                               uint8_t *val);

/* Byte sent to controller, provide next */
typedef int (*i2c_target_read_processed_cb_t)(struct i2c_target_config *config,
                                               uint8_t *val);

/* STOP condition received */
typedef int (*i2c_target_stop_cb_t)(struct i2c_target_config *config);

/* Error occurred */
typedef int (*i2c_target_error_cb_t)(struct i2c_target_config *config,
                                      enum i2c_error_reason reason);
```

#### Buffer Mode Callbacks

Requires `CONFIG_I2C_TARGET_BUFFER_MODE=y`:

```c
typedef int (*i2c_target_buf_write_received_cb_t)(
    struct i2c_target_config *config,
    uint8_t *ptr, uint32_t len);

typedef int (*i2c_target_buf_read_requested_cb_t)(
    struct i2c_target_config *config,
    uint8_t **ptr, uint32_t *len);
```

---

### RTIO API

Requires `CONFIG_I2C_RTIO=y`. Real-time I/O subsystem integration.

#### Device Definition

```c
I2C_DT_IODEV_DEFINE(name, node_id, addr);
I2C_IODEV_DEFINE(name, bus, addr);
```

#### Transfer Functions

```c
struct rtio_sqe *i2c_rtio_copy(struct rtio *r, struct rtio_iodev *iodev,
                                const struct i2c_msg *msgs, uint8_t num_msgs);
```
Copy messages to RTIO submission queue.

```c
struct rtio_sqe *i2c_rtio_copy_reg_write_byte(struct rtio *r,
                                               struct rtio_iodev *iodev,
                                               uint8_t reg_addr, uint8_t data);
```
Queue single register write.

```c
struct rtio_sqe *i2c_rtio_copy_reg_burst_read(struct rtio *r,
                                               struct rtio_iodev *iodev,
                                               uint8_t start_addr,
                                               void *buf, size_t num_bytes);
```
Queue burst register read.

#### Submission

```c
void i2c_iodev_submit(struct rtio_iodev_sqe *iodev_sqe);
```

---

### SMBus API

SMBus protocol functions in `<zephyr/drivers/smbus.h>`.

#### Configuration

```c
int smbus_configure(const struct device *dev, uint32_t dev_config);
int smbus_get_config(const struct device *dev, uint32_t *dev_config);
```

#### SMBus Commands

```c
int smbus_quick(const struct device *dev, uint16_t addr,
                enum smbus_direction direction);
```
Quick command (address only).

```c
int smbus_byte_write(const struct device *dev, uint16_t addr, uint8_t byte);
int smbus_byte_read(const struct device *dev, uint16_t addr, uint8_t *byte);
```
Send/receive byte (no command).

```c
int smbus_byte_data_write(const struct device *dev, uint16_t addr,
                          uint8_t cmd, uint8_t byte);
int smbus_byte_data_read(const struct device *dev, uint16_t addr,
                         uint8_t cmd, uint8_t *byte);
```
Write/read byte with command.

```c
int smbus_word_data_write(const struct device *dev, uint16_t addr,
                          uint8_t cmd, uint16_t word);
int smbus_word_data_read(const struct device *dev, uint16_t addr,
                         uint8_t cmd, uint16_t *word);
```
Write/read word with command.

```c
int smbus_pcall(const struct device *dev, uint16_t addr, uint8_t cmd,
                uint16_t send_word, uint16_t *recv_word);
```
Process call (send word, receive word).

```c
int smbus_block_write(const struct device *dev, uint16_t addr, uint8_t cmd,
                      uint8_t count, uint8_t *buf);
int smbus_block_read(const struct device *dev, uint16_t addr, uint8_t cmd,
                     uint8_t *count, uint8_t *buf);
```
Block write/read (up to 32 bytes).

```c
int smbus_block_pcall(const struct device *dev, uint16_t addr, uint8_t cmd,
                      uint8_t snd_count, uint8_t *snd_buf,
                      uint8_t *rcv_count, uint8_t *rcv_buf);
```
Block process call.

#### SMBus Mode Flags

| Flag | Description |
|------|-------------|
| `SMBUS_MODE_CONTROLLER` | Controller mode |
| `SMBUS_MODE_PEC` | Packet error checking |
| `SMBUS_MODE_HOST_NOTIFY` | Host notify support |
| `SMBUS_MODE_SMBALERT` | SMBus alert support |

#### SMBus Alert/Notify Callbacks

```c
int smbus_smbalert_set_cb(const struct device *dev, struct smbus_callback *cb);
int smbus_smbalert_remove_cb(const struct device *dev, struct smbus_callback *cb);
int smbus_host_notify_set_cb(const struct device *dev, struct smbus_callback *cb);
int smbus_host_notify_remove_cb(const struct device *dev, struct smbus_callback *cb);
```

## Devicetree

Complete devicetree binding reference for Zephyr I2C controllers and devices.

### Table of Contents

1. [I2C Controller Properties](#i2c-controller-properties)
2. [I2C Device Properties](#i2c-device-properties)
3. [Common Patterns](#common-patterns)
4. [Target/Slave Mode](#targetslave-mode)
5. [I2C Switches/Mux](#i2c-switchesmux)
6. [Vendor-Specific Properties](#vendor-specific-properties)

---

### I2C Controller Properties

From `dts/bindings/i2c/i2c-controller.yaml`:

| Property | Type | Required | Description |
|----------|------|----------|-------------|
| `#address-cells` | const | yes | Must be 1 |
| `#size-cells` | const | yes | Must be 0 |
| `clock-frequency` | int | no | Initial clock frequency in Hz |
| `sq-size` | int | no | RTIO submission queue size (default: 4) |
| `cq-size` | int | no | RTIO completion queue size (default: 4) |

#### Basic Controller Example

```dts
&i2c0 {
    status = "okay";
    #address-cells = <1>;
    #size-cells = <0>;
    pinctrl-0 = <&i2c0_default>;
    pinctrl-names = "default";
    clock-frequency = <I2C_BITRATE_FAST>;
};
```

#### Clock Frequency Constants

Use these in devicetree overlays:

| Constant | Value | Speed |
|----------|-------|-------|
| `I2C_BITRATE_STANDARD` | 100000 | 100 kHz |
| `I2C_BITRATE_FAST` | 400000 | 400 kHz |
| `I2C_BITRATE_FAST_PLUS` | 1000000 | 1 MHz |
| `I2C_BITRATE_HIGH` | 3400000 | 3.4 MHz |
| `I2C_BITRATE_ULTRA` | 5000000 | 5 MHz |

---

### I2C Device Properties

From `dts/bindings/i2c/i2c-device.yaml`:

| Property | Type | Required | Description |
|----------|------|----------|-------------|
| `reg` | int | yes | 7-bit I2C device address |

#### From Included `power.yaml`

| Property | Type | Description |
|----------|------|-------------|
| `supply-gpios` | phandle-array | GPIO controlling device power |
| `vin-supply` | phandle | Reference to power regulator |

#### Basic Device Example

```dts
&i2c0 {
    status = "okay";
    clock-frequency = <I2C_BITRATE_STANDARD>;

    /* Temperature sensor at address 0x48 */
    temp_sensor: tmp102@48 {
        compatible = "ti,tmp102";
        reg = <0x48>;
    };

    /* Accelerometer at address 0x1D */
    accel: lis2dh@1d {
        compatible = "st,lis2dh";
        reg = <0x1d>;
    };
};
```

#### Device with Power Control

```dts
&i2c0 {
    sensor@48 {
        compatible = "vendor,sensor";
        reg = <0x48>;
        supply-gpios = <&gpio0 5 GPIO_ACTIVE_HIGH>;
        vin-supply = <&vdd_3v3>;
    };
};
```

---

### Common Patterns

#### Using DT_NODELABEL

```dts
/* In board DTS */
&i2c0 {
    my_sensor: sensor@48 {
        compatible = "vendor,sensor";
        reg = <0x48>;
    };
};
```

```c
/* In application code */
#define SENSOR_NODE DT_NODELABEL(my_sensor)
static const struct i2c_dt_spec sensor = I2C_DT_SPEC_GET(SENSOR_NODE);
```

#### Using DT_ALIAS

```dts
/* In board DTS */
/ {
    aliases {
        i2c-0 = &i2c0;
        temp-sensor = &tmp102;
    };
};

&i2c0 {
    tmp102: tmp102@48 {
        compatible = "ti,tmp102";
        reg = <0x48>;
    };
};
```

```c
/* In application code */
#define I2C_DEV DT_ALIAS(i2c_0)
#define SENSOR DT_ALIAS(temp_sensor)
```

#### Multiple Devices on One Bus

```dts
&i2c0 {
    status = "okay";
    clock-frequency = <I2C_BITRATE_FAST>;

    eeprom@50 {
        compatible = "atmel,at24";
        reg = <0x50>;
    };

    rtc@68 {
        compatible = "nxp,pcf8563";
        reg = <0x68>;
    };

    gpio_expander@20 {
        compatible = "nxp,pca9535";
        reg = <0x20>;
        gpio-controller;
        #gpio-cells = <2>;
    };
};
```

#### Overlay for Custom Board

```dts
/* boards/my_board.overlay */
&i2c1 {
    status = "okay";
    pinctrl-0 = <&i2c1_sda_pb7 &i2c1_scl_pb6>;
    pinctrl-names = "default";
    clock-frequency = <I2C_BITRATE_FAST>;

    custom_sensor: sensor@29 {
        compatible = "vendor,custom-sensor";
        reg = <0x29>;
    };
};
```

---

### Target/Slave Mode

#### EEPROM Target (Zephyr Built-in)

```dts
&i2c0 {
    eeprom0: eeprom@52 {
        compatible = "zephyr,i2c-target-eeprom";
        reg = <0x52>;
        size = <256>;  /* EEPROM size in bytes */
    };
};
```

#### Nordic nRF TWIS (Target Mode)

```dts
&pinctrl {
    i2c2_default: i2c2_default {
        group1 {
            psels = <NRF_PSEL(TWIS_SDA, 0, 26)>,
                    <NRF_PSEL(TWIS_SCL, 0, 25)>;
            bias-pull-up;
        };
    };

    i2c2_sleep: i2c2_sleep {
        group1 {
            psels = <NRF_PSEL(TWIS_SDA, 0, 26)>,
                    <NRF_PSEL(TWIS_SCL, 0, 25)>;
            low-power-enable;
        };
    };
};

&i2c2 {
    compatible = "nordic,nrf-twis";
    pinctrl-0 = <&i2c2_default>;
    pinctrl-1 = <&i2c2_sleep>;
    pinctrl-names = "default", "sleep";
    status = "okay";
};
```

#### ITE Target Mode

```dts
&i2c0 {
    status = "okay";
    pinctrl-0 = <&i2c5_clk_gpa4_default &i2c5_data_gpa5_default>;
    pinctrl-names = "default";
    scl-gpios = <&gpioa 4 0>;
    sda-gpios = <&gpioa 5 0>;

    target-enable;  /* Enable target mode */

    i2c0_target: target@52 {
        compatible = "ite,target-i2c";
        reg = <0x52>;
    };
};
```

---

### I2C Switches/Mux

#### TI TCA9546A Switch

```dts
&i2c0 {
    mux: tca9546a@77 {
        compatible = "ti,tca9546a";
        reg = <0x77>;
        status = "okay";
        #address-cells = <1>;
        #size-cells = <0>;
        reset-gpios = <&gpio5 3 GPIO_ACTIVE_LOW>;

        mux_i2c@0 {
            compatible = "ti,tca9546a-channel";
            reg = <0>;
            #address-cells = <1>;
            #size-cells = <0>;

            temp_sens_0: tmp11x@49 {
                compatible = "ti,tmp11x";
                reg = <0x49>;
            };
        };

        mux_i2c@1 {
            compatible = "ti,tca9546a-channel";
            reg = <1>;
            #address-cells = <1>;
            #size-cells = <0>;

            temp_sens_1: tmp11x@49 {
                compatible = "ti,tmp11x";
                reg = <0x49>;
            };
        };
    };
};
```

#### TCA954x Properties

| Property | Type | Description |
|----------|------|-------------|
| `reset-gpios` | phandle-array | Active-low reset GPIO |
| `i2c-mux-idle-disconnect` | bool | Disconnect channels when idle |

---

### Vendor-Specific Properties

#### STM32

```dts
&i2c1 {
    status = "okay";
    pinctrl-0 = <&i2c1_scl_pb8 &i2c1_sda_pb9>;
    pinctrl-names = "default";
    clock-frequency = <I2C_BITRATE_FAST>;

    /* Optional: DMA support */
    dmas = <&dma1 6 3 0x400>,
           <&dma1 7 3 0x400>;
    dma-names = "tx", "rx";

    /* Optional: precomputed timings */
    timings = <...>;
};
```

#### Nordic nRF

```dts
&i2c0 {
    compatible = "nordic,nrf-twim";
    status = "okay";
    pinctrl-0 = <&i2c0_default>;
    pinctrl-names = "default";
    /* easydma-maxcnt-bits set in SoC DTS */
};
```

#### ESP32

```dts
&i2c0 {
    status = "okay";
    clock-frequency = <I2C_BITRATE_FAST>;
    sda-gpios = <&gpio0 21 GPIO_OPEN_DRAIN>;
    scl-gpios = <&gpio0 22 GPIO_OPEN_DRAIN>;
    scl-timeout-us = <100000>;  /* Clock stretching timeout */
};
```

#### GPIO Bitbang

```dts
i2c_gpio: i2c-gpio {
    compatible = "gpio-i2c";
    status = "okay";
    sda-gpios = <&gpio0 4 (GPIO_OPEN_DRAIN | GPIO_PULL_UP)>;
    scl-gpios = <&gpio0 5 (GPIO_OPEN_DRAIN | GPIO_PULL_UP)>;
    #address-cells = <1>;
    #size-cells = <0>;
};
```

#### Common Vendor Properties

| Vendor | Properties |
|--------|------------|
| **STM32** | `timings`, `dmas`, `dma-names` |
| **ESP32** | `tx-lsb`, `rx-lsb`, `scl-timeout-us`, `scl-gpios`, `sda-gpios` |
| **Nordic** | `easydma-maxcnt-bits` (set in SoC DTS) |
| **NXP LPI2C** | `bus-idle-timeout`, `scl-gpios`, `sda-gpios` |
| **Renesas** | `rise-time-ns`, `fall-time-ns`, `duty-cycle-percent` |
| **DesignWare** | `lcnt-offset`, `hcnt-offset`, `sda-timeout-value`, `scl-timeout-value` |
| **ITE** | `port-num`, `channel-switch-sel`, `target-enable`, `target-pio-mode` |

---

### Bus Recovery GPIOs

Many drivers support bus recovery via GPIO bitbanging:

```dts
&i2c0 {
    /* SCL/SDA GPIOs for bus recovery */
    scl-gpios = <&gpio0 22 GPIO_OPEN_DRAIN>;
    sda-gpios = <&gpio0 21 GPIO_OPEN_DRAIN>;
};
```

Enable with corresponding Kconfig:
- `CONFIG_I2C_STM32_BUS_RECOVERY`
- `CONFIG_I2C_MCUX_LPI2C_BUS_RECOVERY`
- `CONFIG_I2C_OMAP_BUS_RECOVERY`
- etc.

## Kconfig

Complete Kconfig reference for Zephyr I2C subsystem.

### Table of Contents

1. [Core Options](#core-options)
2. [Async/RTIO Options](#asyncrtio-options)
3. [Target Mode Options](#target-mode-options)
4. [Shell/Debug Options](#shelldebug-options)
5. [Bus Recovery Options](#bus-recovery-options)
6. [Driver-Specific Options](#driver-specific-options)

---

### Core Options

#### CONFIG_I2C

```kconfig
menuconfig I2C
    bool "Inter-Integrated Circuit (I2C) bus drivers"
```
Main enabler for I2C driver subsystem.

#### CONFIG_I2C_INIT_PRIORITY

```kconfig
config I2C_INIT_PRIORITY
    int "I2C device driver initialization priority"
    default KERNEL_INIT_PRIORITY_DEVICE
    depends on I2C
```
Init priority for I2C drivers.

#### CONFIG_I2C_CALLBACK

```kconfig
config I2C_CALLBACK
    bool "I2C asynchronous callback API"
    depends on I2C
```
Enable async `i2c_transfer_cb()` API.

#### CONFIG_I2C_ALLOW_NO_STOP_TRANSACTIONS

```kconfig
config I2C_ALLOW_NO_STOP_TRANSACTIONS
    bool "Allow I2C transfers with no STOP"
    select DEPRECATED
```
**Deprecated.** Allow transfers without STOP on last message.

---

### Async/RTIO Options

#### CONFIG_I2C_RTIO

```kconfig
config I2C_RTIO
    bool "I2C RTIO API"
    select EXPERIMENTAL
    select RTIO
    select RTIO_WORKQ
```
Enable Real-Time I/O subsystem for I2C. Experimental.

#### CONFIG_I2C_RTIO_SQ_SIZE

```kconfig
config I2C_RTIO_SQ_SIZE
    int "Submission queue size for blocking calls"
    default 4
    depends on I2C_RTIO
```

#### CONFIG_I2C_RTIO_CQ_SIZE

```kconfig
config I2C_RTIO_CQ_SIZE
    int "Completion queue size for blocking calls"
    default 4
    depends on I2C_RTIO
```

#### CONFIG_I2C_RTIO_FALLBACK_MSGS

```kconfig
config I2C_RTIO_FALLBACK_MSGS
    int "i2c_msg structs for fallback handler"
    default 4
    depends on I2C_RTIO
```

---

### Target Mode Options

#### CONFIG_I2C_TARGET

```kconfig
menuconfig I2C_TARGET
    bool "I2C Target drivers"
```
Enable I2C target (slave) mode support.

#### CONFIG_I2C_TARGET_INIT_PRIORITY

```kconfig
config I2C_TARGET_INIT_PRIORITY
    int "Target driver init priority"
    default 60
    depends on I2C_TARGET
```

#### CONFIG_I2C_TARGET_BUFFER_MODE

```kconfig
config I2C_TARGET_BUFFER_MODE
    bool "I2C target driver buffer mode"
    select EXPERIMENTAL
    depends on I2C_TARGET
```
Enable buffer-mode callbacks for target drivers.

#### CONFIG_I2C_EEPROM_TARGET

```kconfig
config I2C_EEPROM_TARGET
    bool "Virtual I2C Target EEPROM driver"
```
Built-in virtual EEPROM target driver.

#### CONFIG_I2C_EEPROM_TARGET_RUNTIME_ADDR

```kconfig
config I2C_EEPROM_TARGET_RUNTIME_ADDR
    bool "Change virtual EEPROM address at runtime"
    depends on I2C_EEPROM_TARGET
```

#### Target Buffer Size Options

| Option | Default | Range | Description |
|--------|---------|-------|-------------|
| `CONFIG_I2C_INFINEON_CAT1_TARGET_BUF` | 64 | 1-1024 | Infineon CAT1 target buffer |
| `CONFIG_I2C_INFINEON_XMC4_TARGET_BUF` | 64 | 1-1024 | Infineon XMC4 target buffer |
| `CONFIG_I2C_TARGET_IT8XXX2_MAX_BUF_SIZE` | 256 | 4-2044 | ITE IT8XXX2 target buffer |
| `CONFIG_I2C_TARGET_IT51XXX_MAX_BUF_SIZE` | 256 | - | ITE IT51XXX target FIFO |
| `CONFIG_I2C_NRFX_TWIS_BUF_SIZE` | 64 | - | Nordic TWIS DMA buffer |

#### CONFIG_I2C_TARGET_ALLOW_POWER_SAVING

```kconfig
config I2C_TARGET_ALLOW_POWER_SAVING
    bool "Allow target to enter low power when idle"
    select EXPERIMENTAL
    depends on I2C_TARGET && !SOC_IT8XXX2_REG_SET_V1
```

---

### Shell/Debug Options

#### CONFIG_I2C_SHELL

```kconfig
config I2C_SHELL
    bool "I2C Shell"
    depends on SHELL
```
Enable I2C shell commands (scan, read, write, recover).

#### CONFIG_I2C_STATS

```kconfig
config I2C_STATS
    bool "I2C device stats"
    depends on STATS
```
Enable I2C transfer statistics.

#### CONFIG_I2C_DUMP_MESSAGES

```kconfig
config I2C_DUMP_MESSAGES
    bool "Log all I2C transactions"
    depends on LOG && I2C_LOG_LEVEL_DBG
```
Debug logging of all I2C transactions.

#### CONFIG_I2C_DUMP_MESSAGES_ALLOWLIST

```kconfig
config I2C_DUMP_MESSAGES_ALLOWLIST
    bool "Allowlist for I2C transaction logging"
    depends on I2C_DUMP_MESSAGES
    depends on DT_HAS_ZEPHYR_I2C_DUMP_ALLOWLIST_ENABLED
```
Limit message dumping to specific devices.

---

### Bus Recovery Options

GPIO-based bus recovery support (varies by driver):

| Option | Driver | Description |
|--------|--------|-------------|
| `CONFIG_I2C_AMBIQ_BUS_RECOVERY` | Ambiq | GPIO bitbang recovery |
| `CONFIG_I2C_MCUX_LPI2C_BUS_RECOVERY` | NXP LPI2C | GPIO bitbang recovery |
| `CONFIG_I2C_MCUX_FLEXCOMM_BUS_RECOVERY` | NXP FlexComm | GPIO bitbang recovery |
| `CONFIG_I2C_OMAP_BUS_RECOVERY` | TI OMAP | Bitbang recovery |
| `CONFIG_I2C_STM32_BUS_RECOVERY` | STM32 | GPIO bitbang recovery |

All select `CONFIG_I2C_BITBANG`.

#### CONFIG_I2C_BITBANG

```kconfig
config I2C_BITBANG
    bool "Software-driven bit-banging I2C library"
```
Base library for software I2C implementation.

#### CONFIG_I2C_GPIO

```kconfig
config I2C_GPIO
    bool "GPIO bit-banging I2C driver"
    select I2C_BITBANG
    default y
    depends on DT_HAS_GPIO_I2C_ENABLED
```
Pure GPIO software I2C driver.

#### CONFIG_I2C_GPIO_CLOCK_STRETCHING

```kconfig
config I2C_GPIO_CLOCK_STRETCHING
    bool "Clock stretching support"
    default y
    depends on I2C_GPIO
```

#### CONFIG_I2C_GPIO_CLOCK_STRETCHING_TIMEOUT_US

```kconfig
config I2C_GPIO_CLOCK_STRETCHING_TIMEOUT_US
    int "Clock stretching timeout (us)"
    default 100000
    depends on I2C_GPIO
```

---

### Driver-Specific Options

#### DMA/Interrupt Options

| Option | Driver | Description |
|--------|--------|-------------|
| `CONFIG_I2C_DW_LPSS_DMA` | DesignWare | DMA for async transfers |
| `CONFIG_I2C_STM32_INTERRUPT` | STM32 | Interrupt support (default y) |
| `CONFIG_I2C_STM32_V2_DMA` | STM32 V2 | DMA support (experimental) |
| `CONFIG_I2C_MAX32_INTERRUPT` | MAX32 | Interrupt support (default y) |
| `CONFIG_I2C_MAX32_DMA` | MAX32 | DMA support |
| `CONFIG_I2C_SAM0_DMA_DRIVEN` | SAM0 | DMA-driven transactions |
| `CONFIG_I2C_SILABS_DMA` | Silabs | DMA support |

#### Timeout Options

| Option | Driver | Default | Description |
|--------|--------|---------|-------------|
| `CONFIG_I2C_DW_RW_TIMEOUT_MS` | DesignWare | 100 | Read/write timeout |
| `CONFIG_I2C_SILABS_TIMEOUT` | Silabs | 1000 | Transfer timeout (ms) |
| `CONFIG_I2C_STM32_TRANSFER_TIMEOUT_MSEC` | STM32 | 500 | Transfer timeout |
| `CONFIG_I2C_SAM0_TRANSFER_TIMEOUT` | SAM0 | 500 | Transfer timeout |
| `CONFIG_I2C_NRFX_TRANSFER_TIMEOUT` | Nordic | 500 | Transfer timeout (0=forever) |
| `CONFIG_I2C_NXP_TRANSFER_TIMEOUT` | NXP FlexComm | 0 | Transfer timeout (0=forever) |
| `CONFIG_I2C_WCH_XFER_TIMEOUT_MS` | WCH | 500 | Transfer timeout |

#### DesignWare Extended

```kconfig
config I2C_DW_CLOCK_SPEED
    int "I2C clock speed"
    default 110 if I2C_RTS5912
    default 32
    depends on I2C_DW

config I2C_DW_EXTENDED_SUPPORT
    bool "Enable extended DesignWare features"
```
SCL/SDA timeout and other extensions.

#### I2C Switch Options

```kconfig
menuconfig I2C_TCA954X
    bool "TCA954x I2C switch"
    default y
    depends on DT_HAS_TI_TCA9546A_ENABLED || ...

config I2C_TCA954X_ROOT_INIT_PRIO
    int "Root driver init priority"
    default I2C_INIT_PRIORITY
    depends on I2C_TCA954X

config I2C_TCA954X_CHANNEL_INIT_PRIO
    int "Channel driver init priority"
    default I2C_INIT_PRIORITY
    depends on I2C_TCA954X
```

#### Emulation

```kconfig
config I2C_EMUL
    bool "I2C emulator driver"
    default y
    depends on DT_HAS_ZEPHYR_I2C_EMUL_CONTROLLER_ENABLED && EMUL
```
For testing without hardware.

---

### Common Driver Enable Options

Most drivers auto-enable based on devicetree. Pattern:

```kconfig
config I2C_<VENDOR>
    bool "<Vendor> I2C driver"
    default y
    depends on DT_HAS_<VENDOR>_<COMPAT>_ENABLED
    select PINCTRL  # common dependency
```

Major drivers:
- `CONFIG_I2C_STM32` - STMicroelectronics
- `CONFIG_I2C_NRFX` - Nordic (TWI/TWIM)
- `CONFIG_I2C_ESP32` - Espressif
- `CONFIG_I2C_MCUX_LPI2C` - NXP i.MX RT
- `CONFIG_I2C_MCUX_FLEXCOMM` - NXP LPC
- `CONFIG_I2C_SAM_TWI` / `TWIM` / `TWIHS` - Atmel SAM
- `CONFIG_I2C_DW` - Synopsys DesignWare
- `CONFIG_I2C_GD32` - GigaDevice
- `CONFIG_I2C_GECKO` - Silicon Labs EFM32
- `CONFIG_I2C_RENESAS_*` - Renesas RA/RZ
- `CONFIG_I2C_XEC` / `XEC_V2` - Microchip XEC

---

### Quick Reference

#### Minimal Configuration

```kconfig
# prj.conf
CONFIG_I2C=y
```

#### Debug Configuration

```kconfig
CONFIG_I2C=y
CONFIG_I2C_SHELL=y
CONFIG_I2C_DUMP_MESSAGES=y
CONFIG_LOG=y
CONFIG_I2C_LOG_LEVEL_DBG=y
```

#### Async Configuration

```kconfig
CONFIG_I2C=y
CONFIG_I2C_CALLBACK=y
# or for RTIO:
CONFIG_I2C_RTIO=y
```

#### Target Mode Configuration

```kconfig
CONFIG_I2C=y
CONFIG_I2C_TARGET=y
CONFIG_I2C_EEPROM_TARGET=y  # optional built-in target
```
