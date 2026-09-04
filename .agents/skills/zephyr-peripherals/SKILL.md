---
name: zephyr-peripherals
description: >
  Hardware peripheral drivers and devicetree configuration in Zephyr
  OS: I2C, SPI, UART, GPIO, plus the underlying device-driver model,
  devicetree bindings/overlays, LVGL display integration, the USB
  device stack, and video/camera capture. Use when configuring
  peripherals via devicetree or Kconfig, calling driver APIs
  (i2c_transfer, spi_transceive, uart_poll_in, gpio_pin_*), writing
  custom device drivers with DEVICE_DT_DEFINE, debugging bus issues
  (NACK, CS timing, baud-rate mismatch), wiring up an LVGL display,
  adding a USB CDC-ACM/HID/MSC device, or capturing frames from a
  camera. Triggers on phrases like "talk to this sensor over I2C",
  "configure SPI flash", "set up a GPIO interrupt", "add a UART
  overlay", "write a driver for", "DT_INST_*", "pinctrl", "USB
  device", "CDC ACM", "usbd_*", "enumeration fails", "capture a
  frame", "CSI camera", "video_*", or any *.overlay/*.dts edit.
---

# Zephyr Peripherals

Validated against: Zephyr 4.4.99 (62acbd571c72, 2026-09-04). Re-check with `mise run check-skills`.

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
| USB device stack (usbd): CDC-ACM/HID/MSC, descriptors, enumeration    | `references/usb-device.md`                    |
| Video/camera capture, formats, buffer queues, CSI/DVP wiring          | `references/video.md`                         |

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

## Validation Checklist

Don't stop at "it compiles" — each of these names an artifact to look at.

- [ ] `builds/<app>/zephyr/zephyr.dts` (the *resolved* tree, after every
      overlay) shows the node with `status = "okay"` and the properties you
      intended. An overlay that silently didn't apply looks identical to a
      typo'd node label until you read this file.
- [ ] The macros your C code uses exist in
      `builds/<app>/zephyr/include/generated/zephyr/devicetree_generated.h`
      — catches a `compatible` that no binding matched.
- [ ] `device list` on the shell reports the device (and every device it
      depends on) as ready, not `DISABLED` or `ERR`.
- [ ] `device_is_ready()` is checked in code before the first API call, and
      the boot log has no init-failure line for the driver.
- [ ] Bus traffic confirmed at the expected address/rate — `i2c scan` finds
      the device, or a logic analyzer shows real edges when the driver claims
      a NACK/timeout.
