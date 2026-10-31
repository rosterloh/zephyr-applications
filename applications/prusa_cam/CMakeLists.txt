# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(prusa_cam)

include(${CMAKE_CURRENT_LIST_DIR}/../common/http_util/http_util.cmake)

target_sources(app PRIVATE
  src/main.c
  src/cfg.c
  src/cam.c
  src/connect.c
  src/stream.c
  src/web.c
  src/wifi.c
)
target_include_directories(app PRIVATE src)

set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated)
generate_inc_file_for_target(app src/index.html ${gen_dir}/index.html.gz.inc --gzip)
generate_inc_file_for_target(app src/isrg_root_x1.der ${gen_dir}/isrg_root_x1.der.inc)

zephyr_linker_sources(SECTIONS sections-rom.ld)
zephyr_linker_section(NAME http_resource_desc_prusa_http
  KVMA RAM_REGION GROUP RODATA_REGION
)
