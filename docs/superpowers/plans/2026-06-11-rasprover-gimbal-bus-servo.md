# RaspRover Gimbal Bus Servo Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Waveshare gimbal support by creating a reusable serial bus-servo driver in `rosterloh-drivers`, configuring pan/tilt actuators on the `ros_driver` board, and exposing `pan_joint` / `tilt_joint` feedback and position commands from `applications/rasprover`.

**Architecture:** Keep protocol and actuator code in `deps/modules/lib/rosterloh-drivers`; rasprover consumes only the generic actuator API. UART0 remains Zephyr console/shell debug. UART1 on GPIO19 TX / GPIO18 RX becomes the bus-servo UART; rasprover removes the stale `zenoh-serial` alias and keeps zenoh over TCP/WiFi.

**Tech Stack:** Zephyr RTOS, devicetree, Kconfig, ztest/twister on `native_sim`, `uart-emul` fake bus tests, rosterloh actuator subsystem, zenoh-pico ROS 2 CDR bridge, host-side pytest/gcc tests for rasprover CDR helpers.

**Spec:** [`docs/superpowers/specs/2026-06-11-rasprover-gimbal-bus-servo-design.md`](../specs/2026-06-11-rasprover-gimbal-bus-servo-design.md)

---

## Scope Check

This plan intentionally spans two repos because the app depends on the driver:

- Tasks 1-5 happen in `deps/modules/lib/rosterloh-drivers/`. Commit those changes inside that module checkout.
- Tasks 6-10 happen in this workspace repo under `applications/rasprover/`, `tests/rasprover/`, and docs.

The driver tasks produce independently testable protocol and actuator behavior before rasprover is changed. The app tasks should not start until the module builds and the bus-servo actuator backend exists.

## File Structure

Driver module files:

- `deps/modules/lib/rosterloh-drivers/include/drivers/bus_servo.h` - public bus-servo protocol API and register constants.
- `deps/modules/lib/rosterloh-drivers/drivers/bus_servo/` - UART framing, checksum, read/write/sync-write operations.
- `deps/modules/lib/rosterloh-drivers/dts/bindings/waveshare,bus-servo.yaml` - UART child bus binding.
- `deps/modules/lib/rosterloh-drivers/dts/bindings/actuator/rosterloh,actuator-bus-servo.yaml` - per-servo actuator child binding.
- `deps/modules/lib/rosterloh-drivers/drivers/actuator/bus_servo/` - actuator backend for position-mode bus servos.
- `deps/modules/lib/rosterloh-drivers/tests/drivers/bus_servo/` - protocol tests with a fake bus.
- `deps/modules/lib/rosterloh-drivers/tests/drivers/actuator/bus_servo/` - actuator backend tests.
- `deps/modules/lib/rosterloh-drivers/boards/waveshare/ros_driver/ros_driver_procpu.dts` - hardware bus and pan/tilt actuator nodes.

Rasprover files:

- `applications/rasprover/src/app_ros_cdr.c` / `.h` - add a small JointState command decoder.
- `applications/rasprover/src/app_gimbal.c` / `.h` - gimbal actuator facade.
- `applications/rasprover/src/app_zenoh.c` - gimbal command subscriber and four-joint publication.
- `applications/rasprover/src/main.c` - initialize `app_gimbal`.
- `applications/rasprover/Kconfig`, `prj.conf`, `CMakeLists.txt` - feature flags and sources.
- `applications/rasprover/boards/ros_driver_esp32_procpu.overlay` - remove `zenoh-serial`.
- `applications/rasprover/boards/native_sim_native_64.overlay` / `.conf` - fake gimbal actuators.
- `tests/rasprover/test_ros_cdr.py` / `test_ros_topics.py` - host tests for CDR and config defaults.
- `applications/rasprover/README.md` - document the command topic.

## Conventions

All commands run from `/home/rio/workspace/zephyr-applications` unless the step explicitly says to run inside the module. Use `uv run` for Python and west commands.

For module tests:

```bash
uv run west twister -p native_sim -T deps/modules/lib/rosterloh-drivers/tests/drivers/bus_servo -i
uv run west twister -p native_sim -T deps/modules/lib/rosterloh-drivers/tests/drivers/actuator/bus_servo -i
```

Commit cadence:

- Driver protocol commits: run inside `deps/modules/lib/rosterloh-drivers`, message prefix `drivers/bus_servo:`.
- Driver actuator commits: run inside `deps/modules/lib/rosterloh-drivers`, message prefix `drivers/actuator/bus_servo:`.
- Board commits in the module: run inside `deps/modules/lib/rosterloh-drivers`, message prefix `boards/ros_driver:`.
- Rasprover commits: run at workspace root, message prefix `rasprover:`.

The local pre-commit hook in this workspace currently fails before running hooks because `.venv/bin/python` cannot import `_sqlite3`. Until that environment is repaired, run `rtk git diff --cached --check` before any `--no-verify` commit and say so in the commit handoff.

---

### Task 1: Pin Bus-Servo Protocol Framing With Failing Tests

**Files:**
- Create: `deps/modules/lib/rosterloh-drivers/tests/drivers/bus_servo/CMakeLists.txt`
- Create: `deps/modules/lib/rosterloh-drivers/tests/drivers/bus_servo/prj.conf`
- Create: `deps/modules/lib/rosterloh-drivers/tests/drivers/bus_servo/testcase.yaml`
- Create: `deps/modules/lib/rosterloh-drivers/tests/drivers/bus_servo/boards/native_sim.overlay`
- Create: `deps/modules/lib/rosterloh-drivers/tests/drivers/bus_servo/src/main.c`
- Create: `deps/modules/lib/rosterloh-drivers/tests/drivers/bus_servo/src/fake_bus.c`
- Create: `deps/modules/lib/rosterloh-drivers/tests/drivers/bus_servo/src/fake_bus.h`
- Create: `deps/modules/lib/rosterloh-drivers/tests/drivers/bus_servo/src/test_protocol.c`

- [ ] **Step 1: Create the bus-servo ztest skeleton**

`deps/modules/lib/rosterloh-drivers/tests/drivers/bus_servo/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_bus_servo)

target_sources(app PRIVATE
    src/main.c
    src/fake_bus.c
    src/test_protocol.c
)

zephyr_include_directories(./src)
zephyr_include_directories(${ZEPHYR_DRIVERS_REPO_ROOT}/drivers/bus_servo)
```

`deps/modules/lib/rosterloh-drivers/tests/drivers/bus_servo/prj.conf`:

```conf
CONFIG_ZTEST=y
CONFIG_UART_INTERRUPT_DRIVEN=y
CONFIG_BUS_SERVO=y
CONFIG_BUS_SERVO_LOG_LEVEL_DBG=y
```

`deps/modules/lib/rosterloh-drivers/tests/drivers/bus_servo/testcase.yaml`:

```yaml
tests:
  drivers.bus_servo:
    tags:
      - drivers
      - bus_servo
    harness: ztest
    platform_allow:
      - native_sim
```

`deps/modules/lib/rosterloh-drivers/tests/drivers/bus_servo/src/main.c`:

```c
#include <zephyr/ztest.h>

ZTEST_SUITE(bus_servo_protocol, NULL, NULL, NULL, NULL, NULL);
```

- [ ] **Step 2: Add the fake UART bus test overlay**

`deps/modules/lib/rosterloh-drivers/tests/drivers/bus_servo/boards/native_sim.overlay`:

```dts
/ {
	bus_servo_uart: uart-emul {
		compatible = "zephyr,uart-emul";
		current-speed = <115200>;
		status = "okay";

		bus_servo0: bus-servo {
			compatible = "waveshare,bus-servo";
			status = "okay";
		};
	};
};
```

- [ ] **Step 3: Add a fake bus harness that records TX frames and injects RX frames**

`deps/modules/lib/rosterloh-drivers/tests/drivers/bus_servo/src/fake_bus.h`:

