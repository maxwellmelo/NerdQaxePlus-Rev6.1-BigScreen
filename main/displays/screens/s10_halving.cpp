// Ported from the approved design to LVGL 8.3 (DISPLAY_PROFILE_
// YYSLUPING_480X320). "Halving": the 210.000-block cycle and the 2.016-block
// difficulty epoch as two concentric rings (an orrery, not a smartwatch
// face), a point orbiting the outer ring at the real cycle position, and the
// countdown/subsidy numbers as an old-almanac page.
//
// Before this pass: a placeholder (see git history / screen_registry.cpp)
// with one centered "10 Halving (em construcao)" label, no palette, no data,
// created only so the screen rotation/build/simulator wiring already existed
// before porting started.
//
// After: the approved design's own indigo/gold palette (#161033 background,
// #efe6d2 ink, #d4af6a gold), two lv_arc rings (each ring is ONE lv_arc: its
// LV_PART_MAIN draws the static track, its LV_PART_INDICATOR draws the data-
// driven progress -- LVGL's arc widget already IS "track + progress", so this
// needed no separate track/indicator pair of widgets the way the approved
// design's SVG did with 4 elements), a small orbiting dot repositioned by
// angle in update (never animated continuously), and the hero "dias" number in
// font_s13_s16_fraunces_bold_64 -- an EXISTING font (already generated for
// screens 13/16: Fraunces Bold, digits + ',' + '-' only, 64px) reused as-is,
// at zero extra flash cost. No new font was created for this screen at all.

#include "../fonts/fonts.h"
#include "../ui_state.h"
#include "nq_fmt.h"
#include "screen.h"
#include "screen_common.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

// Reused from screens 13 (24h) / 16 (sala): Fraunces Bold, 64px, glyphs
// 0x2C-0x2D (',' '-') + 0x30-0x39 (digits) only -- exactly the "digits +
// comma + hyphen" set this screen's hero number needs, so no new Fraunces
// cut was generated (see main/displays/fonts/fonts.h's top-of-file note:
// fonts NOT in that shared header are declared locally by whichever screen
// uses them, same pattern s07_sorte.cpp uses for its own font). This is a
// pure reuse: the.c file is already in main/displays/fonts/ and already
// picked up by the CMake glob, so this screen adds it to the link at zero
// marginal flash cost.
LV_FONT_DECLARE(font_s13_s16_fraunces_bold_64);

namespace
{

// ---- 10-halving's own palette (the approved design's
// CSS) -- deliberately its own identity, not the generic BIG_*/SCR_* palette
// (CONTRATO.md: "cada tela tem identidade propria, tirada do assunto dela").
constexpr uint32_t S10_BG = 0x161033;           // deep indigo night sky
constexpr uint32_t S10_INK = 0xefe6d2;          // main text / hero number
constexpr uint32_t S10_MUTED = 0xa89bcf;        // section labels, title
constexpr uint32_t S10_DATE = 0xcdc3ea;         // estimated-date line
constexpr uint32_t S10_TRACK_OUTER = 0x3c3163;  // outer ring: empty track
constexpr uint32_t S10_PROG_OUTER = 0xd4af6a;   // outer ring: cycle progress (gold)
constexpr uint32_t S10_TRACK_INNER = 0x2a2350;  // inner ring: empty track
constexpr uint32_t S10_PROG_INNER = 0xe9cf8f;   // inner ring: epoch progress (pale gold)
constexpr uint32_t S10_DOT = 0xf3e6b8;          // orbiting point
constexpr uint32_t S10_CAP_OUTER = 0xd4af6a;    // "ciclo de 210.000 blocos" caption
constexpr uint32_t S10_CAP_INNER = 0x9a8fd0;    // "epoca: 2.016 blocos" caption
constexpr uint32_t S10_SUBSIDY_GOLD = 0xd4af6a; // second subsidy value, bold

// Ring geometry -- same center/radii/stroke widths as the approved design's SVG
// (the approved design: CX=128, CY=156, R1=96, SW1=9, R2=64, SW2=6), so the two
// rings land pixel-for-pixel where the approved design puts them on the 480x320
// canvas.
constexpr int CX = 128, CY = 156;
constexpr int R1 = 96, SW1 = 9;
constexpr int R2 = 64, SW2 = 6;
constexpr int DOT_R = 5;  // the approved design.s10-dot: 10x10px circle
constexpr int HALO_R = 8; // the approved design's box-shadow ring, baked as a solid bg-colored disc

constexpr float PI_F = 3.14159265358979323846f;

// Minimum arc value (out of the 0.1000 range set on both rings) so a sliver of
// progress is always visible, even at cyclePos/epochPos == 0 -- mirrors the
// approved design's `Math.max(0.3, pos * 360)` (never a literally invisible
// 0-deg arc).
constexpr int16_t MIN_ARC_VALUE = 3;

// Right-column geometry (the approved design.s10-col: left:246 right:14 top:34
// bottom:14).
constexpr int COL_X = 246;
constexpr int COL_W = 480 - 14 - COL_X; // 220

// halvingDays formatted with ONE decimal, comma separator, but WITHOUT
// nqfmt's usual '.' thousands grouping: font_s13_s16_fraunces_bold_64 (the
// hero's font, reused from screens 13/16) only has digits + ',' + '-'
// glyphs, no '.'. halvingDays can reach ~1458 days right after a halving
// (210 000 blocks * 600 s / 86 400), so above 999 nqfmt::n1 would insert a
// thousands dot this font cannot render. Simplification: no thousands
// grouping on this one hero field (a day count reads fine without it, e.g.
// "1458,3" instead of "1.458,3") instead of spending flash on a second,
// wider Fraunces cut just to add one glyph.
void heroDaysText(char *buf, size_t n, float days)
{
    if (n == 0) {
        return;
    }
    if (std::isnan(days)) {
        snprintf(buf, n, "--");
        return;
    }
    char raw[nqfmt::kNumBufMax];
    nqfmt::n1(raw, sizeof(raw), days);
    size_t w = 0;
    for (size_t i = 0; raw[i] != '\0' && w + 1 < n; ++i) {
        if (raw[i] != '.') {
            buf[w++] = raw[i];
        }
    }
    buf[w] = '\0';
}

class S10HalvingScreen : public Screen {
  public:
    int number() const override
    {
        return 10;
    }
    const char *name() const override
    {
        return "halving";
    }

