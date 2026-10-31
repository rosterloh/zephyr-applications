cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pstop_api)

set(APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../common/http_util/http_util.cmake)

target_sources(app PRIVATE
  src/main.c
  ${APP_SRC}/api.c
  ${APP_SRC}/lockstep.c
  ${APP_SRC}/session.c
  ${APP_SRC}/app_settings.c
  ${APP_SRC}/estop_gpio.c
  ${APP_SRC}/estop_verdict.c
  ${APP_SRC}/health_logic.c
  ${APP_SRC}/health.c
  ${APP_SRC}/ring.c
  ${APP_SRC}/leds.c
  ${APP_SRC}/uplink.c
  ${APP_SRC}/wifi.c
  ${APP_SRC}/crash.c
  ${APP_SRC}/verbose.c
)
target_include_directories(app PRIVATE ${APP_SRC})
