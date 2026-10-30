# GNU build ID: a SHA-1 of the linked image, so a coredump or log pulled off a
# device can be matched to the exact zephyr.elf it came from. An application
# picks it up with:
#
#   include(${CMAKE_CURRENT_LIST_DIR}/../common/build_id/build_id.cmake)
#
# after find_package(Zephyr), plus `rsource "../common/build_id/Kconfig"`.
# include() rather than a zephyr_library(); see ../watchdog/watchdog.cmake.

zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR})

if(CONFIG_APP_BUILD_ID)
  # Zephyr's ld/xt-ld base flags pass --build-id=none; the later option wins.
  zephyr_ld_options(-Wl,--build-id=sha1)
  # Inside rodata, so the note is loaded and survives objcopy to zephyr.bin.
  zephyr_linker_sources(RODATA ${CMAKE_CURRENT_LIST_DIR}/build_id.ld)
  target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/app_build_id.c)

  # build_id.ld keeps the note PROGBITS, which `readelf -n` does not read;
  # add an unloaded SHT_NOTE copy so it does (elf2image skips address 0). A
  # target of its own: Zephyr has already read extra_post_build_commands by
  # the time an app's CMakeLists runs.
  set(elf ${ZEPHYR_BINARY_DIR}/${KERNEL_ELF_NAME})
  set(note ${ZEPHYR_BINARY_DIR}/build_id.note)
  add_custom_target(build_id_note ALL
    COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/../../../scripts/build_id.py
      ${elf} --note ${note}
    COMMAND ${CMAKE_OBJCOPY} --remove-section .note.gnu.build-id
      --add-section .note.gnu.build-id=${note}
      --set-section-flags .note.gnu.build-id=readonly
      ${elf}
    DEPENDS ${logical_target_for_zephyr_elf}
    VERBATIM
  )
endif()
