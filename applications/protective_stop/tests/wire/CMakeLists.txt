# Golden-frame tests for the pstop wire format. Links only the pstop_c core
# (pure C, no kernel, no net), so this runs anywhere ztest runs.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pstop_wire)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../pstop_c.cmake)

target_sources(app PRIVATE src/main.c ${PSTOP_C_SRCS})
target_include_directories(app PRIVATE ${PSTOP_C_INCLUDE})
