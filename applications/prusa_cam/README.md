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
       prusa show

`-k 1` is WPA2-PSK; see `wifi cred add -h` for others.

Then open `http://<camera ip>/` and log in as `admin` to change resolution,
interval, JPEG quality, mirror/flip and flash. The page needs a password set
first; until then the API answers 403.

## Interfaces

| Route | Auth | |
|---|---|---|
| `GET /` | no | Config page (no secrets in it) |
| `GET /snapshot.jpg` | yes | A fresh frame |
| `GET /api/status` | yes | Upload count and last result |
| `GET`/`POST /api/config` | yes | JSON config; POST takes any subset of `token`, `password`, `interval`, `resolution`, `quality`, `hflip`, `vflip`, `flash` |

Shell: `prusa show`, `prusa set <key> <value>`, `prusa snap`.

## Not done yet

Compared with the upstream firmware: no service AP / captive provisioning,
MJPEG stream, OTA, mDNS, SD card logging, EXIF, static IP or DHT sensor.
