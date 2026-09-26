cmake_minimum_required(VERSION 3.16)
project(nqsim LANGUAGES C CXX)

#  -----------------------------------------------------------------------
# nqsim - PC screen simulator for the NerdQAxe++ display-reference designs
# pilot screens (19-zen, 01-painel, 14-hashrate). tools/sim/README.md for the
# one-line Docker command to build and run this, and (tools/sim) for the full
# design write-up / decisions this file implements.
#
#  Compiles the REAL screen code, unmodified, straight from its real
#  location in the firmware tree (main/displays/screens/*.cpp) against the
#  REAL LVGL 8.3.11 vendored in managed_components/ and the REAL
#  main/displays/lv_conf.h -- nothing under main/ is copied or duplicated
#  here. Only 3 things are added, all confined to this directory:
#  1. host_shim/lvgl_psram.h - a malloc/free/realloc-based stand-in for
#  main/lvgl_psram.h (which needs ESP-IDF's heap_caps_* PSRAM API).
#  2. This CMakeLists.txt + src/main.cpp - the simulator driver itself
#  (no such thing exists on real hardware: DisplayDriver drives LVGL
#  there; here, main.cpp does, directly, per the task's "instantiate
#  screens directly, no ScreenManager/DisplayDriver needed").
#  3. third_party/lodepng - vendored PNG encoder (MIT/zlib license, see
#  third_party/lodepng/LICENSE), used only to write the output PNGs.
#  -----------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE)
 set(CMAKE_BUILD_TYPE Release)
endif()

