# Nicla Vision as a USB webcam

```bash
west blobs fetch hal_infineon
west build -d applications/embedded_vision/build -b arduino_nicla_vision/stm32h747xx/m7 applications/embedded_vision
west flash -d applications/embedded_vision/build
```