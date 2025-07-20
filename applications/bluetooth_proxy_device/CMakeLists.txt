cmake_minimum_required(VERSION 3.20.0)

# if(NOT EXISTS $ENV{GATEWAY_DEVICE_ID})
#   message(FATAL "Build requires GATEWAY_DEVICE_ID variable in environment")
# else()
#   message(STATUS "GATEWAY_DEVICE_ID=$ENV{GATEWAY_DEVICE_ID}")
# endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bluetooth_proxy_device)

FILE(GLOB app_sources src/*.c)

target_include_directories(app PRIVATE src)
target_sources(app PRIVATE ${app_sources})