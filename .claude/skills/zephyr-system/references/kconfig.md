# Kconfig

## Overview

Expert guidance on Zephyr's Kconfig system for build-time configuration, symbol definition, and dependency management.

### Table of Contents

1. [Core Concepts](#core-concepts)
2. [Common Workflows](#common-workflows)
3. [Configuration Files](#configuration-files)
4. [Advanced Topics](#advanced-topics)
5. [Troubleshooting](#troubleshooting)

---

### Core Concepts

#### Define vs. Configure (CRITICAL)

| Action | File | Syntax |
|--------|------|--------|
| **Define** symbol | `Kconfig` | `config MY_FEAT` (NO prefix) |
| **Configure** value | `prj.conf` | `CONFIG_MY_FEAT=y` (WITH prefix) |

#### Symbol Types

- **bool**: `y` or `n`
- **int**: Integer value
- **hex**: Hexadecimal value
- **string**: Quoted text

#### Key Files After Build
- `build/zephyr/.config` — Final resolved configuration
- `build/zephyr/kconfig/Kconfig.modules` — Auto-generated module Kconfigs

---

### Common Workflows

#### 1. Defining Symbols in Kconfig
Create new configuration options for drivers, modules, or apps.

- **Syntax (types, menus, choices, conditionals)**: See [syntax.md](#syntax)
- **Best practices (naming, placement, dependencies)**: See [best_practices.md](#best_practices)

**Quick Example:**
```kconfig
config MY_DRIVER_ENABLE
    bool "Enable My Driver"
    depends on I2C
    help
      Enable support for My Driver over I2C.
```

#### 2. Configuring Applications
Set values in `prj.conf` or board-specific overlays.

| Type | Example |
|------|---------|
| Boolean | `CONFIG_LOG=y` |
| Integer | `CONFIG_MAIN_STACK_SIZE=2048` |
| String | `CONFIG_BT_DEVICE_NAME="MyDevice"` |
| Hex | `CONFIG_FLASH_BASE_ADDRESS=0x08000000` |

#### 3. Using Menuconfig
Interactive configuration exploration and modification.

- **Launch and navigate menuconfig**: See [menuconfig.md](#menuconfig)

```bash
west build -t menuconfig
```

#### 4. Writing Module Kconfig
Integrate external modules with Zephyr's build system.

- **Module integration details**: See [best_practices.md](#modules--drivers)

---

### Configuration Files

#### Application Level
| File | Purpose |
|------|---------|
| `prj.conf` | Main app configuration |
| `boards/<board>.conf` | Board-specific overrides |
| `app.overlay` | Devicetree + Kconfig overlay |

#### Module/Driver Level
| File | Purpose |
|------|---------|
| `Kconfig` | Symbol definitions |
| `zephyr/module.yml` | Module metadata pointing to Kconfig |

---

### Advanced Topics

#### Kconfig Functions
Integrate Kconfig with devicetree at build time.

- **Functions reference (dt_chosen_enabled, dt_nodelabel_enabled, etc.)**: See [functions.md](#functions)

#### Practical Examples
Complete Kconfig patterns for common scenarios.

- **Driver, subsystem, choice examples**: See [examples.md](#examples)

---

### Troubleshooting

For common errors and debugging techniques:
- See [debugging.md](#debugging)

#### Quick Reference

| Error | Likely Cause | Fix |
|-------|--------------|-----|
| "X" has unmet dependencies | Missing `depends on` | Enable required dependency first |
| Symbol not visible | `depends on` condition false | Check what it depends on |
| Unknown symbol "X" | Typo or Kconfig not sourced | Verify spelling, check module.yml |
| warning: Y selected by X but not visible | `select` bypassing `depends on` | Use `imply` or fix dependencies |

---

### References

- [syntax.md](#syntax) — Types, menus, choices, conditionals, sourcing
- [best_practices.md](#best_practices) — Naming, organization, modules, dependency safety
- [menuconfig.md](#menuconfig) — Interactive configuration workflow
- [functions.md](#functions) — Kconfig functions for devicetree integration
- [examples.md](#examples) — Complete Kconfig patterns
- [debugging.md](#debugging) — Error resolution and debugging techniques

## Best Practices

### Table of Contents

1. [Defining vs. Configuring](#1-defining-vs-configuring)
2. [Naming Conventions](#2-naming-conventions)
3. [File Locations](#3-file-locations)
4. [Dependencies](#4-dependencies-depends-on-vs-select)
5. [Description Guidelines](#5-description-guidelines)
6. [Modules & Drivers](#6-modules--drivers)
7. [Overlay Configurations](#7-overlay-configurations)
8. [Anti-Patterns](#8-anti-patterns)

---

### 1. Defining vs. Configuring

| Action | File | Syntax |
|--------|------|--------|
| **Define** (create symbol) | `Kconfig` | `config MY_SYMBOL` |
| **Configure** (set value) | `prj.conf` | `CONFIG_MY_SYMBOL=y` |

**Key insight:** Kconfig files create options; prj.conf sets values for existing options.

---

### 2. Naming Conventions

#### Prefix Rules

- In `Kconfig`: define as `MY_SYMBOL` (no prefix)
- In `prj.conf`: use `CONFIG_MY_SYMBOL=y` (with prefix)
- In C code: use `CONFIG_MY_SYMBOL`

#### Namespacing

Use consistent prefixes to avoid collisions:

| Context | Prefix Pattern | Example |
|---------|---------------|---------|
| Driver | `<DRIVER_NAME>_` | `LIS2DH_TRIGGER` |
| Subsystem | `<SUBSYS>_` | `BT_L2CAP_MTU` |
| Application | `APP_` | `APP_MAX_USERS` |
| Board | `BOARD_` | `BOARD_HAS_USB` |
| SoC | `SOC_` | `SOC_FLASH_SIZE` |

---

### 3. File Locations

#### Applications

| File | Purpose |
|------|---------|
| `prj.conf` | Main configuration |
| `boards/<board>.conf` | Board-specific overrides |
| `Kconfig` | App-specific symbol definitions |

#### Modules / Drivers

| File | Purpose |
|------|---------|
| `Kconfig` | Symbol definitions |
| `zephyr/module.yml` | Module metadata |

**module.yml example:**
```yaml
build:
  cmake: .
  kconfig: Kconfig
```

#### Subsystems

```
subsys/my_subsystem/
├── Kconfig           # Main subsystem Kconfig
├── Kconfig.feature_a # Optional: split large configs
└── CMakeLists.txt
```

---

### 4. Dependencies (`depends on` vs `select`)

#### depends on (PREFERRED)

Use for hardware requirements or upstream subsystems.

```kconfig
config MY_DRIVER
    bool "My Driver"
    depends on SPI
    depends on GPIO
```

**Behavior:** Symbol invisible and disabled if dependency unmet.

#### select (USE SPARINGLY)

Forces another symbol on. Bypasses dependency checking.

```kconfig
config MY_SUBSYSTEM
    bool "My Subsystem"
    select RING_BUFFER
    select POLL
```

**Risks:**
- Ignores `depends on` of selected symbol
- Can cause "unmet dependencies" warnings
- Hard to debug dependency chains

**When to use:** Only for strictly required, hidden implementation details.

#### imply (PREFERRED over select)

Suggests a default, but user can override.

```kconfig
config MY_FEATURE
    bool "My Feature"
    imply LOG
    imply SENSOR
```

**Use when:** Dependency is "nice to have" but not strictly required.

#### Decision Table

| Situation | Use |
|-----------|-----|
| Hardware requirement | `depends on` |
| Required internal detail | `select` |
| Optional enhancement | `imply` |
| User should decide | `depends on` |

---

### 5. Description Guidelines

#### Prompt (visible in menuconfig)

Keep short and descriptive:
```kconfig
bool "Enable Foo Feature"
int "Maximum buffer size"
```

#### Help Text

Indented, explains what and why:
```kconfig
config FOO_ENABLE
    bool "Enable Foo Feature"
    help
      Enables the Foo feature which provides XYZ functionality.
      Enable this if your application needs to do ABC.

      Requires approximately 2KB of RAM when enabled.
```

---

### 6. Modules & Drivers

#### Directory Structure

```
my_module/
├── zephyr/
│   └── module.yml      # Required for Zephyr integration
├── Kconfig             # Symbol definitions
├── CMakeLists.txt      # Build rules
├── include/
│   └── my_module.h
└── src/
    └── my_module.c
```

#### module.yml

```yaml
build:
  cmake: .
  kconfig: Kconfig
```

#### Driver Kconfig Pattern

```kconfig
config MY_DRIVER
    bool "My Driver"
    default y
    depends on DT_HAS_VENDOR_MY_DEVICE_ENABLED
    select I2C if DT_ANY_INST_HAS_PROP_STATUS_OKAY(vendor_my_device_i2c)
    help
      Enable driver for Vendor My Device.

if MY_DRIVER

config MY_DRIVER_INIT_PRIORITY
    int "Init priority"
    default 80
    help
      Device driver initialization priority.

endif # MY_DRIVER
```

#### Registering Module with West

In workspace `west.yml`:
```yaml
manifest:
  projects:
    - name: my_module
      path: modules/my_module
      url: https://github.com/...
```

Or add path to `ZEPHYR_EXTRA_MODULES` in CMake.

---

### 7. Overlay Configurations

#### Board-Specific Overlays

```
my_app/
├── prj.conf                    # Base configuration
├── boards/
│   ├── nrf52840dk_nrf52840.conf  # nRF52840 DK specific
│   └── nucleo_f401re.conf        # Nucleo F401RE specific
```

#### Build Type Overlays

```
my_app/
├── prj.conf              # Default
├── prj_debug.conf        # Debug build
└── prj_release.conf      # Release build
```

**Usage:**
```bash
west build -b nrf52840dk_nrf52840 -- -DCONF_FILE="prj_release.conf"
```

#### Multiple Overlay Files

```bash
west build -- -DEXTRA_CONF_FILE="overlay1.conf;overlay2.conf"
```

---

### 8. Anti-Patterns

#### DON'T: Overuse select

```kconfig
# BAD: Forces complex dependency chain
config MY_FEATURE
    select NETWORKING
    select WIFI
    select DNS_RESOLVER
```

**Better:**
```kconfig
config MY_FEATURE
    depends on NETWORKING && WIFI
    imply DNS_RESOLVER
```

#### DON'T: Circular Dependencies

```kconfig
# BAD: Creates cycle
config A
    depends on B

config B
    depends on A
```

#### DON'T: Skip help text

```kconfig
# BAD: No context for user
config MAGIC_NUMBER
    int
    default 42
```

**Better:**
```kconfig
config MAGIC_NUMBER
    int "Protocol version number"
    default 42
    help
      The protocol version to use. Must match server.
```

#### DON'T: Duplicate CONFIG_ prefix

```kconfig
# BAD: Will become CONFIG_CONFIG_FOO
config CONFIG_FOO
    bool
```

## Debugging

### Common Errors and Solutions

#### 1. Unmet Direct Dependencies

**Error:**
```
warning: FOO (defined at drivers/Kconfig:10) has direct dependency BAR
but BAR is not currently enabled
```

**Cause:** Symbol `FOO` has `depends on BAR`, but `BAR` is not enabled.

**Fix:**
1. Enable the dependency first: `CONFIG_BAR=y`
2. Or check why `BAR` itself cannot be enabled (chain dependencies)

**Debug command:**
```bash
west build -t menuconfig
# Search for FOO, check its dependencies
```

#### 2. Symbol Not Visible

**Symptom:** Setting `CONFIG_FOO=y` in `prj.conf` has no effect or produces warning.

**Causes:**
- `depends on` condition is false
- Symbol is defined but Kconfig file not sourced
- Wrong architecture/SoC/board

**Debug:**
```bash
# Check if symbol exists and its state
west build -t menuconfig
# Press / to search for the symbol
```

**Fix:**
1. Trace `depends on` chain — enable all required dependencies
2. Check `build/zephyr/Kconfig.modules` for module inclusion
3. Verify `zephyr/module.yml` points to correct Kconfig path

#### 3. Unknown Symbol Warning

**Error:**
```
warning: attempt to assign nonexistent symbol FOO
```

**Causes:**
- Typo in symbol name
- Missing `CONFIG_` prefix in prj.conf (or extra prefix in Kconfig)
- Kconfig file not sourced by build system

**Fix:**
1. Verify exact symbol name in Kconfig definition
2. In `Kconfig`: `config FOO` (no prefix)
3. In `prj.conf`: `CONFIG_FOO=y` (with prefix)
4. For modules, check `zephyr/module.yml` has correct Kconfig path

#### 4. Select Bypassing Dependencies

**Error:**
```
warning: FOO selects BAR, which has unmet direct dependencies (QWERTY)
```

**Cause:** `select` forces a symbol on without checking its `depends on`.

**Fix:**
1. Add the same dependencies to the selecting symbol:
   ```kconfig
   config FOO
       bool "Foo"
       depends on QWERTY  # Add same deps as BAR
       select BAR
   ```
2. Or use `imply` instead of `select` if optional:
   ```kconfig
   config FOO
       bool "Foo"
       imply BAR
   ```

#### 5. Recursive Dependency Detected

**Error:**
```
Kconfig:XX: error: recursive dependency detected!
```

**Cause:** Circular reference in `depends on` or `select` chains.

**Debug:**
The error message shows the cycle. Example:
```
FOO -> BAR -> BAZ -> FOO
```

**Fix:**
1. Break the cycle by removing one dependency
2. Restructure to use `imply` instead of `select`
3. Introduce an intermediate symbol

#### 6. Symbol Value Unexpectedly Changed

**Symptom:** `CONFIG_FOO` is `n` even though you set it to `y`.

**Causes:**
- Another config file overrides it (board, SoC, or defconfig)
- `default n if SOME_CONDITION` applies
- `depends on` became false

**Debug:**
```bash
# Check final resolved value
grep CONFIG_FOO build/zephyr/.config

# Check where it's set
grep -r "CONFIG_FOO" boards/ soc/
```

---

### Debugging Techniques

#### 1. Search in Menuconfig

```bash
west build -t menuconfig
# Press / to search
# Shows: defined at, depends on, selected by, implied by
```

#### 2. Check Final Configuration

```bash
# View resolved .config
cat build/zephyr/.config | grep FOO
```

#### 3. Trace Module Inclusion

```bash
# Check if module Kconfig is sourced
cat build/zephyr/Kconfig.modules
```

#### 4. Verbose Build Output

```bash
west build -v 2>&1 | grep -i kconfig
```

#### 5. Compare Configurations

```bash
# Save current config
cp build/zephyr/.config config_before

# Make changes, rebuild
west build

# Compare
diff config_before build/zephyr/.config
```

---

### Configuration Precedence

Lower entries override higher ones:

1. Kconfig `default` values
2. Board defconfig (`boards/<board>/<board>_defconfig`)
3. SoC Kconfig defaults
4. Application `prj.conf`
5. Board-specific overlay (`boards/<board>.conf`)
6. CMake `-DCONFIG_*` flags
7. `menuconfig` changes (saved to `build/zephyr/.config`)

---

### Quick Checklist

| Issue | Check |
|-------|-------|
| Symbol not found | Spelling, CONFIG_ prefix, module.yml |
| Symbol not visible | `depends on` chain |
| Value ignored | Override in defconfig or board.conf |
| Build warning | Read full message, trace dependencies |
| Unexpected default | Check conditional `default` statements |

## Examples

Complete patterns for common Zephyr Kconfig scenarios.

### Table of Contents

1. [Simple Driver Toggle](#simple-driver-toggle)
2. [Driver with Parameters](#driver-with-parameters)
3. [Subsystem with Feature Flags](#subsystem-with-feature-flags)
4. [Mutually Exclusive Choices](#mutually-exclusive-choices)
5. [Module Integration](#module-integration)
6. [Board-Specific Configuration](#board-specific-configuration)
7. [Application with App-Specific Options](#application-with-app-specific-options)

---

### Simple Driver Toggle

Enable/disable a driver with hardware dependency.

```kconfig
# drivers/sensor/my_sensor/Kconfig

config MY_SENSOR
    bool "My Sensor Driver"
    default y
    depends on I2C
    help
      Enable driver for My Sensor over I2C bus.
```

**prj.conf:**
```
CONFIG_I2C=y
CONFIG_MY_SENSOR=y
```

---

### Driver with Parameters

Driver with configurable parameters.

```kconfig
# drivers/sensor/my_sensor/Kconfig

menuconfig MY_SENSOR
    bool "My Sensor Driver"
    depends on I2C
    help
      Enable My Sensor driver.

if MY_SENSOR

config MY_SENSOR_POLL_INTERVAL_MS
    int "Polling interval in milliseconds"
    default 100
    range 10 10000
    help
      How often to poll the sensor.

config MY_SENSOR_TRIGGER
    bool "Enable trigger mode"
    depends on GPIO
    help
      Use GPIO interrupt instead of polling.

config MY_SENSOR_I2C_ADDR
    hex "I2C address"
    default 0x48
    help
      I2C slave address of the sensor.

endif # MY_SENSOR
```

**prj.conf:**
```
CONFIG_I2C=y
CONFIG_MY_SENSOR=y
CONFIG_MY_SENSOR_POLL_INTERVAL_MS=50
CONFIG_MY_SENSOR_TRIGGER=y
```

---

### Subsystem with Feature Flags

Subsystem with optional features.

```kconfig
# subsys/my_subsystem/Kconfig

menuconfig MY_SUBSYSTEM
    bool "My Subsystem"
    help
      Enable My Subsystem.

if MY_SUBSYSTEM

config MY_SUBSYSTEM_FEATURE_A
    bool "Enable Feature A"
    default y
    help
      Feature A provides...

config MY_SUBSYSTEM_FEATURE_B
    bool "Enable Feature B"
    select REQUIRES_HEAP
    help
      Feature B requires heap allocation.

config MY_SUBSYSTEM_LOG_LEVEL
    int "Log level (0=off, 4=debug)"
    default 3
    range 0 4
    depends on LOG
    help
      Set logging verbosity.

endif # MY_SUBSYSTEM
```

---

### Mutually Exclusive Choices

Select one backend from multiple options.

```kconfig
# subsys/storage/Kconfig

menuconfig STORAGE
    bool "Storage subsystem"

if STORAGE

choice STORAGE_BACKEND
    prompt "Storage backend"
    default STORAGE_FLASH

config STORAGE_FLASH
    bool "Flash storage"
    depends on FLASH
    help
      Use internal flash for storage.

config STORAGE_EEPROM
    bool "EEPROM storage"
    depends on EEPROM
    help
      Use external EEPROM.

config STORAGE_SD
    bool "SD Card storage"
    depends on DISK_ACCESS
    help
      Use SD card via FAT filesystem.

endchoice

endif # STORAGE
```

**prj.conf:**
```
CONFIG_STORAGE=y
CONFIG_STORAGE_SD=y
```

---

### Module Integration

External module with Zephyr integration.

**Directory structure:**
```
my_module/
├── zephyr/
│   └── module.yml
├── Kconfig
├── CMakeLists.txt
└── src/
    └── my_module.c
```

**zephyr/module.yml:**
```yaml
build:
  cmake: .
  kconfig: Kconfig
```

**Kconfig:**
```kconfig
config MY_MODULE
    bool "My External Module"
    help
      Enable my external module functionality.

if MY_MODULE

config MY_MODULE_BUFFER_SIZE
    int "Buffer size"
    default 256

endif # MY_MODULE
```

**CMakeLists.txt:**
```cmake
if(CONFIG_MY_MODULE)
  zephyr_library()
  zephyr_library_sources(src/my_module.c)
endif()
```

---

### Board-Specific Configuration

Different defaults per board.

```kconfig
config MY_DRIVER_DMA
    bool "Use DMA for transfers"
    default y if SOC_SERIES_STM32F4X
    default y if SOC_SERIES_NRF52X
    default n
    depends on DMA
    help
      Enable DMA acceleration. Enabled by default on
      capable SoCs.

config MY_DRIVER_BUFFER_SIZE
    int "Transfer buffer size"
    default 4096 if SOC_SERIES_STM32F4X
    default 1024 if SOC_SERIES_NRF52X
    default 512
    help
      Size varies based on available RAM.
```

---

### Application with App-Specific Options

Application-level Kconfig.

**app/Kconfig:**
```kconfig
mainmenu "My Application Configuration"

config APP_VERSION_MAJOR
    int "Major version"
    default 1

config APP_VERSION_MINOR
    int "Minor version"
    default 0

config APP_DEVICE_NAME
    string "Device name"
    default "MyDevice"

config APP_FEATURE_ADVANCED
    bool "Enable advanced features"
    default n
    select LOG
    select SENSOR
    help
      Enables advanced features that require
      logging and sensor subsystems.

menu "Network Settings"
    visible if NETWORKING

config APP_SERVER_HOST
    string "Server hostname"
    default "api.example.com"

config APP_SERVER_PORT
    int "Server port"
    default 8080
    range 1 65535

endmenu

source "Kconfig.zephyr"
```

**prj.conf:**
```
CONFIG_APP_DEVICE_NAME="ProductionUnit"
CONFIG_APP_FEATURE_ADVANCED=y
CONFIG_APP_SERVER_PORT=443
```

---

### Using in C Code

Access configured values:

```c
#include <zephyr/kernel.h>

/* Boolean check */
#if defined(CONFIG_MY_FEATURE)
    /* Feature enabled */
#endif

/* Integer value */
#define BUFFER_SIZE CONFIG_MY_BUFFER_SIZE

/* String value */
const char *name = CONFIG_APP_DEVICE_NAME;

/* Conditional compilation */
#ifdef CONFIG_MY_SENSOR_TRIGGER
static void sensor_trigger_handler(void) { ... }
#endif
```

## Functions

Zephyr extends Kconfig with functions for build-time evaluation, particularly for devicetree integration.

### Devicetree Functions

These functions query devicetree properties at Kconfig evaluation time.

#### dt_chosen_enabled

Check if a chosen node is enabled.

```kconfig
config MY_FEATURE
    bool "My Feature"
    default y if $(dt_chosen_enabled,zephyr,console)
```

**Returns:** `y` if the chosen node exists and has `status = "okay"`, empty otherwise.

#### dt_nodelabel_enabled

Check if a node with given label is enabled.

```kconfig
config USE_EXTERNAL_FLASH
    bool "Use external flash"
    default y if $(dt_nodelabel_enabled,mx25r64)
```

#### dt_node_has_prop

Check if a node has a specific property.

```kconfig
config HAS_CUSTOM_MAC
    bool
    default y if $(dt_node_has_prop,/soc/ethernet,local-mac-address)
```

#### dt_compat_enabled

Check if any node with compatible string is enabled.

```kconfig
config SENSOR_BME280
    bool "BME280 support"
    default y if $(dt_compat_enabled,bosch$(COMMA)bme280)
```

**Note:** Use `$(COMMA)` for commas in compatible strings.

#### dt_alias_enabled

Check if an alias points to an enabled node.

```kconfig
config HAS_LED0
    bool
    default y if $(dt_alias_enabled,led0)
```

---

### String Functions

#### dt_chosen_label

Get the label of a chosen node.

```kconfig
# Mostly used in visible symbols for display
```

#### dt_node_str_prop_equals

Check if a string property equals a value.

```kconfig
config IS_UART_BACKEND
    bool
    default y if $(dt_node_str_prop_equals,/chosen/zephyr$(COMMA)console,compatible,ns16550)
```

---

### Common Patterns

#### Conditional Default Based on Hardware

```kconfig
config SPI_FLASH_DRIVER
    bool "SPI Flash driver"
    default y if $(dt_compat_enabled,jedec$(COMMA)spi-nor)
    help
      Auto-enabled when compatible SPI NOR flash is in devicetree.
```

#### Feature Auto-Enable

```kconfig
config USB_DEVICE_STACK
    bool "USB Device Stack"
    default y if $(dt_chosen_enabled,zephyr,usb-device)
```

#### Board-Specific Hardware Detection

```kconfig
config DISPLAY_DRIVER
    bool "Display driver"
    default y if $(dt_nodelabel_enabled,display0)
    depends on DISPLAY
```

---

### Function Reference

| Function | Purpose | Example |
|----------|---------|---------|
| `dt_chosen_enabled` | Chosen node enabled? | `$(dt_chosen_enabled,zephyr,console)` |
| `dt_nodelabel_enabled` | Node label enabled? | `$(dt_nodelabel_enabled,uart0)` |
| `dt_compat_enabled` | Compatible enabled? | `$(dt_compat_enabled,nordic$(COMMA)nrf-uart)` |
| `dt_alias_enabled` | Alias target enabled? | `$(dt_alias_enabled,led0)` |
| `dt_node_has_prop` | Node has property? | `$(dt_node_has_prop,/soc,interrupt-parent)` |

---

### Special Variables

| Variable | Meaning |
|----------|---------|
| `$(COMMA)` | Literal comma (for compatible strings) |
| `$(ZEPHYR_BASE)` | Path to Zephyr root |
| `$(BOARD)` | Current board name |
| `$(ARCH)` | Current architecture |

---

### Debugging Function Results

Functions are evaluated during CMake configuration. To see results:

```bash
# Check generated Kconfig.modules
cat build/zephyr/Kconfig.modules

# Run menuconfig and search for symbol
west build -t menuconfig
# The "Depends on" will show evaluated function results
```

## Menuconfig

Interactive tool for exploring and modifying Kconfig symbols.

### Launching Menuconfig

```bash
# After initial build
west build -t menuconfig

# GUI version (requires Tk)
west build -t guiconfig
```

**Note:** Run `west build` at least once before using menuconfig.

---

### Navigation

#### Keyboard Shortcuts

| Key | Action |
|-----|--------|
| `↑` / `↓` | Navigate items |
| `Enter` | Enter submenu or toggle bool |
| `Esc` (2x) | Go back / Exit |
| `Space` | Toggle bool / Select choice |
| `?` | Show help for current item |
| `/` | **Search** (most useful!) |
| `Y` | Set bool to yes |
| `N` | Set bool to no |
| `M` | Set tristate to module (rarely used in Zephyr) |

#### Search (`/`)

The most powerful feature for debugging:

1. Press `/`
2. Type symbol name (without `CONFIG_` prefix)
3. Results show:
   - **Defined at**: Source file and line
   - **Depends on**: What must be enabled
   - **Selected by**: What forces this on
   - **Implied by**: What suggests this
   - **Current value**: y/n/value

**Example search result:**
```
Symbol: LOG
Type  : bool
Prompt: Logging
  Location:
    -> Subsystems and OS Services
  Defined at subsys/logging/Kconfig:1
  Depends on: PRINTK
  Selected by: SHELL [=y]
```

---

### Common Tasks

#### Find Why a Symbol is Disabled

1. Press `/`, search for symbol
2. Check "Depends on" — all must be satisfied
3. Trace each dependency recursively

#### Find What Enables a Symbol

1. Search for the symbol
2. Check "Selected by" and "Implied by"
3. One of these is forcing the value

#### Change a Value

1. Navigate to the symbol
2. For bool: press `Y` or `N`
3. For int/hex/string: press `Enter`, type value

#### Save Changes

1. Press `Esc` twice to exit
2. Select "Yes" to save when prompted
3. Changes saved to `build/zephyr/.config`

**Note:** Changes are lost on clean rebuild. Copy to `prj.conf` for persistence:
```bash
grep CONFIG_MY_SETTING build/zephyr/.config >> prj.conf
```

---

### Guiconfig Features

The GUI version (`west build -t guiconfig`) adds:

- Tree view with expand/collapse
- Split view showing symbol info
- Regex search
- Show all symbols (including hidden)
- Jump to definition

---

### Workflow Integration

#### Discovery Workflow
1. Build once: `west build -b <board> .`
2. Launch: `west build -t menuconfig`
3. Search and explore symbols
4. Note required settings
5. Add to `prj.conf` for permanence

#### Debugging Workflow
1. Encounter Kconfig error
2. Launch menuconfig
3. Search for problematic symbol
4. Trace "Depends on" chain
5. Enable missing dependencies in `prj.conf`

---

### Tips

- **Hidden symbols**: Some symbols have no prompt (internal use). Search can still find them.
- **Gray items**: Dependencies not met. Check what they depend on.
- **[*] vs < >**: `[*]` is selected, `< >` is available but not selected.
- **-->**: Indicates a submenu.

## Syntax

Detailed Kconfig language syntax for Zephyr OS configuration symbols.

### Table of Contents

1. [Basic Symbol Definition](#basic-symbol-definition)
2. [Properties](#properties)
3. [Menus and Organization](#menus-and-organization)
4. [Choices](#choices)
5. [Sourcing Files](#sourcing-files)
6. [Conditionals](#conditionals)
7. [Hidden Symbols](#hidden-symbols)
8. [Expressions](#expressions)

---

### Basic Symbol Definition

Symbols are defined in `Kconfig` files.

```kconfig
config MY_SYMBOL
    bool "Description of my symbol"
    default y
    depends on OTHER_SYMBOL
    help
      Detailed help text goes here.
      It can span multiple lines (indented).
```

#### Types

| Type | Values | Example |
|------|--------|---------|
| `bool` | `y` / `n` | `bool "Enable feature"` |
| `int` | Integer | `int "Buffer size"` |
| `hex` | Hexadecimal | `hex "Base address"` |
| `string` | Text | `string "Device name"` |

---

### Properties

#### default

Sets initial value. Can be conditional. Multiple defaults evaluated top-to-bottom.

```kconfig
default y if DEBUG
default n
```

#### depends on

Sets visibility AND validity. If unmet, symbol is invisible and forced to `n`.

```kconfig
depends on GPIO && I2C
depends on (ARCH_HAS_FOO || ARCH_HAS_BAR)
```

#### select

Reverse dependency. Forces target to `y` when this symbol is `y`.

```kconfig
select SERIAL
select RING_BUFFER if ASYNC_MODE
```

**Warning:** Ignores `depends on` of selected symbol. Use sparingly.

#### imply

Weak reverse dependency. Sets target default to `y`, but user can override.

```kconfig
imply LOG
imply SENSOR_TRIGGER if GPIO
```

**Preferred** over `select` when dependency is optional.

#### range

Limits valid values for `int` / `hex`.

```kconfig
range 1 100
range 0x1000 0xFFFF
range 10 1000 if HIGH_MEMORY
```

#### visible if

Controls visibility without affecting value. Different from `depends on`.

```kconfig
config INTERNAL_BUFFER
    int "Buffer size"
    visible if ADVANCED_OPTIONS
    default 256
```

---

### Menus and Organization

#### menu / endmenu

Group related symbols visually.

```kconfig
menu "My Subsystem Configuration"

config MY_SUBSYSTEM_ENABLE
    bool "Enable My Subsystem"

endmenu
```

#### menuconfig

Collapsible menu that is also a symbol.

```kconfig
menuconfig MY_SUBSYSTEM
    bool "My Subsystem"

if MY_SUBSYSTEM

config MY_PARAM
    int "Parameter"
    default 10

endif # MY_SUBSYSTEM
```

#### comment

Display-only text in menus.

```kconfig
comment "Advanced options below"
    depends on EXPERT_MODE
```

---

### Choices

Mutually exclusive options.

```kconfig
choice PROTOCOL_TYPE
    prompt "Select Protocol"
    default PROTOCOL_A

config PROTOCOL_A
    bool "Protocol A"

config PROTOCOL_B
    bool "Protocol B"

endchoice
```

#### Optional Choice

```kconfig
choice
    prompt "Optional backend"
    optional

config BACKEND_X
    bool "Backend X"

endchoice
```

---

### Sourcing Files

#### source

Include other Kconfig files. File must exist.

```kconfig
source "drivers/sensor/my_sensor/Kconfig"
```

#### rsource

Relative source (relative to current file).

```kconfig
rsource "subdir/Kconfig"
```

#### osource

Optional source. No error if file doesn't exist.

```kconfig
osource "Kconfig.local"
```

#### orsource

Optional relative source.

```kconfig
orsource "optional/Kconfig"
```

**Paths:** Relative to `ZEPHYR_BASE`, module root, or current file (rsource).

---

### Conditionals

#### if / endif

Wrap multiple symbols in a condition.

```kconfig
if SOC_NRF52832

config MY_NRF_FEATURE
    bool "Feature for nRF52"

config NRF_BUFFER_SIZE
    int "Buffer size"
    default 512

endif # SOC_NRF52832
```

---

### Hidden Symbols

Symbols without a prompt are "hidden" — not visible in menuconfig but can be selected or depend on.

```kconfig
config HAS_HARDWARE_FPU
    bool
    default y if CPU_HAS_FPU

config MY_MATH_LIB
    bool "Math library"
    select HAS_HARDWARE_FPU
```

#### Common Hidden Pattern

```kconfig
# Hidden capability symbol
config HAS_SPI_ASYNC
    bool

# Visible feature depending on capability
config USE_SPI_ASYNC
    bool "Use async SPI"
    depends on HAS_SPI_ASYNC
```

---

### Expressions

#### Operators

| Operator | Meaning |
|----------|---------|
| `&&` | AND |
| `\|\|` | OR |
| `!` | NOT |
| `=` | Equals |
| `!=` | Not equals |
| `<`, `>`, `<=`, `>=` | Comparison (int/hex) |
| `()` | Grouping |

#### Examples

```kconfig
depends on (GPIO && I2C) || SPI
depends on BUFFER_SIZE > 256
depends on !LEGACY_MODE
default y if SOC_FAMILY = "nordic"
```

#### String Comparison

```kconfig
default y if BOARD = "nrf52840dk_nrf52840"
```