```c
#ifndef TEST_BUS_SERVO_FAKE_BUS_H_
#define TEST_BUS_SERVO_FAKE_BUS_H_

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>

struct fake_bus {
	uint8_t last_tx[64];
	size_t last_tx_len;
	uint8_t rx[64];
	size_t rx_len;
	bool drop_response;
};

void fake_bus_init(struct fake_bus *bus);
void fake_bus_attach(struct fake_bus *bus, const struct device *uart_dev);
void fake_bus_queue_rx(struct fake_bus *bus, const uint8_t *data, size_t len);

#endif /* TEST_BUS_SERVO_FAKE_BUS_H_ */
```

`deps/modules/lib/rosterloh-drivers/tests/drivers/bus_servo/src/fake_bus.c`:

```c
#include "fake_bus.h"

#include <string.h>
#include <zephyr/drivers/uart.h>

static struct fake_bus *active_bus;

void fake_bus_init(struct fake_bus *bus)
{
	memset(bus, 0, sizeof(*bus));
}

void fake_bus_attach(struct fake_bus *bus, const struct device *uart_dev)
{
	ARG_UNUSED(uart_dev);
	active_bus = bus;
}

void fake_bus_queue_rx(struct fake_bus *bus, const uint8_t *data, size_t len)
{
	memcpy(bus->rx, data, len);
	bus->rx_len = len;
}

int uart_fifo_fill(const struct device *dev, const uint8_t *tx_data, int len)
{
	ARG_UNUSED(dev);
	memcpy(active_bus->last_tx + active_bus->last_tx_len, tx_data, len);
	active_bus->last_tx_len += len;
	return len;
}

int uart_fifo_read(const struct device *dev, uint8_t *rx_data, const int size)
{
	ARG_UNUSED(dev);
	if (active_bus->drop_response || active_bus->rx_len == 0) {
		return 0;
	}
	size_t n = MIN((size_t)size, active_bus->rx_len);
	memcpy(rx_data, active_bus->rx, n);
	memmove(active_bus->rx, active_bus->rx + n, active_bus->rx_len - n);
	active_bus->rx_len -= n;
	return (int)n;
}
```

- [ ] **Step 4: Add protocol tests that define the API contract**

`deps/modules/lib/rosterloh-drivers/tests/drivers/bus_servo/src/test_protocol.c`:

```c
#include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/ztest.h>
#include <drivers/bus_servo.h>

#include "fake_bus.h"

#define BUS_NODE   DT_NODELABEL(bus_servo0)
#define UART_NODE  DT_PARENT(BUS_NODE)
#define IFACE_NAME DEVICE_DT_NAME(BUS_NODE)

static const struct device *uart_dev = DEVICE_DT_GET(UART_NODE);
static struct fake_bus bus;
static int iface;

static void before_each(void *fixture)
{
	ARG_UNUSED(fixture);
	struct bus_servo_iface_param param = {
		.rx_timeout_us = 50000,
		.serial = {.baud = 115200, .parity = UART_CFG_PARITY_NONE},
	};

	fake_bus_init(&bus);
	fake_bus_attach(&bus, uart_dev);
	iface = bus_servo_iface_get_by_name(IFACE_NAME);
	zassert_true(iface >= 0, "iface lookup failed: %d", iface);
	zassert_ok(bus_servo_init(iface, param), "bus_servo_init failed");
}

static void after_each(void *fixture)
{
	ARG_UNUSED(fixture);
	bus_servo_disable(iface);
}

ZTEST_SUITE(bus_servo_protocol, NULL, NULL, before_each, after_each, NULL);

ZTEST(bus_servo_protocol, test_write_u16_frame_bytes)
{
	/* FF FF id len inst addr value_l value_h checksum */
	zassert_ok(bus_servo_write_u16(iface, 2, 0x2a, 0x07ff));

	const uint8_t expected[] = {
		0xff, 0xff, 0x02, 0x05, 0x03, 0x2a, 0xff, 0x07, 0xc5,
	};
	zassert_equal(bus.last_tx_len, sizeof(expected));
	zassert_mem_equal(bus.last_tx, expected, sizeof(expected));
}

ZTEST(bus_servo_protocol, test_read_u16_frame_and_response_parse)
{
	const uint8_t response[] = {
		0xff, 0xff, 0x01, 0x04, 0x00, 0x34, 0x12, 0xb4,
	};
	uint16_t value = 0;

	fake_bus_queue_rx(&bus, response, sizeof(response));
	zassert_ok(bus_servo_read_u16(iface, 1, 0x38, &value));

	const uint8_t expected_request[] = {
		0xff, 0xff, 0x01, 0x04, 0x02, 0x38, 0x02, 0xbe,
	};
	zassert_equal(value, 0x1234);
	zassert_equal(bus.last_tx_len, sizeof(expected_request));
	zassert_mem_equal(bus.last_tx, expected_request, sizeof(expected_request));
}

ZTEST(bus_servo_protocol, test_timeout_returns_etimedout)
{
	uint16_t value = 0;

	bus.drop_response = true;
	zassert_equal(bus_servo_read_u16(iface, 1, 0x38, &value), -ETIMEDOUT);
}
```

- [ ] **Step 5: Run the test and verify it fails for missing implementation**

Run:

```bash
uv run west twister -p native_sim -T deps/modules/lib/rosterloh-drivers/tests/drivers/bus_servo -i
```

Expected: FAIL during compile with an error containing `drivers/bus_servo.h` not found or `CONFIG_BUS_SERVO` undefined.

- [ ] **Step 6: Commit the red test**

Run inside `deps/modules/lib/rosterloh-drivers`:

```bash
git add tests/drivers/bus_servo
git commit -m "tests/bus_servo: pin serial protocol framing"
```

### Task 2: Implement Bus-Servo Protocol Core

**Files:**
- Create: `deps/modules/lib/rosterloh-drivers/include/drivers/bus_servo.h`
- Create: `deps/modules/lib/rosterloh-drivers/drivers/bus_servo/CMakeLists.txt`
- Create: `deps/modules/lib/rosterloh-drivers/drivers/bus_servo/Kconfig`
- Create: `deps/modules/lib/rosterloh-drivers/drivers/bus_servo/bus_servo.c`
- Create: `deps/modules/lib/rosterloh-drivers/drivers/bus_servo/bus_servo_internal.h`
- Create: `deps/modules/lib/rosterloh-drivers/drivers/bus_servo/bus_servo_serial.c`
- Create: `deps/modules/lib/rosterloh-drivers/dts/bindings/waveshare,bus-servo.yaml`
- Modify: `deps/modules/lib/rosterloh-drivers/drivers/CMakeLists.txt`
- Modify: `deps/modules/lib/rosterloh-drivers/drivers/Kconfig`

- [ ] **Step 1: Add public API and protocol constants**

`deps/modules/lib/rosterloh-drivers/include/drivers/bus_servo.h`:

```c
#ifndef ZEPHYR_INCLUDE_DRIVERS_BUS_SERVO_H_
#define ZEPHYR_INCLUDE_DRIVERS_BUS_SERVO_H_

#include <stddef.h>
#include <stdint.h>
#include <zephyr/drivers/uart.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BUS_SERVO_BROADCAST_ID 0xfe

enum bus_servo_instruction {
	BUS_SERVO_INST_PING = 0x01,
	BUS_SERVO_INST_READ = 0x02,
	BUS_SERVO_INST_WRITE = 0x03,
	BUS_SERVO_INST_REG_WRITE = 0x04,
	BUS_SERVO_INST_ACTION = 0x05,
	BUS_SERVO_INST_SYNC_WRITE = 0x83,
};

enum bus_servo_register {
	BUS_SERVO_REG_TORQUE_ENABLE = 40,
	BUS_SERVO_REG_GOAL_ACCEL = 41,
	BUS_SERVO_REG_GOAL_POSITION_L = 42,
	BUS_SERVO_REG_GOAL_TIME_L = 44,
	BUS_SERVO_REG_GOAL_SPEED_L = 46,
	BUS_SERVO_REG_PRESENT_POSITION_L = 56,
	BUS_SERVO_REG_PRESENT_SPEED_L = 58,
	BUS_SERVO_REG_PRESENT_LOAD_L = 60,
	BUS_SERVO_REG_PRESENT_VOLTAGE = 62,
	BUS_SERVO_REG_PRESENT_TEMPERATURE = 63,
	BUS_SERVO_REG_MOVING = 66,
	BUS_SERVO_REG_PRESENT_CURRENT_L = 69,
};

struct bus_servo_iface_param {
	uint32_t rx_timeout_us;
	struct {
		uint32_t baud;
		enum uart_config_parity parity;
	} serial;
};

int bus_servo_iface_get_by_name(const char *iface_name);
int bus_servo_init(int iface, struct bus_servo_iface_param param);
int bus_servo_disable(int iface);
int bus_servo_ping(int iface, uint8_t id);

int bus_servo_read_u8(int iface, uint8_t id, uint8_t addr, uint8_t *out);
int bus_servo_read_u16(int iface, uint8_t id, uint8_t addr, uint16_t *out);
int bus_servo_write_u8(int iface, uint8_t id, uint8_t addr, uint8_t value);
int bus_servo_write_u16(int iface, uint8_t id, uint8_t addr, uint16_t value);

int bus_servo_write_position_ex(int iface, uint8_t id, uint16_t position, uint16_t speed,
				uint8_t accel);
int bus_servo_sync_write_position_ex(int iface, const uint8_t ids[], const uint16_t positions[],
				     const uint16_t speeds[], const uint8_t accels[], size_t n);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_BUS_SERVO_H_ */
```

