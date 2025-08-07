# Zephyr Applications

<a href="https://github.com/rosterloh/zephyr-applications/actions/workflows/build.yml?query=branch%3Amain">
  <img src="https://github.com/rosterloh/zephyr-applications/actions/workflows/build.yml/badge.svg?event=push">
</a>

## Getting Started

Before getting started, make sure you have a proper Zephyr development
environment. Follow the official
[Zephyr Getting Started Guide](https://docs.zephyrproject.org/latest/getting_started/index.html).

Setup a global venv
```shell
python3 -m venv ~/envs/zephyr
source ~/envs/zephyr/bin/activate
pip install west
```

### Initialisation

The first step is to initialise the workspace folder (``zephyr_ws``) where this repo
and all Zephyr modules will be cloned. Run the following command:

```shell
mkdir -p ~/zephyr_ws/applications
git clone https://github.com/rosterloh/zephyr-applications ~/zephyr_ws/applications
west init -l ~/zephyr_ws/applications/zephyr-applications
cd ~/zephyr_ws
west update
west packages pip --install
west sdk install --version 0.17.3 --install-dir ~/zephyr-sdk --toolchains arm-zephyr-eabi riscv64-zephyr-elf xtensa-espressif_esp32_zephyr-elf xtensa-espressif_esp32s3_zephyr-elf
west blobs fetch hal_espressif
```

### Building

All commands are implemented as VSCode tasks. press ctrl+shift+b for the build tasks menu

## Testing

### Testing the application

```shell
west twister -T app -v --inline-logs --integration
```

### Testing the drivers

Run all tests with

```shell
west twister -T tests -v --inline-logs --integration
```

or a specific test with

```shell
west twister -p native_sim -s drivers/seesaw/drivers.sensor.seesaw
```
