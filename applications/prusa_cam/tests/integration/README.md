# Integration tests

`test_integration.py` drives the whole app the way a user would: the `prusa`
shell commands, the web API on port 80, the MJPEG stream on port 81, and the
uploads to Prusa Connect. The camera service is a local stand-in
(`FakeService`, port 18080) that records each request and answers with a
status the test picks; the firmware is built to reach it over plain HTTP
(`CONFIG_PRUSA_CAMERA_TLS=n`), so the TLS handshake itself is not covered.

## native_sim (CI)

    mise run test prusa_cam

The app runs on the host with host sockets, so the web server is on
127.0.0.1:8280 and the stream on 8281. The camera is `sim/sim_cam.c`, and
every frame is `sim/frame.jpg`. Linux only, like native_sim.

## On the board (HIL)

    PRUSA_HIL_WIFI_SSID=... PRUSA_HIL_WIFI_PSK=... \
        mise run hil prusa_cam --serial /dev/ttyACM1

Twister builds `app.prusa_cam.hil` with this machine's address as the camera
service host, flashes the board (MCUboot and the app), and runs the same tests
over the serial console. The board joins the WiFi network, and the tests find
its address with `net ipv4`. Without the two variables the board must already
have a network stored. Frames come from the real sensor, so the tests check
only that they are JPEGs. The board and this machine must be on one network,
and nothing may block port 18080 inbound (the macOS firewall asks once).

The console adapter must not reset the board when the port opens; the boot
test reboots it from the shell instead.
