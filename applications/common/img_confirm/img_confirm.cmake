# Health-gated MCUboot image confirm. Include after find_package(Zephyr) and
# source the matching Kconfig with `rsource "../common/img_confirm/Kconfig"`.
# Needs ../watchdog/ for the revert deadline. See watchdog.cmake for why this
# adds to `app` instead of declaring a zephyr_library().

zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR})

target_sources_ifdef(CONFIG_APP_IMG_CONFIRM app PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/app_img_confirm.c)
