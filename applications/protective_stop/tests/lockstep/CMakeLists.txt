cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(lockstep)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../pstop_c.cmake)

set(APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

target_sources(app PRIVATE
  src/main.c
  ${APP_SRC}/lockstep.c
  ${APP_SRC}/session.c
  ${APP_SRC}/app_settings.c
  ${APP_SRC}/estop_gpio.c
  ${APP_SRC}/estop_verdict.c
  ${APP_SRC}/pstop_time.c
  ${PSTOP_C_SRCS}
)
target_include_directories(app PRIVATE ${APP_SRC} ${PSTOP_C_INCLUDE})
