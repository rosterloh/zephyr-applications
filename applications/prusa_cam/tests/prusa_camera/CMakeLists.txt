# SPDX-License-Identifier: Apache-2.0
# The network-free half of the camera service client (common/prusa_camera).
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(prusa_camera_proto)

set(lib ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/prusa_camera)
target_sources(app PRIVATE src/main.c ${lib}/prusa_camera_proto.c)
target_include_directories(app PRIVATE ${lib})
