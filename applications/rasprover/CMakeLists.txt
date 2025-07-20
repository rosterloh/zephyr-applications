cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(rasprover)

# We always want colored gcc output
zephyr_compile_options(-fdiagnostics-color=always)

target_sources(app PRIVATE 
  src/main.c
  src/app_display.c
  src/app_network.c
  src/app_sensors.c
  src/app_settings.c
)