# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# WiFi, PSRAM, coredump and hawkBit: every ESP32 board, none of it on native_sim
if(NOT BOARD MATCHES "^native_sim")
  list(APPEND EXTRA_CONF_FILE ${CMAKE_CURRENT_LIST_DIR}/esp32.conf)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(prusa_cam)

include(${CMAKE_CURRENT_LIST_DIR}/../common/http_util/http_util.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../common/prusa_camera/prusa_camera.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../common/build_id/build_id.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../common/watchdog/watchdog.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../common/img_confirm/img_confirm.cmake)

target_sources(app PRIVATE
  src/main.c
  src/cfg.c
  src/cam.c
  src/connect.c
  src/stream.c
  src/web.c
)
target_sources_ifdef(CONFIG_WIFI app PRIVATE src/wifi.c)
target_include_directories(app PRIVATE src)

set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated)
generate_inc_file_for_target(app src/index.html ${gen_dir}/index.html.gz.inc --gzip)

if(CONFIG_DT_HAS_VND_JPEG_CAMERA_ENABLED)
  target_sources(app PRIVATE sim/sim_cam.c)
  generate_inc_file_for_target(app sim/frame.jpg ${gen_dir}/sim_frame.jpg.inc)
endif()

zephyr_linker_sources(SECTIONS sections-rom.ld)
zephyr_linker_section(NAME http_resource_desc_prusa_http
  KVMA RAM_REGION GROUP RODATA_REGION
)
