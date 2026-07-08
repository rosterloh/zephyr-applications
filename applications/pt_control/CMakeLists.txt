cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(pt_control)

# We always want colored gcc output
zephyr_compile_options(-fdiagnostics-color=always)

target_sources(app PRIVATE
  src/main.c
  src/app_gimbal.c
  src/app_ros_cdr.c
)

target_sources_ifdef(CONFIG_NETWORKING    app PRIVATE src/app_network.c)
target_sources_ifdef(CONFIG_APP_ZENOH     app PRIVATE src/app_zenoh.c)
target_sources_ifdef(CONFIG_APP_TIME_SYNC app PRIVATE src/app_time.c)
target_sources_ifdef(CONFIG_APP_DISPLAY   app PRIVATE src/app_display.c src/cfb_font_unscii8.c)