- [ ] **Step 2: Add Kconfig, CMake, and DT binding**

`deps/modules/lib/rosterloh-drivers/drivers/bus_servo/Kconfig`:

```kconfig
config BUS_SERVO
	bool "Waveshare/SCServo serial bus servo driver"
	depends on SERIAL && UART_INTERRUPT_DRIVEN
	default n
	help
	  Serial protocol driver for Waveshare gimbal bus servos using the
	  SCServo-style packet format.

module = BUS_SERVO
module-str = bus_servo
source "subsys/logging/Kconfig.template.log_config"
```

`deps/modules/lib/rosterloh-drivers/drivers/bus_servo/CMakeLists.txt`:

```cmake
zephyr_library_named(bus_servo)
zephyr_library_sources(bus_servo.c bus_servo_serial.c)
```

`deps/modules/lib/rosterloh-drivers/dts/bindings/waveshare,bus-servo.yaml`:

```yaml
description: Waveshare/SCServo serial bus

compatible: "waveshare,bus-servo"

include: uart-device.yaml

properties:
  tx-en-gpios:
    type: phandle-array
    required: false
    description: Optional transmit enable pin for half-duplex transceivers.
```

Modify `deps/modules/lib/rosterloh-drivers/drivers/Kconfig` inside `menu "Drivers"`:

```kconfig
rsource "bus_servo/Kconfig"
```

Modify `deps/modules/lib/rosterloh-drivers/drivers/CMakeLists.txt`:

```cmake
add_subdirectory_ifdef(CONFIG_BUS_SERVO bus_servo)
```

- [ ] **Step 3: Implement context table, checksum, read/write frames**

`deps/modules/lib/rosterloh-drivers/drivers/bus_servo/bus_servo_internal.h`:

```c
#ifndef ROSTERLOH_DRIVERS_BUS_SERVO_INTERNAL_H_
#define ROSTERLOH_DRIVERS_BUS_SERVO_INTERNAL_H_

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <drivers/bus_servo.h>

#define BUS_SERVO_MAX_PACKET_SIZE 64

struct bus_servo_serial_config {
	const struct device *dev;
	const struct gpio_dt_spec *tx_en;
	uint8_t uart_buf[BUS_SERVO_MAX_PACKET_SIZE];
	size_t uart_buf_len;
};

struct bus_servo_context {
	const char *iface_name;
	struct bus_servo_serial_config *cfg;
	struct k_mutex iface_lock;
	struct k_sem wait_sem;
	uint32_t rx_timeout_us;
	bool configured;
	uint8_t expected_id;
	uint8_t rx_error;
	uint8_t rx_params[BUS_SERVO_MAX_PACKET_SIZE];
	size_t rx_param_len;
};

uint8_t bus_servo_checksum(const uint8_t *buf, size_t len);
int bus_servo_serial_init(struct bus_servo_context *ctx, struct bus_servo_iface_param param);
int bus_servo_tx_rx(struct bus_servo_context *ctx, const uint8_t *packet, size_t len,
		    bool expect_status);
struct bus_servo_context *bus_servo_get_context(uint8_t iface);

#endif /* ROSTERLOH_DRIVERS_BUS_SERVO_INTERNAL_H_ */
```

`deps/modules/lib/rosterloh-drivers/drivers/bus_servo/bus_servo.c` should define:

```c
#define DT_DRV_COMPAT waveshare_bus_servo

#include <errno.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>

#include "bus_servo_internal.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bus_servo, CONFIG_BUS_SERVO_LOG_LEVEL);

uint8_t bus_servo_checksum(const uint8_t *buf, size_t len)
{
	uint16_t sum = 0;
	for (size_t i = 0; i < len; i++) {
		sum += buf[i];
	}
	return (uint8_t)(~sum);
}
```

Continue in the same file with:

- DT instance table, same shape as `drivers/dynamixel/dynamixel.c`
- `bus_servo_iface_get_by_name()`
- `bus_servo_init()`
- `bus_servo_disable()`
- `bus_servo_get_context()`
- packet builder for `PING`, `READ`, `WRITE`
- `bus_servo_read_u8()`, `bus_servo_read_u16()`, `bus_servo_write_u8()`, `bus_servo_write_u16()`

Use this helper shape for read/write builders:

```c
static size_t make_packet(uint8_t *out, uint8_t id, uint8_t instruction,
			  const uint8_t *params, size_t param_len)
{
	out[0] = 0xff;
	out[1] = 0xff;
	out[2] = id;
	out[3] = (uint8_t)(param_len + 2);
	out[4] = instruction;
	memcpy(&out[5], params, param_len);
	out[5 + param_len] = bus_servo_checksum(&out[2], param_len + 3);
	return param_len + 6;
}
```

- [ ] **Step 4: Implement serial init and synchronous TX/RX**

`deps/modules/lib/rosterloh-drivers/drivers/bus_servo/bus_servo_serial.c` should:

- configure UART to `param.serial.baud`, 8N1
- use `uart_fifo_fill()` to transmit a full packet
- read bytes with `uart_fifo_read()` until a full status packet is available or `rx_timeout_us` expires
- validate `0xff 0xff`, matching ID, length, checksum
- store status error byte and parameters in `ctx`

Use `k_busy_wait(50)` in the polling loop for the first pass. Do not use an ISR parser until the synchronous path passes tests.

- [ ] **Step 5: Run the protocol tests and verify green**

Run:

```bash
uv run west twister -p native_sim -T deps/modules/lib/rosterloh-drivers/tests/drivers/bus_servo -i
```

Expected: PASS for `drivers.bus_servo`.

- [ ] **Step 6: Commit protocol core**

Run inside `deps/modules/lib/rosterloh-drivers`:

```bash
git add include/drivers/bus_servo.h drivers/Kconfig drivers/CMakeLists.txt drivers/bus_servo dts/bindings/waveshare,bus-servo.yaml
git commit -m "drivers/bus_servo: add serial protocol core"
```

### Task 3: Add Position Helpers and Sync-Write Coverage

**Files:**
- Modify: `deps/modules/lib/rosterloh-drivers/tests/drivers/bus_servo/src/test_protocol.c`
- Modify: `deps/modules/lib/rosterloh-drivers/drivers/bus_servo/bus_servo.c`
- Modify: `deps/modules/lib/rosterloh-drivers/include/drivers/bus_servo.h`

- [ ] **Step 1: Add failing tests for high-level position and sync-write frames**

Append to `deps/modules/lib/rosterloh-drivers/tests/drivers/bus_servo/src/test_protocol.c`:

