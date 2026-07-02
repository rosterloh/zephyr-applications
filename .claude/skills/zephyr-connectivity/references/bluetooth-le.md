# Bluetooth Low Energy

## Overview

### Quick Start

1. **Enable BLE**: `CONFIG_BT=y` in `prj.conf`
2. **Choose GAP Role**: See [GAP Roles](#gap-roles) section
3. **Initialize**: Call `bt_enable(NULL)` in `main()`
4. **Define Services**: Use GATT macros or built-in services
5. **Start Advertising/Scanning**: Based on role

### Core Initialization Pattern

```c
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>

int main(void)
{
    int err = bt_enable(NULL);
    if (err) {
        printk("Bluetooth init failed (err %d)\n", err);
        return 0;
    }

    /* If using persistent storage */
    if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
        settings_load();
    }

    /* Register connection callbacks, start advertising, etc. */
}
```

### GAP Roles

| Role | Kconfig | Description | Key APIs |
|------|---------|-------------|----------|
| Peripheral | `CONFIG_BT_PERIPHERAL=y` | Connectable advertiser, GATT server | `bt_le_adv_start()`, `bt_gatt_service_register()` |
| Central | `CONFIG_BT_CENTRAL=y` | Scans and connects, GATT client | `bt_le_scan_start()`, `bt_conn_le_create()` |
| Broadcaster | `CONFIG_BT_BROADCASTER=y` | Non-connectable advertiser | `bt_le_adv_start()` with non-conn params |
| Observer | `CONFIG_BT_OBSERVER=y` | Passive scanner | `bt_le_scan_start()` |

### Detailed References

- **GAP (Advertising, Scanning, Connections)**: [#gap](#gap)
- **GATT (Services, Characteristics, Client/Server)**: [#gatt](#gatt)
- **Built-in Services (BAS, DIS, HRS, NUS, OTS)**: [#services](#services)
- **Kconfig Options**: [#kconfig](#kconfig)
- **Resource Locations**: [#locations](#locations)

### Common Patterns

#### Peripheral (GATT Server)

```c
/* Advertising data */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_HRS_VAL)),
};

static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
            sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

/* Start connectable advertising. BT_LE_ADV_CONN was removed; use the
 * GAP-recommended fast advertising parameters. Use BT_LE_ADV_CONN_FAST_2
 * if you need the slower interval, or BT_LE_ADV_NCONN for non-connectable.
 */
err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
```

#### Central (GATT Client)

```c
/* Scan callback */
static void scan_cb(const bt_addr_le_t *addr, int8_t rssi,
                    uint8_t adv_type, struct net_buf_simple *buf)
{
    /* Check advertising data, connect if target found */
    bt_le_scan_stop();
    bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN,
                      BT_LE_CONN_PARAM_DEFAULT, &conn);
}

/* Start scanning */
err = bt_le_scan_start(BT_LE_SCAN_ACTIVE, scan_cb);
```

#### Connection Callbacks

```c
static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        printk("Connection failed (err 0x%02x)\n", err);
    } else {
        printk("Connected\n");
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    printk("Disconnected (reason 0x%02x)\n", reason);
}

/* Static registration (preferred) */
BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};
```

### Minimum Kconfig for Peripheral

```
CONFIG_BT=y
CONFIG_BT_PERIPHERAL=y
CONFIG_BT_DEVICE_NAME="My Device"
```

### Minimum Kconfig for Central

```
CONFIG_BT=y
CONFIG_BT_CENTRAL=y
CONFIG_BT_GATT_CLIENT=y
```

### Security (Pairing/Bonding)

Enable SMP: `CONFIG_BT_SMP=y`

```c
/* Set security level on connection */
bt_conn_set_security(conn, BT_SECURITY_L2);  /* Encrypted, no MITM */

/* Authentication callbacks for pairing */
static struct bt_conn_auth_cb auth_cb = {
    .passkey_display = passkey_display,
    .passkey_confirm = passkey_confirm,
    .cancel = auth_cancel,
};
bt_conn_auth_cb_register(&auth_cb);
```

Security levels:
- `BT_SECURITY_L1`: No encryption
- `BT_SECURITY_L2`: Encrypted, no MITM protection
- `BT_SECURITY_L3`: Encrypted + MITM (legacy pairing)
- `BT_SECURITY_L4`: Encrypted + MITM (LE Secure Connections)

### Persistent Storage

For bonding persistence:

```
CONFIG_BT_SETTINGS=y
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_NVS=y
CONFIG_SETTINGS=y
```

Call `settings_load()` after `bt_enable()`.

### Related Skills

- **zephyr-kconfig**: Configure `CONFIG_BT_*` options
- **zephyr-devicetree**: Flash partitions for bonding storage
- **zephyr-shell-commands**: BLE shell for debugging (`CONFIG_BT_SHELL=y`)
- **zephyr-settings**: Persistent storage backend selection

## Gap

GAP defines device discovery, connection establishment, and security procedures.

### Table of Contents

- [Advertising](#advertising)
- [Scanning](#scanning)
- [Connections](#connections)
- [Extended Advertising](#extended-advertising)
- [Connection Parameters](#connection-parameters)

### Advertising

#### Legacy Advertising

```c
#include <zephyr/bluetooth/bluetooth.h>

/* Advertising data (max 31 bytes) */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL,
                  BT_UUID_16_ENCODE(BT_UUID_HRS_VAL),
                  BT_UUID_16_ENCODE(BT_UUID_BAS_VAL)),
};

/* Scan response data (max 31 bytes) */
static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
            sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

/* Start connectable advertising */
err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
```

#### Advertising Parameters

| Macro | Description |
|-------|-------------|
| `BT_LE_ADV_CONN` | Connectable, scannable |
| `BT_LE_ADV_CONN_FAST_1` | Fast connectable (30-60ms interval) |
| `BT_LE_ADV_CONN_FAST_2` | Fast connectable (100-150ms interval) |
| `BT_LE_ADV_NCONN` | Non-connectable, non-scannable |
| `BT_LE_ADV_NCONN_NAME` | Non-connectable with name in ad data |

#### Advertising Data Types

| Macro | Description |
|-------|-------------|
| `BT_DATA_FLAGS` | Advertising flags |
| `BT_DATA_UUID16_ALL` | Complete list of 16-bit UUIDs |
| `BT_DATA_UUID16_SOME` | Incomplete list of 16-bit UUIDs |
| `BT_DATA_UUID128_ALL` | Complete list of 128-bit UUIDs |
| `BT_DATA_NAME_COMPLETE` | Complete local name |
| `BT_DATA_NAME_SHORTENED` | Shortened local name |
| `BT_DATA_MANUFACTURER_DATA` | Manufacturer-specific data |
| `BT_DATA_SVC_DATA16` | Service data with 16-bit UUID |

#### Custom Advertising Parameters

```c
struct bt_le_adv_param param = {
    .id = BT_ID_DEFAULT,
    .options = BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_USE_NAME,
    .interval_min = BT_GAP_ADV_FAST_INT_MIN_1,  /* 30ms */
    .interval_max = BT_GAP_ADV_FAST_INT_MAX_1,  /* 60ms */
};

err = bt_le_adv_start(&param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
```

#### Stopping Advertising

```c
err = bt_le_adv_stop();
```

### Scanning

#### Basic Scanning

```c
static void scan_cb(const bt_addr_le_t *addr, int8_t rssi,
                    uint8_t adv_type, struct net_buf_simple *buf)
{
    char addr_str[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
    printk("Device: %s, RSSI: %d\n", addr_str, rssi);

    /* Parse advertising data */
    while (buf->len > 1) {
        uint8_t len = net_buf_simple_pull_u8(buf);
        uint8_t type;

        if (len == 0 || len > buf->len) {
            break;
        }

        type = net_buf_simple_pull_u8(buf);
        /* Handle type... */
        net_buf_simple_pull(buf, len - 1);
    }
}

/* Start active scanning */
err = bt_le_scan_start(BT_LE_SCAN_ACTIVE, scan_cb);

/* Stop scanning */
err = bt_le_scan_stop();
```

#### Scan Parameters

| Macro | Description |
|-------|-------------|
| `BT_LE_SCAN_ACTIVE` | Active scanning (requests scan responses) |
| `BT_LE_SCAN_PASSIVE` | Passive scanning |
| `BT_LE_SCAN_CODED` | Scan on LE Coded PHY |

#### Custom Scan Parameters

```c
struct bt_le_scan_param scan_param = {
    .type = BT_LE_SCAN_TYPE_ACTIVE,
    .options = BT_LE_SCAN_OPT_FILTER_DUPLICATE,
    .interval = BT_GAP_SCAN_FAST_INTERVAL,  /* 60ms */
    .window = BT_GAP_SCAN_FAST_WINDOW,      /* 30ms */
};

err = bt_le_scan_start(&scan_param, scan_cb);
```

### Connections

#### Creating Connection (Central)

```c
static struct bt_conn *default_conn;

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        printk("Failed to connect (err %u)\n", err);
        bt_conn_unref(default_conn);
        default_conn = NULL;
        return;
    }
    printk("Connected\n");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    printk("Disconnected (reason %u)\n", reason);
    bt_conn_unref(default_conn);
    default_conn = NULL;
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

/* From scan callback, connect to device */
bt_le_scan_stop();
err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN,
                        BT_LE_CONN_PARAM_DEFAULT, &default_conn);
```

#### Connection Parameters

| Macro | Description |
|-------|-------------|
| `BT_LE_CONN_PARAM_DEFAULT` | Default parameters (30-50ms interval) |

#### Custom Connection Parameters

```c
struct bt_le_conn_param conn_param = {
    .interval_min = 24,  /* 30ms (N * 1.25ms) */
    .interval_max = 40,  /* 50ms */
    .latency = 0,
    .timeout = 400,      /* 4s (N * 10ms) */
};

err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, &conn_param, &conn);
```

#### Updating Connection Parameters

```c
err = bt_conn_le_param_update(conn, BT_LE_CONN_PARAM_DEFAULT);
```

#### Disconnecting

```c
err = bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
```

### Extended Advertising

Requires: `CONFIG_BT_EXT_ADV=y`

```c
static struct bt_le_ext_adv *adv;

struct bt_le_adv_param adv_param = {
    .id = BT_ID_DEFAULT,
    .sid = 0,
    .secondary_max_skip = 0,
    .options = BT_LE_ADV_OPT_EXT_ADV | BT_LE_ADV_OPT_CONN,
    .interval_min = BT_GAP_ADV_FAST_INT_MIN_2,
    .interval_max = BT_GAP_ADV_FAST_INT_MAX_2,
    .peer = NULL,
};

/* Create extended advertising set */
err = bt_le_ext_adv_create(&adv_param, NULL, &adv);

/* Set advertising data */
err = bt_le_ext_adv_set_data(adv, ad, ARRAY_SIZE(ad), NULL, 0);

/* Start extended advertising */
err = bt_le_ext_adv_start(adv, BT_LE_EXT_ADV_START_DEFAULT);

/* Stop and delete */
err = bt_le_ext_adv_stop(adv);
err = bt_le_ext_adv_delete(adv);
```

#### Extended Advertising Options

| Option | Description |
|--------|-------------|
| `BT_LE_ADV_OPT_EXT_ADV` | Use extended advertising PDUs |
| `BT_LE_ADV_OPT_CODED` | Use LE Coded PHY |
| `BT_LE_ADV_OPT_NO_2M` | Disable 2M PHY for secondary |
| `BT_LE_ADV_OPT_ANONYMOUS` | Anonymous advertising |

### Connection Parameters

#### GAP Timing Constants

| Define | Value | Description |
|--------|-------|-------------|
| `BT_GAP_ADV_FAST_INT_MIN_1` | 30ms | Fast advertising min |
| `BT_GAP_ADV_FAST_INT_MAX_1` | 60ms | Fast advertising max |
| `BT_GAP_ADV_SLOW_INT_MIN` | 1s | Slow advertising min |
| `BT_GAP_SCAN_FAST_INTERVAL` | 60ms | Fast scan interval |
| `BT_GAP_SCAN_FAST_WINDOW` | 30ms | Fast scan window |
| `BT_GAP_INIT_CONN_INT_MIN` | 30ms | Initial connection interval min |
| `BT_GAP_INIT_CONN_INT_MAX` | 50ms | Initial connection interval max |

#### Address Types

```c
/* Get local identity address */
size_t count = 1;
bt_id_get(addrs, &count);

/* Address types */
BT_ADDR_LE_PUBLIC   /* Public device address */
BT_ADDR_LE_RANDOM   /* Random device address */
```

#### PHY Options

```c
/* LE PHY types */
BT_GAP_LE_PHY_1M     /* 1 Mbps */
BT_GAP_LE_PHY_2M     /* 2 Mbps */
BT_GAP_LE_PHY_CODED  /* Coded PHY (long range) */
```

## Gatt

GATT defines the data structure and procedures for exchanging data over BLE connections.

### Table of Contents

- [GATT Server (Peripheral)](#gatt-server-peripheral)
- [GATT Client (Central)](#gatt-client-central)
- [Custom Services](#custom-services)
- [Notifications and Indications](#notifications-and-indications)
- [Attribute Permissions](#attribute-permissions)

### GATT Server (Peripheral)

#### Defining a Service

```c
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

/* Custom 128-bit UUID */
#define BT_UUID_MY_SERVICE_VAL \
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)

#define BT_UUID_MY_SERVICE BT_UUID_DECLARE_128(BT_UUID_MY_SERVICE_VAL)
#define BT_UUID_MY_CHAR    BT_UUID_DECLARE_128(BT_UUID_128_ENCODE( \
    0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef1))

static uint8_t my_value[20];

/* Read callback */
static ssize_t read_my_char(struct bt_conn *conn,
                            const struct bt_gatt_attr *attr,
                            void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset,
                             my_value, sizeof(my_value));
}

/* Write callback */
static ssize_t write_my_char(struct bt_conn *conn,
                             const struct bt_gatt_attr *attr,
                             const void *buf, uint16_t len,
                             uint16_t offset, uint8_t flags)
{
    if (offset + len > sizeof(my_value)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    memcpy(my_value + offset, buf, len);
    return len;
}

/* Service definition */
BT_GATT_SERVICE_DEFINE(my_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_MY_SERVICE),
    BT_GATT_CHARACTERISTIC(BT_UUID_MY_CHAR,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                           read_my_char, write_my_char, my_value),
);
```

#### Service Macros

| Macro | Description |
|-------|-------------|
| `BT_GATT_SERVICE_DEFINE` | Statically define a GATT service |
| `BT_GATT_PRIMARY_SERVICE` | Declare primary service |
| `BT_GATT_SECONDARY_SERVICE` | Declare secondary service |
| `BT_GATT_INCLUDE_SERVICE` | Include another service |
| `BT_GATT_CHARACTERISTIC` | Declare characteristic with value |
| `BT_GATT_DESCRIPTOR` | Declare descriptor |
| `BT_GATT_CCC` | Client Characteristic Configuration |
| `BT_GATT_CUD` | Characteristic User Description |
| `BT_GATT_CEP` | Characteristic Extended Properties |

#### Dynamic Service Registration

```c
static struct bt_gatt_attr attrs[] = {
    BT_GATT_PRIMARY_SERVICE(BT_UUID_MY_SERVICE),
    BT_GATT_CHARACTERISTIC(BT_UUID_MY_CHAR,
                           BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ,
                           read_my_char, NULL, NULL),
};

static struct bt_gatt_service my_svc = BT_GATT_SERVICE(attrs);

/* Register at runtime */
err = bt_gatt_service_register(&my_svc);

/* Unregister */
err = bt_gatt_service_unregister(&my_svc);
```

### GATT Client (Central)

Requires: `CONFIG_BT_GATT_CLIENT=y`

#### Service Discovery

```c
static struct bt_gatt_discover_params discover_params;

static uint8_t discover_cb(struct bt_conn *conn,
                           const struct bt_gatt_attr *attr,
                           struct bt_gatt_discover_params *params)
{
    if (!attr) {
        printk("Discovery complete\n");
        return BT_GATT_ITER_STOP;
    }

    printk("Handle: %u\n", attr->handle);

    /* Continue discovery */
    return BT_GATT_ITER_CONTINUE;
}

/* Discover all primary services */
discover_params.uuid = NULL;
discover_params.func = discover_cb;
discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
discover_params.type = BT_GATT_DISCOVER_PRIMARY;

err = bt_gatt_discover(conn, &discover_params);
```

#### Discovery Types

| Type | Description |
|------|-------------|
| `BT_GATT_DISCOVER_PRIMARY` | Primary services |
| `BT_GATT_DISCOVER_SECONDARY` | Secondary services |
| `BT_GATT_DISCOVER_INCLUDE` | Included services |
| `BT_GATT_DISCOVER_CHARACTERISTIC` | Characteristics |
| `BT_GATT_DISCOVER_DESCRIPTOR` | Descriptors |
| `BT_GATT_DISCOVER_ATTRIBUTE` | Any attribute |

#### Reading Attributes

```c
static struct bt_gatt_read_params read_params;

static uint8_t read_cb(struct bt_conn *conn, uint8_t err,
                       struct bt_gatt_read_params *params,
                       const void *data, uint16_t length)
{
    if (err) {
        printk("Read failed (err %u)\n", err);
        return BT_GATT_ITER_STOP;
    }

    if (!data) {
        printk("Read complete\n");
        return BT_GATT_ITER_STOP;
    }

    printk("Data: ");
    for (int i = 0; i < length; i++) {
        printk("%02x ", ((uint8_t *)data)[i]);
    }
    printk("\n");

    return BT_GATT_ITER_CONTINUE;
}

read_params.func = read_cb;
read_params.handle_count = 1;
read_params.single.handle = char_handle;
read_params.single.offset = 0;

err = bt_gatt_read(conn, &read_params);
```

#### Writing Attributes

```c
static struct bt_gatt_write_params write_params;

static void write_cb(struct bt_conn *conn, uint8_t err,
                     struct bt_gatt_write_params *params)
{
    if (err) {
        printk("Write failed (err %u)\n", err);
    } else {
        printk("Write complete\n");
    }
}

static uint8_t data[] = {0x01, 0x02, 0x03};

write_params.func = write_cb;
write_params.handle = char_handle;
write_params.offset = 0;
write_params.data = data;
write_params.length = sizeof(data);

err = bt_gatt_write(conn, &write_params);

/* Write without response */
err = bt_gatt_write_without_response(conn, char_handle, data, sizeof(data), false);
```

### Custom Services

#### With Notifications

```c
static void ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    bool notify_enabled = (value == BT_GATT_CCC_NOTIFY);
    printk("Notifications %s\n", notify_enabled ? "enabled" : "disabled");
}

BT_GATT_SERVICE_DEFINE(my_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_MY_SERVICE),
    BT_GATT_CHARACTERISTIC(BT_UUID_MY_CHAR,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ,
                           read_my_char, NULL, my_value),
    BT_GATT_CCC(ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);
```

#### With Indications

```c
BT_GATT_SERVICE_DEFINE(my_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_MY_SERVICE),
    BT_GATT_CHARACTERISTIC(BT_UUID_MY_CHAR,
                           BT_GATT_CHRC_INDICATE,
                           BT_GATT_PERM_NONE,
                           NULL, NULL, NULL),
    BT_GATT_CCC(ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);
```

### Notifications and Indications

#### Server-Side (Sending)

```c
/* Get attribute for the characteristic value */
const struct bt_gatt_attr *attr = &my_svc.attrs[2];  /* Index of char value */

/* Send notification */
err = bt_gatt_notify(NULL, attr, data, sizeof(data));

/* Send notification with callback */
static struct bt_gatt_notify_params notify_params;

static void notify_cb(struct bt_conn *conn, void *user_data)
{
    printk("Notification sent\n");
}

notify_params.attr = attr;
notify_params.data = data;
notify_params.len = sizeof(data);
notify_params.func = notify_cb;

err = bt_gatt_notify_cb(conn, &notify_params);

/* Send indication */
static struct bt_gatt_indicate_params ind_params;

static void indicate_cb(struct bt_conn *conn,
                        struct bt_gatt_indicate_params *params,
                        uint8_t err)
{
    printk("Indication %s\n", err ? "failed" : "acknowledged");
}

ind_params.attr = attr;
ind_params.data = data;
ind_params.len = sizeof(data);
ind_params.func = indicate_cb;

err = bt_gatt_indicate(conn, &ind_params);
```

#### Client-Side (Subscribing)

```c
static struct bt_gatt_subscribe_params subscribe_params;

static uint8_t notify_cb(struct bt_conn *conn,
                         struct bt_gatt_subscribe_params *params,
                         const void *data, uint16_t length)
{
    if (!data) {
        printk("Unsubscribed\n");
        params->value_handle = 0;
        return BT_GATT_ITER_STOP;
    }

    printk("Notification received: %u bytes\n", length);
    return BT_GATT_ITER_CONTINUE;
}

subscribe_params.notify = notify_cb;
subscribe_params.value_handle = char_value_handle;
subscribe_params.ccc_handle = ccc_handle;
subscribe_params.value = BT_GATT_CCC_NOTIFY;  /* or BT_GATT_CCC_INDICATE */

err = bt_gatt_subscribe(conn, &subscribe_params);

/* Unsubscribe */
err = bt_gatt_unsubscribe(conn, &subscribe_params);
```

### Attribute Permissions

| Permission | Description |
|------------|-------------|
| `BT_GATT_PERM_NONE` | No access |
| `BT_GATT_PERM_READ` | Read access |
| `BT_GATT_PERM_WRITE` | Write access |
| `BT_GATT_PERM_READ_ENCRYPT` | Read with encryption |
| `BT_GATT_PERM_WRITE_ENCRYPT` | Write with encryption |
| `BT_GATT_PERM_READ_AUTHEN` | Read with authentication |
| `BT_GATT_PERM_WRITE_AUTHEN` | Write with authentication |
| `BT_GATT_PERM_READ_LESC` | Read with LE Secure Connections |
| `BT_GATT_PERM_WRITE_LESC` | Write with LE Secure Connections |
| `BT_GATT_PERM_PREPARE_WRITE` | Allow prepare writes |

#### Characteristic Properties

| Property | Description |
|----------|-------------|
| `BT_GATT_CHRC_BROADCAST` | Broadcast supported |
| `BT_GATT_CHRC_READ` | Read supported |
| `BT_GATT_CHRC_WRITE_WITHOUT_RESP` | Write without response |
| `BT_GATT_CHRC_WRITE` | Write with response |
| `BT_GATT_CHRC_NOTIFY` | Notify supported |
| `BT_GATT_CHRC_INDICATE` | Indicate supported |
| `BT_GATT_CHRC_AUTH` | Authenticated signed writes |
| `BT_GATT_CHRC_EXT_PROP` | Extended properties |

### UUIDs

#### Standard 16-bit UUIDs

```c
#include <zephyr/bluetooth/uuid.h>

/* Using standard UUIDs */
BT_UUID_GAP       /* Generic Access */
BT_UUID_GATT      /* Generic Attribute */
BT_UUID_HRS       /* Heart Rate Service */
BT_UUID_BAS       /* Battery Service */
BT_UUID_DIS       /* Device Information Service */

/* Compare UUIDs */
if (bt_uuid_cmp(uuid, BT_UUID_HRS) == 0) {
    /* UUID matches Heart Rate Service */
}
```

#### Custom 128-bit UUIDs

```c
/* Define custom UUID */
#define MY_UUID_VAL BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)
#define MY_UUID BT_UUID_DECLARE_128(MY_UUID_VAL)

/* At compile time */
static struct bt_uuid_128 my_uuid = BT_UUID_INIT_128(MY_UUID_VAL);

/* String to UUID */
struct bt_uuid_128 uuid;
bt_uuid_create(&uuid.uuid, "12345678-1234-5678-1234-56789abcdef0", 36);
```

## Kconfig

Common Kconfig options for BLE development in Zephyr.

### Table of Contents

- [Core Options](#core-options)
- [GAP Role Options](#gap-role-options)
- [GATT Options](#gatt-options)
- [Security Options](#security-options)
- [Connection Options](#connection-options)
- [Buffer Options](#buffer-options)
- [Logging Options](#logging-options)

### Core Options

#### Enable Bluetooth

```
CONFIG_BT=y
```

#### Device Name and Appearance

```
CONFIG_BT_DEVICE_NAME="My Device"
CONFIG_BT_DEVICE_APPEARANCE=0    # Generic (see Bluetooth SIG assigned numbers)
```

Common appearance values:
- `0` - Unknown
- `64` - Generic Phone
- `128` - Generic Computer
- `833` - Heart Rate Sensor
- `961` - Running/Walking Sensor

#### Bluetooth Shell (Debugging)

```
CONFIG_BT_SHELL=y
```

### GAP Role Options

#### Peripheral Role

```
CONFIG_BT_PERIPHERAL=y
```

#### Central Role

```
CONFIG_BT_CENTRAL=y
```

#### Broadcaster Role

```
CONFIG_BT_BROADCASTER=y
```

#### Observer Role

```
CONFIG_BT_OBSERVER=y
```

### GATT Options

#### Client Role

```
CONFIG_BT_GATT_CLIENT=y
```

#### Dynamic Database

```
CONFIG_BT_GATT_DYNAMIC_DB=y          # Allow runtime service registration
```

#### Auto-MTU Exchange

```
CONFIG_BT_GATT_AUTO_UPDATE_MTU=y     # Automatically negotiate MTU
CONFIG_BT_L2CAP_TX_MTU=247           # Maximum MTU size
```

#### GATT Caching

```
CONFIG_BT_GATT_CACHING=y             # Enable GATT caching
CONFIG_BT_GATT_SERVICE_CHANGED=y     # Service changed characteristic
```

#### Multiple Notifications

```
CONFIG_BT_GATT_NOTIFY_MULTIPLE=y     # Send multiple notifications at once
```

### Security Options

#### SMP (Security Manager Protocol)

```
CONFIG_BT_SMP=y                      # Enable pairing/bonding
CONFIG_BT_SMP_SC_ONLY=n              # Allow legacy pairing
CONFIG_BT_SMP_SC_PAIR_ONLY=y         # Require LE Secure Connections
```

#### Bonding

```
CONFIG_BT_SETTINGS=y                 # Required for bonding persistence
CONFIG_BT_MAX_PAIRED=5               # Max bonded devices
CONFIG_BT_BONDABLE=y                 # Allow bonding
CONFIG_BT_BONDING_REQUIRED=n         # Require bonding for connections
```

#### Privacy

```
CONFIG_BT_PRIVACY=y                  # Enable RPA (Resolvable Private Address)
CONFIG_BT_RPA_TIMEOUT=900            # RPA rotation interval (seconds)
```

#### Fixed Passkey

```
CONFIG_BT_FIXED_PASSKEY=y
CONFIG_BT_PASSKEY=123456             # 6-digit passkey
```

#### OOB Pairing

```
CONFIG_BT_OOB_DATA_FIXED=y           # Use fixed OOB data (for testing)
```

### Connection Options

#### Connection Count

```
CONFIG_BT_MAX_CONN=1                 # Max simultaneous connections
```

#### Connection Parameters

```
CONFIG_BT_CONN_PARAM_UPDATE_TIMEOUT=5000   # Parameter update timeout (ms)
```

#### PHY Options

```
CONFIG_BT_USER_PHY_UPDATE=y          # Allow application to control PHY
CONFIG_BT_PHY_UPDATE=y               # Enable PHY update procedure
```

#### Data Length Extension

```
CONFIG_BT_USER_DATA_LEN_UPDATE=y     # Allow application to control data length
CONFIG_BT_DATA_LEN_UPDATE=y          # Enable data length extension
CONFIG_BT_CTLR_DATA_LENGTH_MAX=251   # Maximum data length
```

### Buffer Options

#### ACL Buffers

```
CONFIG_BT_BUF_ACL_TX_SIZE=251        # TX buffer size
CONFIG_BT_BUF_ACL_TX_COUNT=7         # Number of TX buffers
CONFIG_BT_BUF_ACL_RX_SIZE=251        # RX buffer size
CONFIG_BT_BUF_ACL_RX_COUNT=6         # Number of RX buffers
```

#### ATT MTU

```
CONFIG_BT_L2CAP_TX_MTU=247           # L2CAP MTU (ATT MTU = MTU - 4)
CONFIG_BT_ATT_PREPARE_COUNT=0        # Prepare write queue size
```

### Extended Advertising

```
CONFIG_BT_EXT_ADV=y                  # Enable extended advertising
CONFIG_BT_EXT_ADV_MAX_ADV_SET=1      # Maximum advertising sets
CONFIG_BT_EXT_ADV_LEGACY_SUPPORT=y   # Also support legacy advertising
```

### Periodic Advertising

```
CONFIG_BT_PER_ADV=y                  # Enable periodic advertising
CONFIG_BT_PER_ADV_SYNC=y             # Enable sync to periodic advertising
CONFIG_BT_PER_ADV_SYNC_MAX=1         # Maximum syncs
```

### Logging Options

```
CONFIG_BT_DEBUG_LOG=y                # General BT logging
CONFIG_BT_LOG_LEVEL_DBG=y            # Debug level

# Subsystem-specific logging
CONFIG_BT_HCI_CORE_LOG_LEVEL_DBG=y   # HCI logging
CONFIG_BT_CONN_LOG_LEVEL_DBG=y       # Connection logging
CONFIG_BT_GATT_LOG_LEVEL_DBG=y       # GATT logging
CONFIG_BT_SMP_LOG_LEVEL_DBG=y        # SMP logging
CONFIG_BT_ATT_LOG_LEVEL_DBG=y        # ATT logging
CONFIG_BT_L2CAP_LOG_LEVEL_DBG=y      # L2CAP logging
```

### Controller Options

For boards with Zephyr's BLE controller:

```
CONFIG_BT_LL_SW_SPLIT=y              # Use Zephyr's LL implementation
CONFIG_BT_CTLR_TX_PWR_PLUS_8=y       # Set TX power (+8 dBm example)
CONFIG_BT_CTLR_TX_BUFFER_SIZE=251    # Controller TX buffer
```

### Common Configurations

#### Minimal Peripheral

```
CONFIG_BT=y
CONFIG_BT_PERIPHERAL=y
CONFIG_BT_DEVICE_NAME="Minimal"
```

#### Peripheral with Bonding

```
CONFIG_BT=y
CONFIG_BT_PERIPHERAL=y
CONFIG_BT_DEVICE_NAME="Secure Device"
CONFIG_BT_SMP=y
CONFIG_BT_SETTINGS=y
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_NVS=y
CONFIG_SETTINGS=y
```

#### Central with GATT Client

```
CONFIG_BT=y
CONFIG_BT_CENTRAL=y
CONFIG_BT_GATT_CLIENT=y
CONFIG_BT_SCAN_NAME_MAX_LEN=32
```

#### High-Throughput Configuration

```
CONFIG_BT=y
CONFIG_BT_PERIPHERAL=y
CONFIG_BT_L2CAP_TX_MTU=247
CONFIG_BT_BUF_ACL_TX_SIZE=251
CONFIG_BT_BUF_ACL_TX_COUNT=10
CONFIG_BT_CTLR_DATA_LENGTH_MAX=251
CONFIG_BT_GATT_NOTIFY_MULTIPLE=y
```

#### Long-Range (Coded PHY)

```
CONFIG_BT=y
CONFIG_BT_PERIPHERAL=y
CONFIG_BT_EXT_ADV=y
CONFIG_BT_CTLR_PHY_CODED=y
```

## Locations

The following locations are relevant for the Bluetooth LE subsystem in Zephyr OS.
Note: `<zephyr-ws-dir>` refers to the Zephyr workspace root (e.g., `zephyr-ws/deps/zephyr`).

### Documentation

| Description | Location |
|-------------|----------|
| BLE Architecture | `<zephyr-ws-dir>/doc/connectivity/bluetooth/bluetooth-arch.rst` |
| LE Host Overview | `<zephyr-ws-dir>/doc/connectivity/bluetooth/bluetooth-le-host.rst` |
| Controller Architecture | `<zephyr-ws-dir>/doc/connectivity/bluetooth/bluetooth-ctlr-arch.rst` |
| Development Guide | `<zephyr-ws-dir>/doc/connectivity/bluetooth/bluetooth-dev.rst` |
| BT Shell Commands | `<zephyr-ws-dir>/doc/connectivity/bluetooth/bluetooth-shell.rst` |
| API Index | `<zephyr-ws-dir>/doc/connectivity/bluetooth/api/index.rst` |
| GAP API | `<zephyr-ws-dir>/doc/connectivity/bluetooth/api/gap.rst` |
| GATT API | `<zephyr-ws-dir>/doc/connectivity/bluetooth/api/gatt.rst` |
| Connection Management | `<zephyr-ws-dir>/doc/connectivity/bluetooth/api/connection_mgmt.rst` |
| L2CAP API | `<zephyr-ws-dir>/doc/connectivity/bluetooth/api/l2cap.rst` |

### Public Headers

| Description | Location |
|-------------|----------|
| Main BT Header | `<zephyr-ws-dir>/include/zephyr/bluetooth/bluetooth.h` |
| Connection API | `<zephyr-ws-dir>/include/zephyr/bluetooth/conn.h` |
| GATT API | `<zephyr-ws-dir>/include/zephyr/bluetooth/gatt.h` |
| GAP Defines | `<zephyr-ws-dir>/include/zephyr/bluetooth/gap.h` |
| UUID Definitions | `<zephyr-ws-dir>/include/zephyr/bluetooth/uuid.h` |
| ATT API | `<zephyr-ws-dir>/include/zephyr/bluetooth/att.h` |
| L2CAP API | `<zephyr-ws-dir>/include/zephyr/bluetooth/l2cap.h` |
| HCI API | `<zephyr-ws-dir>/include/zephyr/bluetooth/hci.h` |
| HCI Types | `<zephyr-ws-dir>/include/zephyr/bluetooth/hci_types.h` |
| Buffers | `<zephyr-ws-dir>/include/zephyr/bluetooth/buf.h` |
| Address Types | `<zephyr-ws-dir>/include/zephyr/bluetooth/addr.h` |
| Crypto API | `<zephyr-ws-dir>/include/zephyr/bluetooth/crypto.h` |

### Service Headers

| Service | Location |
|---------|----------|
| Battery Service (BAS) | `<zephyr-ws-dir>/include/zephyr/bluetooth/services/bas.h` |
| Device Info (DIS) | `<zephyr-ws-dir>/include/zephyr/bluetooth/services/dis.h` |
| Heart Rate (HRS) | `<zephyr-ws-dir>/include/zephyr/bluetooth/services/hrs.h` |
| Nordic UART (NUS) | `<zephyr-ws-dir>/include/zephyr/bluetooth/services/nus.h` |
| Object Transfer (OTS) | `<zephyr-ws-dir>/include/zephyr/bluetooth/services/ots.h` |
| Immediate Alert (IAS) | `<zephyr-ws-dir>/include/zephyr/bluetooth/services/ias.h` |
| Current Time (CTS) | `<zephyr-ws-dir>/include/zephyr/bluetooth/services/cts.h` |

### Source Code

| Component | Location |
|-----------|----------|
| Bluetooth Subsystem Root | `<zephyr-ws-dir>/subsys/bluetooth/` |
| Host Stack | `<zephyr-ws-dir>/subsys/bluetooth/host/` |
| Controller | `<zephyr-ws-dir>/subsys/bluetooth/controller/` |
| Services | `<zephyr-ws-dir>/subsys/bluetooth/services/` |
| Common Code | `<zephyr-ws-dir>/subsys/bluetooth/common/` |
| Crypto | `<zephyr-ws-dir>/subsys/bluetooth/crypto/` |
| Mesh | `<zephyr-ws-dir>/subsys/bluetooth/mesh/` |
| Audio | `<zephyr-ws-dir>/subsys/bluetooth/audio/` |

### Key Host Source Files

| File | Description |
|------|-------------|
| `host/hci_core.c` | HCI core implementation |
| `host/conn.c` | Connection management |
| `host/gatt.c` | GATT implementation |
| `host/att.c` | ATT protocol |
| `host/smp.c` | Security Manager Protocol |
| `host/adv.c` | Advertising |
| `host/scan.c` | Scanning |
| `host/l2cap.c` | L2CAP layer |
| `host/keys.c` | Key management |
| `host/settings.c` | BT settings integration |

### Kconfig Files

| Description | Location |
|-------------|----------|
| Main BT Kconfig | `<zephyr-ws-dir>/subsys/bluetooth/Kconfig` |
| Advertising Kconfig | `<zephyr-ws-dir>/subsys/bluetooth/Kconfig.adv` |
| Logging Kconfig | `<zephyr-ws-dir>/subsys/bluetooth/Kconfig.logging` |
| Host Kconfig | `<zephyr-ws-dir>/subsys/bluetooth/host/Kconfig` |
| GATT Kconfig | `<zephyr-ws-dir>/subsys/bluetooth/host/Kconfig.gatt` |
| L2CAP Kconfig | `<zephyr-ws-dir>/subsys/bluetooth/host/Kconfig.l2cap` |

### Samples

| Sample | Description | Location |
|--------|-------------|----------|
| Peripheral HR | Heart rate peripheral | `<zephyr-ws-dir>/samples/bluetooth/peripheral_hr/` |
| Central HR | Heart rate central | `<zephyr-ws-dir>/samples/bluetooth/central_hr/` |
| Beacon | iBeacon/Eddystone | `<zephyr-ws-dir>/samples/bluetooth/beacon/` |
| Peripheral | Basic peripheral | `<zephyr-ws-dir>/samples/bluetooth/peripheral/` |
| Central | Basic central | `<zephyr-ws-dir>/samples/bluetooth/central/` |
| Broadcaster | Non-connectable advertiser | `<zephyr-ws-dir>/samples/bluetooth/broadcaster/` |
| HCI UART | HCI over UART | `<zephyr-ws-dir>/samples/bluetooth/hci_uart/` |
| Peripheral OTS | Object Transfer | `<zephyr-ws-dir>/samples/bluetooth/peripheral_ots/` |
| Extended Adv | Extended advertising | `<zephyr-ws-dir>/samples/bluetooth/extended_adv/` |
| Direct Adv | Directed advertising | `<zephyr-ws-dir>/samples/bluetooth/direct_adv/` |
| Eddystone | Eddystone beacon | `<zephyr-ws-dir>/samples/bluetooth/eddystone/` |

### Shell Commands

When `CONFIG_BT_SHELL=y` is enabled:

| Command | Description |
|---------|-------------|
| `bt init` | Initialize Bluetooth |
| `bt advertise on` | Start advertising |
| `bt advertise off` | Stop advertising |
| `bt scan on` | Start scanning |
| `bt scan off` | Stop scanning |
| `bt connect <addr>` | Connect to device |
| `bt disconnect` | Disconnect |
| `gatt discover` | Discover services |
| `gatt read <handle>` | Read attribute |
| `gatt write <handle> <data>` | Write attribute |
| `gatt subscribe <handle>` | Subscribe to notifications |

### Tests

| Description | Location |
|-------------|----------|
| Bluetooth Tests | `<zephyr-ws-dir>/tests/bluetooth/` |
| GATT Tests | `<zephyr-ws-dir>/tests/bluetooth/gatt/` |
| Host Tests | `<zephyr-ws-dir>/tests/bluetooth/host/` |
| Controller Tests | `<zephyr-ws-dir>/tests/bluetooth/controller/` |

## Services

Zephyr provides ready-to-use implementations of common Bluetooth SIG services.

### Table of Contents

- [Battery Service (BAS)](#battery-service-bas)
- [Device Information Service (DIS)](#device-information-service-dis)
- [Heart Rate Service (HRS)](#heart-rate-service-hrs)
- [Nordic UART Service (NUS)](#nordic-uart-service-nus)
- [Object Transfer Service (OTS)](#object-transfer-service-ots)
- [Immediate Alert Service (IAS)](#immediate-alert-service-ias)

### Battery Service (BAS)

Exposes battery level as a percentage (0-100).

#### Kconfig

```
CONFIG_BT_BAS=y
```

#### API

```c
#include <zephyr/bluetooth/services/bas.h>

/* Get current battery level */
uint8_t level = bt_bas_get_battery_level();

/* Set battery level (triggers notification if enabled) */
int err = bt_bas_set_battery_level(75);
```

#### Sample Usage

```c
/* Periodically update battery level */
void update_battery(void)
{
    uint8_t level = read_battery_percentage();  /* Your ADC/fuel gauge code */
    bt_bas_set_battery_level(level);
}
```

#### Advertising

```c
BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_BAS_VAL)),
```

### Device Information Service (DIS)

Exposes device manufacturer, model, serial number, and other info.

#### Kconfig

```
CONFIG_BT_DIS=y
CONFIG_BT_DIS_MANUF="My Company"
CONFIG_BT_DIS_MODEL="My Product"
CONFIG_BT_DIS_SERIAL_NUMBER=y
CONFIG_BT_DIS_FW_REV=y
CONFIG_BT_DIS_HW_REV=y
CONFIG_BT_DIS_SW_REV=y

# Optional PnP ID
CONFIG_BT_DIS_PNP=y
CONFIG_BT_DIS_PNP_VID_SRC=1       # 1=Bluetooth SIG, 2=USB
CONFIG_BT_DIS_PNP_VID=0x05F1      # Vendor ID
CONFIG_BT_DIS_PNP_PID=0x0001      # Product ID
CONFIG_BT_DIS_PNP_VER=0x0001      # Product version
```

#### Dynamic Strings

For runtime-configurable values:

```
CONFIG_BT_DIS_SERIAL_NUMBER_STR=""
CONFIG_BT_DIS_FW_REV_STR=""
CONFIG_BT_DIS_HW_REV_STR=""
CONFIG_BT_DIS_SW_REV_STR=""
```

Then set via settings subsystem or implement `bt_dis_str_t` callbacks.

#### Advertising

```c
BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_DIS_VAL)),
```

### Heart Rate Service (HRS)

Exposes heart rate measurements with optional body sensor location.

#### Kconfig

```
CONFIG_BT_HRS=y
CONFIG_BT_HRS_DEFAULT_PERM_RW=y  # Read/write permissions
```

#### API

```c
#include <zephyr/bluetooth/services/hrs.h>

/* Send heart rate notification */
int err = bt_hrs_notify(heartrate_bpm);

/* Register callback for notification state changes */
static void hrs_ntf_changed(bool enabled)
{
    printk("HRS notifications %s\n", enabled ? "enabled" : "disabled");
}

static struct bt_hrs_cb hrs_cb = {
    .ntf_changed = hrs_ntf_changed,
};

bt_hrs_cb_register(&hrs_cb);
```

#### Sample Usage

```c
/* Periodically send heart rate */
void update_heart_rate(void)
{
    uint8_t bpm = read_heart_rate_sensor();
    bt_hrs_notify(bpm);
}
```

#### Advertising

```c
BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_HRS_VAL)),
```

### Nordic UART Service (NUS)

Provides UART-like data transfer over BLE. Not a Bluetooth SIG service, but widely used.

#### Kconfig

```
CONFIG_BT_NUS=y
```

#### API

```c
#include <zephyr/bluetooth/services/nus.h>

/* Receive callback */
static void nus_received(struct bt_conn *conn, const uint8_t *data, uint16_t len)
{
    printk("Received %d bytes\n", len);
}

/* Send enabled callback */
static void nus_sent(struct bt_conn *conn)
{
    printk("Data sent\n");
}

static struct bt_nus_cb nus_cb = {
    .received = nus_received,
    .sent = nus_sent,
};

/* Initialize */
err = bt_nus_init(&nus_cb);

/* Send data */
err = bt_nus_send(conn, data, len);
```

#### Custom UUIDs (Nordic UART)

```
Service:     6E400001-B5A3-F393-E0A9-E50E24DCCA9E
TX Char:     6E400003-B5A3-F393-E0A9-E50E24DCCA9E (notify)
RX Char:     6E400002-B5A3-F393-E0A9-E50E24DCCA9E (write)
```

### Object Transfer Service (OTS)

Enables transfer of arbitrary data objects (files, images, etc.).

#### Kconfig

```
CONFIG_BT_OTS=y
CONFIG_BT_OTS_MAX_OBJ_CNT=10
```

#### Complex API

OTS has a complex API for managing objects. Key structures:

- `struct bt_ots` - OTS instance
- `struct bt_ots_obj` - Object metadata
- `struct bt_ots_cb` - Callbacks for object operations

See `samples/bluetooth/peripheral_ots` for complete example.

#### Advertising

```c
BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_OTS_VAL)),
```

### Immediate Alert Service (IAS)

Simple alerting service (e.g., for "Find Me" functionality).

#### Kconfig

```
CONFIG_BT_IAS=y
CONFIG_BT_IAS_CLIENT=y  /* For central role */
```

#### Server API

```c
#include <zephyr/bluetooth/services/ias.h>

static void alert_stop(void)
{
    /* Stop alerting */
}

static void alert_start(void)
{
    /* Start alerting */
}

static void alert_high(void)
{
    /* High alert */
}

BT_IAS_CB_DEFINE(ias_callbacks) = {
    .no_alert = alert_stop,
    .mild_alert = alert_start,
    .high_alert = alert_high,
};
```

#### Client API

```c
/* Set alert level on remote device */
err = bt_ias_client_alert_write(conn, BT_IAS_ALERT_LVL_HIGH_ALERT);
```

### Current Time Service (CTS)

Exposes current date/time.

#### Kconfig

```
CONFIG_BT_CTS=y
```

#### API

```c
#include <zephyr/bluetooth/services/cts.h>

/* Server: Set current time */
struct bt_cts_current_time ct = {
    .exact_time_256.year = 2024,
    .exact_time_256.month = 12,
    .exact_time_256.day = 25,
    /* ... */
};
bt_cts_set_current_time(&ct);
```

### TX Power Service (TPS)

Exposes transmit power level.

#### Kconfig

```
CONFIG_BT_TPS=y
```

Service is automatically available; no additional API needed.

### Service Summary

| Service | UUID | Kconfig | Primary Use |
|---------|------|---------|-------------|
| BAS | 0x180F | `CONFIG_BT_BAS=y` | Battery level |
| DIS | 0x180A | `CONFIG_BT_DIS=y` | Device info |
| HRS | 0x180D | `CONFIG_BT_HRS=y` | Heart rate |
| NUS | Custom | `CONFIG_BT_NUS=y` | UART over BLE |
| OTS | 0x1825 | `CONFIG_BT_OTS=y` | File transfer |
| IAS | 0x1802 | `CONFIG_BT_IAS=y` | Find me alerts |
| CTS | 0x1805 | `CONFIG_BT_CTS=y` | Current time |
| TPS | 0x1804 | `CONFIG_BT_TPS=y` | TX power |
