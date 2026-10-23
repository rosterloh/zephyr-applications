# The ring and status-pixel pixel computation. Pure functions, no HAL.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pstop_ring)

set(APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

target_sources(app PRIVATE src/main.c ${APP_SRC}/ring.c)
target_include_directories(app PRIVATE ${APP_SRC})
