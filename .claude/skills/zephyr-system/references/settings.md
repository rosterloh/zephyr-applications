# Settings Subsystem

## Overview

### Quick Start

1. **Choose Backend**: See [#backend_comparison](#backend_comparison)
2. **Define Handler**: Use `SETTINGS_STATIC_HANDLER_DEFINE` or `settings_handler` struct
3. **Initialize**: Call `settings_subsys_init()` → `settings_register()` → `settings_load()`
4. **Save Changes**: Use `settings_save_one()` or `settings_save()`

### Core Concepts

- **Keys**: Hierarchical strings (e.g., `id/serial`, `wifi/ssid`)
- **Handlers**: Implement `h_set`, `h_get`, `h_export`, `h_commit` for your subtree
- **Backends**: Storage implementations (NVS, ZMS, FCB, File) - as of Zephyr 4.4, **ZMS is the preferred modern backend** (chosen first in `subsys/settings/Kconfig` defaults when both are enabled), and **NVS remains the stable, widely-used choice** for classical NOR flash. Avoid FCB and File backends for new designs.

### Handler Commit Priority

When multiple handlers depend on each other during initialization, use commit priority (`cprio`):
- Lower values = Higher priority (executed first during commit)
- Default priority: 0
- Use `settings_register_with_cprio()` for dynamic handlers
- Use `SETTINGS_STATIC_HANDLER_DEFINE_WITH_CPRIO()` for static handlers

**Example**: A network service that other handlers depend on should have higher priority (lower cprio value).

### Handler Function Quick Reference

| Function | Called When | Return Value | Required For |
|----------|-------------|--------------|--------------|
| `h_set` | Loading from storage or `runtime_set` | 0 on success | Loading values |
| `h_get` | Runtime get (`CONFIG_SETTINGS_RUNTIME=y`) | Length on success | Runtime access |
| `h_export` | Saving all settings | 0 on success | Persistence |
| `h_commit` | After all settings loaded | 0 on success | Validation/init |

### Multiple Storage Sources

Settings supports loading from multiple sources but saves to a single destination:
- Multiple **source** backends: Load settings from all registered sources
- Single **destination** backend: All saves go to one location

**Use case**: Factory defaults in read-only flash + user overrides in NVS.

### References

- **Backend Selection**: [#backend_comparison](#backend_comparison) - Choose the right storage backend
- **API Reference**: [#api_reference](#api_reference) - Function signatures and structures
- **Examples**: [#examples](#examples) - Implementation patterns and integrations
- **Troubleshooting**: [#troubleshooting](#troubleshooting) - Debugging common issues
- **Locations**: [#locations](#locations) - Source code and documentation paths

### Kconfig Requirements

Ensure the following are enabled in `prj.conf`:
- `CONFIG_SETTINGS=y`
- One or more backends: `CONFIG_SETTINGS_NVS=y`, `CONFIG_SETTINGS_ZMS=y`, `CONFIG_SETTINGS_FILE=y`, or `CONFIG_SETTINGS_FCB=y`
- `CONFIG_SETTINGS_RUNTIME=y` if using runtime API
- `CONFIG_SETTINGS_DYNAMIC_HANDLERS=y` if registering handlers at runtime

### Related Skills

This skill works well with:
- **zephyr-kconfig**: Configure `CONFIG_SETTINGS_*` options
- **zephyr-devicetree**: Define storage partitions and flash regions
- **zephyr-shell-commands**: Add runtime settings CLI

## Api Reference

### Core API

#### Initialization
- `int settings_subsys_init(void)`: Initialize the settings subsystem and backends. Call after FS is mounted if using a file backend.

#### Handler Registration

##### Dynamic Registration
- `int settings_register(struct settings_handler *cf)`: Register a dynamic settings handler with default commit priority (0).
- `int settings_register_with_cprio(struct settings_handler *cf, int cprio)`: Register a handler with explicit commit priority.

##### Static Registration
- `SETTINGS_STATIC_HANDLER_DEFINE(name, tree, get, set, commit, export)`: Define a static settings handler with default priority.
- `SETTINGS_STATIC_HANDLER_DEFINE_WITH_CPRIO(name, tree, cprio, get, set, commit, export)`: Define a static handler with explicit commit priority.

**Commit Priority (`cprio`):**
- Lower values = Higher priority (executed first during commit)
- Default: 0
- Use negative values for handlers that others depend on
- Use positive values for handlers that depend on others

#### Loading Settings
- `int settings_load(void)`: Load all registered settings from persistent storage.
- `int settings_load_subtree(const char *subtree)`: Load a specific subtree.
- `ssize_t settings_load_one(const char *name, void *buf, size_t buf_len)`: Load a single setting into a buffer.
- `int settings_load_subtree_direct(const char *subtree, settings_load_direct_cb cb, void *param)`: Load a subtree using a custom callback, bypassing registered handlers.

#### Saving Settings
- `int settings_save(void)`: Save all currently running settings that differ from persisted values.
- `int settings_save_subtree(const char *subtree)`: Save a specific subtree.
- `int settings_save_one(const char *name, const void *value, size_t val_len)`: Write a single value to storage.
- `int settings_delete(const char *name)`: Delete a single setting from storage (sets value to NULL).

#### Value Inspection
- `ssize_t settings_get_val_len(const char *name)`: Get the data length of a stored value without loading it.

**Use case:** Dynamic memory allocation for variable-length settings:
```c
ssize_t len = settings_get_val_len("my_app/config");
if (len > 0) {
    char *buf = k_malloc(len);
    if (buf) {
        settings_load_one("my_app/config", buf, len);
        /* Use buffer... */
        k_free(buf);
    }
}
```

#### Runtime API (requires CONFIG_SETTINGS_RUNTIME)
- `int settings_runtime_set(const char *name, const void *data, size_t len)`: Inject a value into a handler in RAM.
- `int settings_runtime_get(const char *name, void *data, size_t len)`: Retrieve a value from a handler in RAM.

#### Backend Registration (for custom backends)
- `void settings_src_register(struct settings_store *cs)`: Register a storage source (read from).
- `void settings_dst_register(struct settings_store *cs)`: Register a storage destination (write to).

---

### Structures and Types

#### settings_handler
```c
struct settings_handler {
    const char *name;      /* Name of subtree (e.g., "my_app") */
    int cprio;             /* Commit priority (lower value = higher priority) */
    int (*h_get)(const char *key, char *val, int val_len_max);
    int (*h_set)(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg);
    int (*h_commit)(void);
    int (*h_export)(int (*export_func)(const char *name, const void *val, size_t val_len));
};
```

#### settings_read_cb
```c
typedef ssize_t (*settings_read_cb)(void *cb_arg, void *data, size_t len);
```
Used within `h_set` to read the actual data from the backend.

#### settings_store (for custom backends)
```c
struct settings_store {
    sys_snode_t cs_next;
    const struct settings_store_itf *cs_itf;
};

struct settings_store_itf {
    int (*csi_load)(struct settings_store *cs, const struct settings_load_arg *arg);
    int (*csi_save)(struct settings_store *cs, const char *name,
                    const char *value, size_t val_len);
};
```

---

### Key Name Processing
- `int settings_name_steq(const char *name, const char *key, const char **next)`: Compare start of name with key. Returns 1 if match.
- `int settings_name_next(const char *name, const char **next)`: Find number of characters before the first separator (`/`).

---

### Return Values

| Value | Meaning |
|-------|---------|
| `0` | Success |
| `-EINVAL` | Invalid argument (size mismatch, bad params) |
| `-ENOENT` | Key not found |
| `-ENOSPC` | No space (flash full) |
| `-EIO` | I/O error |
| `-ENOTSUP` | Feature not enabled |
| `-ENODEV` | Backend not initialized |

## Backend Comparison

This guide helps you choose the right storage backend for your Zephyr Settings implementation.

### Backend Comparison Table

| Backend | Best For | Flash Wear | Speed | Memory | Status |
|---------|----------|------------|-------|--------|--------|
| **NVS** | General purpose | Low | Fast | Low | **Recommended** (Zephyr 4.1+) |
| **ZMS** | Memory-optimized | Low | Fast | Lower | **Recommended** (Zephyr 4.1+) |
| **FCB** | Legacy systems | Medium | Medium | Medium | Legacy option |
| **File** | Filesystem available | Varies | Slow | High | Requires mounted FS |

> **As of Zephyr 4.1**: NVS and ZMS are the recommended backends for non-filesystem storage.

### When to Use Each Backend

#### NVS (Non-Volatile Storage)
- **Use when**: General-purpose persistent storage on flash
- **Advantages**: Battle-tested, good wear leveling, widely used
- **Disadvantages**: Slightly higher memory than ZMS

#### ZMS (Zephyr Memory Storage)
- **Use when**: Memory is constrained, need efficient storage
- **Advantages**: Lower memory footprint, hash-based lookups
- **Disadvantages**: Potential hash collisions (configurable)

#### FCB (Flash Circular Buffer)
- **Use when**: Maintaining legacy code
- **Advantages**: Simple circular buffer design
- **Disadvantages**: Not recommended for new designs

#### File Backend
- **Use when**: Filesystem is already mounted (LittleFS, FAT, etc.)
- **Advantages**: Human-readable storage, easy debugging
- **Disadvantages**: Slower, requires filesystem overhead

---

### Backend Configuration

#### NVS Backend

```c
#include <zephyr/settings/settings.h>
```

**Kconfig (`prj.conf`):**
```
CONFIG_SETTINGS=y
CONFIG_SETTINGS_NVS=y
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_NVS=y

# Optional: Speed up lookups
CONFIG_NVS_LOOKUP_CACHE=y
CONFIG_NVS_LOOKUP_CACHE_SIZE=128
```

**Devicetree (optional, for non-default partition):**
```dts
/ {
    chosen {
        zephyr,settings-partition = &storage_partition;
    };
};

&flash0 {
    partitions {
        compatible = "fixed-partitions";
        #address-cells = <1>;
        #size-cells = <1>;

        storage_partition: partition@f0000 {
            label = "storage";
            reg = <0x000f0000 0x00010000>;
        };
    };
};
```

---

#### ZMS Backend

```c
#include <zephyr/settings/settings.h>
```

**Kconfig (`prj.conf`):**
```
CONFIG_SETTINGS=y
CONFIG_SETTINGS_ZMS=y
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_ZMS=y

# Handle hash collisions (2^n possible collisions)
CONFIG_SETTINGS_ZMS_MAX_COLLISIONS_BITS=2
```

**Devicetree:** Same as NVS (uses `zephyr,settings-partition`).

---

#### File Backend

```c
#include <zephyr/settings/settings.h>
#include <zephyr/fs/fs.h>
```

**Kconfig (`prj.conf`):**
```
CONFIG_SETTINGS=y
CONFIG_SETTINGS_FILE=y
CONFIG_FILE_SYSTEM=y
CONFIG_FILE_SYSTEM_LITTLEFS=y

# Optional: Custom path (default: /settings/run)
CONFIG_SETTINGS_FILE_PATH="/lfs/settings"
```

**Important**: Filesystem must be mounted BEFORE `settings_subsys_init()`:
```c
int main(void)
{
    /* 1. Mount filesystem first */
    fs_mount(&lfs_mnt);

    /* 2. Then initialize settings */
    settings_subsys_init();
    settings_load();
}
```

---

#### FCB Backend (Legacy)

```c
#include <zephyr/settings/settings.h>
```

**Kconfig (`prj.conf`):**
```
CONFIG_SETTINGS=y
CONFIG_SETTINGS_FCB=y
CONFIG_FLASH=y
CONFIG_FCB=y
```

---

### Custom Backend Implementation

For specialized storage (e.g., external EEPROM, cloud sync):

**Kconfig:**
```
CONFIG_SETTINGS=y
CONFIG_SETTINGS_CUSTOM=y
```

**Implementation:**
```c
#include <zephyr/settings/settings.h>

static int my_backend_load(struct settings_store *cs,
                           const struct settings_load_arg *arg)
{
    /* Load from custom storage */
    /* Call arg->cb for each key-value pair found */
    return 0;
}

static int my_backend_save(struct settings_store *cs, const char *name,
                           const char *value, size_t val_len)
{
    /* Save to custom storage */
    return 0;
}

static struct settings_store_itf my_backend_itf = {
    .csi_load = my_backend_load,
    .csi_save = my_backend_save,
};

static struct settings_store my_backend_store = {
    .cs_itf = &my_backend_itf
};

void my_backend_init(void)
{
    /* Register as both read source and write destination */
    settings_dst_register(&my_backend_store);  /* Write target */
    settings_src_register(&my_backend_store);  /* Read source */
}
```

---

### Advanced Kconfig Options

#### Settings Subsystem
```
CONFIG_SETTINGS=y                          # Enable settings
CONFIG_SETTINGS_RUNTIME=y                  # Enable runtime API
CONFIG_SETTINGS_DYNAMIC_HANDLERS=y         # Allow runtime handler registration
CONFIG_SETTINGS_LOG_LEVEL_DBG=y            # Debug logging
CONFIG_SETTINGS_ENCODE_LEN=y               # Encode data length for integrity
```

#### NVS Backend Tuning
```
CONFIG_NVS=y
CONFIG_NVS_LOOKUP_CACHE=y                  # Speed up lookups
CONFIG_NVS_LOOKUP_CACHE_SIZE=128           # Cache size
```

#### ZMS Backend Tuning
```
CONFIG_ZMS=y
CONFIG_SETTINGS_ZMS_MAX_COLLISIONS_BITS=2  # Collision handling (2^n)
```

---

### Decision Flowchart

```
Is filesystem already required?
├── YES → Use File Backend
└── NO → Is memory extremely constrained?
          ├── YES → Use ZMS
          └── NO → Use NVS (safest default)
```

### Migration Notes

- **FCB → NVS/ZMS**: Settings keys are compatible; only backend configuration changes
- **NVS → ZMS**: Direct migration possible; both use flash partitions
- **File → NVS/ZMS**: May require re-provisioning settings on first boot

## Examples

### Static Handler Definition

```c
#include <zephyr/settings/settings.h>

static uint32_t my_val = 100;

static int my_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
    const char *next;
    if (settings_name_steq(name, "val", &next) && !next) {
        if (len != sizeof(my_val)) {
            return -EINVAL;
        }
        return read_cb(cb_arg, &my_val, sizeof(my_val));
    }
    return -ENOENT;
}

static int my_export(int (*storage_func)(const char *name, const void *value, size_t val_len))
{
    return storage_func("my_app/val", &my_val, sizeof(my_val));
}

SETTINGS_STATIC_HANDLER_DEFINE(my_app, "my_app", NULL, my_set, NULL, my_export);
```

### Basic Usage (Init, Load, Save)

```c
int main(void)
{
    int rc;

    rc = settings_subsys_init();
    if (rc) {
        LOG_ERR("Settings init failed: %d", rc);
        return rc;
    }

    /* Load all settings from storage */
    rc = settings_load();
    if (rc) {
        LOG_WRN("Settings load failed: %d, using defaults", rc);
    }

    /* Modify value and save */
    my_val = 200;
    settings_save_one("my_app/val", &my_val, sizeof(my_val));

    /* Or save all modified values */
    settings_save();

    return 0;
}
```

### Handling Multiple Keys in a Subtree

```c
static int multi_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
    const char *next;
    if (settings_name_steq(name, "key1", &next) && !next) {
        return read_cb(cb_arg, &val1, sizeof(val1));
    } else if (settings_name_steq(name, "key2", &next) && !next) {
        return read_cb(cb_arg, &val2, sizeof(val2));
    }
    return -ENOENT;
}
```

### Runtime Set/Get

```c
/* Inject value into RAM handler */
settings_runtime_set("my_app/val", &new_val, sizeof(new_val));

/* Get value from RAM handler */
settings_runtime_get("my_app/val", &buf, sizeof(buf));
```

---

### Real-World Use Cases

#### Device Calibration Data

```c
#include <zephyr/settings/settings.h>

struct calibration {
    float offset;
    float gain;
    uint32_t timestamp;
};

static struct calibration sensor_cal = {0.0f, 1.0f, 0};

static int cal_set(const char *name, size_t len,
                   settings_read_cb read_cb, void *cb_arg)
{
    const char *next;
    if (settings_name_steq(name, "sensor", &next) && !next) {
        if (len != sizeof(sensor_cal)) {
            return -EINVAL;
        }
        return read_cb(cb_arg, &sensor_cal, sizeof(sensor_cal));
    }
    return -ENOENT;
}

static int cal_export(int (*storage_func)(const char *name,
                      const void *value, size_t val_len))
{
    return storage_func("cal/sensor", &sensor_cal, sizeof(sensor_cal));
}

SETTINGS_STATIC_HANDLER_DEFINE(cal, "cal", NULL, cal_set, NULL, cal_export);
```

#### WiFi Credentials (String Handling)

```c
static char wifi_ssid[33] = "";
static char wifi_pass[64] = "";

static int wifi_set(const char *name, size_t len,
                    settings_read_cb read_cb, void *cb_arg)
{
    const char *next;

    if (settings_name_steq(name, "ssid", &next) && !next) {
        if (len >= sizeof(wifi_ssid)) {
            return -EINVAL;
        }
        int rc = read_cb(cb_arg, wifi_ssid, len);
        wifi_ssid[len] = '\0';  /* Ensure null termination */
        return rc;
    }

    if (settings_name_steq(name, "pass", &next) && !next) {
        if (len >= sizeof(wifi_pass)) {
            return -EINVAL;
        }
        int rc = read_cb(cb_arg, wifi_pass, len);
        wifi_pass[len] = '\0';
        return rc;
    }

    return -ENOENT;
}

static int wifi_export(int (*storage_func)(const char *name,
                       const void *value, size_t val_len))
{
    int rc = storage_func("wifi/ssid", wifi_ssid, strlen(wifi_ssid));
    if (rc) {
        return rc;
    }
    return storage_func("wifi/pass", wifi_pass, strlen(wifi_pass));
}

SETTINGS_STATIC_HANDLER_DEFINE(wifi, "wifi", NULL, wifi_set, NULL, wifi_export);
```

#### Factory Reset Pattern

```c
void factory_reset(void)
{
    /* Delete all application settings */
    settings_delete("wifi/ssid");
    settings_delete("wifi/pass");
    settings_delete("cal/sensor");
    settings_delete("app/config");

    /* Reset to defaults in RAM */
    memset(wifi_ssid, 0, sizeof(wifi_ssid));
    memset(wifi_pass, 0, sizeof(wifi_pass));
    sensor_cal = (struct calibration){0.0f, 1.0f, 0};

    LOG_INF("Factory reset complete");
}
```

---

### Integration Examples

#### Settings + Bluetooth

Store and restore Bluetooth device name:

```c
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/settings/settings.h>

static char bt_name[CONFIG_BT_DEVICE_NAME_MAX + 1] = CONFIG_BT_DEVICE_NAME;

static int bt_settings_set(const char *name, size_t len,
                           settings_read_cb read_cb, void *cb_arg)
{
    const char *next;

    if (settings_name_steq(name, "name", &next) && !next) {
        if (len >= sizeof(bt_name)) {
            return -EINVAL;
        }
        int rc = read_cb(cb_arg, bt_name, len);
        bt_name[len] = '\0';
        return rc;
    }
    return -ENOENT;
}

static int bt_settings_commit(void)
{
    /* Apply the loaded name to Bluetooth stack */
    return bt_set_name(bt_name);
}

static int bt_settings_export(int (*storage_func)(const char *name,
                              const void *value, size_t val_len))
{
    return storage_func("bt_app/name", bt_name, strlen(bt_name));
}

SETTINGS_STATIC_HANDLER_DEFINE(bt_app, "bt_app", NULL,
    bt_settings_set, bt_settings_commit, bt_settings_export);
```

#### Settings + Shell Commands

Runtime settings modification via shell:

```c
#include <zephyr/shell/shell.h>
#include <zephyr/settings/settings.h>
#include <stdlib.h>

static int cmd_settings_set(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 3) {
        shell_error(sh, "Usage: settings_set <key> <value>");
        return -EINVAL;
    }

    uint32_t value = strtoul(argv[2], NULL, 0);
    int rc = settings_save_one(argv[1], &value, sizeof(value));

    if (rc) {
        shell_error(sh, "Failed to save: %d", rc);
    } else {
        shell_print(sh, "Saved %s = %u", argv[1], value);
    }
    return rc;
}

static int cmd_settings_get(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 2) {
        shell_error(sh, "Usage: settings_get <key>");
        return -EINVAL;
    }

    uint32_t value;
    ssize_t len = settings_load_one(argv[1], &value, sizeof(value));

    if (len < 0) {
        shell_error(sh, "Failed to load: %zd", len);
        return len;
    }
    shell_print(sh, "%s = %u", argv[1], value);
    return 0;
}

static int cmd_settings_delete(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 2) {
        shell_error(sh, "Usage: settings_delete <key>");
        return -EINVAL;
    }

    int rc = settings_delete(argv[1]);
    if (rc) {
        shell_error(sh, "Failed to delete: %d", rc);
    } else {
        shell_print(sh, "Deleted %s", argv[1]);
    }
    return rc;
}

SHELL_STATIC_SUBCMD_SET_CREATE(settings_cmds,
    SHELL_CMD(set, NULL, "Set a setting value", cmd_settings_set),
    SHELL_CMD(get, NULL, "Get a setting value", cmd_settings_get),
    SHELL_CMD(delete, NULL, "Delete a setting", cmd_settings_delete),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(settings, &settings_cmds, "Settings commands", NULL);
```

#### Settings + Logging Configuration

Persist log level across reboots:

```c
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/settings/settings.h>

static uint32_t saved_log_level = LOG_LEVEL_INF;

static int log_settings_set(const char *name, size_t len,
                            settings_read_cb read_cb, void *cb_arg)
{
    const char *next;

    if (settings_name_steq(name, "level", &next) && !next) {
        if (len != sizeof(saved_log_level)) {
            return -EINVAL;
        }
        return read_cb(cb_arg, &saved_log_level, sizeof(saved_log_level));
    }
    return -ENOENT;
}

static int log_settings_commit(void)
{
    /* Apply saved log level to all modules */
    uint32_t modules_cnt = log_src_cnt_get(0);

    for (uint32_t i = 0; i < modules_cnt; i++) {
        log_filter_set(NULL, 0, i, saved_log_level);
    }

    LOG_INF("Log level set to %u", saved_log_level);
    return 0;
}

static int log_settings_export(int (*storage_func)(const char *name,
                               const void *value, size_t val_len))
{
    return storage_func("log/level", &saved_log_level, sizeof(saved_log_level));
}

SETTINGS_STATIC_HANDLER_DEFINE(log_cfg, "log", NULL,
    log_settings_set, log_settings_commit, log_settings_export);

/* Call this to change and persist log level */
void set_global_log_level(uint32_t level)
{
    saved_log_level = level;
    log_settings_commit();  /* Apply immediately */
    settings_save_one("log/level", &saved_log_level, sizeof(saved_log_level));
}
```

#### Settings + Devicetree Partition

Reference storage partition from devicetree:

```dts
/* boards/my_board.overlay or app.overlay */
/ {
    chosen {
        zephyr,settings-partition = &storage_partition;
    };
};

&flash0 {
    partitions {
        compatible = "fixed-partitions";
        #address-cells = <1>;
        #size-cells = <1>;

        /* Application code */
        slot0_partition: partition@10000 {
            label = "image-0";
            reg = <0x00010000 0x000e0000>;
        };

        /* Settings storage (64KB) */
        storage_partition: partition@f0000 {
            label = "storage";
            reg = <0x000f0000 0x00010000>;
        };
    };
};
```

---

### Handler with Commit Priority

When handlers depend on each other:

```c
/* Network service - must initialize first (higher priority = lower cprio) */
SETTINGS_STATIC_HANDLER_DEFINE_WITH_CPRIO(net_cfg, "net", -10,
    NULL, net_set, net_commit, net_export);

/* App config - depends on network (lower priority = higher cprio) */
SETTINGS_STATIC_HANDLER_DEFINE_WITH_CPRIO(app_cfg, "app", 10,
    NULL, app_set, app_commit, app_export);
```

Or with dynamic registration:

```c
static struct settings_handler net_handler = {
    .name = "net",
    .h_set = net_set,
    .h_commit = net_commit,
    .h_export = net_export,
};

static struct settings_handler app_handler = {
    .name = "app",
    .h_set = app_set,
    .h_commit = app_commit,
    .h_export = app_export,
};

void init_settings(void)
{
    settings_subsys_init();

    /* Register with explicit priorities */
    settings_register_with_cprio(&net_handler, -10);  /* Higher priority */
    settings_register_with_cprio(&app_handler, 10);   /* Lower priority */

    settings_load();
}
```

## Locations

The following locations are relevant for the settings subsystem in Zephyr OS.
Note: `<zephyr-ws-dir>` refers to the Zephyr workspace root (e.g., `zephyr-ws/deps/zephyr`).

| Description | Location |
|-------------|----------|
| Documentation | `<zephyr-ws-dir>/doc/services/storage/settings/index.rst` |
| C Headers (Subsystem) | `<zephyr-ws-dir>/subsys/settings/include/settings` |
| Public Header | `<zephyr-ws-dir>/include/zephyr/settings/settings.h` |
| Samples | `<zephyr-ws-dir>/samples/subsys/settings` |
| Source Code | `<zephyr-ws-dir>/subsys/settings` |

## Troubleshooting

This guide covers common issues, debugging strategies, and anti-patterns when working with Zephyr Settings.

### Common Issues

#### 1. Settings Not Persisting After Reboot

**Symptoms:** Values reset to defaults after power cycle.

**Causes:**
- Forgot to call `settings_subsys_init()` before `settings_load()`
- Filesystem not mounted (for file backend)
- Flash partition not properly defined in devicetree
- Backend not registered (`CONFIG_SETTINGS_NVS=y` etc.)
- `h_export` not implemented (values can't be saved)

**Solution:**
```c
/* Correct initialization order */
int main(void)
{
    /* 1. Mount FS first (if using file backend) */
    fs_mount(&lfs_mnt);

    /* 2. Initialize settings subsystem */
    int rc = settings_subsys_init();
    if (rc) {
        LOG_ERR("Settings init failed: %d", rc);
    }

    /* 3. Register handlers (if dynamic) */
    settings_register(&my_handler);

    /* 4. Load settings from storage */
    settings_load();

    /* Now settings are available */
}
```

---

#### 2. `h_set` Returns -EINVAL

**Symptoms:** Settings fail to load with `-EINVAL` error.

**Causes:**
- Size mismatch: `len` doesn't match expected variable size
- Incorrect `read_cb` usage
- Corrupted storage data

**Solution:**
```c
static int my_set(const char *name, size_t len,
                  settings_read_cb read_cb, void *cb_arg)
{
    const char *next;

    if (settings_name_steq(name, "val", &next) && !next) {
        /* Always validate size before reading */
        if (len != sizeof(my_val)) {
            LOG_WRN("Size mismatch: expected %zu, got %zu",
                    sizeof(my_val), len);
            return -EINVAL;
        }
        return read_cb(cb_arg, &my_val, sizeof(my_val));
    }
    return -ENOENT;
}
```

---

#### 3. ZMS Backend Collision Errors

**Symptoms:** `-ENOSPC` errors or hash collision warnings in logs.

**Cause:** Too many unique keys for configured collision bits.

**Solution:**
```
# Increase collision handling capacity (2^n possible collisions)
CONFIG_SETTINGS_ZMS_MAX_COLLISIONS_BITS=4
```

---

#### 4. `h_export` Not Being Called

**Symptoms:** `settings_save()` completes but values aren't persisted.

**Cause:** Handler doesn't implement `h_export`, so `settings_save()` can't persist values.

**Solution:**
```c
static int my_export(int (*storage_func)(const char *name,
                     const void *value, size_t val_len))
{
    /* Export all values that need persistence */
    int rc = storage_func("my_app/val", &my_val, sizeof(my_val));
    if (rc) {
        return rc;
    }
    return storage_func("my_app/name", my_name, strlen(my_name));
}

/* Include export function in handler definition */
SETTINGS_STATIC_HANDLER_DEFINE(my_app, "my_app", NULL, my_set, NULL, my_export);
```

---

#### 5. Race Condition During `h_commit`

**Symptoms:** Dependent handlers fail during commit phase.

**Cause:** Wrong commit priority ordering - handlers execute in wrong order.

**Solution:**
```c
/* Service that others depend on (lower cprio = higher priority) */
settings_register_with_cprio(&base_handler, -10);

/* Service that depends on above (higher cprio = lower priority) */
settings_register_with_cprio(&dependent_handler, 10);
```

Or with static handlers:
```c
SETTINGS_STATIC_HANDLER_DEFINE_WITH_CPRIO(base, "base", -10,
    NULL, base_set, base_commit, base_export);

SETTINGS_STATIC_HANDLER_DEFINE_WITH_CPRIO(dependent, "dependent", 10,
    NULL, dep_set, dep_commit, dep_export);
```

---

#### 6. Settings Load Partially Fails

**Symptoms:** Some settings load, others don't.

**Causes:**
- Key name mismatch between save and load
- Handler subtree doesn't match key prefix
- `h_set` returns `-ENOENT` for valid keys

**Debugging:**
```c
static int my_set(const char *name, size_t len,
                  settings_read_cb read_cb, void *cb_arg)
{
    LOG_DBG("Loading key: '%s', len: %zu", name, len);

    /* ... rest of implementation */
}
```

---

### Debugging Tips

#### 1. Enable Settings Logging

```
CONFIG_SETTINGS_LOG_LEVEL_DBG=y
CONFIG_LOG=y
```

#### 2. Verify Backend Registration

Check that backend init functions are called during startup. Add logging to confirm:
```c
LOG_INF("Settings subsys init returned: %d", settings_subsys_init());
```

#### 3. Use `settings_load_one()` for Testing

Test individual settings loading without full handler setup:
```c
uint32_t test_val;
ssize_t len = settings_load_one("my_app/val", &test_val, sizeof(test_val));
if (len < 0) {
    LOG_ERR("Load failed: %zd", len);
} else {
    LOG_INF("Loaded value: %u (len: %zd)", test_val, len);
}
```

#### 4. Check Flash Partition

Verify storage partition is properly defined:
```bash
west build -t partition_manager_report
```

#### 5. Dump All Settings

Iterate through settings for debugging:
```c
static int print_cb(const char *key, size_t len,
                    settings_read_cb read_cb, void *cb_arg, void *param)
{
    printk("Key: %s, len: %zu\n", key, len);
    return 0;
}

void dump_all_settings(void)
{
    settings_load_subtree_direct("", print_cb, NULL);
}
```

---

### Common Anti-Patterns

#### DON'T: Save Inside `h_set`

```c
/* WRONG: Don't save during load - causes infinite loop or corruption */
static int bad_set(const char *name, size_t len,
                   settings_read_cb read_cb, void *cb_arg)
{
    read_cb(cb_arg, &my_val, sizeof(my_val));
    settings_save_one("my/val", &my_val, sizeof(my_val));  /* BAD! */
    return 0;
}
```

**DO:** Separate load and save operations.

---

#### DON'T: Ignore Return Values

```c
/* WRONG: Ignoring errors */
settings_subsys_init();  /* What if this fails? */
settings_load();         /* What if this fails? */
```

**DO:** Check return values:
```c
int rc = settings_subsys_init();
if (rc) {
    LOG_ERR("Settings init failed: %d", rc);
    /* Handle error - use defaults, retry, etc. */
}

rc = settings_load();
if (rc) {
    LOG_WRN("Settings load failed: %d, using defaults", rc);
}
```

---

#### DON'T: Use Settings for High-Frequency Data

Settings is for **configuration**, not **telemetry** or **logging**.

| Good Uses | Bad Uses |
|-----------|----------|
| Device name | Sensor readings |
| Calibration data | Event counters |
| WiFi credentials | Timestamps |
| User preferences | Frequently changing state |

**Reason:** Flash wear leveling has limits. Frequent writes reduce flash lifespan.

**Alternative:** Use RAM buffers, logging subsystem, or battery-backed RTC RAM.

---

#### DON'T: Forget Null Termination for Strings

```c
/* WRONG: String may not be null-terminated */
static int bad_string_set(const char *name, size_t len,
                          settings_read_cb read_cb, void *cb_arg)
{
    if (settings_name_steq(name, "name", &next) && !next) {
        return read_cb(cb_arg, my_name, len);  /* Missing null terminator! */
    }
    return -ENOENT;
}
```

**DO:** Always null-terminate strings:
```c
static int good_string_set(const char *name, size_t len,
                           settings_read_cb read_cb, void *cb_arg)
{
    if (settings_name_steq(name, "name", &next) && !next) {
        if (len >= sizeof(my_name)) {
            return -EINVAL;  /* Too long */
        }
        int rc = read_cb(cb_arg, my_name, len);
        my_name[len] = '\0';  /* Ensure null termination */
        return rc;
    }
    return -ENOENT;
}
```

---

#### DON'T: Hardcode Key Names Inconsistently

```c
/* WRONG: Key mismatch between save and handler */
settings_save_one("myapp/value", &val, sizeof(val));  /* Note: "myapp" */

SETTINGS_STATIC_HANDLER_DEFINE(my_app, "my_app", ...);  /* Note: "my_app" */
```

**DO:** Use constants for key names:
```c
#define SETTINGS_KEY_VALUE "my_app/value"

settings_save_one(SETTINGS_KEY_VALUE, &val, sizeof(val));
```

---

### Error Code Reference

| Error | Meaning | Common Cause |
|-------|---------|--------------|
| `-EINVAL` | Invalid argument | Size mismatch, bad parameters |
| `-ENOENT` | Not found | Key doesn't exist, handler missing |
| `-ENOSPC` | No space | Flash full, ZMS collision limit |
| `-EIO` | I/O error | Flash write failure |
| `-ENOTSUP` | Not supported | Feature not enabled in Kconfig |
| `-ENODEV` | No device | Backend not initialized |
