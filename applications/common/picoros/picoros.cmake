# Pico-ROS (https://github.com/Pico-ROS/Pico-ROS-software) built into the
# application, together with the Micro-CDR codec picoserdes is written against.
# An application picks it up with:
#
#   set(PICOROS_USER_TYPE_FILE "${CMAKE_CURRENT_LIST_DIR}/src/<app>_types.h")
#   include(${CMAKE_CURRENT_LIST_DIR}/../common/picoros/picoros.cmake)
#
# after find_package(Zephyr), and sources the matching Kconfig fragment with
# `rsource "../common/picoros/Kconfig"`.
#
# Why a shared cmake fragment and not a west module: neither upstream ships
# zephyr/module.yml, so west clones the sources but Zephyr's module scan skips
# both. Rather than carry module wrappers for two third-party trees (which
# CLAUDE.md rules out -- west owns those paths), the sources are added to the
# pre-existing `app` target, the same shape applications/common/watchdog uses
# and for the same reason: Zephyr reads ZEPHYR_LIBS before the application's
# CMakeLists runs, so a zephyr_library() declared here would compile but never
# link.
#
# zenoh-pico itself *is* a proper west module and keeps building as one; this
# fragment only adds the ROS client layer above it.

if(NOT CONFIG_PICOROS)
  return()
endif()

# west.yml clones both under <workspace>/deps/modules/lib/. WEST_TOPDIR is set
# by Zephyr's west.cmake; the fallback keeps a plain `cmake -DZEPHYR_BASE=...`
# build (no west in PATH) working.
if(DEFINED WEST_TOPDIR)
  set(_picoros_workspace "${WEST_TOPDIR}")
else()
  get_filename_component(_picoros_workspace "${ZEPHYR_BASE}/../.." ABSOLUTE)
endif()

set(PICOROS_DIR "${_picoros_workspace}/deps/modules/lib/picoros")
set(MICROCDR_DIR "${_picoros_workspace}/deps/modules/lib/micro-cdr")

foreach(dir IN ITEMS "${PICOROS_DIR}" "${MICROCDR_DIR}")
  if(NOT EXISTS "${dir}")
    message(FATAL_ERROR
      "CONFIG_PICOROS=y but ${dir} is missing. Run `mise run west-update`.")
  endif()
endforeach()

if(NOT PICOROS_USER_TYPE_FILE)
  message(FATAL_ERROR
    "CONFIG_PICOROS=y requires PICOROS_USER_TYPE_FILE to be set to the "
    "application's MSG_LIST/SRV_LIST header before including picoros.cmake. "
    "picoserdes generates serialize/deserialize code for every type in that "
    "list, so each application supplies a trimmed one rather than reusing "
    "upstream's 1300-line examples/example_types.h.")
endif()

zephyr_include_directories(
  ${PICOROS_DIR}/src
  ${MICROCDR_DIR}/include
  # ucdr/config.h: Micro-CDR's CMake normally generates this from config.h.in.
  # We are not running that CMake, so a checked-in equivalent lives here.
  ${CMAKE_CURRENT_LIST_DIR}
)

target_sources(app PRIVATE
  ${PICOROS_DIR}/src/picoros.c
  ${PICOROS_DIR}/src/picoserdes.c
  ${MICROCDR_DIR}/src/c/common.c
  ${MICROCDR_DIR}/src/c/types/array.c
  ${MICROCDR_DIR}/src/c/types/basic.c
  ${MICROCDR_DIR}/src/c/types/sequence.c
  ${MICROCDR_DIR}/src/c/types/string.c
)

target_sources_ifdef(CONFIG_PICOROS_PARAMS app PRIVATE
  ${PICOROS_DIR}/src/picoparams.c
)

# picoserdes.c and every translation unit that includes picoserdes.h must see
# the same MSG_LIST/SRV_LIST, so this is a global definition rather than a
# property of one source file.
zephyr_compile_definitions(USER_TYPE_FILE="${PICOROS_USER_TYPE_FILE}")
