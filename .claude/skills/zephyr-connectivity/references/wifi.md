# WiFi

## Overview

### Quick Start

1. **Enable WiFi**: `CONFIG_WIFI=y` and driver (e.g., `CONFIG_WIFI_NRF70=y`) in `prj.conf`
2. **Enable Management API**: `CONFIG_NET_L2_WIFI_MGMT=y`
3. **Choose Mode**: STA (station) or AP (access point)
4. **Get Interface**: `net_if_get_wifi_sta()` or `net_if_get_wifi_sap()`
5. **Connect/Enable AP**: Use `net_mgmt()` with `NET_REQUEST_WIFI_*` commands

### Core Initialization Pattern

```c
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_mgmt.h>

static struct net_mgmt_event_callback wifi_cb;

static void wifi_event_handler(struct net_mgmt_event_callback *cb,
                               uint32_t mgmt_event, struct net_if *iface)
{
    switch (mgmt_event) {
    case NET_EVENT_WIFI_CONNECT_RESULT:
        /* Handle connection result */
        break;
    case NET_EVENT_WIFI_DISCONNECT_RESULT:
        /* Handle disconnection */
        break;
    case NET_EVENT_WIFI_SCAN_RESULT:
        /* Handle scan result */
        break;
    case NET_EVENT_WIFI_SCAN_DONE:
        /* Scan complete */
        break;
    }
}

int main(void)
{
    struct net_if *iface = net_if_get_wifi_sta();
    if (!iface) {
        printk("WiFi interface not found\n");
        return -1;
    }

    /* Register event callbacks */
    net_mgmt_init_event_callback(&wifi_cb, wifi_event_handler,
                                 NET_EVENT_WIFI_CONNECT_RESULT |
                                 NET_EVENT_WIFI_DISCONNECT_RESULT |
                                 NET_EVENT_WIFI_SCAN_RESULT |
                                 NET_EVENT_WIFI_SCAN_DONE);
    net_mgmt_add_event_callback(&wifi_cb);

    /* Ready to scan/connect */
}
```

### WiFi Modes

| Mode | Interface Function | Description | Key Commands |
|------|-------------------|-------------|--------------|
| Station (STA) | `net_if_get_wifi_sta()` | Connects to access points | `NET_REQUEST_WIFI_CONNECT`, `NET_REQUEST_WIFI_SCAN` |
| Access Point (AP) | `net_if_get_wifi_sap()` | Creates a WiFi network | `NET_REQUEST_WIFI_AP_ENABLE`, `NET_REQUEST_WIFI_AP_DISABLE` |

### Detailed References

