cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(protective_stop)

zephyr_compile_options(-fdiagnostics-color=always)

include(${CMAKE_CURRENT_SOURCE_DIR}/pstop_c.cmake)

target_sources(app PRIVATE
  src/main.c
  src/pstop_time.c
  ${PSTOP_C_SRCS}
)

target_include_directories(app PRIVATE ${PSTOP_C_INCLUDE} src)
