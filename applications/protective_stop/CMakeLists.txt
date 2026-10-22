cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(protective_stop)

zephyr_compile_options(-fdiagnostics-color=always)

target_sources(app PRIVATE
  src/main.c
  src/estop_verdict.c
  src/estop_gpio.c
  src/app_settings.c
  src/session.c
  src/lockstep.c
)

if(CONFIG_PROTECTIVE_STOP_HTTP)
  target_sources(app PRIVATE
    src/http_util.c
    src/api.c
    src/http_server.c
  )
  zephyr_linker_sources(SECTIONS sections-rom.ld)
  zephyr_linker_section(NAME http_resource_desc_pstop_http
    KVMA RAM_REGION GROUP RODATA_REGION
  )
endif()

target_include_directories(app PRIVATE src)
