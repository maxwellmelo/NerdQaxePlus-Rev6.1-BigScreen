#pragma once

// Tiny shared helpers for the placeholder / pilot screens in
// main/displays/screens/. Deliberately reuses the same placeholder palette as
// bigPanel/bigText in./ui.cpp (BIG_* constants) -- this is NOT the final
// pixel-perfect styling from each the approved design's own palette, just
// enough for the skeleton to look like a screen and prove the data pipeline
// (uiDataFill -> UiState -> Screen::update) reaches real LVGL widgets. The 3
// pilot screens will be redone with full visual fidelity in a later pass.

#include "lvgl.h"
// ui.h is pulled in only for its ui_font_OpenSansBold13/14/24/45 declarations,
// and NOTHING in this header or in the 3 visually-ported pilot screens
// (s19_zen.cpp/s01_painel.cpp/s14_hashrate.cpp) actually references any
// ui_font_* symbol any more (verified by grep -- their own generated fonts
// in./fonts/fonts.h are used instead). ui.h itself pulls in./boards/board.h ->
// asic.h/bm1368.h/nvs_config.h, which need real ESP-IDF headers (driver/*,
// nvs_flash,.) and cannot compile on a PC host. The PC screen simulator
// (tools/sim, see tools/sim/README.md and ) compiles this header directly
// against the real./ui_state.h + these same 3 screens, so this include is
// guarded out ONLY for that host build (NQSIM_HOST_BUILD, defined solely by
// tools/sim/CMakeLists.txt -- never defined by the real ESP-IDF build, so the
// firmware is unaffected).
#ifndef NQSIM_HOST_BUILD
#include "../ui.h" // font declarations: ui_font_OpenSansBold13/14/24/45
#endif

#include <math.h>
#include <stdio.h>

constexpr uint32_t SCR_BG = 0x0E1113;     // BIG_BG
constexpr uint32_t SCR_HEADER = 0x181E22; // BIG_HEADER
constexpr uint32_t SCR_PANEL = 0x1E2529;  // BIG_PANEL
constexpr uint32_t SCR_TEXT = 0xF4F7F8;   // BIG_TEXT
constexpr uint32_t SCR_MUTED = 0xA8B3B9;  // BIG_MUTED
constexpr uint32_t SCR_GREEN = 0x58D68D;  // BIG_GREEN
constexpr uint32_t SCR_ORANGE = 0xF7931A; // BIG_ORANGE

// Creates a full-cover (480x320) opaque container as the LAST (topmost)
// child of `parent`. This hides whatever `parent` already draws (e.g. the
// legacy static ui_MiningScreen panels) without having to touch those
// objects at all. destroying a screen is then just lv_obj_del on this
// one root: every label/widget the screen created is a descendant and is
// freed with it -- no per-widget bookkeeping needed.
inline lv_obj_t *screenMakeRoot(lv_obj_t *parent, uint32_t bgColor)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_size(root, 480, 320);
    lv_obj_set_style_bg_color(root, lv_color_hex(bgColor), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(root, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(root, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    return root;
}

inline lv_obj_t *screenMakeLabel(lv_obj_t *parent, const char *text, int x, int y, int w, const lv_font_t *font, uint32_t color,
                                 lv_text_align_t align = LV_TEXT_ALIGN_LEFT)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(lbl, x, y);
    lv_obj_set_size(lbl, w, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(lbl, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(lbl, align, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(lbl, text);
    return lbl;
}

// NAN-safe formatting: a UiState field that is not available yet is NAN and
// must show "--", never an invented number (0.00, etc).
inline void screenFmtFloat(char *buf, size_t n, float v, const char *unit, int decimals = 2)
{
    if (isnan(v)) {
        snprintf(buf, n, "--");
        return;
    }
    if (unit && unit[0]) {
        snprintf(buf, n, "%.*f %s", decimals, (double) v, unit);
    } else {
        snprintf(buf, n, "%.*f", decimals, (double) v);
    }
}