```c
ZTEST(bus_servo_protocol, test_write_position_ex_frame_bytes)
{
	zassert_ok(bus_servo_write_position_ex(iface, 2, 2047, 300, 20));

	/* write addr 41: accel, pos_l, pos_h, time_l, time_h, speed_l, speed_h */
	const uint8_t expected[] = {
		0xff, 0xff, 0x02, 0x0a, 0x03, 0x29, 0x14, 0xff,
		0x07, 0x00, 0x00, 0x2c, 0x01, 0x7b,
	};
	zassert_equal(bus.last_tx_len, sizeof(expected));
	zassert_mem_equal(bus.last_tx, expected, sizeof(expected));
}

ZTEST(bus_servo_protocol, test_sync_write_position_ex_frame_bytes)
{
	const uint8_t ids[] = {2, 1};
	const uint16_t pos[] = {2047, 1500};
	const uint16_t spd[] = {300, 250};
	const uint8_t acc[] = {20, 10};

	zassert_ok(bus_servo_sync_write_position_ex(iface, ids, pos, spd, acc, 2));

	const uint8_t expected[] = {
		0xff, 0xff, 0xfe, 0x13, 0x83, 0x29, 0x07,
		0x02, 0x14, 0xff, 0x07, 0x00, 0x00, 0x2c, 0x01,
		0x01, 0x0a, 0xdc, 0x05, 0x00, 0x00, 0xfa, 0x00,
		0xe2,
	};
	zassert_equal(bus.last_tx_len, sizeof(expected));
	zassert_mem_equal(bus.last_tx, expected, sizeof(expected));
}
```

- [ ] **Step 2: Run tests and verify the new tests fail**

Run:

```bash
uv run west twister -p native_sim -T deps/modules/lib/rosterloh-drivers/tests/drivers/bus_servo -i
```

Expected: FAIL with undefined references or assertion failures for `bus_servo_write_position_ex` and `bus_servo_sync_write_position_ex`.

- [ ] **Step 3: Implement position helpers**

In `deps/modules/lib/rosterloh-drivers/drivers/bus_servo/bus_servo.c`, implement:

```c
int bus_servo_write_position_ex(int iface, uint8_t id, uint16_t position, uint16_t speed,
				uint8_t accel)
{
	uint8_t params[] = {
		BUS_SERVO_REG_GOAL_ACCEL,
		accel,
		(uint8_t)position,
		(uint8_t)(position >> 8),
		0x00,
		0x00,
		(uint8_t)speed,
		(uint8_t)(speed >> 8),
	};

	return bus_servo_write_params(iface, id, params, sizeof(params));
}

int bus_servo_sync_write_position_ex(int iface, const uint8_t ids[], const uint16_t positions[],
				     const uint16_t speeds[], const uint8_t accels[], size_t n)
{
	uint8_t params[2 + (1 + 7) * 8];

	if (n == 0 || n > 8 || ids == NULL || positions == NULL || speeds == NULL ||
	    accels == NULL) {
		return -EINVAL;
	}

	params[0] = BUS_SERVO_REG_GOAL_ACCEL;
	params[1] = 7;
	for (size_t i = 0; i < n; i++) {
		size_t off = 2 + i * 8;
		params[off + 0] = ids[i];
		params[off + 1] = accels[i];
		params[off + 2] = (uint8_t)positions[i];
		params[off + 3] = (uint8_t)(positions[i] >> 8);
		params[off + 4] = 0x00;
		params[off + 5] = 0x00;
		params[off + 6] = (uint8_t)speeds[i];
		params[off + 7] = (uint8_t)(speeds[i] >> 8);
	}

	return bus_servo_send_instruction(iface, BUS_SERVO_BROADCAST_ID,
					  BUS_SERVO_INST_SYNC_WRITE, params,
					  2 + n * 8, false);
}
```

If `bus_servo_write_params()` or `bus_servo_send_instruction()` do not exist yet from Task 2, add them as static helpers in `bus_servo.c` with the packet builder from Task 2.

- [ ] **Step 4: Run protocol tests and verify green**

Run:

```bash
uv run west twister -p native_sim -T deps/modules/lib/rosterloh-drivers/tests/drivers/bus_servo -i
```

Expected: PASS for `drivers.bus_servo`.

- [ ] **Step 5: Commit position helpers**

Run inside `deps/modules/lib/rosterloh-drivers`:

```bash
git add include/drivers/bus_servo.h drivers/bus_servo tests/drivers/bus_servo
git commit -m "drivers/bus_servo: add position and sync write helpers"
```

### Task 4: Add Bus-Servo Actuator Backend With Tests

**Files:**
- Create: `deps/modules/lib/rosterloh-drivers/dts/bindings/actuator/rosterloh,actuator-bus-servo.yaml`
- Create: `deps/modules/lib/rosterloh-drivers/drivers/actuator/bus_servo/CMakeLists.txt`
- Create: `deps/modules/lib/rosterloh-drivers/drivers/actuator/bus_servo/Kconfig`
- Create: `deps/modules/lib/rosterloh-drivers/drivers/actuator/bus_servo/actuator_bus_servo.c`
- Create: `deps/modules/lib/rosterloh-drivers/tests/drivers/actuator/bus_servo/CMakeLists.txt`
- Create: `deps/modules/lib/rosterloh-drivers/tests/drivers/actuator/bus_servo/prj.conf`
- Create: `deps/modules/lib/rosterloh-drivers/tests/drivers/actuator/bus_servo/testcase.yaml`
- Create: `deps/modules/lib/rosterloh-drivers/tests/drivers/actuator/bus_servo/boards/native_sim.overlay`
- Create: `deps/modules/lib/rosterloh-drivers/tests/drivers/actuator/bus_servo/src/main.c`
- Create: `deps/modules/lib/rosterloh-drivers/tests/drivers/actuator/bus_servo/src/test_set_position.c`
- Modify: `deps/modules/lib/rosterloh-drivers/drivers/actuator/CMakeLists.txt`
- Modify: `deps/modules/lib/rosterloh-drivers/drivers/actuator/Kconfig`

- [ ] **Step 1: Add actuator backend tests first**

`deps/modules/lib/rosterloh-drivers/tests/drivers/actuator/bus_servo/testcase.yaml`:

```yaml
tests:
  drivers.actuator.bus_servo:
    tags:
      - drivers
      - actuator
      - bus_servo
    harness: ztest
    platform_allow:
      - native_sim
```

`deps/modules/lib/rosterloh-drivers/tests/drivers/actuator/bus_servo/prj.conf`:

```conf
CONFIG_ZTEST=y
CONFIG_UART_INTERRUPT_DRIVEN=y
CONFIG_ACTUATOR=y
CONFIG_BUS_SERVO=y
CONFIG_ACTUATOR_BUS_SERVO=y
CONFIG_BUS_SERVO_LOG_LEVEL_DBG=y
```

`deps/modules/lib/rosterloh-drivers/tests/drivers/actuator/bus_servo/src/main.c`:

```c
#include <zephyr/ztest.h>

ZTEST_SUITE(bus_servo_actuator, NULL, NULL, NULL, NULL, NULL);
```

`deps/modules/lib/rosterloh-drivers/tests/drivers/actuator/bus_servo/boards/native_sim.overlay`:

```dts
/ {
	bus_servo_uart: uart-emul {
		compatible = "zephyr,uart-emul";
		current-speed = <115200>;
		status = "okay";

		bus_servo0: bus-servo {
			compatible = "waveshare,bus-servo";
			status = "okay";

			pan: servo@2 {
				compatible = "rosterloh,actuator-bus-servo";
				reg = <2>;
				default-mode = "position";
				ticks-per-rev = <4096>;
				rad-zero-tick = <2047>;
				position-min-rad-milli = <-3142>;
				position-max-rad-milli = <3142>;
				speed = <300>;
				accel = <20>;
			};
		};
	};
};
```

`deps/modules/lib/rosterloh-drivers/tests/drivers/actuator/bus_servo/src/test_set_position.c`:

```c
#include <math.h>
#include <zephyr/actuator/actuator.h>
#include <zephyr/device.h>
#include <zephyr/ztest.h>

#define PAN DEVICE_DT_GET(DT_NODELABEL(pan))

ZTEST(bus_servo_actuator, test_set_position_pi_over_two_succeeds)
{
	zassert_true(device_is_ready(PAN));
	zassert_ok(actuator_enable(PAN));
	zassert_ok(actuator_set_position(PAN, (float)M_PI / 2.0f));
}

ZTEST(bus_servo_actuator, test_feedback_reports_position)
{
	struct actuator_feedback fb;

	zassert_true(device_is_ready(PAN));
	zassert_ok(actuator_enable(PAN));
	zassert_ok(actuator_read_feedback(PAN, &fb));
	zassert_true((fb.valid_mask & ACTUATOR_FB_POSITION) != 0);
}
```

