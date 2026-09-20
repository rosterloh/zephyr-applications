# protective_stop Safety Core (Slice 2) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A `native_sim` build of `applications/protective_stop` that bonds, heartbeats and completes an arming gesture against upstream's real `machine_app` over host loopback.

**Architecture:** Vendor upstream's `pstop_c` protocol library via west and compile its remote-side sources directly, porting only `time_get_now()`. Two sampler threads independently read one pole each of a dual-channel loopback stop switch and encode a 48-byte message per machine slot; a comparator thread `memcmp`s the two encodings and transmits only on an exact match. Peer table and role persist through the Settings API on a ZMS backend.

**Tech Stack:** Zephyr (`native_sim/native/64`), `pstop_c` (Apache-2.0, C99), `CONFIG_NET_NATIVE_OFFLOADED_SOCKETS`, `CONFIG_ZMS` + `CONFIG_SETTINGS_ZMS`, `gpio_emul`, ztest/twister, mise + west + uv.

**Spec:** `docs/superpowers/specs/2026-09-13-protective-stop-design.md`

## Global Constraints

- Python/west: never call `west`, `python` or `pytest` bare. Use `mise run <task>` or `mise x -- <cmd>`. Never activate `.venv`.
- Builds: `mise run agent-build protective_stop --board native_sim/native/64`. Never create a top-level `build/`.
- Upstream pin: `polymathrobotics/protective-stop` @ `4ce8094a060544ca8a2be1084c711a41445af221`. Pinned to a SHA, never `main`.
- `PSTOP_MESSAGE_SIZE` is **48** bytes, little-endian, CRC-16 (poly `0x8D95`, init `0xFFFF`) over the first 46.
- Message codes: `PSTOP_MESSAGE_OK 0x55`, `PSTOP_MESSAGE_STOP 0x92`, `PSTOP_MESSAGE_BOND 0xAD`, `PSTOP_MESSAGE_UNBOND 0x6A`, `PSTOP_MESSAGE_UNKNOWN 0x0F`.
- `PSTOP_MAX_MACHINES` is 4. Local UDP ports 8891–8894, slot order.
- Lockstep tick: 10 Hz. Sampler publish deadline: 80 ms. Send period: `heartbeat_timeout / 2` clamped to [100, 1000] ms.
- Bond retry 5000 ms. Rebond watchdog floor 2500 ms, `REBOND_MACHINE_MAX_MISSED` 5, jitter margin 500 ms.
- Debounce: `LOOP_RECLOSE_DEBOUNCE_TICKS` 3, `LOOP_BOOT_OPEN_CONFIRM_TICKS` 5.
- C style: clang-format against the in-tree `.clang-format`. Verify with `mise x -- clang-format --dry-run --Werror <files>`.
- Do not modify anything under `deps/` and commit it here; west owns those paths.
- Every commit message ends with `Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>`.

## File Structure

| File | Responsibility |
|---|---|
| `west.yml` | adds the pinned `protective-stop` project |
| `mise.toml` | registers `protective_stop` in the `app` task's board case |
| `applications/protective_stop/pstop_c.cmake` | locates `pstop_c`, lists the 6 remote-side sources; included by the app **and** by each test |
| `applications/protective_stop/src/pstop_time.c` | the entire OS port: `time_get_now()` |
| `applications/protective_stop/src/estop_verdict.{c,h}` | pure verdict/debounce/priming logic, no HAL — unit-testable |
| `applications/protective_stop/src/estop_gpio.{c,h}` | drives and samples both loopback phases; simulated pole under `gpio_emul` |
| `applications/protective_stop/src/app_settings.{c,h}` | ZMS-backed peer table, role, device id |
| `applications/protective_stop/src/session.{c,h}` | one machine slot: socket, bond FSM, counters, rebond watchdog |
| `applications/protective_stop/src/lockstep.{c,h}` | two sampler threads + comparator thread |
| `applications/protective_stop/src/main.c` | init order and thread start |
| `applications/protective_stop/tests/wire/` | golden 48-byte frames |
| `applications/protective_stop/tests/verdict/` | `estop_decide` truth table |

**Ownership rule that shapes the design:** the message `stamp` and `counter` must be *identical* in both samplers' encodings, or the comparator fails every tick. So the comparator snapshots `now_ms` and decides which slots send, then wakes the samplers; the counter lives in the session and is advanced only by the comparator after a successful transmit. Samplers diverge **only** in which physical channel they read and which expression they use to form the verdict.

---

### Task 1: West project, app skeleton, time port, golden wire frames

**Files:**
- Modify: `west.yml`
- Modify: `mise.toml:348-359` (the `app` task `case`)
- Create: `applications/protective_stop/pstop_c.cmake`
- Create: `applications/protective_stop/CMakeLists.txt`
- Create: `applications/protective_stop/prj.conf`
- Create: `applications/protective_stop/Kconfig`
- Create: `applications/protective_stop/VERSION`
- Create: `applications/protective_stop/README.md`
- Create: `applications/protective_stop/src/pstop_time.c`
- Create: `applications/protective_stop/src/main.c`
- Create: `applications/protective_stop/boards/native_sim_native_64.conf`
- Test: `applications/protective_stop/tests/wire/{CMakeLists.txt,prj.conf,testcase.yaml,src/main.c}`

**Interfaces:**
- Consumes: nothing.
- Produces: `uint64_t time_get_now(void)` (declares in `pstop/time.h`, defined by us). CMake variables `PSTOP_C_SRCS` (list) and `PSTOP_C_INCLUDE` (path) from `pstop_c.cmake`.

- [ ] **Step 1: Add the pinned west project**

