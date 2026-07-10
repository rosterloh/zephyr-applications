# PowerBread

Dual-channel breadboard power monitor on the Adafruit QT Py ESP32-C3, ported
from [XIAO-powerbread](https://github.com/nicho810/XIAO-powerbread): an
INA3221 measures voltage/current/power on two supply rails, a 0.96" ST7735
LCD shows an LVGL UI (dashboard, line chart, statistics), and a resistor-ladder
dial wheel navigates it. Measurements stream as CSV over USB serial on demand.

## Wiring

| Function          | QT Py pin | GPIO   | Notes                          |
|-------------------|-----------|--------|--------------------------------|
| INA3221 SDA/SCL   | SDA/SCL   | 5 / 6  | Stemma QT, addr 0x40, 50 mR shunts |
| LCD DIN (MOSI)    | MO        | 7      | SPI2                           |
| LCD CLK           | SCK       | 10     | SPI2                           |
| LCD DC            | MI        | 8      | re-pinmuxed from SPI2 MISO     |
| LCD RST           | A3        | 0      |                                |
| LCD CS            | —         | —      | tied low on the module         |
| Dial wheel ladder | A2 / D2   | 1      | ADC1 channel 1                 |

## Controls

- Dial up/down: cycle mode (dashboard → chart → stats)
- Dial short press: switch focused channel (1 ↔ 2)
- Dial long press (≥1 s): reset stats and energy counters

Shell (USB serial), root command `powerbread`:
`read`, `reset`, `stream <on|off>` (CSV: `t_ms,ch,mV,mA,mW`),
`mode <dash|chart|stats>`, `channel <1|2>`, `dial` (raw dial mV, for
calibrating the `adc-keys` thresholds in the board overlay).

## Build & flash

    uv run poe app powerbread
    uv run poe flash powerbread

## Simulator

The UI runs on the host with synthetic sensor data (SDL window, no
hardware needed) — useful for iterating on screens:

    uv run poe app powerbread --board native_sim/native/64
    ./builds/powerbread/zephyr/zephyr.exe -rt
