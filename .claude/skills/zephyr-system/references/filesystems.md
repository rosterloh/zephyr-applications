# File Systems

## Overview

### Quick Decision: Choose Your File System

```
What storage medium and use case?
├── Internal flash (MCU) + power-loss safety → LittleFS
├── SD card / USB / PC compatibility needed → FAT
├── Large block device + Linux compatibility → ext2
├── Log-style append-only data on flash → FCB
├── Key-value pairs (not files) → See zephyr-storage skill
└── Virtualized environment (QEMU) → VirtioFS
```

| Feature | LittleFS | FAT | ext2 | FCB |
|---------|----------|-----|------|-----|
| Storage type | Flash | Disk (SD/USB) | Block device | Flash |
| Power-loss safe | Yes | No | No | Yes |
| Wear leveling | Yes | No | No | Yes |
| PC compatible | No | Yes | Linux only | No |
| Best for | Embedded flash | Removable media | Large storage | Logs/journals |

### VFS API (Common to All File Systems)

All file systems use the same API from `<zephyr/fs/fs.h>`.

#### Basic File Operations

```c
#include <zephyr/fs/fs.h>

struct fs_file_t file;
fs_file_t_init(&file);  // MUST initialize before use

// Open file (create if needed)
int rc = fs_open(&file, "/lfs/data.txt", FS_O_CREATE | FS_O_RDWR);

// Write data
const char *data = "Hello";
fs_write(&file, data, strlen(data));

// Seek to beginning
fs_seek(&file, 0, FS_SEEK_SET);

// Read data
char buf[32];
ssize_t bytes = fs_read(&file, buf, sizeof(buf));

// Close file
fs_close(&file);
```

#### Open Flags

| Flag | Description |
|------|-------------|
| `FS_O_READ` | Open for read |
| `FS_O_WRITE` | Open for write |
| `FS_O_RDWR` | Read and write |
| `FS_O_CREATE` | Create if not exists |
| `FS_O_APPEND` | Append mode |
| `FS_O_TRUNC` | Truncate to zero |

#### Directory Operations

```c
struct fs_dir_t dir;
struct fs_dirent entry;

fs_dir_t_init(&dir);  // MUST initialize before use
fs_opendir(&dir, "/lfs");

while (fs_readdir(&dir, &entry) == 0 && entry.name[0] != 0) {
    if (entry.type == FS_DIR_ENTRY_DIR) {
        printk("[DIR]  %s\n", entry.name);
    } else {
        printk("[FILE] %s (%zu bytes)\n", entry.name, entry.size);
    }
}
fs_closedir(&dir);

// Create directory
fs_mkdir("/lfs/newdir");

// Delete file or empty directory
fs_unlink("/lfs/oldfile.txt");

// Rename/move
fs_rename("/lfs/old.txt", "/lfs/new.txt");
```

#### Mount and Volume Operations

```c
// Mount filesystem
fs_mount(&mount_point);

// Unmount
fs_unmount(&mount_point);

// Get volume stats
struct fs_statvfs stat;
fs_statvfs("/lfs", &stat);
printk("Total: %lu blocks, Free: %lu blocks\n", stat.f_blocks, stat.f_bfree);

// Format filesystem (requires CONFIG_FILE_SYSTEM_MKFS=y)
fs_mkfs(FS_LITTLEFS, (uintptr_t)PARTITION_ID(storage_partition), NULL, 0);
/* FIXED_PARTITION_ID still expands correctly but is __DEPRECATED_MACRO in 4.4. */
```

### Kconfig Quick Reference

#### Core File System

```kconfig
CONFIG_FILE_SYSTEM=y              # Enable VFS
CONFIG_FILE_SYSTEM_SHELL=y        # Shell commands (ls, cd, cat, etc.)
CONFIG_FILE_SYSTEM_MKFS=y         # Enable formatting
```

#### LittleFS

```kconfig
CONFIG_FILE_SYSTEM_LITTLEFS=y
CONFIG_FS_LITTLEFS_NUM_FILES=4    # Max open files
CONFIG_FS_LITTLEFS_NUM_DIRS=4     # Max open directories
CONFIG_FS_LITTLEFS_CACHE_SIZE=64  # Cache per file (RAM usage)
CONFIG_FS_LITTLEFS_BLOCK_CYCLES=512  # Wear leveling cycles
```

#### FAT

```kconfig
CONFIG_FAT_FILESYSTEM_ELM=y
CONFIG_FS_FATFS_LFN=y             # Long file names
CONFIG_FS_FATFS_EXFAT=y           # exFAT support
CONFIG_FS_FATFS_MKFS=y            # Format support
```

#### ext2

```kconfig
CONFIG_FILE_SYSTEM_EXT2=y
CONFIG_EXT2_MAX_BLOCK_SIZE=4096
```

#### FCB

```kconfig
CONFIG_FCB=y
CONFIG_FLASH_MAP=y
```

### Devicetree Fstab (Automount)

Define mount points in devicetree for automatic mounting:

```dts
/ {
    fstab {
        compatible = "zephyr,fstab";
        lfs1: lfs1 {
            compatible = "zephyr,fstab,littlefs";
            mount-point = "/lfs";
            partition = <&storage_partition>;
            automount;
            read-size = <16>;
            prog-size = <16>;
            cache-size = <64>;
            lookahead-size = <32>;
            block-cycles = <512>;
        };
    };
};
```

Access automounted filesystem:

```c
#define PARTITION_NODE DT_NODELABEL(lfs1)
FS_FSTAB_DECLARE_ENTRY(PARTITION_NODE);
struct fs_mount_t *mp = &FS_FSTAB_ENTRY(PARTITION_NODE);
// mp is already mounted, use directly
```

### References

