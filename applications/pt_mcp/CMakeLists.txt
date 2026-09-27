cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pt_mcp)

# Shared application code (see applications/common/).
include(${CMAKE_CURRENT_LIST_DIR}/../common/watchdog/watchdog.cmake)

target_sources(app PRIVATE
  src/main.c
)
