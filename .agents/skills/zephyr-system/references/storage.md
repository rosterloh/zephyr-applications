# Direct Flash Storage (NVS, ZMS)

## Overview

### Quick Decision: NVS vs ZMS

```
What hardware are you targeting?
├── Classical NOR flash (most MCUs) → Use NVS
├── RRAM/MRAM (no erase required) → Use ZMS
├── Need >64K unique IDs → Use ZMS (supports 32/64-bit IDs)
└── Unsure → Use NVS (battle-tested default)
```

| Feature | NVS | ZMS |
|---------|-----|-----|
| ID size | 16-bit (65K max) | 32-bit or 64-bit |
| Best for | Classical flash | RRAM, MRAM, large flash |
| Erase optimization | Standard | Single-write erase (256x faster on RRAM) |
| ATE size | 8 bytes | 16 bytes |
| Data CRC | Optional | Built-in (in ATE) |
| Small data optimization | No | Yes (data stored in ATE if <=8 bytes) |
| Status | Stable, widely used | Newer, recommended for new designs |

### When to Use This Skill vs zephyr-settings

| Use Case | Skill |
|----------|-------|
| Store data with **numeric IDs** | **zephyr-storage** (this skill) |
| Store data with **string keys** (e.g., `wifi/ssid`) | zephyr-settings |
| Direct flash control needed | **zephyr-storage** |
| Integration with Bluetooth/Shell settings | zephyr-settings |
| Maximum performance, minimal overhead | **zephyr-storage** |

### Basic Usage Pattern

Both NVS and ZMS follow the same pattern:

```c
#include <zephyr/kvss/nvs.h>  // or <zephyr/kvss/zms.h>
                              // Note: <zephyr/fs/nvs.h> and <zephyr/fs/zms.h>
                              // still work but emit deprecation warnings as
                              // of Zephyr 4.4 — they are stub headers that
                              // forward to <zephyr/kvss/...>.

// 1. Define storage structure
static struct nvs_fs fs;  // or struct zms_fs

// 2. Configure and mount
fs.flash_device = PARTITION_DEVICE(storage_partition);
fs.offset = PARTITION_OFFSET(storage_partition);
// FIXED_PARTITION_DEVICE / FIXED_PARTITION_OFFSET still expand correctly
// in Zephyr 4.4 but are flagged __DEPRECATED_MACRO — prefer the names above.
fs.sector_size = /* flash page size */;
fs.sector_count = 3;  // minimum 2
nvs_mount(&fs);  // or zms_mount()

// 3. Read/Write/Delete
nvs_write(&fs, ID, data, len);
nvs_read(&fs, ID, buffer, len);
nvs_delete(&fs, ID);
```

### Kconfig Quick Reference

#### NVS
```
CONFIG_FLASH=y
CONFIG_FLASH_PAGE_LAYOUT=y
CONFIG_FLASH_MAP=y
CONFIG_NVS=y

# Optional performance
CONFIG_NVS_LOOKUP_CACHE=y
CONFIG_NVS_LOOKUP_CACHE_SIZE=128

# Optional integrity
CONFIG_NVS_DATA_CRC=y
```

#### ZMS
```
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_ZMS=y

# Optional: 64-bit IDs (default is 32-bit)
CONFIG_ZMS_ID_64BIT=y

# Optional performance
CONFIG_ZMS_LOOKUP_CACHE=y
CONFIG_ZMS_LOOKUP_CACHE_SIZE=128
```

### References

