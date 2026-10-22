# codex_keys

A twelve-key macropad with a dial, driven by an Adafruit QT Py ESP32-C3, acting
as a BLE controller for ChatGPT Desktop's Codex Micro features.

**Status: spike.** It brings up the BLE identity and vendor HID transport that
ChatGPT Desktop probes for, emits key and dial events, answers the host's status
requests, and logs everything received. It does not yet render the host's agent
lighting state on the NeoPixels.

## Provenance and caveats

The wire protocol is undocumented and was reverse-engineered by
[imliubo/codex-micro-4-core2](https://github.com/imliubo/codex-micro-4-core2),
whose `docs/TECHNICAL.md` this implementation follows. It can break with any
ChatGPT Desktop update.

The device advertises as `Codex Micro` / `Work Louder`, VID `0x303A`, PID
`0x8360`. **Those identifiers are not ours.** They belong to the Work Louder
Codex Micro keyboard and are emitted only because the host uses them for
compatibility detection. Don't ship this as a product or imply it is official
hardware.

## Hardware

- **Board:** `adafruit_qt_py_esp32c3`
- **Keys:** 3× [Adafruit NeoKey 1x4 QT](https://www.adafruit.com/product/4980)
  (PID 4980) = 12 keys with a NeoPixel behind each.
- **Dial:** [Adafruit I2C QT Rotary Encoder](https://www.adafruit.com/product/4991)
  (PID 4991).

Everything is a *seesaw* device daisy-chained on the STEMMA QT bus, so the whole
box is four I2C addresses and one cable.

**Solder the address jumpers before assembly** — the three NeoKeys ship at the
same address:

| Board | Jumper | Address |
| --- | --- | --- |
| Keys 0–3 | none | `0x30` |
| Keys 4–7 | A0 | `0x31` |
| Keys 8–11 | A1 | `0x32` |
| Encoder | none | `0x36` |

Per NeoKey, the keys are seesaw GPIO 4–7 (active-low) and the NeoPixels chain
from seesaw pin 3. On the encoder, the push switch is seesaw pin 24 and the
onboard NeoPixel (unused) is pin 6.

### Key mapping

The twelve keys cover the host's entire key set. `key_map[]` in `src/main.c` is
the whole mapping — reorder it to match how the strips ended up in the box.

| Keys | Host ids |
| --- | --- |
| 0–5 | `AG00`–`AG05` (agent keys, carry their slot index) |
| 6–11 | `ACT06`–`ACT10`, `ACT12` (command keys) |

The dial sends `ENC_CW`/`ENC_CC` step events and `ENC` press/release. Not used:
the host's four-way `v.oai.rad` directional input — trade four command keys for
it if it turns out to matter.

## Protocol

BLE HID with a single vendor-defined collection (usage page `0xFF00`, usage
`0x01`, report id 6): one 63-byte input report and one 63-byte output report.
Each report body is framed as:

| Offset | Size | Meaning |
| --- | --- | --- |
| 0 | 1 | Message type, always `2` |
| 1 | 1 | Payload length, 0–61 |
| 2 | ≤61 | UTF-8 JSON fragment |
| … | | zero padding to 63 bytes |

Messages are newline-terminated JSON, fragmented across reports with a 4 ms gap.
`src/codex_hid.c` owns the service and framing, `src/codex_rpc.c` the messages,
`src/codex_json.c` a minimal top-level member lookup (Zephyr's descriptor-based
JSON parser can't handle the variant `id` field, and the `id` nested inside
`v.oai.thstatus` params would otherwise shadow the request id).

### Implemented

- **Device → host:** `v.oai.hid` press/release for all twelve keys and the dial
  press, plus `ENC_CW`/`ENC_CC` step events.
- **Host → device:** `sys.version` and `device.status` are answered;
  `v.oai.thstatus`, `v.oai.rgbcfg`, `lights.preview` and `host.focused_app` are
  acknowledged but not acted on. Unknown methods get `-32601`.

### Not implemented

Directional `v.oai.rad` events, and NeoPixel rendering of the host's agent
lighting state — the next step, and the reason for putting an RGB LED behind
every key.

## Build & flash

```bash
uv run poe app codex_keys
uv run poe flash codex_keys
```

No sysbuild/MCUboot yet, so no OTA — flash over USB.

## Trying it

Pair from ChatGPT Desktop (Just Works, bonds persist). The log tells you how far
the host got:

```
codex_keys starting
Advertising as "Codex Micro" (303a:8360)
Connected
Security changed: level 2
host subscribed to input reports        <- host found the HID service
rx: {"method":"sys.version","id":1}     <- host is talking the protocol
tx: {"id":1,"result":{"version":"0.1.0-qtpy"}}
```

If the host connects but never subscribes, the descriptor or identity is being
rejected. Forget the device on the host after any descriptor change so cached
metadata is refreshed.

## Test

```bash
uv run pytest tests/codex_keys/
```

Host-compiles `codex_json.c` with gcc/ASAN and checks the request parser against
nested-`id`, string-id and truncated-input cases.
