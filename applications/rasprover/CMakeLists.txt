cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(rasprover)

# We always want colored gcc output
zephyr_compile_options(-fdiagnostics-color=always)

target_sources(app PRIVATE
  src/main.c
  src/app_display.c
  src/app_sensors.c
  src/app_settings.c
)

target_sources_ifdef(CONFIG_NETWORKING app PRIVATE src/app_network.c)
target_sources_ifdef(CONFIG_APP_ZENOH  app PRIVATE src/app_zenoh.c)

# On Debian/Ubuntu, libsdl2-dev uses a multiarch include path that Zephyr's
# build doesn't pick up automatically. Add it via pkg-config when present.
# On Debian/Ubuntu, libsdl2-dev puts _real_SDL_config.h under a multiarch
# path (/usr/include/<triplet>/SDL2/) that Zephyr's NSI bottom-layer build
# doesn't find. NSI_BUILD_OPTIONS comes from native_simulator's compile
# options, so we add the multiarch path there.
if(CONFIG_SDL_DISPLAY)
  execute_process(
    COMMAND gcc -dumpmachine
    OUTPUT_VARIABLE _host_triplet OUTPUT_STRIP_TRAILING_WHITESPACE)
  set(_sdl_multiarch "/usr/include/${_host_triplet}")
  if(EXISTS "${_sdl_multiarch}/SDL2/_real_SDL_config.h")
    # For CMake (Zephyr kernel) targets
    zephyr_include_directories("${_sdl_multiarch}")
    # For the NSI bottom-layer Makefile (display_sdl_bottom.c etc.)
    if(TARGET native_simulator)
      target_compile_options(native_simulator INTERFACE "-I${_sdl_multiarch}")
    endif()
  endif()
endif()
