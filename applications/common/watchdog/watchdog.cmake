# Shared task-watchdog helper. An application picks it up with:
#
#   include(${CMAKE_CURRENT_LIST_DIR}/../common/watchdog/watchdog.cmake)
#
# after find_package(Zephyr), and sources the matching Kconfig fragment with
# `rsource "../common/watchdog/Kconfig"`.
#
# include(), not add_subdirectory() with a zephyr_library(): Zephyr reads its
# ZEPHYR_LIBS global property while processing its own CMakeLists.txt, which
# find_package(Zephyr) has already finished by the time the application's
# CMakeLists runs. A zephyr_library() registered after that point compiles but
# is never linked, which shows up as undefined references to every symbol in
# it. Out-of-tree *modules* escape this because west adds them during Zephyr's
# own processing; application-side shared code does not. Adding the source to
# the pre-existing `app` target sidesteps the ordering entirely.
#
# The include directory is added unconditionally so app_watchdog.h resolves --
# and supplies its no-op inline fallbacks -- even when CONFIG_APP_WATCHDOG is
# disabled.

zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR})

target_sources_ifdef(CONFIG_APP_WATCHDOG app PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/app_watchdog.c)
