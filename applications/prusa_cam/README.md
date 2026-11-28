# prusa_cam

A [Prusa Connect](https://connect.prusa3d.com) camera for the AI-Thinker
ESP32-CAM (OV2640) or the DFRobot ESP32-S3 AI Camera, DFR1154 (OV3660): it
takes a JPEG every N seconds and uploads it to
Prusa Connect over HTTPS, using the
[camera service API v1](https://camera-service.prusa3d.com/docs/). Written from scratch for Zephyr; the feature set and
the Connect API usage follow
[Prusa-Firmware-ESP32-Cam](https://github.com/prusa3d/Prusa-Firmware-ESP32-Cam).

Boards and camera drivers live in `rosterloh-drivers`
(`ai_thinker_esp32_cam` + `espressif,esp32-i2s-cam`; `dfrobot_esp32s3_ai_cam`
+ `ovti,ov3660`). The DFR1154 uses Zephyr's LCD_CAM driver with JPEG support
from `zephyr/patches/zephyr/0005-*`. Its "flash" is the IR LEDs, and the
console is the USB-C port.

## Build

    mise run app prusa_cam --sysbuild
    mise run app prusa_cam --sysbuild --board dfrobot_esp32s3_ai_cam/esp32s3/procpu

Sysbuild adds MCUboot (swap-using-move), which hawkBit OTA needs; a plain
build fails at Kconfig. The first flash over USB writes the bootloader too.

It also builds for `native_sim/native/64`, with a canned-JPEG camera and host
sockets, for the integration tests: see `tests/integration/README.md`.

## Setup (serial console, 115200)

1. In Prusa Connect: printer → Camera → *Add new other camera*, copy the token.
   The camera must make its first call within 15 minutes of this; that call
   binds the token to this device's fingerprint (`prusa show`).
2. On the camera:

       wifi cred add -s <ssid> -k 1 -p <psk>
       wifi cred auto_connect
       prusa set token <token>
       prusa set password <admin password>
       prusa test
       prusa show

`-k 1` is WPA2-PSK; see `wifi cred add -h` for others. `prusa test` checks the
token and fingerprint with the service; `prusa show` then gives the result.

Then open `http://prusa-zcam.local/` (mDNS) or `http://<camera ip>/`, and
log in as `admin` to change the camera name, resolution, upload interval
(10 s, 30 s, 60 s or 10 min, the cadences the service knows), JPEG quality,
mirror/flip, rotation, flash, brightness/contrast/saturation, the auto
exposure/white balance/gain loops, and a static IPv4 address. The page needs a password set
first; until then the API answers 403. The page's Terminal tab is the device shell
(`/shell`, same login; ESP32 boards only, not native_sim). It is line mode: the page
turns the shell's echo and VT100 codes off, and Up/Down recall earlier commands.

## Interfaces

| Route | Auth | |
|---|---|---|
| `GET /` | no | Config page (no secrets in it) |
| `GET /snapshot.jpg` | yes | A fresh frame |
| `GET /api/status` | yes | Upload count, last result and what it means, whether uploads are stopped, and the printer's state |
| `POST /api/test` | yes | Check the token with `GET /v1/camera-info`; the result shows in `/api/status` |
| `GET`/`POST /api/config` | yes | JSON config; POST takes any subset of `name`, `token`, `password`, `interval`, `resolution`, `quality`, `hflip`, `vflip`, `flash`, `rotation` (0/90/180/270), `ip`, `netmask`, `gateway`, `dns` (`""` = DHCP / gateway), `brightness`, `contrast`, `saturation` (-2..2), `auto_exposure`, `auto_wb`, `auto_gain`, `ha_key` (write-only), `printer_ip` (`""` = none), `printer_key` (write-only) |
| `GET /shell` (WebSocket) | yes | Device shell; a bad login is closed right after the upgrade |
| `GET :81/stream` | yes | MJPEG stream, one viewer at a time |

Rotation is not applied to the pixels. Prusa Connect gets it as
`config.rotation` in camera-info; `/snapshot.jpg` carries an Exif Orientation
tag for browsers. The stream is sent as captured. A static address applies on the next WiFi connect.

## Talking to Prusa Connect

The client lives in `applications/common/prusa_camera` and follows the
[camera service API](https://camera-service.prusa3d.com/docs/). Camera-info
(name, interval as `trigger_scheme`, rotation, resolution, and the WiFi MAC,
address and SSID as `network_info`) is sent at boot, after a settings change,
and when the address changes. Then a snapshot is sent every interval.

- A refusal (400/401/403/413/415/422, or a token that is not 20 letters/digits)
  stops uploads until the settings change or `prusa test` passes, instead of
  repeating the same request. 403 usually means the camera missed the
  15-minute window after it was created in Connect.
- 429/503 (rate limit) waits for the next interval.
- Network or 5xx errors back off, doubling up to 10 minutes.

Shell: `prusa show`, `prusa set <key> <value>`, `prusa test`, `prusa snap`. For a static
address set `netmask` and `gateway` before `ip`; `prusa set ip dhcp` reverts.
`prusa factory_reset` erases all settings (config, WiFi networks, Home Assistant and
hawkBit settings) and reboots.

Resolutions run from QVGA to UXGA plus HD (1280x720) and FHD (1920x1080); the page and
the shell offer only those the sensor reports as JPEG sizes (the OV2640 driver has no
HD/FHD). WiFi retries back off from 10 s to 5 minutes while the network is missing or
rejects the password.

## Only while printing (PrusaLink)

With the printer's address and PrusaLink API key set (config page, or
`prusa set printer_ip <A.B.C.D>` and `prusa set printer_key <key>`; the key is on
the printer under Settings > Network > PrusaLink), the camera asks the printer for
its state (`GET /api/v1/status`, `X-Api-Key`, plain HTTP on port 80) each interval
and uploads snapshots only while it is PRINTING, PAUSED or ATTENTION, plus one
more as the print ends. Camera-info, `prusa snap` and the connection test still go
through. If the printer does not answer, uploads run as if none were set.
`prusa set printer_ip 0` turns it off. The local snapshot, the stream and Home
Assistant are not affected.

Written for Buddy firmware's PrusaLink (MK4, Core One, XL, MINI). PrusaLink on a
Raspberry Pi (MK3) documents digest login, which this does not do; untested there.

## Home Assistant

The camera shows up in Home Assistant as an ESPHome device (found by mDNS as
`prusa-zcam`): Camera, Upload (a problem while the last upload failed or
uploads are stopped), Uploads, WiFi signal, a Flash LED switch and an Upload
snapshot button. The camera sends a fresh frame, without the flash or Exif
rotation, each time Home Assistant asks; its live view runs at the round-trip
rate.

The API is Noise-encrypted and refuses everyone until a key is set. Make one
with `openssl rand -base64 32`, set it with `prusa ha_key <key>` or the config
page's Home Assistant key field, and paste the same key into Home Assistant.
`CONFIG_PRUSA_CAM_ESPHOME=n` leaves it out.

## OTA (hawkBit)

Point it at a hawkBit server once from the shell; the settings persist:

    hawkbit set addr hawkbit.lan     # host name or IP literal
    hawkbit set port 8080
    hawkbit set ddi_token <target token>  # at most 32 characters
    hawkbit run                      # poll now instead of waiting

The target's controller ID is the chip's hardware ID (`hawkbit info`). Upload
`builds/prusa_cam/prusa_cam/zephyr/zephyr.signed.bin` as the artifact. Plain
HTTP only.

A new image boots on trial. It is confirmed once the camera has produced a
frame and WiFi has an IPv4 address; if that does not happen within two minutes
the task watchdog resets the board and MCUboot boots the old image again, which
hawkBit then reports as a failed update. The poller only runs on a confirmed
image. An interrupted download resumes where it stopped.

## Not done yet

Compared with the upstream firmware: no service AP / captive provisioning,
SD card logging or DHT sensor.
