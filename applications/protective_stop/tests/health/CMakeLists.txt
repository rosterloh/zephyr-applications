# The health counters' pure logic and the last_log ring. No HAL.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pstop_health)

set(APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

target_sources(app PRIVATE src/main.c ${APP_SRC}/health_logic.c ${APP_SRC}/log_ring.c)
target_include_directories(app PRIVATE ${APP_SRC})
