cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(protective_stop)

# Shared application code (see applications/common/).
include(${CMAKE_CURRENT_LIST_DIR}/../common/build_id/build_id.cmake)

zephyr_compile_options(-fdiagnostics-color=always)

target_sources(app PRIVATE
  src/main.c
  src/estop_verdict.c
  src/estop_gpio.c
  src/app_settings.c
  src/session.c
  src/lockstep.c
  src/health_logic.c
  src/health.c
  src/ring.c
  src/leds.c
  src/uplink.c
  src/wifi.c
  src/crash.c
  src/log_ring.c
  src/last_log.c
  src/verbose.c
)

if(CONFIG_PROTECTIVE_STOP_HTTP)
  include(${CMAKE_CURRENT_LIST_DIR}/../common/http_util/http_util.cmake)
  target_sources(app PRIVATE
    src/api.c
    src/http_server.c
  )
  # The admin page ships gzipped; the browser inflates it.
  set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated)
  generate_inc_file_for_target(app src/admin.html ${gen_dir}/admin.html.gz.inc --gzip)
  zephyr_linker_sources(SECTIONS sections-rom.ld)
  zephyr_linker_section(NAME http_resource_desc_pstop_http
    KVMA RAM_REGION GROUP RODATA_REGION
  )
endif()

target_include_directories(app PRIVATE src)