- **Station Mode (Scanning, Connecting)**: [#sta_mode](#sta_mode)
- **Access Point Mode**: [#ap_mode](#ap_mode)
- **Power Save & TWT**: [#power_save](#power_save)
- **Kconfig Options**: [#kconfig](#kconfig)
- **File Locations**: [#locations](#locations)

### Common Patterns

#### Scan for Networks

```c
static void scan_result_handler(struct net_mgmt_event_callback *cb,
                                uint32_t mgmt_event, struct net_if *iface)
{
    if (mgmt_event == NET_EVENT_WIFI_SCAN_RESULT) {
        const struct wifi_scan_result *entry =
            (const struct wifi_scan_result *)cb->info;
        printk("SSID: %s, RSSI: %d, Security: %d\n",
               entry->ssid, entry->rssi, entry->security);
    }
}

/* Start scan */
int ret = net_mgmt(NET_REQUEST_WIFI_SCAN, iface, NULL, 0);
```

#### Connect to Network (WPA2-PSK)

```c
/* `ssid` and `psk` are `const uint8_t *` — cast string literals to avoid
 * -Wpointer-sign. Zephyr's own samples (samples/net/wifi/apsta_mode) cast
 * the same way.
 */
struct wifi_connect_req_params params = {
    .ssid = (const uint8_t *)"MyNetwork",
    .ssid_length = strlen("MyNetwork"),
    .psk = (const uint8_t *)"MyPassword",
    .psk_length = strlen("MyPassword"),
    .security = WIFI_SECURITY_TYPE_PSK,
    .channel = WIFI_CHANNEL_ANY,
    .band = WIFI_FREQ_BAND_UNKNOWN,  /* Auto-select */
};

int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));
```

#### Disconnect

```c
int ret = net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0);
```

#### Get Interface Status

```c
struct wifi_iface_status status = {0};
int ret = net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status, sizeof(status));
if (ret == 0 && status.state >= WIFI_STATE_ASSOCIATED) {
    printk("Connected to: %s\n", status.ssid);
    printk("RSSI: %d dBm\n", status.rssi);
}
```

### Security Types

| Type | Enum Value | Description |
|------|------------|-------------|
| Open | `WIFI_SECURITY_TYPE_NONE` | No security |
| WPA2-PSK | `WIFI_SECURITY_TYPE_PSK` | Pre-shared key (most common) |
| WPA2-PSK-SHA256 | `WIFI_SECURITY_TYPE_PSK_SHA256` | Enhanced PSK |
| WPA3-SAE | `WIFI_SECURITY_TYPE_SAE` | Simultaneous Auth of Equals |
| WPA3-SAE-H2E | `WIFI_SECURITY_TYPE_SAE_H2E` | Hash-to-Element SAE |
| EAP-TLS | `WIFI_SECURITY_TYPE_EAP_TLS` | Enterprise with certificates |
| WPA2/WPA3 Mixed | `WIFI_SECURITY_TYPE_WPA_AUTO_PERSONAL` | Auto-select personal |

### Minimum Kconfig for Station Mode

```
CONFIG_WIFI=y
CONFIG_WIFI_NRF70=y                    # Or your driver
CONFIG_NET_L2_WIFI_MGMT=y
CONFIG_NET_L2_ETHERNET=y
CONFIG_NETWORKING=y
CONFIG_NET_IPV4=y
CONFIG_NET_DHCPV4=y
```

### Minimum Kconfig for AP Mode

```
CONFIG_WIFI=y
CONFIG_WIFI_NRF70=y
CONFIG_NET_L2_WIFI_MGMT=y
CONFIG_WIFI_NM_WPA_SUPPLICANT=y        # Required for AP
CONFIG_WIFI_NM_WPA_SUPPLICANT_AP=y
CONFIG_NET_CONFIG_MY_IPV4_ADDR="192.168.1.1"
CONFIG_NET_CONFIG_MY_IPV4_NETMASK="255.255.255.0"
```

### Key Events

| Event | Description |
|-------|-------------|
| `NET_EVENT_WIFI_SCAN_RESULT` | Single scan result (called per AP found) |
| `NET_EVENT_WIFI_SCAN_DONE` | Scan complete |
| `NET_EVENT_WIFI_CONNECT_RESULT` | Connection attempt result |
| `NET_EVENT_WIFI_DISCONNECT_RESULT` | Disconnection notification |
| `NET_EVENT_WIFI_AP_ENABLE_RESULT` | AP mode enabled |
| `NET_EVENT_WIFI_AP_DISABLE_RESULT` | AP mode disabled |
| `NET_EVENT_WIFI_AP_STA_CONNECTED` | Client connected to AP |
| `NET_EVENT_WIFI_AP_STA_DISCONNECTED` | Client disconnected from AP |
| `NET_EVENT_WIFI_TWT` | TWT event (setup/teardown) |

### Power Save

Enable power save after connection:

```c
struct wifi_ps_params ps_params = {
    .enabled = WIFI_PS_ENABLED,
    .mode = WIFI_PS_MODE_WMM,  /* or WIFI_PS_MODE_LEGACY */
};

net_mgmt(NET_REQUEST_WIFI_PS, iface, &ps_params, sizeof(ps_params));
```

### Related Skills

- **zephyr-kconfig**: Configure `CONFIG_WIFI_*` and `CONFIG_NET_*` options
- **zephyr-devicetree**: WiFi driver device tree bindings
- **zephyr-shell-commands**: WiFi shell for debugging (`CONFIG_NET_SHELL=y`, `CONFIG_WIFI_SHELL=y`)

## Ap Mode

Access Point mode allows the device to create a WiFi network that other devices can connect to.

### Prerequisites

AP mode requires the wpa_supplicant network manager:

```
CONFIG_WIFI_NM_WPA_SUPPLICANT=y
CONFIG_WIFI_NM_WPA_SUPPLICANT_AP=y
```

### Basic AP Configuration

```c
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_if.h>

static struct net_mgmt_event_callback ap_cb;

static void ap_event_handler(struct net_mgmt_event_callback *cb,
                             uint32_t mgmt_event, struct net_if *iface)
{
    switch (mgmt_event) {
    case NET_EVENT_WIFI_AP_ENABLE_RESULT: {
        const struct wifi_status *status =
            (const struct wifi_status *)cb->info;
        if (status->status == 0) {
            printk("AP mode enabled\n");
        } else {
            printk("AP enable failed: %d\n", status->status);
        }
        break;
    }
    case NET_EVENT_WIFI_AP_DISABLE_RESULT:
        printk("AP mode disabled\n");
        break;
    case NET_EVENT_WIFI_AP_STA_CONNECTED: {
        const struct wifi_ap_sta_info *sta =
            (const struct wifi_ap_sta_info *)cb->info;
        printk("Station connected: %02x:%02x:%02x:%02x:%02x:%02x\n",
               sta->mac[0], sta->mac[1], sta->mac[2],
               sta->mac[3], sta->mac[4], sta->mac[5]);
        break;
    }
    case NET_EVENT_WIFI_AP_STA_DISCONNECTED: {
        const struct wifi_ap_sta_info *sta =
            (const struct wifi_ap_sta_info *)cb->info;
        printk("Station disconnected: %02x:%02x:%02x:%02x:%02x:%02x\n",
               sta->mac[0], sta->mac[1], sta->mac[2],
               sta->mac[3], sta->mac[4], sta->mac[5]);
        break;
    }
    }
}

void start_ap(void)
{
    struct net_if *iface = net_if_get_wifi_sap();
    if (!iface) {
        printk("SoftAP interface not found\n");
        return;
    }

    /* Register event callbacks */
    net_mgmt_init_event_callback(&ap_cb, ap_event_handler,
                                 NET_EVENT_WIFI_AP_ENABLE_RESULT |
                                 NET_EVENT_WIFI_AP_DISABLE_RESULT |
                                 NET_EVENT_WIFI_AP_STA_CONNECTED |
                                 NET_EVENT_WIFI_AP_STA_DISCONNECTED);
    net_mgmt_add_event_callback(&ap_cb);

    /* Configure AP parameters */
    struct wifi_connect_req_params params = {
        .ssid = "MyAccessPoint",
        .ssid_length = strlen("MyAccessPoint"),
        .psk = "password123",
        .psk_length = strlen("password123"),
        .security = WIFI_SECURITY_TYPE_PSK,
        .channel = 6,
        .band = WIFI_FREQ_BAND_2_4_GHZ,
    };

    int ret = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, iface,
                       &params, sizeof(params));
    if (ret) {
        printk("AP enable request failed: %d\n", ret);
    }
}
```

### Stop AP Mode

```c
void stop_ap(struct net_if *iface)
{
    int ret = net_mgmt(NET_REQUEST_WIFI_AP_DISABLE, iface, NULL, 0);
    if (ret) {
        printk("AP disable request failed: %d\n", ret);
    }
}
```

### AP Configuration Parameters

The same `wifi_connect_req_params` structure is used for AP mode:

```c
struct wifi_connect_req_params params = {
    .ssid = "NetworkName",       /* AP SSID (1-32 chars) */
    .ssid_length = 11,
    .psk = "password",           /* For WPA2 (8-63 chars) */
    .psk_length = 8,
    .security = WIFI_SECURITY_TYPE_PSK,  /* Open or PSK */
    .channel = 6,                /* Specific channel */
    .band = WIFI_FREQ_BAND_2_4_GHZ,
    .bandwidth = WIFI_FREQ_BANDWIDTH_20MHZ,  /* Optional */
};
```

#### Security Options for AP

| Security | Configuration |
|----------|---------------|
| Open | `.security = WIFI_SECURITY_TYPE_NONE` |
| WPA2-PSK | `.security = WIFI_SECURITY_TYPE_PSK`, `.psk` required |
| WPA3-SAE | `.security = WIFI_SECURITY_TYPE_SAE`, `.sae_password` required |

### Get Connected Stations

```c
void list_connected_stations(struct net_if *iface)
{
    struct wifi_ap_sta_list sta_list = {0};

    int ret = net_mgmt(NET_REQUEST_WIFI_AP_STA_LIST, iface,
                       &sta_list, sizeof(sta_list));
    if (ret) {
        printk("Failed to get station list: %d\n", ret);
        return;
    }

    printk("Connected stations: %d\n", sta_list.num_sta);
    for (int i = 0; i < sta_list.num_sta; i++) {
        printk("  %d: %02x:%02x:%02x:%02x:%02x:%02x\n", i,
               sta_list.sta[i].mac[0], sta_list.sta[i].mac[1],
               sta_list.sta[i].mac[2], sta_list.sta[i].mac[3],
               sta_list.sta[i].mac[4], sta_list.sta[i].mac[5]);
    }
}
```

### Disconnect a Station

```c
void disconnect_station(struct net_if *iface, const uint8_t *mac)
{
    struct wifi_ap_sta_info sta = {0};
    memcpy(sta.mac, mac, WIFI_MAC_ADDR_LEN);

    int ret = net_mgmt(NET_REQUEST_WIFI_AP_STA_DISCONNECT, iface,
                       &sta, sizeof(sta));
    if (ret) {
        printk("Failed to disconnect station: %d\n", ret);
    }
}
```

### AP + STA Mode (Concurrent)

Some drivers support running AP and STA modes simultaneously:

```c
/* Get both interfaces */
struct net_if *sta_iface = net_if_get_wifi_sta();
struct net_if *ap_iface = net_if_get_wifi_sap();

/* Connect as STA first */
struct wifi_connect_req_params sta_params = {
    .ssid = "ExternalNetwork",
    .ssid_length = strlen("ExternalNetwork"),
    .psk = "password",
    .psk_length = strlen("password"),
    .security = WIFI_SECURITY_TYPE_PSK,
};
net_mgmt(NET_REQUEST_WIFI_CONNECT, sta_iface, &sta_params, sizeof(sta_params));

/* Then enable AP */
struct wifi_connect_req_params ap_params = {
    .ssid = "MyHotspot",
    .ssid_length = strlen("MyHotspot"),
    .psk = "appassword",
    .psk_length = strlen("appassword"),
    .security = WIFI_SECURITY_TYPE_PSK,
    .channel = 6,
};
net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, ap_iface, &ap_params, sizeof(ap_params));
```

### AP Mode Kconfig

```
# Core requirements
CONFIG_WIFI=y
CONFIG_WIFI_NRF70=y                        # Or your driver
CONFIG_NET_L2_WIFI_MGMT=y
CONFIG_NETWORKING=y

# wpa_supplicant for AP
CONFIG_WIFI_NM_WPA_SUPPLICANT=y
CONFIG_WIFI_NM_WPA_SUPPLICANT_AP=y

# Network configuration
CONFIG_NET_IPV4=y
CONFIG_NET_CONFIG_SETTINGS=y
CONFIG_NET_CONFIG_MY_IPV4_ADDR="192.168.4.1"
CONFIG_NET_CONFIG_MY_IPV4_NETMASK="255.255.255.0"

# Optional: DHCP server for clients
CONFIG_NET_DHCPV4_SERVER=y

# Max stations
CONFIG_WIFI_MGMT_AP_MAX_NUM_STA=8
```

### DHCP Server for AP Mode

To provide IP addresses to connected clients:

```c
#include <zephyr/net/dhcpv4_server.h>

void setup_dhcp_server(struct net_if *iface)
{
    struct in_addr base_addr;
    struct in_addr netmask;

    net_addr_pton(AF_INET, "192.168.4.2", &base_addr);

    int ret = net_dhcpv4_server_start(iface, &base_addr);
    if (ret) {
        printk("DHCP server start failed: %d\n", ret);
    }
}
```

### AP Events

| Event | Description | Info Structure |
|-------|-------------|----------------|
| `NET_EVENT_WIFI_AP_ENABLE_RESULT` | AP enabled/failed | `wifi_status` |
| `NET_EVENT_WIFI_AP_DISABLE_RESULT` | AP disabled | `wifi_status` |
| `NET_EVENT_WIFI_AP_STA_CONNECTED` | Client connected | `wifi_ap_sta_info` |
| `NET_EVENT_WIFI_AP_STA_DISCONNECTED` | Client disconnected | `wifi_ap_sta_info` |

### Common Issues

#### AP Won't Start
- Verify `CONFIG_WIFI_NM_WPA_SUPPLICANT_AP=y` is set
- Check if channel is valid for the regulatory domain
- Ensure driver supports AP mode

#### Clients Can't Connect
- Verify SSID is being broadcast (check with external device)
- Check security settings match client configuration
- Ensure IP configuration is correct

#### No IP for Clients
- Enable DHCP server (`CONFIG_NET_DHCPV4_SERVER=y`)
- Configure static IP for AP interface
- Verify subnet configuration

## Kconfig

This reference covers the key Kconfig options for WiFi configuration in Zephyr.

### Core WiFi Configuration

#### Driver Selection

```
# Enable WiFi support
CONFIG_WIFI=y

# Select your WiFi driver (choose one)
CONFIG_WIFI_NRF70=y          # Nordic nRF70 series
CONFIG_WIFI_ESP32=y          # ESP32 integrated WiFi
CONFIG_WIFI_ESWIFI=y         # Inventek eS-WiFi
CONFIG_WIFI_WINC1500=y       # Microchip WINC1500
CONFIG_WIFI_ESP_AT=y         # ESP-AT module
CONFIG_WIFI_AIROC=y          # Infineon AIROC
```

#### WiFi Usage Mode

```
# Select primary usage mode (affects buffer allocation)
CONFIG_WIFI_NM_WPA_SUPPLICANT_STA_MODE_ONLY=y  # Station only
CONFIG_WIFI_NM_WPA_SUPPLICANT_AP_MODE_ONLY=y   # AP only
# If neither set, both modes supported
```

### Management API

```
# WiFi L2 management API (required for net_mgmt WiFi commands)
CONFIG_NET_L2_WIFI_MGMT=y

# Extended management features
CONFIG_WIFI_MGMT_RAW_SCAN_RESULTS=y     # Raw 802.11 scan data
CONFIG_WIFI_MGMT_RAW_SCAN_RESULT_LENGTH=1024
CONFIG_WIFI_MGMT_SCAN_SSID_FILT_MAX=4   # Max SSIDs for filtered scan
CONFIG_WIFI_MGMT_SCAN_CHAN_MAX_MANUAL=8 # Max channels for manual scan
```

### Power Save Options

```
# Power save support
CONFIG_WIFI_MGMT_PS=y

# Target Wake Time (WiFi 6)
CONFIG_WIFI_MGMT_TWT=y
CONFIG_WIFI_MGMT_TWT_CHECK_IP=y   # Verify IP before TWT setup
```

### Access Point Mode

```
# AP mode support
CONFIG_WIFI_NM_WPA_SUPPLICANT_AP=y

# Maximum stations in AP mode
CONFIG_WIFI_MGMT_AP_MAX_NUM_STA=8

# DPP (Device Provisioning Protocol)
CONFIG_WIFI_NM_WPA_SUPPLICANT_DPP=y
```

### Network Manager (wpa_supplicant)

```
# Enable wpa_supplicant network manager
CONFIG_WIFI_NM_WPA_SUPPLICANT=y

# WPA3 support
CONFIG_WIFI_NM_WPA_SUPPLICANT_WPA3=y

# Enterprise authentication
CONFIG_WIFI_NM_WPA_SUPPLICANT_ENTERPRISE=y

# 802.11r Fast Transition
CONFIG_WIFI_NM_WPA_SUPPLICANT_FAST_TRANSITION=y

# 802.11k Radio Resource Management
CONFIG_WIFI_NM_WPA_SUPPLICANT_RRM=y
CONFIG_WIFI_NM_WPA_SUPPLICANT_11K=y

# 802.11v BSS Transition Management
CONFIG_WIFI_NM_WPA_SUPPLICANT_BTM=y

# Thread stack size for hostap
CONFIG_WIFI_NM_WPA_SUPPLICANT_THREAD_STACK_SIZE=16384
```

### Credential Storage

```
# Persistent credential storage
CONFIG_WIFI_CREDENTIALS=y
CONFIG_WIFI_CREDENTIALS_BACKEND_SETTINGS=y
CONFIG_WIFI_CREDENTIALS_MAX_ENTRIES=10

# Auto-connect to stored credentials
CONFIG_WIFI_CREDENTIALS_CONNECT_STORED=y

# Required for settings backend
CONFIG_SETTINGS=y
CONFIG_NVS=y
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
```

### Network Stack Integration

```
# Basic networking
CONFIG_NETWORKING=y
CONFIG_NET_L2_ETHERNET=y

# IPv4 support
CONFIG_NET_IPV4=y
CONFIG_NET_DHCPV4=y

# IPv6 support
CONFIG_NET_IPV6=y
CONFIG_NET_DHCPV6=y

# Static IP configuration
CONFIG_NET_CONFIG_SETTINGS=y
CONFIG_NET_CONFIG_MY_IPV4_ADDR="192.168.1.100"
CONFIG_NET_CONFIG_MY_IPV4_NETMASK="255.255.255.0"
CONFIG_NET_CONFIG_MY_IPV4_GW="192.168.1.1"

# DNS
CONFIG_DNS_RESOLVER=y
```

### Buffer Configuration

```
# Network buffers (increase for high throughput)
CONFIG_NET_BUF_RX_COUNT=16
CONFIG_NET_BUF_TX_COUNT=16
CONFIG_NET_PKT_RX_COUNT=8
CONFIG_NET_PKT_TX_COUNT=8

# Maximum packet data size
CONFIG_NET_BUF_DATA_SIZE=1500
```

### Shell and Debugging

```
# WiFi shell commands
CONFIG_NET_SHELL=y
CONFIG_WIFI_SHELL=y

# Verbose logging
CONFIG_WIFI_LOG_LEVEL_DBG=y
CONFIG_NET_L2_WIFI_MGMT_LOG_LEVEL_DBG=y
CONFIG_WIFI_NM_LOG_LEVEL_DBG=y

# wpa_supplicant debug
CONFIG_WIFI_NM_WPA_SUPPLICANT_DEBUG_LEVEL=5  # 0=none to 5=verbose
```

### Driver-Specific Options

#### Nordic nRF70 Series

```
CONFIG_WIFI_NRF70=y
CONFIG_WIFI_NRF70_LOG_LEVEL_DBG=y

# Performance tuning
CONFIG_NRF70_RX_NUM_BUFS=8
CONFIG_NRF70_MAX_TX_AGGREGATION=4
```

#### ESP32

```
CONFIG_WIFI_ESP32=y
CONFIG_ESP32_WIFI_STA_RECONNECT=y
CONFIG_ESP32_WIFI_STA_AUTO_DHCPV4=y
```

#### ESP-AT Module

```
CONFIG_WIFI_ESP_AT=y
CONFIG_WIFI_ESP_AT_MDM_RX_BUF_SIZE=1600
CONFIG_WIFI_ESP_AT_SCAN_MAC_ADDRESS=y
```

### Offload vs Native Networking

#### Offloaded WiFi (chip handles TCP/IP)

```
CONFIG_WIFI_OFFLOAD=y
# No CONFIG_NET_L2_ETHERNET needed
```

#### Native WiFi (Zephyr handles TCP/IP)

```
CONFIG_WIFI_USE_NATIVE_NETWORKING=y
CONFIG_NET_L2_ETHERNET=y
```

### Common Configuration Profiles

#### Minimal Station Mode

```
CONFIG_WIFI=y
CONFIG_WIFI_NRF70=y
CONFIG_NET_L2_WIFI_MGMT=y
CONFIG_NETWORKING=y
CONFIG_NET_L2_ETHERNET=y
CONFIG_NET_IPV4=y
CONFIG_NET_DHCPV4=y
```

#### Full-Featured Station

```
CONFIG_WIFI=y
CONFIG_WIFI_NRF70=y
CONFIG_NET_L2_WIFI_MGMT=y
CONFIG_WIFI_NM_WPA_SUPPLICANT=y
CONFIG_WIFI_NM_WPA_SUPPLICANT_WPA3=y
CONFIG_WIFI_CREDENTIALS=y
CONFIG_WIFI_CREDENTIALS_BACKEND_SETTINGS=y
CONFIG_WIFI_MGMT_PS=y
CONFIG_WIFI_MGMT_TWT=y
CONFIG_NETWORKING=y
CONFIG_NET_L2_ETHERNET=y
CONFIG_NET_IPV4=y
CONFIG_NET_DHCPV4=y
CONFIG_SETTINGS=y
CONFIG_NVS=y
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
```

#### Access Point

```
CONFIG_WIFI=y
CONFIG_WIFI_NRF70=y
CONFIG_NET_L2_WIFI_MGMT=y
CONFIG_WIFI_NM_WPA_SUPPLICANT=y
CONFIG_WIFI_NM_WPA_SUPPLICANT_AP=y
CONFIG_WIFI_MGMT_AP_MAX_NUM_STA=8
CONFIG_NETWORKING=y
CONFIG_NET_L2_ETHERNET=y
CONFIG_NET_IPV4=y
CONFIG_NET_CONFIG_SETTINGS=y
CONFIG_NET_CONFIG_MY_IPV4_ADDR="192.168.4.1"
CONFIG_NET_CONFIG_MY_IPV4_NETMASK="255.255.255.0"
CONFIG_NET_DHCPV4_SERVER=y
```

#### Station + AP (Concurrent)

```
CONFIG_WIFI=y
CONFIG_WIFI_NRF70=y
CONFIG_NET_L2_WIFI_MGMT=y
CONFIG_WIFI_NM_WPA_SUPPLICANT=y
CONFIG_WIFI_NM_WPA_SUPPLICANT_AP=y
# Both STA and AP interfaces available
CONFIG_NET_IF_MAX_IPV4_COUNT=2
CONFIG_NET_IF_MAX_IPV6_COUNT=2
```

### Kconfig Dependencies

```
# WiFi Management API dependencies
NET_L2_WIFI_MGMT → NETWORKING, NET_L2_ETHERNET (for native)

# wpa_supplicant dependencies
WIFI_NM_WPA_SUPPLICANT → NET_SOCKETS, POSIX_API

# Credential storage dependencies
WIFI_CREDENTIALS_BACKEND_SETTINGS → SETTINGS, NVS, FLASH, FLASH_MAP

# TWT dependencies
WIFI_MGMT_TWT → NET_L2_WIFI_MGMT
```

## Locations

This reference provides the key file locations for WiFi development in Zephyr OS.

### Header Files

#### Core WiFi Headers

| Header | Path | Description |
|--------|------|-------------|
| wifi.h | `include/zephyr/net/wifi.h` | Core protocol definitions, security types, status codes |
| wifi_mgmt.h | `include/zephyr/net/wifi_mgmt.h` | Management API, NET_REQUEST/EVENT definitions, driver ops |
| wifi_nm.h | `include/zephyr/net/wifi_nm.h` | Network manager interface |
| wifi_utils.h | `include/zephyr/net/wifi_utils.h` | Channel/band utility functions |
| wifi_credentials.h | `include/zephyr/net/wifi_credentials.h` | Credential storage API |

#### Network Management

| Header | Path | Description |
|--------|------|-------------|
| net_mgmt.h | `include/zephyr/net/net_mgmt.h` | net_mgmt() API for WiFi commands |
| net_if.h | `include/zephyr/net/net_if.h` | Network interface helpers |

### Source Files

#### WiFi L2 Subsystem

| File | Path | Description |
|------|------|-------------|
| wifi_mgmt.c | `subsys/net/l2/wifi/wifi_mgmt.c` | WiFi management implementation |
| wifi_shell.c | `subsys/net/l2/wifi/wifi_shell.c` | WiFi shell commands |
| wifi_nm.c | `subsys/net/l2/wifi/wifi_nm.c` | Network manager integration |
| wifi_utils.c | `subsys/net/l2/wifi/wifi_utils.c` | Utility functions |
| Kconfig | `subsys/net/l2/wifi/Kconfig` | L2 WiFi Kconfig options |

#### Credential Storage

| File | Path | Description |
|------|------|-------------|
| wifi_credentials.c | `subsys/net/lib/wifi_credentials/` | Credential management |
| Kconfig | `subsys/net/lib/wifi_credentials/Kconfig` | Credential Kconfig |

### WiFi Drivers

#### Driver Location

All WiFi drivers are in `drivers/wifi/`:

| Driver | Path | Chips/Modules |
|--------|------|---------------|
| nrf_wifi | `drivers/wifi/nrf_wifi/` | Nordic nRF70 series |
| esp32 | `drivers/wifi/esp32/` | ESP32 integrated WiFi |
| eswifi | `drivers/wifi/eswifi/` | Inventek eS-WiFi modules |
| winc1500 | `drivers/wifi/winc1500/` | Microchip WINC1500 |
| esp_at | `drivers/wifi/esp_at/` | ESP-AT firmware modules |
| infineon_airoc | `drivers/wifi/infineon/` | Infineon AIROC |
| simplelink | `drivers/wifi/simplelink/` | TI SimpleLink |

#### Driver Kconfig

| File | Path |
|------|------|
| Main driver Kconfig | `drivers/wifi/Kconfig` |
| nRF WiFi Kconfig | `drivers/wifi/nrf_wifi/Kconfig.nrfwifi` |
| ESP32 Kconfig | `drivers/wifi/esp32/Kconfig.esp32` |

### Network Manager (wpa_supplicant)

| Path | Description |
|------|-------------|
| `modules/hostap/` | wpa_supplicant integration |
| `modules/hostap/Kconfig` | wpa_supplicant Kconfig |
| `modules/hostap/src/` | wpa_supplicant source adaptations |

### Documentation

| Path | Description |
|------|-------------|
| `doc/connectivity/networking/api/wifi.rst` | WiFi API documentation |
| `doc/connectivity/networking/api/wifi_credentials.rst` | Credentials API docs |
| `doc/connectivity/networking/` | General networking docs |

### Sample Applications

#### WiFi Samples Location

All WiFi samples are in `samples/net/wifi/`:

| Sample | Path | Description |
|--------|------|-------------|
| shell | `samples/net/wifi/shell/` | Comprehensive WiFi shell demo |
| apsta_mode | `samples/net/wifi/apsta_mode/` | Concurrent AP+STA mode |
| test_certs | `samples/net/wifi/test_certs/` | Enterprise certificate testing |

#### Related Network Samples

| Sample | Path | Description |
|--------|------|-------------|
| dhcpv4_client | `samples/net/dhcpv4_client/` | DHCP client example |
| sockets | `samples/net/sockets/` | Socket programming examples |

### Test Suites

| Path | Description |
|------|-------------|
| `tests/net/wifi/` | WiFi subsystem tests |
| `tests/net/wifi_credentials/` | Credential storage tests |

### Device Tree Bindings

| Path | Description |
|------|-------------|
| `dts/bindings/wifi/` | WiFi device tree bindings |
| `dts/bindings/wifi/nordic,nrf70.yaml` | nRF70 binding |
| `dts/bindings/wifi/espressif,esp32-wifi.yaml` | ESP32 WiFi binding |

### Board Configurations

#### Shields with WiFi

| Shield | Path |
|--------|------|
| nRF7002 shields | `boards/shields/nrf7002*/` |
| M.2 WiFi shields | `boards/shields/nxp_m2_wifi_bt/` |
| MikroE WiFi Click | `boards/shields/mikroe_wifi_bt_click/` |

#### Boards with Integrated WiFi

| Board | Path |
|-------|------|
| nRF7002 DK | `boards/nordic/nrf7002dk/` |
| ESP32 boards | `boards/espressif/esp32*/` |
| RPi Pico W | `boards/raspberrypi/rpi_pico/rpi_pico_w.overlay` |

### Quick Reference Paths

```
# Headers
include/zephyr/net/wifi.h
include/zephyr/net/wifi_mgmt.h

# L2 implementation
subsys/net/l2/wifi/

# Drivers
drivers/wifi/<driver_name>/

# wpa_supplicant
modules/hostap/

# Samples
samples/net/wifi/

# Kconfig entry points
drivers/wifi/Kconfig
subsys/net/l2/wifi/Kconfig
modules/hostap/Kconfig

# Documentation
doc/connectivity/networking/api/wifi.rst
```

## Power Save

Power management is critical for battery-powered WiFi devices. Zephyr supports legacy power save, WMM power save, and WiFi 6 Target Wake Time (TWT).

### Power Save Modes

| Mode | Description | Use Case |
|------|-------------|----------|
| Legacy PS | 802.11 power save, wakes on DTIM | Simple, broad compatibility |
| WMM PS | WiFi Multimedia power save, per-AC | Better for mixed traffic |
| TWT | WiFi 6 scheduled wake times | Maximum power savings |

### Enable Power Save

#### Basic Power Save

```c
#include <zephyr/net/wifi_mgmt.h>

void enable_power_save(struct net_if *iface)
{
    struct wifi_ps_params params = {
        .enabled = WIFI_PS_ENABLED,
        .mode = WIFI_PS_MODE_WMM,  /* or WIFI_PS_MODE_LEGACY */
    };

    int ret = net_mgmt(NET_REQUEST_WIFI_PS, iface, &params, sizeof(params));
    if (ret) {
        printk("Power save enable failed: %d\n", ret);
    }
}

void disable_power_save(struct net_if *iface)
{
    struct wifi_ps_params params = {
        .enabled = WIFI_PS_DISABLED,
    };

    int ret = net_mgmt(NET_REQUEST_WIFI_PS, iface, &params, sizeof(params));
    if (ret) {
        printk("Power save disable failed: %d\n", ret);
    }
}
```

#### Power Save Configuration Structure

```c
struct wifi_ps_params {
    enum wifi_ps enabled;           /* WIFI_PS_ENABLED/DISABLED */
    enum wifi_ps_mode mode;         /* LEGACY or WMM */
    int listen_interval;            /* Beacon intervals to sleep */
    enum wifi_ps_wakeup_mode wakeup_mode;
    unsigned short timeout_ms;      /* Inactivity timeout */
    enum wifi_config_ps_exit_strategy exit_strategy;
};
```

#### Advanced Power Save Settings

```c
struct wifi_ps_params params = {
    .enabled = WIFI_PS_ENABLED,
    .mode = WIFI_PS_MODE_WMM,
    .listen_interval = 3,        /* Wake every 3 beacons */
    .wakeup_mode = WIFI_PS_WAKEUP_MODE_DTIM,
    .timeout_ms = 100,           /* Enter PS after 100ms idle */
    .exit_strategy = WIFI_PS_EXIT_EVERY_TIM,
};
```

### Get Power Save Status

```c
void get_ps_status(struct net_if *iface)
{
    struct wifi_ps_config config = {0};

    int ret = net_mgmt(NET_REQUEST_WIFI_PS_CONFIG, iface,
                       &config, sizeof(config));
    if (ret) {
        printk("PS config get failed: %d\n", ret);
        return;
    }

    printk("Power Save: %s\n",
           config.ps_params.enabled == WIFI_PS_ENABLED ? "enabled" : "disabled");
    printk("Mode: %s\n",
           config.ps_params.mode == WIFI_PS_MODE_WMM ? "WMM" : "Legacy");
    printk("Listen interval: %d\n", config.ps_params.listen_interval);
}
```

### Target Wake Time (TWT) - WiFi 6

TWT allows the device to negotiate specific wake times with the AP, providing predictable power consumption and reduced contention.

#### TWT Requirements
- WiFi 6 (802.11ax) capable hardware
- WiFi 6 AP with TWT support
- `CONFIG_WIFI_MGMT_TWT=y`

#### TWT Setup

```c
#include <zephyr/net/wifi_mgmt.h>

static struct net_mgmt_event_callback twt_cb;

static void twt_event_handler(struct net_mgmt_event_callback *cb,
                              uint32_t mgmt_event, struct net_if *iface)
{
    if (mgmt_event == NET_EVENT_WIFI_TWT) {
        const struct wifi_twt_params *twt =
            (const struct wifi_twt_params *)cb->info;

        switch (twt->operation) {
        case WIFI_TWT_SETUP:
            if (twt->resp_status == WIFI_TWT_RESP_RECEIVED) {
                printk("TWT setup complete\n");
                printk("  Wake interval: %llu us\n", twt->setup.twt_wake_interval);
                printk("  Wake duration: %u us\n", twt->setup.twt_interval);
            } else {
                printk("TWT setup failed: %d\n", twt->fail_reason);
            }
            break;
        case WIFI_TWT_TEARDOWN:
            printk("TWT teardown complete\n");
            break;
        }
    }
}

void setup_twt(struct net_if *iface)
{
    /* Register TWT event callback */
    net_mgmt_init_event_callback(&twt_cb, twt_event_handler,
                                 NET_EVENT_WIFI_TWT);
    net_mgmt_add_event_callback(&twt_cb);

    struct wifi_twt_params params = {
        .operation = WIFI_TWT_SETUP,
        .negotiation_type = WIFI_TWT_INDIVIDUAL,
        .setup_cmd = WIFI_TWT_SETUP_CMD_REQUEST,
        .dialog_token = 1,
        .flow_id = 0,
        .setup = {
            .responder = 0,             /* AP is responder */
            .trigger = 1,               /* Trigger-enabled */
            .implicit = 1,              /* Implicit TWT */
            .announce = 0,              /* Unannounced */
            .twt_wake_interval = 65000, /* Wake interval in us */
            .twt_interval = 1,          /* Min wake duration */
        },
    };

    int ret = net_mgmt(NET_REQUEST_WIFI_TWT, iface, &params, sizeof(params));
    if (ret) {
        printk("TWT setup request failed: %d\n", ret);
    }
}
```

#### TWT Teardown

```c
void teardown_twt(struct net_if *iface, uint8_t flow_id)
{
    struct wifi_twt_params params = {
        .operation = WIFI_TWT_TEARDOWN,
        .flow_id = flow_id,
        .teardown = {
            .teardown_all = 0,  /* Only this flow */
        },
    };

    int ret = net_mgmt(NET_REQUEST_WIFI_TWT, iface, &params, sizeof(params));
    if (ret) {
        printk("TWT teardown failed: %d\n", ret);
    }
}

/* Teardown all TWT flows */
void teardown_all_twt(struct net_if *iface)
{
    struct wifi_twt_params params = {
        .operation = WIFI_TWT_TEARDOWN,
        .teardown = {
            .teardown_all = 1,
        },
    };

    net_mgmt(NET_REQUEST_WIFI_TWT, iface, &params, sizeof(params));
}
```

#### TWT Parameters Structure

```c
struct wifi_twt_params {
    enum wifi_twt_operation operation;      /* SETUP/TEARDOWN */
    enum wifi_twt_negotiation_type negotiation_type;
    enum wifi_twt_setup_cmd setup_cmd;      /* REQUEST/SUGGEST/DEMAND */
    uint8_t dialog_token;
    uint8_t flow_id;                        /* 0-7 */
    enum wifi_twt_setup_resp_status resp_status;
    enum wifi_twt_fail_reason fail_reason;

    union {
        struct {
            uint64_t twt_wake_interval;     /* Wake interval in us */
            uint32_t twt_interval;          /* Service period in us */
            bool responder;
            bool trigger;
            bool implicit;
            bool announce;
            bool wake_ahead;
        } setup;

        struct {
            bool teardown_all;
        } teardown;
    };
};
```

### TWT Types

| Type | Description |
|------|-------------|
| Individual | Negotiated between single STA and AP |
| Broadcast | AP broadcasts TWT schedule to all STAs |
| Implicit | Intervals repeat automatically |
| Explicit | Each interval explicitly signaled |
| Triggered | AP triggers transmission |
| Untriggered | STA can transmit anytime during SP |

### Power Save Kconfig

```
# Basic power save
CONFIG_WIFI_MGMT_PS=y

# TWT support (WiFi 6)
CONFIG_WIFI_MGMT_TWT=y

# TWT IP address check before setup
CONFIG_WIFI_MGMT_TWT_CHECK_IP=y
```

### Power Consumption Guidelines

#### Maximum Power Save
1. Enable TWT with long wake intervals
2. Use implicit TWT for periodic data
3. Minimize beacon listen interval
4. Use triggered mode if AP supports it

#### Balanced Performance
1. Enable WMM power save
2. Use moderate listen interval (3-5)
3. Consider latency requirements

#### Low Latency
1. Disable power save or use short timeout
2. Use DTIM wakeup mode
3. Avoid TWT for real-time traffic

### Common Issues

#### Power Save Not Working
- Verify AP supports power save features
- Check if traffic pattern prevents sleep
- Ensure connected state before enabling PS

#### TWT Setup Rejected
- AP may not support TWT
- Requested parameters may be out of range
- Try different wake interval/duration

#### High Latency with Power Save
- Reduce listen interval
- Use triggered TWT
- Consider disabling PS for latency-critical apps

## Sta Mode

Station mode connects to an existing WiFi access point. This is the most common WiFi operation.

### Scanning for Networks

#### Basic Scan

```c
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_mgmt.h>

static struct net_mgmt_event_callback scan_cb;

static void scan_handler(struct net_mgmt_event_callback *cb,
                         uint32_t mgmt_event, struct net_if *iface)
{
    if (mgmt_event == NET_EVENT_WIFI_SCAN_RESULT) {
        const struct wifi_scan_result *entry =
            (const struct wifi_scan_result *)cb->info;

        printk("%-32s | Ch: %3d | RSSI: %4d | Security: %s\n",
               entry->ssid,
               entry->channel,
               entry->rssi,
               wifi_security_txt(entry->security));
    }

    if (mgmt_event == NET_EVENT_WIFI_SCAN_DONE) {
        printk("Scan complete\n");
    }
}

void start_scan(struct net_if *iface)
{
    net_mgmt_init_event_callback(&scan_cb, scan_handler,
                                 NET_EVENT_WIFI_SCAN_RESULT |
                                 NET_EVENT_WIFI_SCAN_DONE);
    net_mgmt_add_event_callback(&scan_cb);

    int ret = net_mgmt(NET_REQUEST_WIFI_SCAN, iface, NULL, 0);
    if (ret) {
        printk("Scan request failed: %d\n", ret);
    }
}
```

#### Filtered Scan

Use `wifi_scan_params` to filter scan results:

```c
struct wifi_scan_params params = {
    .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    .bands = WIFI_FREQ_BAND_2_4_GHZ | WIFI_FREQ_BAND_5_GHZ,
    .dwell_time_active = 50,   /* ms per channel */
    .dwell_time_passive = 130, /* ms for passive scan */
    .max_bss_cnt = 10,         /* Max results to return */
};

/* Scan specific SSID */
params.ssids[0] = "TargetNetwork";
params.num_ssids = 1;

/* Scan specific channels */
params.chan[0].channel = 6;
params.chan[0].band = WIFI_FREQ_BAND_2_4_GHZ;
params.num_chans = 1;

int ret = net_mgmt(NET_REQUEST_WIFI_SCAN, iface, &params, sizeof(params));
```

#### Scan Result Structure

```c
struct wifi_scan_result {
    uint8_t ssid[WIFI_SSID_MAX_LEN];
    uint8_t ssid_length;
    uint8_t band;                    /* WIFI_FREQ_BAND_* */
    uint8_t channel;
    enum wifi_security_type security;
    enum wifi_mfp_options mfp;       /* Management Frame Protection */
    int8_t rssi;
    uint8_t mac[6];                  /* BSSID */
    uint8_t mac_length;
};
```

### Connecting to a Network

#### WPA2-PSK Connection

```c
static struct net_mgmt_event_callback conn_cb;

static void connection_handler(struct net_mgmt_event_callback *cb,
                               uint32_t mgmt_event, struct net_if *iface)
{
    if (mgmt_event == NET_EVENT_WIFI_CONNECT_RESULT) {
        const struct wifi_status *status =
            (const struct wifi_status *)cb->info;

        if (status->status == 0) {
            printk("Connected successfully\n");
        } else {
            printk("Connection failed: %d\n", status->status);
        }
    }
}

void connect_to_network(struct net_if *iface)
{
    net_mgmt_init_event_callback(&conn_cb, connection_handler,
                                 NET_EVENT_WIFI_CONNECT_RESULT);
    net_mgmt_add_event_callback(&conn_cb);

    struct wifi_connect_req_params params = {
        .ssid = "MyNetwork",
        .ssid_length = strlen("MyNetwork"),
        .psk = "MyPassword",
        .psk_length = strlen("MyPassword"),
        .security = WIFI_SECURITY_TYPE_PSK,
        .channel = WIFI_CHANNEL_ANY,
        .band = WIFI_FREQ_BAND_UNKNOWN,
        .mfp = WIFI_MFP_OPTIONAL,
    };

    int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface,
                       &params, sizeof(params));
    if (ret) {
        printk("Connection request failed: %d\n", ret);
    }
}
```

#### WPA3-SAE Connection

```c
struct wifi_connect_req_params params = {
    .ssid = "SecureNetwork",
    .ssid_length = strlen("SecureNetwork"),
    .sae_password = "MyPassword",
    .sae_password_length = strlen("MyPassword"),
    .security = WIFI_SECURITY_TYPE_SAE,
    .mfp = WIFI_MFP_REQUIRED,  /* Mandatory for WPA3 */
    .channel = WIFI_CHANNEL_ANY,
};
```

#### Enterprise (EAP-TLS) Connection

```c
struct wifi_connect_req_params params = {
    .ssid = "EnterpriseNetwork",
    .ssid_length = strlen("EnterpriseNetwork"),
    .security = WIFI_SECURITY_TYPE_EAP_TLS,
    .mfp = WIFI_MFP_OPTIONAL,
    /* Certificates configured via wifi_credentials or runtime */
};
```

#### Connect Parameters Structure

```c
struct wifi_connect_req_params {
    const uint8_t *ssid;
    uint8_t ssid_length;
    const uint8_t *psk;           /* For WPA2-PSK */
    uint8_t psk_length;
    const uint8_t *sae_password;  /* For WPA3-SAE */
    uint8_t sae_password_length;
    uint8_t band;                 /* WIFI_FREQ_BAND_* or UNKNOWN */
    uint8_t channel;              /* WIFI_CHANNEL_ANY or specific */
    enum wifi_security_type security;
    enum wifi_mfp_options mfp;
    int timeout;                  /* Connection timeout in seconds */
};
```

### Disconnecting

```c
static void disconnect_handler(struct net_mgmt_event_callback *cb,
                               uint32_t mgmt_event, struct net_if *iface)
{
    if (mgmt_event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
        const struct wifi_status *status =
            (const struct wifi_status *)cb->info;
        printk("Disconnected: reason %d\n", status->status);
    }
}

void disconnect(struct net_if *iface)
{
    int ret = net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0);
    if (ret) {
        printk("Disconnect request failed: %d\n", ret);
    }
}
```

### Checking Connection Status

```c
void check_status(struct net_if *iface)
{
    struct wifi_iface_status status = {0};

    int ret = net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface,
                       &status, sizeof(status));
    if (ret) {
        printk("Status request failed: %d\n", ret);
        return;
    }

    printk("State: %s\n", wifi_state_txt(status.state));

    if (status.state >= WIFI_STATE_ASSOCIATED) {
        printk("SSID: %s\n", status.ssid);
        printk("BSSID: %02x:%02x:%02x:%02x:%02x:%02x\n",
               status.bssid[0], status.bssid[1], status.bssid[2],
               status.bssid[3], status.bssid[4], status.bssid[5]);
        printk("Channel: %d\n", status.channel);
        printk("RSSI: %d dBm\n", status.rssi);
        printk("Security: %s\n", wifi_security_txt(status.security));
        printk("Link mode: %s\n", wifi_link_mode_txt(status.link_mode));
    }
}
```

#### Interface Status Structure

```c
struct wifi_iface_status {
    enum wifi_iface_state state;     /* WIFI_STATE_* */
    unsigned int ssid_len;
    char ssid[WIFI_SSID_MAX_LEN];
    char bssid[WIFI_MAC_ADDR_LEN];
    enum wifi_frequency_bands band;
    unsigned int channel;
    enum wifi_iface_mode iface_mode; /* STA, AP, etc. */
    enum wifi_link_mode link_mode;   /* WiFi 4/5/6/6E/7 */
    enum wifi_security_type security;
    enum wifi_mfp_options mfp;
    int rssi;
    unsigned int beacon_interval;
    unsigned int dtim_period;
    enum wifi_twt_sleep_state twt_capable;
};
```

### Credential Storage

For persistent credentials, use the WiFi credentials API:

```c
#include <zephyr/net/wifi_credentials.h>

/* Store credentials */
struct wifi_credentials_personal creds = {
    .header = {
        .type = WIFI_SECURITY_TYPE_PSK,
        .ssid = "MyNetwork",
        .ssid_len = strlen("MyNetwork"),
    },
    .password = "MyPassword",
    .password_len = strlen("MyPassword"),
};

wifi_credentials_set_personal(&creds);

/* Auto-connect using stored credentials */
wifi_credentials_connect_stored(iface);
```

Kconfig for credentials:
```
CONFIG_WIFI_CREDENTIALS=y
CONFIG_WIFI_CREDENTIALS_BACKEND_SETTINGS=y
CONFIG_SETTINGS=y
CONFIG_NVS=y
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
```

### Connection State Machine

```
DISCONNECTED
    │
    ▼ (NET_REQUEST_WIFI_CONNECT)
ASSOCIATING
    │
    ├──(fail)──► DISCONNECTED
    ▼
ASSOCIATED
    │
    ▼ (4-way handshake)
COMPLETED (Connected)
    │
    ▼ (NET_REQUEST_WIFI_DISCONNECT or AP loss)
DISCONNECTED
```

### Common Issues

#### Connection Timeout
- Increase timeout in `wifi_connect_req_params.timeout`
- Check signal strength (RSSI should be > -80 dBm)
- Verify SSID and password are correct

#### Authentication Failure
- Verify security type matches AP configuration
- For WPA3, ensure `mfp = WIFI_MFP_REQUIRED`
- Check password length (8-63 chars for WPA2/WPA3)

#### No Scan Results
- Ensure WiFi is enabled and interface is up
- Check regulatory domain settings
- Try both active and passive scan types