`deps/modules/lib/rosterloh-drivers/tests/drivers/actuator/bus_servo/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(actuator_bus_servo_tests)

target_sources(app PRIVATE
    src/main.c
    src/test_set_position.c
    ${CMAKE_CURRENT_LIST_DIR}/../../bus_servo/src/fake_bus.c
)

zephyr_include_directories(../../bus_servo/src)
```

- [ ] **Step 2: Run backend tests and verify they fail before backend exists**

Run:

```bash
uv run west twister -p native_sim -T deps/modules/lib/rosterloh-drivers/tests/drivers/actuator/bus_servo -i
```

Expected: FAIL because `rosterloh,actuator-bus-servo` binding or `CONFIG_ACTUATOR_BUS_SERVO` does not exist.

- [ ] **Step 3: Add binding and build wiring**

`deps/modules/lib/rosterloh-drivers/dts/bindings/actuator/rosterloh,actuator-bus-servo.yaml`:

```yaml
description: Waveshare/SCServo bus-servo actuator

compatible: "rosterloh,actuator-bus-servo"

include: [actuator-common.yaml]

properties:
  reg:
    type: int
    required: true
    description: Servo bus ID.

  ticks-per-rev:
    type: int
    default: 4096

  rad-zero-tick:
    type: int
    default: 2047

  position-min-rad-milli:
    type: int

  position-max-rad-milli:
    type: int

  invert-position:
    type: boolean

  speed:
    type: int
    default: 300
    description: Position move speed passed to bus_servo_write_position_ex.

  accel:
    type: int
    default: 20
    description: Position move acceleration passed to bus_servo_write_position_ex.
```

`deps/modules/lib/rosterloh-drivers/drivers/actuator/bus_servo/Kconfig`:

```kconfig
config ACTUATOR_BUS_SERVO
	bool "Bus-servo actuator backend"
	depends on ACTUATOR && BUS_SERVO
	default n
	help
	  Exposes Waveshare/SCServo serial bus servos as generic actuator devices.
```

`deps/modules/lib/rosterloh-drivers/drivers/actuator/bus_servo/CMakeLists.txt`:

```cmake
zephyr_library_named(actuator_bus_servo)
zephyr_library_sources(actuator_bus_servo.c)
```

Modify `drivers/actuator/Kconfig`:

```kconfig
rsource "bus_servo/Kconfig"
```

Modify `drivers/actuator/CMakeLists.txt`:

```cmake
add_subdirectory_ifdef(CONFIG_ACTUATOR_BUS_SERVO bus_servo)
```

- [ ] **Step 4: Implement actuator backend**

`deps/modules/lib/rosterloh-drivers/drivers/actuator/bus_servo/actuator_bus_servo.c` should mirror `actuator_dxl.c` structure:

- `struct bus_servo_actuator_data` embeds `struct actuator_common_data` first
- `struct bus_servo_actuator_config` embeds `struct actuator_cb_offsets` first
- `actuator_enable()` writes `BUS_SERVO_REG_TORQUE_ENABLE = 1`
- `actuator_disable()` writes `BUS_SERVO_REG_TORQUE_ENABLE = 0`
- `set_setpoint(POSITION)` converts radians to ticks and calls `bus_servo_write_position_ex()`
- velocity/effort return `-ENOTSUP`
- feedback reads `BUS_SERVO_REG_PRESENT_POSITION_L` and returns `ACTUATOR_FB_POSITION`
- `group_set_setpoints(POSITION)` calls `bus_servo_sync_write_position_ex()` when all devices share one iface

Use these conversion helpers:

```c
static uint16_t rad_to_ticks(const struct bus_servo_actuator_config *cfg, float rad)
{
	if (cfg->pos_min_milli != INT32_MIN) {
		rad = MAX(rad, (float)cfg->pos_min_milli / 1000.0f);
	}
	if (cfg->pos_max_milli != INT32_MAX) {
		rad = MIN(rad, (float)cfg->pos_max_milli / 1000.0f);
	}
	if (cfg->invert_position) {
		rad = -rad;
	}
	float ticks = (float)cfg->rad_zero_tick +
		      rad * (float)cfg->ticks_per_rev / (2.0f * (float)M_PI);
	ticks = CLAMP(ticks, 0.0f, (float)(cfg->ticks_per_rev - 1));
	return (uint16_t)(ticks + 0.5f);
}

static float ticks_to_rad(const struct bus_servo_actuator_config *cfg, uint16_t ticks)
{
	float rad = ((float)ticks - (float)cfg->rad_zero_tick) *
		    (2.0f * (float)M_PI) / (float)cfg->ticks_per_rev;
	return cfg->invert_position ? -rad : rad;
}
```

- [ ] **Step 5: Run actuator backend tests and verify green**

Run:

```bash
uv run west twister -p native_sim -T deps/modules/lib/rosterloh-drivers/tests/drivers/actuator/bus_servo -i
```

Expected: PASS for `drivers.actuator.bus_servo`.

- [ ] **Step 6: Run protocol tests again**

Run:

```bash
uv run west twister -p native_sim -T deps/modules/lib/rosterloh-drivers/tests/drivers/bus_servo -i
```

Expected: PASS for `drivers.bus_servo`.

- [ ] **Step 7: Commit actuator backend**

Run inside `deps/modules/lib/rosterloh-drivers`:

```bash
git add drivers/actuator dts/bindings/actuator/rosterloh,actuator-bus-servo.yaml tests/drivers/actuator/bus_servo
git commit -m "drivers/actuator/bus_servo: expose bus servos as actuators"
```

### Task 5: Configure `ros_driver` Board Servo Bus

**Files:**
- Modify: `deps/modules/lib/rosterloh-drivers/boards/waveshare/ros_driver/ros_driver_procpu.dts`

- [ ] **Step 1: Add board DTS nodes**

In `deps/modules/lib/rosterloh-drivers/boards/waveshare/ros_driver/ros_driver_procpu.dts`, add aliases:

```dts
aliases {
	gimbal-pan = &gimbal_pan_servo;
	gimbal-tilt = &gimbal_tilt_servo;
};
```

Keep the existing `zephyr,console = &uart0;` and `zephyr,shell-uart = &uart0;`.

Under `&uart1`, add:

```dts
&uart1 {
	status = "okay";
	current-speed = <115200>;
	pinctrl-0 = <&uart1_default>;
	pinctrl-names = "default";

	gimbal_servo_bus: bus-servo {
		compatible = "waveshare,bus-servo";
		status = "okay";

		gimbal_pan_servo: servo@2 {
			compatible = "rosterloh,actuator-bus-servo";
			reg = <2>;
			label = "Gimbal pan";
			default-mode = "position";
			ticks-per-rev = <4096>;
			rad-zero-tick = <2047>;
			position-min-rad-milli = <-3142>;
			position-max-rad-milli = <3142>;
			speed = <300>;
			accel = <20>;
		};

		gimbal_tilt_servo: servo@1 {
			compatible = "rosterloh,actuator-bus-servo";
			reg = <1>;
			label = "Gimbal tilt";
			default-mode = "position";
			ticks-per-rev = <4096>;
			rad-zero-tick = <2047>;
			position-min-rad-milli = <-524>;
			position-max-rad-milli = <1571>;
			invert-position;
			speed = <300>;
			accel = <20>;
		};
	};
};
```

- [ ] **Step 2: Build a driver-module smoke test that includes DTS**

Run:

```bash
uv run west twister -p native_sim -T deps/modules/lib/rosterloh-drivers/tests/drivers/actuator/bus_servo -i
```

Expected: PASS.

- [ ] **Step 3: Commit board DTS**

Run inside `deps/modules/lib/rosterloh-drivers`:

```bash
git add boards/waveshare/ros_driver/ros_driver_procpu.dts
git commit -m "boards/ros_driver: describe gimbal bus servos"
```

