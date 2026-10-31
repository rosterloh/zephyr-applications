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
  # Roots the service's chain can end in. It is behind Cloudflare, which
  # issues from Google Trust Services (2026-10-31: WE1 -> GTS Root R4) and
  # also Let's Encrypt; R1 is the GTS RSA root.
  foreach(ca gts_root_r4 gts_root_r1 isrg_root_x1)
    generate_inc_file_for_target(app ${CMAKE_CURRENT_LIST_DIR}/${ca}.der
      ${ZEPHYR_BINARY_DIR}/include/generated/prusa_camera_${ca}.der.inc)
  endforeach()
endif()
