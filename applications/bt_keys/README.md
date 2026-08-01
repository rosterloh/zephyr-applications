# bt_keys

Turns an [Adafruit NeoKey 1x4 QT](https://www.adafruit.com/product/4980) into a
Bluetooth LE HID keypad running on an Adafruit QT Py ESP32-C3. The firmware is
updatable wirelessly over Bluetooth (MCUmgr SMP + MCUboot).

## Hardware

- **Board:** `adafruit_qt_py_esp32c3`
- **Keypad:** Adafruit NeoKey 1x4 QT (PID 4980), an Adafruit *seesaw* device.
  Plug it into the QT Py's STEMMA QT connector. The four keys are read from
  seesaw GPIO pins 4–7 (active-low) and the four NeoPixels are driven from
  seesaw pin 3. The seesaw MFD/GPIO/NeoPixel drivers come from the
  `rosterloh-drivers` module; the devicetree node lives in
  `boards/adafruit_qt_py_esp32c3.overlay`.

## Behaviour

- Advertises as a HID keyboard named `bt_keys`. Pair from a phone/PC (Just
  Works pairing; bonds persist across reboots).
- Each key press is sent as an HID report. The NeoPixel under a held key lights
  cyan for visual feedback.
- The HID report map exposes **two** input reports so a key can emit either a
  standard keyboard usage or a consumer-control (media) usage.
- On the `adafruit_qt_py_esp32s3` board target, key presses are also sent over
  a USB HID interface (same report map), in addition to BLE HoG. The
  `adafruit_qt_py_esp32c3` target has no USB device controller and stays
  BLE-only.

### Key mapping

Defaults to a media pad:

| Key | Action              |
|-----|---------------------|
| 0   | Scan Previous Track |
| 1   | Play / Pause        |
| 2   | Scan Next Track     |
| 3   | Mute                |

Mappings are stored in Settings and can be changed at runtime over the USB
serial console shell:

```
keys list
keys set <index> <keyboard|consumer> <usage-hex>
```

Examples:

```
keys set 0 consumer 0xE9   # key 0 -> Volume Up
keys set 3 keyboard  0x04   # key 3 -> keyboard 'a'
```

`usage-hex` is a HID usage code on the selected page (Consumer page 0x0C for
`consumer`, Keyboard/Keypad page 0x07 for `keyboard`). Changes are saved
immediately and survive reboots.

## Build & flash

```bash
uv run poe app bt_keys --sysbuild      # build app + MCUboot
uv run poe flash bt_keys               # initial flash over USB
```

## OTA update over Bluetooth

The app runs an MCUmgr SMP server over BLE, so signed application images can be
uploaded and swapped in by MCUboot without a cable. Build a new image, then use
any SMP client (e.g. `mcumgr`, or nRF Connect Device Manager on mobile):

```bash
# after bumping VERSION and rebuilding with --sysbuild
mcumgr --conntype ble --connstring peer_name='bt_keys' \
    image upload builds/bt_keys/bt_keys/zephyr/zephyr.signed.bin
mcumgr --conntype ble --connstring peer_name='bt_keys' image confirm
mcumgr --conntype ble --connstring peer_name='bt_keys' reset
```

> Images are unsigned in this development configuration
> (`sysbuild/mcuboot.conf`). Switch MCUboot to `ECDSA_P256` signing and sign
> images with `west sign` before deploying.