### Task 6: Add Rasprover Static Tests for UART Alias and Gimbal Defaults

**Files:**
- Modify: `tests/rasprover/test_ros_topics.py`

- [ ] **Step 1: Add failing static tests for the desired config**

Append to `tests/rasprover/test_ros_topics.py`:

```python
def test_rasprover_overlay_removes_stale_zenoh_serial_alias() -> None:
    overlay = (REPO_ROOT / "applications" / "rasprover" / "boards" / "ros_driver_esp32_procpu.overlay").read_text()

    assert "zenoh-serial" not in overlay
    assert "ssd1306_ssd1306_128x32" in overlay
    assert "ina219@42" in overlay


def test_gimbal_topic_defaults_to_joint_state_command() -> None:
    kconfig = (REPO_ROOT / "applications" / "rasprover" / "Kconfig").read_text()
    app_zenoh = (REPO_ROOT / "applications" / "rasprover" / "src" / "app_zenoh.c").read_text()

    assert "config APP_GIMBAL" in kconfig
    assert "config APP_ZENOH_GIMBAL_CMD_KEY" in kconfig
    assert 'default "rt/rasprover/gimbal_cmd"' in kconfig
    assert '#define GIMBAL_CMD_KEY          CONFIG_APP_ZENOH_GIMBAL_CMD_KEY' in app_zenoh
    assert "pan_joint" in (REPO_ROOT / "applications" / "rasprover" / "src" / "app_gimbal.c").read_text()
    assert "tilt_joint" in (REPO_ROOT / "applications" / "rasprover" / "src" / "app_gimbal.c").read_text()
```

- [ ] **Step 2: Run tests and verify they fail**

Run:

```bash
uv run pytest tests/rasprover/test_ros_topics.py
```

Expected: FAIL because the overlay still contains `zenoh-serial`, `APP_GIMBAL` does not exist, and `app_gimbal.c` does not exist.

- [ ] **Step 3: Commit the red rasprover config tests**

Run:

```bash
rtk git add tests/rasprover/test_ros_topics.py
rtk git commit -m "tests/rasprover: pin gimbal topic and uart config"
```

### Task 7: Add JointState Command CDR Decoder

**Files:**
- Modify: `tests/rasprover/test_ros_cdr.py`
- Modify: `applications/rasprover/src/app_ros_cdr.h`
- Modify: `applications/rasprover/src/app_ros_cdr.c`

- [ ] **Step 1: Add failing host tests for decoding gimbal JointState commands**

Append to `tests/rasprover/test_ros_cdr.py`:

```python
def _joint_state_payload(names: list[str], positions: list[float]) -> bytes:
    payload = bytearray(b"\x00\x01\x00\x00")
    payload += struct.pack("<II", 0, 0)
    payload += struct.pack("<I", 1)
    payload += b"\x00\x00\x00\x00"
    payload += struct.pack("<I", len(names))
    for name in names:
        encoded = name.encode() + b"\x00"
        payload += struct.pack("<I", len(encoded))
        payload += encoded
        while (len(payload) - 4) % 4:
            payload += b"\x00"
    payload += struct.pack("<I", len(positions))
    while (len(payload) - 4) % 8:
        payload += b"\x00"
    payload += struct.pack("<" + "d" * len(positions), *positions)
    payload += struct.pack("<I", 0)
    payload += struct.pack("<I", 0)
    return bytes(payload)


def test_joint_state_decoder_extracts_named_gimbal_positions(tmp_path: Path) -> None:
    payload = _joint_state_payload(["tilt_joint", "pan_joint"], [0.25, -0.5])
    source = f"""
    #include "app_ros_cdr.h"
    #include <stdint.h>
    #include <stdio.h>

    static const uint8_t payload[] = {{{", ".join(str(b) for b in payload)}}};

    int main(void)
    {{
        struct app_ros_joint_command cmd;
        if (!app_ros_decode_joint_command(payload, sizeof(payload), &cmd)) {{
            return 1;
        }}
        printf("%d %.3f %.3f\\n", cmd.has_pan && cmd.has_tilt, cmd.pan_position, cmd.tilt_position);
        return 0;
    }}
    """

    output = _compile_and_run(tmp_path, source)
    assert output == b"1 -0.500 0.250\n"


def test_joint_state_decoder_rejects_missing_tilt(tmp_path: Path) -> None:
    payload = _joint_state_payload(["pan_joint"], [0.0])
    source = f"""
    #include "app_ros_cdr.h"
    #include <stdint.h>

    static const uint8_t payload[] = {{{", ".join(str(b) for b in payload)}}};

    int main(void)
    {{
        struct app_ros_joint_command cmd;
        return app_ros_decode_joint_command(payload, sizeof(payload), &cmd) ? 1 : 0;
    }}
    """

    _compile_and_run(tmp_path, source)
```

- [ ] **Step 2: Run CDR tests and verify they fail**

Run:

```bash
uv run pytest tests/rasprover/test_ros_cdr.py
```

Expected: FAIL because `struct app_ros_joint_command` and `app_ros_decode_joint_command()` do not exist.

- [ ] **Step 3: Add decoder API**

In `applications/rasprover/src/app_ros_cdr.h`, add:

```c
struct app_ros_joint_command {
	bool has_pan;
	bool has_tilt;
	double pan_position;
	double tilt_position;
};

bool app_ros_decode_joint_command(const uint8_t *buf, size_t len,
				  struct app_ros_joint_command *out);
```

Also include `<stdbool.h>` in the header.

- [ ] **Step 4: Implement a bounded JointState command decoder**

In `applications/rasprover/src/app_ros_cdr.c`, add a reader beside the writer helpers:

```c
struct cdr_reader {
	const uint8_t *buf;
	size_t len;
	size_t off;
	bool ok;
};

static size_t cdr_read_rel(const struct cdr_reader *r)
{
	return (r->off >= CDR_HEADER_SIZE) ? (r->off - CDR_HEADER_SIZE) : 0;
}

static void cdr_read_align(struct cdr_reader *r, size_t alignment)
{
	size_t rem = cdr_read_rel(r) % alignment;
	if (rem != 0) {
		r->off += alignment - rem;
	}
	if (r->off > r->len) {
		r->ok = false;
	}
}
```

Then implement these helpers:

- `cdr_read_u32()`
- `cdr_read_f64()`
- `cdr_skip_string()`
- `cdr_read_string_match()`

`app_ros_decode_joint_command()` must:

- validate `buf != NULL`, `out != NULL`, `len >= 4`, and `buf[0..3] == {0x00, 0x01, 0x00, 0x00}`
- skip `stamp.sec`, `stamp.nanosec`, and `frame_id`
- read up to 8 joint names into local flags for `pan_joint` and `tilt_joint`
- read the position sequence
- require `position_count == name_count`
- set `out->pan_position` and `out->tilt_position`
- return `true` only when both target joints are present
- ignore velocity and effort sequences after the position sequence

- [ ] **Step 5: Run CDR tests and verify green**

Run:

```bash
uv run pytest tests/rasprover/test_ros_cdr.py
```

Expected: PASS.

- [ ] **Step 6: Commit the decoder**

Run:

```bash
rtk git add tests/rasprover/test_ros_cdr.py applications/rasprover/src/app_ros_cdr.h applications/rasprover/src/app_ros_cdr.c
rtk git commit -m "rasprover: decode gimbal joint state commands"
```

### Task 8: Add `app_gimbal` and Native Sim Fake Actuators

**Files:**
- Create: `applications/rasprover/src/app_gimbal.h`
- Create: `applications/rasprover/src/app_gimbal.c`
- Modify: `applications/rasprover/CMakeLists.txt`
- Modify: `applications/rasprover/Kconfig`
- Modify: `applications/rasprover/prj.conf`
- Modify: `applications/rasprover/src/main.c`
- Modify: `applications/rasprover/boards/native_sim_native_64.overlay`
- Modify: `applications/rasprover/boards/native_sim_native_64.conf`
- Modify: `applications/rasprover/boards/ros_driver_esp32_procpu.overlay`

