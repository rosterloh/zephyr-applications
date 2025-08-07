cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(embedded_vision)

# We always want colored gcc output
zephyr_compile_options(-fdiagnostics-color=always)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})