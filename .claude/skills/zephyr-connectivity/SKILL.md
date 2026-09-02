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

Validated against: Zephyr 4.4.99 (3062245d5980, 2026-09-01). Re-check with `mise run check-skills`.

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
- **Multicast joins survive an interface down/up** as of 4.5:
  `net_if_down()` sends the leave message but keeps the addresses in the
  interface's multicast list and rejoins on the way back up. Don't re-join
  manually after a reconnect — relevant to zenoh's UDP multicast scouting.

## Validation Checklist

A link that works once is not a link that works. Verify recovery too.

- [ ] The interface is genuinely up with an address: `net if` on the shell
      (not just "no error from the connect call").
- [ ] Peer-side confirmation, not just device-side: the WiFi AP lists the
      station, or a BLE central (`bluetoothctl`, nRF Connect) enumerates the
      expected services and characteristic properties.
- [ ] Every scan has a matching stop — a second connect attempt succeeds
      without a reboot.
- [ ] Reconnect exercised deliberately: drop the AP / disconnect the peer and
      confirm the device recovers on its own. Boot-time auto-connect is the
      path most likely to wedge a driver.
- [ ] TLS/DTLS: the handshake completes against the real endpoint, and a
      deliberate failure (wrong CA) reports a clean error rather than
      hanging — raise the mbedTLS log level to read the alert.
- [ ] BLE bonds survive a power cycle if pairing is used: reconnect without
      re-pairing.