In `west.yml`, append to `projects:` (top level, a sibling of `rosterloh-drivers` — *not* inside the zephyr `import:` allowlist, which governs Zephyr's own modules):

```yaml
    # pstop_c: the Apache-2.0 protective-stop protocol library. Pinned to a
    # SHA, not main: this is safety-relevant code and an upstream change to
    # the state machine must be a deliberate, reviewed bump.
    - name: protective-stop
      url: https://github.com/polymathrobotics/protective-stop.git
      revision: 4ce8094a060544ca8a2be1084c711a41445af221
      path: deps/modules/lib/protective-stop
```

- [ ] **Step 2: Fetch it**

Run: `mise run west-update`
Expected: `deps/modules/lib/protective-stop/pstop_c/pstop/include/pstop/pstop_msg.h` exists.

Verify: `ls deps/modules/lib/protective-stop/pstop_c/pstop/src/pstop/`

- [ ] **Step 3: Write the pstop_c locator**

Create `applications/protective_stop/pstop_c.cmake`:

```cmake
# SPDX-License-Identifier: Apache-2.0
#
# Locate and compile the vendored pstop_c core. Mirrors upstream's own ESP-IDF
# wrapper (components/pstop/CMakeLists.txt): pstop_c is not a Zephyr module
# (it has no zephyr/module.yml upstream yet), so we reference its path directly.
#
# Included by the app CMakeLists AND by each test CMakeLists, so the source
# list lives in exactly one place.

set(PSTOP_C "${WEST_TOPDIR}/deps/modules/lib/protective-stop/pstop_c")

if(NOT EXISTS "${PSTOP_C}/pstop/include/pstop/pstop_msg.h")
  message(FATAL_ERROR
    "pstop_c not found at ${PSTOP_C}\n"
    "Run: mise run west-update")
endif()

# A REMOTE needs 6 of the 11 core sources. machine.c, protocol.c,
# pstop_application.c and pstop_remote_data.c are machine-side and are
# deliberately not compiled. Upstream's time.c only implements __linux__ and
# returns 0 elsewhere, so src/pstop_time.c replaces it.
set(PSTOP_C_SRCS
  "${PSTOP_C}/pstop/src/pstop/checksum.c"
  "${PSTOP_C}/pstop/src/pstop/device_id.c"
  "${PSTOP_C}/pstop/src/pstop/endian.c"
  "${PSTOP_C}/pstop/src/pstop/os.c"
  "${PSTOP_C}/pstop/src/pstop/protocol_data.c"
  "${PSTOP_C}/pstop/src/pstop/pstop_msg.c"
)

set(PSTOP_C_INCLUDE "${PSTOP_C}/pstop/include")
```

- [ ] **Step 4: Write the failing golden-frame test**

Create `applications/protective_stop/tests/wire/src/main.c`:

```c
/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Golden-byte tests for the pstop wire format.
 *
 * Why golden bytes: a wrong field offset does not fail the build, it fails at
 * the machine, which rejects the frame on checksum and silently never bonds.
 * These arrays were produced by upstream's OWN encoder (pstop_c @ 4ce8094)
 * compiled on the host, so they pin the format as the machine will read it,
 * independently of whatever our build happens to do.
 *
 * Layout being pinned (little-endian, 48 bytes):
 *   [0]     version u8          [1]     message u8
 *   [2:10]  stamp u64           [10:18] received_stamp u64
 *   [18:22] id u32              [22:26] receiver_id u32
 *   [26:30] heartbeat_timeout   [30:34] counter u32
 *   [34:38] received_counter    [38:42] padding1  [42:46] padding2
 *   [46:48] crc16 over [0:46], poly 0x8D95, init 0xFFFF
 */

#include <string.h>
#include <zephyr/ztest.h>

#include "pstop/device_id.h"
#include "pstop/pstop_msg.h"

#define ME_ID      0xC0A80105U /* 192.168.1.5 */
#define MACHINE_ID 0x01020304U

static const uint8_t golden_bond[PSTOP_MESSAGE_SIZE] = {
	0x02, 0xAD, 0x34, 0x12, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x05, 0x01, 0xA8, 0xC0, 0x04, 0x03,
	0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x65, 0x2A,
};

static const uint8_t golden_ok[PSTOP_MESSAGE_SIZE] = {
	0x02, 0x55, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x05, 0x01, 0xA8, 0xC0, 0x04, 0x03,
	0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00,
	0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x86, 0x48,
};

static const uint8_t golden_stop[PSTOP_MESSAGE_SIZE] = {
	0x02, 0x92, 0x00, 0x30, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x2F, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x05, 0x01, 0xA8, 0xC0, 0x04, 0x03,
	0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00,
	0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xB1, 0x66,
};

ZTEST(pstop_wire, test_message_size_is_48)
{
	zassert_equal(PSTOP_MESSAGE_SIZE, 48U, "wire size must be 48");
}

ZTEST(pstop_wire, test_encode_bond_matches_golden)
{
	device_id_t me = {.data = ME_ID};
	device_id_t machine = {.data = MACHINE_ID};
	pstop_msg_t msg;
	uint8_t buf[PSTOP_MESSAGE_SIZE];

	pstop_create_bond_message(&msg, 0x1234ULL, &me, &machine, 1U);
	pstop_message_encode(&msg, buf);

	zassert_mem_equal(buf, golden_bond, PSTOP_MESSAGE_SIZE, "BOND frame diverged");
}

ZTEST(pstop_wire, test_encode_ok_matches_golden)
{
	device_id_t me = {.data = ME_ID};
	device_id_t machine = {.data = MACHINE_ID};
	pstop_msg_t msg;
	uint8_t buf[PSTOP_MESSAGE_SIZE];

	pstop_create_ok_message(&msg, 0x2000ULL, 0x1F00ULL, &me, &machine, 7U, 5U);
	pstop_message_encode(&msg, buf);

	zassert_mem_equal(buf, golden_ok, PSTOP_MESSAGE_SIZE, "OK frame diverged");
}

ZTEST(pstop_wire, test_encode_stop_matches_golden)
{
	device_id_t me = {.data = ME_ID};
	device_id_t machine = {.data = MACHINE_ID};
	pstop_msg_t msg;
	uint8_t buf[PSTOP_MESSAGE_SIZE];

	pstop_create_stop_message(&msg, 0x3000ULL, 0x2F00ULL, &me, &machine, 8U, 6U);
	pstop_message_encode(&msg, buf);

	zassert_mem_equal(buf, golden_stop, PSTOP_MESSAGE_SIZE, "STOP frame diverged");
}

ZTEST(pstop_wire, test_decode_golden_ok_recovers_fields)
{
	pstop_msg_t msg;

	pstop_message_decode(&msg, golden_ok);

	zassert_equal(msg.version, 0x02U, "version");
	zassert_equal(msg.message, PSTOP_MESSAGE_OK, "message type");
	zassert_equal(msg.stamp, 0x2000ULL, "stamp");
	zassert_equal(msg.received_stamp, 0x1F00ULL, "received_stamp");
	zassert_equal(msg.id.data, ME_ID, "sender id");
	zassert_equal(msg.receiver_id.data, MACHINE_ID, "receiver id");
	zassert_equal(msg.counter, 7U, "counter");
	zassert_equal(msg.received_counter, 5U, "received_counter");
	zassert_equal(msg.checksum, msg.calculated_checksum, "crc must self-verify");
}

ZTEST(pstop_wire, test_corrupt_byte_breaks_checksum)
{
	uint8_t buf[PSTOP_MESSAGE_SIZE];
	pstop_msg_t msg;

	memcpy(buf, golden_ok, PSTOP_MESSAGE_SIZE);
	buf[30] ^= 0x01U; /* flip a counter bit */
	pstop_message_decode(&msg, buf);

	zassert_not_equal(msg.checksum, msg.calculated_checksum,
			  "a flipped payload bit must break the crc");
}

ZTEST_SUITE(pstop_wire, NULL, NULL, NULL, NULL, NULL);
```

- [ ] **Step 5: Add the test build files**

Create `applications/protective_stop/tests/wire/CMakeLists.txt`:

```cmake
# Golden-frame tests for the pstop wire format. Links only the pstop_c core
# (pure C, no kernel, no net), so this runs anywhere ztest runs.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pstop_wire)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../pstop_c.cmake)

target_sources(app PRIVATE src/main.c ${PSTOP_C_SRCS})
target_include_directories(app PRIVATE ${PSTOP_C_INCLUDE})
```

Create `applications/protective_stop/tests/wire/prj.conf`:

```
CONFIG_ZTEST=y
```

Create `applications/protective_stop/tests/wire/testcase.yaml`:

```yaml
tests:
  protective_stop.wire:
    tags: protective_stop pstop wire
    # The code under test is pure C. qemu_cortex_m3 is listed alongside
    # native_sim for the same reason as rasprover.ros_cdr: ztest registration
    # uses ELF iterable sections, so unit_testing does not build on macOS.
    platform_allow:
      - native_sim
      - qemu_cortex_m3
    integration_platforms:
      - native_sim
```

- [ ] **Step 6: Run the test**

Run: `mise x -- west twister -T applications/protective_stop/tests/wire -p native_sim --inline-logs`
Expected: PASS, 6 test cases.

This one test does not follow the usual fail-first cycle, and deliberately so: the code under test is vendored, not written here, and the golden arrays were produced by upstream's encoder rather than by our build. Writing a failing version first would only prove that a file we have not created yet does not exist. The assertion that matters is that *our* build of *upstream's* source reproduces bytes captured from *upstream's* build — which is exactly what a first green run demonstrates.

To convince yourself the test can fail, temporarily flip one byte in `golden_bond` and re-run; it must report a mismatch. Revert it afterwards.

- [ ] **Step 7: Write the time port**

Create `applications/protective_stop/src/pstop_time.c`:

```c
/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr port of pstop_c's time_get_now().
 *
 * Upstream's pstop_c/pstop/src/pstop/time.c implements this only for
 * __linux__ and returns 0 everywhere else, which would silently break every
 * heartbeat and timeout. pstop_c.cmake excludes that file so only this
 * definition is linked.
 *
 * Units: pstop expects milliseconds. k_uptime_get() returns milliseconds
 * since boot from the monotonic kernel clock -- no RTC or NTP
 * discontinuities, which is exactly what a timeout clock needs.
 */

#include <zephyr/kernel.h>

#include "pstop/time.h"

uint64_t time_get_now(void)
{
	return (uint64_t)k_uptime_get();
}
```

- [ ] **Step 8: Write the app skeleton**

Create `applications/protective_stop/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(protective_stop)

zephyr_compile_options(-fdiagnostics-color=always)

include(${CMAKE_CURRENT_SOURCE_DIR}/pstop_c.cmake)

target_sources(app PRIVATE
  src/main.c
  src/pstop_time.c
  ${PSTOP_C_SRCS}
)

target_include_directories(app PRIVATE ${PSTOP_C_INCLUDE} src)
```

Create `applications/protective_stop/Kconfig`:

```
# SPDX-License-Identifier: Apache-2.0

mainmenu "protective_stop"

source "Kconfig.zephyr"
```

Create `applications/protective_stop/VERSION`:

```
VERSION_MAJOR = 0
VERSION_MINOR = 1
PATCHLEVEL = 0
VERSION_TWEAK = 0
EXTRAVERSION =
```

Create `applications/protective_stop/prj.conf`:

```
CONFIG_LOG=y
CONFIG_LOG_MODE_DEFERRED=y

# pstop_c is C99 with designated initialisers and // comments.
CONFIG_STD_C99=y

CONFIG_NETWORKING=y
CONFIG_NET_IPV4=y
CONFIG_NET_IPV6=n
CONFIG_NET_UDP=y
CONFIG_NET_SOCKETS=y

CONFIG_GPIO=y

CONFIG_MAIN_STACK_SIZE=4096
```

Create `applications/protective_stop/boards/native_sim_native_64.conf`:

```
# Host-backed BSD sockets: zsock_* calls go straight to the host's syscalls,
# so this build talks to upstream's real machine_app over ordinary loopback
# with no TAP interface, no net-tools setup and no root.
CONFIG_NET_NATIVE_OFFLOADED_SOCKETS=y
CONFIG_NET_DRIVERS=y

# No L2 is needed -- offloaded sockets bypass the IP stack entirely.
CONFIG_NET_L2_ETHERNET=n
CONFIG_NET_IPV4=n
CONFIG_NET_UDP=n

CONFIG_GPIO_EMUL=y
```

Create `applications/protective_stop/src/main.c`:

```c
/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "pstop/config.h"
#include "pstop/time.h"

LOG_MODULE_REGISTER(pstop, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("protective_stop: pstop v%u, %u-byte frames, clock=%llu ms",
		(unsigned int)PSTOP_VERSION, (unsigned int)PSTOP_MESSAGE_SIZE,
		(unsigned long long)time_get_now());
	return 0;
}
```

- [ ] **Step 9: Register the app with the mise build task**

In `mise.toml`, add a case arm after the `data_collection)` line (around line 358):

```
    protective_stop)      ALLOWED="waveshare_esp32_s3_eth/esp32s3/procpu native_sim/native/64"; DEFAULT="native_sim/native/64" ;;
```

And add `protective_stop` to the `arg "<app>"` help string on line 341.

- [ ] **Step 10: Build and run**

Run: `mise run agent-build protective_stop --board native_sim/native/64`
Expected: build succeeds, `logs/protective_stop-build.log` written.

Run: `./builds/protective_stop/zephyr/zephyr.exe`
Expected: a line like `protective_stop: pstop v2, 48-byte frames, clock=1 ms`. A clock of `0` means the port is not linked — check that upstream's `time.c` is absent from `PSTOP_C_SRCS`.

- [ ] **Step 11: Write the README**

Create `applications/protective_stop/README.md`:

```markdown
# protective_stop

A Zephyr **remote** for the Polymath pstop protocol — the unit carrying the
physical stop switch. Interoperates with an upstream machine (`machn`, the
ROS 2 `protective_stop_machine` node, or the plain-C `host` app).

Design: `docs/superpowers/specs/2026-09-13-protective-stop-design.md`

## Build

    mise run app protective_stop                                    # native_sim
    mise run app protective_stop --board waveshare_esp32_s3_eth/esp32s3/procpu

## Protocol library

The pstop protocol is **not reimplemented here**. `pstop_c` is vendored via
west at `deps/modules/lib/protective-stop`, pinned to a SHA, and its
remote-side sources are compiled directly (see `pstop_c.cmake`). Our entire
port is `src/pstop_time.c`.

## Tests

    mise x -- west twister -T applications/protective_stop -p native_sim --inline-logs
```

- [ ] **Step 12: Format and commit**

```bash
mise x -- clang-format --dry-run --Werror \
  applications/protective_stop/src/*.c \
  applications/protective_stop/tests/wire/src/main.c
git add west.yml mise.toml applications/protective_stop
git commit -m "feat(protective_stop): vendor pstop_c and pin the wire format

Adds the protective-stop west project pinned to 4ce8094, an app skeleton
building on native_sim, and the Zephyr time port -- the only OS dependency
pstop_c has.

The golden frames in tests/wire were produced by upstream's own encoder
compiled on the host, so they pin the format as the machine reads it rather
than merely re-asserting whatever our build does.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: Pure stop-switch verdict logic

**Files:**
- Create: `applications/protective_stop/src/estop_verdict.h`
- Create: `applications/protective_stop/src/estop_verdict.c`
- Test: `applications/protective_stop/tests/verdict/{CMakeLists.txt,prj.conf,testcase.yaml,src/main.c}`
- Modify: `applications/protective_stop/CMakeLists.txt`

**Interfaces:**
- Consumes: `PSTOP_MESSAGE_OK`, `PSTOP_MESSAGE_STOP` from `pstop/pstop_msg.h` (Task 1).
- Produces:
  - `typedef struct {...} estop_state_t`
  - `void estop_state_init(estop_state_t *st)`
  - `uint8_t estop_decide(estop_state_t *st, int core_id, int rb_hi, int rb_lo)` — returns `PSTOP_MESSAGE_OK` or `PSTOP_MESSAGE_STOP`
  - `bool estop_channels_primed(const estop_state_t st[2])`

- [ ] **Step 1: Write the failing test**

Create `applications/protective_stop/tests/verdict/src/main.c`:

```c
/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Truth table for the SIL-critical stop-switch verdict core.
 *
 * The loop is healthy only when THIS tick's drive-high echoed high AND this
 * tick's drive-low echoed low. Everything else -- cut wire, stuck-high short,
 * dead input -- is STOP. The open->STOP edge is never filtered; only the
 * release direction is debounced.
 */

#include <zephyr/ztest.h>

#include "estop_verdict.h"
#include "pstop/pstop_msg.h"

/* Feed n healthy ticks and return the last verdict. */
static uint8_t run_healthy(estop_state_t *st, int core_id, unsigned int n)
{
	uint8_t msg = PSTOP_MESSAGE_STOP;

	for (unsigned int i = 0U; i < n; i++) {
		msg = estop_decide(st, core_id, 1, 0);
	}
	return msg;
}

ZTEST(estop_verdict, test_closed_loop_reports_ok_after_debounce)
{
	estop_state_t st;

	estop_state_init(&st);

	/* Not yet debounced. */
	zassert_equal(estop_decide(&st, 0, 1, 0), PSTOP_MESSAGE_STOP, "tick 1");
	zassert_equal(estop_decide(&st, 0, 1, 0), PSTOP_MESSAGE_STOP, "tick 2");
	/* Third consecutive healthy tick satisfies LOOP_RECLOSE_DEBOUNCE_TICKS. */
	zassert_equal(estop_decide(&st, 0, 1, 0), PSTOP_MESSAGE_OK, "tick 3");
}

ZTEST(estop_verdict, test_open_loop_stops_immediately)
{
	estop_state_t st;

	estop_state_init(&st);
	zassert_equal(run_healthy(&st, 0, 5U), PSTOP_MESSAGE_OK, "should be armed");

	/* Cut wire / button pressed: drive high, read low. Single tick, no filter. */
	zassert_equal(estop_decide(&st, 0, 0, 0), PSTOP_MESSAGE_STOP, "open must stop at once");
}

ZTEST(estop_verdict, test_stuck_high_input_is_stop)
{
	estop_state_t st;

	estop_state_init(&st);
	/* IN shorted high: drive-low phase still reads 1. Never healthy. */
	for (unsigned int i = 0U; i < 10U; i++) {
		zassert_equal(estop_decide(&st, 0, 1, 1), PSTOP_MESSAGE_STOP,
			      "stuck-high must never report OK");
	}
}

ZTEST(estop_verdict, test_dead_input_is_stop)
{
	estop_state_t st;

	estop_state_init(&st);
	for (unsigned int i = 0U; i < 10U; i++) {
		zassert_equal(estop_decide(&st, 0, 0, 1), PSTOP_MESSAGE_STOP,
			      "inverted/dead input must never report OK");
	}
}

ZTEST(estop_verdict, test_release_is_debounced_but_stop_is_not)
{
	estop_state_t st;

	estop_state_init(&st);
	(void)run_healthy(&st, 0, 3U);

	zassert_equal(estop_decide(&st, 0, 0, 0), PSTOP_MESSAGE_STOP, "blip opens");
	/* One healthy tick is not enough to re-arm. */
	zassert_equal(estop_decide(&st, 0, 1, 0), PSTOP_MESSAGE_STOP, "reclose tick 1");
	zassert_equal(estop_decide(&st, 0, 1, 0), PSTOP_MESSAGE_STOP, "reclose tick 2");
	zassert_equal(estop_decide(&st, 0, 1, 0), PSTOP_MESSAGE_OK, "reclose tick 3");
}

ZTEST(estop_verdict, test_both_cores_agree_on_every_input)
{
	/* Core 0 forms the verdict arithmetically, core 1 by boolean. They must
	 * be identical for all four input combinations at every debounce depth,
	 * or the comparator would silence a healthy device. */
	for (int hi = 0; hi <= 1; hi++) {
		for (int lo = 0; lo <= 1; lo++) {
			estop_state_t a;
			estop_state_t b;

			estop_state_init(&a);
			estop_state_init(&b);

			for (unsigned int i = 0U; i < 6U; i++) {
				uint8_t va = estop_decide(&a, 0, hi, lo);
				uint8_t vb = estop_decide(&b, 1, hi, lo);

				zassert_equal(va, vb,
					      "core diversity diverged at hi=%d lo=%d tick=%u",
					      hi, lo, i);
			}
		}
	}
}

ZTEST(estop_verdict, test_not_primed_until_both_channels_settle)
{
	estop_state_t st[2];

	estop_state_init(&st[0]);
	estop_state_init(&st[1]);
	zassert_false(estop_channels_primed(st), "must not be primed at boot");

	/* Channel A settles via a full closed-debounce cycle. */
	(void)run_healthy(&st[0], 0, 3U);
	zassert_false(estop_channels_primed(st), "B has not settled");

	(void)run_healthy(&st[1], 1, 3U);
	zassert_true(estop_channels_primed(st), "both settled");
}

ZTEST(estop_verdict, test_button_held_at_boot_settles_via_open_streak)
{
	estop_state_t st[2];

	estop_state_init(&st[0]);
	estop_state_init(&st[1]);

	/* Button genuinely held down at power-on: never healthy. After
	 * LOOP_BOOT_OPEN_CONFIRM_TICKS consecutive opens the channel settles,
	 * so STOP flows instead of the device staying mute forever. */
	for (unsigned int i = 0U; i < LOOP_BOOT_OPEN_CONFIRM_TICKS; i++) {
		(void)estop_decide(&st[0], 0, 0, 0);
		(void)estop_decide(&st[1], 1, 0, 0);
	}
	zassert_true(estop_channels_primed(st), "held-open must settle");
}

ZTEST_SUITE(estop_verdict, NULL, NULL, NULL, NULL, NULL);
```

- [ ] **Step 2: Add the test build files**

Create `applications/protective_stop/tests/verdict/CMakeLists.txt`:

```cmake
# Truth-table tests for the pure verdict core. No HAL, no kernel services.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(estop_verdict)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../pstop_c.cmake)

set(APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

target_sources(app PRIVATE src/main.c ${APP_SRC}/estop_verdict.c)
target_include_directories(app PRIVATE ${APP_SRC} ${PSTOP_C_INCLUDE})
```

Create `applications/protective_stop/tests/verdict/prj.conf`:

```
CONFIG_ZTEST=y
```

Create `applications/protective_stop/tests/verdict/testcase.yaml`:

```yaml
tests:
  protective_stop.verdict:
    tags: protective_stop pstop safety
    platform_allow:
      - native_sim
      - qemu_cortex_m3
    integration_platforms:
      - native_sim
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `mise x -- west twister -T applications/protective_stop/tests/verdict -p native_sim --inline-logs`
Expected: FAIL — build error, `estop_verdict.h: No such file or directory`.

- [ ] **Step 4: Write the header**

Create `applications/protective_stop/src/estop_verdict.h`:

```c
/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Pure stop-switch decision core -- no HAL, no I/O, no kernel. Ported from
 * upstream firmware/main/estop_verdict.{c,h}, which was extracted from main.c
 * so the SIL-critical verdict logic is unit-testable to branch + MC/DC. That
 * reasoning holds here too, and gpio_emul additionally lets us test the HAL
 * glue in estop_gpio.c.
 */

#ifndef PROTECTIVE_STOP_ESTOP_VERDICT_H
#define PROTECTIVE_STOP_ESTOP_VERDICT_H

#include <stdbool.h>
#include <stdint.h>

/* Release-direction debounce: after ANY unhealthy read, this many consecutive
 * healthy ticks are required before the channel reports closed again. The
 * open->STOP edge stays SINGLE-TICK -- the stop path is never filtered. This
 * only extends how long a STOP episode lasts, so an EMC-induced blip produces
 * a >=300 ms episode instead of chattering.
 */
#define LOOP_RECLOSE_DEBOUNCE_TICKS 3U

/* Boot warm-up: the comparator sends NOTHING until each channel has settled --
 * either one full closed-debounce cycle, or this many CONSECUTIVE open reads
 * (switch genuinely held at boot, so STOP flows ~500 ms later, well before the
 * bond completes). Without the consecutive-open requirement, a first-sample
 * glitch would put a STOP->OK episode -- the arming gesture -- on the wire at
 * every power-on.
 */
#define LOOP_BOOT_OPEN_CONFIRM_TICKS 5U

/* Per-channel state, carried across ticks. One instance per sampler. */
typedef struct {
	bool high_ok;      /* most recent drive-high tick read IN==1 */
	bool low_ok;       /* most recent drive-low tick read IN==0 */
	bool primed_high;  /* a drive-high sample has been taken since boot */
	bool primed_low;   /* a drive-low sample has been taken since boot */
	uint8_t closed_streak; /* consecutive healthy ticks (release debounce) */
	uint8_t open_streak;   /* consecutive unhealthy ticks (boot warm-up) */
	bool settled;      /* warm-up done; see the two constants above */
} estop_state_t;

/* Zero a channel's state. Fail-safe: a zeroed state reports STOP. */
void estop_state_init(estop_state_t *st);

/* Decide this tick's pstop message byte from two FRESH both-phase reads
 * (rb_hi = readback after driving the loop HIGH, rb_lo = after driving it
 * LOW), updating health, debounce and priming. Returns PSTOP_MESSAGE_OK or
 * PSTOP_MESSAGE_STOP. Pure: no HAL, no globals.
 *
 * core_id selects a DIVERSE expression of the same decision: core 0 by
 * arithmetic image, core 1 by boolean. They are logically identical when
 * correct, so lockstep does not diverge in normal operation -- but a
 * systematic bug in either expression makes only that core wrong, the
 * comparator's memcmp diverges, nothing is sent, and the machine stops on
 * heartbeat liveness. This turns lockstep from mere redundancy (identical
 * code, identical error) into real diversity.
 */
uint8_t estop_decide(estop_state_t *st, int core_id, int rb_hi, int rb_lo);

/* True once BOTH channels have sampled both phases AND settled. The
 * comparator holds off sending until then.
 */
bool estop_channels_primed(const estop_state_t st[2]);

#endif /* PROTECTIVE_STOP_ESTOP_VERDICT_H */
```

- [ ] **Step 5: Write the implementation**

Create `applications/protective_stop/src/estop_verdict.c`:

```c
/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "estop_verdict.h"
#include "pstop/pstop_msg.h" /* PSTOP_MESSAGE_OK / PSTOP_MESSAGE_STOP */

void estop_state_init(estop_state_t *st)
{
	(void)memset(st, 0, sizeof(*st));
}

uint8_t estop_decide(estop_state_t *st, int core_id, int rb_hi, int rb_lo)
{
	/* The OK codeword is selected by the ARITHMETIC IMAGE of both fresh
	 * reads -- index 0 (OK) iff rb_hi==1 AND rb_lo==0, computed with no
	 * interpretable boolean. So OK cannot be produced by a stale/latched
	 * flag or by a fault in the health/debounce logic below: every check
	 * there is a STOP-ONLY override that may raise msg to STOP but can
	 * never lower it to OK.
	 */
	static const uint8_t k_estop_msg[2] = {PSTOP_MESSAGE_OK, PSTOP_MESSAGE_STOP};
	uint8_t msg;

	if (core_id == 0) {
		msg = k_estop_msg[(unsigned int)((rb_hi ^ 1) | rb_lo) & 1U];
	} else {
		msg = ((rb_hi == 1) && (rb_lo == 0)) ? PSTOP_MESSAGE_OK : PSTOP_MESSAGE_STOP;
	}

	st->high_ok = (rb_hi == 1);
	st->low_ok = (rb_lo == 0);
	st->primed_high = true;
	st->primed_low = true;

	const bool raw_closed = st->high_ok && st->low_ok;

	/* Asymmetric release debounce: open reports IMMEDIATELY, closed only
	 * after LOOP_RECLOSE_DEBOUNCE_TICKS consecutive healthy ticks.
	 */
	if (raw_closed) {
		if (st->closed_streak < (uint8_t)255U) {
			st->closed_streak++;
		}
		st->open_streak = 0U;
		if (st->closed_streak >= LOOP_RECLOSE_DEBOUNCE_TICKS) {
			st->settled = true;
		}
	} else {
		st->closed_streak = 0U;
		if (st->open_streak < (uint8_t)255U) {
			st->open_streak++;
		}
		if (st->open_streak >= LOOP_BOOT_OPEN_CONFIRM_TICKS) {
			st->settled = true; /* held open: STOP flows */
		}
	}

	/* STOP-ONLY override. msg is already STOP whenever this tick's live
	 * sample did not match the driven level, so a fault here cannot
	 * manufacture an OK.
	 */
	if (!(raw_closed && (st->closed_streak >= LOOP_RECLOSE_DEBOUNCE_TICKS))) {
		msg = PSTOP_MESSAGE_STOP;
	}

	return msg;
}

bool estop_channels_primed(const estop_state_t st[2])
{
	return st[0].primed_high && st[0].primed_low && st[1].primed_high &&
	       st[1].primed_low && st[0].settled && st[1].settled;
}
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `mise x -- west twister -T applications/protective_stop/tests/verdict -p native_sim --inline-logs`
Expected: PASS, 8 test cases.

- [ ] **Step 7: Add to the app build**

In `applications/protective_stop/CMakeLists.txt`, add `src/estop_verdict.c` to `target_sources(app PRIVATE ...)`.

Run: `mise run agent-build protective_stop --board native_sim/native/64`
Expected: build succeeds.

- [ ] **Step 8: Format and commit**

```bash
mise x -- clang-format --dry-run --Werror \
  applications/protective_stop/src/estop_verdict.c \
  applications/protective_stop/src/estop_verdict.h \
  applications/protective_stop/tests/verdict/src/main.c
git add applications/protective_stop
git commit -m "feat(protective_stop): pure stop-switch verdict core

Ports upstream's estop_verdict with its safety properties intact: a loop is
healthy only when this tick's drive-high echoed high AND this tick's
drive-low echoed low, the open->STOP edge is never debounced, and the
health/debounce logic can only raise a verdict to STOP.

Keeps upstream's core diversity -- core 0 decides arithmetically, core 1 by
boolean -- so a systematic bug in one expression diverges the lockstep
comparator instead of being duplicated into both channels.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: Loopback GPIO sampling

**Files:**
- Create: `applications/protective_stop/src/estop_gpio.h`
- Create: `applications/protective_stop/src/estop_gpio.c`
- Create: `applications/protective_stop/boards/native_sim_native_64.overlay`
- Test: `applications/protective_stop/tests/gpio/{CMakeLists.txt,prj.conf,testcase.yaml,src/main.c}`
- Modify: `applications/protective_stop/CMakeLists.txt`

**Interfaces:**
- Consumes: `estop_state_t`, `estop_decide()` (Task 2).
- Produces:
  - `int estop_gpio_init(void)` — 0 on success, negative errno otherwise
  - `uint8_t estop_channel_sample(int core_id, estop_state_t *st)` — drives both phases on channel `core_id` and returns that tick's verdict
  - `void estop_sim_set_pole(int ch, bool closed)` — **only** under `CONFIG_GPIO_EMUL`; models one DPST pole

- [ ] **Step 1: Write the devicetree overlay**

Create `applications/protective_stop/boards/native_sim_native_64.overlay`:

```dts
/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Dual-channel stop-switch loopback, modelled on gpio_emul.
 *
 * On hardware each channel runs out of a drive pin, through one pole of the
 * external DPST normally-closed switch, and back into a sense pin pulled DOWN
 * (so an open loop or a cut wire reads 0 = STOP). native_sim has no wire, so
 * estop_gpio.c mirrors OUT onto IN through a simulated pole; see
 * estop_sim_set_pole().
 *
 * Pin numbers mirror the hardware build (39/40, 41/42) purely so the two
 * overlays read alike; on gpio_emul any pin below ngpios would do.
 */

/ {
	zephyr,user {
		estop-a-out-gpios = <&gpio0 0 GPIO_ACTIVE_HIGH>;
		estop-a-in-gpios  = <&gpio0 1 GPIO_ACTIVE_HIGH>;
		estop-b-out-gpios = <&gpio0 2 GPIO_ACTIVE_HIGH>;
		estop-b-in-gpios  = <&gpio0 3 GPIO_ACTIVE_HIGH>;
	};
};
```

- [ ] **Step 2: Write the failing test**

Create `applications/protective_stop/tests/gpio/src/main.c`:

```c
/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * HAL glue tests: does driving both phases through a simulated DPST pole
 * produce the right verdict, and does a single-pole fault split the channels?
 */

#include <zephyr/ztest.h>

#include "estop_gpio.h"
#include "estop_verdict.h"
#include "pstop/pstop_msg.h"

static void *suite_setup(void)
{
	zassert_ok(estop_gpio_init(), "gpio init");
	return NULL;
}

ZTEST(estop_gpio, test_closed_pole_arms_after_debounce)
{
	estop_state_t st;
	uint8_t msg = PSTOP_MESSAGE_STOP;

	estop_state_init(&st);
	estop_sim_set_pole(0, true);

	for (unsigned int i = 0U; i < LOOP_RECLOSE_DEBOUNCE_TICKS; i++) {
		msg = estop_channel_sample(0, &st);
	}
	zassert_equal(msg, PSTOP_MESSAGE_OK, "closed pole must arm");
}

ZTEST(estop_gpio, test_open_pole_stops)
{
	estop_state_t st;

	estop_state_init(&st);
	estop_sim_set_pole(0, true);
	for (unsigned int i = 0U; i < LOOP_RECLOSE_DEBOUNCE_TICKS; i++) {
		(void)estop_channel_sample(0, &st);
	}

	estop_sim_set_pole(0, false);
	zassert_equal(estop_channel_sample(0, &st), PSTOP_MESSAGE_STOP,
		      "open pole must stop on the next tick");
}

ZTEST(estop_gpio, test_single_pole_fault_splits_the_channels)
{
	estop_state_t st[2];
	uint8_t v0;
	uint8_t v1;

	estop_state_init(&st[0]);
	estop_state_init(&st[1]);

	/* Pole A intact, pole B broken: exactly the fault lockstep exists for. */
	estop_sim_set_pole(0, true);
	estop_sim_set_pole(1, false);

	for (unsigned int i = 0U; i < LOOP_RECLOSE_DEBOUNCE_TICKS; i++) {
		v0 = estop_channel_sample(0, &st[0]);
		v1 = estop_channel_sample(1, &st[1]);
	}

	zassert_equal(v0, PSTOP_MESSAGE_OK, "healthy channel reports OK");
	zassert_equal(v1, PSTOP_MESSAGE_STOP, "broken channel reports STOP");
	zassert_not_equal(v0, v1, "the channels must disagree, silencing the comparator");
}

ZTEST(estop_gpio, test_channels_are_independent)
{
	estop_state_t st[2];

	estop_state_init(&st[0]);
	estop_state_init(&st[1]);
	estop_sim_set_pole(0, true);
	estop_sim_set_pole(1, true);

	for (unsigned int i = 0U; i < LOOP_RECLOSE_DEBOUNCE_TICKS; i++) {
		(void)estop_channel_sample(0, &st[0]);
		(void)estop_channel_sample(1, &st[1]);
	}

	/* Sampling channel 0 must not disturb channel 1's pins. */
	estop_sim_set_pole(0, false);
	(void)estop_channel_sample(0, &st[0]);
	zassert_equal(estop_channel_sample(1, &st[1]), PSTOP_MESSAGE_OK,
		      "channel 1 must be unaffected by channel 0");
}

ZTEST_SUITE(estop_gpio, NULL, suite_setup, NULL, NULL, NULL);
```

- [ ] **Step 3: Add the test build files**

Create `applications/protective_stop/tests/gpio/CMakeLists.txt`:

```cmake
# HAL glue tests. Needs gpio_emul, so native_sim only.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(estop_gpio)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../pstop_c.cmake)

set(APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

target_sources(app PRIVATE
  src/main.c
  ${APP_SRC}/estop_verdict.c
  ${APP_SRC}/estop_gpio.c
)
target_include_directories(app PRIVATE ${APP_SRC} ${PSTOP_C_INCLUDE})
```

Create `applications/protective_stop/tests/gpio/prj.conf`:

```
CONFIG_ZTEST=y
CONFIG_GPIO=y
CONFIG_GPIO_EMUL=y
```

Create `applications/protective_stop/tests/gpio/testcase.yaml`:

```yaml
tests:
  protective_stop.gpio:
    tags: protective_stop pstop safety gpio
    # gpio_emul is the simulated DPST wire, so this is native_sim-only.
    platform_allow:
      - native_sim
    integration_platforms:
      - native_sim
```

Copy the overlay so twister picks it up:

```bash
cp applications/protective_stop/boards/native_sim_native_64.overlay \
   applications/protective_stop/tests/gpio/boards/native_sim_native_64.overlay
```

(Create the `boards/` directory first: `mkdir -p applications/protective_stop/tests/gpio/boards`.)

- [ ] **Step 4: Run the test to verify it fails**

Run: `mise x -- west twister -T applications/protective_stop/tests/gpio -p native_sim --inline-logs`
Expected: FAIL — `estop_gpio.h: No such file or directory`.

- [ ] **Step 5: Write the header**

Create `applications/protective_stop/src/estop_gpio.h`:

```c
/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Thin HAL glue for the dual-channel stop-switch loopback. All decision logic
 * lives in estop_verdict.c; this file only drives pins and reads them back.
 */

#ifndef PROTECTIVE_STOP_ESTOP_GPIO_H
#define PROTECTIVE_STOP_ESTOP_GPIO_H

#include <stdbool.h>
#include <stdint.h>

#include "estop_verdict.h"

#define ESTOP_CHANNELS 2

/* Configure both channels' pins. Drive pins as outputs held low, sense pins as
 * inputs pulled DOWN so an open loop reads 0 = STOP. Returns 0 or -errno.
 */
int estop_gpio_init(void);

/* Drive channel `core_id`'s loop high, read the echo, drive it low, read
 * again, then hand both reads to estop_decide(). Called exactly ONCE per tick
 * per channel; the returned verdict is reused for every machine slot so all
 * sessions carry the same tick verdict.
 */
uint8_t estop_channel_sample(int core_id, estop_state_t *st);

#ifdef CONFIG_GPIO_EMUL
/* Model one pole of the DPST switch: closed conducts the driven level to the
 * sense pin, open leaves it pulled down. native_sim has no wire, so the
 * loopback has to be modelled explicitly -- there is nothing to test
 * otherwise. Not compiled on hardware.
 */
void estop_sim_set_pole(int ch, bool closed);
#endif

#endif /* PROTECTIVE_STOP_ESTOP_GPIO_H */
```

- [ ] **Step 6: Write the implementation**

Create `applications/protective_stop/src/estop_gpio.c`:

```c
/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#ifdef CONFIG_GPIO_EMUL
#include <zephyr/drivers/gpio/gpio_emul.h>
#endif

#include "estop_gpio.h"

LOG_MODULE_REGISTER(estop_gpio, LOG_LEVEL_INF);

#define ZUSER DT_PATH(zephyr_user)

/* Propagation through the wire and the switch contacts. */
#define ESTOP_SETTLE_US 10

static const struct gpio_dt_spec estop_out[ESTOP_CHANNELS] = {
	GPIO_DT_SPEC_GET(ZUSER, estop_a_out_gpios),
	GPIO_DT_SPEC_GET(ZUSER, estop_b_out_gpios),
};

static const struct gpio_dt_spec estop_in[ESTOP_CHANNELS] = {
	GPIO_DT_SPEC_GET(ZUSER, estop_a_in_gpios),
	GPIO_DT_SPEC_GET(ZUSER, estop_b_in_gpios),
};

#ifdef CONFIG_GPIO_EMUL
static bool sim_pole_closed[ESTOP_CHANNELS];

void estop_sim_set_pole(int ch, bool closed)
{
	if ((ch >= 0) && (ch < ESTOP_CHANNELS)) {
		sim_pole_closed[ch] = closed;
	}
}

/* Model the wire: a closed pole conducts the driven level to the sense pin, an
 * open pole leaves it at the pull-down's 0.
 */
static void sim_propagate(int ch, int driven)
{
	(void)gpio_emul_input_set_dt(&estop_in[ch], sim_pole_closed[ch] ? driven : 0);
}
#else
#define sim_propagate(ch, driven) ((void)0)
#endif

int estop_gpio_init(void)
{
	for (int c = 0; c < ESTOP_CHANNELS; c++) {
		int ret;

		if (!gpio_is_ready_dt(&estop_out[c]) || !gpio_is_ready_dt(&estop_in[c])) {
			LOG_ERR("channel %d gpio not ready", c);
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&estop_out[c], GPIO_OUTPUT_INACTIVE);
		if (ret != 0) {
			LOG_ERR("channel %d out configure: %d", c, ret);
			return ret;
		}

		/* Pull DOWN: an open loop or a cut wire reads 0 = STOP. */
		ret = gpio_pin_configure_dt(&estop_in[c], GPIO_INPUT | GPIO_PULL_DOWN);
		if (ret != 0) {
			LOG_ERR("channel %d in configure: %d", c, ret);
			return ret;
		}
	}

	LOG_INF("stop-switch loopback ready: %d channels", ESTOP_CHANNELS);
	return 0;
}

uint8_t estop_channel_sample(int core_id, estop_state_t *st)
{
	int rb_hi;
	int rb_lo;

	/* Sample BOTH loop phases THIS tick. A healthy closed loop conducts as
	 * driven, so rb_hi==1 AND rb_lo==0. Driving only one phase would let a
	 * stuck-high sense pin masquerade as a closed loop.
	 */
	(void)gpio_pin_set_dt(&estop_out[core_id], 1);
	sim_propagate(core_id, 1);
	k_busy_wait(ESTOP_SETTLE_US);
	rb_hi = gpio_pin_get_dt(&estop_in[core_id]);

	(void)gpio_pin_set_dt(&estop_out[core_id], 0);
	sim_propagate(core_id, 0);
	k_busy_wait(ESTOP_SETTLE_US);
	rb_lo = gpio_pin_get_dt(&estop_in[core_id]);

	/* A read error must never read as a healthy level. */
	if (rb_hi < 0) {
		rb_hi = 0;
	}
	if (rb_lo < 0) {
		rb_lo = 1;
	}

	return estop_decide(st, core_id, rb_hi, rb_lo);
}
```

- [ ] **Step 7: Run the test to verify it passes**

Run: `mise x -- west twister -T applications/protective_stop/tests/gpio -p native_sim --inline-logs`
Expected: PASS, 4 test cases.

- [ ] **Step 8: Add to the app build and commit**

Add `src/estop_gpio.c` to `target_sources(app PRIVATE ...)` in the app `CMakeLists.txt`.

```bash
mise run agent-build protective_stop --board native_sim/native/64
mise x -- clang-format --dry-run --Werror \
  applications/protective_stop/src/estop_gpio.c \
  applications/protective_stop/src/estop_gpio.h \
  applications/protective_stop/tests/gpio/src/main.c
git add applications/protective_stop
git commit -m "feat(protective_stop): dual-channel loopback sampling

Drives both phases of each channel every tick and hands the pair to the
verdict core. Sense pins are pulled down so an open loop or a cut wire reads
STOP, and a read error is coerced to the unhealthy level rather than the
healthy one.

native_sim has no wire, so estop_sim_set_pole() models the DPST pole
explicitly -- without it there is no way to test that a single-pole fault
splits the two channels, which is the fault lockstep exists to catch.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: ZMS-backed settings

**Files:**
- Create: `applications/protective_stop/src/app_settings.h`
- Create: `applications/protective_stop/src/app_settings.c`
- Test: `applications/protective_stop/tests/settings/{CMakeLists.txt,prj.conf,testcase.yaml,src/main.c}`
- Modify: `applications/protective_stop/CMakeLists.txt`, `applications/protective_stop/prj.conf`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces:
  - `#define PSTOP_MAX_MACHINES 4`
  - `struct pstop_peer { uint32_t ip; uint16_t port; uint32_t id; bool configured; }`
  - `int app_settings_init(void)`
  - `const struct pstop_peer *app_settings_peer(int slot)` — never NULL for `0 <= slot < PSTOP_MAX_MACHINES`
  - `int app_settings_set_peer(int slot, uint32_t ip, uint16_t port, uint32_t id)`
  - `int app_settings_clear_peer(int slot)`
  - `uint32_t app_settings_device_id(void)` / `int app_settings_set_device_id(uint32_t id)`
  - `bool app_settings_is_operator(void)` / `int app_settings_set_operator(bool op)`

- [ ] **Step 1: Write the failing test**

Create `applications/protective_stop/tests/settings/src/main.c`:

```c
/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Settings round-trip over ZMS. On native_sim the ZMS backend sits on
 * flash_simulator's native backend, which persists to a host file, so this
 * exercises the real persistence path rather than a RAM stub.
 */

#include <zephyr/settings/settings.h>
#include <zephyr/ztest.h>

#include "app_settings.h"

static void *suite_setup(void)
{
	zassert_ok(app_settings_init(), "settings init");
	return NULL;
}

ZTEST(app_settings, test_peers_default_to_unconfigured)
{
	for (int slot = 0; slot < PSTOP_MAX_MACHINES; slot++) {
		const struct pstop_peer *p = app_settings_peer(slot);

		zassert_not_null(p, "slot %d", slot);
		/* A fresh slot must not be configured: an unconfigured slot is
		 * never heartbeated, so a garbage default would silently point
		 * the remote at an unintended address.
		 */
		if (!p->configured) {
			zassert_equal(p->ip, 0U, "slot %d ip", slot);
			zassert_equal(p->port, 0U, "slot %d port", slot);
		}
	}
}

ZTEST(app_settings, test_set_and_read_back_peer)
{
	const struct pstop_peer *p;

	zassert_ok(app_settings_set_peer(1, 0xC0A80164U, 8890U, 0x01020304U), "set");

	p = app_settings_peer(1);
	zassert_true(p->configured, "configured");
	zassert_equal(p->ip, 0xC0A80164U, "ip");
	zassert_equal(p->port, 8890U, "port");
	zassert_equal(p->id, 0x01020304U, "machine id");
}

ZTEST(app_settings, test_clear_peer)
{
	zassert_ok(app_settings_set_peer(2, 0x0A000001U, 8890U, 0x11U), "set");
	zassert_true(app_settings_peer(2)->configured, "configured");

	zassert_ok(app_settings_clear_peer(2), "clear");
	zassert_false(app_settings_peer(2)->configured, "cleared");
}

ZTEST(app_settings, test_out_of_range_slot_is_rejected)
{
	zassert_not_equal(app_settings_set_peer(-1, 1U, 1U, 1U), 0, "negative slot");
	zassert_not_equal(app_settings_set_peer(PSTOP_MAX_MACHINES, 1U, 1U, 1U), 0,
			  "slot past the end");
	zassert_is_null(app_settings_peer(PSTOP_MAX_MACHINES), "peer past the end");
}

ZTEST(app_settings, test_role_defaults_to_stop_only)
{
	/* Maximally safe default: a remote that cannot re-arm until someone
	 * deliberately promotes it. Mirrors upstream's default.
	 */
	zassert_ok(app_settings_set_operator(false), "reset role");
	zassert_false(app_settings_is_operator(), "default must be stop-only");
}

ZTEST(app_settings, test_role_round_trips)
{
	zassert_ok(app_settings_set_operator(true), "promote");
	zassert_true(app_settings_is_operator(), "operator");

	zassert_ok(app_settings_set_operator(false), "demote");
	zassert_false(app_settings_is_operator(), "stop-only");
}

ZTEST(app_settings, test_device_id_round_trips)
{
	zassert_ok(app_settings_set_device_id(0xC0A80105U), "set");
	zassert_equal(app_settings_device_id(), 0xC0A80105U, "read back");
}

ZTEST_SUITE(app_settings, NULL, suite_setup, NULL, NULL, NULL);
```

- [ ] **Step 2: Add the test build files**

Create `applications/protective_stop/tests/settings/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(app_settings)

set(APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

target_sources(app PRIVATE src/main.c ${APP_SRC}/app_settings.c)
target_include_directories(app PRIVATE ${APP_SRC})
```

Create `applications/protective_stop/tests/settings/prj.conf`:

```
CONFIG_ZTEST=y

CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_FLASH_SIMULATOR=y
CONFIG_FLASH_SIMULATOR_DOUBLE_WRITES=y

CONFIG_ZMS=y
CONFIG_SETTINGS=y
CONFIG_SETTINGS_ZMS=y
```

Create `applications/protective_stop/tests/settings/testcase.yaml`:

```yaml
tests:
  protective_stop.settings:
    tags: protective_stop pstop settings
    # Needs flash_simulator behind ZMS.
    platform_allow:
      - native_sim
    integration_platforms:
      - native_sim
```

No devicetree overlay is needed: `native_sim.dts` already declares a `storage_partition` (`partition@fc000`, label `"storage"`) on the simulated flash, and `settings_zms.c:24` binds to exactly that label via `PARTITION_ID(storage_partition)`. On the hardware board the same label is supplied by `partitions_0x0_amp_16M.dtsi`, so nothing here is native_sim-specific.

- [ ] **Step 3: Run the test to verify it fails**

Run: `mise x -- west twister -T applications/protective_stop/tests/settings -p native_sim --inline-logs`
Expected: FAIL — `app_settings.h: No such file or directory`.

- [ ] **Step 4: Write the header**

Create `applications/protective_stop/src/app_settings.h`:

```c
/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Persistent configuration: the machine peer table, this remote's role, and
 * its device id.
 *
 * Settings-over-ZMS rather than raw ZMS: raw ZMS is a flat uint32 id -> bytes
 * store, so using it directly would mean inventing an id map and keeping it
 * stable across firmware versions forever. Settings gives string keys and
 * allocates ids itself. Upstream's NVS blobs are string-keyed too, so the
 * persistence model stays recognisable.
 */

#ifndef PROTECTIVE_STOP_APP_SETTINGS_H
#define PROTECTIVE_STOP_APP_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

/* One remote heartbeats up to this many machines independently. */
#define PSTOP_MAX_MACHINES 4

struct pstop_peer {
	uint32_t ip;   /* IPv4, host byte order */
	uint16_t port; /* machine's UDP port */
	uint32_t id;   /* the machine's pstop device id */
	bool configured;
};

/* Initialise the settings subsystem and load everything under "pstop/".
 * Returns 0 or -errno.
 */
int app_settings_init(void);

/* Read a peer slot. Returns NULL if slot is out of range. The returned
 * pointer stays valid for the process lifetime.
 */
const struct pstop_peer *app_settings_peer(int slot);

/* Set and persist a peer slot. Returns 0 or -errno (-EINVAL for a bad slot). */
int app_settings_set_peer(int slot, uint32_t ip, uint16_t port, uint32_t id);

/* Empty and persist a peer slot. */
int app_settings_clear_peer(int slot);

/* This remote's pstop device id. Zero means "derive from the active uplink";
 * a non-zero stored value pins it. Pinning matters because the machine's
 * operator allowlist is keyed on this value, so an id that moves with DHCP
 * silently demotes the remote to stop-only.
 */
uint32_t app_settings_device_id(void);
int app_settings_set_device_id(uint32_t id);

/* Self-claimed role, announced in every pstop frame. Defaults to stop-only:
 * the machine ANDs this claim with its own operator allowlist, so claiming
 * operator is necessary but not sufficient to re-arm.
 */
bool app_settings_is_operator(void);
int app_settings_set_operator(bool op);

#endif /* PROTECTIVE_STOP_APP_SETTINGS_H */
```

- [ ] **Step 5: Write the implementation**

Create `applications/protective_stop/src/app_settings.c`:

```c
/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>  /* snprintf */
#include <stdlib.h> /* atoi */
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include "app_settings.h"

LOG_MODULE_REGISTER(app_settings, LOG_LEVEL_INF);

#define PSTOP_SETTINGS_ROOT "pstop"

static struct pstop_peer peers[PSTOP_MAX_MACHINES];
static uint32_t device_id;
static bool is_operator;

static int pstop_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	const char *next;

	if (settings_name_steq(name, "device_id", &next) && !next) {
		if (len != sizeof(device_id)) {
			return -EINVAL;
		}
		return (read_cb(cb_arg, &device_id, sizeof(device_id)) < 0) ? -EIO : 0;
	}

	if (settings_name_steq(name, "operator", &next) && !next) {
		if (len != sizeof(is_operator)) {
			return -EINVAL;
		}
		return (read_cb(cb_arg, &is_operator, sizeof(is_operator)) < 0) ? -EIO : 0;
	}

	if (settings_name_steq(name, "peers", &next) && next) {
		/* next is "<slot>" */
		int slot = atoi(next);

		if ((slot < 0) || (slot >= PSTOP_MAX_MACHINES)) {
			return -EINVAL;
		}
		if (len != sizeof(peers[slot])) {
			return -EINVAL;
		}
		return (read_cb(cb_arg, &peers[slot], sizeof(peers[slot])) < 0) ? -EIO : 0;
	}

	return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(pstop, PSTOP_SETTINGS_ROOT, NULL, pstop_settings_set, NULL, NULL);

int app_settings_init(void)
{
	int ret;

	(void)memset(peers, 0, sizeof(peers));
	device_id = 0U;
	is_operator = false;

	ret = settings_subsys_init();
	if (ret != 0) {
		LOG_ERR("settings_subsys_init: %d", ret);
		return ret;
	}

	ret = settings_load_subtree(PSTOP_SETTINGS_ROOT);
	if (ret != 0) {
		LOG_ERR("settings_load_subtree: %d", ret);
		return ret;
	}

	LOG_INF("settings loaded: device_id=%08x role=%s", device_id,
		is_operator ? "operator" : "stop_only");
	return 0;
}

const struct pstop_peer *app_settings_peer(int slot)
{
	if ((slot < 0) || (slot >= PSTOP_MAX_MACHINES)) {
		return NULL;
	}
	return &peers[slot];
}

static int save_peer(int slot)
{
	char key[32];

	(void)snprintf(key, sizeof(key), PSTOP_SETTINGS_ROOT "/peers/%d", slot);
	return settings_save_one(key, &peers[slot], sizeof(peers[slot]));
}

int app_settings_set_peer(int slot, uint32_t ip, uint16_t port, uint32_t id)
{
	if ((slot < 0) || (slot >= PSTOP_MAX_MACHINES)) {
		return -EINVAL;
	}

	peers[slot].ip = ip;
	peers[slot].port = port;
	peers[slot].id = id;
	peers[slot].configured = true;

	return save_peer(slot);
}

int app_settings_clear_peer(int slot)
{
	if ((slot < 0) || (slot >= PSTOP_MAX_MACHINES)) {
		return -EINVAL;
	}

	(void)memset(&peers[slot], 0, sizeof(peers[slot]));
	return save_peer(slot);
}

uint32_t app_settings_device_id(void)
{
	return device_id;
}

int app_settings_set_device_id(uint32_t id)
{
	device_id = id;
	return settings_save_one(PSTOP_SETTINGS_ROOT "/device_id", &device_id, sizeof(device_id));
}

bool app_settings_is_operator(void)
{
	return is_operator;
}

int app_settings_set_operator(bool op)
{
	is_operator = op;
	return settings_save_one(PSTOP_SETTINGS_ROOT "/operator", &is_operator,
				 sizeof(is_operator));
}
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `mise x -- west twister -T applications/protective_stop/tests/settings -p native_sim --inline-logs`
Expected: PASS, 7 test cases.

- [ ] **Step 7: Enable settings in the app and commit**

Append to `applications/protective_stop/prj.conf`:

```
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_ZMS=y
CONFIG_SETTINGS=y
CONFIG_SETTINGS_ZMS=y
```

Append to `applications/protective_stop/boards/native_sim_native_64.conf`:

```
# ZMS needs a flash backend; flash_simulator's native backend persists to a
# host file, so settings survive a restart of the simulation.
CONFIG_FLASH_SIMULATOR=y
CONFIG_FLASH_SIMULATOR_DOUBLE_WRITES=y
```

Add `src/app_settings.c` to `target_sources(app PRIVATE ...)`.

```bash
mise run agent-build protective_stop --board native_sim/native/64
mise x -- clang-format --dry-run --Werror \
  applications/protective_stop/src/app_settings.c \
  applications/protective_stop/src/app_settings.h \
  applications/protective_stop/tests/settings/src/main.c
git add applications/protective_stop
git commit -m "feat(protective_stop): ZMS-backed peer table, role and device id

Uses the Settings API on a ZMS backend rather than raw ZMS: raw ZMS is a flat
id->bytes store, so using it directly would mean owning an id map forever.
Settings gives string keys, matching upstream's string-keyed NVS blobs.

Role defaults to stop-only, so a remote cannot re-arm a machine until it is
deliberately promoted AND the machine's own allowlist agrees.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 5: Per-machine session and bond state machine

**Files:**
- Create: `applications/protective_stop/src/session.h`
- Create: `applications/protective_stop/src/session.c`
- Test: `applications/protective_stop/tests/session/{CMakeLists.txt,prj.conf,testcase.yaml,src/main.c}`
- Modify: `applications/protective_stop/CMakeLists.txt`

**Interfaces:**
- Consumes: `struct pstop_peer`, `PSTOP_MAX_MACHINES` (Task 4); `protocol_data_t`, `pstop_msg_t`, `pstop_message_encode/decode`, `pstop_create_generic_message` (Task 1).
- Produces:
  - `enum pstop_sess_state { PSTOP_SESS_IDLE = 0, PSTOP_SESS_BONDING = 1, PSTOP_SESS_BONDED = 2 }`
  - `struct pstop_session { ... }` (fields below)
  - `int pstop_session_open(struct pstop_session *s, int slot, const struct pstop_peer *peer)`
  - `void pstop_session_close(struct pstop_session *s)`
  - `uint32_t pstop_session_send_period_ms(const struct pstop_session *s)`
  - `uint32_t pstop_session_rebond_after_ms(const struct pstop_session *s)`
  - `bool pstop_session_due(const struct pstop_session *s, uint64_t now_ms)`
  - `void pstop_session_build(struct pstop_session *s, uint8_t verdict, uint64_t now_ms, uint8_t *out48)`
  - `int pstop_session_commit_send(struct pstop_session *s, const uint8_t *buf48, uint64_t now_ms)`
  - `void pstop_session_poll(struct pstop_session *s, uint64_t now_ms)`
  - `void pstop_session_watchdog(struct pstop_session *s, uint64_t now_ms)`

- [ ] **Step 1: Write the failing test**

Create `applications/protective_stop/tests/session/src/main.c`:

```c
/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Session timing and framing rules. These are the ones that silently break
 * interop when wrong: send period, counter decimation, and a rebond watchdog
 * that must never fire before the machine's own bond-drop timeout.
 */

#include <string.h>

#include <zephyr/ztest.h>

#include "app_settings.h"
#include "pstop/pstop_msg.h"
#include "session.h"

static struct pstop_session sess;
static const struct pstop_peer peer = {
	.ip = 0x7F000001U, /* 127.0.0.1 */
	.port = 8890U,
	.id = 0x01020304U,
	.configured = true,
};

static void test_before(void *fixture)
{
	ARG_UNUSED(fixture);
	(void)memset(&sess, 0, sizeof(sess));
	zassert_ok(pstop_session_open(&sess, 0, &peer), "open");
}

static void test_after(void *fixture)
{
	ARG_UNUSED(fixture);
	pstop_session_close(&sess);
}

ZTEST(pstop_session, test_send_period_defaults_to_floor_before_first_reply)
{
	/* No reply yet, so no machine-advertised heartbeat. Until one arrives
	 * we transmit at the fast end rather than guessing slow.
	 */
	zassert_equal(pstop_session_send_period_ms(&sess), 100U, "pre-reply period");
}

ZTEST(pstop_session, test_send_period_is_half_the_advertised_heartbeat)
{
	sess.hb_ms = 1000U; /* what upstream's machine_app advertises */
	zassert_equal(pstop_session_send_period_ms(&sess), 500U, "half of 1000");

	sess.hb_ms = 400U;
	zassert_equal(pstop_session_send_period_ms(&sess), 200U, "half of 400");
}

ZTEST(pstop_session, test_send_period_is_clamped)
{
	sess.hb_ms = 10U; /* absurdly fast */
	zassert_equal(pstop_session_send_period_ms(&sess), 100U, "clamped to the 10 Hz tick");

	sess.hb_ms = 60000U; /* absurdly slow */
	zassert_equal(pstop_session_send_period_ms(&sess), 1000U, "clamped to 1 s");
}

ZTEST(pstop_session, test_counter_advances_only_on_commit)
{
	uint8_t a[PSTOP_MESSAGE_SIZE];
	uint8_t b[PSTOP_MESSAGE_SIZE];

	/* Building twice without committing must produce identical bytes --
	 * this is exactly what the two lockstep samplers do, and if build()
	 * advanced the counter they would never match.
	 */
	pstop_session_build(&sess, PSTOP_MESSAGE_OK, 5000ULL, a);
	pstop_session_build(&sess, PSTOP_MESSAGE_OK, 5000ULL, b);
	zassert_mem_equal(a, b, PSTOP_MESSAGE_SIZE, "two builds must agree");
}

ZTEST(pstop_session, test_committed_sends_have_contiguous_counters)
{
	uint8_t buf[PSTOP_MESSAGE_SIZE];
	pstop_msg_t msg;
	uint32_t first;

	pstop_session_build(&sess, PSTOP_MESSAGE_OK, 5000ULL, buf);
	pstop_message_decode(&msg, buf);
	first = msg.counter;
	(void)pstop_session_commit_send(&sess, buf, 5000ULL);

	pstop_session_build(&sess, PSTOP_MESSAGE_OK, 5500ULL, buf);
	pstop_message_decode(&msg, buf);

	/* Contiguous, NOT +5 for the five 100 ms ticks that elapsed. The
	 * machine rejects gaps beyond max_lost_messages + 1.
	 */
	zassert_equal(msg.counter, first + 1U, "counter must advance by exactly one per send");
}

ZTEST(pstop_session, test_first_message_is_a_bond)
{
	uint8_t buf[PSTOP_MESSAGE_SIZE];
	pstop_msg_t msg;

	pstop_session_build(&sess, PSTOP_MESSAGE_OK, 1000ULL, buf);
	pstop_message_decode(&msg, buf);

	zassert_equal(msg.message, PSTOP_MESSAGE_BOND,
		      "an unbonded session must bond before it heartbeats");
}

ZTEST(pstop_session, test_bonded_session_carries_the_verdict)
{
	uint8_t buf[PSTOP_MESSAGE_SIZE];
	pstop_msg_t msg;

	sess.state = PSTOP_SESS_BONDED;

	pstop_session_build(&sess, PSTOP_MESSAGE_STOP, 1000ULL, buf);
	pstop_message_decode(&msg, buf);
	zassert_equal(msg.message, PSTOP_MESSAGE_STOP, "verdict must reach the wire");

	pstop_session_build(&sess, PSTOP_MESSAGE_OK, 1000ULL, buf);
	pstop_message_decode(&msg, buf);
	zassert_equal(msg.message, PSTOP_MESSAGE_OK, "verdict must reach the wire");
}

ZTEST(pstop_session, test_addressing_matches_the_peer)
{
	uint8_t buf[PSTOP_MESSAGE_SIZE];
	pstop_msg_t msg;

	(void)app_settings_set_device_id(0xC0A80105U);
	pstop_session_build(&sess, PSTOP_MESSAGE_OK, 1000ULL, buf);
	pstop_message_decode(&msg, buf);

	zassert_equal(msg.receiver_id.data, peer.id, "receiver must be the machine id");
	zassert_equal(msg.id.data, 0xC0A80105U, "sender must be our device id");
}

ZTEST(pstop_session, test_rebond_watchdog_exceeds_the_machine_timeout)
{
	/* The machine drops a bond after hb_ms * max_missed. Our watchdog must
	 * fire LATER, or a sub-timeout reply blip causes a nuisance rebond
	 * while the machine still holds the bond.
	 */
	sess.hb_ms = 400U;
	zassert_true(pstop_session_rebond_after_ms(&sess) > (400U * 5U),
		     "watchdog must outlast the machine's 400*5 ms bond timeout");

	sess.hb_ms = 1000U;
	zassert_true(pstop_session_rebond_after_ms(&sess) > (1000U * 5U),
		     "watchdog must outlast the machine's 1000*5 ms bond timeout");
}

ZTEST(pstop_session, test_rebond_watchdog_has_a_floor)
{
	sess.hb_ms = 0U; /* no reply yet, nothing advertised */
	zassert_true(pstop_session_rebond_after_ms(&sess) >= 2500U,
		     "pre-reply watchdog must still have a sane floor");
}

ZTEST_SUITE(pstop_session, NULL, NULL, test_before, test_after, NULL);
```

- [ ] **Step 2: Add the test build files**

Create `applications/protective_stop/tests/session/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pstop_session)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../pstop_c.cmake)

set(APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

target_sources(app PRIVATE
  src/main.c
  ${APP_SRC}/session.c
  ${APP_SRC}/app_settings.c
  ${PSTOP_C_SRCS}
  ${APP_SRC}/pstop_time.c
)
target_include_directories(app PRIVATE ${APP_SRC} ${PSTOP_C_INCLUDE})
```

Create `applications/protective_stop/tests/session/prj.conf`:

```
CONFIG_ZTEST=y

CONFIG_NETWORKING=y
CONFIG_NET_SOCKETS=y

CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_FLASH_SIMULATOR=y
CONFIG_FLASH_SIMULATOR_DOUBLE_WRITES=y
CONFIG_ZMS=y
CONFIG_SETTINGS=y
CONFIG_SETTINGS_ZMS=y
```

Create `applications/protective_stop/tests/session/boards/native_sim_native_64.conf`:

```
CONFIG_NET_NATIVE_OFFLOADED_SOCKETS=y
CONFIG_NET_DRIVERS=y
CONFIG_NET_L2_ETHERNET=n
CONFIG_NET_IPV4=n
CONFIG_NET_UDP=n
```

Create `applications/protective_stop/tests/session/testcase.yaml`:

```yaml
tests:
  protective_stop.session:
    tags: protective_stop pstop session
    platform_allow:
      - native_sim
    integration_platforms:
      - native_sim
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `mise x -- west twister -T applications/protective_stop/tests/session -p native_sim --inline-logs`
Expected: FAIL — `session.h: No such file or directory`.

- [ ] **Step 4: Write the header**

Create `applications/protective_stop/src/session.h`:

```c
/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * One pstop session: the socket, bond state machine, counters and reply-loss
 * watchdog for a single machine slot. One socket per machine keeps the reply
 * path trivially demultiplexed and stops a dead machine from stalling the
 * heartbeats to the others.
 *
 * Build/commit split: the two lockstep samplers both call
 * pstop_session_build(), which must be side-effect free so their encodings are
 * byte-identical. Only the comparator calls pstop_session_commit_send(), which
 * advances the counter.
 */

#ifndef PROTECTIVE_STOP_SESSION_H
#define PROTECTIVE_STOP_SESSION_H

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/net/socket.h>

#include "app_settings.h"
#include "pstop/protocol_data.h"
#include "pstop/pstop_msg.h"

/* Local UDP source ports: slot i binds PSTOP_LOCAL_PORT + i. */
#define PSTOP_LOCAL_PORT 8891

/* Transmit every heartbeat_timeout/2, clamped to this window. The lower bound
 * is the 10 Hz lockstep tick: sampling is a safety property and never slows.
 */
#define PSTOP_MIN_SEND_PERIOD_MS 100U
#define PSTOP_MAX_SEND_PERIOD_MS 1000U

/* One BOND in flight at a time. Long spacing matters because the protocol
 * rejects duplicate counters with MSG_LOST once a client is registered.
 */
#define PSTOP_BOND_RETRY_MS 5000U

/* Reply-loss watchdog floor, covering the pre-first-reply state. The real
 * threshold is derived per session so it always outlasts the machine's own
 * bond-drop timeout -- see pstop_session_rebond_after_ms().
 */
#define PSTOP_REBOND_FLOOR_MS 2500U

/* The machine's missed-heartbeat multiplier is NOT on the wire (replies carry
 * only heartbeat_timeout), so this is a hard-coded coupling to machn's
 * MACHN_MAX_MISSED_HEARTBEATS. If that changes upstream, this must change too.
 */
#define PSTOP_REBOND_MACHINE_MAX_MISSED 5U
#define PSTOP_REBOND_JITTER_MARGIN_MS 500U

enum pstop_sess_state {
	PSTOP_SESS_IDLE = 0,
	PSTOP_SESS_BONDING = 1,
	PSTOP_SESS_BONDED = 2,
};

struct pstop_session {
	int slot;
	int sock;
	struct sockaddr_in peer_addr;
	uint32_t peer_id;

	protocol_data_t proto; /* pstop_c's per-peer counters and stamps */
	enum pstop_sess_state state;

	uint32_t hb_ms; /* heartbeat window advertised by the machine */

	uint64_t last_send_ms;
	uint64_t last_reply_ms;
	uint64_t last_bond_ms;

	/* Telemetry, surfaced by /state.json in slice 3. */
	uint32_t sent;
	uint32_t replies;
	uint32_t send_fail;
	uint32_t rebonds;
	uint8_t last_msg;
};

/* Bind a socket on PSTOP_LOCAL_PORT + slot and point the session at peer.
 * Returns 0 or -errno. Safe to call on a zeroed struct.
 */
int pstop_session_open(struct pstop_session *s, int slot, const struct pstop_peer *peer);

void pstop_session_close(struct pstop_session *s);

/* heartbeat_timeout/2, clamped. Before the first reply hb_ms is 0 and this
 * returns the floor -- transmit fast rather than guess slow.
 */
uint32_t pstop_session_send_period_ms(const struct pstop_session *s);

/* How long without a reply before this session re-bonds. Always greater than
 * the machine's own hb_ms * max_missed bond-drop timeout.
 */
uint32_t pstop_session_rebond_after_ms(const struct pstop_session *s);

/* Is this session due to transmit on this tick? */
bool pstop_session_due(const struct pstop_session *s, uint64_t now_ms);

/* Encode this tick's 48-byte frame into out48. SIDE-EFFECT FREE: calling it
 * twice with the same arguments must produce identical bytes, because both
 * lockstep samplers call it and the comparator byte-compares the results.
 */
void pstop_session_build(struct pstop_session *s, uint8_t verdict, uint64_t now_ms,
			 uint8_t *out48);

/* Transmit a frame the comparator has already validated, then advance the
 * counter. Counter advances ONLY here, so the machine sees contiguous
 * counters despite send decimation. Returns 0 or -errno.
 */
int pstop_session_commit_send(struct pstop_session *s, const uint8_t *buf48, uint64_t now_ms);

/* Drain any pending replies (non-blocking) and adopt the machine's advertised
 * heartbeat window.
 */
void pstop_session_poll(struct pstop_session *s, uint64_t now_ms);

/* Re-bond if the machine has gone quiet for longer than the derived window. */
void pstop_session_watchdog(struct pstop_session *s, uint64_t now_ms);

#endif /* PROTECTIVE_STOP_SESSION_H */
```

- [ ] **Step 5: Write the implementation**

Create `applications/protective_stop/src/session.c`:

```c
/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/byteorder.h>

#include "session.h"

LOG_MODULE_REGISTER(pstop_session, LOG_LEVEL_INF);

int pstop_session_open(struct pstop_session *s, int slot, const struct pstop_peer *peer)
{
	struct sockaddr_in local;
	int sock;
	int ret;

	if ((s == NULL) || (peer == NULL) || (slot < 0) || (slot >= PSTOP_MAX_MACHINES)) {
		return -EINVAL;
	}

	sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) {
		LOG_ERR("slot %d socket: %d", slot, errno);
		return -errno;
	}

	(void)memset(&local, 0, sizeof(local));
	local.sin_family = AF_INET;
	local.sin_port = htons((uint16_t)(PSTOP_LOCAL_PORT + slot));
	local.sin_addr.s_addr = htonl(INADDR_ANY);

	ret = zsock_bind(sock, (struct sockaddr *)&local, sizeof(local));
	if (ret < 0) {
		LOG_ERR("slot %d bind %u: %d", slot, PSTOP_LOCAL_PORT + slot, errno);
		(void)zsock_close(sock);
		return -errno;
	}

	s->slot = slot;
	s->sock = sock;
	s->peer_id = peer->id;
	s->state = PSTOP_SESS_IDLE;
	s->hb_ms = 0U;
	s->last_send_ms = 0U;
	s->last_reply_ms = 0U;
	s->last_bond_ms = 0U;
	s->sent = 0U;
	s->replies = 0U;
	s->send_fail = 0U;
	s->rebonds = 0U;
	s->last_msg = PSTOP_MESSAGE_UNKNOWN;

	(void)memset(&s->peer_addr, 0, sizeof(s->peer_addr));
	s->peer_addr.sin_family = AF_INET;
	s->peer_addr.sin_port = htons(peer->port);
	s->peer_addr.sin_addr.s_addr = htonl(peer->ip);

	protocol_data_init(&s->proto);
	device_id_set(&s->proto.remote_id, peer->id);

	LOG_INF("slot %d bound :%u -> machine %08x", slot, PSTOP_LOCAL_PORT + slot, peer->id);
	return 0;
}

void pstop_session_close(struct pstop_session *s)
{
	if ((s != NULL) && (s->sock >= 0)) {
		(void)zsock_close(s->sock);
		s->sock = -1;
		s->state = PSTOP_SESS_IDLE;
	}
}

uint32_t pstop_session_send_period_ms(const struct pstop_session *s)
{
	uint32_t period;

	/* No reply yet: transmit at the fast end rather than guessing slow. */
	if (s->hb_ms == 0U) {
		return PSTOP_MIN_SEND_PERIOD_MS;
	}

	/* Half the machine's window gives 2x margin, so ordinary jitter can
	 * never cost the machine a whole heartbeat.
	 */
	period = s->hb_ms / 2U;

	if (period < PSTOP_MIN_SEND_PERIOD_MS) {
		period = PSTOP_MIN_SEND_PERIOD_MS;
	}
	if (period > PSTOP_MAX_SEND_PERIOD_MS) {
		period = PSTOP_MAX_SEND_PERIOD_MS;
	}
	return period;
}

uint32_t pstop_session_rebond_after_ms(const struct pstop_session *s)
{
	uint32_t derived = (s->hb_ms * PSTOP_REBOND_MACHINE_MAX_MISSED) +
			   PSTOP_REBOND_JITTER_MARGIN_MS;

	return (derived < PSTOP_REBOND_FLOOR_MS) ? PSTOP_REBOND_FLOOR_MS : derived;
}

bool pstop_session_due(const struct pstop_session *s, uint64_t now_ms)
{
	if (s->state == PSTOP_SESS_BONDING) {
		return (now_ms - s->last_bond_ms) >= PSTOP_BOND_RETRY_MS;
	}
	if (s->state == PSTOP_SESS_IDLE) {
		return true;
	}
	return (now_ms - s->last_send_ms) >= pstop_session_send_period_ms(s);
}

void pstop_session_build(struct pstop_session *s, uint8_t verdict, uint64_t now_ms,
			 uint8_t *out48)
{
	pstop_msg_t msg;
	device_id_t me;
	uint8_t type;

	device_id_set(&me, app_settings_device_id());

	/* An unbonded session must bond before it can heartbeat. */
	type = (s->state == PSTOP_SESS_BONDED) ? verdict : PSTOP_MESSAGE_BOND;

	if (type == PSTOP_MESSAGE_BOND) {
		/* A bond establishes the counter baseline, so it carries no
		 * received stamp or counter.
		 */
		pstop_create_bond_message(&msg, now_ms, &me, &s->proto.remote_id,
					  s->proto.msg_counter + 1U);
	} else {
		pstop_create_generic_message(&msg, type, now_ms, s->proto.last_received_stamp,
					     &me, &s->proto.remote_id,
					     s->proto.msg_counter + 1U,
					     s->proto.last_received_counter);
	}

	pstop_message_encode(&msg, out48);
}

int pstop_session_commit_send(struct pstop_session *s, const uint8_t *buf48, uint64_t now_ms)
{
	ssize_t n;

	n = zsock_sendto(s->sock, buf48, PSTOP_MESSAGE_SIZE, 0,
			 (struct sockaddr *)&s->peer_addr, sizeof(s->peer_addr));
	if (n != (ssize_t)PSTOP_MESSAGE_SIZE) {
		s->send_fail++;
		LOG_WRN("slot %d send failed: %d", s->slot, errno);
		return -errno;
	}

	/* Counter advances ONLY on a transmitting tick, so the machine sees
	 * contiguous counters despite send decimation. protocol.c rejects gaps
	 * larger than max_lost_messages + 1.
	 */
	s->proto.msg_counter++;
	s->last_send_ms = now_ms;
	s->sent++;

	if (s->state == PSTOP_SESS_IDLE) {
		s->state = PSTOP_SESS_BONDING;
		s->last_bond_ms = now_ms;
	} else if (s->state == PSTOP_SESS_BONDING) {
		s->last_bond_ms = now_ms;
	} else {
		/* bonded; nothing further */
	}

	return 0;
}

void pstop_session_poll(struct pstop_session *s, uint64_t now_ms)
{
	uint8_t buf[PSTOP_MESSAGE_SIZE];
	pstop_msg_t msg;
	ssize_t n;

	for (;;) {
		n = zsock_recvfrom(s->sock, buf, sizeof(buf), ZSOCK_MSG_DONTWAIT, NULL, NULL);
		if (n != (ssize_t)PSTOP_MESSAGE_SIZE) {
			return;
		}

		pstop_message_decode(&msg, buf);

		/* A frame whose checksum does not verify tells us nothing; drop
		 * it rather than adopting its counters.
		 */
		if (msg.checksum != msg.calculated_checksum) {
			LOG_WRN("slot %d bad checksum", s->slot);
			continue;
		}

		s->proto.last_received_counter = msg.counter;
		s->proto.last_received_stamp = msg.stamp;
		s->last_reply_ms = now_ms;
		s->last_msg = msg.message;
		s->replies++;

		/* The machine governs the rate: it advertises its per-operator
		 * window in every reply, including the bond ack. A reply
		 * carrying 0 (a reject path) changes nothing.
		 */
		if (msg.heartbeat_timeout != 0U) {
			s->hb_ms = msg.heartbeat_timeout;
		}

		if (s->state != PSTOP_SESS_BONDED) {
			LOG_INF("slot %d bonded (hb=%u ms)", s->slot, s->hb_ms);
			s->state = PSTOP_SESS_BONDED;
		}
	}
}

void pstop_session_watchdog(struct pstop_session *s, uint64_t now_ms)
{
	if (s->state != PSTOP_SESS_BONDED) {
		return;
	}

	if ((now_ms - s->last_reply_ms) < pstop_session_rebond_after_ms(s)) {
		return;
	}

	/* The link has desynced and will not recover on its own. Re-bond this
	 * session only; the others are untouched.
	 */
	LOG_WRN("slot %d silent for %u ms, re-bonding", s->slot,
		(unsigned int)(now_ms - s->last_reply_ms));
	s->state = PSTOP_SESS_IDLE;
	s->hb_ms = 0U;
	s->rebonds++;
	protocol_data_init(&s->proto);
	device_id_set(&s->proto.remote_id, s->peer_id);
}
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `mise x -- west twister -T applications/protective_stop/tests/session -p native_sim --inline-logs`
Expected: PASS, 10 test cases.

- [ ] **Step 7: Add to the app build and commit**

Add `src/session.c` to `target_sources(app PRIVATE ...)`.

```bash
mise run agent-build protective_stop --board native_sim/native/64
mise x -- clang-format --dry-run --Werror \
  applications/protective_stop/src/session.c \
  applications/protective_stop/src/session.h \
  applications/protective_stop/tests/session/src/main.c
git add applications/protective_stop
git commit -m "feat(protective_stop): per-machine session and bond state machine

One socket per slot so a dead machine cannot stall the heartbeats to the
others. The machine governs the rate: we adopt the heartbeat window it
advertises in every reply and transmit at half of it, clamped to [100, 1000]
ms.

Splitting build from commit_send is load-bearing, not stylistic: both
lockstep samplers call build(), so it must be side-effect free or their
encodings never match. The counter therefore advances only on a committed
send, which is also what keeps counters contiguous under send decimation --
protocol.c rejects gaps beyond max_lost_messages + 1.

The rebond watchdog is derived per session so it always outlasts the
machine's own hb_ms * max_missed bond-drop timeout; firing earlier would
cause nuisance rebonds while the machine still holds the bond.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 6: Lockstep samplers and comparator

**Files:**
- Create: `applications/protective_stop/src/lockstep.h`
- Create: `applications/protective_stop/src/lockstep.c`
- Test: `applications/protective_stop/tests/lockstep/{CMakeLists.txt,prj.conf,testcase.yaml,src/main.c,boards/native_sim_native_64.conf,boards/native_sim_native_64.overlay}`
- Modify: `applications/protective_stop/CMakeLists.txt`

**Interfaces:**
- Consumes: `estop_channel_sample()`, `estop_channels_primed()`, `estop_state_t` (Tasks 2–3); `struct pstop_session` and all `pstop_session_*` (Task 5); `app_settings_peer()` (Task 4).
- Produces:
  - `int pstop_lockstep_init(void)` — opens sessions for every configured peer slot
  - `void pstop_lockstep_tick(uint64_t now_ms)` — one synchronous 10 Hz tick, exposed for tests
  - `int pstop_lockstep_start(void)` — spawns the sampler and comparator threads
  - `uint32_t pstop_lockstep_mismatches(void)` — lifetime comparator mismatch count
  - `const struct pstop_session *pstop_lockstep_session(int slot)`

- [ ] **Step 1: Write the failing test**

Create `applications/protective_stop/tests/lockstep/src/main.c`:

```c
/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * The comparator's contract: transmit only when both samplers produced
 * byte-identical frames, and stay silent until both channels have settled.
 *
 * These use the synchronous pstop_lockstep_tick() rather than the threads, so
 * the tests are deterministic. The threads are exercised by the live interop
 * run in Task 7.
 */

#include <zephyr/ztest.h>

#include "app_settings.h"
#include "estop_gpio.h"
#include "lockstep.h"
#include "session.h"

static void *suite_setup(void)
{
	zassert_ok(app_settings_init(), "settings");
	/* Point slot 0 at a port nothing is listening on: sends succeed at the
	 * socket layer, replies never come, which is all these tests need.
	 */
	zassert_ok(app_settings_set_peer(0, 0x7F000001U, 18890U, 0x01020304U), "peer");
	zassert_ok(app_settings_set_device_id(0xC0A80105U), "device id");
	zassert_ok(estop_gpio_init(), "gpio");
	zassert_ok(pstop_lockstep_init(), "lockstep");
	return NULL;
}

ZTEST(lockstep, test_silent_until_both_channels_settle)
{
	const struct pstop_session *s;

	/* Both poles closed but not yet debounced: nothing may go out. The
	 * boot-priming STOP must never reach a machine, because a machine
	 * treats STOP->OK as the arming gesture and would arm with no operator
	 * action at all.
	 */
	estop_sim_set_pole(0, true);
	estop_sim_set_pole(1, true);

	pstop_lockstep_tick(1000ULL);

	s = pstop_lockstep_session(0);
	zassert_equal(s->sent, 0U, "nothing may be sent before both channels settle");
}

ZTEST(lockstep, test_sends_once_settled)
{
	const struct pstop_session *s;
	uint64_t now = 2000ULL;

	estop_sim_set_pole(0, true);
	estop_sim_set_pole(1, true);

	for (unsigned int i = 0U; i < 8U; i++) {
		pstop_lockstep_tick(now);
		now += 100ULL;
	}

	s = pstop_lockstep_session(0);
	zassert_true(s->sent > 0U, "a settled, agreeing pair must transmit");
}

ZTEST(lockstep, test_split_channels_silence_every_session)
{
	const struct pstop_session *s;
	uint32_t sent_before;
	uint32_t mismatches_before;
	uint64_t now = 5000ULL;

	/* Settle first. */
	estop_sim_set_pole(0, true);
	estop_sim_set_pole(1, true);
	for (unsigned int i = 0U; i < 8U; i++) {
		pstop_lockstep_tick(now);
		now += 100ULL;
	}

	s = pstop_lockstep_session(0);
	sent_before = s->sent;
	mismatches_before = pstop_lockstep_mismatches();

	/* Now break ONE pole: the channels disagree, so nothing may go out and
	 * the machine must time out on heartbeat liveness.
	 */
	estop_sim_set_pole(1, false);
	for (unsigned int i = 0U; i < 10U; i++) {
		pstop_lockstep_tick(now);
		now += 100ULL;
	}

	zassert_equal(s->sent, sent_before, "a channel split must silence transmission");
	zassert_true(pstop_lockstep_mismatches() > mismatches_before,
		     "the mismatch must be counted");
}

ZTEST(lockstep, test_recovers_when_the_split_heals)
{
	const struct pstop_session *s;
	uint32_t sent_before;
	uint64_t now = 9000ULL;

	estop_sim_set_pole(0, true);
	estop_sim_set_pole(1, false);
	for (unsigned int i = 0U; i < 6U; i++) {
		pstop_lockstep_tick(now);
		now += 100ULL;
	}

	s = pstop_lockstep_session(0);
	sent_before = s->sent;

	estop_sim_set_pole(1, true);
	for (unsigned int i = 0U; i < 10U; i++) {
		pstop_lockstep_tick(now);
		now += 100ULL;
	}

	zassert_true(s->sent > sent_before, "transmission must resume once the channels agree");
}

ZTEST_SUITE(lockstep, NULL, suite_setup, NULL, NULL, NULL);
```

- [ ] **Step 2: Add the test build files**

Create `applications/protective_stop/tests/lockstep/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(lockstep)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../pstop_c.cmake)

