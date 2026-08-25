cmake_minimum_required(VERSION 3.20.0)

# Default to the Raspberry Pi Camera v2, but let -DSHIELD=... or
# `west build --shield` pick a different camera (e.g. arducam_tof_camera).
# Must precede find_package(Zephyr).
if(NOT DEFINED SHIELD)
  set(SHIELD raspberry_pi_camera_module_2)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(data_collection)

# We always want colored gcc output
zephyr_compile_options(-fdiagnostics-color=always)

target_sources(app PRIVATE src/main.c)

target_sources_ifdef(CONFIG_APP_CAM_MGMT app PRIVATE src/cam_mgmt.c)
