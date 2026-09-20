# Shared task-watchdog helper. An application picks it up with:
#
#   add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../common/watchdog watchdog)
#
# after find_package(Zephyr), and sources the matching Kconfig fragment with
# `rsource "../common/watchdog/Kconfig"`. The include directory is added
# unconditionally so app_watchdog.h resolves — and supplies its no-op inline
# fallbacks — even when CONFIG_APP_WATCHDOG is disabled.

zephyr_include_directories(.)

if(CONFIG_APP_WATCHDOG)
  zephyr_library()
  zephyr_library_sources(app_watchdog.c)
endif()