set(APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

target_sources(app PRIVATE
  src/main.c
  ${APP_SRC}/lockstep.c
  ${APP_SRC}/session.c
  ${APP_SRC}/app_settings.c
  ${APP_SRC}/estop_gpio.c
  ${APP_SRC}/estop_verdict.c
  ${APP_SRC}/pstop_time.c
  ${PSTOP_C_SRCS}
)
target_include_directories(app PRIVATE ${APP_SRC} ${PSTOP_C_INCLUDE})
```

Create `applications/protective_stop/tests/lockstep/prj.conf`:

```
CONFIG_ZTEST=y

CONFIG_GPIO=y
CONFIG_GPIO_EMUL=y

CONFIG_NETWORKING=y
CONFIG_NET_SOCKETS=y

CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_FLASH_SIMULATOR=y
CONFIG_FLASH_SIMULATOR_DOUBLE_WRITES=y
CONFIG_ZMS=y
CONFIG_SETTINGS=y
CONFIG_SETTINGS_ZMS=y
```

Create `applications/protective_stop/tests/lockstep/boards/native_sim_native_64.conf`:

```
CONFIG_NET_NATIVE_OFFLOADED_SOCKETS=y
CONFIG_NET_DRIVERS=y
CONFIG_NET_L2_ETHERNET=n
CONFIG_NET_IPV4=n
CONFIG_NET_UDP=n
```

Copy the overlay:

```bash
mkdir -p applications/protective_stop/tests/lockstep/boards
cp applications/protective_stop/boards/native_sim_native_64.overlay \
   applications/protective_stop/tests/lockstep/boards/native_sim_native_64.overlay
```

Create `applications/protective_stop/tests/lockstep/testcase.yaml`:

```yaml
tests:
  protective_stop.lockstep:
    tags: protective_stop pstop safety lockstep
    platform_allow:
      - native_sim
    integration_platforms:
      - native_sim
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `mise x -- west twister -T applications/protective_stop/tests/lockstep -p native_sim --inline-logs`
Expected: FAIL — `lockstep.h: No such file or directory`.

- [ ] **Step 4: Write the header**

Create `applications/protective_stop/src/lockstep.h`:

```c
/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Lockstep comparator: two samplers independently read one channel each and
 * encode a frame per machine slot; nothing is transmitted unless the two
 * encodings are byte-identical.
 *
 * Comparing ENCODED BYTES rather than verdicts is the point. One memcmp
 * covers the switch reading, the counter, the timestamp, the receiver id and
 * both message buffers -- and the CRC sits inside the compared region, so a
 * fault in CRC computation is caught too.
 *
 * Threads, not pinned cores: the safety property is two independent samplers
 * whose encodings must agree, which threads satisfy. This maps onto CONFIG_SMP
 * pinned cores on hardware with no change to the comparator.
 */

#ifndef PROTECTIVE_STOP_LOCKSTEP_H
#define PROTECTIVE_STOP_LOCKSTEP_H

#include <stdint.h>

#include "session.h"

/* Stop-switch sampling cadence. A safety property: it never slows down, even
 * when the machine asks for a slower heartbeat.
 */
#define PSTOP_TICK_MS 100U

/* Each sampler must publish within this long of the tick, leaving the
 * comparator slack to drain replies and re-align. A missed deadline is
 * treated as a mismatch.
 */
#define PSTOP_SAMPLER_DEADLINE_MS 80U

/* Open a session for every configured peer slot. Returns 0 or -errno. */
int pstop_lockstep_init(void);

/* Run exactly one tick synchronously on the calling thread: sample both
 * channels, build, compare, transmit, poll, run the watchdogs. Exposed so
 * tests can drive the comparator deterministically.
 */
void pstop_lockstep_tick(uint64_t now_ms);

/* Spawn the two sampler threads and the comparator thread. */
int pstop_lockstep_start(void);

/* Lifetime count of ticks where the two encodings disagreed. */
uint32_t pstop_lockstep_mismatches(void);

/* Read a slot's session, or NULL if the slot is out of range. */
const struct pstop_session *pstop_lockstep_session(int slot);

#endif /* PROTECTIVE_STOP_LOCKSTEP_H */
```

- [ ] **Step 5: Write the implementation**

Create `applications/protective_stop/src/lockstep.c`:

```c
/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app_settings.h"
#include "estop_gpio.h"
#include "estop_verdict.h"
#include "lockstep.h"
#include "pstop/pstop_msg.h"
#include "session.h"

LOG_MODULE_REGISTER(lockstep, LOG_LEVEL_INF);

#define SAMPLER_STACK_SIZE 2048
#define COMPARATOR_STACK_SIZE 4096
#define SAMPLER_PRIORITY 4
#define COMPARATOR_PRIORITY 5

static struct pstop_session sessions[PSTOP_MAX_MACHINES];
static bool slot_active[PSTOP_MAX_MACHINES];

static estop_state_t estop[ESTOP_CHANNELS];

/* Per-sampler encoded frames: [channel][slot][48]. The comparator memcmps
 * frames[0][slot] against frames[1][slot].
 */
static uint8_t frames[ESTOP_CHANNELS][PSTOP_MAX_MACHINES][PSTOP_MESSAGE_SIZE];

/* The comparator snapshots these BEFORE waking the samplers, so both samplers
 * encode the same timestamp and the same due-set. Without that the two
 * encodings differ in the stamp field and every tick looks like a mismatch.
 */
static uint64_t tick_now_ms;
static bool tick_due[PSTOP_MAX_MACHINES];

static uint32_t mismatches;

static K_SEM_DEFINE(go_sem_0, 0, 1);
static K_SEM_DEFINE(go_sem_1, 0, 1);
static K_SEM_DEFINE(done_sem_0, 0, 1);
static K_SEM_DEFINE(done_sem_1, 0, 1);

static struct k_sem *const go_sem[ESTOP_CHANNELS] = {&go_sem_0, &go_sem_1};
static struct k_sem *const done_sem[ESTOP_CHANNELS] = {&done_sem_0, &done_sem_1};

int pstop_lockstep_init(void)
{
	int opened = 0;

	for (int c = 0; c < ESTOP_CHANNELS; c++) {
		estop_state_init(&estop[c]);
	}

	for (int slot = 0; slot < PSTOP_MAX_MACHINES; slot++) {
		const struct pstop_peer *peer = app_settings_peer(slot);
		int ret;

		slot_active[slot] = false;
		sessions[slot].sock = -1;

		if ((peer == NULL) || !peer->configured) {
			continue;
		}

		ret = pstop_session_open(&sessions[slot], slot, peer);
		if (ret != 0) {
			LOG_ERR("slot %d open failed: %d", slot, ret);
			continue;
		}

		slot_active[slot] = true;
		opened++;
	}

	LOG_INF("lockstep ready: %d session(s)", opened);
	return 0;
}

/* Sample one channel and encode every due slot's frame into that channel's
 * buffer. Runs on the sampler thread, or inline from pstop_lockstep_tick().
 */
static void sampler_pass(int channel)
{
	uint8_t verdict = estop_channel_sample(channel, &estop[channel]);

	for (int slot = 0; slot < PSTOP_MAX_MACHINES; slot++) {
		if (slot_active[slot] && tick_due[slot]) {
			pstop_session_build(&sessions[slot], verdict, tick_now_ms,
					    frames[channel][slot]);
		}
	}
}

/* Compare and transmit. Returns true if every due slot agreed. */
static bool comparator_pass(void)
{
	bool all_agreed = true;

	/* Hold everything back until both channels have settled. The
	 * boot-priming STOP must never reach a machine: a machine reads
	 * STOP->OK as the arming gesture and would arm with no operator action.
	 */
	if (!estop_channels_primed(estop)) {
		return true;
	}

	for (int slot = 0; slot < PSTOP_MAX_MACHINES; slot++) {
		if (!slot_active[slot] || !tick_due[slot]) {
			continue;
		}

		if (memcmp(frames[0][slot], frames[1][slot], PSTOP_MESSAGE_SIZE) != 0) {
			/* Silence is the safe action: every bonded machine
			 * heartbeat-times-out and stops.
			 */
			all_agreed = false;
			continue;
		}

		(void)pstop_session_commit_send(&sessions[slot], frames[0][slot], tick_now_ms);
	}

	if (!all_agreed) {
		mismatches++;
	}

	return all_agreed;
}

void pstop_lockstep_tick(uint64_t now_ms)
{
	tick_now_ms = now_ms;

	for (int slot = 0; slot < PSTOP_MAX_MACHINES; slot++) {
		tick_due[slot] = slot_active[slot] &&
				 pstop_session_due(&sessions[slot], now_ms);
	}

	for (int c = 0; c < ESTOP_CHANNELS; c++) {
		sampler_pass(c);
	}

	(void)comparator_pass();

	for (int slot = 0; slot < PSTOP_MAX_MACHINES; slot++) {
		if (slot_active[slot]) {
			pstop_session_poll(&sessions[slot], now_ms);
			pstop_session_watchdog(&sessions[slot], now_ms);
		}
	}
}

uint32_t pstop_lockstep_mismatches(void)
{
	return mismatches;
}

const struct pstop_session *pstop_lockstep_session(int slot)
{
	if ((slot < 0) || (slot >= PSTOP_MAX_MACHINES)) {
		return NULL;
	}
	return &sessions[slot];
}

static void sampler_thread(void *p1, void *p2, void *p3)
{
	int channel = (int)(intptr_t)p1;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (;;) {
		k_sem_take(go_sem[channel], K_FOREVER);
		sampler_pass(channel);
		k_sem_give(done_sem[channel]);
	}
}

static void comparator_thread(void *p1, void *p2, void *p3)
{
	int64_t next = k_uptime_get();

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (;;) {
		bool published = true;

		next += PSTOP_TICK_MS;

		/* Snapshot the tick's shared inputs BEFORE waking the samplers,
		 * so both encode identical stamps and the same due-set.
		 */
		tick_now_ms = (uint64_t)k_uptime_get();
		for (int slot = 0; slot < PSTOP_MAX_MACHINES; slot++) {
			tick_due[slot] = slot_active[slot] &&
					 pstop_session_due(&sessions[slot], tick_now_ms);
		}

		for (int c = 0; c < ESTOP_CHANNELS; c++) {
			k_sem_give(go_sem[c]);
		}

		for (int c = 0; c < ESTOP_CHANNELS; c++) {
			if (k_sem_take(done_sem[c], K_MSEC(PSTOP_SAMPLER_DEADLINE_MS)) != 0) {
				/* A sampler missed its deadline. Treat it as a
				 * mismatch: we cannot know its frame is current.
				 */
				LOG_WRN("sampler %d missed its deadline", c);
				published = false;
			}
		}

		if (published) {
			(void)comparator_pass();
		} else {
			mismatches++;
		}

		for (int slot = 0; slot < PSTOP_MAX_MACHINES; slot++) {
			if (slot_active[slot]) {
				pstop_session_poll(&sessions[slot], tick_now_ms);
				pstop_session_watchdog(&sessions[slot], tick_now_ms);
			}
		}

		k_sleep(K_TIMEOUT_ABS_MS(next));
	}
}

K_THREAD_STACK_DEFINE(sampler_stack_0, SAMPLER_STACK_SIZE);
K_THREAD_STACK_DEFINE(sampler_stack_1, SAMPLER_STACK_SIZE);
K_THREAD_STACK_DEFINE(comparator_stack, COMPARATOR_STACK_SIZE);

static struct k_thread sampler_thread_data[ESTOP_CHANNELS];
static struct k_thread comparator_thread_data;

int pstop_lockstep_start(void)
{
	(void)k_thread_create(&sampler_thread_data[0], sampler_stack_0, SAMPLER_STACK_SIZE,
			      sampler_thread, (void *)(intptr_t)0, NULL, NULL,
			      SAMPLER_PRIORITY, 0, K_NO_WAIT);
	(void)k_thread_name_set(&sampler_thread_data[0], "pstop_ch_a");

	(void)k_thread_create(&sampler_thread_data[1], sampler_stack_1, SAMPLER_STACK_SIZE,
			      sampler_thread, (void *)(intptr_t)1, NULL, NULL,
			      SAMPLER_PRIORITY, 0, K_NO_WAIT);
	(void)k_thread_name_set(&sampler_thread_data[1], "pstop_ch_b");

	(void)k_thread_create(&comparator_thread_data, comparator_stack, COMPARATOR_STACK_SIZE,
			      comparator_thread, NULL, NULL, NULL, COMPARATOR_PRIORITY, 0,
			      K_NO_WAIT);
	(void)k_thread_name_set(&comparator_thread_data, "pstop_cmp");

	LOG_INF("lockstep threads started");
	return 0;
}
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `mise x -- west twister -T applications/protective_stop/tests/lockstep -p native_sim --inline-logs`
Expected: PASS, 4 test cases.

- [ ] **Step 7: Add to the app build and commit**

Add `src/lockstep.c` to `target_sources(app PRIVATE ...)`.

```bash
mise run agent-build protective_stop --board native_sim/native/64
mise x -- clang-format --dry-run --Werror \
  applications/protective_stop/src/lockstep.c \
  applications/protective_stop/src/lockstep.h \
  applications/protective_stop/tests/lockstep/src/main.c
git add applications/protective_stop
git commit -m "feat(protective_stop): lockstep samplers and comparator

Two samplers read one channel each and encode a frame per slot; nothing is
transmitted unless the two encodings are byte-identical. Comparing encoded
bytes rather than verdicts means one memcmp covers the switch reading, the
counter, the stamp, the receiver id and both buffers -- and the CRC is inside
the compared region, so a fault in CRC computation is caught too.

The comparator snapshots the tick timestamp and the due-set before waking the
samplers. Without that the two encodings differ in the stamp field and every
tick would look like a mismatch.

Transmission is held back until both channels settle, so the boot-priming
STOP never reaches a machine -- a machine reads STOP->OK as the arming
gesture and would otherwise arm with no operator action at all.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 7: Wire up main and prove interop against upstream

**Files:**
- Modify: `applications/protective_stop/src/main.c`
- Create: `applications/protective_stop/tests/interop/README.md`
- Modify: `applications/protective_stop/README.md`

**Interfaces:**
- Consumes: everything from Tasks 1–6.
- Produces: a running remote; no new API.

- [ ] **Step 1: Rewrite main**

Replace `applications/protective_stop/src/main.c`:

```c
/*
 * Copyright (c) 2026 Richard Osterloh <richard.osterloh@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * protective_stop: a Zephyr pstop REMOTE.
 *
 * Init order matters. Settings first (the peer table decides which sessions
 * exist), then the stop-switch GPIOs (so the first sampler tick reads real
 * pins), then the sessions, and only then the threads.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app_settings.h"
#include "estop_gpio.h"
#include "lockstep.h"
#include "pstop/config.h"

LOG_MODULE_REGISTER(pstop, LOG_LEVEL_INF);

/* Fallback peer, applied only when nothing has been provisioned yet. Matches
 * upstream's machine_app defaults so a fresh native_sim build talks to it
 * without any configuration. Slice 3's POST /api/pstop_peers replaces this.
 */
#define DEFAULT_MACHINE_IP   0x7F000001U /* 127.0.0.1 */
#define DEFAULT_MACHINE_PORT 8890U
#define DEFAULT_MACHINE_ID   0x01020304U
#define DEFAULT_DEVICE_ID    0xC0A80105U /* 192.168.1.5 */

int main(void)
{
	int ret;

	LOG_INF("protective_stop remote: pstop v%u, %u-byte frames",
		(unsigned int)PSTOP_VERSION, (unsigned int)PSTOP_MESSAGE_SIZE);

	ret = app_settings_init();
	if (ret != 0) {
		LOG_ERR("settings init failed: %d", ret);
		return ret;
	}

	if (app_settings_peer(0)->configured == false) {
		LOG_WRN("no peer provisioned; using the built-in default");
		(void)app_settings_set_peer(0, DEFAULT_MACHINE_IP, DEFAULT_MACHINE_PORT,
					    DEFAULT_MACHINE_ID);
	}

	if (app_settings_device_id() == 0U) {
		/* Slice 4 derives this from the active uplink. Until then a
		 * pinned id keeps the machine's operator allowlist stable.
		 */
		LOG_WRN("no device id provisioned; using the built-in default");
		(void)app_settings_set_device_id(DEFAULT_DEVICE_ID);
	}

	ret = estop_gpio_init();
	if (ret != 0) {
		LOG_ERR("stop-switch gpio init failed: %d", ret);
		return ret;
	}

	ret = pstop_lockstep_init();
	if (ret != 0) {
		LOG_ERR("lockstep init failed: %d", ret);
		return ret;
	}

	return pstop_lockstep_start();
}
```

- [ ] **Step 2: Build**

Run: `mise run agent-build protective_stop --board native_sim/native/64`
Expected: build succeeds.

- [ ] **Step 3: Build upstream's machine**

```bash
cmake -S deps/modules/lib/protective-stop/pstop_c -B /tmp/pstop_machine_build
cmake --build /tmp/pstop_machine_build --target machine_app
```

Expected: `/tmp/pstop_machine_build/machine_app` exists.

- [ ] **Step 4: Run the machine**

```bash
/tmp/pstop_machine_build/machine_app 8890
```

Expected: `Connected to localhost:8890`. Leave it running in its own terminal.

- [ ] **Step 5: Run the remote against it**

In a second terminal:

```bash
./builds/protective_stop/zephyr/zephyr.exe
```

Expected, on the **machine** side:

```
Got message: 173 from C0A80105      <- 173 = 0xAD = BOND
[...] 192.168.1.5 BOND 0
Got message: 146 from C0A80105      <- 146 = 0x92 = STOP
Robot Status = STOP
```

The remote logs `slot 0 bonded (hb=1000 ms)`. It reports STOP because with
`gpio_emul` and no simulated pole closed, both loops read open — which is the
correct fail-safe.

If the machine prints nothing, check in order: the remote's `slot 0 bound
:8891` line appeared; `CONFIG_NET_NATIVE_OFFLOADED_SOCKETS=y` is in the
native_sim conf (without it `zsock_*` goes to Zephyr's own stack and never
reaches the host loopback); the machine is on port 8890.

- [ ] **Step 6: Record the interop procedure**

Create `applications/protective_stop/tests/interop/README.md`:

```markdown
# Live interop against upstream's machine

This is the real proof of API compatibility: our remote against upstream's
own machine implementation, unmodified, over host loopback.

`native_sim` uses `CONFIG_NET_NATIVE_OFFLOADED_SOCKETS`, so `zsock_*` calls
go straight to host syscalls. No TAP interface, no `net-tools`, no root.

## Build the machine

    cmake -S deps/modules/lib/protective-stop/pstop_c -B /tmp/pstop_machine_build
    cmake --build /tmp/pstop_machine_build --target machine_app

## Run the pair

Terminal 1:

    /tmp/pstop_machine_build/machine_app 8890

Terminal 2:

    mise run app protective_stop
    ./builds/protective_stop/zephyr/zephyr.exe

## What to expect

The machine logs each frame by type: `173` = BOND (0xAD), `85` = OK (0x55),
`146` = STOP (0x92). A successful bond is followed by heartbeats at 500 ms,
because `machine_app` advertises a 1000 ms window and the remote transmits at
half of it.

With no simulated pole closed, both loops read open and the remote correctly
reports STOP — fail-safe is the right behaviour for an unwired switch, not a
bug.

## Reading the counters

`machine_app` prints `Invalid request: N` for rejected frames. The ones that
matter here:

| N | Meaning | Likely cause |
|---|---|---|
| 10 | `PSTOP_MSG_LOST` | counter gap beyond `max_lost_messages + 1` — the counter advanced on a non-transmitting tick |
| 11 | `PSTOP_MSG_REPETITION` | duplicate counter — two BONDs in flight |
| 14 | `PSTOP_MSG_INVALID_CHECKSUM` | frame corrupted, or a field-layout divergence |
| 15 | `PSTOP_ERROR_INVALID_ID` | `receiver_id` does not match the machine's `machine_device_id` |
```

- [ ] **Step 7: Update the app README and commit**

Add to `applications/protective_stop/README.md`, after the Tests section:

```markdown
## Interop

See `tests/interop/README.md` for running this remote against upstream's own
`machine_app`.
```

```bash
mise x -- clang-format --dry-run --Werror applications/protective_stop/src/main.c
git add applications/protective_stop
git commit -m "feat(protective_stop): wire up main and document live interop

Init order is load-bearing: settings first because the peer table decides
which sessions exist, then the stop-switch GPIOs so the first sampler tick
reads real pins, then sessions, then threads.

Built-in defaults match upstream's machine_app, so a fresh native_sim build
bonds against it with no provisioning. Slice 3's POST /api/pstop_peers
replaces them.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

- [ ] **Step 8: Run the whole suite**

Run: `mise x -- west twister -T applications/protective_stop -p native_sim --inline-logs`
Expected: 5 suites pass — `wire`, `verdict`, `gpio`, `settings`, `session`, `lockstep`.

---

## Slice 2 done when

- [ ] `mise x -- west twister -T applications/protective_stop -p native_sim` is green.
- [ ] `mise run agent-build protective_stop --board native_sim/native/64` succeeds.
- [ ] The remote bonds against upstream's `machine_app` and heartbeats at half the advertised window.
- [ ] Breaking one simulated pole silences transmission and the machine stops on heartbeat loss.

## Deliberately not in this slice

HTTP control plane (slice 3). WS2812, W5500, WiFi and coredump routes (slice 4). OTA (deferred). `UNBOND` on shutdown — upstream's remote does not send it either; the machine ages the bond out.
