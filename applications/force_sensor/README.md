# Force Sensor Test

```bash
west build -d ./build -b adafruit_qt_py_esp32c3 --sysbuild .
west flash --domain force_sensor --esp-device /dev/ttyACM1
west espressif -p /dev/ttyACM1 monitor
``` 