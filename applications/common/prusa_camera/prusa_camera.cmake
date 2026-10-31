# Prusa camera service client. Include after find_package(Zephyr) and source
# the matching Kconfig with `rsource "../common/prusa_camera/Kconfig"`. See
# ../watchdog/watchdog.cmake for why this adds to `app` instead of declaring a
# zephyr_library().

zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR})

if(CONFIG_PRUSA_CAMERA)
  target_sources(app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/prusa_camera.c
    ${CMAKE_CURRENT_LIST_DIR}/prusa_camera_proto.c
  )
  # ISRG Root X1. shortcut: the camera-service chain is not verified yet; swap this file if it is not Let's Encrypt.
  generate_inc_file_for_target(app ${CMAKE_CURRENT_LIST_DIR}/prusa_camera_ca.der
    ${ZEPHYR_BINARY_DIR}/include/generated/prusa_camera_ca.der.inc)
endif()
