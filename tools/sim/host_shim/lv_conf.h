#pragma once

// host_shim/lv_conf.h - simulator-only LVGL config.
//
// The FIRMWARE build does not read main/displays/lv_conf.h at all: ESP-IDF
// builds LVGL from Kconfig (CONFIG_LV_CONF_SKIP=y in the sdkconfig). The
// simulator has no Kconfig, so it starts from main/displays/lv_conf.h and then
// forces the options that change what is drawn to the values the firmware
// really uses. tools/sim/CMakeLists.txt checks these against
// build-16mb/sdkconfig and refuses to build if they diverge.
//
// 2026-09-23: this file used to claim the two builds always saw identical
// config. They did not: LV_USE_FONT_COMPRESSED was 1 here and n in the
// firmware, so the simulator rendered text that was blank on the device.

#include "../../../main/displays/lv_conf.h"

#undef LV_USE_FONT_COMPRESSED
#define LV_USE_FONT_COMPRESSED 1 // CONFIG_LV_USE_FONT_COMPRESSED=y

#undef LV_COLOR_MIX_ROUND_OFS
#define LV_COLOR_MIX_ROUND_OFS 128 // CONFIG_LV_COLOR_MIX_ROUND_OFS=128

#undef LV_FONT_FMT_TXT_LARGE
#define LV_FONT_FMT_TXT_LARGE 0 // CONFIG_LV_FONT_FMT_TXT_LARGE is not set
