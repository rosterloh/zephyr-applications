cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(codex_keys)

target_sources(app PRIVATE
  src/main.c
  src/codex_hid.c
  src/codex_json.c
  src/codex_rpc.c
)