- [ ] **Step 1: Run the existing static tests and confirm the Task 6 failures remain**

Run:

```bash
uv run pytest tests/rasprover/test_ros_topics.py
```

Expected: FAIL for missing `APP_GIMBAL`, stale `zenoh-serial`, and missing `app_gimbal.c`.

- [ ] **Step 2: Add gimbal API header**

`applications/rasprover/src/app_gimbal.h`:

```c
#ifndef __APP_GIMBAL_H__
#define __APP_GIMBAL_H__

#include <stdbool.h>

#define APP_GIMBAL_JOINT_COUNT 2

struct app_gimbal_joint_state {
	const char *name;
	double position_rad;
	double velocity_rad_s;
};

bool app_gimbal_init(void);
void app_gimbal_set_positions(float pan_rad, float tilt_rad);
bool app_gimbal_read_joint_state(struct app_gimbal_joint_state joints[APP_GIMBAL_JOINT_COUNT]);

#endif /* __APP_GIMBAL_H__ */
```

- [ ] **Step 3: Implement `app_gimbal.c`**

`applications/rasprover/src/app_gimbal.c`:

```c
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_gimbal, LOG_LEVEL_INF);

#include <zephyr/actuator/actuator.h>
#include <zephyr/device.h>

#include "app_gimbal.h"

static const struct device *const pan = DEVICE_DT_GET(DT_ALIAS(gimbal_pan));
static const struct device *const tilt = DEVICE_DT_GET(DT_ALIAS(gimbal_tilt));
static bool ready;

static bool read_joint(const struct device *dev, const char *name,
		       struct app_gimbal_joint_state *joint)
{
	struct actuator_feedback fb;

	if (actuator_read_feedback(dev, &fb) != 0 || !(fb.valid_mask & ACTUATOR_FB_POSITION)) {
		return false;
	}

	*joint = (struct app_gimbal_joint_state){
		.name = name,
		.position_rad = fb.position,
		.velocity_rad_s = (fb.valid_mask & ACTUATOR_FB_VELOCITY) ? fb.velocity : 0.0,
	};
	return true;
}

bool app_gimbal_init(void)
{
	if (!device_is_ready(pan) || !device_is_ready(tilt)) {
		LOG_ERR("gimbal devices not ready");
		return false;
	}

	if (actuator_enable(pan) != 0 || actuator_enable(tilt) != 0) {
		LOG_ERR("gimbal enable failed");
		return false;
	}

	ready = true;
	LOG_INF("gimbal ready");
	return true;
}

void app_gimbal_set_positions(float pan_rad, float tilt_rad)
{
	if (!ready) {
		return;
	}

	if (actuator_set_position(pan, pan_rad) != 0) {
		LOG_WRN("pan_joint setpoint failed");
	}
	if (actuator_set_position(tilt, tilt_rad) != 0) {
		LOG_WRN("tilt_joint setpoint failed");
	}
}

bool app_gimbal_read_joint_state(struct app_gimbal_joint_state joints[APP_GIMBAL_JOINT_COUNT])
{
	if (!ready || joints == NULL) {
		return false;
	}

	return read_joint(pan, "pan_joint", &joints[0]) &&
	       read_joint(tilt, "tilt_joint", &joints[1]);
}
```

- [ ] **Step 4: Wire Kconfig, CMake, prj.conf, and main**

In `applications/rasprover/CMakeLists.txt`:

```cmake
target_sources_ifdef(CONFIG_APP_GIMBAL app PRIVATE src/app_gimbal.c)
```

In `applications/rasprover/Kconfig`, after `APP_MOTORS`:

```kconfig
menuconfig APP_GIMBAL
	bool "Enable gimbal bus-servo control"
	default y
	depends on ACTUATOR
	help
	  Controls the Waveshare pan/tilt gimbal through two actuator devices.

if APP_GIMBAL

config APP_ZENOH_GIMBAL_CMD_KEY
	string "Zenoh key expression for gimbal JointState commands"
	default "rt/rasprover/gimbal_cmd"
	depends on APP_ZENOH
	help
	  Zenoh key used for sensor_msgs/JointState gimbal position commands.
	  Keep this aligned with APP_ZENOH_KEY_PREFIX unless intentionally
	  routing the gimbal command topic elsewhere.

endif # APP_GIMBAL
```

In `applications/rasprover/prj.conf`:

```conf
CONFIG_BUS_SERVO=y
CONFIG_ACTUATOR_BUS_SERVO=y
CONFIG_UART_INTERRUPT_DRIVEN=y
CONFIG_APP_GIMBAL=y
```

In `applications/rasprover/src/main.c`:

```c
#include "app_gimbal.h"
```

and after `app_motors_init()`:

```c
#ifdef CONFIG_APP_GIMBAL
	app_gimbal_init();
#endif
```

- [ ] **Step 5: Update overlays**

In `applications/rasprover/boards/ros_driver_esp32_procpu.overlay`, remove:

```dts
/ {
	aliases {
		zenoh-serial = &uart1;
	};
};
```

In `applications/rasprover/boards/native_sim_native_64.overlay`, add aliases and fake nodes:

```dts
/ {
	aliases {
		gimbal-pan = &fake_gimbal_pan;
		gimbal-tilt = &fake_gimbal_tilt;
	};

	fake_gimbal_pan: fake-gimbal-pan {
		compatible = "rosterloh,actuator-fake";
	};

	fake_gimbal_tilt: fake-gimbal-tilt {
		compatible = "rosterloh,actuator-fake";
	};
};
```

If the overlay already has an `/ { aliases { ... }; };` block for motors, merge the gimbal aliases into that existing block instead of creating a duplicate root node.

In `applications/rasprover/boards/native_sim_native_64.conf`:

```conf
CONFIG_BUS_SERVO=n
CONFIG_ACTUATOR_BUS_SERVO=n
CONFIG_APP_GIMBAL=y
```

- [ ] **Step 6: Run static tests and native sim build**

Run:

```bash
uv run pytest tests/rasprover/test_ros_topics.py
uv run poe app rasprover --board native_sim/native/64
```

Expected: pytest PASS. Native sim build PASS.

- [ ] **Step 7: Commit gimbal app facade**

Run:

```bash
rtk git add applications/rasprover/src/app_gimbal.* applications/rasprover/CMakeLists.txt applications/rasprover/Kconfig applications/rasprover/prj.conf applications/rasprover/src/main.c applications/rasprover/boards tests/rasprover/test_ros_topics.py
rtk git commit -m "rasprover: add gimbal actuator facade"
```

### Task 9: Integrate Gimbal With Zenoh JointState Publish and Command Subscribe

**Files:**
- Modify: `applications/rasprover/src/app_zenoh.c`
- Modify: `tests/rasprover/test_ros_topics.py`
- Modify: `tests/rasprover/test_ros_cdr.py`

- [ ] **Step 1: Add a static test for gimbal zenoh command wiring**

Append to `tests/rasprover/test_ros_topics.py`:

```python
def test_gimbal_command_subscriber_is_declared() -> None:
    app_zenoh = (REPO_ROOT / "applications" / "rasprover" / "src" / "app_zenoh.c").read_text()

    assert "GIMBAL_CMD_KEY" in app_zenoh
    assert "declare_gimbal_cmd_subscriber" in app_zenoh
    assert "app_ros_decode_joint_command" in app_zenoh
    assert "app_gimbal_set_positions" in app_zenoh
```

- [ ] **Step 2: Run tests and verify failure**

Run:

```bash
uv run pytest tests/rasprover/test_ros_topics.py
```

Expected: FAIL because `app_zenoh.c` does not yet include gimbal command subscriber code.

- [ ] **Step 3: Add gimbal command subscriber**

In `applications/rasprover/src/app_zenoh.c`, add:

```c
#ifdef CONFIG_APP_GIMBAL
#include "app_gimbal.h"
#endif
```

Add macros and state:

```c
#ifdef CONFIG_APP_GIMBAL
#define GIMBAL_CMD_KEY CONFIG_APP_ZENOH_GIMBAL_CMD_KEY
static z_owned_subscriber_t _sub_gimbal_cmd;
#endif
```

Add handler:

