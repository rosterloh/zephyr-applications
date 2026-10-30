# Shared reset-reason record, pulled in by watchdog.cmake (the watchdog is its
# main writer). Gated by CONFIG_APP_RESET_REASON, which needs an
# `app_reset_reason` zephyr,retention node in the application's devicetree.

zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR})

target_sources_ifdef(CONFIG_APP_RESET_REASON app PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/app_reset_reason.c)
