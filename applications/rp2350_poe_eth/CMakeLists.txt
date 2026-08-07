cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(rp2350_poe_eth LANGUAGES C)

target_sources(app PRIVATE
  src/main.c
  src/app_net.c
)

target_sources_ifdef(CONFIG_APP_STATUS_LED app PRIVATE src/app_status.c)
