# UART

## Overview

Expert guidance for Zephyr's UART driver subsystem covering three API modes: polling, interrupt-driven, and asynchronous (DMA).

### Table of Contents

1. [API Selection](#api-selection)
2. [Getting Device Reference](#getting-device-reference)
3. [Common Workflows](#common-workflows)
4. [Configuration](#configuration)
5. [Error Handling](#error-handling)
6. [Troubleshooting](#troubleshooting)

---

### API Selection

Zephyr provides three UART access methods. Choose based on requirements:

| API | Kconfig | Use Case | Blocking? |
|-----|---------|----------|-----------|
| **Polling** | (default) | Simple, low-throughput, debug output | Yes (TX), No (RX) |
| **Interrupt-driven** | `CONFIG_UART_INTERRUPT_DRIVEN` | Background RX, medium throughput | No |
| **Async (DMA)** | `CONFIG_UART_ASYNC_API` | High throughput, zero-copy, low CPU | No |

#### Decision Tree

```
Need simple debug output? → Polling
Need background receive? → Interrupt-driven
Need high throughput + low CPU? → Async (DMA)
Need both RX and TX without blocking? → Interrupt-driven or Async
```

#### API Mutual Exclusivity

**WARNING**: Interrupt-driven and Async APIs must NOT be used simultaneously on the same peripheral. Both require hardware interrupts. `CONFIG_UART_EXCLUSIVE_API_CALLBACKS=y` (default) ensures only one callback type is active.

---

### Getting Device Reference

#### From Devicetree (Preferred)

```c
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>

/* Using chosen node (common for console) */
#define UART_NODE DT_CHOSEN(zephyr_shell_uart)
static const struct device *const uart_dev = DEVICE_DT_GET(UART_NODE);

/* Using specific node label */
#define UART_NODE DT_NODELABEL(uart0)
static const struct device *const uart_dev = DEVICE_DT_GET(UART_NODE);

/* Runtime check (in main or init) */
if (!device_is_ready(uart_dev)) {
    printk("UART device not ready\n");
    return -ENODEV;
}
```

---

### Common Workflows

#### 1. Polling API (Simplest)

Best for debug output or simple blocking TX.

```c
#include <zephyr/drivers/uart.h>

/* TX: Blocking - waits until character sent */
void print_uart(const char *str)
{
    while (*str) {
        uart_poll_out(uart_dev, *str++);
    }
}

/* RX: Non-blocking - returns -1 if no data */
int read_char(void)
{
    unsigned char c;
    if (uart_poll_in(uart_dev, &c) == 0) {
        return c;
    }
    return -1;
}
```

- **Full API details**: See [#polling](#polling)

#### 2. Interrupt-Driven API

Best for background receive with message queues.

```c
#include <zephyr/drivers/uart.h>

K_MSGQ_DEFINE(uart_msgq, 32, 10, 4);
static char rx_buf[32];
static int rx_pos;

void uart_isr(const struct device *dev, void *user_data)
{
    if (!uart_irq_update(dev)) return;
    if (!uart_irq_rx_ready(dev)) return;

    uint8_t c;
    while (uart_fifo_read(dev, &c, 1) == 1) {
        if (c == '\n' || c == '\r') {
            rx_buf[rx_pos] = '\0';
            k_msgq_put(&uart_msgq, rx_buf, K_NO_WAIT);
            rx_pos = 0;
        } else if (rx_pos < sizeof(rx_buf) - 1) {
            rx_buf[rx_pos++] = c;
        }
    }
}

int setup_uart_irq(void)
{
    int ret = uart_irq_callback_user_data_set(uart_dev, uart_isr, NULL);
    if (ret < 0) return ret;
    uart_irq_rx_enable(uart_dev);
    return 0;
}
```

- **Full ISR patterns & TX handling**: See [#interrupt](#interrupt)

#### 3. Async API (DMA)

Best for high throughput with minimal CPU usage.

```c
#include <zephyr/drivers/uart.h>

uint8_t rx_buf[2][64];
volatile int rx_buf_idx;

void uart_async_cb(const struct device *dev, struct uart_event *evt, void *data)
{
    switch (evt->type) {
    case UART_TX_DONE:
        /* TX complete - buffer can be reused */
        break;
    case UART_RX_RDY:
        /* Data available: evt->data.rx.buf + evt->data.rx.offset, len: evt->data.rx.len */
        process_data(evt->data.rx.buf + evt->data.rx.offset, evt->data.rx.len);
        break;
    case UART_RX_BUF_REQUEST:
        /* Provide next buffer for continuous reception */
        uart_rx_buf_rsp(dev, rx_buf[rx_buf_idx], sizeof(rx_buf[0]));
        rx_buf_idx = rx_buf_idx ? 0 : 1;
        break;
    case UART_RX_DISABLED:
        /* RX stopped - can re-enable */
        break;
    default:
        break;
    }
}

int setup_uart_async(void)
{
    uart_callback_set(uart_dev, uart_async_cb, NULL);
    rx_buf_idx = 1;
    return uart_rx_enable(uart_dev, rx_buf[0], sizeof(rx_buf[0]), 100 /* timeout_us */);
}
```

- **Full event handling & buffer management**: See [#async](#async)

---

### Configuration

#### Kconfig Essentials

```kconfig
CONFIG_SERIAL=y                    # Enable serial driver subsystem
CONFIG_UART_INTERRUPT_DRIVEN=y     # Enable interrupt API
CONFIG_UART_ASYNC_API=y            # Enable async/DMA API
CONFIG_UART_USE_RUNTIME_CONFIGURE=y # Runtime baud/parity changes
CONFIG_UART_LINE_CTRL=y            # Line control (RTS/CTS/DTR)
```

- **Full Kconfig reference**: See [#kconfig](#kconfig)

#### Devicetree Essentials

```dts
&uart0 {
    status = "okay";
    current-speed = <115200>;
    hw-flow-control;  /* Enable RTS/CTS */
    pinctrl-0 = <&uart0_default>;
    pinctrl-names = "default";
};
```

- **Full devicetree reference**: See [#devicetree](#devicetree)

#### Runtime Configuration

```c
struct uart_config cfg = {
    .baudrate = 115200,
    .parity = UART_CFG_PARITY_NONE,
    .stop_bits = UART_CFG_STOP_BITS_1,
    .data_bits = UART_CFG_DATA_BITS_8,
    .flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
};
uart_configure(uart_dev, &cfg);
```

---

### Error Handling

#### Error Types

| Error | Value | Cause |
|-------|-------|-------|
| `UART_ERROR_OVERRUN` | 0x01 | Data lost - not read fast enough |
| `UART_ERROR_PARITY` | 0x02 | Parity mismatch |
| `UART_ERROR_FRAMING` | 0x04 | Stop bit not detected |
| `UART_BREAK` | 0x08 | Break condition (line held low) |
| `UART_ERROR_COLLISION` | 0x10 | RS-485 TX/RX collision |
| `UART_ERROR_NOISE` | 0x20 | Noise on line |

#### Checking Errors

```c
int err = uart_err_check(uart_dev);
if (err & UART_ERROR_OVERRUN) {
    /* Handle overrun */
}
```

---

### Troubleshooting

#### Quick Reference

| Issue | Likely Cause | Fix |
|-------|--------------|-----|
| No data received | Wrong baud rate | Verify `current-speed` in DTS matches sender |
| Garbled data | Baud rate mismatch | Check both ends use same baud rate |
| TX hangs | Flow control mismatch | Disable `hw-flow-control` or connect CTS |
| RX callback not called | IRQ not enabled | Call `uart_irq_rx_enable()` |
| `-ENOTSUP` from API | API not enabled | Enable `CONFIG_UART_INTERRUPT_DRIVEN` or `CONFIG_UART_ASYNC_API` |
| `-EBUSY` on rx_enable | RX already active | Call `uart_rx_disable()` first |
| Overrun errors | Not reading fast enough | Increase buffer size, use async API |

#### Debug Checklist

1. **Device ready?** Check `device_is_ready(uart_dev)`
2. **Pins correct?** Verify pinctrl in devicetree
3. **Baud rate match?** Both ends must match
4. **Flow control?** If enabled, CTS must be asserted for TX
5. **API enabled?** Check Kconfig for required API

---

### References

- [#polling](#polling) — Polling API functions and patterns
- [#interrupt](#interrupt) — Interrupt-driven API, ISR patterns, TX handling
- [#async](#async) — Async/DMA API, event handling, buffer management
- [#kconfig](#kconfig) — All UART Kconfig options
- [#devicetree](#devicetree) — Devicetree properties and examples

### Source Locations

Key files in Zephyr source tree:
- `include/zephyr/drivers/uart.h` — Public API header
- `drivers/serial/` — Driver implementations
- `dts/bindings/serial/uart-controller.yaml` — Base DTS binding
- `samples/drivers/uart/` — Sample applications

## Async

The asynchronous API enables high-throughput UART with DMA, minimal CPU overhead, and zero-copy operation. Requires `CONFIG_UART_ASYNC_API=y`.

### Table of Contents

1. [Event Model](#event-model)
2. [API Functions](#api-functions)
3. [RX Flow](#rx-flow)
4. [TX Flow](#tx-flow)
5. [Buffer Management](#buffer-management)
6. [Complete Examples](#complete-examples)

---

### Event Model

The async API is event-driven. Register a callback to receive `struct uart_event`:

```c
typedef void (*uart_callback_t)(const struct device *dev,
                                struct uart_event *evt,
                                void *user_data);
```

#### Event Types

| Event | Trigger | Action Required |
|-------|---------|-----------------|
| `UART_TX_DONE` | TX buffer fully sent | Free/reuse TX buffer |
| `UART_TX_ABORTED` | TX aborted (timeout/manual) | Handle partial send |
| `UART_RX_RDY` | Data received in buffer | Process received data |
| `UART_RX_BUF_REQUEST` | Need next RX buffer | Provide buffer via `uart_rx_buf_rsp()` |
| `UART_RX_BUF_RELEASED` | Buffer no longer used | Buffer safe to reuse |
| `UART_RX_DISABLED` | RX stopped | Can call `uart_rx_enable()` again |
| `UART_RX_STOPPED` | RX stopped due to error | Check error, re-enable if needed |

#### Event Data Structures

```c
struct uart_event {
    enum uart_event_type type;
    union uart_event_data {
        struct uart_event_tx tx;        /* TX_DONE, TX_ABORTED */
        struct uart_event_rx rx;        /* RX_RDY */
        struct uart_event_rx_buf rx_buf; /* RX_BUF_RELEASED */
        struct uart_event_rx_stop rx_stop; /* RX_STOPPED */
    } data;
};

struct uart_event_rx {
    uint8_t *buf;    /* Pointer to buffer */
    size_t offset;   /* Offset of new data in buffer */
    size_t len;      /* Length of new data */
};
```

**Data location**: `evt->data.rx.buf + evt->data.rx.offset` for `len` bytes.

---

### API Functions

#### Setup

```c
int uart_callback_set(const struct device *dev,
                      uart_callback_t callback,
                      void *user_data);
```

**Returns**: `0` on success, `-ENOSYS` if not implemented, `-ENOTSUP` if API not enabled.

#### Transmit

```c
int uart_tx(const struct device *dev, const uint8_t *buf,
            size_t len, int32_t timeout);
```

- **timeout**: Microseconds. `SYS_FOREVER_US` for no timeout. Only meaningful with flow control.
- **Returns**: `0` on success, `-EBUSY` if TX already in progress.

```c
int uart_tx_abort(const struct device *dev);
```

Aborts current TX. Generates `UART_TX_ABORTED` with bytes sent.

#### Receive

```c
int uart_rx_enable(const struct device *dev, uint8_t *buf,
                   size_t len, int32_t timeout);
```

- **buf**: First receive buffer
- **timeout**: Inactivity timeout in microseconds. Triggers `UART_RX_RDY` if no data received for this period.
- **Returns**: `0` on success, `-EBUSY` if RX already active.

```c
int uart_rx_buf_rsp(const struct device *dev, uint8_t *buf, size_t len);
```

Provide next buffer in response to `UART_RX_BUF_REQUEST`.

- **Returns**: `0` on success, `-EBUSY` if buffer already provided, `-EACCES` if RX disabled.

```c
int uart_rx_disable(const struct device *dev);
```

Stop receiving. Generates `UART_RX_RDY` (if data pending), `UART_RX_BUF_RELEASED` for each buffer, then `UART_RX_DISABLED`.

---

### RX Flow

#### Event Sequence

```
uart_rx_enable(buf1)
        │
        ▼
UART_RX_BUF_REQUEST ────► uart_rx_buf_rsp(buf2)
        │
        ▼
    [Receiving into buf1]
        │
        ▼
UART_RX_RDY (buf1, partial)  ← Timeout or buffer full
        │
        ▼
UART_RX_BUF_RELEASED (buf1)
        │
        ▼
    [Receiving into buf2]
        │
        ▼
UART_RX_BUF_REQUEST ────► uart_rx_buf_rsp(buf1)
        │
        ▼
    ... continues ...
```

#### Double Buffering Pattern

```c
#define RX_BUF_SIZE 64
uint8_t rx_bufs[2][RX_BUF_SIZE];
volatile int next_buf_idx = 1;

void uart_cb(const struct device *dev, struct uart_event *evt, void *data)
{
    switch (evt->type) {
    case UART_RX_RDY:
        /* Process: evt->data.rx.buf + evt->data.rx.offset, len: evt->data.rx.len */
        process_data(evt->data.rx.buf + evt->data.rx.offset, evt->data.rx.len);
        break;

    case UART_RX_BUF_REQUEST:
        uart_rx_buf_rsp(dev, rx_bufs[next_buf_idx], RX_BUF_SIZE);
        next_buf_idx = next_buf_idx ? 0 : 1;
        break;

    case UART_RX_BUF_RELEASED:
        /* Buffer evt->data.rx_buf.buf is now safe to reuse */
        break;

    case UART_RX_DISABLED:
        /* RX stopped, can re-enable */
        break;

    case UART_RX_STOPPED:
        /* Error occurred: evt->data.rx_stop.reason */
        LOG_ERR("RX stopped: reason=%d", evt->data.rx_stop.reason);
        break;
    }
}

int start_rx(const struct device *dev)
{
    uart_callback_set(dev, uart_cb, NULL);
    next_buf_idx = 1;
    return uart_rx_enable(dev, rx_bufs[0], RX_BUF_SIZE, 100 /* 100us timeout */);
}
```

---

### TX Flow

#### Event Sequence

```
uart_tx(buf, len, timeout)
        │
        ▼
    [Transmitting]
        │
        ├──► UART_TX_DONE (success)
        │         └── buf can be freed/reused
        │
        └──► UART_TX_ABORTED (timeout or uart_tx_abort)
                  └── Check evt->data.tx.len for bytes sent
```

#### TX Pattern with Buffer Pool

```c
#include <zephyr/net_buf.h>

NET_BUF_POOL_DEFINE(tx_pool, 8, 64, 0, NULL);

struct k_fifo tx_queue;
struct net_buf *tx_pending;

void uart_cb(const struct device *dev, struct uart_event *evt, void *data)
{
    struct net_buf *buf;

    switch (evt->type) {
    case UART_TX_DONE:
        /* Free completed buffer */
        net_buf_unref(tx_pending);
        tx_pending = NULL;

        /* Send next queued buffer */
        buf = k_fifo_get(&tx_queue, K_NO_WAIT);
        if (buf) {
            if (uart_tx(dev, buf->data, buf->len, SYS_FOREVER_US) == 0) {
                tx_pending = buf;
            } else {
                net_buf_unref(buf);
            }
        }
        break;

    case UART_TX_ABORTED:
        LOG_WRN("TX aborted, sent %d bytes", evt->data.tx.len);
        net_buf_unref(tx_pending);
        tx_pending = NULL;
        break;
    }
}

int send_async(const struct device *dev, const uint8_t *data, size_t len)
{
    struct net_buf *buf = net_buf_alloc(&tx_pool, K_NO_WAIT);
    if (!buf) return -ENOMEM;

    memcpy(net_buf_add(buf, len), data, len);

    if (tx_pending == NULL) {
        if (uart_tx(dev, buf->data, buf->len, SYS_FOREVER_US) == 0) {
            tx_pending = buf;
            return 0;
        }
        net_buf_unref(buf);
        return -EIO;
    }

    /* Queue for later */
    k_fifo_put(&tx_queue, buf);
    return 0;
}
```

---

### Buffer Management

#### Rules

1. **Never reuse a buffer until released** - Wait for `UART_RX_BUF_RELEASED`
2. **Provide next buffer promptly** - On `UART_RX_BUF_REQUEST`, provide buffer immediately
3. **Handle partial fills** - `UART_RX_RDY` can fire multiple times per buffer
4. **Offset matters** - Data is at `buf + offset`, not `buf`

#### Memory Considerations

| Buffer Count | Behavior |
|--------------|----------|
| 1 buffer | RX stops when buffer full, gaps possible |
| 2 buffers | Seamless switching, recommended minimum |
| 3+ buffers | Extra margin for slow processing |

#### Buffer Size Considerations

- **Too small**: Frequent `UART_RX_RDY` events, overhead
- **Too large**: Memory waste, high latency before data available
- **Recommended**: Match typical message size or ~64-256 bytes

---

### Complete Examples

#### Continuous RX with Processing

```c
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>

#define UART_DEV DEVICE_DT_GET(DT_NODELABEL(uart0))
#define BUF_SIZE 128

static uint8_t rx_buf[2][BUF_SIZE];
static int buf_idx;

K_MSGQ_DEFINE(rx_queue, sizeof(struct rx_data), 16, 4);

struct rx_data {
    uint8_t *buf;
    size_t offset;
    size_t len;
};

void uart_cb(const struct device *dev, struct uart_event *evt, void *data)
{
    struct rx_data rx;

    switch (evt->type) {
    case UART_RX_RDY:
        rx.buf = evt->data.rx.buf;
        rx.offset = evt->data.rx.offset;
        rx.len = evt->data.rx.len;
        k_msgq_put(&rx_queue, &rx, K_NO_WAIT);
        break;

    case UART_RX_BUF_REQUEST:
        uart_rx_buf_rsp(dev, rx_buf[buf_idx], BUF_SIZE);
        buf_idx = buf_idx ? 0 : 1;
        break;

    case UART_RX_DISABLED:
    case UART_RX_BUF_RELEASED:
        break;

    case UART_RX_STOPPED:
        LOG_ERR("RX error: %d", evt->data.rx_stop.reason);
        /* Re-enable RX */
        k_work_submit(&rx_restart_work);
        break;
    }
}

int main(void)
{
    const struct device *dev = UART_DEV;
    struct rx_data rx;

    uart_callback_set(dev, uart_cb, NULL);
    buf_idx = 1;
    uart_rx_enable(dev, rx_buf[0], BUF_SIZE, 1000); /* 1ms timeout */

    while (1) {
        if (k_msgq_get(&rx_queue, &rx, K_FOREVER) == 0) {
            process_data(rx.buf + rx.offset, rx.len);
        }
    }
}
```

---

### Common Issues

| Problem | Cause | Solution |
|---------|-------|----------|
| RX stops unexpectedly | No buffer provided | Always respond to `UART_RX_BUF_REQUEST` |
| Data gaps | Single buffer | Use double buffering |
| `-EBUSY` on `uart_rx_enable` | RX already active | Call `uart_rx_disable()` first, wait for `UART_RX_DISABLED` |
| `-EBUSY` on `uart_tx` | TX already in progress | Queue data, send on `UART_TX_DONE` |
| Buffer overwritten | Reused before released | Wait for `UART_RX_BUF_RELEASED` |
| No events | Callback not set | Call `uart_callback_set()` first |
| `-ENOTSUP` | API not enabled | Enable `CONFIG_UART_ASYNC_API` |

### Performance Tips

1. **Use DMA-capable buffers** - Some MCUs require specific memory regions
2. **Align buffers** - 4-byte alignment often required for DMA
3. **Minimize callback work** - Defer processing to thread context
4. **Size buffers for throughput** - Larger buffers = fewer interrupts

## Devicetree

Devicetree configuration for UART peripherals in Zephyr.

### Table of Contents

1. [Base Properties](#base-properties)
2. [Common Patterns](#common-patterns)
3. [Pin Control](#pin-control)
4. [Overlay Examples](#overlay-examples)
5. [Accessing from C](#accessing-from-c)

---

### Base Properties

Properties from `uart-controller.yaml` binding:

| Property | Type | Required | Description |
|----------|------|----------|-------------|
| `status` | string | No | "okay" to enable, "disabled" to disable |
| `current-speed` | int | No | Initial baud rate in bps |
| `hw-flow-control` | boolean | No | Enable RTS/CTS hardware flow control |
| `parity` | string | No | "none", "odd", "even", "mark", "space" |
| `stop-bits` | string | No | "0_5", "1", "1_5", "2" |
| `data-bits` | int | No | 5, 6, 7, 8, or 9 |
| `clock-frequency` | int | No | Clock frequency for UART operation |

#### Default Values

If not specified:
- `current-speed`: Driver/SoC default (often 115200)
- `parity`: "none"
- `stop-bits`: "1"
- `data-bits`: 8
- `hw-flow-control`: disabled

---

### Common Patterns

#### Basic UART Enable

```dts
&uart0 {
    status = "okay";
    current-speed = <115200>;
};
```

#### With Hardware Flow Control

```dts
&uart0 {
    status = "okay";
    current-speed = <115200>;
    hw-flow-control;
};
```

#### Full Configuration

```dts
&uart0 {
    status = "okay";
    current-speed = <9600>;
    parity = "even";
    stop-bits = "2";
    data-bits = <8>;
};
```

#### Disable a UART

```dts
&uart1 {
    status = "disabled";
};
```

---

### Pin Control

Most UARTs require pin control configuration. This is SoC-specific.

#### Nordic nRF

```dts
&pinctrl {
    uart0_default: uart0_default {
        group1 {
            psels = <NRF_PSEL(UART_TX, 0, 6)>,
                    <NRF_PSEL(UART_RX, 0, 8)>;
        };
    };

    uart0_sleep: uart0_sleep {
        group1 {
            psels = <NRF_PSEL(UART_TX, 0, 6)>,
                    <NRF_PSEL(UART_RX, 0, 8)>;
            low-power-enable;
        };
    };
};

&uart0 {
    status = "okay";
    current-speed = <115200>;
    pinctrl-0 = <&uart0_default>;
    pinctrl-1 = <&uart0_sleep>;
    pinctrl-names = "default", "sleep";
};
```

#### STM32

```dts
&pinctrl {
    usart2_tx_pa2: usart2_tx_pa2 {
        pinmux = <STM32_PINMUX('A', 2, AF7)>;
    };

    usart2_rx_pa3: usart2_rx_pa3 {
        pinmux = <STM32_PINMUX('A', 3, AF7)>;
    };
};

&usart2 {
    status = "okay";
    current-speed = <115200>;
    pinctrl-0 = <&usart2_tx_pa2 &usart2_rx_pa3>;
    pinctrl-names = "default";
};
```

#### ESP32

```dts
&pinctrl {
    uart0_default: uart0_default {
        group1 {
            pinmux = <UART0_TX_GPIO1>;
            output-high;
        };
        group2 {
            pinmux = <UART0_RX_GPIO3>;
            bias-pull-up;
        };
    };
};

&uart0 {
    status = "okay";
    current-speed = <115200>;
    pinctrl-0 = <&uart0_default>;
    pinctrl-names = "default";
};
```

---

### Overlay Examples

#### Board Overlay (app/boards/<board>.overlay)

Modify UART for specific application:

```dts
/* Change baud rate for GPS module */
&uart1 {
    status = "okay";
    current-speed = <9600>;
};
```

#### Adding a Second UART

```dts
/* Enable UART2 for debug */
&uart2 {
    status = "okay";
    current-speed = <115200>;
    pinctrl-0 = <&uart2_default>;
    pinctrl-names = "default";
};
```

#### Console/Shell UART Selection

Use `chosen` node to select console UART:

```dts
/ {
    chosen {
        zephyr,console = &uart0;
        zephyr,shell-uart = &uart0;
    };
};
```

#### UART for Custom Device

```dts
/ {
    gps_device: gps {
        compatible = "vendor,gps-module";
        uart = <&uart1>;
    };
};

&uart1 {
    status = "okay";
    current-speed = <9600>;
};
```

---

### Accessing from C

#### Get Device from Chosen Node

```c
/* For console/shell UART */
#define UART_NODE DT_CHOSEN(zephyr_shell_uart)
static const struct device *const uart = DEVICE_DT_GET(UART_NODE);
```

#### Get Device from Node Label

```c
#define UART_NODE DT_NODELABEL(uart0)
static const struct device *const uart = DEVICE_DT_GET(UART_NODE);
```

#### Get Device from Alias

```dts
/ {
    aliases {
        debug-uart = &uart1;
    };
};
```

```c
#define UART_NODE DT_ALIAS(debug_uart)
static const struct device *const uart = DEVICE_DT_GET(UART_NODE);
```

#### Check Node Exists

```c
#if DT_NODE_EXISTS(DT_NODELABEL(uart2))
    /* UART2 is defined */
#endif
```

#### Get Properties

```c
/* Get configured baud rate from DTS */
#define UART_BAUD DT_PROP(DT_NODELABEL(uart0), current_speed)

/* Check if flow control enabled */
#if DT_PROP(DT_NODELABEL(uart0), hw_flow_control)
    /* HW flow control enabled */
#endif
```

---

### Vendor-Specific Properties

Some SoCs have additional properties. Check vendor bindings:

#### Nordic nRF

| Property | Description |
|----------|-------------|
| `rx-pin-pull-up` | Enable internal pull-up on RX |
| `disable-rx` | Disable RX pin (TX only) |

#### STM32

| Property | Description |
|----------|-------------|
| `tx-invert` | Invert TX signal |
| `rx-invert` | Invert RX signal |
| `single-wire` | Half-duplex single-wire mode |

#### Location of Bindings

- `dts/bindings/serial/uart-controller.yaml` - Base binding
- `dts/bindings/serial/<vendor>*.yaml` - Vendor-specific bindings

---

### Troubleshooting

| Issue | Cause | Solution |
|-------|-------|----------|
| UART not working | `status` not "okay" | Add `status = "okay";` |
| Wrong baud rate | Not specified in DTS | Add `current-speed = <115200>;` |
| No data | Pins not configured | Add pinctrl configuration |
| Garbled output | Baud mismatch | Verify `current-speed` matches receiver |
| Device not found | Wrong node reference | Check `DT_NODELABEL` vs actual node name |
| Build error "node not found" | UART not enabled in SoC DTS | Check SoC dtsi file |

---

### Common DTS Macros

| Macro | Usage |
|-------|-------|
| `DT_NODELABEL(label)` | Get node by label (e.g., `uart0`) |
| `DT_CHOSEN(prop)` | Get chosen node (e.g., `zephyr_shell_uart`) |
| `DT_ALIAS(alias)` | Get node by alias |
| `DEVICE_DT_GET(node)` | Get device pointer from node |
| `DT_PROP(node, prop)` | Get property value |
| `DT_NODE_EXISTS(node)` | Check if node exists |
| `DT_NODE_HAS_STATUS(node, okay)` | Check if node is enabled |

## Interrupt

The interrupt-driven API enables background UART operations without blocking the main thread. Requires `CONFIG_UART_INTERRUPT_DRIVEN=y`.

### Table of Contents

1. [API Functions](#api-functions)
2. [ISR Pattern](#isr-pattern)
3. [RX Patterns](#rx-patterns)
4. [TX Patterns](#tx-patterns)
5. [Complete Examples](#complete-examples)

---

### API Functions

#### Callback Setup

```c
int uart_irq_callback_user_data_set(const struct device *dev,
                                     uart_irq_callback_user_data_t cb,
                                     void *user_data);
```

**Callback signature**:
```c
typedef void (*uart_irq_callback_user_data_t)(const struct device *dev, void *user_data);
```

#### Interrupt Control

| Function | Purpose |
|----------|---------|
| `uart_irq_rx_enable(dev)` | Enable RX interrupt |
| `uart_irq_rx_disable(dev)` | Disable RX interrupt |
| `uart_irq_tx_enable(dev)` | Enable TX interrupt |
| `uart_irq_tx_disable(dev)` | Disable TX interrupt |
| `uart_irq_err_enable(dev)` | Enable error interrupt |
| `uart_irq_err_disable(dev)` | Disable error interrupt |

#### ISR Helper Functions

| Function | Purpose | Returns |
|----------|---------|---------|
| `uart_irq_update(dev)` | Refresh IRQ status cache | 1 on success |
| `uart_irq_rx_ready(dev)` | Check if RX data available | 1 if data ready |
| `uart_irq_tx_ready(dev)` | Check if TX FIFO can accept data | >0 if ready |
| `uart_irq_tx_complete(dev)` | Check if TX fully done | 1 if idle |
| `uart_irq_is_pending(dev)` | Check for any pending IRQ | 1 if pending |

#### FIFO Operations

```c
int uart_fifo_read(const struct device *dev, uint8_t *rx_data, const int size);
int uart_fifo_fill(const struct device *dev, const uint8_t *tx_data, int size);
```

**Returns**: Number of bytes read/written, or negative errno.

**Critical**: Must drain FIFO completely when `uart_irq_rx_ready()` returns true.

---

### ISR Pattern

#### Standard ISR Structure

```c
void uart_isr_handler(const struct device *dev, void *user_data)
{
    /* Step 1: Update IRQ status (REQUIRED first) */
    if (!uart_irq_update(dev)) {
        return;
    }

    /* Step 2: Handle RX if data available */
    if (uart_irq_rx_ready(dev)) {
        uint8_t buf[64];
        int len;
        /* Drain entire FIFO */
        while ((len = uart_fifo_read(dev, buf, sizeof(buf))) > 0) {
            process_rx_data(buf, len);
        }
    }

    /* Step 3: Handle TX if FIFO ready */
    if (uart_irq_tx_ready(dev)) {
        /* Fill FIFO with pending TX data */
        int sent = uart_fifo_fill(dev, tx_buf, tx_len);
        if (sent > 0) {
            tx_buf += sent;
            tx_len -= sent;
        }
        if (tx_len == 0) {
            uart_irq_tx_disable(dev);  /* No more data to send */
        }
    }
}
```

#### Key Rules

1. **Always call `uart_irq_update()` first** - required before checking rx/tx ready
2. **Drain RX FIFO completely** - some hardware auto-clears IRQ only when FIFO empty
3. **Disable TX IRQ when done** - TX IRQ fires continuously when FIFO empty
4. **Keep ISR short** - defer processing to thread context

---

### RX Patterns

#### Line-Based with Message Queue

```c
#include <zephyr/drivers/uart.h>

#define MSG_SIZE 64
K_MSGQ_DEFINE(uart_msgq, MSG_SIZE, 10, 4);

static char rx_line[MSG_SIZE];
static int rx_pos;

void uart_rx_isr(const struct device *dev, void *user_data)
{
    if (!uart_irq_update(dev)) return;
    if (!uart_irq_rx_ready(dev)) return;

    uint8_t c;
    while (uart_fifo_read(dev, &c, 1) == 1) {
        if (c == '\n' || c == '\r') {
            if (rx_pos > 0) {
                rx_line[rx_pos] = '\0';
                k_msgq_put(&uart_msgq, rx_line, K_NO_WAIT);
                rx_pos = 0;
            }
        } else if (rx_pos < MSG_SIZE - 1) {
            rx_line[rx_pos++] = c;
        }
    }
}

/* Thread processing */
void process_lines(void)
{
    char buf[MSG_SIZE];
    while (k_msgq_get(&uart_msgq, buf, K_FOREVER) == 0) {
        handle_command(buf);
    }
}
```

#### Ring Buffer Pattern

```c
#include <zephyr/sys/ring_buffer.h>

RING_BUF_DECLARE(uart_rx_ring, 256);

void uart_rx_isr(const struct device *dev, void *user_data)
{
    if (!uart_irq_update(dev)) return;
    if (!uart_irq_rx_ready(dev)) return;

    uint8_t buf[32];
    int len;
    while ((len = uart_fifo_read(dev, buf, sizeof(buf))) > 0) {
        ring_buf_put(&uart_rx_ring, buf, len);
    }
    k_sem_give(&rx_sem);  /* Signal waiting thread */
}

/* Thread context */
int read_from_uart(uint8_t *buf, size_t len, k_timeout_t timeout)
{
    if (k_sem_take(&rx_sem, timeout) != 0) {
        return -ETIMEDOUT;
    }
    return ring_buf_get(&uart_rx_ring, buf, len);
}
```

---

### TX Patterns

#### Non-Blocking TX with Ring Buffer

```c
RING_BUF_DECLARE(uart_tx_ring, 256);
static volatile bool tx_active;

void uart_tx_isr(const struct device *dev)
{
    uint8_t buf[16];
    int len = ring_buf_get(&uart_tx_ring, buf, sizeof(buf));

    if (len > 0) {
        uart_fifo_fill(dev, buf, len);
    } else {
        uart_irq_tx_disable(dev);
        tx_active = false;
    }
}

int uart_send(const struct device *dev, const uint8_t *data, size_t len)
{
    int written = ring_buf_put(&uart_tx_ring, data, len);

    if (!tx_active) {
        tx_active = true;
        uart_irq_tx_enable(dev);
    }
    return written;
}
```

#### Blocking TX (with ISR assist)

```c
K_SEM_DEFINE(tx_done_sem, 0, 1);
static const uint8_t *tx_ptr;
static size_t tx_remaining;

void uart_tx_isr(const struct device *dev)
{
    if (tx_remaining > 0) {
        int sent = uart_fifo_fill(dev, tx_ptr, tx_remaining);
        tx_ptr += sent;
        tx_remaining -= sent;
    }

    if (tx_remaining == 0) {
        uart_irq_tx_disable(dev);
        k_sem_give(&tx_done_sem);
    }
}

int uart_send_blocking(const struct device *dev, const uint8_t *data, size_t len)
{
    tx_ptr = data;
    tx_remaining = len;
    uart_irq_tx_enable(dev);
    return k_sem_take(&tx_done_sem, K_FOREVER);
}
```

---

### Complete Examples

#### Echo Bot (RX + TX)

```c
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>

#define UART_DEV DEVICE_DT_GET(DT_CHOSEN(zephyr_shell_uart))
#define BUF_SIZE 64

K_MSGQ_DEFINE(rx_msgq, BUF_SIZE, 8, 4);

static char rx_buf[BUF_SIZE];
static int rx_pos;

void serial_cb(const struct device *dev, void *user_data)
{
    if (!uart_irq_update(dev)) return;
    if (!uart_irq_rx_ready(dev)) return;

    uint8_t c;
    while (uart_fifo_read(dev, &c, 1) == 1) {
        if (c == '\r' || c == '\n') {
            if (rx_pos > 0) {
                rx_buf[rx_pos] = '\0';
                k_msgq_put(&rx_msgq, rx_buf, K_NO_WAIT);
                rx_pos = 0;
            }
        } else if (rx_pos < BUF_SIZE - 1) {
            rx_buf[rx_pos++] = c;
        }
    }
}

void print_uart(const struct device *dev, const char *str)
{
    while (*str) {
        uart_poll_out(dev, *str++);
    }
}

int main(void)
{
    const struct device *dev = UART_DEV;
    char line[BUF_SIZE];

    if (!device_is_ready(dev)) {
        return -ENODEV;
    }

    uart_irq_callback_user_data_set(dev, serial_cb, NULL);
    uart_irq_rx_enable(dev);

    print_uart(dev, "Echo bot ready\r\n");

    while (k_msgq_get(&rx_msgq, line, K_FOREVER) == 0) {
        print_uart(dev, "Echo: ");
        print_uart(dev, line);
        print_uart(dev, "\r\n");
    }
    return 0;
}
```

---

### Common Issues

| Problem | Cause | Solution |
|---------|-------|----------|
| Callback never called | IRQ not enabled | Call `uart_irq_rx_enable()` |
| Missing characters | FIFO not drained | Read until `uart_fifo_read()` returns 0 |
| TX IRQ storms | TX IRQ not disabled | Disable TX IRQ when buffer empty |
| Data corruption | ISR too slow | Use ring buffer, process in thread |
| `-ENOSYS` error | API not implemented | Check driver support |
| `-ENOTSUP` error | API not enabled | Enable `CONFIG_UART_INTERRUPT_DRIVEN` |

## Kconfig

Complete reference for Zephyr UART/Serial subsystem Kconfig configuration options.

### Table of Contents

1. [Essential Options](#essential-options)
2. [API Selection](#api-selection)
3. [Runtime Configuration](#runtime-configuration)
4. [Advanced Options](#advanced-options)
5. [Logging](#logging)
6. [Example Configurations](#example-configurations)

---

### Essential Options

#### Core Serial

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_SERIAL` | bool | y | Enable serial driver subsystem |
| `CONFIG_SERIAL_HAS_DRIVER` | bool | (auto) | Set by driver to indicate serial available |
| `CONFIG_SERIAL_INIT_PRIORITY` | int | varies | Driver initialization priority |

#### Minimal Configuration

```kconfig
CONFIG_SERIAL=y
```

This enables polling API only. Interrupt and async APIs require additional options.

---

### API Selection

#### Interrupt-Driven API

```kconfig
CONFIG_UART_INTERRUPT_DRIVEN=y
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_UART_INTERRUPT_DRIVEN` | bool | n | Enable interrupt-driven UART API |
| `CONFIG_SERIAL_SUPPORT_INTERRUPT` | bool | (auto) | Set by driver if hardware supports interrupts |

**Required for**: `uart_irq_*` functions, `uart_fifo_read/fill`, ISR callbacks.

#### Async (DMA) API

```kconfig
CONFIG_UART_ASYNC_API=y
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_UART_ASYNC_API` | bool | n | Enable asynchronous UART API |
| `CONFIG_SERIAL_SUPPORT_ASYNC` | bool | (auto) | Set by driver if hardware supports async/DMA |

**Required for**: `uart_tx`, `uart_rx_enable`, `uart_callback_set`.

#### API Callback Exclusivity

```kconfig
CONFIG_UART_EXCLUSIVE_API_CALLBACKS=y
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_UART_EXCLUSIVE_API_CALLBACKS` | bool | y | Only one API's callbacks active at a time |

**Effect**: Setting async callback disables interrupt callback and vice versa.

**Warning**: Do not disable unless you fully understand the implications.

---

### Runtime Configuration

#### Dynamic Configuration

```kconfig
CONFIG_UART_USE_RUNTIME_CONFIGURE=y
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_UART_USE_RUNTIME_CONFIGURE` | bool | y | Enable `uart_configure()` at runtime |

**Required for**: Changing baud rate, parity, stop bits at runtime.

**Disable to**: Reduce footprint if configuration is static (from DTS only).

#### Line Control

```kconfig
CONFIG_UART_LINE_CTRL=y
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_UART_LINE_CTRL` | bool | n | Enable line control API |

**Required for**: `uart_line_ctrl_set/get` for RTS, CTS, DTR, DSR, baud rate control.

#### Driver Commands

```kconfig
CONFIG_UART_DRV_CMD=y
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_UART_DRV_CMD` | bool | n | Enable driver-specific commands |

**Required for**: `uart_drv_cmd()` for hardware-specific functions.

---

### Advanced Options

#### Wide Data (9-bit+)

```kconfig
CONFIG_UART_WIDE_DATA=y
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_UART_WIDE_DATA` | bool | n | Enable 9/16-bit data support |
| `CONFIG_SERIAL_SUPPORT_WIDE_DATA` | bool | (auto) | Set by driver if hardware supports wide data |

**Required for**: `uart_poll_in_u16`, `uart_poll_out_u16`, `uart_fifo_fill_u16`, etc.

#### UART Pipe

```kconfig
CONFIG_UART_PIPE=y
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_UART_PIPE` | bool | n | Enable pipe UART driver |

Custom protocol handling without shell/console interpretation.

#### Async Helpers

```kconfig
CONFIG_UART_ASYNC_RX_HELPER=y
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_UART_ASYNC_RX_HELPER` | bool | n | Helper for variable-length async RX |

Zero-copy handling with multiple RX buffers.

#### Async-to-Interrupt Adapter

```kconfig
CONFIG_UART_ASYNC_TO_INT_DRIVEN_API=y
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_UART_ASYNC_TO_INT_DRIVEN_API` | bool | n | Adapter layer for async-only drivers |
| `CONFIG_UART_ASYNC_TO_INT_DRIVEN_RX_TIMEOUT` | int | 100 | RX timeout in bauds |

Allows using interrupt API on drivers that only implement async.

---

### Logging

```kconfig
CONFIG_UART_LOG_LEVEL_DBG=y
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_UART_LOG_LEVEL_OFF` | bool | | Disable UART logging |
| `CONFIG_UART_LOG_LEVEL_ERR` | bool | | Error level only |
| `CONFIG_UART_LOG_LEVEL_WRN` | bool | | Warning and above |
| `CONFIG_UART_LOG_LEVEL_INF` | bool | y | Info and above (default) |
| `CONFIG_UART_LOG_LEVEL_DBG` | bool | | Debug and above |

---

### Example Configurations

#### Minimal (Polling Only)

```kconfig
CONFIG_SERIAL=y
```

#### Interrupt-Driven Console

```kconfig
CONFIG_SERIAL=y
CONFIG_UART_INTERRUPT_DRIVEN=y
CONFIG_CONSOLE=y
CONFIG_UART_CONSOLE=y
```

#### High-Throughput Async

```kconfig
CONFIG_SERIAL=y
CONFIG_UART_ASYNC_API=y
CONFIG_UART_USE_RUNTIME_CONFIGURE=y
```

#### Full-Featured Development

```kconfig
CONFIG_SERIAL=y
CONFIG_UART_INTERRUPT_DRIVEN=y
CONFIG_UART_ASYNC_API=y
CONFIG_UART_USE_RUNTIME_CONFIGURE=y
CONFIG_UART_LINE_CTRL=y
CONFIG_UART_LOG_LEVEL_DBG=y
```

#### Minimal Footprint

```kconfig
CONFIG_SERIAL=y
CONFIG_UART_USE_RUNTIME_CONFIGURE=n
# Use DTS for static configuration
```

#### RS-485 Mode

```kconfig
CONFIG_SERIAL=y
CONFIG_UART_INTERRUPT_DRIVEN=y
CONFIG_UART_LINE_CTRL=y
# Hardware-specific RS-485 options may vary by driver
```

---

### Shell Integration

For shell over UART:

```kconfig
CONFIG_SHELL=y
CONFIG_SHELL_BACKEND_SERIAL=y
CONFIG_SHELL_BACKEND_SERIAL_INTERRUPT_DRIVEN=y
```

---

### Driver-Specific Options

Many SoC-specific options exist under `CONFIG_UART_<VENDOR>_*`. Examples:

- `CONFIG_UART_NRFX_*` - Nordic nRF
- `CONFIG_UART_STM32_*` - STM32
- `CONFIG_UART_SAM_*` - Atmel SAM
- `CONFIG_UART_ESP32_*` - ESP32
- `CONFIG_UART_NS16550_*` - Generic 16550

Check `drivers/serial/Kconfig.<vendor>` for vendor-specific options.

---

### Troubleshooting Kconfig

| Error | Cause | Solution |
|-------|-------|----------|
| `uart_irq_*` undefined | API not enabled | Enable `CONFIG_UART_INTERRUPT_DRIVEN` |
| `uart_tx` undefined | API not enabled | Enable `CONFIG_UART_ASYNC_API` |
| `uart_configure` returns `-ENOSYS` | Runtime config disabled | Enable `CONFIG_UART_USE_RUNTIME_CONFIGURE` |
| Symbol not visible in menuconfig | Missing dependency | Check `depends on` in Kconfig |
| "SERIAL_SUPPORT_INTERRUPT not set" | Hardware doesn't support | Check driver or use different UART |

## Polling

The polling API is the simplest UART interface. Always available when `CONFIG_SERIAL=y`.

### Table of Contents

1. [API Functions](#api-functions)
2. [Usage Patterns](#usage-patterns)
3. [Wide Data Support](#wide-data-support)

---

### API Functions

#### uart_poll_out

```c
void uart_poll_out(const struct device *dev, unsigned char out_char);
```

**Behavior**: Blocking. Waits until character is transmitted.

**Flow control**: If hardware flow control enabled, waits for CTS assertion.

**Example**:
```c
void send_string(const struct device *dev, const char *str)
{
    while (*str) {
        uart_poll_out(dev, *str++);
    }
}
```

#### uart_poll_in

```c
int uart_poll_in(const struct device *dev, unsigned char *p_char);
```

**Behavior**: Non-blocking. Returns immediately.

**Returns**:
- `0`: Character received, stored in `*p_char`
- `-1`: No data available (FIFO empty)
- `-ENOSYS`: Not implemented
- `-EBUSY`: Async RX active (call `uart_rx_disable()` first)

**Example**:
```c
int try_read(const struct device *dev)
{
    unsigned char c;
    if (uart_poll_in(dev, &c) == 0) {
        return (int)c;
    }
    return -1;  /* No data */
}
```

---

### Usage Patterns

#### Simple Debug Output

```c
#include <zephyr/drivers/uart.h>

#define UART_DEV DEVICE_DT_GET(DT_CHOSEN(zephyr_shell_uart))

void debug_print(const char *msg)
{
    const struct device *dev = UART_DEV;
    if (!device_is_ready(dev)) return;

    while (*msg) {
        uart_poll_out(dev, *msg++);
    }
}
```

#### Blocking Read with Timeout

Polling RX is non-blocking, so implement timeout manually:

```c
int read_with_timeout(const struct device *dev, char *buf, size_t len, k_timeout_t timeout)
{
    int64_t end = k_uptime_get() + k_ticks_to_ms_ceil64(timeout.ticks);
    size_t pos = 0;
    unsigned char c;

    while (pos < len && k_uptime_get() < end) {
        if (uart_poll_in(dev, &c) == 0) {
            buf[pos++] = c;
        } else {
            k_yield();  /* Let other threads run */
        }
    }
    return pos;
}
```

#### Echo Loop (Busy-Wait)

Simple but CPU-intensive:

```c
void echo_loop(const struct device *dev)
{
    unsigned char c;
    while (1) {
        if (uart_poll_in(dev, &c) == 0) {
            uart_poll_out(dev, c);
        }
    }
}
```

#### Line-Based Input

```c
int read_line(const struct device *dev, char *buf, size_t max_len)
{
    size_t pos = 0;
    unsigned char c;

    while (pos < max_len - 1) {
        if (uart_poll_in(dev, &c) == 0) {
            if (c == '\n' || c == '\r') {
                buf[pos] = '\0';
                return pos;
            }
            buf[pos++] = c;
        }
        k_msleep(1);  /* Reduce CPU usage */
    }
    buf[pos] = '\0';
    return pos;
}
```

---

### Wide Data Support

For 9-bit or 16-bit data modes (requires `CONFIG_UART_WIDE_DATA=y`):

#### uart_poll_out_u16

```c
void uart_poll_out_u16(const struct device *dev, uint16_t out_u16);
```

#### uart_poll_in_u16

```c
int uart_poll_in_u16(const struct device *dev, uint16_t *p_u16);
```

**Returns**: Same as `uart_poll_in`.

**Note**: Not all hardware supports wide data. Check driver documentation.

---

### When to Use Polling

| Scenario | Recommendation |
|----------|----------------|
| Debug/diagnostic output | Use polling TX |
| Low-volume, infrequent data | Use polling |
| High-volume or continuous RX | Use interrupt or async |
| Background processing needed | Use interrupt or async |
| Bootloader or early init | Use polling (simpler) |

### Limitations

- **TX**: Blocking can stall thread for duration of transmission
- **RX**: Easy to miss data if not polled frequently enough
- **CPU**: Polling loops consume CPU cycles
- **No buffering**: Single-character operations only