- **NVS Details**: [#nvs](#nvs) - API, Kconfig, wear leveling calculations
- **ZMS Details**: [#zms](#zms) - API, Kconfig, ATE formats, 64-bit IDs
- **Comparison**: [#comparison](#comparison) - Side-by-side feature comparison
- **Devicetree**: [#devicetree](#devicetree) - Partition configuration
- **Locations**: [#locations](#locations) - Source code and documentation paths

### Related Skills

- **zephyr-settings**: High-level persistence with string keys (uses NVS/ZMS as backend)
- **zephyr-devicetree**: Configure storage partitions
- **zephyr-kconfig**: Configure `CONFIG_NVS_*` and `CONFIG_ZMS_*` options

## Comparison

### Feature Comparison

| Feature | NVS | ZMS |
|---------|-----|-----|
| **ID Size** | 16-bit (65,535 max) | 32-bit or 64-bit |
| **ATE Size** | 8 bytes | 16 bytes |
| **Max Write Size** | No limit | 64 KB |
| **Address Space** | 32-bit | 64-bit |
| **Small Data Optimization** | No | Yes (<=8 bytes stored in ATE) |
| **Built-in Data CRC** | Optional (adds 4 bytes) | In ATE (no extra space) |
| **Erase Optimization** | Standard flash erase | Single-write invalidation |
| **Cycle Counter** | Per-entry | Per-sector |
| **Get Data Length API** | No | Yes (`zms_get_data_length`) |
| **Since Zephyr Version** | 1.12 | 3.6 |

### When to Use Each

#### Use NVS When

- Using **classical NOR flash** (most common MCUs)
- Need **proven, battle-tested** storage
- ID count is **under 65,535**
- Memory footprint is critical (smaller ATEs)
- Working with **existing NVS-based** codebase

#### Use ZMS When

- Using **RRAM, MRAM**, or other non-erase memory
- Need **more than 65K unique IDs**
- Storing many **small values** (<=8 bytes) - they fit in ATE
- Need **built-in data integrity** without extra overhead
- Working with **large flash partitions** (>4GB address space)
- Starting a **new project** (recommended for new designs)

### Performance Comparison

#### Write Performance

| Scenario | NVS | ZMS |
|----------|-----|-----|
| Small data (4 bytes) | 8 + 4 = 12 bytes | 16 bytes (in ATE) |
| Medium data (32 bytes) | 8 + 32 = 40 bytes | 16 + 32 = 48 bytes |
| Large data (1KB) | 8 + 1024 = 1032 bytes | 16 + 1024 = 1040 bytes |
| Sector erase (RRAM) | 256 writes | 1 write |

#### Read Performance

Both have O(n) lookup without cache, O(1) with cache enabled.

```kconfig
# NVS cache
CONFIG_NVS_LOOKUP_CACHE=y
CONFIG_NVS_LOOKUP_CACHE_SIZE=128

# ZMS cache
CONFIG_ZMS_LOOKUP_CACHE=y
CONFIG_ZMS_LOOKUP_CACHE_SIZE=128  # Uses 8 bytes RAM each
```

### API Comparison

#### Initialization

```c
// NVS
struct nvs_fs nvs;
nvs.flash_device = FIXED_PARTITION_DEVICE(storage_partition);
nvs.offset = FIXED_PARTITION_OFFSET(storage_partition);
nvs.sector_size = page_size;
nvs.sector_count = 3;
nvs_mount(&nvs);

// ZMS (identical pattern)
struct zms_fs zms;
zms.flash_device = FIXED_PARTITION_DEVICE(storage_partition);
zms.offset = FIXED_PARTITION_OFFSET(storage_partition);
zms.sector_size = page_size;
zms.sector_count = 4;  // Recommend 2x data size
zms_mount(&zms);
```

#### Read/Write

```c
// NVS
nvs_write(&nvs, id, data, len);          // id is uint16_t
nvs_read(&nvs, id, buffer, len);
nvs_delete(&nvs, id);

// ZMS
zms_write(&zms, id, data, len);          // id is zms_id_t (32 or 64-bit)
zms_read(&zms, id, buffer, len);
zms_delete(&zms, id);
zms_get_data_length(&zms, id);           // ZMS-only API
```

#### Clear Storage

```c
// NVS - can continue using after clear
nvs_clear(&nvs);
nvs_write(&nvs, 1, data, len);  // OK

// ZMS - MUST remount after clear
zms_clear(&zms);
zms_mount(&zms);                // Required!
zms_write(&zms, 1, data, len);
```

### Memory Overhead

#### RAM Usage

| Component | NVS | ZMS |
|-----------|-----|-----|
| Base structure | ~32 bytes | ~48 bytes |
| Mutex | ~24 bytes | ~24 bytes |
| Cache (per entry) | 4 bytes | 8 bytes |
| **Typical (128 cache)** | ~568 bytes | ~1096 bytes |

#### Flash Usage Per Entry

| Data Size | NVS | ZMS |
|-----------|-----|-----|
| 1-4 bytes | 12 bytes (+CRC: 16) | 16 bytes |
| 5-8 bytes | 16 bytes (+CRC: 20) | 16 bytes |
| 9-16 bytes | 24 bytes (+CRC: 28) | 32 bytes |
| 32 bytes | 40 bytes (+CRC: 44) | 48 bytes |
| 128 bytes | 136 bytes (+CRC: 140) | 144 bytes |

### Migration Considerations

#### NVS to ZMS

1. Data format is **incompatible** - cannot mount NVS partition with ZMS
2. Must migrate data programmatically:
   ```c
   // Read from NVS
   nvs_read(&old_nvs, id, buffer, len);
   // Write to ZMS
   zms_write(&new_zms, id, buffer, len);
   ```
3. Or erase and reinitialize on first boot

#### Settings Backend Migration

Settings subsystem abstracts the backend:
```kconfig
# Change backend in prj.conf
# FROM:
CONFIG_SETTINGS_NVS=y

# TO:
CONFIG_SETTINGS_ZMS=y
```

**Warning**: Existing settings data will be lost; device needs reprovisioning.

### Recommended Configurations

#### Memory-Constrained Device (NVS)

```kconfig
CONFIG_FLASH=y
CONFIG_FLASH_PAGE_LAYOUT=y
CONFIG_FLASH_MAP=y
CONFIG_NVS=y
CONFIG_NVS_LOOKUP_CACHE=y
CONFIG_NVS_LOOKUP_CACHE_SIZE=64
```

#### RRAM/MRAM Device (ZMS)

```kconfig
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_ZMS=y
CONFIG_ZMS_LOOKUP_CACHE=y
CONFIG_ZMS_LOOKUP_CACHE_SIZE=128
CONFIG_ZMS_NO_DOUBLE_WRITE=y  # Maximize cell lifespan
```

#### High-Reliability (ZMS with CRC)

```kconfig
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_ZMS=y
CONFIG_ZMS_DATA_CRC=y  # Only with 32-bit IDs
CONFIG_ZMS_LOOKUP_CACHE=y
```

#### Large ID Space (ZMS 64-bit)

```kconfig
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_ZMS=y
CONFIG_ZMS_ID_64BIT=y
CONFIG_ZMS_LOOKUP_CACHE=y
```

### Decision Flowchart

```
START
  │
  ├─ Using RRAM/MRAM? ─────────────────────────────► ZMS
  │
  ├─ Need >65K unique IDs? ────────────────────────► ZMS
  │
  ├─ Starting new project? ────────────────────────► ZMS (recommended)
  │
  ├─ Existing NVS codebase? ───────────────────────► NVS (unless migrating)
  │
  ├─ Extreme memory constraints? ──────────────────► NVS (smaller ATEs)
  │
  └─ Default / Unsure ─────────────────────────────► NVS (battle-tested)
```

## Devicetree

### Storage Partition Setup

Both NVS and ZMS use devicetree to define the flash partition for storage.

#### Basic Partition Definition

```dts
&flash0 {
    partitions {
        compatible = "fixed-partitions";
        #address-cells = <1>;
        #size-cells = <1>;

        /* Application code */
        slot0_partition: partition@0 {
            label = "image-0";
            reg = <0x00000000 0x000e0000>;
        };

        /* Storage partition for NVS/ZMS */
        storage_partition: partition@e0000 {
            label = "storage";
            reg = <0x000e0000 0x00020000>;  /* 128KB */
        };
    };
};
```

#### Using the Chosen Node

For Settings subsystem integration:

```dts
/ {
    chosen {
        zephyr,settings-partition = &storage_partition;
    };
};
```

### Partition Sizing Guidelines

#### Minimum Requirements

| Requirement | NVS | ZMS |
|-------------|-----|-----|
| Minimum sectors | 2 | 2 |
| Sector size | >= flash erase block | >= flash erase block |
| Recommended sectors | 3+ | 4+ (2x data size) |

#### Calculating Required Size

**NVS:**
```
Total size = SECTOR_COUNT * SECTOR_SIZE
Usable space ≈ (SECTOR_COUNT - 1) * (SECTOR_SIZE - overhead)
Overhead per entry = 8 bytes + data + (4 bytes if CRC enabled)
```

**ZMS:**
```
Total size = SECTOR_COUNT * SECTOR_SIZE
Usable space ≈ (SECTOR_COUNT - 1) * (SECTOR_SIZE - 80)
Overhead per entry = 16 bytes (+ data if > 8 bytes)
```

#### Example Sizing

For 100 entries averaging 32 bytes each:

**NVS:**
- Per entry: 8 + 32 = 40 bytes
- Total data: 100 * 40 = 4,000 bytes
- With 3 sectors of 4KB: 2 * 4,096 = 8,192 bytes usable
- Sufficient

**ZMS:**
- Per entry: 16 + 32 = 48 bytes
- Total data: 100 * 48 = 4,800 bytes
- Recommended: 2x = 9,600 bytes
- With 4 sectors of 4KB: 3 * (4,096 - 80) = 12,048 bytes usable
- Sufficient with room for GC

### Board-Specific Overlays

#### Creating a Board Overlay

Create `boards/<board>.overlay` in your application:

```dts
/* boards/nrf52840dk_nrf52840.overlay */

&flash0 {
    partitions {
        compatible = "fixed-partitions";
        #address-cells = <1>;
        #size-cells = <1>;

        storage_partition: partition@fc000 {
            label = "storage";
            reg = <0x000fc000 0x00004000>;  /* 16KB at end of flash */
        };
    };
};
```

#### Common Board Examples

##### Nordic nRF52840 (1MB flash, 4KB pages)

```dts
storage_partition: partition@f8000 {
    label = "storage";
    reg = <0x000f8000 0x00008000>;  /* 32KB = 8 sectors */
};
```

##### STM32F4 (2KB pages typical)

```dts
storage_partition: partition@70000 {
    label = "storage";
    reg = <0x00070000 0x00010000>;  /* 64KB */
};
```

##### ESP32 (4KB pages)

```dts
storage_partition: partition@310000 {
    label = "storage";
    reg = <0x00310000 0x00006000>;  /* 24KB = 6 sectors */
};
```

### Accessing Partition in Code

#### Using Flash Map Macros

```c
#include <zephyr/storage/flash_map.h>

#define STORAGE_PARTITION storage_partition
#define STORAGE_DEVICE    FIXED_PARTITION_DEVICE(STORAGE_PARTITION)
#define STORAGE_OFFSET    FIXED_PARTITION_OFFSET(STORAGE_PARTITION)
#define STORAGE_SIZE      FIXED_PARTITION_SIZE(STORAGE_PARTITION)
```

#### Getting Flash Page Info

```c
#include <zephyr/drivers/flash.h>

struct flash_pages_info info;
int rc = flash_get_page_info_by_offs(fs.flash_device, fs.offset, &info);
if (rc == 0) {
    fs.sector_size = info.size;  /* Use actual page size */
}
```

### Multiple Storage Partitions

For separate NVS/ZMS instances:

```dts
&flash0 {
    partitions {
        /* Settings storage */
        settings_partition: partition@e0000 {
            label = "settings";
            reg = <0x000e0000 0x00010000>;
        };

        /* Application data storage */
        data_partition: partition@f0000 {
            label = "appdata";
            reg = <0x000f0000 0x00010000>;
        };
    };
};

/ {
    chosen {
        zephyr,settings-partition = &settings_partition;
    };
};
```

In code:

```c
static struct nvs_fs settings_fs;
static struct nvs_fs data_fs;

void init_storage(void)
{
    /* Settings partition */
    settings_fs.flash_device = FIXED_PARTITION_DEVICE(settings_partition);
    settings_fs.offset = FIXED_PARTITION_OFFSET(settings_partition);
    /* ... */
    nvs_mount(&settings_fs);

    /* Data partition */
    data_fs.flash_device = FIXED_PARTITION_DEVICE(data_partition);
    data_fs.offset = FIXED_PARTITION_OFFSET(data_partition);
    /* ... */
    nvs_mount(&data_fs);
}
```

### External Flash

#### QSPI Flash Example

```dts
&qspi {
    status = "okay";

    mx25r64: mx25r6435f@0 {
        compatible = "nordic,qspi-nor";
        reg = <0>;
        /* ... flash properties ... */

        partitions {
            compatible = "fixed-partitions";
            #address-cells = <1>;
            #size-cells = <1>;

            storage_partition: partition@0 {
                label = "storage";
                reg = <0x00000000 0x00100000>;  /* 1MB */
            };
        };
    };
};
```

### Verifying Partition Setup

#### At Runtime

```c
#include <zephyr/storage/flash_map.h>

void check_partition(void)
{
    const struct flash_area *fa;
    int rc = flash_area_open(FIXED_PARTITION_ID(storage_partition), &fa);
    if (rc == 0) {
        printk("Partition: offset=0x%lx, size=%u\n",
               (unsigned long)fa->fa_off, fa->fa_size);
        flash_area_close(fa);
    }
}
```

#### Build-Time Check

Use `west build -t menuconfig` or check generated devicetree:

```bash
# View generated devicetree
cat builds/zephyr/zephyr.dts

# Check partition table
west build -t partition_table  # If supported
```

### Common Issues

#### Partition Overlaps

Ensure partitions don't overlap with:
- Bootloader
- Application slots (for DFU)
- Other storage areas

#### Alignment Issues

- `reg` address must align to flash erase block
- Size should be multiple of erase block

#### Partition Not Found

1. Check label matches code reference
2. Verify `compatible = "fixed-partitions"`
3. Ensure flash node is enabled (`status = "okay"`)

## Locations

### Zephyr Repository Paths

All paths relative to Zephyr root (`zephyr/`).

#### NVS

| Resource | Path |
|----------|------|
| Header | `include/zephyr/fs/nvs.h` |
| Implementation | `subsys/fs/nvs/nvs.c` |
| Private header | `subsys/fs/nvs/nvs_priv.h` |
| Kconfig | `subsys/fs/nvs/Kconfig` |
| Documentation | `doc/services/storage/nvs/nvs.rst` |
| Sample | `samples/subsys/nvs/` |
| Tests | `tests/subsys/fs/nvs/` |

#### ZMS

| Resource | Path |
|----------|------|
| Header | `include/zephyr/fs/zms.h` |
| Implementation | `subsys/fs/zms/zms.c` |
| Private header | `subsys/fs/zms/zms_priv.h` |
| Kconfig | `subsys/fs/zms/Kconfig` |
| Documentation | `doc/services/storage/zms/zms.rst` |
| Tests | `tests/subsys/fs/zms/` |

#### Settings Backend

| Resource | Path |
|----------|------|
| NVS backend header | `subsys/settings/include/settings/settings_nvs.h` |
| NVS backend impl | `subsys/settings/src/settings_nvs.c` |
| ZMS backend header | `subsys/settings/include/settings/settings_zms.h` |
| ZMS backend impl | `subsys/settings/src/settings_zms.c` |
| Settings Kconfig | `subsys/settings/Kconfig` |

#### Flash Map

| Resource | Path |
|----------|------|
| Flash map header | `include/zephyr/storage/flash_map.h` |
| Flash driver header | `include/zephyr/drivers/flash.h` |

### Online Documentation

#### Official Zephyr Docs

- NVS: https://docs.zephyrproject.org/latest/services/storage/nvs/nvs.html
- ZMS: https://docs.zephyrproject.org/latest/services/storage/zms/zms.html
- Settings: https://docs.zephyrproject.org/latest/services/settings/index.html
- Flash Map: https://docs.zephyrproject.org/latest/services/storage/flash_map/flash_map.html

#### API Reference

- NVS API: https://docs.zephyrproject.org/latest/doxygen/html/group__nvs.html
- ZMS API: https://docs.zephyrproject.org/latest/doxygen/html/group__zms.html

### Sample Applications

#### NVS Sample

Location: `samples/subsys/nvs/`

```
samples/subsys/nvs/
├── CMakeLists.txt
├── Kconfig
├── prj.conf
├── README.rst
├── sample.yaml
├── src/
│   └── main.c
└── boards/
    ├── nrf52840dk_nrf52840.overlay
    ├── nucleo_f429zi.overlay
    └── ...
```

Build and run:
```bash
west build -b <board> samples/subsys/nvs
west flash
```

#### ZMS Sample

No dedicated sample as of Zephyr 3.6. Use NVS sample as template, replacing:
- `#include <zephyr/fs/nvs.h>` → `#include <zephyr/fs/zms.h>`
- `struct nvs_fs` → `struct zms_fs`
- `nvs_*` functions → `zms_*` functions
- `CONFIG_NVS=y` → `CONFIG_ZMS=y`

### Tests

#### Running Tests

```bash
# NVS tests
west twister -T tests/subsys/fs/nvs/

# ZMS tests
west twister -T tests/subsys/fs/zms/

# Settings with NVS backend
west twister -T tests/subsys/settings/nvs/

# Settings with ZMS backend
west twister -T tests/subsys/settings/zms/
```

### Key Files to Reference

When implementing storage:

1. **API usage**: `samples/subsys/nvs/src/main.c`
2. **Board overlays**: `samples/subsys/nvs/boards/*.overlay`
3. **Kconfig options**: `subsys/fs/nvs/Kconfig` or `subsys/fs/zms/Kconfig`
4. **Error codes**: Check return values in `include/zephyr/fs/nvs.h` or `zms.h`

When debugging:

1. **Enable logging**: `CONFIG_NVS_LOG_LEVEL_DBG=y` or `CONFIG_ZMS_LOG_LEVEL_DBG=y`
2. **Check implementation**: `subsys/fs/nvs/nvs.c` or `subsys/fs/zms/zms.c`

## Nvs

NVS stores id-data pairs in flash using a FIFO-managed circular buffer. Flash is divided into sectors; elements are appended until a sector is full, then a new sector is prepared (erased) and valid data is copied.

- **ID**: 16-bit unsigned integer (0-65535)
- **Metadata**: 8 bytes per entry (id, offset, length, CRC)
- **Minimum sectors**: 2 (one always kept empty for garbage collection)

### API Reference

#### Header
```c
#include <zephyr/fs/nvs.h>
```

#### Data Structure

```c
struct nvs_fs {
    off_t offset;                    // Flash offset
    uint32_t sector_size;            // Must be multiple of erase-block-size
    uint16_t sector_count;           // Minimum 2
    const struct device *flash_device;
    // Internal fields (initialized by nvs_mount)
    uint32_t ate_wra;                // ATE write address
    uint32_t data_wra;               // Data write address
    bool ready;
    struct k_mutex nvs_lock;
};
```

#### Functions

| Function | Description | Return |
|----------|-------------|--------|
| `nvs_mount(fs)` | Initialize and mount NVS | 0 on success, -errno on error |
| `nvs_clear(fs)` | Erase all data | 0 on success |
| `nvs_write(fs, id, data, len)` | Write entry (len=0 deletes) | Bytes written, 0 if unchanged, -errno |
| `nvs_read(fs, id, data, len)` | Read latest entry | Bytes read, -errno on error |
| `nvs_read_hist(fs, id, data, len, cnt)` | Read historical entry (cnt=0 is latest) | Bytes read |
| `nvs_delete(fs, id)` | Delete entry | 0 on success |
| `nvs_calc_free_space(fs)` | Calculate free bytes (slow) | Free bytes, -errno |
| `nvs_sector_max_data_size(fs)` | Free space in current sector | Bytes |
| `nvs_sector_use_next(fs)` | Force sector switch (use sparingly) | 0 on success |

#### Write Behavior

- Writing with `len=0` is equivalent to `nvs_delete()`
- NVS checks if data is unchanged before writing; returns 0 if no write needed
- Each write consumes: 8 bytes metadata + data length (+ 4 bytes if `CONFIG_NVS_DATA_CRC`)

### Kconfig Options

```kconfig
CONFIG_NVS=y                      # Enable NVS
CONFIG_NVS_LOOKUP_CACHE=y         # Enable lookup cache (faster reads)
CONFIG_NVS_LOOKUP_CACHE_SIZE=128  # Cache entries (power of 2 recommended)
CONFIG_NVS_DATA_CRC=y             # CRC-32 on data (adds 4 bytes per entry)
CONFIG_NVS_INIT_BAD_MEMORY_REGION=y  # Auto-init corrupted regions
CONFIG_NVS_LOG_LEVEL_DBG=y        # Debug logging
```

**Dependencies:**
```kconfig
CONFIG_FLASH=y
CONFIG_FLASH_PAGE_LAYOUT=y
CONFIG_FLASH_MAP=y
```

### Complete Example

```c
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/fs/nvs.h>

static struct nvs_fs fs;

#define STORAGE_PARTITION storage_partition
#define ADDRESS_ID  1
#define COUNTER_ID  2

int main(void)
{
    int rc;
    struct flash_pages_info info;
    uint32_t counter = 0;
    char address[16];

    /* Setup NVS */
    fs.flash_device = FIXED_PARTITION_DEVICE(STORAGE_PARTITION);
    if (!device_is_ready(fs.flash_device)) {
        printk("Flash device not ready\n");
        return -1;
    }

    fs.offset = FIXED_PARTITION_OFFSET(STORAGE_PARTITION);
    rc = flash_get_page_info_by_offs(fs.flash_device, fs.offset, &info);
    if (rc) {
        printk("Unable to get page info\n");
        return -1;
    }

    fs.sector_size = info.size;
    fs.sector_count = 3;

    rc = nvs_mount(&fs);
    if (rc) {
        printk("NVS mount failed: %d\n", rc);
        return -1;
    }

    /* Read or initialize address */
    rc = nvs_read(&fs, ADDRESS_ID, address, sizeof(address));
    if (rc <= 0) {
        strcpy(address, "192.168.1.1");
        nvs_write(&fs, ADDRESS_ID, address, strlen(address) + 1);
    }

    /* Read, increment, and save counter */
    nvs_read(&fs, COUNTER_ID, &counter, sizeof(counter));
    counter++;
    nvs_write(&fs, COUNTER_ID, &counter, sizeof(counter));

    printk("Address: %s, Counter: %u\n", address, counter);
    return 0;
}
```

### Flash Wear Calculation

Expected device lifetime formula:

```
Lifetime (minutes) = (SECTOR_COUNT * SECTOR_SIZE * PAGE_ERASES) / (WRITES_PER_MIN * (DATA_SIZE + 8))
```

**Example**: 4-byte counter updated every minute, 2 sectors of 1024 bytes, 20,000 erase cycles:
- Storage per write: 4 + 8 = 12 bytes
- Writes per sector: 1024 / 12 = 85
- Time to fill both sectors: 85 * 2 = 170 minutes
- Lifetime: 170 * 20,000 = 3,400,000 minutes (~6.5 years)

**To extend lifetime:**
- Increase `SECTOR_COUNT`
- Increase `SECTOR_SIZE`
- Reduce write frequency
- Batch updates when possible

### Troubleshooting

#### MPU Fault or -ETIMEDOUT

NVS using internal flash requires:
```kconfig
CONFIG_MPU_ALLOW_FLASH_WRITE=y
```

#### Data Not Persisting

1. Check `nvs_mount()` return value
2. Verify partition exists in devicetree
3. Ensure `sector_size` is multiple of flash erase-block-size
4. Ensure `sector_size` is power of 2

#### Slow Reads

Enable lookup cache:
```kconfig
CONFIG_NVS_LOOKUP_CACHE=y
CONFIG_NVS_LOOKUP_CACHE_SIZE=128
```

#### Running Out of Space

- Increase partition size in devicetree
- Increase `sector_count`
- Delete unused entries with `nvs_delete()`

## Zms

ZMS is a key-value storage system designed for all types of non-volatile storage, including classical NOR flash and newer technologies like RRAM/MRAM that don't require erase operations.

**Key advantages over NVS:**
- Single-write sector invalidation (256x faster on RRAM/MRAM)
- 32-bit or 64-bit IDs (vs NVS 16-bit)
- Small data optimization (data stored in ATE if <=8 bytes)
- Built-in data CRC in ATE structure
- 64-bit address space for large partitions

### API Reference

#### Header
```c
#include <zephyr/fs/zms.h>
```

#### Data Structure

```c
struct zms_fs {
    off_t offset;                    // Flash offset
    uint32_t sector_size;            // Multiple of erase-block-size
    uint32_t sector_count;           // Minimum 2
    const struct device *flash_device;
    // Internal fields (initialized by zms_mount)
    uint64_t ate_wra;                // ATE write address (64-bit)
    uint64_t data_wra;               // Data write address (64-bit)
    uint8_t sector_cycle;            // Current cycle counter
    bool ready;
    struct k_mutex zms_lock;
    size_t ate_size;                 // ATE size (16 bytes)
};
```

#### ID Type

```c
// Depends on CONFIG_ZMS_ID_64BIT
#if CONFIG_ZMS_ID_64BIT
typedef uint64_t zms_id_t;  // 64-bit IDs
#else
typedef uint32_t zms_id_t;  // 32-bit IDs (default)
#endif
```

#### Functions

| Function | Description | Return |
|----------|-------------|--------|
| `zms_mount(fs)` | Initialize and mount ZMS | 0, -ENOTSUP, -EPROTONOSUPPORT, -EINVAL, -ENXIO, -EIO |
| `zms_clear(fs)` | Erase all data (must remount after) | 0, -EACCES, -ENXIO, -EIO, -EINVAL |
| `zms_write(fs, id, data, len)` | Write entry (max 64KB, len=0 deletes) | Bytes written, 0 if unchanged, -errno |
| `zms_read(fs, id, data, len)` | Read latest entry | Bytes read, -ENOENT if not found |
| `zms_read_hist(fs, id, data, len, cnt)` | Read historical entry | Bytes read |
| `zms_delete(fs, id)` | Delete entry | 0 on success |
| `zms_get_data_length(fs, id)` | Get stored data length | Length, -ENOENT if not found |
| `zms_calc_free_space(fs)` | Calculate free bytes (slow) | Free bytes |
| `zms_active_sector_free_space(fs)` | Free space in current sector | Bytes |
| `zms_sector_use_next(fs)` | Force sector switch (use sparingly) | 0 on success |

#### Key Differences from NVS API

| Feature | NVS | ZMS |
|---------|-----|-----|
| ID type | `uint16_t` | `zms_id_t` (32 or 64-bit) |
| Max write size | Unlimited | 64 KB |
| Get data length | Not available | `zms_get_data_length()` |
| Remount after clear | Not required | Required |

### Kconfig Options

#### Core
```kconfig
CONFIG_ZMS=y                      # Enable ZMS
```

#### ID Size
```kconfig
CONFIG_ZMS_ID_64BIT=y             # Use 64-bit IDs (default: 32-bit)
```

**Warning**: Changing ID size makes existing storage incompatible.

#### Performance
```kconfig
CONFIG_ZMS_LOOKUP_CACHE=y         # Enable lookup cache
CONFIG_ZMS_LOOKUP_CACHE_SIZE=128  # Cache entries (8 bytes RAM each)
CONFIG_ZMS_LOOKUP_CACHE_FOR_SETTINGS=y  # Optimized for Settings backend
CONFIG_ZMS_NO_DOUBLE_WRITE=y      # Avoid rewriting same data (slower writes)
```

#### Advanced
```kconfig
CONFIG_ZMS_DATA_CRC=y             # Extra data CRC (not available with 64-bit IDs)
CONFIG_ZMS_CUSTOMIZE_BLOCK_SIZE=y # Custom internal buffer
CONFIG_ZMS_CUSTOM_BLOCK_SIZE=32   # Buffer size (default 32)
```

**Dependencies:**
```kconfig
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
```

### ATE (Allocation Table Entry) Format

#### 32-bit ID Format (Default)

16-byte entry:
```
+-----+----------+-----+-----------+-------------+---------------+
| 0   | 1        | 2-3 | 4-7       | 8-11        | 12-15         |
+-----+----------+-----+-----------+-------------+---------------+
| crc8| cycle_cnt| len | id (32b)  | offset/data | data_crc/meta |
+-----+----------+-----+-----------+-------------+---------------+
```

- Small data (<=8 bytes): stored directly in bytes 8-15
- Large data: offset in bytes 8-11, CRC in 12-15

#### 64-bit ID Format

16-byte entry:
```
+-----+----------+-----+-------------------------+-------------------+
| 0   | 1        | 2-3 | 4-11                    | 12-15             |
+-----+----------+-----+-------------------------+-------------------+
| crc8| cycle_cnt| len | id (64-bit)             | offset/data/meta  |
+-----+----------+-----+-------------------------+-------------------+
```

- Small data (<=4 bytes): stored directly in bytes 12-15
- Large data: offset in bytes 12-15

### Complete Example

```c
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/fs/zms.h>

static struct zms_fs fs;

#define STORAGE_PARTITION storage_partition
#define CONFIG_ID   1
#define COUNTER_ID  2
#define SENSOR_ID   3

int main(void)
{
    int rc;
    struct flash_pages_info info;
    uint32_t counter = 0;

    /* Setup ZMS */
    fs.flash_device = FIXED_PARTITION_DEVICE(STORAGE_PARTITION);
    if (!device_is_ready(fs.flash_device)) {
        printk("Flash device not ready\n");
        return -1;
    }

    fs.offset = FIXED_PARTITION_OFFSET(STORAGE_PARTITION);
    rc = flash_get_page_info_by_offs(fs.flash_device, fs.offset, &info);
    if (rc) {
        printk("Unable to get page info\n");
        return -1;
    }

    fs.sector_size = info.size;
    fs.sector_count = 4;  // Recommend 2x data size for optimal GC

    rc = zms_mount(&fs);
    if (rc) {
        printk("ZMS mount failed: %d\n", rc);
        return -1;
    }

    /* Check data length before reading */
    ssize_t len = zms_get_data_length(&fs, CONFIG_ID);
    if (len > 0) {
        char *config = k_malloc(len);
        zms_read(&fs, CONFIG_ID, config, len);
        printk("Config: %s\n", config);
        k_free(config);
    }

    /* Read, increment, and save counter */
    zms_read(&fs, COUNTER_ID, &counter, sizeof(counter));
    counter++;
    zms_write(&fs, COUNTER_ID, &counter, sizeof(counter));

    printk("Counter: %u\n", counter);
    return 0;
}
```

### Flash Wear and Lifetime

#### Sector Layout

ZMS always keeps one sector empty for garbage collection:
- 4 sectors configured = 3 usable for data
- Header overhead: 80 bytes per sector (5 ATEs)

#### Available Space Calculation

**Small data (<=8 bytes):**
```
Space = ((SECTOR_COUNT - 1) * (SECTOR_SIZE - 80) * 8) / 16
```

**Large data:**
```
Space = ((SECTOR_COUNT - 1) * (SECTOR_SIZE - 80)) / (DATA_SIZE + 16)
```

#### Lifetime Formula

```
Lifetime (min) = (SECTOR_SIZE - 80) * SECTOR_COUNT * MAX_WRITES / (EFFECTIVE_SIZE * WRITES_PER_MIN)
```

Where `EFFECTIVE_SIZE`:
- Small data: 16 bytes
- Large data: 16 + sizeof(data)

**Example**: 4-byte counter every minute, 4 sectors of 1024 bytes, 20,000 write cycles:
- Effective size: 16 bytes (stored in ATE)
- Sector effective space: 944 bytes
- Lifetime: (944 * 4 * 20,000) / (16 * 1) = 4,720,000 minutes (~9 years)

#### Best Practices

1. **Partition size**: 2x expected data size for optimal GC
2. **Sector size**: Large enough for max single entry
3. **Small data**: Keep values <=8 bytes when possible (stored in ATE)
4. **Settings paths**: Use <=8 byte path names for ZMS backend optimization

### Triggering Garbage Collection

For real-time applications needing predictable write latency:

```c
/* Check remaining space in current sector */
ssize_t free = zms_active_sector_free_space(&fs);

/* If running low, trigger GC proactively */
if (free < MINIMUM_REQUIRED) {
    zms_sector_use_next(&fs);  // Triggers GC on next sector
}
```

### Troubleshooting

#### Mount Fails with -ENOTSUP or -EPROTONOSUPPORT
- Storage was initialized with different ZMS version or ATE format
- Solution: Erase partition and reinitialize

#### Data CRC Errors After Enabling CONFIG_ZMS_DATA_CRC
- CRC feature change invalidates existing data
- Solution: Erase and reinitialize storage

#### Changing Between 32-bit and 64-bit IDs
- Incompatible ATE formats
- Solution: Erase and reinitialize storage

#### Slow Writes with CONFIG_ZMS_NO_DOUBLE_WRITE
- Expected behavior; searches entire storage before writing
- Only enable when write cycles are critical concern

#### Reflashing does NOT clear the storage partition

**`west flash` / `mise run flash` only erases the image region.** On ESP32 targets
esptool writes just the app slot (e.g. `0x20000–0x105fff`), so whatever was in
the ZMS/settings/storage partition survives — across a rebuild, across a
different app, across a Zephyr version bump.

Consequences seen on hardware in this workspace:

- A freshly flashed image that will not come up at all, because `fs_zms` cannot
  mount stale content. On `pt_mcp` (xiao_esp32c5) this looked like endless bare
  `uart:~$` prompts with no boot banner and a completely dead shell — no echo,
  no command execution.
- A first BLE pairing failing with `Security failed: level 1 err 4` from
  leftover bond data (`bt_keys` on esp32c3).
- Any of the "erase and reinitialize" fixes above appearing not to work, because
  the reflash never actually erased anything.

Full chip erase is the recovery:

```bash
mise x -- esptool --port /dev/cu.usbmodem1101 erase-flash
mise run flash <app>
```

Two caveats:

- **This also erases MCUboot** on a sysbuild image — reflash `merged.hex`, not
  just the app. See `./sysbuild-mcuboot.md`.
- It clears stored WiFi credentials too, which is *why* it recovers a board
  wedged by boot-time auto-connect — see
  `../../zephyr-connectivity/references/wifi.md`.

When a storage-format change is deliberate (ZMS version, `CONFIG_ZMS_DATA_CRC`,
32↔64-bit IDs), plan the erase rather than assuming a reflash covers it. On
targets whose console is USB-Serial/JTAG, remember early boot output is lost on
reset anyway because the USB device re-enumerates before the host monitor
reattaches — a missing banner is not by itself evidence of a fault.
