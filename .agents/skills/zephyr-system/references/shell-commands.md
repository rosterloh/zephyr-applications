# Shell Commands

## Overview

Expert guidance on Zephyr's Shell subsystem for creating custom CLI commands, managing subcommands, and interacting with users via console interfaces.

### Table of Contents

1. [Core Concepts](#core-concepts)
2. [Common Workflows](#common-workflows)
3. [Command Structure](#command-structure)
4. [Configuration](#configuration)
5. [Troubleshooting](#troubleshooting)

---

### Core Concepts

#### Command Registration vs. Handler Implementation

| Step | What | Where |
|------|------|-------|
| **Define Handler** | Implement `shell_cmd_handler` function | Your C file |
| **Register Command** | Use `SHELL_CMD_REGISTER` or `SHELL_CMD_ARG_REGISTER` | Your C file (global scope) |
| **Enable Shell** | Set `CONFIG_SHELL=y` | prj.conf |

#### Handler Prototype

```c
typedef int (*shell_cmd_handler)(const struct shell *sh, size_t argc, char **argv);
```

- `sh`: Shell instance pointer (use for all output)
- `argc`: Argument count (includes command name)
- `argv`: Argument vector. `argv[0]` is the command name
- Return: `0` success, `-EINVAL` invalid args, `-ENOEXEC` execution failed

#### Key Rules

- **Use shell output functions**: Always use `shell_print()`, `shell_error()`, etc. Never use `printk()` in handlers
- **Return codes matter**: Return `0` on success, negative errno on failure
- **Argument counting**: `mandatory` count includes the command name itself

---

### Common Workflows

#### 1. Creating a Simple Root Command

```c
static int cmd_hello(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    shell_print(sh, "Hello, World!");
    return 0;
}

SHELL_CMD_REGISTER(hello, NULL, "Print hello message", cmd_hello);
```

#### 2. Creating Commands with Subcommands

- **Macro syntax and registration**: See [macros.md](#macros)
- **Complete examples**: See [examples.md](#examples)

**Quick Example:**
```c
static int cmd_demo_ping(const struct shell *sh, size_t argc, char **argv)
{
    shell_print(sh, "pong");
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_demo,
    SHELL_CMD(ping, NULL, "Ping command", cmd_demo_ping),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(demo, &sub_demo, "Demo commands", NULL);
```

#### 3. Commands with Argument Validation

Use `SHELL_CMD_ARG_REGISTER` to automatically validate argument counts.

```c
/* Command requires exactly 2 args: command + one parameter */
SHELL_CMD_ARG_REGISTER(greet, NULL, "Greet <name>", cmd_greet, 2, 0);
```

- **Argument parsing techniques**: See [advanced.md](#advanced)

#### 4. Dynamic Commands

Create commands whose subcommands are determined at runtime.

- **Dynamic command creation**: See [advanced.md](#dynamic-commands)

#### 5. Dictionary Commands

Map string subcommands to data values (perfect for enums, settings).

- **Dictionary command patterns**: See [advanced.md](#dictionary-commands)

---

### Command Structure

#### Hierarchy

```
Root Commands (Level 0)
├── Registered with SHELL_CMD_REGISTER or SHELL_CMD_ARG_REGISTER
├── Stored in dedicated memory section, alphabetically sorted
│
└── Subcommands (Level 1+)
    ├── Static: SHELL_STATIC_SUBCMD_SET_CREATE
    ├── Dynamic: SHELL_DYNAMIC_CMD_CREATE
    └── Dictionary: SHELL_SUBCMD_DICT_SET_CREATE
```

#### Macro Reference

| Macro | Purpose |
|-------|---------|
| `SHELL_CMD_REGISTER` | Register root command |
| `SHELL_CMD_ARG_REGISTER` | Register root command with arg validation |
| `SHELL_STATIC_SUBCMD_SET_CREATE` | Define static subcommand array |
| `SHELL_DYNAMIC_CMD_CREATE` | Define dynamic subcommand generator |
| `SHELL_SUBCMD_DICT_SET_CREATE` | Define dictionary subcommands |
| `SHELL_CMD` | Define a subcommand entry |
| `SHELL_CMD_ARG` | Define subcommand with arg validation |
| `SHELL_COND_CMD` | Conditional subcommand (Kconfig-based) |

- **Complete macro documentation**: See [macros.md](#macros)

---

### Configuration

#### Essential Kconfig Options

```kconfig
CONFIG_SHELL=y                    # Enable shell subsystem
CONFIG_SHELL_BACKEND_SERIAL=y     # UART backend (most common)
CONFIG_SHELL_LOG_BACKEND=y        # Shell as logging backend
```

#### Backend Selection

| Backend | Kconfig | Use Case |
|---------|---------|----------|
| UART | `CONFIG_SHELL_BACKEND_SERIAL` | Default, most common |
| USB CDC ACM | Snippet `cdc-acm-console` | USB serial |
| RTT | `CONFIG_SHELL_BACKEND_RTT` | Segger J-Link debugging |
| Telnet | `CONFIG_SHELL_BACKEND_TELNET` | Network access |
| BLE NUS | Snippet `nus-console` | Bluetooth LE |

- **Kconfig options reference**: See [kconfig.md](#kconfig)
- **Backend configuration guide**: See [backends.md](#backends)

---

### Troubleshooting

For common errors and debugging techniques:
- See [debugging.md](#debugging)

#### Quick Reference

| Issue | Likely Cause | Fix |
|-------|--------------|-----|
| Command not appearing | Not registered or shell disabled | Check `SHELL_CMD_REGISTER`, enable `CONFIG_SHELL` |
| Output not visible | Using `printk()` instead of `shell_print()` | Use shell output functions |
| "Wrong parameter count" | Mandatory arg count wrong | Adjust `mandatory` (includes command name) |
| Handler not called | Arg validation failing | Check mandatory/optional counts |
| Subcommands not showing | Missing `SHELL_SUBCMD_SET_END` | Add terminator to subcommand array |

---

### References

- [macros.md](#macros) — Command registration macros, subcommand creation
- [api.md](#api) — Output functions, helper functions, handler prototypes
- [advanced.md](#advanced) — Dynamic commands, dictionary commands, getopt, argument parsing
- [kconfig.md](#kconfig) — Shell Kconfig options reference
- [backends.md](#backends) — Backend configuration (UART, RTT, USB, Telnet, BLE)
- [examples.md](#examples) — Complete working examples
- [debugging.md](#debugging) — Error resolution and debugging techniques

## Advanced

This document covers advanced Zephyr shell features, including dynamic command generation, dictionary-based subcommands, modular registration, and custom backends.

### Dynamic Commands

Dynamic commands are determined at runtime. This is useful for commands that depend on the current state of the system, such as listing available devices, files, or active threads.

#### Purpose
Commands whose subcommands are not known at compile time or change during execution (e.g., listing connected BLE peers or mounted filesystems).

#### Callback Signature
The dynamic command system relies on a callback that retrieves command entries by index.

```c
void (*shell_dynamic_get)(size_t idx, struct shell_static_entry *entry);
```

#### Key Requirements
- **Populating `shell_static_entry`**: The callback should fill the `entry` pointer with the syntax, handler, subcommands, and help text for the given `idx`.
- **Alphabetical Sorting**: The entries returned by the callback MUST be sorted alphabetically to allow the shell to perform binary searches for completion and execution.
- **NULL Termination**: When `idx` is out of range, the callback must set `entry->syntax` to `NULL` to signify the end of the list.

#### Example: Device Enumeration
```c
#include <zephyr/shell/shell.h>
#include <zephyr/device.h>

static void device_name_get(size_t idx, struct shell_static_entry *entry)
{
    const struct device *dev = shell_device_lookup(idx, NULL);

    if (dev != NULL) {
        entry->syntax = dev->name;
        entry->handler = NULL;
        entry->subcmd = NULL;
        entry->help = "Device name";
    } else {
        entry->syntax = NULL;
    }
}

SHELL_DYNAMIC_CMD_CREATE(dsub_device_name, device_name_get);
SHELL_CMD_REGISTER(device, &dsub_device_name, "Device commands", NULL);
```

---

### Dictionary Commands

Dictionary commands map string syntax to specific data values (integers or pointers). They are ideal for "set" style commands where you want to provide a fixed list of named options.

#### Purpose
Simplifying handlers that would otherwise require multiple `strcmp` calls to map a string argument to a configuration value.

#### Macro
```c
SHELL_SUBCMD_DICT_SET_CREATE(name, handler, (syntax, value, help), ...);
```

#### Handler Prototype
Dictionary handlers receive an extra `void *data` parameter containing the value associated with the chosen syntax.

```c
int (*shell_dict_cmd_handler)(const struct shell *sh, size_t argc, char **argv, void *data);
```

#### Triplet Format
The macro takes triplets in the form `(syntax, value, help)`.
- `syntax`: The string the user types.
- `value`: The data (cast to `void *`) passed to the handler.
- `help`: Help string for this specific option.

#### Example: Gain Settings
```c
static int gain_handler(const struct shell *sh, size_t argc, char **argv, void *data)
{
    /* Data is passed as the value defined in the triplet */
    int value = (intptr_t)data;
    shell_print(sh, "Setting gain to: %d", value);
    return 0;
}

SHELL_SUBCMD_DICT_SET_CREATE(sub_gain, gain_handler,
    (low, 1, "Low gain (1x)"),
    (medium, 5, "Medium gain (5x)"),
    (high, 10, "High gain (10x)")
);

SHELL_CMD_REGISTER(gain, &sub_gain, "Configure gain", NULL);
```

---

### Distributed Subcommand Registration

Distributed registration allows subcommands to be defined in different source files, facilitating modularity without modifying a central command list.

#### Usage Pattern
1.  **Define the Set**: Create an extensible subcommand set in a header or common file.
    ```c
    SHELL_SUBCMD_SET_CREATE(extensible_subcmds, (parent_command));
    ```
2.  **Add Commands**: Use `SHELL_SUBCMD_ADD` or `SHELL_SUBCMD_COND_ADD` in any compilation unit.
    ```c
    /* In sensor_a.c */
    SHELL_SUBCMD_ADD(extensible_subcmds, sensor_a, NULL, "Sensor A help", handler_a, 1, 0);

    /* In sensor_b.c */
    SHELL_SUBCMD_COND_ADD(CONFIG_SENSOR_B, extensible_subcmds, sensor_b, NULL, "Sensor B help", handler_b, 1, 0);
    ```

#### Use Case
A "sensor" command where different drivers can register their own subcommands (`sensor read sensor_a`, `sensor read sensor_b`) only if the driver is enabled in Kconfig.

---

### Argument Parsing

The shell provides several mechanisms for handling and validating arguments.

#### Special Constants
- `SHELL_OPT_ARG_RAW`: If used as the number of optional arguments, all remaining text after the mandatory arguments is passed as a single string in `argv[mandatory]`. This is useful for `echo`-like commands.
- `SHELL_OPT_ARG_CHECK_SKIP`: Disables the shell's built-in argument count checking for that command, allowing the handler to perform custom validation.

#### Parsing Techniques
- **Standard Parsing**: Use `strtol`, `strtoul`, or `strtobool` for numeric and boolean parsing.
- **Parent Context**: To access the parent command name, use `argv[-1]`.
- **Getopt Support**: If `CONFIG_SHELL_GETOPT` is enabled, you can use `getopt` for complex flag parsing.

```c
static int cmd_complex(const struct shell *sh, size_t argc, char **argv)
{
    int c;
    /* argv[0] is the command name, so getopt starts at index 1 */
    while ((c = getopt(argc, argv, "ab:")) != -1) {
        switch (c) {
        case 'a': /* handle flag a */ break;
        case 'b': /* handle optarg */ break;
        }
    }
    return 0;
}
```

---

### Structured Help

Structured help ensures consistent formatting and allows the shell to programmatically query command usage.

#### Purpose
Provides a standardized way to define multi-line descriptions and usage strings that the shell can format appropriately for the terminal width.

#### Macro Syntax
```c
SHELL_HELP(description, usage)
```

#### Functions
- `shell_help_is_structured()`: Can be used within a handler to check if the help provided is in the structured format.

#### Example
```c
#define MY_HELP SHELL_HELP( \
    "This is a multi-line description of the command.\n" \
    "It explains what the command does in detail.", \
    "usage: <arg1> [arg2]" \
)

SHELL_CMD_REGISTER(mycmd, NULL, MY_HELP, cmd_handler);
```

---

### Shell Bypass Mode

Bypass mode allows a callback to take direct control of the shell's input stream, bypassing the command processor.

#### Purpose
Handling raw data transfers, implementing terminal emulators, or supporting special protocols like XMODEM/YMODEM where binary data would otherwise trigger shell control sequences.

#### API and Callback
```c
void shell_set_bypass(const struct shell *sh, shell_bypass_cb_t cb);

/* Callback signature */
void (*shell_bypass_cb_t)(const struct shell *sh, uint8_t *data, size_t len);
```

#### Use Case
A command like `transfer_file` that sets a bypass callback to receive raw bytes until a termination sequence is detected, then restores normal shell operation by calling `shell_set_bypass(sh, NULL)`.

---

### Conditional Commands

Commands can be included or excluded at compile-time based on Kconfig options or arbitrary expressions, reducing code size for unused features.

- `SHELL_COND_CMD` / `SHELL_COND_CMD_ARG`: Include command if a Kconfig option is `y`.
- `SHELL_EXPR_CMD` / `SHELL_EXPR_CMD_ARG`: Include command if a C expression evaluates to true.

```c
/* Only included if CONFIG_STATS is enabled */
SHELL_COND_CMD(CONFIG_STATS, stats, NULL, "Show statistics", cmd_stats);

/* Only included on 64-bit architectures */
SHELL_EXPR_CMD(sizeof(void *) == 8, arch64, NULL, "64-bit specific", cmd_64);
```

---

### Custom Shell Backends

You can define custom transport backends (e.g., Bluetooth, SPI, custom hardware) by implementing the `shell_transport_api`.

#### Required API Callbacks
A backend must provide a `struct shell_transport_api` with:
- `init` / `uninit`: Life-cycle management.
- `enable`: Activate the backend.
- `write`: Send data from shell to backend.
- `read`: Receive data from backend to shell.
- `update`: Optional periodic maintenance.

#### Implementation Macro
Use `SHELL_DEFINE` to create a shell instance tied to your custom transport.

```c
struct my_transport_ctx { ... };
const struct shell_transport_api my_api = { ... };

#define MY_SHELL_DEFINE(_name) \
    static struct my_transport_ctx _name##_ctx; \
    static struct shell_transport _name##_transport = { \
        .api = &my_api, \
        .ctx = &_name##_ctx \
    }; \
    SHELL_DEFINE(_name, "prompt> ", &_name##_transport, 10, 0, SHELL_FLAG_USE_COLORS)
```

## Api

The shell pointer (`const struct shell *sh`) passed to command handlers provides access to these API functions for interaction and control.

> **CRITICAL RULE:** NEVER use `printk()` or `printf()` inside shell handlers. These functions bypass the shell backend, can corrupt the input line, and may not go to the same transport (e.g., if using shell over BLE or Telnet). ALWAYS use `shell_print()` and its variants.

### Output Functions

#### shell_print
Prints a formatted message followed by a newline.
- **Parameters:**
  - `sh`: Pointer to the shell instance.
  - `fmt`: Format string (printf style).
  - `...`: Arguments for the format string.
- **Return Value:** None.

#### shell_info
Prints an informational message in green.
- **Parameters:**
  - `sh`: Pointer to the shell instance.
  - `fmt`: Format string.
- **Return Value:** None.

#### shell_warn
Prints a warning message in yellow.
- **Parameters:**
  - `sh`: Pointer to the shell instance.
  - `fmt`: Format string.
- **Return Value:** None.

#### shell_error
Prints an error message in red.
- **Parameters:**
  - `sh`: Pointer to the shell instance.
  - `fmt`: Format string.
- **Return Value:** None.

#### shell_fprintf
Prints a formatted message with a specific color.
- **Parameters:**
  - `sh`: Pointer to the shell instance.
  - `color`: Color code (see [Colors](#colors)).
  - `fmt`: Format string.
- **Return Value:** None.

#### shell_vfprintf
The `vprintf` variant of `shell_fprintf`.
- **Parameters:**
  - `sh`: Pointer to the shell instance.
  - `color`: Color code.
  - `fmt`: Format string.
  - `args`: Variable arguments list (`va_list`).
- **Return Value:** None.

#### shell_hexdump
Prints a hex dump of a data buffer.
- **Parameters:**
  - `sh`: Pointer to the shell instance.
  - `data`: Pointer to the data to dump.
  - `len`: Length of the data.
- **Return Value:** None.

#### shell_hexdump_line
Prints a single line of a hex dump.
- **Parameters:**
  - `sh`: Pointer to the shell instance.
  - `offset`: Offset to print at the start of the line.
  - `data`: Pointer to the data.
  - `len`: Length of the data to print in this line.
- **Return Value:** None.

### Colors
Used with `shell_fprintf` and `shell_vfprintf`.
- `SHELL_NORMAL`: Default text color.
- `SHELL_INFO`: Green (Information).
- `SHELL_OPTION`: Cyan (Options).
- `SHELL_WARNING`: Yellow (Warning).
- `SHELL_ERROR`: Red (Error).

### Command Execution & Help

#### shell_help
Prints the help message for the current command context.
- **Parameters:** `sh`: Shell instance pointer.
- **Return Value:** None.
- **Usage:** Typically used when `argc` is incorrect or a `-h` flag is detected.

#### shell_execute_cmd
Programmatically executes a shell command string.
- **Parameters:**
  - `sh`: Shell instance pointer (can be NULL to use default).
  - `cmd`: The command string to execute.
- **Return Value:** `0` on success, or error code.

### Device Functions
Helpers for shell commands that interact with system devices.

#### shell_device_get_binding
Get a device binding by name, but only if the device is ready.
- **Parameters:** `name`: Device name string.
- **Return Value:** Pointer to `struct device` or `NULL`.

#### shell_device_get_binding_all
Get a device binding by name, including devices that are not ready.
- **Parameters:** `name`: Device name string.
- **Return Value:** Pointer to `struct device` or `NULL`.

#### shell_device_lookup
Look up a device by index, filtered by a name prefix (ready devices only).
- **Parameters:**
  - `idx`: Device index.
  - `prefix`: Name prefix to filter by.
- **Return Value:** Pointer to `struct device` or `NULL`.

#### shell_device_lookup_all
Look up any device by index with a prefix filter.
- **Parameters:**
  - `idx`: Device index.
  - `prefix`: Name prefix to filter by.
- **Return Value:** Pointer to `struct device` or `NULL`.

#### shell_device_lookup_non_ready
Look up only non-ready devices by index with a prefix filter.
- **Parameters:**
  - `idx`: Device index.
  - `prefix`: Name prefix to filter by.
- **Return Value:** Pointer to `struct device` or `NULL`.

#### shell_device_filter
Look up a device by index using a custom filter function.
- **Parameters:**
  - `idx`: Device index.
  - `filter`: Function pointer to a filter `bool (*)(const struct device *dev)`.
- **Return Value:** Pointer to `struct device` or `NULL`.

### Shell Control

#### shell_prompt_change
Changes the prompt for the specified shell instance.
- **Parameters:**
  - `sh`: Shell instance.
  - `prompt`: New prompt string.
- **Return Value:** None.

#### shell_set_bypass
Sets a bypass callback to handle raw input data directly.
- **Parameters:**
  - `sh`: Shell instance.
  - `bypass`: Function of type `shell_bypass_cb_t`.
- **Return Value:** None.

#### shell_ready
Checks if the shell is ready to accept commands.
- **Parameters:** `sh`: Shell instance.
- **Return Value:** `true` if ready, `false` otherwise.

#### shell_set_root_cmd
Sets a root command that will be active for all shell instances.
- **Parameters:** `cmd`: Pointer to a static command entry.
- **Return Value:** None.

#### shell_get_return_value
Gets the return value of the last executed command.
- **Parameters:** `sh`: Shell instance.
- **Return Value:** Last command's integer return value.

### Configuration
Modify shell behavior at runtime.

- `shell_insert_mode_set(sh, val)`: Enable/disable insert mode.
- `shell_use_colors_set(sh, val)`: Enable/disable ANSI color output.
- `shell_use_vt100_set(sh, val)`: Enable/disable VT100 terminal support.
- `shell_echo_set(sh, val)`: Enable/disable local echo.
- `shell_obscure_set(sh, obscure)`: Enable/disable obscuring input (e.g., for passwords).
- `shell_mode_delete_set(sh, val)`: Change how the delete key is handled.

### Handler Prototypes

#### shell_cmd_handler
Standard command handler.
```c
int (*)(const struct shell *sh, size_t argc, char **argv)
```

#### shell_dict_cmd_handler
Handler for dictionary-based commands (where data is associated with the command).
```c
int (*)(const struct shell *sh, size_t argc, char **argv, void *data)
```

#### shell_dynamic_get
Function used to dynamically generate subcommands or completions.
```c
void (*)(size_t idx, struct shell_static_entry *entry)
```

#### shell_bypass_cb_t
Callback for bypass mode (raw data handling).
```c
void (*)(const struct shell *sh, uint8_t *data, size_t len)
```

### Return Values
Commands should return these standard values:

- `0`: Success.
- `1` (`SHELL_CMD_HELP_PRINTED`): Command requested help, and help was printed.
- `-EINVAL`: One or more arguments are invalid.
- `-ENOEXEC`: The command failed to execute or internal logic error.

## Backends

Complete guide for configuring Zephyr Shell transport backends.

### Table of Contents

1. [Overview](#overview)
2. [UART Backend](#uart-backend)
3. [USB CDC ACM Backend](#usb-cdc-acm-backend)
4. [RTT Backend](#rtt-backend)
5. [Telnet Backend](#telnet-backend)
6. [Bluetooth LE NUS Backend](#bluetooth-le-nus-backend)
7. [RPMSG Backend](#rpmsg-backend)
8. [Dummy Backend](#dummy-backend)
9. [Multiple Backends](#multiple-backends)

---

### Overview

The shell can be connected to different transport layers for command input and output. Each backend has specific configuration requirements and use cases.

| Backend | Use Case | Connection |
|---------|----------|------------|
| UART | Default, most common | Serial terminal |
| USB CDC ACM | USB serial | USB cable |
| RTT | Debugging | J-Link probe |
| Telnet | Network access | TCP/IP |
| BLE NUS | Wireless | Bluetooth LE |
| RPMSG | Multi-core | Inter-processor |
| Dummy | Testing | Programmatic |

---

### UART Backend

The most common backend, enabled by default when shell is enabled.

#### Configuration

```kconfig
# prj.conf
CONFIG_SHELL=y
CONFIG_SHELL_BACKEND_SERIAL=y
```

#### Additional Options

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_SHELL_BACKEND_SERIAL_INTERRUPT_DRIVEN` | y | Use interrupt-driven UART |
| `CONFIG_SHELL_BACKEND_SERIAL_TX_RING_BUFFER_SIZE` | 8 | TX ring buffer size |
| `CONFIG_SHELL_BACKEND_SERIAL_RX_RING_BUFFER_SIZE` | 64 | RX ring buffer size |

#### Devicetree

The shell uses the device designated by the `zephyr,shell-uart` chosen node:

```dts
/ {
    chosen {
        zephyr,shell-uart = &uart0;
    };
};
```

#### Accessing Backend

```c
#include <zephyr/shell/shell_uart.h>

const struct shell *sh = shell_backend_uart_get_ptr();
shell_execute_cmd(sh, "help");
```

---

### USB CDC ACM Backend

Connect via USB as a virtual serial port.

#### Configuration

**Recommended**: Use the `cdc-acm-console` snippet:

```bash
west build -S cdc-acm-console [...]
```

**Manual Configuration**:

```kconfig
# prj.conf
CONFIG_USB_DEVICE_STACK=y
CONFIG_USB_CDC_ACM=y
CONFIG_SHELL=y
CONFIG_SHELL_BACKEND_SERIAL=y
```

With devicetree overlay:

```dts
/ {
    chosen {
        zephyr,shell-uart = &cdc_acm_uart0;
    };
};

&zephyr_udc0 {
    cdc_acm_uart0: cdc_acm_uart0 {
        compatible = "zephyr,cdc-acm-uart";
    };
};
```

---

### RTT Backend

Use with Segger J-Link for debugging without UART.

#### Configuration

```kconfig
# prj.conf
CONFIG_USE_SEGGER_RTT=y
CONFIG_SHELL=y
CONFIG_SHELL_BACKEND_RTT=y
CONFIG_SHELL_BACKEND_SERIAL=n  # Disable UART if not needed
```

#### Options

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_SHELL_BACKEND_RTT_BUFFER` | 0 | RTT channel/buffer index |

#### Connecting

**Using west**:
```bash
west rtt
```

**Using PuTTY**:
1. Start debug session: `west attach`
2. Open PuTTY with Telnet to localhost:19021
3. Set Terminal → Local echo: Force off
4. Set Terminal → Local line editing: Force off

**Using JLinkRTTClient** (macOS alternative):
```bash
# Terminal 1
JLinkRTTLogger -Device NRF52840_XXAA -RTTChannel 1 -if SWD -Speed 4000 ~/rtt.log

# Terminal 2
nc localhost 19021
```

#### Using RTT with Logging

To use both shell and logging via RTT on separate channels:

```kconfig
CONFIG_SHELL_BACKEND_RTT_BUFFER=0        # Shell on channel 0
CONFIG_LOG_BACKEND_RTT=y
CONFIG_LOG_BACKEND_RTT_BUFFER=1          # Logging on channel 1
```

---

### Telnet Backend

Access shell over the network.

#### Configuration

```kconfig
# prj.conf
CONFIG_NETWORKING=y
CONFIG_NET_TCP=y
CONFIG_SHELL=y
CONFIG_SHELL_BACKEND_TELNET=y
```

#### Options

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_SHELL_TELNET_PORT` | 23 | Telnet port |
| `CONFIG_SHELL_TELNET_LINE_BUF_SIZE` | 80 | Line buffer size |
| `CONFIG_SHELL_TELNET_SEND_TIMEOUT` | 100 | Send timeout (ms) |
| `CONFIG_SHELL_TELNET_SUPPORT_COMMAND` | n | Handle telnet commands |

#### Connecting

```bash
telnet <device_ip> <port>
# Example:
telnet 192.168.1.100 23
```

#### Telnet Command Support

Enable `CONFIG_SHELL_TELNET_SUPPORT_COMMAND=y` for character-at-a-time mode, which enables:
- Line editing
- Tab completion
- Command history

**Trade-off**: Increased network traffic.

---

### Bluetooth LE NUS Backend

Wireless shell access via Bluetooth LE Nordic UART Service.

#### Configuration

**Recommended**: Use the `nus-console` snippet:

```bash
west build -S nus-console [...]
```

**Manual Configuration**:

There is no `SHELL_BACKEND_NUS`. Upstream exposes NUS as a *virtual UART*
(`CONFIG_UART_BT`), so the shell keeps using the normal serial backend and
you point `zephyr,shell-uart` at the NUS UART node in devicetree:

```kconfig
# prj.conf
CONFIG_BT=y
CONFIG_BT_PERIPHERAL=y
CONFIG_BT_ZEPHYR_NUS=y
CONFIG_BT_ZEPHYR_NUS_AUTO_START_BLUETOOTH=y
CONFIG_UART_BT=y
CONFIG_SHELL=y
CONFIG_SHELL_BACKEND_SERIAL=y

# NUS needs a bigger RX stack and larger ACL buffers to be usable
CONFIG_BT_RX_STACK_SIZE=2048
CONFIG_BT_L2CAP_TX_MTU=512
CONFIG_BT_BUF_ACL_RX_SIZE=502
CONFIG_BT_BUF_ACL_TX_SIZE=502
```

```dts
/* app.overlay */
/ {
    chosen {
        zephyr,console = &bt_nus_console_uart;
        zephyr,shell-uart = &bt_nus_console_uart;
    };

    bt_nus_console_uart: bt_nus_console_uart {
        compatible = "zephyr,nus-uart";
        rx-fifo-size = <1024>;
        tx-fifo-size = <1024>;
    };
};
```

The `nus-console` snippet above is exactly this config — prefer it.

#### Connecting

Use a BLE terminal app that supports NUS (Nordic UART Service):
- nRF Connect (mobile)
- Serial Bluetooth Terminal (Android)
- BLE Serial (iOS)

---

### RPMSG Backend

For multi-core systems with inter-processor communication.

#### Configuration

```kconfig
# prj.conf
CONFIG_SHELL=y
CONFIG_SHELL_BACKEND_RPMSG=y
CONFIG_IPC_SERVICE=y
CONFIG_MBOX=y
```

#### Devicetree

```dts
/ {
    chosen {
        zephyr,shell-ipc = &ipc0;
    };
};
```

---

### Dummy Backend

For testing and programmatic command execution.

#### Configuration

```kconfig
# prj.conf
CONFIG_SHELL=y
CONFIG_SHELL_BACKEND_DUMMY=y
```

#### Usage

```c
#include <zephyr/shell/shell_dummy.h>

int main(void)
{
    const struct shell *sh = shell_backend_dummy_get_ptr();

    /* Execute command programmatically */
    shell_execute_cmd(sh, "help");

    /* Get output from dummy backend */
    const char *output = shell_backend_dummy_get_output(sh, &output_size);
}
```

---

### Multiple Backends

Multiple backends can be active simultaneously.

#### Example: UART + RTT

```kconfig
CONFIG_SHELL=y
CONFIG_SHELL_BACKEND_SERIAL=y
CONFIG_USE_SEGGER_RTT=y
CONFIG_SHELL_BACKEND_RTT=y
```

#### Backend Access

```c
#include <zephyr/shell/shell_uart.h>
#include <zephyr/shell/shell_rtt.h>

/* Execute on specific backend */
shell_execute_cmd(shell_backend_uart_get_ptr(), "version");
shell_execute_cmd(shell_backend_rtt_get_ptr(), "kernel threads");
```

#### Considerations

- Each backend runs independently
- Commands registered once, available on all backends
- Log messages may appear on multiple backends if shell log backend enabled
- Memory usage increases with each backend

## Debugging

### Common Build Errors

#### Undefined Reference to Shell Commands
**Error:** `undefined reference to __shell_root_cmds_start` or similar linker errors.
**Cause:** Shell subsystem is not properly initialized or no commands are registered.
**Solution:** Ensure `CONFIG_SHELL=y` is enabled in `prj.conf`.

#### Missing Header File
**Error:** `fatal error: zephyr/shell/shell.h: No such file or directory`
**Cause:** The shell header is included but the shell subsystem is not enabled in Kconfig.
**Solution:** Add `CONFIG_SHELL=y` to your configuration.

#### Duplicate Command Names
**Error:** No build error, but only one command appears or system behaves unexpectedly.
**Cause:** Multiple calls to `SHELL_CMD_REGISTER` or `SHELL_SUBCMD_SET_CREATE` using the same command name at the same level.
**Solution:** Ensure every command and sub-command name is unique within its parent context.

### Runtime Issues

#### Command Not Appearing in Help
**Issue:** Command is registered in code but does not show up when typing `help`.
**Checklist:**
1. Verify `CONFIG_SHELL=y` is in the final `.config`.
2. Ensure the file containing `SHELL_CMD_REGISTER` is actually being compiled (check `CMakeLists.txt`).
3. Check if the command is conditional on a Kconfig that is disabled.

#### Wrong Argument Count
**Issue:** Command returns "Invalid number of arguments" or similar.
**Cause:** The `min_args` and `max_args` parameters in `SHELL_CMD_ARG_REGISTER` do not match the input.
**Solution:**
- `min_args`: Minimum arguments including the command name itself.
- `max_args`: Maximum arguments including the command name itself.

#### Output Not Visible
**Issue:** `shell_print()` or `shell_fprintf()` calls execute but nothing appears on the console.
**Cause:**
1. Incorrect shell backend configuration (e.g., `CONFIG_SHELL_BACKEND_SERIAL` vs `CONFIG_SHELL_BACKEND_RTT`).
2. Logging level is suppressing output if the shell is integrated with the logger.
3. The shell thread has lower priority than a CPU-bound thread.
**Solution:** Verify backend settings and check if other shell output (like the prompt) is visible.

### Debugging Techniques

#### Checking .config
Always verify the generated configuration in the build directory:
```bash
grep CONFIG_SHELL builds/zephyr/.config
```

#### Using Shell Statistics
If `CONFIG_SHELL_STATS=y` is enabled, use the built-in stats command to monitor shell behavior:
```bash
shell stats show
```

#### Verifying Backend
Ensure the physical transport is working.
For UART:
```kconfig
CONFIG_SHELL_BACKEND_SERIAL=y
CONFIG_UART_CONSOLE=y
```
For RTT:
```kconfig
CONFIG_SHELL_BACKEND_RTT=y
CONFIG_USE_SEGGER_RTT=y
```

### Kconfig Verification

#### Required Base Options
Ensure these are set for a functional shell:
- `CONFIG_SHELL=y`: Enables the shell subsystem.
- `CONFIG_SHELL_BACKENDS=y`: Enables shell backends.
- `CONFIG_SHELL_BACKEND_SERIAL=y`: Typical for physical console access.

#### Troubleshooting Dependencies
The shell requires `CONFIG_MULTITHREADING=y`. If multithreading is disabled, the shell subsystem will not function as it relies on its own thread.

### Best Practices for Debugging

1. **Simplify Commands**: Test with a basic `SHELL_CMD_REGISTER` without subcommands or complex arguments first.
2. **Check Return Codes**: Command handlers should return `0` on success.
3. **Use Dummy Backend**: Use `CONFIG_SHELL_BACKEND_DUMMY=y` to verify shell logic without hardware dependencies.
4. **Stack Size**: If a command causes a crash or stack overflow, increase `CONFIG_SHELL_STACK_SIZE`.

## Examples

Complete working examples for common Zephyr shell command patterns.

### Table of Contents

1. [Simple Root Command](#simple-root-command)
2. [Command with Arguments](#command-with-arguments)
3. [Commands with Subcommands](#commands-with-subcommands)
4. [Device Control Commands](#device-control-commands)
5. [Dictionary Commands](#dictionary-commands)
6. [Dynamic Commands](#dynamic-commands)
7. [Conditional Commands](#conditional-commands)
8. [Commands with Getopt](#commands-with-getopt)
9. [Driver Shell Integration](#driver-shell-integration)

---

### Simple Root Command

Minimal command with no arguments.

```c
#include <zephyr/shell/shell.h>

static int cmd_version(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "Firmware v1.0.0");
    shell_print(sh, "Build: %s %s", __DATE__, __TIME__);
    return 0;
}

SHELL_CMD_REGISTER(version, NULL, "Show firmware version", cmd_version);
```

**Usage:**
```
uart:~$ version
Firmware v1.0.0
Build: Feb 10 2026 10:30:00
```

---

### Command with Arguments

Command that validates and uses arguments.

```c
#include <zephyr/shell/shell.h>
#include <stdlib.h>

static int cmd_repeat(const struct shell *sh, size_t argc, char **argv)
{
    int count;
    int ret = 0;

    /* Parse count argument */
    count = shell_strtoul(argv[1], 10, &ret);
    if (ret != 0) {
        shell_error(sh, "Invalid count: %s", argv[1]);
        return -EINVAL;
    }

    /* Print message the specified number of times */
    for (int i = 0; i < count; i++) {
        shell_print(sh, "[%d] %s", i + 1, argv[2]);
    }

    return 0;
}

/* mandatory=3: command + count + message */
SHELL_CMD_ARG_REGISTER(repeat, NULL,
    "Repeat message N times\n"
    "Usage: repeat <count> <message>",
    cmd_repeat, 3, 0);
```

**Usage:**
```
uart:~$ repeat 3 hello
[1] hello
[2] hello
[3] hello
```

---

### Commands with Subcommands

Hierarchical command structure with subcommands.

```c
#include <zephyr/shell/shell.h>

/* Subcommand handlers */
static int cmd_led_on(const struct shell *sh, size_t argc, char **argv)
{
    shell_print(sh, "LED turned ON");
    /* gpio_pin_set_dt(&led, 1); */
    return 0;
}

static int cmd_led_off(const struct shell *sh, size_t argc, char **argv)
{
    shell_print(sh, "LED turned OFF");
    /* gpio_pin_set_dt(&led, 0); */
    return 0;
}

static int cmd_led_toggle(const struct shell *sh, size_t argc, char **argv)
{
    shell_print(sh, "LED toggled");
    /* gpio_pin_toggle_dt(&led); */
    return 0;
}

static int cmd_led_status(const struct shell *sh, size_t argc, char **argv)
{
    shell_print(sh, "LED status: ON");
    /* shell_print(sh, "LED status: %s", gpio_pin_get_dt(&led) ? "ON" : "OFF"); */
    return 0;
}

/* Create subcommand set */
SHELL_STATIC_SUBCMD_SET_CREATE(led_cmds,
    SHELL_CMD(on,     NULL, "Turn LED on",     cmd_led_on),
    SHELL_CMD(off,    NULL, "Turn LED off",    cmd_led_off),
    SHELL_CMD(toggle, NULL, "Toggle LED",      cmd_led_toggle),
    SHELL_CMD(status, NULL, "Show LED status", cmd_led_status),
    SHELL_SUBCMD_SET_END
);

/* Register root command with subcommands */
SHELL_CMD_REGISTER(led, &led_cmds, "LED control commands", NULL);
```

**Usage:**
```
uart:~$ led on
LED turned ON
uart:~$ led status
LED status: ON
uart:~$ led
led - LED control commands
Subcommands:
  on      : Turn LED on
  off     : Turn LED off
  toggle  : Toggle LED
  status  : Show LED status
```

---

### Device Control Commands

Pattern for device-specific commands.

```c
#include <zephyr/shell/shell.h>
#include <zephyr/drivers/sensor.h>

static int cmd_sensor_read(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev;
    struct sensor_value temp;
    int ret;

    /* Get device by name from argument */
    dev = shell_device_get_binding(argv[1]);
    if (dev == NULL) {
        shell_error(sh, "Device not found: %s", argv[1]);
        return -ENODEV;
    }

    /* Sample the sensor */
    ret = sensor_sample_fetch(dev);
    if (ret < 0) {
        shell_error(sh, "Failed to fetch sample: %d", ret);
        return ret;
    }

    /* Get temperature value */
    ret = sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &temp);
    if (ret < 0) {
        shell_error(sh, "Failed to get temperature: %d", ret);
        return ret;
    }

    shell_print(sh, "Temperature: %d.%06d C",
                temp.val1, temp.val2);
    return 0;
}

SHELL_CMD_ARG_REGISTER(sensor_read, NULL,
    "Read sensor value\n"
    "Usage: sensor_read <device_name>",
    cmd_sensor_read, 2, 0);
```

---

### Dictionary Commands

Map string values to data.

```c
#include <zephyr/shell/shell.h>

/* Handler receives data pointer with the mapped value */
static int log_level_handler(const struct shell *sh,
                             size_t argc, char **argv, void *data)
{
    int level = (int)(intptr_t)data;

    shell_print(sh, "Log level set to: %s (value: %d)", argv[0], level);
    /* log_filter_set(NULL, 0, level); */

    return 0;
}

/* Dictionary: (syntax, value, help) */
SHELL_SUBCMD_DICT_SET_CREATE(log_level_cmds, log_level_handler,
    (none,  0, "Disable logging"),
    (err,   1, "Error level only"),
    (wrn,   2, "Warning and above"),
    (inf,   3, "Info and above"),
    (dbg,   4, "Debug (all messages)")
);

SHELL_CMD_REGISTER(loglevel, &log_level_cmds, "Set log level", NULL);
```

**Usage:**
```
uart:~$ loglevel dbg
Log level set to: dbg (value: 4)
uart:~$ loglevel
loglevel - Set log level
Subcommands:
  none : Disable logging
  err  : Error level only
  wrn  : Warning and above
  inf  : Info and above
  dbg  : Debug (all messages)
```

---

### Dynamic Commands

Commands determined at runtime.

```c
#include <zephyr/shell/shell.h>
#include <string.h>

/* Storage for dynamic command names */
#define MAX_DYNAMIC_CMDS 10
#define MAX_CMD_LEN 32

static char dynamic_cmds[MAX_DYNAMIC_CMDS][MAX_CMD_LEN];
static uint8_t dynamic_cmd_count;

/* Dynamic command getter function */
static void dynamic_cmd_get(size_t idx, struct shell_static_entry *entry)
{
    if (idx < dynamic_cmd_count) {
        entry->syntax = dynamic_cmds[idx];
        entry->handler = NULL;
        entry->subcmd = NULL;
        entry->help = "Dynamic command";
    } else {
        entry->syntax = NULL;  /* No more commands */
    }
}

SHELL_DYNAMIC_CMD_CREATE(dynamic_subcmds, dynamic_cmd_get);

/* Add a new dynamic command */
static int cmd_dynamic_add(const struct shell *sh, size_t argc, char **argv)
{
    if (dynamic_cmd_count >= MAX_DYNAMIC_CMDS) {
        shell_error(sh, "Maximum commands reached");
        return -ENOMEM;
    }

    strncpy(dynamic_cmds[dynamic_cmd_count], argv[1], MAX_CMD_LEN - 1);
    dynamic_cmds[dynamic_cmd_count][MAX_CMD_LEN - 1] = '\0';
    dynamic_cmd_count++;

    shell_print(sh, "Added command: %s", argv[1]);
    return 0;
}

/* List dynamic commands */
static int cmd_dynamic_list(const struct shell *sh, size_t argc, char **argv)
{
    shell_print(sh, "Dynamic commands (%d):", dynamic_cmd_count);
    for (int i = 0; i < dynamic_cmd_count; i++) {
        shell_print(sh, "  %d: %s", i, dynamic_cmds[i]);
    }
    return 0;
}

/* Execute a dynamic command */
static int cmd_dynamic_exec(const struct shell *sh, size_t argc, char **argv)
{
    shell_print(sh, "Executing dynamic command: %s", argv[1]);
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(dynamic_cmds_set,
    SHELL_CMD_ARG(add, NULL, "Add dynamic command", cmd_dynamic_add, 2, 0),
    SHELL_CMD(list, NULL, "List dynamic commands", cmd_dynamic_list),
    SHELL_CMD_ARG(exec, &dynamic_subcmds, "Execute dynamic command",
                  cmd_dynamic_exec, 2, 0),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(dynamic, &dynamic_cmds_set, "Dynamic command demo", NULL);
```

**Usage:**
```
uart:~$ dynamic add test1
Added command: test1
uart:~$ dynamic add test2
Added command: test2
uart:~$ dynamic list
Dynamic commands (2):
  0: test1
  1: test2
uart:~$ dynamic exec test<TAB>
test1  test2
```

---

### Conditional Commands

Commands enabled by Kconfig flags.

```c
#include <zephyr/shell/shell.h>

static int cmd_debug_dump(const struct shell *sh, size_t argc, char **argv)
{
    shell_print(sh, "Debug dump...");
    /* Dump debug information */
    return 0;
}

static int cmd_debug_reset(const struct shell *sh, size_t argc, char **argv)
{
    shell_print(sh, "Resetting system...");
    /* sys_reboot(SYS_REBOOT_COLD); */
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(debug_cmds,
    /* Always available */
    SHELL_CMD(dump, NULL, "Dump debug info", cmd_debug_dump),

    /* Only when CONFIG_DEBUG_COMMANDS=y */
    SHELL_COND_CMD(CONFIG_DEBUG_COMMANDS, reset, NULL,
                   "Reset system", cmd_debug_reset),
    SHELL_SUBCMD_SET_END
);

/* Conditionally register entire command tree */
SHELL_COND_CMD_REGISTER(CONFIG_SHELL_DEBUG, debug, &debug_cmds,
                        "Debug commands", NULL);
```

---

### Commands with Getopt

Standard argument parsing with getopt.

**Kconfig:**
```kconfig
CONFIG_POSIX_C_LIB_EXT=y
CONFIG_GETOPT_LONG=y
CONFIG_SHELL_GETOPT=y
```

**Code:**
```c
#include <zephyr/shell/shell.h>
#include <getopt.h>

static int cmd_config(const struct shell *sh, size_t argc, char **argv)
{
    int opt;
    bool verbose = false;
    const char *name = NULL;
    int count = 1;

    /* Reset getopt */
    optreset = 1;
    optind = 1;

    while ((opt = getopt(argc, argv, "vn:c:h")) != -1) {
        switch (opt) {
        case 'v':
            verbose = true;
            break;
        case 'n':
            name = optarg;
            break;
        case 'c':
            count = atoi(optarg);
            break;
        case 'h':
            shell_print(sh, "Usage: config [-v] [-n name] [-c count]");
            shell_print(sh, "  -v        Verbose mode");
            shell_print(sh, "  -n name   Set name");
            shell_print(sh, "  -c count  Set count");
            return 0;
        default:
            shell_error(sh, "Unknown option: %c", opt);
            return -EINVAL;
        }
    }

    shell_print(sh, "Configuration:");
    shell_print(sh, "  Verbose: %s", verbose ? "yes" : "no");
    shell_print(sh, "  Name: %s", name ? name : "(not set)");
    shell_print(sh, "  Count: %d", count);

    return 0;
}

SHELL_CMD_ARG_REGISTER(config, NULL,
    "Configure settings\n"
    "Usage: config [-v] [-n name] [-c count] [-h]",
    cmd_config, 1, 6);
```

**Usage:**
```
uart:~$ config -v -n myapp -c 5
Configuration:
  Verbose: yes
  Name: myapp
  Count: 5
```

---

### Driver Shell Integration

Pattern for driver-specific shell commands (e.g., GPIO shell).

```c
#include <zephyr/shell/shell.h>
#include <zephyr/drivers/gpio.h>

/* Get device from argument */
static const struct device *get_gpio_device(const struct shell *sh,
                                            const char *name)
{
    const struct device *dev = shell_device_get_binding(name);

    if (dev == NULL) {
        shell_error(sh, "GPIO device not found: %s", name);
    }
    return dev;
}

static int cmd_gpio_set(const struct shell *sh, size_t argc, char **argv)
{
    const struct device *dev;
    gpio_pin_t pin;
    int value;
    int ret;

    dev = get_gpio_device(sh, argv[1]);
    if (dev == NULL) {
        return -ENODEV;
    }

    pin = shell_strtoul(argv[2], 10, &ret);
    if (ret != 0) {
        shell_error(sh, "Invalid pin: %s", argv[2]);
        return -EINVAL;
    }

    value = shell_strtoul(argv[3], 10, &ret);
    if (ret != 0 || value > 1) {
        shell_error(sh, "Invalid value (0 or 1): %s", argv[3]);
        return -EINVAL;
    }

    ret = gpio_pin_set(dev, pin, value);
    if (ret < 0) {
        shell_error(sh, "Failed to set pin: %d", ret);
        return ret;
    }

    shell_print(sh, "%s pin %d = %d", argv[1], pin, value);
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(mygpio_cmds,
    SHELL_CMD_ARG(set, NULL,
        "Set GPIO pin\n"
        "Usage: mygpio set <device> <pin> <0|1>",
        cmd_gpio_set, 4, 0),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(mygpio, &mygpio_cmds, "Custom GPIO commands", NULL);
```

## Kconfig

Complete reference for Zephyr Shell subsystem Kconfig configuration options.

### Table of Contents

1. [Essential Options](#essential-options)
2. [Backend Options](#backend-options)
3. [Feature Options](#feature-options)
4. [Memory Optimization](#memory-optimization)
5. [Built-in Commands](#built-in-commands)

---

### Essential Options

#### Core Shell

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_SHELL` | bool | n | Enable the shell subsystem |
| `CONFIG_SHELL_THREAD_PRIORITY_OVERRIDE` | bool | n | Override default shell thread priority |
| `CONFIG_SHELL_STACK_SIZE` | int | 2048 | Shell thread stack size |

#### Prompt Configuration

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_SHELL_PROMPT_UART` | string | "uart:~$" | Default shell prompt |
| `CONFIG_SHELL_PROMPT_BUFF_SIZE` | int | 32 | Prompt buffer size |

---

### Backend Options

#### UART Backend (Most Common)

```kconfig
CONFIG_SHELL=y
CONFIG_SHELL_BACKEND_SERIAL=y
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_SHELL_BACKEND_SERIAL` | bool | y | Enable UART/serial backend |
| `CONFIG_SHELL_BACKEND_SERIAL_INIT_PRIORITY` | int | 0 | Initialization priority |
| `CONFIG_SHELL_BACKEND_SERIAL_INTERRUPT_DRIVEN` | bool | y | Use interrupt-driven UART |

#### RTT Backend (Segger J-Link)

```kconfig
CONFIG_USE_SEGGER_RTT=y
CONFIG_SHELL_BACKEND_RTT=y
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_SHELL_BACKEND_RTT` | bool | n | Enable RTT backend |
| `CONFIG_SHELL_BACKEND_RTT_BUFFER` | int | 0 | RTT buffer/channel index |

#### USB CDC ACM Backend

Use snippet `cdc-acm-console`:
```bash
west build -S cdc-acm-console [...]
```

#### Telnet Backend

```kconfig
CONFIG_SHELL_BACKEND_TELNET=y
CONFIG_NETWORKING=y
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_SHELL_BACKEND_TELNET` | bool | n | Enable Telnet backend |
| `CONFIG_SHELL_TELNET_PORT` | int | 23 | Telnet port number |
| `CONFIG_SHELL_TELNET_SUPPORT_COMMAND` | bool | n | Handle telnet commands |

#### Bluetooth LE NUS Backend

Use snippet `nus-console`:
```bash
west build -S nus-console [...]
```

#### Dummy Backend (Testing)

```kconfig
CONFIG_SHELL_BACKEND_DUMMY=y
```

Useful for executing shell commands programmatically without physical transport.

---

### Feature Options

#### Tab Completion

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_SHELL_TAB` | bool | y | Enable Tab key functionality |
| `CONFIG_SHELL_TAB_AUTOCOMPLETION` | bool | y | Enable auto-completion |

#### Command History

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_SHELL_HISTORY` | bool | y | Enable command history |
| `CONFIG_SHELL_HISTORY_BUFFER` | int | 128 | History buffer size (bytes) |

#### Wildcards

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_SHELL_WILDCARD` | bool | y | Enable wildcard support (`*`, `?`) |

#### Meta Keys

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_SHELL_METAKEYS` | bool | y | Enable Ctrl+key shortcuts |

#### VT100 / Colors

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_SHELL_VT100_COLORS` | bool | y | Enable colored output |
| `CONFIG_SHELL_VT100_COMMANDS` | bool | y | Enable VT100 escape sequences |

#### Getopt Support

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_SHELL_GETOPT` | bool | n | Thread-safe getopt for shell |

Requires:
```kconfig
CONFIG_POSIX_C_LIB_EXT=y
CONFIG_GETOPT_LONG=y
```

#### Help System

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_SHELL_HELP` | bool | y | Enable help for commands (`-h`, `--help`) |
| `CONFIG_SHELL_HELP_ON_WRONG_ARGUMENT_COUNT` | bool | y | Print help when args wrong |

---

### Memory Optimization

#### Minimal Shell Configuration

For resource-constrained devices, enable minimal mode:

```kconfig
CONFIG_SHELL_MINIMAL=y
```

This disables many features by default. Selectively re-enable what you need:

```kconfig
CONFIG_SHELL_MINIMAL=y
CONFIG_SHELL_HISTORY=n
CONFIG_SHELL_WILDCARD=n
CONFIG_SHELL_METAKEYS=n
CONFIG_SHELL_TAB_AUTOCOMPLETION=n
CONFIG_SHELL_VT100_COLORS=n
```

#### Buffer Sizes

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_SHELL_CMD_BUFF_SIZE` | int | 128 | Command buffer size |
| `CONFIG_SHELL_PRINTF_BUFF_SIZE` | int | 30 | Printf buffer size |
| `CONFIG_SHELL_ARGC_MAX` | int | 12 | Maximum argument count |

---

### Built-in Commands

#### Enable/Disable Built-in Commands

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_SHELL_CMDS` | bool | y | Enable built-in commands (clear, history, etc.) |
| `CONFIG_SHELL_CMDS_RESIZE` | bool | n | Enable `resize` command |
| `CONFIG_SHELL_CMDS_SELECT` | bool | n | Enable `select` command (set root) |

#### Available Built-in Commands

When `CONFIG_SHELL_CMDS=y`:
- `clear` - Clear screen
- `history` - Show command history
- `shell` - Shell configuration subcommands
  - `echo` - Toggle echo
  - `colors` - Toggle colored output
  - `stats` - Show shell statistics

---

### Logging Integration

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_SHELL_LOG_BACKEND` | bool | y | Shell as logging backend |
| `CONFIG_SHELL_LOG_LEVEL` | int | 3 (INFO) | Default log level for shell |

**Warning**: Shell is a complex logger backend. For early boot debugging, use simpler backends like `CONFIG_LOG_BACKEND_UART`.

---

### Example Configurations

#### Full-Featured Development

```kconfig
CONFIG_SHELL=y
CONFIG_SHELL_BACKEND_SERIAL=y
CONFIG_SHELL_TAB=y
CONFIG_SHELL_TAB_AUTOCOMPLETION=y
CONFIG_SHELL_HISTORY=y
CONFIG_SHELL_HISTORY_BUFFER=512
CONFIG_SHELL_WILDCARD=y
CONFIG_SHELL_METAKEYS=y
CONFIG_SHELL_VT100_COLORS=y
CONFIG_SHELL_CMDS=y
CONFIG_SHELL_LOG_BACKEND=y
```

#### Minimal Production

```kconfig
CONFIG_SHELL=y
CONFIG_SHELL_MINIMAL=y
CONFIG_SHELL_BACKEND_SERIAL=y
CONFIG_SHELL_HISTORY=n
CONFIG_SHELL_WILDCARD=n
CONFIG_SHELL_VT100_COLORS=n
CONFIG_SHELL_STACK_SIZE=1024
CONFIG_SHELL_CMD_BUFF_SIZE=64
```

#### RTT for Debugging

```kconfig
CONFIG_USE_SEGGER_RTT=y
CONFIG_SHELL=y
CONFIG_SHELL_BACKEND_RTT=y
CONFIG_SHELL_BACKEND_SERIAL=n
CONFIG_RTT_CONSOLE=n
```

## Macros

This reference document covers the macros provided by `shell.h` for registering commands, subcommands, and managing shell behavior.

### Root Command Registration

These macros register a command at the top level of the shell.

#### `SHELL_CMD_REGISTER(syntax, subcmd, help, handler)`
Registers a root command without argument validation.
- **syntax**: Command name (string).
- **subcmd**: Pointer to a subcommand set (created via `SHELL_STATIC_SUBCMD_SET_CREATE`) or `NULL`.
- **help**: Help string displayed by the `help` command.
- **handler**: Function pointer to the command handler of type `shell_cmd_handler`.

#### `SHELL_CMD_ARG_REGISTER(syntax, subcmd, help, handler, mandatory, optional)`
Registers a root command with argument validation.
- **syntax**: Command name (string).
- **subcmd**: Pointer to subcommands or `NULL`.
- **help**: Help string.
- **handler**: Command handler.
- **mandatory**: Number of mandatory arguments. **Note**: This count includes the command name itself. (e.g., `mandatory=2` means the command + 1 required argument).
- **optional**: Number of optional arguments.

#### `SHELL_COND_CMD_REGISTER(flag, syntax, subcmd, help, handler)`
Conditionally registers a root command based on a Kconfig flag or macro existence.
- **flag**: If the flag evaluates to true (non-zero), the command is registered.

#### `SHELL_COND_CMD_ARG_REGISTER(flag, syntax, subcmd, help, handler, mandatory, optional)`
Conditionally registers a root command with argument validation.

---

### Subcommand Creation

Macros used to define sets of subcommands.

#### `SHELL_STATIC_SUBCMD_SET_CREATE(name, ...)`
Creates a static set of subcommands.
- **name**: The name of the subcommand set variable.
- **...**: A list of subcommand entries (e.g., `SHELL_CMD`, `SHELL_CMD_ARG`).
- **Note**: Must always be terminated with `SHELL_SUBCMD_SET_END`.

#### `SHELL_SUBCMD_SET_CREATE(_name, _parent)`
Creates a subcommand set that can be extended using `SHELL_SUBCMD_ADD`.
- **_name**: Name of the set.
- **_parent**: Parent command name.

#### `SHELL_SUBCMD_ADD(_parent, _syntax, _subcmd, _help, _handler, _mand, _opt)`
Adds a subcommand to a set created via `SHELL_SUBCMD_SET_CREATE`.
- **_parent**: Name of the parent subcommand set.
- **_syntax**: Subcommand name.
- **_subcmd**: Pointer to further subcommands or `NULL`.
- **_mand**: Mandatory arguments (including subcommand name).
- **_opt**: Optional arguments.

#### `SHELL_SUBCMD_COND_ADD(_flag, _parent, _syntax, _subcmd, _help, _handler, _mand, _opt)`
Conditionally adds a subcommand to a set.

#### `SHELL_SUBCMD_SET_END`
Mandatory terminator for `SHELL_STATIC_SUBCMD_SET_CREATE`.

---

### Subcommand Entries

Macros used within `SHELL_STATIC_SUBCMD_SET_CREATE` to define individual subcommands.

#### `SHELL_CMD(syntax, subcmd, help, handler)`
Defines a subcommand entry without argument validation.

#### `SHELL_CMD_ARG(syntax, subcmd, help, handler, mand, opt)`
Defines a subcommand entry with argument validation.
- **mand**: Mandatory arguments (including subcommand name).

#### `SHELL_COND_CMD(flag, syntax, subcmd, help, handler)`
Conditionally defines a subcommand entry.

#### `SHELL_COND_CMD_ARG(flag, syntax, subcmd, help, handler, mand, opt)`
Conditionally defines a subcommand entry with argument validation.

#### `SHELL_EXPR_CMD(expr, syntax, subcmd, help, handler)`
Defines a subcommand entry that is enabled if `expr` is true at runtime.

#### `SHELL_EXPR_CMD_ARG(expr, syntax, subcmd, help, handler, mand, opt)`
Defines a subcommand entry with runtime expression check and argument validation.

---

### Dynamic and Dictionary Commands

#### `SHELL_DYNAMIC_CMD_CREATE(name, get)`
Creates a dynamic subcommand set.
- **name**: Name of the set.
- **get**: Function pointer to a function that returns a subcommand at a given index.

#### `SHELL_SUBCMD_DICT_SET_CREATE(name, handler, ...)`
Creates a dictionary subcommand set. All subcommands in this set share the same handler, but pass a different value (from the dictionary) to it.
- **...**: List of dictionary entries: `(syntax, value, help)`.

---

### Helper Macros

#### `SHELL_HELP(description, usage)`
Defines a help structure for a command.
- **description**: Brief description.
- **usage**: Detailed usage string.

---

### Constants

Special values used in `mandatory` or `optional` argument counts.

#### `SHELL_OPT_ARG_RAW` (0xFE)
If used as the value for `optional` arguments, the shell will treat all remaining characters in the command line as a single, raw string and pass it as the last argument in `argv`.

#### `SHELL_OPT_ARG_CHECK_SKIP` (0xFF)
Used to skip argument validation for optional arguments, allowing an unlimited number.

#### `SHELL_OPT_ARG_MAX` (0xFD)
The maximum number of optional arguments that can be validated.

---

### Argument Counting Note

For all macros involving `mandatory` and `optional` arguments:
- **Mandatory count**: Includes the command (or subcommand) name itself.
- **Example**: A command `log level <val>` where `val` is required:
  - `mandatory = 2` (one for `level`, one for `<val>`)
  - `optional = 0`

### Example Usage

```c
static int cmd_demo_ping(const struct shell *sh, size_t argc, char **argv) {
    shell_print(sh, "pong");
    return 0;
}

static int cmd_demo_echo(const struct shell *sh, size_t argc, char **argv) {
    shell_print(sh, "%s", argv[1]);
    return 0;
}

/* Define subcommands */
SHELL_STATIC_SUBCMD_SET_CREATE(sub_demo,
    SHELL_CMD(ping, NULL, "Ping command", cmd_demo_ping),
    /* mandatory=2: 'echo' + 1 arg */
    SHELL_CMD_ARG(echo, NULL, "Echo command", cmd_demo_echo, 2, 0),
    SHELL_SUBCMD_SET_END
);

/* Register root command */
SHELL_CMD_REGISTER(demo, &sub_demo, "Demo commands", NULL);
```
