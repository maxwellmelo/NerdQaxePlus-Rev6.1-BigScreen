// Ported from the approved design to LVGL 8.3 (DISPLAY_PROFILE_
// YYSLUPING_480X320). "Zen": just the hashrate, huge, and a thin bar that
// shows how much of the frequency ceiling the thermal governor is using
// right now (and, by its color, whether heat is holding it back).
//
// Before this pass: a skeleton
// that only proved create/update/destroy and the UiState pipeline
// worked -- generic BIG_* dashboard palette, built-in OpenSans bitmap fonts,
// no bar at all.
//
// After: the approved design's own palette (deep teal #0f2b2c / warm ivory
// #f3e9d2) and its own type (Instrument Serif hero digits, Archivo secondary
// text), plus the frequency-headroom bar with its 3-state color rule.

#include "../fonts/fonts.h"
#include "../ui_state.h"
#include "nq_fmt.h"
#include "screen.h"
#include "screen_common.h"

#include <cmath>
#include <cstdio>

namespace
{

// ---- 19-zen's own palette (the approved design's CSS) ------
// Deliberately NOT the generic BIG_* palette in ui.cpp / screen_common.h's
// SCR_* constants: this screen (like 01-painel and 14-hashrate) has its own
// identity per CONTRATO.md ("cada tela tem identidade própria, tirada do
// assunto dela").
constexpr uint32_t ZEN_BG = 0x0f2b2c;
constexpr uint32_t ZEN_INK = 0xf3e9d2;    // main text / hero number
constexpr uint32_t ZEN_MUTED = 0xa9c3bd;  // unit label, footer base color
constexpr uint32_t ZEN_RAIL = 0x24494a;   // bar track (empty part)
constexpr uint32_t ZEN_GREEN = 0x8fbf9f;  // bar: normal
constexpr uint32_t ZEN_ORANGE = 0xd07a3c; // bar: low thermal headroom
constexpr uint32_t ZEN_RED = 0xe5484d;    // bar: governor EMERGENCY

// Rail geometry (the approved design: left:36 right:36 bottom:54, so on a
// 480-wide screen it runs x=36.444, width 408; bottom:54 -> y = 320-54 = 266).
constexpr int RAIL_X = 36;
constexpr int RAIL_Y = 266;
constexpr int RAIL_W = 480 - 2 * RAIL_X; // 408
constexpr int RAIL_H = 2;
constexpr int FILL_H = 4;

class S19ZenScreen : public Screen {
  public:
    int number() const override
    {
        return 19;
    }
    const char *name() const override
    {
        return "zen";
    }

