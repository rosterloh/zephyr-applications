.. _rp2350_poe_eth:

Waveshare RP2350-POE-ETH
########################

Overview
********

The `Waveshare RP2350-POE-ETH`_ is a Raspberry Pi RP2350A board with an onboard
WIZnet W6300 hardwired TCP/IP Ethernet controller and a header for an 802.3af
PoE module, so the board can run from the network cable alone.

This is an **out-of-tree** board definition maintained in this repository. It is
not part of upstream Zephyr.

Hardware
********

- RP2350A (dual Cortex-M33 @ 150 MHz), 520 KB SRAM
- 16 MB onboard NOR flash
- WIZnet W6300 Ethernet controller on SPI0
- WS2812 RGB status LED on GP25
- PoE / USB power source sense lines
- VSYS divider on ADC3 (GP29)
- 20 castellated GPIOs

Supported Features
==================

+-----------+------------+-------------------------------------+
| Interface | Controller | Driver/Component                    |
+===========+============+=====================================+
| NVIC      | on-chip    | nested vector interrupt controller   |
+-----------+------------+-------------------------------------+
| UART      | on-chip    | serial                              |
+-----------+------------+-------------------------------------+
| GPIO      | on-chip    | gpio                                |
+-----------+------------+-------------------------------------+
| SPI       | on-chip    | spi                                 |
+-----------+------------+-------------------------------------+
| I2C       | on-chip    | i2c                                 |
+-----------+------------+-------------------------------------+
| ADC       | on-chip    | adc                                 |
+-----------+------------+-------------------------------------+
| Flash     | on-chip    | flash                               |
+-----------+------------+-------------------------------------+
| Ethernet  | W6300      | ``wiznet,w6300``                    |
+-----------+------------+-------------------------------------+
| LED strip | PIO0       | ``worldsemi,ws2812-rpi_pico-pio``   |
+-----------+------------+-------------------------------------+
| USB       | on-chip    | usb device                          |
+-----------+------------+-------------------------------------+
| Watchdog  | on-chip    | watchdog                            |
+-----------+------------+-------------------------------------+

Connections and IOs
===================

The W6300 wiring is taken from the vendor's board support code
(``examples/C/01-MQTT/lib/Config/DEV_Config.h`` in the `vendor repository`_),
and matches the WIZnet W5500-EVB-Pico2 reference layout. Unlike the WIZnet
W6300-EVB-Pico2 — which bit-bangs the bus — these pins land on the RP2350 SPI0
hardware function, so the peripheral is used directly.

+----------+--------------------------------+
| RP2350   | Function                       |
+==========+================================+
| GP0      | UART0 TX (console)             |
+----------+--------------------------------+
| GP1      | UART0 RX (console)             |
+----------+--------------------------------+
| GP4/GP5  | I2C0 SDA/SCL                   |
+----------+--------------------------------+
| GP6/GP7  | I2C1 SDA/SCL                   |
+----------+--------------------------------+
| GP16     | W6300 MISO (SPI0 RX)           |
+----------+--------------------------------+
| GP17     | W6300 CS                       |
+----------+--------------------------------+
| GP18     | W6300 SCLK (SPI0 SCK)          |
+----------+--------------------------------+
| GP19     | W6300 MOSI (SPI0 TX)           |
+----------+--------------------------------+
| GP20     | W6300 RESET (active low)       |
+----------+--------------------------------+
| GP21     | W6300 INT (active low)         |
+----------+--------------------------------+
| GP23     | PoE power detect               |
+----------+--------------------------------+
| GP24     | USB power detect               |
+----------+--------------------------------+
| GP25     | WS2812 status LED              |
+----------+--------------------------------+
| GP29     | VSYS divider (ADC3)            |
+----------+--------------------------------+

Board variants
==============

``rp2350_poe_eth/rp2350a/m33``
    Single-image build. The application is linked at the base of flash, with a
    1 MB storage partition at the top.

``rp2350_poe_eth/rp2350a/m33/mcuboot``
    MCUboot layout for OTA: 64 KB bootloader, two 1.5 MB image slots and a
    13.9 MB storage partition. Use this with ``--sysbuild``.

Programming and Debugging
*************************

The board enumerates as a UF2 mass storage device when reset with BOOTSEL held.
Copy ``zephyr.uf2`` onto it, or use a SWD probe:

.. code-block:: console

   west build -b rp2350_poe_eth/rp2350a/m33 --build-dir builds/hello samples/hello_world
   west flash --build-dir builds/hello

.. note::

   The OpenOCD bundled with the Zephyr SDK does not support the RP2350. Use the
   Raspberry Pi OpenOCD fork or an external probe (J-Link, probe-rs).

References
**********

.. _Waveshare RP2350-POE-ETH:
   https://www.waveshare.com/rp2350-poe-eth.htm

.. _vendor repository:
   https://github.com/waveshareteam/RP2350-POE-ETH
