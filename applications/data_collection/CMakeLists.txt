cmake_minimum_required(VERSION 3.20.0)

# The IMX219 (Raspberry Pi Camera v2) is wired to the CSI connector, and Zephyr
# ships a shield for exactly that module. Set it here rather than expecting every
# caller to pass --shield, since the camera is part of this application.
# Must precede find_package(Zephyr).
set(SHIELD raspberry_pi_camera_module_2)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(data_collection)

# We always want colored gcc output
zephyr_compile_options(-fdiagnostics-color=always)

target_sources(app PRIVATE src/main.c)
