# Shared HTTP request helpers (query string, basic auth, number parsing).
# An application picks them up with:
#
#   include(${CMAKE_CURRENT_LIST_DIR}/../common/http_util/http_util.cmake)
#
# Plain C with no Kconfig of its own; see ../watchdog/watchdog.cmake for why
# this adds to the `app` target instead of declaring a zephyr_library().

zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR})
target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/http_util.c)
