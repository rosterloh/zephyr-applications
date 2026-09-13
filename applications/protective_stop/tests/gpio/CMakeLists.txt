# HAL glue tests. Needs gpio_emul, so native_sim only.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(estop_gpio)

set(APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

target_sources(app PRIVATE
  src/main.c
  ${APP_SRC}/estop_verdict.c
  ${APP_SRC}/estop_gpio.c
)
target_include_directories(app PRIVATE ${APP_SRC})
