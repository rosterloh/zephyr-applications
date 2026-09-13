# SPDX-License-Identifier: Apache-2.0
#
# Locate and compile the vendored pstop_c core. Mirrors upstream's own ESP-IDF
# wrapper (components/pstop/CMakeLists.txt): pstop_c is not a Zephyr module
# (it has no zephyr/module.yml upstream yet), so we reference its path directly.
#
# Included by the app CMakeLists AND by each test CMakeLists, so the source
# list lives in exactly one place.

set(PSTOP_C "${WEST_TOPDIR}/deps/modules/lib/protective-stop/pstop_c")

if(NOT EXISTS "${PSTOP_C}/pstop/include/pstop/pstop_msg.h")
  message(FATAL_ERROR
    "pstop_c not found at ${PSTOP_C}\n"
    "Run: mise run west-update")
endif()

# A REMOTE needs 6 of the 11 core sources. machine.c, protocol.c,
# pstop_application.c and pstop_remote_data.c are machine-side and are
# deliberately not compiled. Upstream's time.c only implements __linux__ and
# returns 0 elsewhere, so src/pstop_time.c replaces it.
set(PSTOP_C_SRCS
  "${PSTOP_C}/pstop/src/pstop/checksum.c"
  "${PSTOP_C}/pstop/src/pstop/device_id.c"
  "${PSTOP_C}/pstop/src/pstop/endian.c"
  "${PSTOP_C}/pstop/src/pstop/os.c"
  "${PSTOP_C}/pstop/src/pstop/protocol_data.c"
  "${PSTOP_C}/pstop/src/pstop/pstop_msg.c"
)

set(PSTOP_C_INCLUDE "${PSTOP_C}/pstop/include")
