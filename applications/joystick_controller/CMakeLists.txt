cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(joystick_controller)

# Must be set before picoros.cmake is included.
set(PICOROS_USER_TYPE_FILE "${CMAKE_CURRENT_LIST_DIR}/src/teleop_types.h")
include(${CMAKE_CURRENT_LIST_DIR}/../common/picoros/picoros.cmake)

target_sources(app PRIVATE
  src/main.c
  src/teleop.c
)