    void create(lv_obj_t *parent) override
    {
        if (m_root) {
            destroy();
        }
        m_root = screenMakeRoot(parent, S10_BG);

        screenMakeLabel(m_root, "Halving", 14, 12, 200, &font_archivo_medium_15, S10_MUTED);

        //  ---- outer ring: 210 000-block halving cycle (state.net.cyclePos) -- ONE
        //  lv_arc widget: its LV_PART_MAIN IS the static track (drawn full circle,
        //  0.360deg) and its LV_PART_INDICATOR IS the data-driven progress -- LVGL's
        //  arc widget already combines what the approved design's SVG needed 2
        //  separate <circle>/<path> elements for.
        m_outerArc = makeRingArc(R1, SW1, S10_TRACK_OUTER, S10_PROG_OUTER);
        //   ---- inner ring: 2016-block difficulty epoch (state.net.epochPos) -
        m_innerArc = makeRingArc(R2, SW2, S10_TRACK_INNER, S10_PROG_INNER);

        //   Fixed tick marking the halving boundary (0deg / top) on the outer
        //   ring -- static, drawn once, never touched by update (the approved design:
        //   a short radial <line> at angle 0).
        {
            constexpr int tickTop = CY - (R1 + SW1 / 2 + 2);
            constexpr int tickH = (R1 + SW1 / 2 + 2) - (R1 - SW1 / 2 - 2);
            lv_obj_t *tick = lv_obj_create(m_root);
            lv_obj_set_pos(tick, CX - 1, tickTop);
            lv_obj_set_size(tick, 2, tickH);
            lv_obj_set_style_bg_color(tick, lv_color_hex(S10_INK), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(tick, LV_OPA_80, LV_PART_MAIN);
            lv_obj_set_style_border_width(tick, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(tick, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(tick, 0, LV_PART_MAIN);
            lv_obj_clear_flag(tick, LV_OBJ_FLAG_SCROLLABLE);
        }

        //   Orbiting point (halo behind + dot on top). Positioned/hidden in
        //   update; hidden here so nothing flashes at (0,0) before the
        //   first update call.
        m_dotHalo = makeDot(HALO_R, S10_BG, S10_BG);
        m_dot = makeDot(DOT_R, S10_DOT, S10_DOT);
        lv_obj_add_flag(m_dotHalo, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(m_dot, LV_OBJ_FLAG_HIDDEN);

        //   Ring captions -- fixed protocol constants (210 000 / 2016 blocks),
        //   never change, so plain static text set once here.
        screenMakeLabel(m_root, "210,000-block cycle", 14, CY + R1 + 14, 226, &font_archivo_regular_15, S10_CAP_OUTER);
        screenMakeLabel(m_root, "epoch: 2,016 blocks", 14, CY + R1 + 32, 226, &font_archivo_regular_15, S10_CAP_INNER);

        //   ---- right column: countdown -----------------------------------
        lv_obj_t *lbl = screenMakeLabel(m_root, "left until next halving", COL_X, 34, COL_W, &font_archivo_medium_15, S10_MUTED);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);

        m_hero = screenMakeLabel(m_root, "--", COL_X, 78, COL_W, &font_s13_s16_fraunces_bold_64, S10_INK);

        screenMakeLabel(m_root, "days", COL_X, 144, 100, &font_archivo_regular_19, S10_MUTED);

        m_sub = screenMakeLabel(m_root, "", COL_X, 174, COL_W, &font_archivo_medium_17, S10_INK);
        lv_label_set_recolor(m_sub, true);
        lv_label_set_long_mode(m_sub, LV_LABEL_LONG_WRAP);

        m_date = screenMakeLabel(m_root, "", COL_X, 202, COL_W, &font_archivo_regular_15, S10_DATE);
        lv_label_set_long_mode(m_date, LV_LABEL_LONG_WRAP);

        //   Subsidy transition: fixed constants (this halving always cuts
        //   3.125 -> 1.5625 BTC), independent of UiState -- set once here,
        //   never touched by update. The approved design's "->" is a literal arrow glyph
        //   (U+2192); simplified to ASCII "->" here since none of the reused
        //   Archivo cuts include U+2192 and adding a whole new glyph to a
        //   shared font just for one arrow was not worth it.
        lv_obj_t *subsidy = screenMakeLabel(m_root, "", COL_X, 284, COL_W, &font_archivo_bold_20, S10_INK);
        lv_label_set_recolor(subsidy, true);
        lv_label_set_text(subsidy, "3.125 -> #d4af6a 1.5625 BTC#");
    }

    void update(const UiState &s) override
    {
        if (!m_root) {
            return;
        }

        bool valid = s.netValid;
        float cyclePos = valid ? nqfmt::clampf(s.cyclePos, 0.0f, 1.0f) : 0.0f;
        float epochFrac = valid ? nqfmt::clampf((float) s.epochPos / 2016.0f, 0.0f, 1.0f) : 0.0f;

        lv_arc_set_value(m_outerArc, valid ? arcValue(cyclePos) : 0);
        lv_arc_set_value(m_innerArc, valid ? arcValue(epochFrac) : 0);

        if (valid) {
            lv_obj_clear_flag(m_dot, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(m_dotHalo, LV_OBJ_FLAG_HIDDEN);
            float angleDeg = cyclePos * 360.0f;
            float rad = (angleDeg - 90.0f) * PI_F / 180.0f;
            float dx = (float) CX + (float) R1 * std::cos(rad);
            float dy = (float) CY + (float) R1 * std::sin(rad);
            lv_coord_t dxi = (lv_coord_t) std::lround(dx);
            lv_coord_t dyi = (lv_coord_t) std::lround(dy);
            lv_obj_set_pos(m_dotHalo, dxi - HALO_R, dyi - HALO_R);
            lv_obj_set_pos(m_dot, dxi - DOT_R, dyi - DOT_R);
        } else {
            lv_obj_add_flag(m_dot, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(m_dotHalo, LV_OBJ_FLAG_HIDDEN);
        }

        char heroBuf[24];
        if (valid) {
            heroDaysText(heroBuf, sizeof(heroBuf), s.halvingDays);
        } else {
            snprintf(heroBuf, sizeof(heroBuf), "--");
        }
        lv_label_set_text(m_hero, heroBuf);

        char subBuf[96];
        if (valid) {
            char leftBuf[nqfmt::kNumBufMax];
            nqfmt::n0(leftBuf, sizeof(leftBuf), (float) s.halvingLeft);
            snprintf(subBuf, sizeof(subBuf), "#f3e6b8 %s blocks# left", leftBuf);
        } else {
            //   netValid == false: never invent a number -- short honest
            //   sentence instead (CONTRATO.md / ui_state.h contract).
            snprintf(subBuf, sizeof(subBuf), "waiting for network data");
        }
        lv_label_set_text(m_sub, subBuf);

        char dateBuf[48];
        if (valid && s.clockValid) {
            int64_t targetS = s.epochS + (int64_t) std::llround((double) s.halvingDays * 86400.0);
            time_t t = (time_t) targetS;
            struct tm tmv;
            localtime_r(&t, &tmv);
            //   English short-month format ("estimated date: Sep 24, 2028")
            //   instead of the old dd/mm/yyyy -- tm_mon is 0-based, kMonthsAbbr
            //   is indexed directly by it (no -1 needed, unlike tm_mday/
            //   tm_year which are already the raw values snprintf needs).
            static const char *kMonthsAbbr[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
            snprintf(dateBuf, sizeof(dateBuf), "estimated date: %s %d, %04d", kMonthsAbbr[tmv.tm_mon], tmv.tm_mday,
                     tmv.tm_year + 1900);
        } else if (valid) {
            snprintf(dateBuf, sizeof(dateBuf), "estimated date: -- (no exact time yet)");
        } else {
            snprintf(dateBuf, sizeof(dateBuf), "estimate unavailable");
        }
        lv_label_set_text(m_date, dateBuf);
    }

    void destroy() override
    {
        if (m_root) {
            lv_obj_del(m_root);
            m_root = nullptr;
            m_outerArc = nullptr;
            m_innerArc = nullptr;
            m_dot = nullptr;
            m_dotHalo = nullptr;
            m_hero = nullptr;
            m_sub = nullptr;
            m_date = nullptr;
        }
    }

  private:
    //  value in [0, 1000] for a ring at fractional position `pos` (0.1), never
    //  fully collapsed to 0 so a sliver always reads as "a ring".
    static int16_t arcValue(float pos)
    {
        int16_t v = (int16_t) std::lround(pos * 1000.0f);
        return v < MIN_ARC_VALUE ? MIN_ARC_VALUE : v;
    }

    //  Ring = ONE lv_arc: LV_PART_MAIN is the always-full-circle track,
    //  LV_PART_INDICATOR is the value-driven progress. Rotated 270deg so angle 0
    //  sits at 12 o'clock (LVGL's own 0deg is 3 o'clock) -- same convention
    //  s13_vinte_quatro_horas.cpp's lvAngle documents, applied here via
    //  lv_arc_set_rotation instead of pre-rotating each angle, since this screen
    //  only ever needs a single full-range 0.360 sweep per ring (no per-slice
    //  angles to convert).
    lv_obj_t *makeRingArc(int r, int sw, uint32_t trackColor, uint32_t progColor)
    {
        lv_obj_t *arc = lv_arc_create(m_root);
        lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
        lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(arc, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(arc, r * 2, r * 2);
        lv_obj_set_pos(arc, CX - r, CY - r);
        lv_obj_set_style_pad_all(arc, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);

        lv_arc_set_bg_angles(arc, 0, 360);
        lv_arc_set_rotation(arc, 270);
        lv_arc_set_range(arc, 0, 1000);
        lv_arc_set_mode(arc, LV_ARC_MODE_NORMAL);

        lv_obj_set_style_arc_width(arc, sw, LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc, lv_color_hex(trackColor), LV_PART_MAIN);
        lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);

        lv_obj_set_style_arc_width(arc, sw, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc, lv_color_hex(progColor), LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);

        lv_arc_set_value(arc, MIN_ARC_VALUE);
        return arc;
    }

    lv_obj_t *makeDot(int r, uint32_t fill, uint32_t border)
    {
        lv_obj_t *dot = lv_obj_create(m_root);
        lv_obj_set_size(dot, r * 2, r * 2);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(dot, lv_color_hex(fill), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(dot, lv_color_hex(border), LV_PART_MAIN);
        lv_obj_set_style_border_width(dot, (fill == border) ? 0 : 1, LV_PART_MAIN);
        lv_obj_set_style_pad_all(dot, 0, LV_PART_MAIN);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        return dot;
    }

    lv_obj_t *m_root = nullptr;
    lv_obj_t *m_outerArc = nullptr;
    lv_obj_t *m_innerArc = nullptr;
    lv_obj_t *m_dot = nullptr;
    lv_obj_t *m_dotHalo = nullptr;
    lv_obj_t *m_hero = nullptr;
    lv_obj_t *m_sub = nullptr;
    lv_obj_t *m_date = nullptr;
};

S10HalvingScreen s_instance;

} // namespace

Screen *screenGetS10Halving()
{
    return &s_instance;
}
