# Point to root directory.
set(REPO_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR} CACHE PATH "Repo root directory")

if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/include)
  zephyr_syscall_include_directories(include)
  zephyr_include_directories(include)
endif()

add_subdirectory(drivers)
# add_subdirectory(lib)