# Root of the firmware checkout (tools/sim/./.).
get_filename_component(NQPP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

set(LVGL_DIR "${NQPP_ROOT}/managed_components/lvgl__lvgl")
set(DISPLAYS_DIR "${NQPP_ROOT}/main/displays")
set(SCREENS_DIR "${DISPLAYS_DIR}/screens")
set(FONTS_DIR "${DISPLAYS_DIR}/fonts")

if(NOT EXISTS "${LVGL_DIR}/lvgl.h")
 message(FATAL_ERROR "LVGL not found at ${LVGL_DIR} -- is C:/esp/nqpp mounted?")
endif()

#  -----------------------------------------------------------------------
#  LVGL 8.3.11, built from the vendored source with its own standalone
#  CMakeLists (managed_components/lvgl__lvgl/env_support/cmake/custom.cmake,
#  selected automatically because ESP_PLATFORM is not defined here).
#
#  LV_CONF_PATH points at host_shim/lv_conf.h, a thin wrapper that
#  `#include`s the REAL main/displays/lv_conf.h verbatim and nothing else
#  (it is NOT copied/duplicated, just #included, and as of the 2026-09-22
#  firmware review pass applies NO overrides at all -- see that file's own
#  comment for the earlier LV_USE_FONT_COMPRESSED workaround this
#  superseded once main/displays/lv_conf.h was fixed at the source). The
#  simulator's LVGL build therefore sees EXACTLY the same lv_conf.h content
#  as the real firmware build, with zero risk of the two diverging.
#
#  LV_COLOR_16_SWAP: main/displays/lv_conf.h itself already sets this to 0
#  (confirmed by reading the file, and left untouched by the wrapper
#  above). The firmware's sdkconfig separately sets
#  CONFIG_LV_COLOR_16_SWAP=y for the real ESP32 SPI panel, which this
#  lv_conf.h does not read (LV_COLOR_16_SWAP here is a plain macro, not
#  wired to Kconfig) -- that mismatch is a pre-existing firmware-side
#  inconsistency, not something this simulator introduces, and is called
#  out again in as a KNOWN RISK: this
#  simulator renders with swap=0 (matches lv_conf.h) and is therefore a
#  faithful preview of DRAWING/COLORS for review, but NOT a bit-for-bit
#  replica of the SPI byte order the real panel would receive if
#  CONFIG_LV_COLOR_16_SWAP=y actually takes effect at runtime. Needs
#  confirming on real hardware later.
#  -----------------------------------------------------------------------
set(LV_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/host_shim/lv_conf.h" CACHE STRING "" FORCE)
set(LV_CONF_INCLUDE_SIMPLE ON CACHE BOOL "" FORCE)
set(LV_LVGL_H_INCLUDE_SIMPLE ON CACHE BOOL "" FORCE)

add_subdirectory("${LVGL_DIR}" lvgl_build)

# lv_conf.h's `#include <lvgl_psram.h>` (LV_MEM_CUSTOM path) needs a provider
# on the include path. The REAL one lives in main/lvgl_psram.h and needs ESP-
# IDF; main/ is deliberately never added to the `lvgl` target's include path,
# so this host_shim/ directory -- and ONLY this directory -- is what the
# simulator's LVGL build sees for that header. host_shim/lvgl_psram.h for the
# full rationale. (custom.cmake already adds LV_CONF_DIR, i.e. this same
# host_shim/ directory, to the `lvgl` target's include path because that's
# where LV_CONF_PATH now lives; this second call is kept anyway so the intent
# -- "host_shim/ must be reachable" -- doesn't silently depend on that
# LV_CONF_PATH side effect.)
target_include_directories(lvgl SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/host_shim")

#  -----------------------------------------------------------------------
#  lodepng (vendored, MIT/zlib license -- see third_party/lodepng/LICENSE).
#  Downloaded from https://github.com/lvandeve/lodepng (master) for this
#  task; used only by src/main.cpp to write the output PNG files.
#  -----------------------------------------------------------------------
add_library(lodepng STATIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lodepng/lodepng.cpp")
target_include_directories(lodepng PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lodepng")

#  -----------------------------------------------------------------------
#  The simulator executable itself. Sources:
#  - src/main.cpp: the simulator driver (display driver, scenarios, PNG
#  output) -- the only genuinely new code in this task.
#  - The REAL 3 pilot screens + their shared helper, compiled straight
#  from main/displays/screens/ (NOT copied).
#  - The REAL ui_state.cpp (uiStateSetDefaults), compiled straight from
#  main/displays/ (NOT copied).
#  - The REAL 11 generated bitmap fonts, compiled straight from
#  main/displays/fonts/ (NOT copied).
#  -----------------------------------------------------------------------

#  ---------------------------------------------------------------------------
#  The firmware's LVGL is configured by Kconfig (build-16mb/sdkconfig), not by
#  main/displays/lv_conf.h. host_shim/lv_conf.h mirrors the options that change
#  rendering; refuse to build if they no longer match the firmware.
set(NQ_FW_SDKCONFIG "${CMAKE_CURRENT_SOURCE_DIR}/../../build-16mb/sdkconfig")
if(EXISTS "${NQ_FW_SDKCONFIG}")
 file(STRINGS "${NQ_FW_SDKCONFIG}" NQ_FW_LV REGEX "CONFIG_LV_(USE_FONT_COMPRESSED|COLOR_MIX_ROUND_OFS|FONT_FMT_TXT_LARGE)")
 foreach(expect "CONFIG_LV_USE_FONT_COMPRESSED=y" "CONFIG_LV_COLOR_MIX_ROUND_OFS=128" "# CONFIG_LV_FONT_FMT_TXT_LARGE is not set")
 list(FIND NQ_FW_LV "${expect}" NQ_IDX)
 if(NQ_IDX EQUAL -1)
 message(FATAL_ERROR "nqsim: firmware sdkconfig differs from host_shim/lv_conf.h (expected '${expect}'). Update the shim to match the firmware.")
 endif()
 endforeach()
else()
 message(WARNING "nqsim: ${NQ_FW_SDKCONFIG} not found - cannot check that the simulator's LVGL config matches the firmware")
endif()

#  Screens and fonts by glob (same set the firmware build picks up), so a
#  ported screen shows up in the simulator without editing this file.
file(GLOB NQ_SCREEN_SRCS CONFIGURE_DEPENDS "${SCREENS_DIR}/s[0-9][0-9]_*.cpp")
file(GLOB NQ_FONT_SRCS CONFIGURE_DEPENDS "${FONTS_DIR}/*.c")
add_executable(nqsim
 src/main.cpp
 "${SCREENS_DIR}/screen_registry.cpp"
 ${NQ_SCREEN_SRCS}
 "${DISPLAYS_DIR}/ui_state.cpp"
 ${NQ_FONT_SRCS}
)

target_include_directories(nqsim PRIVATE
 "${DISPLAYS_DIR}"
 "${SCREENS_DIR}"
 "${FONTS_DIR}"
)

# NQSIM_HOST_BUILD: gates the one guarded include in screen_common.h (see that
# file and ) that skips pulling in./ui.h's ESP-IDF-only transitive dependencies
# (boards/board.h -> asic.h/bm1368.h/nvs_config.h) on the host build. Never
# defined for the real ESP-IDF build, so the firmware itself is unaffected.
target_compile_definitions(nqsim PRIVATE NQSIM_HOST_BUILD=1)

target_link_libraries(nqsim PRIVATE lvgl lodepng)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
 target_compile_options(nqsim PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

install(TARGETS nqsim RUNTIME DESTINATION bin)
