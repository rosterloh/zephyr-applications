# prusa_cam

A [Prusa Connect](https://connect.prusa3d.com) camera for the AI-Thinker
ESP32-CAM: it takes a JPEG from the OV2640 every N seconds and uploads it to
Prusa Connect over HTTPS, using the
[camera service API v1](https://camera-service.prusa3d.com/docs/). Written from scratch for Zephyr; the feature set and
the Connect API usage follow
[Prusa-Firmware-ESP32-Cam](https://github.com/prusa3d/Prusa-Firmware-ESP32-Cam).

Board and camera driver live in `rosterloh-drivers`
(`ai_thinker_esp32_cam`, `espressif,esp32-i2s-cam`).

## Build

    mise run app prusa_cam

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

Then open `http://prusa-esp32cam.local/` (mDNS) or `http://<camera ip>/`, and
log in as `admin` to change the camera name, resolution, upload interval
(10 s, 30 s, 60 s or 10 min, the cadences the service knows), JPEG quality,
mirror/flip, rotation, flash, brightness/contrast/saturation, the auto
exposure/white balance/gain loops, and a static IPv4 address. The page needs a password set
first; until then the API answers 403.

## Interfaces

| Route | Auth | |
|---|---|---|
| `GET /` | no | Config page (no secrets in it) |
| `GET /snapshot.jpg` | yes | A fresh frame |
| `GET /api/status` | yes | Upload count, last result and what it means, and whether uploads are stopped |
| `POST /api/test` | yes | Check the token with `GET /v1/camera-info`; the result shows in `/api/status` |
| `GET`/`POST /api/config` | yes | JSON config; POST takes any subset of `name`, `token`, `password`, `interval`, `resolution`, `quality`, `hflip`, `vflip`, `flash`, `rotation` (0/90/180/270), `ip`, `netmask`, `gateway`, `dns` (`""` = DHCP / gateway), `brightness`, `contrast`, `saturation` (-2..2), `auto_exposure`, `auto_wb`, `auto_gain` |
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

## Not done yet

Compared with the upstream firmware: no service AP / captive provisioning,
OTA, SD card logging or DHT sensor.
