---
name: zephyr-connectivity
description: >
  Network connectivity in Zephyr OS: BSD sockets and TLS/DTLS, WiFi
  (STA/AP, scanning, WPA2/3, TWT), and Bluetooth Low Energy (GAP roles,
  GATT services, advertising, scanning, pairing). Use when implementing
  network clients/servers, adding TLS, resolving hostnames, connecting
  to or hosting a WiFi network, building BLE peripherals/centrals,
  defining GATT services, or configuring advertising data. Triggers on
  socket(), zsock_*, getaddrinfo, mbedtls, net_if_*, wifi_connect,
  bt_enable, bt_gatt_*, bt_le_adv_start, "NUS", "BAS", "advertising",
  "GATT", "scan results".
---

# Zephyr Connectivity

Validated against: Zephyr 4.4.99 (b3e7c445b343, 2026-07-26). Re-check with `uv run poe check-skills`.

## Scope

Network and wireless connectivity in Zephyr — Bluetooth Low Energy,
WiFi, and the BSD-style socket / TLS layer that sits on top of the IP
stack. Does NOT cover Ethernet driver internals, low-level radio
configuration, or the `net_buf` primitive (see `zephyr-kernel`).

## Pick the right reference

| You're working on...                                            | Load                          |
|-----------------------------------------------------------------|-------------------------------|
| BLE peripheral / central, GATT services, advertising, pairing   | `references/bluetooth-le.md`  |
| WiFi STA or AP, scanning, WPA2/WPA3, TWT, power save            | `references/wifi.md`          |
| TCP/UDP sockets, TLS/DTLS, DNS / getaddrinfo                    | `references/sockets.md`       |

## Universal traps

- **Stack + driver are separate Kconfig switches.** Enabling `CONFIG_BT`
  or `CONFIG_WIFI` alone is not enough — the controller / driver also
  needs to be enabled (e.g. `CONFIG_BT_HCI`, vendor WiFi driver).
- **TLS credentials must be added with `tls_credential_add()` BEFORE
  the socket option `TLS_SEC_TAG_LIST` is set.** Order matters.
- **Pair every scan with a stop.** Orphaned scans (BT or WiFi) prevent
  subsequent connects with no obvious error.
- **DTLS uses sockets** with `SOCK_DGRAM` + `IPPROTO_DTLS_1_2`, not a
  separate API. Easy to confuse with TLS option codes.
- **`net_if_get_default()` returns NULL** if no interface has been
  brought up — always null-check before configuring an IP.
