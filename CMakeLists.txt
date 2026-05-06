if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/include)
  zephyr_syscall_include_directories(include)
  zephyr_include_directories(include)
endif()
