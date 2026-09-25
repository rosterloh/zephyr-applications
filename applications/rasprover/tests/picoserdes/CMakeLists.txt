# Ztest suite for rasprover's ROS 2 message wire format, as produced by
# Pico-ROS's picoserdes from src/rasprover_types.h.
#
# picoserdes and Micro-CDR are linked directly here rather than through
# applications/common/picoros/picoros.cmake: that fragment also pulls in
# picoros.c, which needs a zenoh-pico session, a network stack and a router.
# Nothing under test here touches zenoh -- it is pure buffer in, buffer out.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(picoserdes)

set(APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
set(COMMON_PICOROS ${CMAKE_CURRENT_SOURCE_DIR}/../../../common/picoros)

if(DEFINED WEST_TOPDIR)
  set(_ws "${WEST_TOPDIR}")
else()
  get_filename_component(_ws "${ZEPHYR_BASE}/../.." ABSOLUTE)
endif()
set(PICOROS_DIR "${_ws}/deps/modules/lib/picoros")
set(MICROCDR_DIR "${_ws}/deps/modules/lib/micro-cdr")

target_sources(app PRIVATE
	src/main.c
	${PICOROS_DIR}/src/picoserdes.c
	${MICROCDR_DIR}/src/c/common.c
	${MICROCDR_DIR}/src/c/types/array.c
	${MICROCDR_DIR}/src/c/types/basic.c
	${MICROCDR_DIR}/src/c/types/sequence.c
	${MICROCDR_DIR}/src/c/types/string.c
)
target_include_directories(app PRIVATE
	${APP_SRC}
	${PICOROS_DIR}/src
	${MICROCDR_DIR}/include
	${COMMON_PICOROS}
)
target_compile_definitions(app PRIVATE USER_TYPE_FILE="${APP_SRC}/rasprover_types.h")