- **LittleFS**: [#littlefs](#littlefs) - Flash-based filesystem configuration and usage
- **FAT**: [#fat](#fat) - SD card and removable media
- **FCB**: [#fcb](#fcb) - Flash Circular Buffer for logs
- **API Reference**: [#api](#api) - Complete VFS API
- **Locations**: [#locations](#locations) - Source code and sample paths

### Related Skills

- **zephyr-storage**: Direct key-value storage (NVS/ZMS) with numeric IDs
- **zephyr-settings**: High-level settings with string keys
- **zephyr-devicetree**: Configure storage partitions

## Api

Complete API reference for Zephyr's Virtual File System abstraction.

### Header

```c
#include <zephyr/fs/fs.h>
```

### Data Types

#### fs_file_t

File handle structure. Must be initialized with `fs_file_t_init()` before use.

```c
struct fs_file_t file;
fs_file_t_init(&file);  // Required before fs_open()
```

#### fs_dir_t

Directory handle structure. Must be initialized with `fs_dir_t_init()` before use.

```c
struct fs_dir_t dir;
fs_dir_t_init(&dir);  // Required before fs_opendir()
```

#### fs_dirent

Directory entry information returned by `fs_readdir()` and `fs_stat()`.

```c
struct fs_dirent {
    enum fs_dir_entry_type type;  // FS_DIR_ENTRY_FILE or FS_DIR_ENTRY_DIR
    char name[MAX_FILE_NAME + 1]; // Entry name
    size_t size;                   // File size (0 for directories)
};
```

#### fs_statvfs

Volume statistics structure.

```c
struct fs_statvfs {
    unsigned long f_bsize;   // Optimal transfer block size
    unsigned long f_frsize;  // Allocation unit size
    unsigned long f_blocks;  // Size of FS in f_frsize units
    unsigned long f_bfree;   // Number of free blocks
};
```

#### fs_mount_t

Mount point structure.

```c
struct fs_mount_t {
    int type;                // FS_FATFS, FS_LITTLEFS, FS_EXT2
    const char *mnt_point;   // Mount point path (e.g., "/lfs")
    void *fs_data;           // FS-specific config (e.g., littlefs_config)
    void *storage_dev;       // Storage device (partition ID or disk name)
    uint8_t flags;           // Mount flags
};
```

### Constants

#### File Open Flags

| Flag | Value | Description |
|------|-------|-------------|
| `FS_O_READ` | 0x01 | Open for reading |
| `FS_O_WRITE` | 0x02 | Open for writing |
| `FS_O_RDWR` | 0x03 | Open for read and write |
| `FS_O_CREATE` | 0x10 | Create file if not exists |
| `FS_O_APPEND` | 0x20 | Append to end of file |
| `FS_O_TRUNC` | 0x40 | Truncate file to zero length |

#### Seek Whence

| Constant | Value | Description |
|----------|-------|-------------|
| `FS_SEEK_SET` | 0 | Seek from beginning |
| `FS_SEEK_CUR` | 1 | Seek from current position |
| `FS_SEEK_END` | 2 | Seek from end |

#### Mount Flags

| Flag | Description |
|------|-------------|
| `FS_MOUNT_FLAG_NO_FORMAT` | Don't format if FS not found |
| `FS_MOUNT_FLAG_READ_ONLY` | Mount read-only |
| `FS_MOUNT_FLAG_AUTOMOUNT` | Auto-mount on boot (fstab only) |
| `FS_MOUNT_FLAG_USE_DISK_ACCESS` | Use Disk Access API (not Flash API) |

#### File System Types

| Type | Description |
|------|-------------|
| `FS_FATFS` | FAT/exFAT file system |
| `FS_LITTLEFS` | LittleFS file system |
| `FS_EXT2` | ext2 file system |
| `FS_VIRTIOFS` | VirtioFS (QEMU) |
| `FS_TYPE_EXTERNAL_BASE` | Base for custom FS types |

### File Operations

#### fs_file_t_init

Initialize file handle. **Must be called before `fs_open()`.**

```c
static inline void fs_file_t_init(struct fs_file_t *zfp);
```

#### fs_open

Open or create a file.

```c
int fs_open(struct fs_file_t *zfp, const char *file_name, fs_mode_t flags);
```

**Parameters:**
- `zfp`: Initialized file handle
- `file_name`: Absolute path (e.g., "/lfs/data.txt")
- `flags`: Combination of `FS_O_*` flags

**Returns:**
- `0` on success
- `-EBUSY` if handle already in use
- `-EINVAL` for invalid path
- `-EROFS` for read-only filesystem
- `-ENOENT` if file not found (and `FS_O_CREATE` not set)
- `-ENOTSUP` if not implemented

**Example:**
```c
struct fs_file_t file;
fs_file_t_init(&file);
int rc = fs_open(&file, "/lfs/config.txt", FS_O_CREATE | FS_O_RDWR);
if (rc < 0) {
    printk("Failed to open: %d\n", rc);
}
```

#### fs_close

Close a file.

```c
int fs_close(struct fs_file_t *zfp);
```

**Returns:** `0` on success, negative errno on error.

#### fs_read

Read from file.

```c
ssize_t fs_read(struct fs_file_t *zfp, void *ptr, size_t size);
```

**Returns:** Number of bytes read (may be less than `size`), or negative errno.

**Example:**
```c
char buf[128];
ssize_t bytes = fs_read(&file, buf, sizeof(buf));
if (bytes < 0) {
    printk("Read error: %zd\n", bytes);
} else {
    printk("Read %zd bytes\n", bytes);
}
```

#### fs_write

Write to file.

```c
ssize_t fs_write(struct fs_file_t *zfp, const void *ptr, size_t size);
```

**Returns:** Number of bytes written, or negative errno.

**Note:** If return value is less than `size`, check `errno` for disk full condition.

#### fs_seek

Move file position.

```c
int fs_seek(struct fs_file_t *zfp, off_t offset, int whence);
```

**Parameters:**
- `offset`: Position offset
- `whence`: `FS_SEEK_SET`, `FS_SEEK_CUR`, or `FS_SEEK_END`

**Returns:** `0` on success, negative errno on error.

#### fs_tell

Get current file position.

```c
off_t fs_tell(struct fs_file_t *zfp);
```

**Returns:** Current position, or negative errno on error.

#### fs_truncate

Truncate or extend file.

```c
int fs_truncate(struct fs_file_t *zfp, off_t length);
```

**Note:** Extension fills with zeros. If disk full during extension, extends to maximum possible and returns success.

#### fs_sync

Flush cached data to storage.

```c
int fs_sync(struct fs_file_t *zfp);
```

**Note:** Not needed before `fs_close()` which flushes automatically.

### Directory Operations

#### fs_dir_t_init

Initialize directory handle. **Must be called before `fs_opendir()`.**

```c
static inline void fs_dir_t_init(struct fs_dir_t *zdp);
```

#### fs_opendir

Open directory for reading.

```c
int fs_opendir(struct fs_dir_t *zdp, const char *path);
```

#### fs_readdir

Read next directory entry.

```c
int fs_readdir(struct fs_dir_t *zdp, struct fs_dirent *entry);
```

**Returns:** `0` on success. End-of-directory when `entry->name[0] == 0`.

**Note:** "." and ".." entries are filtered out for consistency.

**Example:**
```c
struct fs_dir_t dir;
struct fs_dirent entry;

fs_dir_t_init(&dir);
if (fs_opendir(&dir, "/lfs") == 0) {
    while (fs_readdir(&dir, &entry) == 0 && entry.name[0] != 0) {
        const char *type = (entry.type == FS_DIR_ENTRY_DIR) ? "DIR" : "FILE";
        printk("[%s] %s (%zu)\n", type, entry.name, entry.size);
    }
    fs_closedir(&dir);
}
```

#### fs_closedir

Close directory.

```c
int fs_closedir(struct fs_dir_t *zdp);
```

#### fs_mkdir

Create directory.

```c
int fs_mkdir(const char *path);
```

**Returns:**
- `0` on success
- `-EEXIST` if already exists
- `-EROFS` if read-only

#### fs_unlink

Delete file or empty directory.

```c
int fs_unlink(const char *path);
```

#### fs_rename

Rename or move file/directory.

```c
int fs_rename(const char *from, const char *to);
```

**Note:** Cannot move between mount points. Destination is overwritten if exists.

#### fs_stat

Get file/directory information.

```c
int fs_stat(const char *path, struct fs_dirent *entry);
```

**Example:**
```c
struct fs_dirent entry;
if (fs_stat("/lfs/data.bin", &entry) == 0) {
    printk("Size: %zu bytes\n", entry.size);
}
```

### Mount Operations

#### fs_mount

Mount a file system.

```c
int fs_mount(struct fs_mount_t *mp);
```

**Returns:**
- `0` on success
- `-ENOENT` if FS type not registered
- `-ENOTSUP` if mount not supported
- `-EROFS` if format needed but mounted read-only

**Example:**
```c
static struct fs_mount_t lfs_mnt = {
    .type = FS_LITTLEFS,
    .mnt_point = "/lfs",
    .fs_data = &lfs_config,
    .storage_dev = (void *)FIXED_PARTITION_ID(storage_partition),
};

int rc = fs_mount(&lfs_mnt);
```

#### fs_unmount

Unmount a file system.

```c
int fs_unmount(struct fs_mount_t *mp);
```

#### fs_readmount

Iterate through mount points.

```c
int fs_readmount(int *index, const char **name);
```

**Example:**
```c
int index = 0;
const char *name;
while (fs_readmount(&index, &name) == 0) {
    printk("Mounted: %s\n", name);
}
```

#### fs_statvfs

Get volume statistics.

```c
int fs_statvfs(const char *path, struct fs_statvfs *stat);
```

**Example:**
```c
struct fs_statvfs stat;
if (fs_statvfs("/lfs", &stat) == 0) {
    size_t total = stat.f_blocks * stat.f_frsize;
    size_t free = stat.f_bfree * stat.f_frsize;
    printk("Total: %zu bytes, Free: %zu bytes\n", total, free);
}
```

#### fs_mkfs

Format storage with file system.

```c
int fs_mkfs(int fs_type, uintptr_t dev_id, void *cfg, int flags);
```

**Requires:** `CONFIG_FILE_SYSTEM_MKFS=y`

**Parameters:**
- `fs_type`: `FS_LITTLEFS`, `FS_FATFS`, etc.
- `dev_id`: Device ID (partition ID or disk name pointer)
- `cfg`: FS-specific config (NULL for defaults)
- `flags`: Additional flags

**Example:**
```c
int rc = fs_mkfs(FS_LITTLEFS,
                  (uintptr_t)FIXED_PARTITION_ID(storage_partition),
                  NULL, 0);
```

#### fs_gc

Trigger garbage collection (LittleFS).

```c
int fs_gc(struct fs_mount_t *mp);
```

**Note:** Only meaningful for file systems with garbage collection (LittleFS).

### Registration API

For implementing custom file systems.

#### fs_register

Register a file system type.

```c
int fs_register(int type, const struct fs_file_system_t *fs);
```

#### fs_unregister

Unregister a file system type.

```c
int fs_unregister(int type, const struct fs_file_system_t *fs);
```

### Fstab Macros

For working with devicetree-defined mount points.

#### FSTAB_ENTRY_DT_MOUNT_FLAGS

Get mount flags from fstab node.

```c
#define FSTAB_ENTRY_DT_MOUNT_FLAGS(node_id)
```

#### FS_FSTAB_ENTRY

Get the mount structure name for an fstab node.

```c
#define FS_FSTAB_ENTRY(node_id)
```

#### FS_FSTAB_DECLARE_ENTRY

Declare an external fstab mount structure.

```c
#define FS_FSTAB_DECLARE_ENTRY(node_id)
```

**Example:**
```c
#define PARTITION_NODE DT_NODELABEL(lfs1)

FS_FSTAB_DECLARE_ENTRY(PARTITION_NODE);

void use_fstab_mount(void) {
    struct fs_mount_t *mp = &FS_FSTAB_ENTRY(PARTITION_NODE);
    // mp is automatically mounted if automount is set
}
```

### Common Error Codes

| Error | Meaning |
|-------|---------|
| `-ENOENT` | File/directory not found |
| `-EEXIST` | Entry already exists |
| `-EBUSY` | Handle already in use |
| `-EINVAL` | Invalid argument/path |
| `-EROFS` | Read-only file system |
| `-ENOSPC` | No space left on device |
| `-ENOTSUP` | Operation not supported |
| `-EBADF` | Bad file descriptor (unopened file) |
| `-EACCES` | Permission denied |

### Usage Patterns

#### Safe File Read

```c
int read_file(const char *path, void *buf, size_t buf_size, size_t *bytes_read)
{
    struct fs_file_t file;
    fs_file_t_init(&file);

    int rc = fs_open(&file, path, FS_O_READ);
    if (rc < 0) {
        return rc;
    }

    ssize_t len = fs_read(&file, buf, buf_size);
    fs_close(&file);

    if (len < 0) {
        return len;
    }

    *bytes_read = len;
    return 0;
}
```

#### Safe File Write

```c
int write_file(const char *path, const void *data, size_t len)
{
    struct fs_file_t file;
    fs_file_t_init(&file);

    int rc = fs_open(&file, path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
    if (rc < 0) {
        return rc;
    }

    ssize_t written = fs_write(&file, data, len);
    int close_rc = fs_close(&file);

    if (written < 0) {
        return written;
    }
    if (written != len) {
        return -ENOSPC;
    }

    return close_rc;
}
```

#### Check If File Exists

```c
bool file_exists(const char *path)
{
    struct fs_dirent entry;
    return fs_stat(path, &entry) == 0;
}
```

## Fat

FAT (File Allocation Table) filesystem using ELM FatFs library. Provides PC compatibility for removable media like SD cards and USB drives.

### When to Use

- SD cards
- USB mass storage
- Need to exchange files with PC/Mac/Linux
- Removable media
- Don't need power-loss resilience

### Kconfig Options

```kconfig
# Required
CONFIG_FILE_SYSTEM=y
CONFIG_FAT_FILESYSTEM_ELM=y
CONFIG_DISK_ACCESS=y         # Auto-selected

# Long file names (recommended)
CONFIG_FS_FATFS_LFN=y
CONFIG_FS_FATFS_MAX_LFN=255

# LFN memory mode (choose one)
CONFIG_FS_FATFS_LFN_MODE_BSS=y    # Static buffer (not thread-safe)
CONFIG_FS_FATFS_LFN_MODE_STACK=y  # Stack buffer (thread-safe)
CONFIG_FS_FATFS_LFN_MODE_HEAP=y   # Heap buffer (thread-safe)

# exFAT support (for >32GB)
CONFIG_FS_FATFS_EXFAT=y

# Formatting support
CONFIG_FS_FATFS_MKFS=y
CONFIG_FS_FATFS_MOUNT_MKFS=y  # Auto-format on failed mount

# File/directory limits
CONFIG_FS_FATFS_NUM_FILES=4
CONFIG_FS_FATFS_NUM_DIRS=4

# Sector size (match your hardware)
CONFIG_FS_FATFS_MAX_SS=512
CONFIG_FS_FATFS_MIN_SS=512

# Code page (character set)
CONFIG_FS_FATFS_CODEPAGE=437  # US English

# Thread safety
CONFIG_FS_FATFS_REENTRANT=y

# Read-only (reduces code size)
CONFIG_FS_FATFS_READ_ONLY=y
```

### Mount Points

FAT FS has restricted mount point names. Valid options:

- `/RAM:`, `/NAND:`, `/CF:`, `/SD:`, `/SD2:`
- `/USB:`, `/USB2:`, `/USB3:`
- Single digits: `/0:`, `/1:`, `/2:`, etc.

Or use `CONFIG_FS_FATFS_CUSTOM_MOUNT_POINTS` for custom names.

### SD Card Setup

#### With SDHC/SDMMC Controller

```c
#include <zephyr/fs/fs.h>
#include <zephyr/storage/disk_access.h>
#include <ff.h>

static FATFS fat_fs;
static struct fs_mount_t mount_point = {
    .type = FS_FATFS,
    .fs_data = &fat_fs,
    .mnt_point = "/SD:",
};

int mount_sd_card(void)
{
    static const char *disk_name = "SD";
    int rc;

    /* Initialize disk (optional - mount does this too) */
    rc = disk_access_init(disk_name);
    if (rc != 0) {
        printk("Disk init failed: %d\n", rc);
        return rc;
    }

    /* Mount filesystem */
    rc = fs_mount(&mount_point);
    if (rc != 0) {
        printk("Mount failed: %d\n", rc);
        return rc;
    }

    printk("SD card mounted at %s\n", mount_point.mnt_point);
    return 0;
}
```

#### With SPI SD Card

Add to prj.conf:
```kconfig
CONFIG_SPI=y
CONFIG_DISK_DRIVER_SDMMC=y
CONFIG_SDMMC_STACK=y
CONFIG_SDHC=y
```

Devicetree overlay:
```dts
&spi1 {
    status = "okay";
    cs-gpios = <&gpio0 4 GPIO_ACTIVE_LOW>;

    sdhc0: sdhc@0 {
        compatible = "zephyr,sdhc-spi-slot";
        reg = <0>;
        spi-max-frequency = <25000000>;
        mmc {
            compatible = "zephyr,sdmmc-disk";
            disk-name = "SD";
        };
    };
};
```

### Complete Example

```c
#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/disk_access.h>
#include <ff.h>

#define DISK_NAME "SD"
#define MOUNT_POINT "/SD:"

static FATFS fat_fs;
static struct fs_mount_t mp = {
    .type = FS_FATFS,
    .fs_data = &fat_fs,
    .mnt_point = MOUNT_POINT,
};

static int list_directory(const char *path)
{
    struct fs_dir_t dir;
    struct fs_dirent entry;
    int rc;

    fs_dir_t_init(&dir);
    rc = fs_opendir(&dir, path);
    if (rc != 0) {
        return rc;
    }

    printk("Contents of %s:\n", path);
    while (fs_readdir(&dir, &entry) == 0 && entry.name[0] != 0) {
        if (entry.type == FS_DIR_ENTRY_DIR) {
            printk("  [DIR]  %s\n", entry.name);
        } else {
            printk("  [FILE] %s (%zu bytes)\n", entry.name, entry.size);
        }
    }

    fs_closedir(&dir);
    return 0;
}

int main(void)
{
    struct fs_file_t file;
    uint32_t block_count, block_size;
    int rc;

    /* Get disk info */
    disk_access_init(DISK_NAME);
    disk_access_ioctl(DISK_NAME, DISK_IOCTL_GET_SECTOR_COUNT, &block_count);
    disk_access_ioctl(DISK_NAME, DISK_IOCTL_GET_SECTOR_SIZE, &block_size);
    printk("SD Card: %u sectors of %u bytes = %u MB\n",
           block_count, block_size, (block_count * block_size) >> 20);

    /* Mount */
    rc = fs_mount(&mp);
    if (rc != 0) {
        printk("Mount failed: %d\n", rc);
        return 0;
    }

    /* List root */
    list_directory(MOUNT_POINT);

    /* Create a file */
    fs_file_t_init(&file);
    rc = fs_open(&file, MOUNT_POINT "/hello.txt", FS_O_CREATE | FS_O_WRITE);
    if (rc == 0) {
        fs_write(&file, "Hello from Zephyr!\n", 19);
        fs_close(&file);
        printk("Created hello.txt\n");
    }

    /* Unmount before removing card */
    fs_unmount(&mp);
    printk("Unmounted\n");

    return 0;
}
```

### Devicetree Fstab (Automount)

```dts
/ {
    fstab {
        compatible = "zephyr,fstab";
        fatfs: fatfs {
            compatible = "zephyr,fstab,fatfs";
            mount-point = "/SD:";
            disk-name = "SD";
            automount;
        };
    };
};
```

Enable in Kconfig:
```kconfig
CONFIG_FS_FATFS_FSTAB_AUTOMOUNT=y
```

### Multi-Partition Support

For multiple FAT partitions on one disk:

```kconfig
CONFIG_FS_FATFS_MULTI_PARTITION=y
```

Define `VolToPart[]` in your application:
```c
#include <ff.h>

/* Map logical drives to physical partitions */
PARTITION VolToPart[] = {
    {3, 1},  /* /0: = Physical drive 3, partition 1 */
    {3, 2},  /* /1: = Physical drive 3, partition 2 */
};
```

### Format SD Card

```c
#include <ff.h>

/* Format as FAT32 */
BYTE work_buf[512];
MKFS_PARM mkfs_opt = {
    .fmt = FM_FAT32,
    .n_fat = 2,      /* Number of FATs */
    .align = 0,      /* Auto-align */
    .n_root = 512,   /* Root directory entries */
    .au_size = 0,    /* Auto cluster size */
};

FRESULT res = f_mkfs("SD:", &mkfs_opt, work_buf, sizeof(work_buf));
```

Or using Zephyr VFS:
```c
fs_mkfs(FS_FATFS, (uintptr_t)"SD:", NULL, 0);
```

### Troubleshooting

#### Mount Returns -ENOENT

SD card not detected:
- Check SPI/SDIO connections
- Verify CS GPIO configuration
- Try lower SPI frequency
- Check power supply (SD cards need stable 3.3V)

#### Long File Names Not Working

Enable in Kconfig:
```kconfig
CONFIG_FS_FATFS_LFN=y
CONFIG_FS_FATFS_MAX_LFN=255
```

#### Files Not Visible on PC

- Call `fs_sync()` before removing card
- Always `fs_unmount()` before removal
- Check that LFN is enabled for long names

## Fcb

FCB provides a log-style storage mechanism optimized for flash. Data is appended to a circular buffer that automatically rotates through flash sectors, providing wear leveling and power-loss resilience.

### When to Use

- Logging/journaling data
- Sensor data history
- Event logs
- Append-only data patterns
- Need power-loss safety
- Need to minimize flash wear

### When NOT to Use

- Random access file storage → Use LittleFS
- PC-compatible files → Use FAT
- Key-value storage → Use NVS/ZMS (zephyr-storage skill)

### Kconfig Options

```kconfig
CONFIG_FCB=y
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_FLASH_PAGE_LAYOUT=y
CONFIG_CRC=y              # Auto-selected

# Optional: disable CRC for faster writes
CONFIG_FCB_ALLOW_FIXED_ENDMARKER=y
```

### Key Concepts

- **Sector**: Flash erase unit. FCB rotates through sectors.
- **Entry**: Single data record with length and optional CRC.
- **Scratch sector**: Reserved for garbage collection (configurable count).
- **Oldest/Active**: FCB tracks oldest and newest data positions.

### Data Structure

```c
#include <zephyr/fs/fcb.h>

struct fcb my_fcb = {
    .f_magic = 0x12345678,   /* Unique magic (not 0xFFFFFFFF) */
    .f_version = 1,          /* Data format version */
    .f_sector_cnt = 4,       /* Number of sectors */
    .f_scratch_cnt = 1,      /* Sectors to keep empty */
    .f_sectors = sectors,    /* Array of flash_sector structs */
};
```

### Basic Usage

#### Initialization

```c
#include <zephyr/fs/fcb.h>
#include <zephyr/storage/flash_map.h>

#define FCB_FLASH_AREA_ID FIXED_PARTITION_ID(storage_partition)

static struct flash_sector sectors[4];
static struct fcb my_fcb;

int init_fcb(void)
{
    int rc;
    const struct flash_area *fa;
    uint32_t sector_cnt = ARRAY_SIZE(sectors);

    /* Get sector layout from flash area */
    rc = flash_area_open(FCB_FLASH_AREA_ID, &fa);
    if (rc) {
        return rc;
    }

    rc = flash_area_get_sectors(FCB_FLASH_AREA_ID, &sector_cnt, sectors);
    flash_area_close(fa);
    if (rc) {
        return rc;
    }

    /* Initialize FCB */
    my_fcb.f_magic = 0xABCD1234;
    my_fcb.f_version = 1;
    my_fcb.f_sector_cnt = sector_cnt;
    my_fcb.f_scratch_cnt = 1;
    my_fcb.f_sectors = sectors;

    rc = fcb_init(FCB_FLASH_AREA_ID, &my_fcb);
    if (rc) {
        /* Clear and retry if corrupted */
        fcb_clear(&my_fcb);
        rc = fcb_init(FCB_FLASH_AREA_ID, &my_fcb);
    }

    return rc;
}
```

#### Writing Entries

```c
int write_log_entry(const void *data, uint16_t len)
{
    struct fcb_entry loc;
    int rc;

    /* Reserve space for entry */
    rc = fcb_append(&my_fcb, len, &loc);
    if (rc == -ENOSPC) {
        /* No space - rotate and retry */
        fcb_rotate(&my_fcb);
        rc = fcb_append(&my_fcb, len, &loc);
    }
    if (rc) {
        return rc;
    }

    /* Write data */
    rc = fcb_flash_write(&my_fcb, loc.fe_sector, loc.fe_data_off, data, len);
    if (rc) {
        return rc;
    }

    /* Finalize entry (writes length/CRC header) */
    rc = fcb_append_finish(&my_fcb, &loc);
    return rc;
}
```

#### Reading Entries

```c
/* Callback for walking entries */
static int process_entry(struct fcb_entry_ctx *ctx, void *arg)
{
    uint8_t buf[256];
    int rc;

    /* Read entry data */
    rc = fcb_flash_read(&my_fcb, ctx->loc.fe_sector,
                        ctx->loc.fe_data_off, buf, ctx->loc.fe_data_len);
    if (rc == 0) {
        /* Process buf[0..fe_data_len-1] */
        printk("Entry: %u bytes\n", ctx->loc.fe_data_len);
    }

    return 0;  /* Return non-zero to stop walk */
}

/* Walk all entries (oldest to newest) */
void read_all_entries(void)
{
    fcb_walk(&my_fcb, NULL, process_entry, NULL);
}

/* Get specific entry by index from end */
int read_last_n_entry(uint8_t n, void *buf, size_t buf_len)
{
    struct fcb_entry loc;
    int rc;

    rc = fcb_offset_last_n(&my_fcb, n, &loc);
    if (rc) {
        return rc;
    }

    return fcb_flash_read(&my_fcb, loc.fe_sector, loc.fe_data_off,
                          buf, MIN(buf_len, loc.fe_data_len));
}
```

#### Rotation and Cleanup

```c
/* Manual rotation (erase oldest sector) */
fcb_rotate(&my_fcb);

/* Check free sectors */
int free = fcb_free_sector_cnt(&my_fcb);

/* Clear all data */
fcb_clear(&my_fcb);

/* Check if empty */
if (fcb_is_empty(&my_fcb)) {
    printk("FCB is empty\n");
}
```

### Complete Example

```c
#include <zephyr/kernel.h>
#include <zephyr/fs/fcb.h>
#include <zephyr/storage/flash_map.h>

#define FCB_AREA_ID FIXED_PARTITION_ID(storage_partition)

static struct flash_sector sectors[4];
static struct fcb log_fcb;

struct log_entry {
    uint32_t timestamp;
    int16_t temperature;
    uint16_t humidity;
};

static int print_log(struct fcb_entry_ctx *ctx, void *arg)
{
    struct log_entry entry;

    if (ctx->loc.fe_data_len == sizeof(entry)) {
        fcb_flash_read(&log_fcb, ctx->loc.fe_sector,
                       ctx->loc.fe_data_off, &entry, sizeof(entry));
        printk("[%u] Temp: %d.%d C, Humidity: %u%%\n",
               entry.timestamp,
               entry.temperature / 10, entry.temperature % 10,
               entry.humidity);
    }
    return 0;
}

int main(void)
{
    uint32_t sector_cnt = ARRAY_SIZE(sectors);
    struct log_entry entry;
    struct fcb_entry loc;
    int rc;

    /* Initialize sectors */
    flash_area_get_sectors(FCB_AREA_ID, &sector_cnt, sectors);

    /* Configure FCB */
    log_fcb.f_magic = 0xLOG12345;
    log_fcb.f_version = 1;
    log_fcb.f_sector_cnt = sector_cnt;
    log_fcb.f_scratch_cnt = 1;
    log_fcb.f_sectors = sectors;

    /* Initialize */
    rc = fcb_init(FCB_AREA_ID, &log_fcb);
    if (rc) {
        printk("FCB init failed: %d, clearing...\n", rc);
        fcb_clear(&log_fcb);
        fcb_init(FCB_AREA_ID, &log_fcb);
    }

    /* Show existing entries */
    printk("Existing log entries:\n");
    fcb_walk(&log_fcb, NULL, print_log, NULL);

    /* Add new entry */
    entry.timestamp = k_uptime_get_32() / 1000;
    entry.temperature = 235;  /* 23.5 C */
    entry.humidity = 45;

    rc = fcb_append(&log_fcb, sizeof(entry), &loc);
    if (rc == -ENOSPC) {
        fcb_rotate(&log_fcb);
        rc = fcb_append(&log_fcb, sizeof(entry), &loc);
    }

    if (rc == 0) {
        fcb_flash_write(&log_fcb, loc.fe_sector, loc.fe_data_off,
                        &entry, sizeof(entry));
        fcb_append_finish(&log_fcb, &loc);
        printk("Added log entry\n");
    }

    printk("Free sectors: %d\n", fcb_free_sector_cnt(&log_fcb));

    return 0;
}
```

### API Reference

| Function | Description |
|----------|-------------|
| `fcb_init(area_id, fcb)` | Initialize FCB on flash area |
| `fcb_append(fcb, len, loc)` | Reserve space for new entry |
| `fcb_append_finish(fcb, loc)` | Finalize appended entry |
| `fcb_walk(fcb, sector, cb, arg)` | Walk entries (NULL sector = all) |
| `fcb_getnext(fcb, loc)` | Get next entry location |
| `fcb_rotate(fcb)` | Erase oldest sector |
| `fcb_clear(fcb)` | Erase all sectors |
| `fcb_is_empty(fcb)` | Check if FCB is empty |
| `fcb_free_sector_cnt(fcb)` | Count free sectors |
| `fcb_offset_last_n(fcb, n, loc)` | Get nth entry from end |
| `fcb_flash_read(fcb, sector, off, dst, len)` | Read from FCB flash |
| `fcb_flash_write(fcb, sector, off, src, len)` | Write to FCB flash |

### Sizing Considerations

- Maximum entry size: 16,383 bytes (`FCB_MAX_LEN`)
- Entry overhead: ~8 bytes per entry
- Reserve 1+ sectors for garbage collection (`f_scratch_cnt`)
- Total storage ≈ (sector_cnt - scratch_cnt) × sector_size

### Troubleshooting

#### fcb_init Returns -ENOMEM

Sectors array too small or sector_cnt wrong:
```c
uint32_t sector_cnt = ARRAY_SIZE(sectors);
flash_area_get_sectors(FCB_AREA_ID, &sector_cnt, sectors);
```

#### fcb_append Returns -ENOSPC

No space left. Rotate and retry:
```c
fcb_rotate(&fcb);
fcb_append(&fcb, len, &loc);
```

#### Data Corruption After Power Loss

FCB is designed to handle this. If `fcb_init` fails:
```c
fcb_clear(&fcb);
fcb_init(area_id, &fcb);
```

## Littlefs

LittleFS is a power-loss resilient filesystem designed for embedded systems with flash storage. It provides wear leveling and handles power failures gracefully.

### When to Use

- Internal MCU flash storage
- External SPI/QSPI flash
- Need power-loss resilience
- Need wear leveling
- Don't need PC compatibility

### Kconfig Options

```kconfig
# Required
CONFIG_FILE_SYSTEM=y
CONFIG_FILE_SYSTEM_LITTLEFS=y

# Flash support (choose based on hardware)
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_FLASH_PAGE_LAYOUT=y  # For explicit erase flash

# Performance tuning
CONFIG_FS_LITTLEFS_NUM_FILES=4       # Max concurrent open files
CONFIG_FS_LITTLEFS_NUM_DIRS=4        # Max concurrent open directories
CONFIG_FS_LITTLEFS_READ_SIZE=16      # Min read size
CONFIG_FS_LITTLEFS_PROG_SIZE=16      # Min program size
CONFIG_FS_LITTLEFS_CACHE_SIZE=64     # Cache size (RAM per file)
CONFIG_FS_LITTLEFS_LOOKAHEAD_SIZE=32 # Lookahead buffer
CONFIG_FS_LITTLEFS_BLOCK_CYCLES=512  # Erase cycles before moving data

# Block device support (for SD cards via LittleFS)
CONFIG_FS_LITTLEFS_BLK_DEV=y

# Disk version compatibility
CONFIG_FS_LITTLEFS_DISK_VERSION=y
```

### Mount Point Setup

#### Option 1: Manual Mount (Programmatic)

```c
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>

/* Declare LittleFS config with default Kconfig values */
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(storage);

static struct fs_mount_t lfs_mount = {
    .type = FS_LITTLEFS,
    .fs_data = &storage,
    .storage_dev = (void *)FIXED_PARTITION_ID(storage_partition),
    .mnt_point = "/lfs",
};

int init_filesystem(void)
{
    int rc = fs_mount(&lfs_mount);
    if (rc < 0) {
        printk("Mount failed: %d\n", rc);
        /* Try formatting */
        rc = fs_mkfs(FS_LITTLEFS, (uintptr_t)lfs_mount.storage_dev, NULL, 0);
        if (rc == 0) {
            rc = fs_mount(&lfs_mount);
        }
    }
    return rc;
}
```

#### Option 2: Devicetree Fstab (Automount)

```dts
/* In board overlay or app overlay */
/ {
    fstab {
        compatible = "zephyr,fstab";
        lfs1: lfs1 {
            compatible = "zephyr,fstab,littlefs";
            mount-point = "/lfs";
            partition = <&storage_partition>;
            automount;
            read-size = <16>;
            prog-size = <16>;
            cache-size = <64>;
            lookahead-size = <32>;
            block-cycles = <512>;
        };
    };
};

&flash0 {
    partitions {
        compatible = "fixed-partitions";
        #address-cells = <1>;
        #size-cells = <1>;

        storage_partition: partition@70000 {
            label = "storage";
            reg = <0x70000 0x10000>;  /* 64KB */
        };
    };
};
```

Access in code:

```c
#include <zephyr/fs/fs.h>

#define PARTITION_NODE DT_NODELABEL(lfs1)

#if DT_NODE_EXISTS(PARTITION_NODE)
FS_FSTAB_DECLARE_ENTRY(PARTITION_NODE);

void use_filesystem(void)
{
    struct fs_mount_t *mp = &FS_FSTAB_ENTRY(PARTITION_NODE);
    /* Already mounted if automount is set */

    struct fs_file_t file;
    fs_file_t_init(&file);
    fs_open(&file, "/lfs/test.txt", FS_O_CREATE | FS_O_WRITE);
    fs_write(&file, "data", 4);
    fs_close(&file);
}
#endif
```

#### Option 3: Custom Configuration

```c
/* Custom cache sizes different from Kconfig defaults */
FS_LITTLEFS_DECLARE_CUSTOM_CONFIG(my_lfs,
    4,      /* alignment */
    16,     /* read_sz */
    16,     /* prog_sz */
    256,    /* cache_sz - larger cache for performance */
    64      /* lookahead_sz */
);

static struct fs_mount_t custom_mount = {
    .type = FS_LITTLEFS,
    .fs_data = &my_lfs,
    .storage_dev = (void *)FIXED_PARTITION_ID(storage_partition),
    .mnt_point = "/custom",
};
```

### Complete Example

```c
#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>

FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(storage);

static struct fs_mount_t lfs_mnt = {
    .type = FS_LITTLEFS,
    .fs_data = &storage,
    .storage_dev = (void *)FIXED_PARTITION_ID(storage_partition),
    .mnt_point = "/lfs",
};

int main(void)
{
    struct fs_file_t file;
    struct fs_statvfs stat;
    uint32_t boot_count = 0;
    char path[64];
    int rc;

    /* Mount filesystem */
    rc = fs_mount(&lfs_mnt);
    if (rc != 0) {
        printk("Mount error: %d, formatting...\n", rc);
        fs_mkfs(FS_LITTLEFS, (uintptr_t)lfs_mnt.storage_dev, NULL, 0);
        rc = fs_mount(&lfs_mnt);
    }

    /* Check space */
    fs_statvfs(lfs_mnt.mnt_point, &stat);
    printk("FS: %lu/%lu blocks free\n", stat.f_bfree, stat.f_blocks);

    /* Read/update boot counter */
    snprintf(path, sizeof(path), "%s/boot_count", lfs_mnt.mnt_point);

    fs_file_t_init(&file);
    rc = fs_open(&file, path, FS_O_CREATE | FS_O_RDWR);
    if (rc == 0) {
        fs_read(&file, &boot_count, sizeof(boot_count));
        boot_count++;
        fs_seek(&file, 0, FS_SEEK_SET);
        fs_write(&file, &boot_count, sizeof(boot_count));
        fs_close(&file);
    }

    printk("Boot count: %u\n", boot_count);
    return 0;
}
```

### Sizing Considerations

#### RAM Usage

Per-file cache memory:
- `cache_size` bytes per open file
- Plus `lookahead_size` bytes global
- Plus internal structures (~100 bytes)

Formula: `RAM ≈ NUM_FILES × cache_size + lookahead_size + overhead`

#### Flash Usage

- Metadata overhead: ~4 bytes per 128 bytes of data
- Block size should match flash erase size
- Minimum 2 blocks required

### Troubleshooting

#### Mount Fails with -ENODEV

Flash device not ready or partition not found:
```c
const struct device *flash = FIXED_PARTITION_DEVICE(storage_partition);
if (!device_is_ready(flash)) {
    printk("Flash not ready\n");
}
```

#### Mount Fails with -ENOENT

Filesystem not formatted. Format first:
```c
fs_mkfs(FS_LITTLEFS, (uintptr_t)FIXED_PARTITION_ID(storage_partition), NULL, 0);
```

#### Writes Fail

- Check `fs_statvfs()` for free space
- Verify partition is large enough
- Check flash write protection

## Locations

Quick reference for Zephyr file system source code, headers, and samples.

### Headers

| Component | Location |
|-----------|----------|
| VFS API | `zephyr/include/zephyr/fs/fs.h` |
| VFS interface | `zephyr/include/zephyr/fs/fs_interface.h` |
| LittleFS | `zephyr/include/zephyr/fs/littlefs.h` |
| FCB | `zephyr/include/zephyr/fs/fcb.h` |

### Implementation

| Component | Location |
|-----------|----------|
| VFS core | `zephyr/subsys/fs/` |
| LittleFS wrapper | `zephyr/subsys/fs/littlefs_fs.c` |
| FAT/FatFs wrapper | `zephyr/subsys/fs/fat_fs.c` |
| ext2 wrapper | `zephyr/subsys/fs/ext2/` |
| FCB | `zephyr/subsys/fs/fcb/` |
| Shell commands | `zephyr/subsys/fs/shell.c` |

### Kconfig

| Component | Location |
|-----------|----------|
| Main FS config | `zephyr/subsys/fs/Kconfig` |
| LittleFS config | `zephyr/subsys/fs/Kconfig.littlefs` |
| FAT/FatFs config | `zephyr/subsys/fs/Kconfig.fatfs` |
| ext2 config | `zephyr/subsys/fs/Kconfig.ext2` |
| FCB config | `zephyr/subsys/fs/fcb/Kconfig` |

### Samples

#### LittleFS Sample

**Location:** `zephyr/samples/subsys/fs/littlefs/`

Demonstrates LittleFS on internal flash with fstab automount.

```bash
west build -b <board> samples/subsys/fs/littlefs
```

**Key files:**
- `src/main.c` - Boot counter, file I/O example
- `boards/*.overlay` - Board-specific flash partition configs

#### FAT/Disk Sample

**Location:** `zephyr/samples/subsys/fs/fs_sample/`

Demonstrates FAT filesystem on SD card or disk.

```bash
west build -b <board> samples/subsys/fs/fs_sample
```

**Key files:**
- `src/main.c` - Disk access and FAT mount example
- `prj_ext.conf` - ext2 filesystem variant

#### Format Sample

**Location:** `zephyr/samples/subsys/fs/format/`

Demonstrates programmatic filesystem formatting.

```bash
west build -b <board> samples/subsys/fs/format
```

#### ext2 with Fstab Sample

**Location:** `zephyr/samples/subsys/fs/ext2_fstab/`

Demonstrates ext2 filesystem with devicetree fstab configuration.

```bash
west build -b <board> samples/subsys/fs/ext2_fstab
```

#### VirtioFS Sample

**Location:** `zephyr/samples/subsys/fs/virtiofs/`

Demonstrates filesystem access via virtio (QEMU).

```bash
west build -b qemu_x86 samples/subsys/fs/virtiofs
```

### Tests

| Component | Location |
|-----------|----------|
| VFS tests | `zephyr/tests/subsys/fs/` |
| LittleFS tests | `zephyr/tests/subsys/fs/littlefs/` |
| FAT tests | `zephyr/tests/subsys/fs/fat_fs_api/` |
| FCB tests | `zephyr/tests/subsys/fs/fcb/` |
| ext2 tests | `zephyr/tests/subsys/fs/ext2/` |

### Documentation

| Topic | Location |
|-------|----------|
| File System guide | `zephyr/doc/services/file_system/` |
| API docs | `zephyr/doc/services/file_system/api/` |

### Third-Party Libraries

Zephyr wraps these external libraries:

| Library | Zephyr Location | Upstream |
|---------|-----------------|----------|
| LittleFS | `modules/fs/littlefs/` | github.com/littlefs-project/littlefs |
| FatFs | `modules/fs/fatfs/` | elm-chan.org/fsw/ff/ |

### Common Board Configurations

Many samples include board-specific configurations in `boards/` subdirectory:

```
samples/subsys/fs/littlefs/boards/
├── nrf52840dk_nrf52840_qspi.overlay    # QSPI flash
├── nrf52840dk_nrf52840_spi.overlay     # SPI flash
├── stm32f746g_disco.overlay            # STM32 internal flash
└── ...
```

These overlays typically define:
- Flash partition layout
- Storage partition for filesystem
- Fstab mount point configuration