    void create(lv_obj_t *parent) override
    {
        if (m_root) {
            destroy();
        }
        m_root = screenMakeRoot(parent, ZEN_BG);

        //  Hero number: the approved design splits "6,31" into a big "6" + a `<small>`
        //  ",31" at ~49% of the hero size (92px vs 188px). Only ONE
        //  Instrument Serif cut was generated (font_instrument_serif_hero,
        //  188px -- see main/displays/fonts/fonts.h) since a second ~92px
        //  weight would cost more flash for a screen that is meant to be
        //  cheap; LVGL 8 also has no runtime font up/down-scaling for
        //  bitmap fonts. Simplification: the whole "6,31" string renders at
        //  the same 188px size in one label, losing the big/small size
        //  contrast but keeping every digit exactly as legible (well above
        //  the >=64px hero-number rule).
        m_hashLabel = screenMakeLabel(m_root, "--", 30, 34, 420, &font_instrument_serif_hero, ZEN_INK, LV_TEXT_ALIGN_LEFT);

        screenMakeLabel(m_root, "terahashes per second", 36, 214, 300, &font_archivo_regular_19, ZEN_MUTED);

        //  Rail (static track) + fill (dynamic bar) + cap (static end-of-rail
        //  tick, purely decorative in the approved design -- it is NOT tied to any
        //  data, just marks where 100% would be).
        m_rail = lv_obj_create(m_root);
        lv_obj_set_pos(m_rail, RAIL_X, RAIL_Y);
        lv_obj_set_size(m_rail, RAIL_W, RAIL_H);
        lv_obj_set_style_bg_color(m_rail, lv_color_hex(ZEN_RAIL), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(m_rail, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(m_rail, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(m_rail, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(m_rail, 0, LV_PART_MAIN);
        lv_obj_clear_flag(m_rail, LV_OBJ_FLAG_SCROLLABLE);

        m_fill = lv_obj_create(m_root);
        lv_obj_set_pos(m_fill, RAIL_X, RAIL_Y - 1);
        lv_obj_set_size(m_fill, (lv_coord_t) (RAIL_W * 0.06f), FILL_H);
        lv_obj_set_style_bg_color(m_fill, lv_color_hex(ZEN_GREEN), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(m_fill, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(m_fill, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(m_fill, 2, LV_PART_MAIN);
        lv_obj_set_style_pad_all(m_fill, 0, LV_PART_MAIN);
        lv_obj_clear_flag(m_fill, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *cap = lv_obj_create(m_root);
        lv_obj_set_pos(cap, RAIL_X + RAIL_W - 2, RAIL_Y - 6);
        lv_obj_set_size(cap, 2, 14);
        lv_obj_set_style_bg_color(cap, lv_color_hex(ZEN_INK), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(cap, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(cap, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(cap, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(cap, 0, LV_PART_MAIN);
        lv_obj_clear_flag(cap, LV_OBJ_FLAG_SCROLLABLE);

        // Footer: "<freq> MHz of <freqCap>" / "regulator at <vrTemp> °C". The
        // approved design bolds only the numeric part of each sentence (<b> in an
        // otherwise-muted <span>); LVGL 8 labels support exactly this kind of inline
        // mixed-color text via lv_label_set_recolor + "#RRGGBB text#" markup, so one
        // label per side reproduces the "regular sentence, emphasized number" look
        // without a second label to position (bold WEIGHT itself isn't reproduced --
        // Archivo Regular 15px has no Bold cut generated -- color contrast carries
        // the same "this is the important part" cue instead). Left/right zones kept
        // narrower than the full 36.444 footer span (with a gap in the middle) so the
        // two labels' bounding boxes never overlap even at the longest realistic
        // values, without having to measure text width at runtime.
        m_footLeft = screenMakeLabel(m_root, "", 36, 284, 200, &font_archivo_regular_15, ZEN_MUTED);
        lv_label_set_recolor(m_footLeft, true);
        m_footRight = screenMakeLabel(m_root, "", 244, 284, 200, &font_archivo_regular_15, ZEN_MUTED, LV_TEXT_ALIGN_RIGHT);
        lv_label_set_recolor(m_footRight, true);
    }

    void update(const UiState &s) override
    {
        if (!m_root) {
            return;
        }

        //  Hero: the approved design reference uses hashrate.m1 (1-minute average), which is
        //  UiState::hr1m.
        if (std::isnan(s.hr1m)) {
            lv_label_set_text(m_hashLabel, "--");
        } else {
            char buf[24];
            nqfmt::ths(buf, sizeof(buf), s.hr1m, 2);
            lv_label_set_text(m_hashLabel, buf);
        }

        //  Bar: NQ.map(freq, fmin, freqCap, 0.06, 1) -- never fully empty
        //  (6% floor) so the bar always reads as "a bar", not a blank rail.
        float used = nqfmt::mapf(s.freq, s.fmin, s.freqCap, 0.06f, 1.0f);
        lv_coord_t fillW = (lv_coord_t) (used * RAIL_W);
        if (fillW < 2) {
            fillW = 2;
        }
        lv_obj_set_width(m_fill, fillW);

        bool hot = !std::isnan(s.headroom) && s.headroom < 1.5f;
        uint32_t color = ZEN_GREEN;
        if (s.govState == UI_GOV_EMERGENCY) {
            color = ZEN_RED;
        } else if (hot) {
            color = ZEN_ORANGE;
        }
        lv_obj_set_style_bg_color(m_fill, lv_color_hex(color), LV_PART_MAIN);

        char freqBuf[16], capBuf[16], vrBuf[16], line[96];
        nqfmt::n0(freqBuf, sizeof(freqBuf), s.freq);
        nqfmt::n0(capBuf, sizeof(capBuf), s.freqCap);
        nqfmt::n0(vrBuf, sizeof(vrBuf), s.vrTemp);

        snprintf(line, sizeof(line), "#f3e9d2 %s MHz# of %s", freqBuf, capBuf);
        lv_label_set_text(m_footLeft, line);

        snprintf(line, sizeof(line),
                 "regulator at #f3e9d2 %s \xc2\xb0"
                 "C#",
                 vrBuf);
        lv_label_set_text(m_footRight, line);
    }

    void destroy() override
    {
        if (m_root) {
            lv_obj_del(m_root);
            m_root = nullptr;
            m_hashLabel = nullptr;
            m_rail = nullptr;
            m_fill = nullptr;
            m_footLeft = nullptr;
            m_footRight = nullptr;
        }
    }

  private:
    lv_obj_t *m_root = nullptr;
    lv_obj_t *m_hashLabel = nullptr;
    lv_obj_t *m_rail = nullptr;
    lv_obj_t *m_fill = nullptr;
    lv_obj_t *m_footLeft = nullptr;
    lv_obj_t *m_footRight = nullptr;
};

S19ZenScreen s_instance;

} // namespace

Screen *screenGetS19Zen()
{
    return &s_instance;
}