```c
#ifdef CONFIG_APP_GIMBAL
static void gimbal_cmd_handler(z_loaned_sample_t *sample, void *arg)
{
	ARG_UNUSED(arg);

	z_owned_slice_t slice;
	if (z_bytes_to_slice(z_sample_payload(sample), &slice) < 0) {
		return;
	}

	const uint8_t *buf = z_slice_data(z_loan(slice));
	size_t len = z_slice_len(z_loan(slice));
	struct app_ros_joint_command cmd;

	if (!app_ros_decode_joint_command(buf, len, &cmd)) {
		LOG_WRN("gimbal_cmd: bad JointState payload (len %zu)", len);
		z_drop(z_move(slice));
		return;
	}
	z_drop(z_move(slice));

	app_gimbal_set_positions((float)cmd.pan_position, (float)cmd.tilt_position);
}

static bool declare_gimbal_cmd_subscriber(void)
{
	z_view_keyexpr_t ke;
	z_view_keyexpr_from_str_unchecked(&ke, GIMBAL_CMD_KEY);

	z_owned_closure_sample_t callback;
	z_closure(&callback, gimbal_cmd_handler, NULL, NULL);

	if (z_declare_subscriber(z_loan(_session), &_sub_gimbal_cmd, z_loan(ke),
				 z_move(callback), NULL) < 0) {
		LOG_ERR("zenoh subscriber declare failed for '%s'", GIMBAL_CMD_KEY);
		return false;
	}

	LOG_INF("zenoh subscribed to '%s'", GIMBAL_CMD_KEY);
	return true;
}
#endif
```

Call it in `app_zenoh_init()` after `declare_cmd_vel_subscriber()`:

```c
#ifdef CONFIG_APP_GIMBAL
	if (!declare_gimbal_cmd_subscriber()) {
		LOG_WRN("continuing without gimbal command subscription");
	}
#endif
```

- [ ] **Step 4: Expand JointState publication to include gimbal joints**

In `joint_state_work_handler()`, change the local arrays so they include motors and gimbal:

```c
#define APP_JOINT_STATE_COUNT \
	(APP_MOTORS_JOINT_COUNT + COND_CODE_1(CONFIG_APP_GIMBAL, (APP_GIMBAL_JOINT_COUNT), (0)))
#define CDR_JOINT_STATE_MAX_SIZE 256
```

Inside the handler, read motor joints first, then gimbal joints if enabled:

```c
struct app_ros_joint_sample joints[APP_JOINT_STATE_COUNT];
size_t joint_count = 0;

for (size_t i = 0; i < ARRAY_SIZE(motor_joints); i++) {
	joints[joint_count++] = (struct app_ros_joint_sample){
		.name = motor_joints[i].name,
		.position = motor_joints[i].position_rad,
		.velocity = motor_joints[i].velocity_rad_s,
	};
}

#ifdef CONFIG_APP_GIMBAL
struct app_gimbal_joint_state gimbal_joints[APP_GIMBAL_JOINT_COUNT];
if (!app_gimbal_read_joint_state(gimbal_joints)) {
	LOG_WRN("gimbal joint feedback unavailable");
	goto reschedule;
}
for (size_t i = 0; i < ARRAY_SIZE(gimbal_joints); i++) {
	joints[joint_count++] = (struct app_ros_joint_sample){
		.name = gimbal_joints[i].name,
		.position = gimbal_joints[i].position_rad,
		.velocity = gimbal_joints[i].velocity_rad_s,
	};
}
#endif
```

Add a `reschedule:` label before the existing `k_work_reschedule()` call.

- [ ] **Step 5: Run tests and builds**

Run:

```bash
uv run pytest tests/rasprover
uv run poe app rasprover --board native_sim/native/64
```

Expected: pytest PASS. Native sim build PASS.

- [ ] **Step 6: Commit zenoh integration**

Run:

```bash
rtk git add applications/rasprover/src/app_zenoh.c tests/rasprover
rtk git commit -m "rasprover: bridge gimbal joint commands over zenoh"
```

### Task 10: Hardware Build, Formatting, and Documentation

**Files:**
- Modify: `applications/rasprover/README.md`
- Modify: `applications/rasprover/CHANGELOG.md`

- [ ] **Step 1: Update rasprover README ROS table**

In `applications/rasprover/README.md`, update the ROS topics table to include:

```markdown
| `rt/rasprover/gimbal_cmd` | `/rasprover/gimbal_cmd` | `sensor_msgs/msg/JointState` |
```

Add a short command example:

```shell
ros2 topic pub /rasprover/gimbal_cmd sensor_msgs/msg/JointState \
  "{name: ['pan_joint', 'tilt_joint'], position: [0.0, 0.0]}"
```

Update the JointState field section so `name` lists:

```markdown
`left_wheel_joint`, `right_wheel_joint`, `pan_joint`, `tilt_joint`
```

- [ ] **Step 2: Update changelog**

In `applications/rasprover/CHANGELOG.md`, add an Unreleased bullet:

```markdown
- Add Waveshare gimbal support with bus-servo pan/tilt actuator feedback and
  `/rasprover/gimbal_cmd` JointState position commands over zenoh.
```

- [ ] **Step 3: Run format checks**

Run:

```bash
uv run clang-format --dry-run --Werror \
  applications/rasprover/src/app_gimbal.c \
  applications/rasprover/src/app_gimbal.h \
  applications/rasprover/src/app_ros_cdr.c \
  applications/rasprover/src/app_ros_cdr.h \
  applications/rasprover/src/app_zenoh.c \
  applications/rasprover/src/main.c \
  deps/modules/lib/rosterloh-drivers/include/drivers/bus_servo.h \
  deps/modules/lib/rosterloh-drivers/drivers/bus_servo/bus_servo.c \
  deps/modules/lib/rosterloh-drivers/drivers/bus_servo/bus_servo_internal.h \
  deps/modules/lib/rosterloh-drivers/drivers/bus_servo/bus_servo_serial.c \
  deps/modules/lib/rosterloh-drivers/drivers/actuator/bus_servo/actuator_bus_servo.c
```

Expected: command exits 0 with no output.

- [ ] **Step 4: Run full relevant verification**

Run:

```bash
uv run pytest tests/rasprover
uv run west twister -p native_sim -T deps/modules/lib/rosterloh-drivers/tests/drivers/bus_servo -i
uv run west twister -p native_sim -T deps/modules/lib/rosterloh-drivers/tests/drivers/actuator/bus_servo -i
uv run poe app rasprover --board native_sim/native/64
uv run poe agent-build rasprover --sysbuild
```

Expected:

- pytest PASS
- both twister suites PASS
- native sim build PASS
- hardware sysbuild PASS

- [ ] **Step 5: Commit docs**

Run:

```bash
rtk git add applications/rasprover/README.md applications/rasprover/CHANGELOG.md
rtk git commit -m "docs: document rasprover gimbal control"
```

- [ ] **Step 6: Summarize cross-repo state**

Run:

```bash
rtk git status --short
rtk git -C deps/modules/lib/rosterloh-drivers status --short
rtk git log --oneline -5
rtk git -C deps/modules/lib/rosterloh-drivers log --oneline -5
```

Expected:

- workspace status is clean or only expected local plan/docs changes remain
- module status is clean
- recent commits include the rasprover gimbal commits and the bus-servo module commits

## Self-Review Checklist

- Spec coverage:
  - Bus-servo protocol driver: Tasks 1-3.
  - Actuator backend: Task 4.
  - UART0 debug and UART1 servo bus DTS: Task 5 and Task 8.
  - Removal of stale `zenoh-serial`: Task 6 and Task 8.
  - `pan_joint` / `tilt_joint` JointState command: Tasks 7 and 9.
  - Expanded `/joint_states`: Task 9.
  - Native sim fake actuators: Task 8.
  - Tests/build/docs: Task 10.
- Marker scan: no banned markers or cross-task shorthand steps are intentional in this plan.
- Type consistency:
  - Driver API uses `bus_servo_*`.
  - App command struct is `struct app_ros_joint_command`.
  - Gimbal API uses `app_gimbal_*`.
  - Joint names are `pan_joint` and `tilt_joint`.
