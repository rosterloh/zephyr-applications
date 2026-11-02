# SNTP-synced realtime clock for ROS message stamps. An application picks it up with:
#
#   include(${CMAKE_CURRENT_LIST_DIR}/../common/time/time.cmake)
#
# after find_package(Zephyr), sources `rsource "../common/time/Kconfig"`, and
# calls app_time_start() once its network is up. Same include() shape as
# ../watchdog/ and for the same reason.
#
# The include directory is added unconditionally so app_time.h resolves -- and
# supplies its zero-stamp inline fallbacks -- even when CONFIG_APP_TIME_SYNC is
# disabled.

zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR})

target_sources_ifdef(CONFIG_APP_TIME_SYNC app PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/app_time.c)
