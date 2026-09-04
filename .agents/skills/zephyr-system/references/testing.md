# Testing (Ztest + Twister)

## Overview

Expert guidance for testing Zephyr RTOS applications using Ztest framework, Twister test runner, and FFF mocking.

### Table of Contents

1. [Quick Start](#quick-start)
2. [Test Project Structure](#test-project-structure)
3. [Ztest Framework Basics](#ztest-framework-basics)
4. [Running Tests with Twister](#running-tests-with-twister)
5. [Advanced Topics](#advanced-topics)
6. [References](#references)

---

### Quick Start

#### Minimal Integration Test

```
tests/foo/bar/
├── CMakeLists.txt
├── prj.conf
├── testcase.yaml
└── src/
    └── main.c
```

**CMakeLists.txt:**
```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(my_test)
target_sources(app PRIVATE src/main.c)
```

**prj.conf:**
```
CONFIG_ZTEST=y
```

**testcase.yaml:**
```yaml
tests:
  mysubsystem.feature.basic:
    tags: feature
    platform_allow:
      - native_sim
    integration_platforms:
      - native_sim
```

**src/main.c:**
```c
#include <zephyr/ztest.h>

ZTEST_SUITE(my_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(my_suite, test_example)
{
    zassert_true(1, "1 was false");
    zassert_equal(2 + 2, 4, "math is broken");
}
```

**Run:**
```bash
./scripts/twister -T tests/foo/bar/ -p native_sim
```

#### Minimal Unit Test

Unit tests compile only the module under test (no full Zephyr OS).

**CMakeLists.txt:**
```cmake
cmake_minimum_required(VERSION 3.20.0)
project(app)
find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})
target_sources(testbinary PRIVATE main.c)
```

**testcase.yaml:**
```yaml
tests:
  mymodule.unit:
    tags: unit
    type: unit
```

**Run:**
```bash
./scripts/twister -T tests/unit/mymodule/
```

---

### Test Project Structure

#### Naming Conventions

| Element | Convention | Example |
|---------|------------|---------|
| Test scenario ID | `subsystem.component[.variant]` | `kernel.semaphore.stress` |
| Ztest suite name | snake_case, descriptive | `semaphore_tests` |
| Test function | `test_<what_is_tested>` | `test_sem_give_take` |

#### testcase.yaml Key Options

| Key | Purpose | Example |
|-----|---------|---------|
| `tags` | Categorize tests | `tags: kernel thread` |
| `platform_allow` | Only run on these | `platform_allow: [native_sim]` |
| `platform_exclude` | Never run on these | `platform_exclude: [qemu_x86]` |
| `depends_on` | Require board features | `depends_on: gpio i2c` |
| `build_only` | Compile but don't run | `build_only: true` |
| `timeout` | Seconds before kill | `timeout: 120` |
| `extra_configs` | Merge Kconfig options | `extra_configs: [CONFIG_LOG=y]` |
| `harness` | Test harness type | `harness: ztest` |
| `type` | Test type | `type: unit` |
| `filter` | Expression-based filter | `filter: CONFIG_BT` |

---

### Ztest Framework Basics

#### Suite Definition

```c
ZTEST_SUITE(suite_name, predicate, setup, before, after, teardown);
```

| Parameter | Type | Purpose |
|-----------|------|---------|
| `suite_name` | identifier | Unique suite name |
| `predicate` | `bool (*)(const void *)` | Optional: decides if suite runs |
| `setup` | `void *(*)(void)` | Optional: runs once, returns fixture |
| `before` | `void (*)(void *)` | Optional: runs before each test |
| `after` | `void (*)(void *)` | Optional: runs after each test |
| `teardown` | `void (*)(void *)` | Optional: runs once at end |

#### Test Macros

| Macro | When to Use |
|-------|-------------|
| `ZTEST(suite, test)` | Standard test |
| `ZTEST_F(suite, test)` | Test with fixture access |
| `ZTEST_USER(suite, test)` | Runs in userspace thread |
| `ZTEST_USER_F(suite, test)` | Userspace + fixture |
| `ZTEST_P(suite, test)` | Parameterized test |

#### Assertions (Fail Immediately)

| Assertion | Checks |
|-----------|--------|
| `zassert_true(cond, msg)` | cond is true |
| `zassert_false(cond, msg)` | cond is false |
| `zassert_equal(a, b, msg)` | a == b |
| `zassert_not_equal(a, b, msg)` | a != b |
| `zassert_is_null(ptr, msg)` | ptr is NULL |
| `zassert_not_null(ptr, msg)` | ptr is not NULL |
| `zassert_mem_equal(a, b, sz, msg)` | memory equal |
| `zassert_str_equal(a, b, msg)` | strings equal |
| `zassert_within(a, b, delta, msg)` | \|a - b\| <= delta |
| `zassert_ok(ret, msg)` | ret == 0 |

#### Expectations (Continue, Fail at End)

Replace `zassert_` with `zexpect_` for non-fatal assertions that continue execution.

#### Assumptions (Skip on Failure)

Replace `zassert_` with `zassume_` to skip the test if condition fails.

#### Skipping Tests

```c
ZTEST(common, test_feature)
{
    Z_TEST_SKIP_IFDEF(CONFIG_FEATURE_DISABLED);
    // or
    ztest_test_skip();
}
```

#### Expected Failures

```c
ZTEST_EXPECT_FAIL(suite, test_known_broken);
ZTEST(suite, test_known_broken)
{
    zassert_true(false, NULL);  // Marked as PASS because failure expected
}
```

---

### Running Tests with Twister

#### Common Commands

```bash
# Run all tests in a directory
./scripts/twister -T tests/kernel/

# Run on specific platform
./scripts/twister -p native_sim -T tests/kernel/threads/

# Run specific test scenario
./scripts/twister --scenario tests/kernel/semaphore/kernel.semaphore

# List available tests
./scripts/twister --list-tests -T tests/kernel/

# Build only (no execution)
./scripts/twister --build-only -T tests/subsys/

# Run on hardware
./scripts/twister --device-testing --device-serial /dev/ttyACM0 -p nrf52840dk/nrf52840 -T tests/
```

#### Test on Hardware

**Single device:**
```bash
./scripts/twister --device-testing --device-serial /dev/ttyACM0 -p frdm_k64f -T tests/kernel
```

**Multiple devices (hardware map):**
```bash
# Generate hardware map
./scripts/twister --generate-hardware-map map.yml

# Edit map.yml to set correct platform names, then:
./scripts/twister --device-testing --hardware-map map.yml -T tests/
```

#### Twister Output Files

| File | Content |
|------|---------|
| `twister.json` | Full results in JSON |
| `twister.log` | Build/run logs |
| `twister_discard.csv` | Skipped tests with reasons |

---

### Advanced Topics

#### Test Fixtures

```c
struct my_fixture {
    int value;
    uint8_t buffer[256];
};

static void *my_suite_setup(void)
{
    struct my_fixture *f = malloc(sizeof(*f));
    f->value = 42;
    return f;
}

static void my_suite_before(void *f)
{
    struct my_fixture *fixture = f;
    memset(fixture->buffer, 0, sizeof(fixture->buffer));
}

static void my_suite_teardown(void *f)
{
    free(f);
}

ZTEST_SUITE(my_suite, NULL, my_suite_setup, my_suite_before, NULL, my_suite_teardown);

ZTEST_F(my_suite, test_with_fixture)
{
    zassert_equal(fixture->value, 42, "fixture not initialized");
}
```

#### Test Rules (Global Hooks)

Apply logic to every test across all suites:

```c
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

DEFINE_FFF_GLOBALS;

static void reset_mocks_before(const struct ztest_unit_test *test, void *fixture)
{
    ARG_UNUSED(test);
    ARG_UNUSED(fixture);
    // Reset all fakes here
}

ZTEST_RULE(mock_reset_rule, reset_mocks_before, NULL);
```

#### Mocking with FFF

- **FFF mocking patterns**: See [#mocking](#mocking)

#### Pytest Integration

For complex integration tests requiring host-side logic:

- **Pytest harness usage**: See [#twister](#pytest-integration)

#### Stress Testing (Ztress)

Test code resilience to preemptions:

```c
#include <zephyr/ztest.h>

ztress_set_timeout(K_MSEC(10000));
ZTRESS_EXECUTE(
    ZTRESS_TIMER(timer_handler, NULL, 10000, Z_TIMEOUT_TICKS(20)),
    ZTRESS_THREAD(thread_handler, NULL, 10000, 0, Z_TIMEOUT_TICKS(20))
);
```

---

### References

- [#ztest](#ztest) - Complete Ztest API, macros, and patterns
- [#twister](#twister) - Twister configuration, harnesses, hardware testing
- [#mocking](#mocking) - FFF fake function framework patterns
- [#examples](#examples) - Complete working test examples

## Examples

Complete working test examples for common scenarios.

### Table of Contents

1. [Minimal Integration Test](#minimal-integration-test)
2. [Minimal Unit Test](#minimal-unit-test)
3. [Test with Fixtures](#test-with-fixtures)
4. [Test with FFF Mocking](#test-with-fff-mocking)
5. [Test with Multiple Suites](#test-with-multiple-suites)
6. [Parameterized Tests](#parameterized-tests)
7. [Stress Test with Ztress](#stress-test-with-ztress)
8. [Pytest Integration Test](#pytest-integration-test)

---

### Minimal Integration Test

#### File Structure

```
tests/mysubsys/feature/
├── CMakeLists.txt
├── prj.conf
├── testcase.yaml
└── src/
    └── main.c
```

#### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(my_test)

target_sources(app PRIVATE src/main.c)
```

#### prj.conf

```
CONFIG_ZTEST=y
```

#### testcase.yaml

```yaml
tests:
  mysubsys.feature.basic:
    tags: feature
    platform_allow:
      - native_sim
    integration_platforms:
      - native_sim
```

#### src/main.c

```c
#include <zephyr/ztest.h>

ZTEST_SUITE(feature_tests, NULL, NULL, NULL, NULL, NULL);

ZTEST(feature_tests, test_basic_assertion)
{
    zassert_true(1, "true should be true");
    zassert_false(0, "false should be false");
}

ZTEST(feature_tests, test_equality)
{
    int a = 42;
    int b = 42;
    zassert_equal(a, b, "a and b should be equal");
}

ZTEST(feature_tests, test_string)
{
    const char *expected = "hello";
    const char *actual = "hello";
    zassert_str_equal(actual, expected, "strings should match");
}
```

#### Run

```bash
./scripts/twister -T tests/mysubsys/feature/ -p native_sim
```

---

### Minimal Unit Test

Unit tests compile only the module under test without full Zephyr.

#### File Structure

```
tests/unit/mymodule/
├── CMakeLists.txt
├── prj.conf
├── testcase.yaml
└── src/
    ├── main.c
    └── mymodule.c  # Module under test
```

#### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20.0)
project(mymodule_unit_test)

find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})

target_sources(testbinary PRIVATE
    src/main.c
    src/mymodule.c
)

target_include_directories(testbinary PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
```

#### prj.conf

```
CONFIG_ZTEST=y
```

#### testcase.yaml

```yaml
tests:
  mymodule.unit.basic:
    tags: unit
    type: unit
```

#### src/mymodule.c

```c
/* Module under test */
#include "mymodule.h"

int mymodule_add(int a, int b)
{
    return a + b;
}

int mymodule_multiply(int a, int b)
{
    return a * b;
}
```

#### src/main.c

```c
#include <zephyr/ztest.h>
#include "mymodule.h"

ZTEST_SUITE(mymodule_tests, NULL, NULL, NULL, NULL, NULL);

ZTEST(mymodule_tests, test_add)
{
    zassert_equal(mymodule_add(2, 3), 5, "2 + 3 should be 5");
    zassert_equal(mymodule_add(-1, 1), 0, "-1 + 1 should be 0");
}

ZTEST(mymodule_tests, test_multiply)
{
    zassert_equal(mymodule_multiply(3, 4), 12, "3 * 4 should be 12");
    zassert_equal(mymodule_multiply(0, 100), 0, "0 * 100 should be 0");
}
```

---

### Test with Fixtures

#### src/main.c

```c
#include <zephyr/ztest.h>
#include <stdlib.h>
#include <string.h>

/* Fixture struct - must be named <suite_name>_fixture */
struct buffer_tests_fixture {
    uint8_t *buffer;
    size_t size;
    size_t used;
};

/* Setup - runs once at suite start */
static void *buffer_tests_setup(void)
{
    struct buffer_tests_fixture *f = malloc(sizeof(*f));

    zassume_not_null(f, "malloc failed");

    f->size = 256;
    f->buffer = malloc(f->size);
    zassume_not_null(f->buffer, "buffer malloc failed");

    return f;
}

/* Before - runs before each test */
static void buffer_tests_before(void *f)
{
    struct buffer_tests_fixture *fixture = f;

    memset(fixture->buffer, 0, fixture->size);
    fixture->used = 0;
}

/* Teardown - runs once at suite end */
static void buffer_tests_teardown(void *f)
{
    struct buffer_tests_fixture *fixture = f;

    free(fixture->buffer);
    free(fixture);
}

ZTEST_SUITE(buffer_tests, NULL, buffer_tests_setup,
            buffer_tests_before, NULL, buffer_tests_teardown);

ZTEST_F(buffer_tests, test_buffer_empty)
{
    zassert_equal(fixture->used, 0, "buffer should be empty");
    zassert_equal(fixture->buffer[0], 0, "buffer should be zeroed");
}

ZTEST_F(buffer_tests, test_buffer_write)
{
    fixture->buffer[0] = 0xAB;
    fixture->used = 1;

    zassert_equal(fixture->used, 1, "used should be 1");
    zassert_equal(fixture->buffer[0], 0xAB, "data should be written");
}

ZTEST_F(buffer_tests, test_buffer_size)
{
    zassert_equal(fixture->size, 256, "size should be 256");
}
```

---

### Test with FFF Mocking

#### src/main.c

```c
#include <zephyr/ztest.h>
#include <zephyr/fff.h>

DEFINE_FFF_GLOBALS;

/* Mock external dependencies */
DEFINE_FAKE_VALUE_FUNC(int, external_init);
DEFINE_FAKE_VALUE_FUNC(int, external_read, uint8_t *, size_t);
DEFINE_FAKE_VOID_FUNC(external_cleanup);

/* FFF fakes list for easy reset */
#define FFF_FAKES_LIST(FAKE) \
    FAKE(external_init)      \
    FAKE(external_read)      \
    FAKE(external_cleanup)

/* Code under test */
static int my_init(void)
{
    return external_init();
}

static int my_read(uint8_t *buf, size_t len)
{
    if (external_init() != 0) {
        return -1;
    }
    int result = external_read(buf, len);
    external_cleanup();
    return result;
}

/* Test rule to reset all fakes before each test */
static void fff_reset_before(const struct ztest_unit_test *test, void *fixture)
{
    ARG_UNUSED(test);
    ARG_UNUSED(fixture);
    FFF_FAKES_LIST(RESET_FAKE);
}

ZTEST_RULE(fff_reset, fff_reset_before, NULL);

ZTEST_SUITE(mock_tests, NULL, NULL, NULL, NULL, NULL);

ZTEST(mock_tests, test_init_success)
{
    external_init_fake.return_val = 0;

    int result = my_init();

    zassert_equal(result, 0, "init should succeed");
    zassert_equal(external_init_fake.call_count, 1, "init should be called once");
}

ZTEST(mock_tests, test_init_failure)
{
    external_init_fake.return_val = -1;

    int result = my_init();

    zassert_equal(result, -1, "init should fail");
}

ZTEST(mock_tests, test_read_success)
{
    external_init_fake.return_val = 0;
    external_read_fake.return_val = 10;

    uint8_t buf[32];
    int result = my_read(buf, sizeof(buf));

    zassert_equal(result, 10, "read should return 10");
    zassert_equal(external_init_fake.call_count, 1, "init called");
    zassert_equal(external_read_fake.call_count, 1, "read called");
    zassert_equal(external_cleanup_fake.call_count, 1, "cleanup called");

    /* Verify read was called with correct arguments */
    zassert_equal(external_read_fake.arg0_val, buf, "buffer passed");
    zassert_equal(external_read_fake.arg1_val, sizeof(buf), "size passed");
}

ZTEST(mock_tests, test_read_fails_if_init_fails)
{
    external_init_fake.return_val = -1;

    uint8_t buf[32];
    int result = my_read(buf, sizeof(buf));

    zassert_equal(result, -1, "should fail");
    zassert_equal(external_read_fake.call_count, 0, "read not called");
}

/* Custom fake example */
static int custom_read_impl(uint8_t *buf, size_t len)
{
    memset(buf, 0xAB, len);
    return len;
}

ZTEST(mock_tests, test_read_with_custom_fake)
{
    external_init_fake.return_val = 0;
    external_read_fake.custom_fake = custom_read_impl;

    uint8_t buf[8];
    int result = my_read(buf, sizeof(buf));

    zassert_equal(result, 8, "should read 8 bytes");
    zassert_equal(buf[0], 0xAB, "buffer should be filled");
}
```

---

### Test with Multiple Suites

#### src/main.c

```c
#include <zephyr/ztest.h>

/* First suite - basic tests */
ZTEST_SUITE(basic_tests, NULL, NULL, NULL, NULL, NULL);

ZTEST(basic_tests, test_one)
{
    zassert_true(1, "test one");
}

ZTEST(basic_tests, test_two)
{
    zassert_true(1, "test two");
}

/* Second suite - with predicate */
struct test_state {
    bool advanced_enabled;
};

static bool advanced_predicate(const void *state)
{
    const struct test_state *s = state;
    return s != NULL && s->advanced_enabled;
}

ZTEST_SUITE(advanced_tests, advanced_predicate, NULL, NULL, NULL, NULL);

ZTEST(advanced_tests, test_advanced_one)
{
    zassert_true(1, "advanced test");
}

/* Third suite - with fixture */
struct fixture_tests_fixture {
    int value;
};

static void *fixture_tests_setup(void)
{
    struct fixture_tests_fixture *f = malloc(sizeof(*f));
    f->value = 42;
    return f;
}

static void fixture_tests_teardown(void *f)
{
    free(f);
}

ZTEST_SUITE(fixture_tests, NULL, fixture_tests_setup,
            NULL, NULL, fixture_tests_teardown);

ZTEST_F(fixture_tests, test_with_fixture)
{
    zassert_equal(fixture->value, 42, "fixture value");
}
```

---

### Parameterized Tests

#### src/main.c

```c
#include <zephyr/ztest.h>

/* Test parameters */
struct math_params {
    int a;
    int b;
    int expected_sum;
    int expected_product;
};

/* Parameter sets */
static struct math_params math_test_params[] = {
    {.a = 1, .b = 2, .expected_sum = 3, .expected_product = 2},
    {.a = 0, .b = 0, .expected_sum = 0, .expected_product = 0},
    {.a = -1, .b = 1, .expected_sum = 0, .expected_product = -1},
    {.a = 100, .b = 200, .expected_sum = 300, .expected_product = 20000},
};

ZTEST_SUITE(param_tests, NULL, NULL, NULL, NULL, NULL);

/* Parameterized test - runs once for each parameter set */
ZTEST_P(param_tests, test_addition, math_test_params)
{
    int result = data->a + data->b;
    zassert_equal(result, data->expected_sum,
                  "%d + %d = %d, expected %d",
                  data->a, data->b, result, data->expected_sum);
}

ZTEST_P(param_tests, test_multiplication, math_test_params)
{
    int result = data->a * data->b;
    zassert_equal(result, data->expected_product,
                  "%d * %d = %d, expected %d",
                  data->a, data->b, result, data->expected_product);
}
```

---

### Stress Test with Ztress

#### prj.conf

```
CONFIG_ZTEST=y
CONFIG_ZTRESS=y
CONFIG_ZTRESS_MAX_THREADS=3
CONFIG_SYS_CLOCK_TICKS_PER_SEC=100000
```

#### src/main.c

```c
#include <zephyr/ztest.h>
#include <zephyr/kernel.h>

/* Shared state for stress test */
static atomic_t counter = ATOMIC_INIT(0);
static K_SEM_DEFINE(sem, 1, 1);

/* Handler for timer context (highest priority) */
static bool timer_handler(void *user_data, uint32_t cnt, bool last_iter)
{
    ARG_UNUSED(user_data);
    ARG_UNUSED(cnt);
    ARG_UNUSED(last_iter);

    atomic_inc(&counter);
    return true;
}

/* Handler for high priority thread */
static bool thread_high_handler(void *user_data, uint32_t cnt, bool last_iter)
{
    ARG_UNUSED(user_data);
    ARG_UNUSED(cnt);
    ARG_UNUSED(last_iter);

    k_sem_take(&sem, K_FOREVER);
    atomic_inc(&counter);
    k_sem_give(&sem);
    return true;
}

/* Handler for low priority thread */
static bool thread_low_handler(void *user_data, uint32_t cnt, bool last_iter)
{
    ARG_UNUSED(user_data);
    ARG_UNUSED(cnt);
    ARG_UNUSED(last_iter);

    k_sem_take(&sem, K_FOREVER);
    atomic_inc(&counter);
    k_sem_give(&sem);
    return true;
}

ZTEST_SUITE(stress_tests, NULL, NULL, NULL, NULL, NULL);

ZTEST(stress_tests, test_concurrent_access)
{
    atomic_set(&counter, 0);

    /* Set 10 second timeout */
    ztress_set_timeout(K_MSEC(10000));

    /* Run stress test with:
     * - Timer context: 10000 iterations, 20 tick initial delay
     * - High priority thread: 10000 iterations, no preemption requirement
     * - Low priority thread: 10000 iterations, expect 100 preemptions
     */
    ZTRESS_EXECUTE(
        ZTRESS_TIMER(timer_handler, NULL, 10000, Z_TIMEOUT_TICKS(20)),
        ZTRESS_THREAD(thread_high_handler, NULL, 10000, 0, Z_TIMEOUT_TICKS(20)),
        ZTRESS_THREAD(thread_low_handler, NULL, 10000, 100, Z_TIMEOUT_TICKS(20))
    );

    /* Verify all iterations completed */
    zassert_true(atomic_get(&counter) >= 30000,
                 "expected at least 30000 iterations");
}
```

---

### Pytest Integration Test

#### File Structure

```
tests/integration/shell_test/
├── CMakeLists.txt
├── prj.conf
├── testcase.yaml
├── src/
│   └── main.c
└── pytest/
    └── test_shell.py
```

#### testcase.yaml

```yaml
tests:
  integration.shell.pytest:
    harness: pytest
    harness_config:
      pytest_root:
        - pytest/test_shell.py
      pytest_dut_scope: session
    platform_allow:
      - native_sim
```

#### prj.conf

```
CONFIG_ZTEST=y
CONFIG_SHELL=y
CONFIG_SHELL_BACKEND_SERIAL=y
```

#### src/main.c

```c
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

static int cmd_hello(const struct shell *sh, size_t argc, char **argv)
{
    shell_print(sh, "Hello from Zephyr!");
    return 0;
}

static int cmd_add(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 3) {
        shell_error(sh, "Usage: add <a> <b>");
        return -EINVAL;
    }

    int a = atoi(argv[1]);
    int b = atoi(argv[2]);

    shell_print(sh, "Result: %d", a + b);
    return 0;
}

SHELL_CMD_REGISTER(hello, NULL, "Say hello", cmd_hello);
SHELL_CMD_REGISTER(add, NULL, "Add two numbers", cmd_add);

int main(void)
{
    return 0;
}
```

#### pytest/test_shell.py

```python
import pytest
import re


def test_hello_command(shell):
    """Test the hello command."""
    lines = shell.exec_command("hello")
    assert "Hello from Zephyr!" in lines, f"Unexpected output: {lines}"


def test_add_command(shell):
    """Test the add command."""
    lines = shell.exec_command("add 2 3")
    assert "Result: 5" in lines, f"Unexpected output: {lines}"


def test_add_large_numbers(shell):
    """Test add with larger numbers."""
    lines = shell.exec_command("add 100 200")
    assert "Result: 300" in lines, f"Unexpected output: {lines}"


def test_add_negative(shell):
    """Test add with negative numbers."""
    lines = shell.exec_command("add -5 10")
    assert "Result: 5" in lines, f"Unexpected output: {lines}"


def test_add_missing_args(shell):
    """Test add with missing arguments."""
    lines = shell.exec_command("add 5")
    assert "Usage:" in lines or "error" in lines.lower(), f"Expected error message, got: {lines}"
```

#### Run

```bash
./scripts/twister -T tests/integration/shell_test/ -p native_sim
```

---

### Notes

1. **Integration tests** use full Zephyr and can run on actual boards or emulators
2. **Unit tests** compile only the module under test (faster iteration)
3. **Fixtures** are best for shared setup that needs cleanup
4. **FFF mocks** are essential for isolating code from dependencies
5. **Test rules** apply to all tests in the binary
6. **Pytest** is powerful for complex integration scenarios requiring host logic

## Mocking

Complete reference for using FFF (Fake Function Framework) for mocking in Zephyr tests.

### Table of Contents

1. [Setup](#setup)
2. [Basic Fakes](#basic-fakes)
3. [Checking Calls](#checking-calls)
4. [Return Values](#return-values)
5. [Custom Fake Implementations](#custom-fake-implementations)
6. [Resetting Fakes](#resetting-fakes)
7. [FFF Fakes List Pattern](#fff-fakes-list-pattern)
8. [Test Rules for Mocks](#test-rules-for-mocks)
9. [Common Patterns](#common-patterns)

---

### Setup

#### Include Header

```c
#include <zephyr/fff.h>
```

#### Define FFF Globals

**Required once per test binary** (typically in main test file):

```c
DEFINE_FFF_GLOBALS;
```

---

### Basic Fakes

#### Void Functions

```c
// For: void my_func(int arg1, char *arg2);
DEFINE_FAKE_VOID_FUNC(my_func, int, char *);
```

#### Value-Returning Functions

```c
// For: int my_func(int arg1, char *arg2);
DEFINE_FAKE_VALUE_FUNC(int, my_func, int, char *);
```

#### Variadic Functions

```c
// For: int printf_like(const char *fmt, ...);
DEFINE_FAKE_VALUE_FUNC_VARARG(int, printf_like, const char *, ...);
```

#### No-Argument Functions

```c
// For: void init(void);
DEFINE_FAKE_VOID_FUNC(init);

// For: int get_value(void);
DEFINE_FAKE_VALUE_FUNC(int, get_value);
```

---

### Checking Calls

#### Call Count

```c
ZTEST(my_suite, test_call_count)
{
    my_func(1, "hello");
    my_func(2, "world");

    zassert_equal(my_func_fake.call_count, 2, "expected 2 calls");
}
```

#### Argument History

```c
ZTEST(my_suite, test_arguments)
{
    my_func(42, "test");

    zassert_equal(my_func_fake.arg0_val, 42, "wrong first arg");
    zassert_str_equal(my_func_fake.arg1_val, "test", "wrong second arg");
}
```

#### Argument History (Multiple Calls)

```c
ZTEST(my_suite, test_arg_history)
{
    my_func(1, "a");
    my_func(2, "b");
    my_func(3, "c");

    // Access history (default keeps last 50 calls)
    zassert_equal(my_func_fake.arg0_history[0], 1, "first call arg0");
    zassert_equal(my_func_fake.arg0_history[1], 2, "second call arg0");
    zassert_equal(my_func_fake.arg0_history[2], 3, "third call arg0");
}
```

---

### Return Values

#### Static Return Value

```c
ZTEST(my_suite, test_return_value)
{
    get_value_fake.return_val = 42;

    int result = get_value();
    zassert_equal(result, 42, "wrong return");
}
```

#### Sequence of Return Values

```c
ZTEST(my_suite, test_return_sequence)
{
    int returns[] = {1, 2, 3, -1};
    SET_RETURN_SEQ(my_func, returns, 4);

    zassert_equal(my_func(0, NULL), 1, "first call");
    zassert_equal(my_func(0, NULL), 2, "second call");
    zassert_equal(my_func(0, NULL), 3, "third call");
    zassert_equal(my_func(0, NULL), -1, "fourth call");
    // Subsequent calls return last value
    zassert_equal(my_func(0, NULL), -1, "fifth call");
}
```

---

### Custom Fake Implementations

#### Simple Custom Fake

```c
// Custom implementation
int my_func_custom(int arg1, char *arg2)
{
    if (arg1 < 0) {
        return -EINVAL;
    }
    return strlen(arg2);
}

ZTEST(my_suite, test_custom_fake)
{
    my_func_fake.custom_fake = my_func_custom;

    zassert_equal(my_func(-1, "test"), -EINVAL, "should fail");
    zassert_equal(my_func(1, "hello"), 5, "should return length");
}
```

#### Custom Fake with State

```c
static int call_counter = 0;
static int stored_values[10];

int my_func_with_state(int value, char *unused)
{
    stored_values[call_counter++] = value;
    return 0;
}

ZTEST(my_suite, test_stateful_fake)
{
    call_counter = 0;
    my_func_fake.custom_fake = my_func_with_state;

    my_func(10, NULL);
    my_func(20, NULL);

    zassert_equal(stored_values[0], 10, "first value");
    zassert_equal(stored_values[1], 20, "second value");
}
```

#### Custom Fake Sequence

```c
int fake_first_call(int arg) { return 1; }
int fake_second_call(int arg) { return 2; }
int fake_remaining(int arg) { return 99; }

ZTEST(my_suite, test_custom_sequence)
{
    int (*fakes[])(int) = {fake_first_call, fake_second_call, fake_remaining};
    SET_CUSTOM_FAKE_SEQ(my_func, fakes, 3);

    zassert_equal(my_func(0), 1, "first");
    zassert_equal(my_func(0), 2, "second");
    zassert_equal(my_func(0), 99, "third");
    zassert_equal(my_func(0), 99, "fourth (stays on last)");
}
```

---

### Resetting Fakes

#### Reset Single Fake

```c
RESET_FAKE(my_func);
```

This resets:
- `call_count` to 0
- All argument history
- `return_val` to 0
- `custom_fake` to NULL

#### Reset in Before Function

```c
static void my_suite_before(void *f)
{
    RESET_FAKE(func1);
    RESET_FAKE(func2);
    RESET_FAKE(func3);
}
```

---

### FFF Fakes List Pattern

For many fakes, use the list macro pattern:

#### Define List

```c
// In header or top of file
#define FFF_FAKES_LIST(FAKE) \
    FAKE(func1)              \
    FAKE(func2)              \
    FAKE(func3)              \
    FAKE(func4)
```

#### Reset All with List

```c
static void reset_all_fakes(void)
{
    FFF_FAKES_LIST(RESET_FAKE);
}
```

#### Use in Test Rule

```c
#define FFF_FAKES_LIST(FAKE) \
    FAKE(sensor_read)        \
    FAKE(sensor_write)       \
    FAKE(sensor_init)

static void fff_reset_before(const struct ztest_unit_test *test, void *f)
{
    ARG_UNUSED(test);
    ARG_UNUSED(f);
    FFF_FAKES_LIST(RESET_FAKE);
}

ZTEST_RULE(fff_reset, fff_reset_before, NULL);
```

#### Multiple Lists for Organization

```c
// Networking fakes
#define NET_FAKES_LIST(FAKE) \
    FAKE(net_send)           \
    FAKE(net_recv)

// Storage fakes
#define STORAGE_FAKES_LIST(FAKE) \
    FAKE(flash_read)             \
    FAKE(flash_write)

// Reset all
#define DO_FOREACH_FAKE(FAKE) \
    NET_FAKES_LIST(FAKE)      \
    STORAGE_FAKES_LIST(FAKE)

static void reset_all(void)
{
    DO_FOREACH_FAKE(RESET_FAKE);
}
```

---

### Test Rules for Mocks

#### Global Mock Reset Rule

```c
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

DEFINE_FFF_GLOBALS;

// Define fakes
DEFINE_FAKE_VALUE_FUNC(int, hw_init);
DEFINE_FAKE_VOID_FUNC(hw_deinit);
DEFINE_FAKE_VALUE_FUNC(int, hw_read, uint8_t *, size_t);

#define FFF_FAKES_LIST(FAKE) \
    FAKE(hw_init)            \
    FAKE(hw_deinit)          \
    FAKE(hw_read)

static void fff_reset_rule_before(const struct ztest_unit_test *test, void *fixture)
{
    ARG_UNUSED(test);
    ARG_UNUSED(fixture);
    FFF_FAKES_LIST(RESET_FAKE);
}

ZTEST_RULE(fff_reset_rule, fff_reset_rule_before, NULL);
```

---

### Common Patterns

#### Mocking a Driver API

```c
// Mock sensor driver
DEFINE_FAKE_VALUE_FUNC(int, sensor_sample_fetch, const struct device *);
DEFINE_FAKE_VALUE_FUNC(int, sensor_channel_get, const struct device *,
                       enum sensor_channel, struct sensor_value *);

ZTEST(my_suite, test_sensor_read)
{
    struct sensor_value expected = {.val1 = 25, .val2 = 500000};

    sensor_sample_fetch_fake.return_val = 0;

    // Custom fake to set output parameter
    sensor_channel_get_fake.custom_fake =
        [](const struct device *dev, enum sensor_channel chan,
           struct sensor_value *val) {
            val->val1 = 25;
            val->val2 = 500000;
            return 0;
        };

    // Call code under test
    int result = my_sensor_read_function(dev);

    zassert_equal(result, 0, "read failed");
    zassert_equal(sensor_sample_fetch_fake.call_count, 1, "fetch not called");
}
```

#### Mocking with Error Injection

```c
ZTEST(my_suite, test_error_handling)
{
    // First call succeeds, second fails
    int returns[] = {0, -EIO};
    SET_RETURN_SEQ(device_read, returns, 2);

    // First read should succeed
    zassert_ok(do_operation(), "first op should pass");

    // Second read should fail and be handled
    zassert_equal(do_operation(), -EIO, "should propagate error");
}
```

#### Verifying Call Order

```c
DEFINE_FAKE_VOID_FUNC(step1);
DEFINE_FAKE_VOID_FUNC(step2);
DEFINE_FAKE_VOID_FUNC(step3);

ZTEST(my_suite, test_initialization_order)
{
    initialize_system();

    // Verify call order using global call counter
    zassert_true(step1_fake.call_count > 0, "step1 not called");
    zassert_true(step2_fake.call_count > 0, "step2 not called");
    zassert_true(step3_fake.call_count > 0, "step3 not called");

    // Use FFF's call order tracking
    // Each fake has .caller_history if FFF_CALL_HISTORY_LEN > 0
}
```

#### Mocking Callbacks

```c
typedef void (*callback_t)(int result, void *user_data);

// The function that accepts a callback
DEFINE_FAKE_VOID_FUNC(async_operation, callback_t, void *);

ZTEST(my_suite, test_callback_invoked)
{
    static int callback_result = -1;
    static void *callback_data = NULL;

    // Custom fake that captures and invokes callback
    async_operation_fake.custom_fake =
        [](callback_t cb, void *data) {
            // Simulate async completion
            cb(42, data);
        };

    void *my_data = (void *)0x1234;

    // Callback implementation for test
    callback_t my_callback = [](int result, void *data) {
        callback_result = result;
        callback_data = data;
    };

    async_operation(my_callback, my_data);

    zassert_equal(callback_result, 42, "callback not invoked correctly");
    zassert_equal(callback_data, my_data, "wrong user data");
}
```

---

### Zephyr Fake Drivers

Zephyr provides pre-built fake drivers:

| Fake | Devicetree Compatible |
|------|----------------------|
| Fake CAN | `zephyr,fake-can` |
| Fake EEPROM | `zephyr,fake-eeprom` |

Enable via devicetree overlay:
```dts
/ {
    fake_eeprom: eeprom@0 {
        compatible = "zephyr,fake-eeprom";
        size = <1024>;
    };
};
```

---

### FFF Extensions (Zephyr)

Zephyr provides simplified macro wrappers. See `<zephyr/fff.h>` for:

- `ZTEST_FFF_FAKE` - Simplified fake declaration
- Additional helper macros for common patterns

---

### Best Practices

1. **Always reset fakes** in before function or test rule
2. **Use FFF_FAKES_LIST** macro for maintainability
3. **Prefer custom_fake** for complex behavior over return sequences
4. **Check call_count** to verify function was called
5. **Verify arguments** using arg_val or arg_history
6. **Organize fakes** by subsystem with separate lists
7. **Document fake behavior** in test comments

## Twister

Complete reference for Twister test runner configuration and usage.

### Table of Contents

1. [Running Tests](#running-tests)
2. [testcase.yaml Configuration](#testcaseyaml-configuration)
3. [Harnesses](#harnesses)
4. [Hardware Testing](#hardware-testing)
5. [Filter Expressions](#filter-expressions)
6. [Output Files](#output-files)

---

### Running Tests

#### Basic Commands

```bash
# Run all tests in a directory
./scripts/twister -T tests/kernel/

# Run on specific platform
./scripts/twister -p native_sim -T tests/kernel/threads/

# Run specific test scenario
./scripts/twister --scenario tests/kernel/semaphore/kernel.semaphore

# List available tests
./scripts/twister --list-tests -T tests/kernel/

# Build only (no execution)
./scripts/twister --build-only -T tests/subsys/

# Run all tests on all platforms
./scripts/twister --all --enable-slow

# Integration mode (uses integration_platforms)
./scripts/twister --integration -T tests/
```

#### Verbose Output

```bash
# Verbose output (shows test method: qemu, native_sim, etc.)
./scripts/twister -v -T tests/kernel/

# Very verbose
./scripts/twister -vv -T tests/kernel/
```

#### Filtering Options

```bash
# By tag
./scripts/twister -t kernel -T tests/

# Exclude tag
./scripts/twister -e slow -T tests/

# By architecture
./scripts/twister -a arm -T tests/

# Multiple platforms
./scripts/twister -p native_sim -p qemu_x86 -T tests/
```

---

### testcase.yaml Configuration

#### Complete Key Reference

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `tags` | list | required | Categorization tags |
| `platform_allow` | list | all | Only run on these platforms |
| `platform_exclude` | list | none | Never run on these platforms |
| `arch_allow` | list | all | Only run on these architectures |
| `arch_exclude` | list | none | Never run on these architectures |
| `depends_on` | list | none | Required board features |
| `build_only` | bool | false | Build but don't run |
| `build_on_all` | bool | false | Build on all platforms (CI) |
| `timeout` | int | 60 | Seconds before kill |
| `slow` | bool | false | Only with --enable-slow |
| `skip` | bool | false | Skip unconditionally |
| `min_ram` | int | 128 | Minimum RAM in KB |
| `min_flash` | int | 512 | Minimum flash in KB |
| `extra_args` | list | none | Extra build arguments |
| `extra_configs` | list | none | Kconfig overlays |
| `filter` | string | none | Expression filter |
| `harness` | string | ztest | Test harness type |
| `harness_config` | dict | none | Harness-specific options |
| `type` | string | integration | "unit" for unit tests |
| `integration_platforms` | list | none | Platforms for --integration |
| `sysbuild` | bool | false | Use sysbuild |
| `required_snippets` | list | none | Required snippets |
| `levels` | list | none | Test levels |

#### Common Patterns

**Basic Integration Test:**
```yaml
tests:
  mysubsys.feature:
    tags: feature subsys
    platform_allow:
      - native_sim
    integration_platforms:
      - native_sim
```

**Unit Test:**
```yaml
tests:
  mymodule.unit.basic:
    tags: unit
    type: unit
```

**Platform-specific Config:**
```yaml
tests:
  driver.test:
    depends_on: gpio spi
    extra_configs:
      - arch:arm:CONFIG_ARM_MPU=y
      - platform:nrf52840dk/nrf52840:CONFIG_DEBUG=y
```

**Build-only Test:**
```yaml
tests:
  driver.build_check:
    build_only: true
    platform_allow:
      - native_sim
      - qemu_x86
```

**Slow/Stress Test:**
```yaml
tests:
  kernel.stress:
    slow: true
    timeout: 300
```

#### Using common Section

```yaml
common:
  tags: kernel
  platform_allow:
    - native_sim
    - qemu_x86

tests:
  kernel.semaphore.basic:
    # inherits common settings

  kernel.semaphore.stress:
    slow: true
    timeout: 120
```

---

### Harnesses

#### Available Harnesses

| Harness | Purpose |
|---------|---------|
| `ztest` | Ztest framework output parsing (default) |
| `console` | Regex matching on console output |
| `pytest` | Python pytest integration |
| `gtest` | Google Test framework |
| `robot` | Robot Framework (Renode) |
| `shell` | Shell command execution |
| `power` | Power consumption measurement |
| `display_capture` | Display verification via camera |

#### Ztest Harness

Default for Ztest-based tests. No special configuration needed.

```yaml
tests:
  my.test:
    harness: ztest
    harness_config:
      ztest_suite_repeat: 3
      ztest_test_repeat: 2
      ztest_test_shuffle: true
```

#### Console Harness

Match regex patterns in output:

```yaml
tests:
  sample.output_check:
    harness: console
    harness_config:
      type: multi_line
      ordered: false
      regex:
        - "Initialization complete"
        - "Result: [0-9]+"
```

| Option | Type | Description |
|--------|------|-------------|
| `type` | `one_line`/`multi_line` | Matching mode |
| `regex` | list | Regular expressions to match |
| `ordered` | bool | Must match in order |
| `fixture` | string | Hardware fixture required |

#### Pytest Harness

Run pytest tests against DUT:

```yaml
tests:
  integration.pytest:
    harness: pytest
    harness_config:
      pytest_root:
        - pytest/test_shell.py
        - pytest/test_api.py::test_specific
      pytest_args:
        - "-v"
        - "--log-level=DEBUG"
      pytest_dut_scope: session
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `pytest_root` | list | `pytest/` | Test files/directories |
| `pytest_args` | list | none | Extra pytest arguments |
| `pytest_dut_scope` | string | function | DUT fixture scope |

**pytest_dut_scope options:**
- `function` - DUT launched for each test
- `class` - DUT shared within test class
- `module` - DUT shared within module
- `session` - DUT launched once

#### Shell Harness

Execute shell commands and verify output:

```yaml
tests:
  shell.test:
    harness: shell
    harness_config:
      shell_commands:
        - "help"
        - "version"
      regex:
        - "Available commands"
```

---

### Hardware Testing

#### Single Device

```bash
./scripts/twister --device-testing \
    --device-serial /dev/ttyACM0 \
    -p nrf52840dk/nrf52840 \
    -T tests/kernel/
```

#### Hardware Map

Generate and use hardware map for multiple devices:

```bash
# Generate hardware map
./scripts/twister --generate-hardware-map map.yml

# Edit map.yml to set correct platform names
# Then run:
./scripts/twister --device-testing \
    --hardware-map map.yml \
    -T tests/
```

**Example map.yml:**
```yaml
- id: 0
  serial: /dev/ttyACM0
  platform: nrf52840dk/nrf52840
  product: SEGGER J-Link
  runner: jlink
  available: true

- id: 1
  serial: /dev/ttyACM1
  platform: frdm_k64f
  product: DAPLink CMSIS-DAP
  runner: pyocd
  available: true
```

#### Fixtures

For tests requiring external hardware:

```yaml
tests:
  sensor.i2c_test:
    harness: ztest
    harness_config:
      fixture: i2c_bme280
```

Mark devices with fixtures in hardware map:
```yaml
- id: 0
  serial: /dev/ttyACM0
  platform: nrf52840dk/nrf52840
  fixtures:
    - i2c_bme280
    - gpio_loopback
```

---

### Filter Expressions

#### Syntax

```yaml
filter: <expression>
```

**Operators:** `and`, `or`, `not`, `==`, `!=`, `<`, `>`, `<=`, `>=`, `in`, `:`

#### Kconfig Filters

```yaml
# Check if config is enabled
filter: CONFIG_BT

# Check config value
filter: CONFIG_LOG_DEFAULT_LEVEL >= 3

# Check config string
filter: CONFIG_SOC : "stm32.*"
```

#### Architecture/Platform Filters

```yaml
# Same as arch_exclude
filter: not ARCH in ["x86", "arc"]

# Same as platform_allow
filter: PLATFORM in ["native_sim", "qemu_x86"]
```

#### Devicetree Filters

```yaml
# Check compatible
filter: dt_compat_enabled("zephyr,gpio-keys")

# Check alias exists
filter: dt_alias_exists("led0")

# Check chosen node
filter: dt_chosen_enabled("zephyr,console")

# Check node label
filter: dt_nodelabel_enabled("i2c0")
```

**Available DT functions:**
- `dt_compat_enabled(compat)` - Enabled node with compatible
- `dt_alias_exists(alias)` - Alias exists and enabled
- `dt_chosen_enabled(chosen)` - Chosen node enabled
- `dt_nodelabel_enabled(label)` - Node label exists
- `dt_nodelabel_prop_enabled(label, prop)` - Node has property
- `dt_node_has_prop(node_id, prop)` - Node has property

#### Environment Variables

```yaml
filter: *MY_ENV_VAR == "enabled"
```

---

### Output Files

#### Location

Default: `twister-out/` directory

#### Key Files

| File | Content |
|------|---------|
| `twister.json` | Complete results in JSON |
| `twister.log` | Build and execution logs |
| `twister_discard.csv` | Skipped tests with reasons |
| `twister_report.xml` | JUnit-style report |
| `testplan.json` | Planned test execution |

#### twister.json Structure

```json
{
  "environment": {
    "zephyr_version": "3.5.0",
    "toolchain": "zephyr"
  },
  "testsuites": [
    {
      "name": "tests/kernel/semaphore/kernel.semaphore",
      "platform": "native_sim",
      "status": "passed",
      "execution_time": 1.23,
      "testcases": [
        {
          "name": "semaphore_tests.test_sem_give_take",
          "status": "passed",
          "execution_time": 0.05
        }
      ]
    }
  ]
}
```

#### Cleanup Options

```bash
# Keep build directories for failed tests only
./scripts/twister --runtime-artifact-cleanup

# Force clean builds
./scripts/twister --clean

# Specify output directory
./scripts/twister -O my_output_dir -T tests/
```

---

### Quarantine

Skip flaky tests using quarantine:

```bash
./scripts/twister --quarantine-list quarantine.yaml -T tests/
```

**quarantine.yaml:**
```yaml
- scenarios:
    - kernel.semaphore.stress
  platforms:
    - qemu_x86
  comment: "Flaky under QEMU - issue #12345"
```

---

### Test Levels

Define and filter by test levels:

```yaml
tests:
  kernel.basic:
    levels:
      - smoke
      - daily

  kernel.stress:
    levels:
      - weekly
      - full
```

```bash
# Run only smoke tests
./scripts/twister --level smoke -T tests/

# Run daily tests
./scripts/twister --level daily -T tests/
```

## Ztest

Complete API reference for the Zephyr Test Framework (Ztest).

### Table of Contents

1. [Suite Definition](#suite-definition)
2. [Test Macros](#test-macros)
3. [Assertion Macros](#assertion-macros)
4. [Expectation Macros](#expectation-macros)
5. [Assumption Macros](#assumption-macros)
6. [Fixtures](#fixtures)
7. [Test Rules](#test-rules)
8. [Test Result Expectations](#test-result-expectations)
9. [Skipping Tests](#skipping-tests)
10. [Parameterized Tests](#parameterized-tests)
11. [Userspace Tests](#userspace-tests)
12. [Custom test_main](#custom-test_main)
13. [Test Shuffling and Repeating](#test-shuffling-and-repeating)

---

### Suite Definition

#### ZTEST_SUITE Macro

```c
ZTEST_SUITE(suite_name, predicate, setup, before, after, teardown);
```

| Parameter | Type | Purpose |
|-----------|------|---------|
| `suite_name` | identifier | Unique name within the binary |
| `predicate` | `bool (*)(const void *)` | Returns true if suite should run (NULL = always run) |
| `setup` | `void *(*)(void)` | Returns fixture pointer, runs once at suite start |
| `before` | `void (*)(void *)` | Runs before each test, receives fixture |
| `after` | `void (*)(void *)` | Runs after each test, receives fixture |
| `teardown` | `void (*)(void *)` | Runs once at suite end, receives fixture |

#### Minimal Suite

```c
ZTEST_SUITE(my_suite, NULL, NULL, NULL, NULL, NULL);
```

#### Suite with Predicate

```c
static bool only_when_ready(const void *state)
{
    return ((const struct test_state *)state)->ready;
}

ZTEST_SUITE(conditional_suite, only_when_ready, NULL, NULL, NULL, NULL);
```

---

### Test Macros

| Macro | Purpose | Fixture Access |
|-------|---------|----------------|
| `ZTEST(suite, test)` | Standard test | No |
| `ZTEST_F(suite, test)` | Test with fixture | Yes, via `fixture` variable |
| `ZTEST_USER(suite, test)` | Runs in userspace thread | No |
| `ZTEST_USER_F(suite, test)` | Userspace + fixture | Yes |
| `ZTEST_P(suite, test)` | Parameterized test | Via `data` pointer |

#### Standard Test

```c
ZTEST(my_suite, test_addition)
{
    zassert_equal(2 + 2, 4, "math failed");
}
```

#### Test with Fixture

```c
ZTEST_F(my_suite, test_with_state)
{
    zassert_equal(fixture->count, 0, "count not reset");
    fixture->count++;
}
```

---

### Assertion Macros

Assertions **fail immediately** and abort the current test.

#### Boolean Assertions

| Macro | Checks |
|-------|--------|
| `zassert_true(cond, msg, ...)` | cond is true |
| `zassert_false(cond, msg, ...)` | cond is false |
| `zassert_ok(ret, msg, ...)` | ret == 0 |

#### Equality Assertions

| Macro | Checks |
|-------|--------|
| `zassert_equal(a, b, msg, ...)` | a == b (integers/pointers) |
| `zassert_not_equal(a, b, msg, ...)` | a != b |
| `zassert_equal_ptr(a, b, msg, ...)` | pointer equality |

#### Pointer Assertions

| Macro | Checks |
|-------|--------|
| `zassert_is_null(ptr, msg, ...)` | ptr == NULL |
| `zassert_not_null(ptr, msg, ...)` | ptr != NULL |

#### Memory/String Assertions

| Macro | Checks |
|-------|--------|
| `zassert_mem_equal(a, b, size, msg, ...)` | memcmp(a, b, size) == 0 |
| `zassert_str_equal(a, b, msg, ...)` | strcmp(a, b) == 0 |

#### Numeric Range Assertions

| Macro | Checks |
|-------|--------|
| `zassert_within(a, b, delta, msg, ...)` | \|a - b\| <= delta |
| `zassert_between_inclusive(a, lo, hi, msg, ...)` | lo <= a <= hi |

#### Unreachable Code

```c
zassert_unreachable("should not reach here");
```

#### Example Output

```
Assertion failed at main.c:42: test_example: expected 4 but got 5 (a not equal to b)
Aborted at unit test function
```

---

### Expectation Macros

Expectations **continue execution** but mark the test as failed at the end.

Replace `zassert_` prefix with `zexpect_`:

```c
ZTEST(my_suite, test_multiple_checks)
{
    zexpect_equal(result->a, 1, "a wrong");
    zexpect_equal(result->b, 2, "b wrong");  // continues even if above fails
    zexpect_equal(result->c, 3, "c wrong");
}
// Test fails at end if any expectation failed
```

All assertion variants have expectation equivalents:
- `zexpect_true`, `zexpect_false`, `zexpect_ok`
- `zexpect_equal`, `zexpect_not_equal`
- `zexpect_is_null`, `zexpect_not_null`
- `zexpect_mem_equal`, `zexpect_str_equal`
- `zexpect_within`

---

### Assumption Macros

Assumptions **skip the test** if the condition fails.

Replace `zassert_` prefix with `zassume_`:

```c
ZTEST(my_suite, test_requires_feature)
{
    zassume_true(feature_available(), "feature not available");
    // Test skipped (not failed) if feature unavailable

    run_feature_test();
}
```

All assertion variants have assumption equivalents:
- `zassume_true`, `zassume_false`, `zassume_ok`
- `zassume_equal`, `zassume_not_equal`
- `zassume_not_null`

---

### Fixtures

#### Defining a Fixture

The fixture struct must be named `<suite_name>_fixture`:

```c
struct my_suite_fixture {
    int count;
    uint8_t buffer[256];
    void *resource;
};
```

#### Fixture Lifecycle Functions

```c
// Called once at suite start, returns fixture pointer
static void *my_suite_setup(void)
{
    struct my_suite_fixture *f = malloc(sizeof(*f));
    zassume_not_null(f, "malloc failed");
    f->resource = acquire_resource();
    return f;
}

// Called before each test
static void my_suite_before(void *f)
{
    struct my_suite_fixture *fixture = f;
    fixture->count = 0;
    memset(fixture->buffer, 0, sizeof(fixture->buffer));
}

// Called after each test
static void my_suite_after(void *f)
{
    struct my_suite_fixture *fixture = f;
    // cleanup per-test state
}

// Called once at suite end
static void my_suite_teardown(void *f)
{
    struct my_suite_fixture *fixture = f;
    release_resource(fixture->resource);
    free(f);
}

ZTEST_SUITE(my_suite, NULL, my_suite_setup, my_suite_before,
            my_suite_after, my_suite_teardown);
```

#### Using Fixtures in Tests

```c
ZTEST_F(my_suite, test_uses_fixture)
{
    // 'fixture' is automatically available, typed as struct my_suite_fixture *
    zassert_equal(fixture->count, 0, "before() should reset count");
    fixture->buffer[0] = 0xAB;
}
```

#### Userspace Fixture Memory

For `ZTEST_USER_F`, fixture memory must be userspace-accessible:

```c
static ZTEST_DMEM struct shared_data userspace_data;
static ZTEST_BMEM uint8_t userspace_buffer[64];
```

---

### Test Rules

Rules run before/after **every test in the binary**, regardless of suite.

#### Defining a Rule

```c
static void rule_before(const struct ztest_unit_test *test, void *fixture)
{
    ARG_UNUSED(test);
    ARG_UNUSED(fixture);
    // Reset global state, mocks, etc.
}

static void rule_after(const struct ztest_unit_test *test, void *fixture)
{
    ARG_UNUSED(test);
    ARG_UNUSED(fixture);
    // Verify invariants, cleanup
}

ZTEST_RULE(my_rule, rule_before, rule_after);
```

#### Common Use: Reset FFF Mocks

```c
#include <zephyr/fff.h>

DEFINE_FFF_GLOBALS;
DEFINE_FAKE_VOID_FUNC(my_function, int);

static void fff_reset_before(const struct ztest_unit_test *test, void *fixture)
{
    ARG_UNUSED(test);
    ARG_UNUSED(fixture);
    RESET_FAKE(my_function);
}

ZTEST_RULE(fff_reset, fff_reset_before, NULL);
```

---

### Test Result Expectations

Mark tests that are **expected to fail or skip**:

```c
// This test is expected to fail - will be marked PASS if it fails
ZTEST_EXPECT_FAIL(my_suite, test_known_broken);
ZTEST(my_suite, test_known_broken)
{
    zassert_true(false, "this fails as expected");
}

// This test is expected to skip - will be marked PASS if it skips
ZTEST_EXPECT_SKIP(my_suite, test_expected_skip);
ZTEST(my_suite, test_expected_skip)
{
    zassume_true(false, "this skips as expected");
}
```

---

### Skipping Tests

#### Runtime Skip

```c
ZTEST(my_suite, test_conditional)
{
    if (!hardware_present()) {
        ztest_test_skip();
        return;  // Must return after skip
    }
    // Test code...
}
```

#### Compile-time Skip

```c
ZTEST(my_suite, test_feature)
{
    Z_TEST_SKIP_IFDEF(CONFIG_FEATURE_DISABLED);
    Z_TEST_SKIP_IFNDEF(CONFIG_REQUIRED_FEATURE);
    // Test runs only if conditions met
}
```

#### Ifdef Pattern

```c
#ifdef CONFIG_MY_FEATURE
ZTEST(my_suite, test_feature)
{
    // Test implementation
}
#else
ZTEST(my_suite, test_feature)
{
    ztest_test_skip();
}
#endif
```

---

### Parameterized Tests

#### Defining Parameters

```c
struct test_params {
    int input;
    int expected;
};

static struct test_params params[] = {
    {.input = 0, .expected = 0},
    {.input = 1, .expected = 1},
    {.input = 2, .expected = 4},
};

ZTEST_P(my_suite, test_parameterized, params);
```

#### Using Parameters

```c
ZTEST_P(my_suite, test_parameterized)
{
    // 'data' points to current parameter
    int result = square(data->input);
    zassert_equal(result, data->expected,
                  "square(%d) = %d, expected %d",
                  data->input, result, data->expected);
}
```

---

### Userspace Tests

When `CONFIG_USERSPACE=y`, userspace tests run in unprivileged mode:

```c
// Runs in userspace thread
ZTEST_USER(my_suite, test_userspace)
{
    // Cannot access kernel memory
    // Tests user-facing API behavior
}

// Userspace with fixture
ZTEST_USER_F(my_suite, test_userspace_with_fixture)
{
    // fixture must use ZTEST_DMEM/ZTEST_BMEM for shared memory
}
```

---

### Custom test_main

Override the default test runner for complex scenarios:

```c
#include <zephyr/ztest.h>

void test_main(void)
{
    struct test_state state = {0};

    // Run suites that check for phase == 0
    state.phase = 0;
    ztest_run_all(&state, false, 1, 1);

    // Run suites that check for phase == 1
    state.phase = 1;
    ztest_run_all(&state, false, 1, 1);

    // Verify all suites ran
    ztest_verify_all_test_suites_ran();
}
```

**Note**: No Kconfig needed. `test_main()` is declared `__weak` in
`subsys/testsuite/ztest/src/ztest.c`, so simply defining your own
`void test_main(void)` overrides the default implementation. (There is no
`CONFIG_ZTEST_CUSTOM_TEST_MAIN`.)

---

### Test Shuffling and Repeating

#### Kconfig Options

```
# Randomize test order
CONFIG_ZTEST_SHUFFLE=y

# Repeat tests
CONFIG_ZTEST_REPEAT=y
CONFIG_ZTEST_SUITE_REPEAT_COUNT=3
CONFIG_ZTEST_TEST_REPEAT_COUNT=3
```

#### Native Simulator Selection

```bash
# List all tests
./zephyr.exe -list

# Run specific tests
./zephyr.exe -test="my_suite::test_one,other_suite::test_two"

# Run all tests in a suite
./zephyr.exe -test="my_suite::*"
```

---

### Best Practices

1. **Test naming**: Prefix with `test_`, be descriptive
2. **One assertion focus**: Each test should verify one behavior
3. **Use fixtures**: For shared setup/teardown logic
4. **Use rules**: For cross-suite concerns (mock reset, invariant checks)
5. **Use expectations**: When testing multiple related conditions
6. **Use assumptions**: For optional test prerequisites
7. **Document tests**: Use doxygen comments for test purposes
