# Network debugging

Two halves: **on-device** introspection through the `net` shell, and
**off-device** packet capture on the host with tcpdump / tshark / Wireshark.
Use the shell to ask the stack what it thinks its state is; use capture to
see what actually went on the wire. They disagree more often than you'd
like — that gap is usually the bug.

> For the **Synapse protocol specifically** (discovery/connect handshake,
> blob framing, message-counter invariants) use the dedicated
> `synapse-trace-debug` skill — it bundles a tshark dissector and a
> capture→analyze→report loop. This file covers the generic layer beneath it.

## On-device: the `net` shell

Needs `CONFIG_NET_SHELL=y`. Drive it exactly like any other shell command
(see `serial.md`). The high-value ones:

| Command            | Tells you                                                    |
|--------------------|--------------------------------------------------------------|
| `net iface`        | interfaces, up/down, link addr, MTU, assigned IPs            |
| `net ipv6`         | IPv6 addresses, prefixes, DAD state                          |
| `net nbr`          | neighbour cache — is the peer's MAC resolved (ND/ARP)?       |
| `net route`        | routing table (IPv6)                                         |
| `net conn`         | open connections / bound contexts                            |
| `net tcp`          | TCP connection state machine                                 |
| `net stats`        | per-layer counters: drops, checksum errs, retransmits        |
| `net ping <addr>`  | ICMP echo from the device itself                             |
| `net mem`/`allocs` | net_buf pool usage — exhaustion shows up here as drops       |

Triage order: `net iface` (link + IP up?) → `net nbr` (peer reachable?) →
`net stats` (where are packets dying?). A climbing drop/error counter in
`net stats` localises the failure to a layer before you ever open a capture.

## Off-device: capturing the wire

### Wired Ethernet boards (RMII, e.g. nucleo_h563zi)
The device's frames hit your host's NIC (or the switch). Capture there:
```
sudo tcpdump -i <host-iface> -w cap.pcap host <device-ip> or ether host <device-mac>
sudo tshark  -i <host-iface> -f "host <device-ip>" -w cap.pcap
```
Find the device first via `net iface` (its MAC/IP). IPv6 link-local traffic
(`fe80::/10`) only appears on the directly-attached segment — capture on the
interface physically wired to the board, not an upstream router.

Open `cap.pcap` in Wireshark for the dissected view; filter examples:
`ipv6.addr == fe80::xxxx`, `icmpv6`, `udp.port == 7077`, `tcp.flags.reset==1`.

### native_sim / QEMU
Zephyr's net-tools create a `zeth` tap interface on the host. Capture it
directly:
```
sudo tcpdump -i zeth -w cap.pcap
```

### USB networking (CDC ECM/NCM)
The board appears as a host network interface (`usb0`/`enxXXXX`). Capture
that interface the same as a wired NIC.

### Can't tap the segment? Mirror from the device.
`CONFIG_NET_CAPTURE=y` lets the device clone packets to a remote collector
over a tunnel — useful when the traffic never reaches a host you can run
tcpdump on (e.g. device-to-device over an isolated link). Configure the
capture interface, then `net capture setup`/`net capture enable` from the
shell. Heavier than host-side capture; only reach for it when you genuinely
can't see the segment.

## Reading a capture — what each absence means

- **No frames at all from the device** → link or driver: check `net iface`
  is `UP` and the PHY negotiated (often a devicetree/pinctrl issue, not
  networking — see `troubleshooting.md`).
- **Frames out, no replies** → addressing/routing: wrong subnet, DAD
  failure, or peer ND/ARP not resolving (`net nbr`).
- **Replies arrive but app sees nothing** → port/socket: firewall on host,
  wrong bind, or the socket thread faulted (`gdb.md`).
- **Retransmits / dup ACKs climbing** → loss or a stalled RX thread; cross
  with `net stats` retransmit counters and net_buf exhaustion (`net mem`).

## Traps

- **Link-local scope.** `fe80::` addresses need the `%iface` zone on the
  host (`ping6 fe80::1%eth0`) and don't route past the local segment.
- **Capture on the right interface.** A bridge/VLAN can hide the board's
  traffic from the obvious `eth0`; if a capture is empty, list interfaces
  and try the one actually cabled to the device.
- **Checksum offload** makes outgoing checksums look wrong in Wireshark
  ("incorrect, should be ...") — that's the host NIC, not the device.
  Disable Wireshark's checksum validation before chasing a phantom bug.
- **tshark + Lua under CAP_NET_RAW** refuses scripts from `$HOME`; copy any
  dissector to `/tmp/` first (the synapse skill's scripts already do this).
