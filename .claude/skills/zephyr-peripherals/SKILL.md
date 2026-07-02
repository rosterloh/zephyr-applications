---
name: zephyr-peripherals
description: >
  Hardware peripheral drivers and devicetree configuration in Zephyr
  OS: I2C, SPI, UART, GPIO, plus the underlying device-driver model,
  devicetree bindings/overlays, and LVGL display integration. Use when
  configuring peripherals via devicetree or Kconfig, calling driver
  APIs (i2c_transfer, spi_transceive, uart_poll_in, gpio_pin_*),
  writing custom device drivers with DEVICE_DT_DEFINE, debugging bus
  issues (NACK, CS timing, baud-rate mismatch), or wiring up an LVGL
  display. Triggers on phrases like "talk to this sensor over I2C",
  "configure SPI flash", "set up a GPIO interrupt", "add a UART
  overlay", "write a driver for", "DT_INST_*", "pinctrl", or any
  *.overlay/*.dts edit.
---

# Zephyr Peripherals

## Scope

Hardware peripheral drivers, the device-driver model that backs them,
and the devicetree layer that configures them. Does NOT cover network
interfaces (see `zephyr-connectivity`), filesystem drivers (see
`zephyr-system`), or kernel-level threading concerns inside a driver
(see `zephyr-kernel`).

## Pick the right reference

| You're working on...                                                  | Load                                          |
|-----------------------------------------------------------------------|-----------------------------------------------|
| I2C transfers, target mode, SMBus, NACK / clock-stretching issues     | `references/i2c.md`                           |
| SPI transceive, async, CS via GPIO or hardware, scatter-gather        | `references/spi.md`                           |
| UART polling, IRQ, async/DMA, baud / parity / flow control            | `references/uart.md`                          |
| GPIO read/write, interrupts, devicetree pin specs                     | `references/gpio.md`                          |
| Devicetree syntax, overlays, bindings, pinctrl, clocks, IRQs, DMA     | `references/devicetree.md`                    |
| Device driver model (DEVICE_DT_DEFINE, init levels, PM)               | `references/device-drivers.md`                |
| Writing a sensor driver (sensor subsystem, fetch/get, channels)       | `references/device-drivers-sensors.md`        |
| Writing a bus driver (controller-side I2C/SPI/UART)                   | `references/device-drivers-bus.md`            |
| LVGL display setup, devicetree bindings, frame buffers                | `references/lvgl.md`                          |

## Universal traps

- **`status = "okay"` is required** on every DT node you want to use.
  A disabled node silently gets no driver; `device_get_binding` returns
  NULL and `device_is_ready` is false.
- **Always `device_is_ready(dev)` before any API call.** Init can fail
  silently — e.g. pinctrl wrong, clock not configured.
- **pinctrl needs both the DT pinctrl-0 entry AND the SoC's pinctrl
  driver Kconfig.** Missing either keeps the peripheral electrically
  silent.
- **Driver log level defaults to WRN.** Bring it up to DBG (e.g.
  `CONFIG_I2C_LOG_LEVEL_DBG=y`) when a driver is misbehaving;
  driver-internal `LOG_DBG` lines often hold the real error.
- **`DT_INST_*` macros only work inside a driver's compatible-bound
  source file.** From application code, use `DT_NODELABEL` /
  `DT_PATH` / `DT_CHOSEN`.
