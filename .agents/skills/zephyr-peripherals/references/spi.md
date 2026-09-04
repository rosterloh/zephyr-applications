# SPI

## Overview

Expert guidance for Zephyr's SPI driver subsystem covering synchronous and asynchronous transfer APIs, devicetree configuration, and common usage patterns.

### Table of Contents

1. [API Selection](#api-selection)
2. [Getting Device Reference](#getting-device-reference)
3. [Common Workflows](#common-workflows)
4. [Configuration](#configuration)
5. [Error Handling](#error-handling)
6. [Troubleshooting](#troubleshooting)

---

### API Selection

Zephyr provides two SPI access methods. Choose based on requirements:

| API | Kconfig | Use Case | Blocking? |
|-----|---------|----------|-----------|
| **Synchronous** | (default) | Simple transfers, blocking until complete | Yes |
| **Asynchronous (callback/signal)** | `CONFIG_SPI_ASYNC` | Non-blocking, callback/signal on completion | No |
| **RTIO** | `CONFIG_SPI_RTIO` | Queue-based async via `SPI_DT_IODEV_DEFINE` / `spi_iodev_submit`. Forward-looking path in Zephyr 4.4 — the callback-based async API still works, but new async code should prefer RTIO when available. | No |

#### Decision Tree

```
Simple blocking transfer? → Synchronous (spi_transceive)
Need non-blocking transfer? → Async (spi_transceive_cb / spi_transceive_signal)
Building a new high-throughput async pipeline? → RTIO (SPI_DT_IODEV_DEFINE + spi_iodev_submit)
High throughput with minimal CPU? → Async with DMA-capable driver
Multiple concurrent SPI operations? → Async with proper buffer management
```

---

### Getting Device Reference

#### From Devicetree (Preferred - for SPI Devices)

Use `SPI_DT_SPEC_GET` to get a complete SPI specification including bus, config, and CS:

```c
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>

/* Define the SPI device from devicetree */
#define MY_SPI_DEVICE DT_NODELABEL(my_spi_device)

static const struct spi_dt_spec spi_dev = SPI_DT_SPEC_GET(
    MY_SPI_DEVICE,
    SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB
);

/* Runtime check (in main or init) */
if (!spi_is_ready_dt(&spi_dev)) {
    printk("SPI device not ready\n");
    return -ENODEV;
}
```

#### Direct Controller Access (Less Common)

```c
/* Get SPI controller directly */
const struct device *spi = DEVICE_DT_GET(DT_NODELABEL(spi0));

if (!device_is_ready(spi)) {
    return -ENODEV;
}

/* Manual config setup */
struct spi_config spi_cfg = {
    .frequency = 1000000U,  /* 1 MHz */
    .operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8),
    .slave = 0,
    .cs = {0},  /* No GPIO CS, use hardware CS */
};
```

---

### Common Workflows

#### 1. Basic Transceive (Simultaneous TX/RX)

```c
#include <zephyr/drivers/spi.h>

uint8_t tx_buf[] = {0x9F, 0x00, 0x00, 0x00};  /* Read ID command + dummy bytes */
uint8_t rx_buf[4];

struct spi_buf tx = {.buf = tx_buf, .len = sizeof(tx_buf)};
struct spi_buf rx = {.buf = rx_buf, .len = sizeof(rx_buf)};
struct spi_buf_set tx_set = {.buffers = &tx, .count = 1};
struct spi_buf_set rx_set = {.buffers = &rx, .count = 1};

int ret = spi_transceive_dt(&spi_dev, &tx_set, &rx_set);
if (ret < 0) {
    printk("SPI transceive error: %d\n", ret);
}
/* rx_buf now contains response */
```

#### 2. Write Only (No RX)

```c
uint8_t cmd[] = {0x06};  /* Write enable command */

struct spi_buf tx = {.buf = cmd, .len = sizeof(cmd)};
struct spi_buf_set tx_set = {.buffers = &tx, .count = 1};

int ret = spi_write_dt(&spi_dev, &tx_set);
```

#### 3. Read Only (TX Ignored)

```c
uint8_t data[16];

struct spi_buf rx = {.buf = data, .len = sizeof(data)};
struct spi_buf_set rx_set = {.buffers = &rx, .count = 1};

int ret = spi_read_dt(&spi_dev, &rx_set);
```

#### 4. Scatter-Gather (Multi-Buffer Transfer)

Use multiple buffers for command + address + data in a single CS assertion:

```c
uint8_t cmd = 0x03;           /* Read command */
uint8_t addr[3] = {0x00, 0x10, 0x00};  /* 24-bit address */
uint8_t data[64];

struct spi_buf tx_bufs[] = {
    {.buf = &cmd, .len = 1},
    {.buf = addr, .len = 3},
    {.buf = NULL, .len = 64},  /* NULL = send zeros while receiving */
};
struct spi_buf rx_bufs[] = {
    {.buf = NULL, .len = 1},   /* NULL = ignore received data */
    {.buf = NULL, .len = 3},
    {.buf = data, .len = 64},
};

struct spi_buf_set tx_set = {.buffers = tx_bufs, .count = 3};
struct spi_buf_set rx_set = {.buffers = rx_bufs, .count = 3};

int ret = spi_transceive_dt(&spi_dev, &tx_set, &rx_set);
```

#### 5. Async Transfer with Callback

Requires `CONFIG_SPI_ASYNC=y`:

```c
#include <zephyr/drivers/spi.h>

static volatile bool transfer_done;
static int transfer_result;

void spi_callback(const struct device *dev, int result, void *userdata)
{
    transfer_result = result;
    transfer_done = true;
}

int async_transfer(void)
{
    uint8_t tx_data[] = {0x01, 0x02, 0x03};
    uint8_t rx_data[3];

    struct spi_buf tx = {.buf = tx_data, .len = sizeof(tx_data)};
    struct spi_buf rx = {.buf = rx_data, .len = sizeof(rx_data)};
    struct spi_buf_set tx_set = {.buffers = &tx, .count = 1};
    struct spi_buf_set rx_set = {.buffers = &rx, .count = 1};

    transfer_done = false;

    int ret = spi_transceive_cb(spi_dev.bus, &spi_dev.config,
                                 &tx_set, &rx_set, spi_callback, NULL);
    if (ret < 0) {
        return ret;
    }

    /* Do other work while transfer is in progress */

    /* Wait for completion (or use k_poll) */
    while (!transfer_done) {
        k_yield();
    }

    return transfer_result;
}
```

- **Full async API details**: See [#async](#async)

---

### Configuration

#### Kconfig Essentials

```kconfig
CONFIG_SPI=y                    # Enable SPI driver subsystem
CONFIG_SPI_ASYNC=y              # Enable async API (callback/signal)
CONFIG_SPI_RTIO=y                # Enable RTIO async path (SPI_DT_IODEV_DEFINE / spi_iodev_submit)
CONFIG_SPI_SLAVE=y              # Enable slave mode (experimental)
CONFIG_SPI_EXTENDED_MODES=y     # Enable dual/quad/octal line modes
CONFIG_SPI_SHELL=y              # Enable SPI shell for debugging
```

- **Full Kconfig reference**: See [#kconfig](#kconfig)

#### Devicetree Essentials

##### SPI Controller

```dts
&spi0 {
    status = "okay";
    pinctrl-0 = <&spi0_default>;
    pinctrl-names = "default";
    cs-gpios = <&gpio0 4 GPIO_ACTIVE_LOW>;  /* CS on GPIO0 pin 4 */

    my_spi_device: sensor@0 {
        compatible = "vendor,sensor";
        reg = <0>;                      /* CS index (matches cs-gpios) */
        spi-max-frequency = <1000000>;  /* 1 MHz max */
        /* Optional SPI mode settings */
        spi-cpol;                       /* Clock idle high (Mode 2 or 3) */
        spi-cpha;                       /* Sample on second edge (Mode 1 or 3) */
    };
};
```

##### SPI Mode Quick Reference

| Mode | CPOL | CPHA | DTS Properties |
|------|------|------|----------------|
| 0 | 0 | 0 | (default) |
| 1 | 0 | 1 | `spi-cpha` |
| 2 | 1 | 0 | `spi-cpol` |
| 3 | 1 | 1 | `spi-cpol; spi-cpha` |

- **Full devicetree reference**: See [#devicetree](#devicetree)

#### Operation Flags

Common flags for `spi_config.operation`:

| Flag | Description |
|------|-------------|
| `SPI_OP_MODE_MASTER` | Controller/master mode (default) |
| `SPI_OP_MODE_SLAVE` | Peripheral/slave mode |
| `SPI_MODE_CPOL` | Clock polarity (idle high) |
| `SPI_MODE_CPHA` | Clock phase (sample on second edge) |
| `SPI_WORD_SET(n)` | Word size in bits (typically 8) |
| `SPI_TRANSFER_MSB` | MSB first (default) |
| `SPI_TRANSFER_LSB` | LSB first |
| `SPI_HOLD_ON_CS` | Keep CS active after transaction |
| `SPI_LOCK_ON` | Lock bus for multiple transactions |
| `SPI_CS_ACTIVE_HIGH` | CS is active high (unusual) |

---

### Error Handling

#### Return Values

| Value | Meaning |
|-------|---------|
| `0` | Success (master mode) |
| `> 0` | Frames received (slave mode) |
| `-ENOTSUP` | Unsupported config (check operation flags) |
| `-EINVAL` | Invalid parameter in spi_config |
| `-EBUSY` | Bus locked by another caller |
| `-errno` | Other failure |

#### Releasing Locked Bus

If using `SPI_LOCK_ON` or `SPI_HOLD_ON_CS`, release when done:

```c
spi_release_dt(&spi_dev);
```

---

### Troubleshooting

#### Quick Reference

| Issue | Likely Cause | Fix |
|-------|--------------|-----|
| No data on bus | Wrong pins | Verify pinctrl in devicetree |
| Garbled data | Mode mismatch | Check CPOL/CPHA match device datasheet |
| CS not toggling | GPIO not configured | Add `cs-gpios` to controller node |
| `-ENOTSUP` | Unsupported operation | Check driver supports requested features |
| `-EBUSY` | Bus locked | Call `spi_release()` or remove `SPI_LOCK_ON` |
| Slow transfer | Low frequency | Increase `spi-max-frequency` in DTS |
| RX data offset | TX/RX length mismatch | Ensure buffer lengths account for command bytes |

#### Debug Checklist

1. **Device ready?** Check `spi_is_ready_dt(&spec)`
2. **Pins correct?** Verify pinctrl (SCK, MOSI, MISO, CS)
3. **Mode match?** CPOL/CPHA must match peripheral device
4. **Frequency valid?** Some devices have min/max limits
5. **CS polarity?** Most devices are active-low (default)
6. **Word size?** Most devices use 8-bit words

---

### References

- [#api](#api) — Full API function reference
- [#async](#async) — Async API with callback/signal patterns
- [#kconfig](#kconfig) — All SPI Kconfig options
- [#devicetree](#devicetree) — Devicetree properties and examples

### Source Locations

Key files in Zephyr source tree:
- `include/zephyr/drivers/spi.h` — Public API header
- `drivers/spi/` — Driver implementations
- `dts/bindings/spi/spi-controller.yaml` — Controller DTS binding
- `dts/bindings/spi/spi-device.yaml` — Device DTS binding
- `samples/drivers/spi_*` — Sample applications

## Api

Complete reference for Zephyr's SPI driver API functions.

### Table of Contents

1. [Core Structures](#core-structures)
2. [Synchronous API](#synchronous-api)
3. [Asynchronous API](#asynchronous-api)
4. [Utility Functions](#utility-functions)
5. [Devicetree Macros](#devicetree-macros)

---

### Core Structures

#### struct spi_config

Configuration for SPI transactions:

```c
struct spi_config {
    uint32_t frequency;           /* Bus frequency in Hz */
    spi_operation_t operation;    /* Operation flags (mode, word size, etc.) */
    uint16_t slave;               /* Slave number (0 to controller limit) */
    struct spi_cs_control cs;     /* GPIO chip-select (optional) */
    uint16_t word_delay;          /* Delay between words in ns */
};
```

#### struct spi_dt_spec

Complete SPI device specification from devicetree:

```c
struct spi_dt_spec {
    const struct device *bus;     /* SPI controller device */
    struct spi_config config;     /* Slave-specific configuration */
};
```

#### struct spi_buf

Single buffer descriptor:

```c
struct spi_buf {
    void *buf;    /* Data buffer, or NULL for NOP */
    size_t len;   /* Buffer length in bytes */
};
```

- If `buf` is NULL for TX: sends zeros for `len` bytes
- If `buf` is NULL for RX: ignores `len` received bytes

#### struct spi_buf_set

Scatter-gather buffer array:

```c
struct spi_buf_set {
    const struct spi_buf *buffers;  /* Array of spi_buf */
    size_t count;                   /* Number of buffers */
};
```

#### struct spi_cs_control

Chip select control (GPIO or hardware):

```c
struct spi_cs_control {
    struct gpio_dt_spec gpio;  /* GPIO for CS (if cs_is_gpio=true) */
    uint32_t delay;            /* Delay in microseconds */
    /* OR for native CS: */
    uint32_t setup_ns;         /* CS setup time in ns */
    uint32_t hold_ns;          /* CS hold time in ns */
    bool cs_is_gpio;           /* True if using GPIO CS */
};
```

---

### Synchronous API

All synchronous functions block until the transfer completes.

#### spi_transceive

```c
int spi_transceive(const struct device *dev,
                   const struct spi_config *config,
                   const struct spi_buf_set *tx_bufs,
                   const struct spi_buf_set *rx_bufs);
```

Read and write data simultaneously.

**Parameters:**
- `dev`: SPI controller device
- `config`: SPI configuration
- `tx_bufs`: TX buffer set (or NULL)
- `rx_bufs`: RX buffer set (or NULL)

**Returns:**
- `0`: Success (master mode)
- `> 0`: Frames received (slave mode)
- `-ENOTSUP`: Unsupported configuration
- `-EINVAL`: Invalid parameter
- `-errno`: Other failure

#### spi_transceive_dt

```c
int spi_transceive_dt(const struct spi_dt_spec *spec,
                      const struct spi_buf_set *tx_bufs,
                      const struct spi_buf_set *rx_bufs);
```

Devicetree variant. Equivalent to `spi_transceive(spec->bus, &spec->config, tx_bufs, rx_bufs)`.

#### spi_read

```c
int spi_read(const struct device *dev,
             const struct spi_config *config,
             const struct spi_buf_set *rx_bufs);
```

Read-only transfer (TX sends zeros or overrun character).

#### spi_read_dt

```c
int spi_read_dt(const struct spi_dt_spec *spec,
                const struct spi_buf_set *rx_bufs);
```

Devicetree variant of `spi_read`.

#### spi_write

```c
int spi_write(const struct device *dev,
              const struct spi_config *config,
              const struct spi_buf_set *tx_bufs);
```

Write-only transfer (RX data discarded).

#### spi_write_dt

```c
int spi_write_dt(const struct spi_dt_spec *spec,
                 const struct spi_buf_set *tx_bufs);
```

Devicetree variant of `spi_write`.

#### spi_release

```c
int spi_release(const struct device *dev,
                const struct spi_config *config);
```

Release locked SPI device. Use after transactions with `SPI_LOCK_ON` or `SPI_HOLD_ON_CS`.

#### spi_release_dt

```c
int spi_release_dt(const struct spi_dt_spec *spec);
```

Devicetree variant of `spi_release`.

---

### Asynchronous API

Requires `CONFIG_SPI_ASYNC=y`. Functions return immediately; completion is signaled via callback or poll signal.

#### spi_transceive_cb

```c
int spi_transceive_cb(const struct device *dev,
                      const struct spi_config *config,
                      const struct spi_buf_set *tx_bufs,
                      const struct spi_buf_set *rx_bufs,
                      spi_callback_t callback,
                      void *userdata);
```

Async transfer with callback notification.

**Callback signature:**
```c
typedef void (*spi_callback_t)(const struct device *dev,
                               int result,
                               void *data);
```

- `result`: 0 on success, -errno on failure
- `data`: userdata passed to transceive_cb

**Returns:**
- `0`: Transfer started successfully
- `-EBUSY`: Bus busy (previous transfer in progress)
- `-errno`: Other failure

#### spi_transceive_signal

```c
int spi_transceive_signal(const struct device *dev,
                          const struct spi_config *config,
                          const struct spi_buf_set *tx_bufs,
                          const struct spi_buf_set *rx_bufs,
                          struct k_poll_signal *sig);
```

Async transfer with k_poll_signal notification. Requires `CONFIG_SPI_ASYNC=y` and `CONFIG_POLL=y`.

**Signal result:** The signal is raised with result code (0 = success, -errno = failure).

#### spi_read_signal / spi_write_signal

```c
int spi_read_signal(const struct device *dev,
                    const struct spi_config *config,
                    const struct spi_buf_set *rx_bufs,
                    struct k_poll_signal *sig);

int spi_write_signal(const struct device *dev,
                     const struct spi_config *config,
                     const struct spi_buf_set *tx_bufs,
                     struct k_poll_signal *sig);
```

Async read/write with signal notification.

---

### Utility Functions

#### spi_is_ready_dt

```c
bool spi_is_ready_dt(const struct spi_dt_spec *spec);
```

Check if SPI bus and CS GPIO (if used) are ready.

**Returns:** `true` if ready, `false` otherwise.

#### spi_cs_is_gpio / spi_cs_is_gpio_dt

```c
bool spi_cs_is_gpio(const struct spi_config *config);
bool spi_cs_is_gpio_dt(const struct spi_dt_spec *spec);
```

Check if CS is controlled via GPIO.

---

### Devicetree Macros

#### SPI_DT_SPEC_GET

```c
#define SPI_DT_SPEC_GET(node_id, operation_, ...)
```

Initialize `struct spi_dt_spec` from devicetree.

**Example:**
```c
static const struct spi_dt_spec my_spi = SPI_DT_SPEC_GET(
    DT_NODELABEL(my_device),
    SPI_OP_MODE_MASTER | SPI_WORD_SET(8)
);
```

#### SPI_DT_SPEC_INST_GET

```c
#define SPI_DT_SPEC_INST_GET(inst, operation_, ...)
```

Same as `SPI_DT_SPEC_GET` but uses `DT_DRV_INST(inst)`.

#### SPI_CONFIG_DT

```c
#define SPI_CONFIG_DT(node_id, operation_, ...)
```

Initialize `struct spi_config` from devicetree (without bus pointer).

#### SPI_CS_CONTROL_INIT

```c
#define SPI_CS_CONTROL_INIT(node_id)
```

Initialize `struct spi_cs_control` from devicetree.

#### SPI_CS_GPIOS_DT_SPEC_GET

```c
#define SPI_CS_GPIOS_DT_SPEC_GET(spi_dev)
```

Get GPIO spec for a SPI device's chip select.

---

### Operation Flags Reference

Combine with bitwise OR for `spi_config.operation`:

| Flag | Value | Description |
|------|-------|-------------|
| `SPI_OP_MODE_MASTER` | 0 | Controller mode (default) |
| `SPI_OP_MODE_SLAVE` | BIT(0) | Peripheral mode |
| `SPI_MODE_CPOL` | BIT(1) | Clock idle high |
| `SPI_MODE_CPHA` | BIT(2) | Sample on second edge |
| `SPI_MODE_LOOP` | BIT(3) | Loopback mode (testing) |
| `SPI_TRANSFER_LSB` | BIT(4) | LSB first |
| `SPI_WORD_SET(n)` | n << 5 | Word size (1-64 bits) |
| `SPI_HOLD_ON_CS` | BIT(12) | Keep CS active after transfer |
| `SPI_LOCK_ON` | BIT(13) | Lock bus for caller |
| `SPI_CS_ACTIVE_HIGH` | BIT(14) | CS active high |
| `SPI_LINES_DUAL` | 1 << 16 | Dual MISO (extended) |
| `SPI_LINES_QUAD` | 2 << 16 | Quad MISO (extended) |
| `SPI_LINES_OCTAL` | 3 << 16 | Octal MISO (extended) |

**Note:** `SPI_LINES_*` require `CONFIG_SPI_EXTENDED_MODES=y`.

## Async

Detailed reference for Zephyr's asynchronous SPI API. Requires `CONFIG_SPI_ASYNC=y`.

### Table of Contents

1. [Overview](#overview)
2. [Callback API](#callback-api)
3. [Signal API](#signal-api)
4. [Buffer Management](#buffer-management)
5. [Complete Examples](#complete-examples)
6. [Common Issues](#common-issues)

---

### Overview

The async SPI API allows non-blocking transfers. The function returns immediately after starting the transfer, and completion is notified via:

1. **Callback** - Function called when transfer completes
2. **Signal** - `k_poll_signal` raised when transfer completes

#### When to Use Async

| Use Case | Recommendation |
|----------|----------------|
| Simple, infrequent transfers | Sync API (simpler) |
| Need to do work during transfer | Async |
| High throughput requirements | Async + DMA driver |
| Multiple concurrent SPI operations | Async with queuing |
| Real-time constraints | Async (predictable latency) |

---

### Callback API

#### spi_transceive_cb

```c
int spi_transceive_cb(const struct device *dev,
                      const struct spi_config *config,
                      const struct spi_buf_set *tx_bufs,
                      const struct spi_buf_set *rx_bufs,
                      spi_callback_t callback,
                      void *userdata);
```

**Callback Signature:**

```c
typedef void (*spi_callback_t)(const struct device *dev,
                               int result,
                               void *data);
```

**Callback Parameters:**
- `dev`: SPI controller device
- `result`: 0 on success, -errno on failure
- `data`: userdata passed to `spi_transceive_cb`

**Important:**
- Callback runs in ISR context (on most drivers)
- Keep callback work minimal
- Do not call blocking functions in callback
- Buffers must remain valid until callback fires

#### Basic Callback Pattern

```c
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>

static struct k_sem transfer_sem;
static int transfer_result;

void spi_done_callback(const struct device *dev, int result, void *userdata)
{
    transfer_result = result;
    k_sem_give(&transfer_sem);
}

int async_transfer_with_callback(const struct spi_dt_spec *spi,
                                  uint8_t *tx, size_t tx_len,
                                  uint8_t *rx, size_t rx_len)
{
    struct spi_buf tx_buf = {.buf = tx, .len = tx_len};
    struct spi_buf rx_buf = {.buf = rx, .len = rx_len};
    struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1};
    struct spi_buf_set rx_set = {.buffers = &rx_buf, .count = 1};

    k_sem_init(&transfer_sem, 0, 1);

    int ret = spi_transceive_cb(spi->bus, &spi->config,
                                 &tx_set, &rx_set,
                                 spi_done_callback, NULL);
    if (ret < 0) {
        return ret;
    }

    /* Wait for completion */
    k_sem_take(&transfer_sem, K_FOREVER);

    return transfer_result;
}
```

#### Callback with User Data

```c
struct transfer_context {
    struct k_sem done;
    int result;
    uint8_t *rx_data;
    size_t rx_len;
};

void spi_callback_with_context(const struct device *dev, int result, void *userdata)
{
    struct transfer_context *ctx = userdata;
    ctx->result = result;
    k_sem_give(&ctx->done);
}

int transfer_with_context(const struct spi_dt_spec *spi, uint8_t *tx, uint8_t *rx, size_t len)
{
    struct transfer_context ctx = {
        .rx_data = rx,
        .rx_len = len,
    };
    k_sem_init(&ctx.done, 0, 1);

    struct spi_buf tx_buf = {.buf = tx, .len = len};
    struct spi_buf rx_buf = {.buf = rx, .len = len};
    struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1};
    struct spi_buf_set rx_set = {.buffers = &rx_buf, .count = 1};

    int ret = spi_transceive_cb(spi->bus, &spi->config,
                                 &tx_set, &rx_set,
                                 spi_callback_with_context, &ctx);
    if (ret < 0) {
        return ret;
    }

    k_sem_take(&ctx.done, K_FOREVER);
    return ctx.result;
}
```

---

### Signal API

Requires `CONFIG_SPI_ASYNC=y` and `CONFIG_POLL=y`.

#### spi_transceive_signal

```c
int spi_transceive_signal(const struct device *dev,
                          const struct spi_config *config,
                          const struct spi_buf_set *tx_bufs,
                          const struct spi_buf_set *rx_bufs,
                          struct k_poll_signal *sig);
```

The signal is raised with the result code when transfer completes.

#### Basic Signal Pattern

```c
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>

int async_transfer_with_signal(const struct spi_dt_spec *spi,
                                uint8_t *tx, size_t tx_len,
                                uint8_t *rx, size_t rx_len)
{
    struct k_poll_signal sig;
    struct k_poll_event evt = K_POLL_EVENT_INITIALIZER(
        K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &sig);

    k_poll_signal_init(&sig);

    struct spi_buf tx_buf = {.buf = tx, .len = tx_len};
    struct spi_buf rx_buf = {.buf = rx, .len = rx_len};
    struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1};
    struct spi_buf_set rx_set = {.buffers = &rx_buf, .count = 1};

    int ret = spi_transceive_signal(spi->bus, &spi->config,
                                     &tx_set, &rx_set, &sig);
    if (ret < 0) {
        return ret;
    }

    /* Wait for completion */
    k_poll(&evt, 1, K_FOREVER);

    /* Get result from signal */
    int result;
    unsigned int signaled;
    k_poll_signal_check(&sig, &signaled, &result);

    return result;
}
```

#### Polling Multiple Signals

```c
struct k_poll_signal spi_sig, other_sig;
struct k_poll_event events[] = {
    K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &spi_sig),
    K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &other_sig),
};

/* Start async SPI transfer */
spi_transceive_signal(..., &spi_sig);

/* Wait for any event */
k_poll(events, ARRAY_SIZE(events), K_FOREVER);

if (events[0].state == K_POLL_STATE_SIGNALED) {
    /* SPI transfer complete */
}
```

---

### Buffer Management

#### Rules

1. **Buffers must remain valid** until transfer completes (callback fires or signal raised)
2. **Do not modify buffers** during transfer
3. **Align buffers** for DMA (often 4-byte or cache-line alignment)
4. **Use static or heap buffers** - stack buffers are risky if function returns before completion

#### DMA Buffer Alignment

```c
/* Ensure proper alignment for DMA */
static uint8_t tx_buf[64] __aligned(4);
static uint8_t rx_buf[64] __aligned(4);

/* Or use cache-line alignment if needed */
static uint8_t tx_buf[64] __aligned(CONFIG_DCACHE_LINE_SIZE);
```

#### Buffer Lifecycle

```c
/* WRONG - buffer may be invalid when callback fires */
void bad_transfer(void)
{
    uint8_t local_buf[16];  /* Stack buffer */
    spi_transceive_cb(..., local_buf, ...);
    return;  /* Function returns, local_buf invalid! */
}

/* CORRECT - buffer persists */
static uint8_t persistent_buf[16];

void good_transfer(void)
{
    spi_transceive_cb(..., persistent_buf, ...);
    /* Wait for completion before returning */
}
```

---

### Complete Examples

#### Non-Blocking Sensor Read

```c
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>

#define SENSOR_DT DT_NODELABEL(my_sensor)
static const struct spi_dt_spec sensor = SPI_DT_SPEC_GET(
    SENSOR_DT, SPI_OP_MODE_MASTER | SPI_WORD_SET(8));

static uint8_t tx_cmd[1] __aligned(4) = {0x80};  /* Read command */
static uint8_t rx_data[8] __aligned(4);
static struct k_sem read_done;

void sensor_read_callback(const struct device *dev, int result, void *userdata)
{
    if (result == 0) {
        /* Process received data */
        int16_t value = (rx_data[1] << 8) | rx_data[2];
        printk("Sensor value: %d\n", value);
    }
    k_sem_give(&read_done);
}

int start_sensor_read(void)
{
    struct spi_buf tx = {.buf = tx_cmd, .len = 1};
    struct spi_buf rx = {.buf = rx_data, .len = sizeof(rx_data)};
    struct spi_buf_set tx_set = {.buffers = &tx, .count = 1};
    struct spi_buf_set rx_set = {.buffers = &rx, .count = 1};

    return spi_transceive_cb(sensor.bus, &sensor.config,
                              &tx_set, &rx_set,
                              sensor_read_callback, NULL);
}

int main(void)
{
    k_sem_init(&read_done, 0, 1);

    if (!spi_is_ready_dt(&sensor)) {
        return -ENODEV;
    }

    while (1) {
        start_sensor_read();

        /* Do other work while transfer runs */
        do_other_stuff();

        /* Wait for read to complete */
        k_sem_take(&read_done, K_FOREVER);

        k_sleep(K_MSEC(100));
    }
}
```

#### Queue-Based Async Transfers

```c
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>

struct spi_transfer_request {
    uint8_t *tx_buf;
    uint8_t *rx_buf;
    size_t len;
    struct k_sem *done;
    int *result;
};

K_MSGQ_DEFINE(transfer_queue, sizeof(struct spi_transfer_request), 8, 4);

static struct spi_transfer_request current_request;
static volatile bool transfer_in_progress;

void spi_queue_callback(const struct device *dev, int result, void *userdata)
{
    struct spi_transfer_request *req = userdata;
    if (req->result) {
        *req->result = result;
    }
    if (req->done) {
        k_sem_give(req->done);
    }
    transfer_in_progress = false;

    /* Start next queued transfer */
    if (k_msgq_get(&transfer_queue, &current_request, K_NO_WAIT) == 0) {
        /* Start next transfer */
        start_transfer(&current_request);
    }
}

int queue_spi_transfer(uint8_t *tx, uint8_t *rx, size_t len,
                        struct k_sem *done, int *result)
{
    struct spi_transfer_request req = {
        .tx_buf = tx, .rx_buf = rx, .len = len,
        .done = done, .result = result
    };

    if (!transfer_in_progress) {
        current_request = req;
        transfer_in_progress = true;
        return start_transfer(&current_request);
    }

    return k_msgq_put(&transfer_queue, &req, K_FOREVER);
}
```

---

### Common Issues

| Problem | Cause | Solution |
|---------|-------|----------|
| Callback never fires | Transfer failed to start | Check return value of transceive_cb |
| Corrupted RX data | Buffer freed/reused too early | Wait for callback before reusing |
| Hard fault in callback | Buffer on stack, function returned | Use static/heap buffers |
| `-EBUSY` | Previous transfer not complete | Wait for callback or use queue |
| Signal never raised | NULL signal passed | Ensure signal pointer is valid |
| Unpredictable behavior | Modifying buffer during transfer | Do not touch buffers until complete |

#### Debugging Tips

1. **Enable SPI logging**: `CONFIG_SPI_LOG_LEVEL_DBG=y`
2. **Check return values**: Always check transceive_cb return
3. **Verify callback fires**: Add printk/logging in callback
4. **Check buffer alignment**: Some DMA controllers require alignment
5. **Use static buffers**: Avoid stack buffers for async operations

## Devicetree

Complete reference for SPI devicetree configuration in Zephyr.

### Table of Contents

1. [Controller Properties](#controller-properties)
2. [Device Properties](#device-properties)
3. [Examples](#examples)
4. [Overlays](#overlays)

---

### Controller Properties

From `dts/bindings/spi/spi-controller.yaml`:

| Property | Type | Required | Description |
|----------|------|----------|-------------|
| `#address-cells` | int | Yes | Always `1` |
| `#size-cells` | int | Yes | Always `0` |
| `cs-gpios` | phandle-array | No | Array of CS GPIO specs |
| `clock-frequency` | int | No | Peripheral clock frequency (Hz) |
| `overrun-character` | int | No | Byte sent when TX exhausted but RX continues |

#### cs-gpios

Array of GPIO specifications for chip select lines. Index corresponds to child node `reg` value.

```dts
&spi0 {
    cs-gpios = <&gpio0 4 GPIO_ACTIVE_LOW>,   /* CS0 */
               <&gpio0 5 GPIO_ACTIVE_LOW>;   /* CS1 */

    device0@0 { reg = <0>; ... };  /* Uses CS0 */
    device1@1 { reg = <1>; ... };  /* Uses CS1 */
};
```

If not defined, controller uses hardware CS or no CS management.

---

### Device Properties

From `dts/bindings/spi/spi-device.yaml`:

| Property | Type | Required | Description |
|----------|------|----------|-------------|
| `reg` | int | Yes | CS index (matches cs-gpios array) |
| `spi-max-frequency` | int | Yes | Maximum clock frequency (Hz) |
| `spi-cpol` | boolean | No | Clock polarity (idle high) |
| `spi-cpha` | boolean | No | Clock phase (sample on 2nd edge) |
| `spi-lsb-first` | boolean | No | LSB first bit order |
| `spi-hold-cs` | boolean | No | Keep CS active between transactions |
| `spi-cs-high` | boolean | No | CS active high (unusual) |
| `duplex` | int | No | Full (0) or half (2048) duplex |
| `frame-format` | int | No | Motorola (0) or TI (32768) |
| `spi-cs-setup-delay-ns` | int | No | CS setup time (ns) |
| `spi-cs-hold-delay-ns` | int | No | CS hold time (ns) |
| `spi-interframe-delay-ns` | int | No | Delay between words (ns) |

#### SPI Modes

| Mode | CPOL | CPHA | Properties |
|------|------|------|------------|
| 0 | 0 | 0 | (none - default) |
| 1 | 0 | 1 | `spi-cpha;` |
| 2 | 1 | 0 | `spi-cpol;` |
| 3 | 1 | 1 | `spi-cpol; spi-cpha;` |

---

### Examples

#### Basic SPI Controller with One Device

```dts
&spi0 {
    status = "okay";
    pinctrl-0 = <&spi0_default>;
    pinctrl-names = "default";
    cs-gpios = <&gpio0 4 GPIO_ACTIVE_LOW>;

    flash0: w25q128@0 {
        compatible = "jedec,spi-nor";
        reg = <0>;
        spi-max-frequency = <40000000>;  /* 40 MHz */
        /* Mode 0 (default) */
    };
};
```

#### Multiple Devices on Same Bus

```dts
&spi1 {
    status = "okay";
    pinctrl-0 = <&spi1_default>;
    pinctrl-names = "default";
    cs-gpios = <&gpio0 10 GPIO_ACTIVE_LOW>,
               <&gpio0 11 GPIO_ACTIVE_LOW>,
               <&gpio0 12 GPIO_ACTIVE_LOW>;

    sensor0: bme280@0 {
        compatible = "bosch,bme280";
        reg = <0>;
        spi-max-frequency = <10000000>;
    };

    display0: ili9341@1 {
        compatible = "ilitek,ili9341";
        reg = <1>;
        spi-max-frequency = <20000000>;
        spi-cpol;  /* Mode 2 */
    };

    adc0: mcp3008@2 {
        compatible = "microchip,mcp3008";
        reg = <2>;
        spi-max-frequency = <1000000>;
    };
};
```

#### Device with Mode 3 and CS Timing

```dts
eeprom0: at25@0 {
    compatible = "atmel,at25";
    reg = <0>;
    spi-max-frequency = <1000000>;
    spi-cpol;
    spi-cpha;
    spi-cs-setup-delay-ns = <100>;
    spi-cs-hold-delay-ns = <100>;
};
```

#### Software Bit-Bang SPI

```dts
spi_bitbang: spi-bitbang {
    compatible = "zephyr,spi-bitbang";
    status = "okay";
    #address-cells = <1>;
    #size-cells = <0>;
    clk-gpios = <&gpio0 1 GPIO_ACTIVE_HIGH>;
    mosi-gpios = <&gpio0 2 GPIO_ACTIVE_HIGH>;
    miso-gpios = <&gpio0 3 GPIO_ACTIVE_HIGH>;
    cs-gpios = <&gpio0 4 GPIO_ACTIVE_LOW>;

    my_device@0 {
        reg = <0>;
        spi-max-frequency = <100000>;
    };
};
```

---

### Overlays

#### Adding a Device to Existing Controller

```dts
/* boards/my_board.overlay */

&spi0 {
    status = "okay";

    my_sensor: sensor@0 {
        compatible = "vendor,sensor";
        reg = <0>;
        spi-max-frequency = <4000000>;
        label = "MY_SENSOR";
    };
};
```

#### Overriding CS GPIO

```dts
&spi0 {
    cs-gpios = <&gpio0 20 GPIO_ACTIVE_LOW>;  /* Change CS pin */
};
```

#### Changing Device Frequency

```dts
&flash0 {
    spi-max-frequency = <80000000>;  /* Increase to 80 MHz */
};
```

#### Enabling Controller

```dts
&spi2 {
    status = "okay";
    pinctrl-0 = <&spi2_default>;
    pinctrl-names = "default";
};
```

---

### Pinctrl Integration

SPI controllers typically require pinctrl configuration:

```dts
&pinctrl {
    spi0_default: spi0_default {
        group1 {
            psels = <NRF_PSEL(SPIM_SCK, 0, 1)>,
                    <NRF_PSEL(SPIM_MOSI, 0, 2)>,
                    <NRF_PSEL(SPIM_MISO, 0, 3)>;
        };
    };
};

&spi0 {
    pinctrl-0 = <&spi0_default>;
    pinctrl-names = "default";
};
```

The exact pinctrl syntax varies by SoC family. Consult board-specific documentation.

---

### Common Binding Files

| File | Purpose |
|------|---------|
| `dts/bindings/spi/spi-controller.yaml` | Base controller binding |
| `dts/bindings/spi/spi-device.yaml` | Base device binding |
| `dts/bindings/spi/zephyr,spi-bitbang.yaml` | Software SPI |
| `dts/bindings/spi/<vendor>,<controller>.yaml` | Vendor-specific controllers |

## Kconfig

Complete reference for SPI-related Kconfig options in Zephyr.

### Core Options

#### CONFIG_SPI

```kconfig
CONFIG_SPI=y
```

Master toggle to enable Serial Peripheral Interface (SPI) bus drivers.

**Dependencies:** None
**Default:** n

---

#### CONFIG_SPI_ASYNC

```kconfig
CONFIG_SPI_ASYNC=y
```

Enable asynchronous API calls (`spi_transceive_cb`, `spi_transceive_signal`).

**Dependencies:** `MULTITHREADING`
**Selects:** `POLL`
**Default:** n

---

#### CONFIG_SPI_SLAVE

```kconfig
CONFIG_SPI_SLAVE=y
```

Enable SPI slave/peripheral mode operations. Support depends on driver and hardware.

**Status:** Experimental
**Default:** n

---

#### CONFIG_SPI_EXTENDED_MODES

```kconfig
CONFIG_SPI_EXTENDED_MODES=y
```

Enable extended operations: dual, quad, and octal line modes (`SPI_LINES_DUAL`, `SPI_LINES_QUAD`, `SPI_LINES_OCTAL`).

**Status:** Experimental
**Default:** n

---

### RTIO Support

#### CONFIG_SPI_RTIO

```kconfig
CONFIG_SPI_RTIO=y
```

Enable RTIO (Real-Time I/O) API for SPI.

**Status:** Experimental
**Selects:** `RTIO`, `RTIO_WORKQ`
**Default:** n

#### CONFIG_SPI_RTIO_FALLBACK_MSGS

```kconfig
CONFIG_SPI_RTIO_FALLBACK_MSGS=4
```

Number of spi_buf structs for RTIO fallback handler when driver doesn't implement native RTIO.

**Dependencies:** `SPI_RTIO`
**Default:** 4

---

### Shell and Debugging

#### CONFIG_SPI_SHELL

```kconfig
CONFIG_SPI_SHELL=y
```

Enable SPI shell for interactive debugging and simple transceive operations.

**Dependencies:** `SHELL`
**Default:** n

**Shell commands:**
- `spi transceive <device> <bytes...>` - Perform SPI transfer
- `spi conf <device> <freq> <mode>` - Configure SPI device

#### CONFIG_SPI_SHELL_MAX_DEVICE_SLOTS

```kconfig
CONFIG_SPI_SHELL_MAX_DEVICE_SLOTS=16
```

Number of device slots in SPI shell. Increase if you see "not enough space" errors.

**Dependencies:** `SPI_SHELL`
**Default:** 16

---

### Statistics

#### CONFIG_SPI_STATS

```kconfig
CONFIG_SPI_STATS=y
```

Enable SPI device statistics (TX bytes, RX bytes, transfer errors).

**Dependencies:** `STATS`
**Default:** n

---

### Initialization

#### CONFIG_SPI_INIT_PRIORITY

```kconfig
CONFIG_SPI_INIT_PRIORITY=50
```

Device driver initialization priority.

**Default:** `KERNEL_INIT_PRIORITY_DEVICE` (typically 50)

---

### Timing

#### CONFIG_SPI_COMPLETION_TIMEOUT_TOLERANCE

```kconfig
CONFIG_SPI_COMPLETION_TIMEOUT_TOLERANCE=200
```

Tolerance in milliseconds for SPI completion timeout logic.

**Default:** 200

---

### Logging

#### CONFIG_SPI_LOG_LEVEL

Set via standard logging configuration:

```kconfig
CONFIG_SPI_LOG_LEVEL_DBG=y   # Debug level
CONFIG_SPI_LOG_LEVEL_INF=y   # Info level
CONFIG_SPI_LOG_LEVEL_WRN=y   # Warning level
CONFIG_SPI_LOG_LEVEL_ERR=y   # Error level
CONFIG_SPI_LOG_LEVEL_OFF=y   # Disable logging
```

**Default:** Inherits from `LOG_DEFAULT_LEVEL`

---

### Common Driver Options

Many SPI controller drivers have their own Kconfig options. Common patterns:

#### DMA Support

```kconfig
CONFIG_SPI_<VENDOR>_DMA=y
```

Enable DMA for SPI transfers (driver-specific).

#### Interrupt vs Polling

Some drivers offer choice between interrupt-driven and polling modes.

---

### Typical prj.conf Examples

#### Basic SPI

```kconfig
CONFIG_SPI=y
CONFIG_GPIO=y  # Usually needed for CS
```

#### SPI with Async

```kconfig
CONFIG_SPI=y
CONFIG_SPI_ASYNC=y
CONFIG_GPIO=y
```

#### SPI with Debugging

```kconfig
CONFIG_SPI=y
CONFIG_GPIO=y
CONFIG_SPI_SHELL=y
CONFIG_SHELL=y
CONFIG_SPI_LOG_LEVEL_DBG=y
```

#### SPI with DMA (STM32 example)

```kconfig
CONFIG_SPI=y
CONFIG_SPI_STM32_DMA=y
CONFIG_DMA=y
```

#### SPI Slave Mode

```kconfig
CONFIG_SPI=y
CONFIG_SPI_SLAVE=y
```

---

### Driver-Specific Kconfig Files

Each SPI controller has its own Kconfig file under `drivers/spi/`:

| File | Controller |
|------|------------|
| `Kconfig.stm32` | STM32 SPI |
| `Kconfig.nrfx` | Nordic nRF |
| `Kconfig.esp32` | ESP32 |
| `Kconfig.sam` | Atmel SAM |
| `Kconfig.mcux_*` | NXP MCUXpresso |
| `Kconfig.bitbang` | Software SPI |
| `Kconfig.pl022` | ARM PL022 |

Consult driver-specific files for advanced options like DMA channels, interrupt priorities, etc.
