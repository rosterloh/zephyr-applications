# Ztest suite for the rasprover ROS 2 CDR encoders/decoder.
#
# app_ros_cdr.c is pure (writes into a caller-supplied buffer, no kernel, no
# net, no fs), so only it and the CDR codec are linked in. The codec comes from
# the west-managed rosterloh-drivers module via CONFIG_CDR.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ros_cdr)

set(APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

target_sources(app PRIVATE
	src/main.c
	${APP_SRC}/app_ros_cdr.c
)
target_include_directories(app PRIVATE ${APP_SRC})
