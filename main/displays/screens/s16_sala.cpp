// Ported from the approved design to LVGL 8.3 (DISPLAY_PROFILE_
// YYSLUPING_480X320). "Sala": the miner as the house's thermometer -- a
// weather-bulletin layout with the room's ESTIMATED temperature (from the
// voltage regulator + power draw, no dedicated ambient sensor), a trend in
// degC/min, a small weather glyph chosen from time-of-day + trend, a
// calculated insight sentence, and a 6-hour forecast strip built from
// UiState::hourFreq/hourAmb.
//
// Before this pass: a skeleton with a
// single placeholder label, no glyph, no forecast, no palette switching.
//
// After: the approved design's own 3 time-of-day palettes (flat-recolored, not
// redrawn), the 4 weather glyphs built from lv_obj/lv_line primitives (toggled
// by visibility, never recreated), the hero temperature in the same shared
// Fraunces digit font as s13_vinte_quatro_horas.cpp, the calculated insight
// sentence, and a 6-cell forecast strip with a reused bar pool + one
// trajectory lv_line.

#include "../fonts/fonts.h"
#include "../ui_state.h"
#include "nq_fmt.h"
#include "screen.h"
#include "screen_common.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

// Shared with s13_vinte_quatro_horas.cpp -- see that file's header comment
// for why ONE font file serves both screens' hero digits.
LV_FONT_DECLARE(font_s13_s16_fraunces_bold_64);

namespace
{

enum Period
{
    PERIOD_MANHA = 0,
    PERIOD_TARDE,
    PERIOD_NOITE
};

struct Palette
{
    uint32_t bg, ink, muted, glyph, warm, cool;
};

// the approved design's --bg/--ink/--muted/--glyph/--warm/--cool
constexpr Palette kPalettes[3] = {
    {0xf5ddb0, 0x3b2a1a, 0x6b4d2e, 0xb85c0a, 0x7e330b, 0x0f4a5a}, // manha (also the default)
    {0xf6b23c, 0x3a2205, 0x4a2e04, 0xd9480f, 0x6a1408, 0x0d3a46}, // tarde
    {0x141d33, 0xf4ecd8, 0xaab4cf, 0xf4ecd8, 0xe8814a, 0x6fc3d9}, // noite
};

Period periodForHour(int hour)
{
    if (hour < 0) {
        hour = 12; // no clock yet: same "assume midday" fallback as the approved design
    }
    if (hour >= 6 && hour < 12) {
        return PERIOD_MANHA;
    }
    if (hour >= 12 && hour < 18) {
        return PERIOD_TARDE;
    }
    return PERIOD_NOITE;
}

enum Glyph
{
    GLYPH_SUN = 0,
    GLYPH_CLOUDSUN,
    GLYPH_MOON,
    GLYPH_FLAKE,
    GLYPH_COUNT
};

Glyph pickGlyph(int hour, float ambient, float slope)
{
    float amb = std::isnan(ambient) ? 32.0f : ambient;
    float sl = std::isnan(slope) ? 0.0f : slope;
    if (sl <= -0.12f && amb <= 30.0f) {
        return GLYPH_FLAKE;
    }
    if (periodForHour(hour) == PERIOD_NOITE) {
        return GLYPH_MOON;
    }
    if (sl >= 0.08f) {
        return GLYPH_SUN;
    }
    return GLYPH_CLOUDSUN;
}

enum TrendKind
{
    TREND_FLAT = 0,
    TREND_WARM,
    TREND_COOL
};

struct TrendInfo
{
    TrendKind kind;
    char text[40];
};

TrendInfo trendInfo(float slope)
{
    TrendInfo t{};
    if (std::isnan(slope)) {
        t.kind = TREND_FLAT;
        std::snprintf(t.text, sizeof(t.text), "no trend reading right now");
        return t;
    }
    char buf[16];
    if (slope >= 0.04f) {
        t.kind = TREND_WARM;
        nqfmt::n1(buf, sizeof(buf), slope);
        std::snprintf(t.text, sizeof(t.text),
                      "warming %s \xc2\xb0"
                      "C/min",
                      buf);
    } else if (slope <= -0.04f) {
        t.kind = TREND_COOL;
        nqfmt::n1(buf, sizeof(buf), -slope);
        std::snprintf(t.text, sizeof(t.text),
                      "cooling %s \xc2\xb0"
                      "C/min",
                      buf);
    } else {
        t.kind = TREND_FLAT;
        std::snprintf(t.text, sizeof(t.text), "stable now");
    }
    return t;
}

constexpr int STRIP_X = 12, STRIP_TOP = 243, STRIP_W = 456, CELL_W = 76;
constexpr int TRACK_H = 16;

// One vertical instrument-less "cell" of the 6-hour forecast strip: a
// reused bar (track + fill) and 3 reused labels, positioned once in
// create and only re-styled/re-texted in update -- never recreated.
struct ForecastCell
{
    lv_obj_t *track = nullptr;
    lv_obj_t *fill = nullptr;
    lv_obj_t *hourLbl = nullptr;
    lv_obj_t *freqLbl = nullptr;
    lv_obj_t *tempLbl = nullptr;
    //   track/fill's own x, stored at create time instead of re-queried via
    //   lv_obj_get_x(track) every update -- see the 2026-09-22 fix note in
    //   updateForecast for why the live query was the wrong tool here.
    lv_coord_t cellX = 0;
};

class S16SalaScreen : public Screen {
  public:
    int number() const override
    {
        return 16;
    }
    const char *name() const override
    {
        return "sala";
    }

    void create(lv_obj_t *parent) override
    {
        if (m_root) {
            destroy();
        }
        m_root = screenMakeRoot(parent, kPalettes[PERIOD_MANHA].bg);

        buildGlyphs();

        m_capLbl = screenMakeLabel(m_root, "Room", 178, 16, 200, &font_archivo_medium_15, kPalettes[0].muted);
        m_approxLbl = screenMakeLabel(m_root, "~", 178, 40, 20, &font_archivo_regular_19, kPalettes[0].muted);
        m_figureLbl = screenMakeLabel(m_root, "--", 198, 28, 150, &font_s13_s16_fraunces_bold_64, kPalettes[0].ink);
        //   Auto width (was a fixed 150px box) so layoutFigureRow can glue
        //   the "\xc2\xb0C" unit right after however wide the actual digits
        //   render, instead of a hardcoded offset that only worked for one
        //   specific digit count -- see the file header comment.
        lv_obj_set_width(m_figureLbl, LV_SIZE_CONTENT);
        m_unitLbl = screenMakeLabel(m_root,
                                    "\xc2\xb0"
                                    "C",
                                    178, 100, 60, &font_archivo_regular_19, kPalettes[0].muted);
        layoutFigureRow(); // positions m_unitLbl right after m_figureLbl -- see update

        m_trendDot = lv_obj_create(m_root);
        lv_obj_set_size(m_trendDot, 11, 11);
        lv_obj_set_style_radius(m_trendDot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_border_width(m_trendDot, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(m_trendDot, 0, LV_PART_MAIN);
        lv_obj_clear_flag(m_trendDot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(m_trendDot, 178, 128);

        m_trendLbl = screenMakeLabel(m_root, "stable now", 196, 124, 270, &font_archivo_medium_17, kPalettes[0].ink);

        m_noteLbl = screenMakeLabel(m_root, "Estimated from the regulator and power draw - the room has no sensor of its own.", 178,
                                    148, 270, &font_archivo_regular_15, kPalettes[0].muted);
        lv_label_set_long_mode(m_noteLbl, LV_LABEL_LONG_WRAP);

        //   The insight bar+sentence below used to sit at a hardcoded y=170/176
        //   that assumed m_noteLbl above only wrapped to a shorter height than
        //   it actually renders at -- the two visibly overlapped. Measuring
        //   m_noteLbl's REAL wrapped height (it's static text, never changes
        //   after this) and placing insight right after it, with a fixed gap,
        //   removes the guesswork and the overlap for good.
        lv_obj_update_layout(m_noteLbl);
        lv_coord_t insightY = lv_obj_get_y(m_noteLbl) + lv_obj_get_height(m_noteLbl) + 8;

        m_insightBar = lv_obj_create(m_root);
        lv_obj_set_pos(m_insightBar, 12, insightY);
        lv_obj_set_size(m_insightBar, 4, 20);
        lv_obj_set_style_radius(m_insightBar, 2, LV_PART_MAIN);
        lv_obj_set_style_border_width(m_insightBar, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(m_insightBar, 0, LV_PART_MAIN);
        lv_obj_clear_flag(m_insightBar, LV_OBJ_FLAG_SCROLLABLE);

        m_insightLbl = screenMakeLabel(m_root, "Still gathering history to forecast the next hours.", 28, insightY, 428,
                                       &font_archivo_regular_15, kPalettes[0].ink);
        lv_label_set_long_mode(m_insightLbl, LV_LABEL_LONG_WRAP);
        //   Bar height/y track the label's own (variable, 1 or 2 lines) real
        //   height every update -- see updateForecast -- so the bar always
        //   spans the same vertical range as the sentence next to it (top
        //   edges match here already; updateForecast keeps it that way).

        m_stripHead = screenMakeLabel(m_root,
                                      "Forecast for the next hours (MHz and \xc2\xb0"
                                      "C)",
                                      12, 218, 320, &font_archivo_medium_15, kPalettes[0].muted);
        //   Static caption, own height measured once: updateForecast places
        //   it (and the whole strip below it) right after the insight
        //   sentence's REAL height every tick, so a long (2-line) insight
        //   never runs into this caption -- see the.md write-up.
        lv_obj_update_layout(m_stripHead);
        m_stripHeadH = lv_obj_get_height(m_stripHead);

        //   trajectory polyline (reused, points recomputed each update)
        m_traj = lv_line_create(m_root);
        lv_obj_set_pos(m_traj, STRIP_X, STRIP_TOP);
        lv_obj_set_size(m_traj, STRIP_W, TRACK_H);
        lv_obj_set_style_line_width(m_traj, 2, LV_PART_MAIN);
        lv_obj_set_style_line_opa(m_traj, 140, LV_PART_MAIN); // ~0.55, matches the approved design's stroke-opacity
        lv_obj_set_style_line_color(m_traj, lv_color_hex(kPalettes[0].ink), LV_PART_MAIN);
        for (int i = 0; i < 6; i++) {
            m_trajPts[i] = {(lv_coord_t) (i * CELL_W + CELL_W / 2), (lv_coord_t) (TRACK_H / 2)};
        }
        lv_line_set_points(m_traj, m_trajPts, 6);

        m_dipDot = lv_obj_create(m_root);
        styleSmallDot(m_dipDot, 4, kPalettes[0].warm);
        m_recDot = lv_obj_create(m_root);
        styleSmallDot(m_recDot, 4, kPalettes[0].cool);

        for (int i = 0; i < 6; i++) {
            ForecastCell &c = m_cells[i];
            int cx = STRIP_X + i * CELL_W;
            c.cellX = cx + CELL_W / 2 - 7;

            c.track = lv_obj_create(m_root);
            lv_obj_set_pos(c.track, c.cellX, STRIP_TOP);
            lv_obj_set_size(c.track, 14, TRACK_H);
            lv_obj_set_style_bg_opa(c.track, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(c.track, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(c.track, 0, LV_PART_MAIN);
            lv_obj_clear_flag(c.track, LV_OBJ_FLAG_SCROLLABLE);

            c.fill = lv_obj_create(m_root);
            lv_obj_set_size(c.fill, 14, 2);
            lv_obj_set_style_radius(c.fill, 2, LV_PART_MAIN);
            lv_obj_set_style_border_width(c.fill, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(c.fill, 0, LV_PART_MAIN);
            lv_obj_clear_flag(c.fill, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_pos(c.fill, c.cellX, STRIP_TOP + TRACK_H - 2);

            c.hourLbl = screenMakeLabel(m_root, "--", cx, STRIP_TOP + TRACK_H + 1, CELL_W, &font_archivo_regular_15,
                                        kPalettes[0].muted, LV_TEXT_ALIGN_CENTER);
            c.freqLbl = screenMakeLabel(m_root, "--", cx, STRIP_TOP + TRACK_H + 15, CELL_W, &font_archivo_bold_20, kPalettes[0].ink,
                                        LV_TEXT_ALIGN_CENTER);
            c.tempLbl = screenMakeLabel(m_root, "--", cx, STRIP_TOP + TRACK_H + 33, CELL_W, &font_archivo_regular_15,
                                        kPalettes[0].muted, LV_TEXT_ALIGN_CENTER);
        }

        m_lastPeriod = -1;
        applyPalette(PERIOD_MANHA);
        m_curGlyph = GLYPH_COUNT; // force first setGlyph to actually apply
        setGlyph(GLYPH_CLOUDSUN);
    }

    void update(const UiState &s) override
    {
        if (!m_root) {
            return;
        }

        int hour = -1;
        if (s.clockValid) {
            time_t t = (time_t) s.epochS;
            struct tm tmv{};
            localtime_r(&t, &tmv);
            hour = tmv.tm_hour;
        }

        Period per = periodForHour(hour);
        if ((int) per != m_lastPeriod) {
            applyPalette(per);
            m_lastPeriod = (int) per;
        }

        if (std::isnan(s.ambient)) {
            lv_label_set_text(m_figureLbl, "--");
        } else {
            char buf[16];
            nqfmt::n0(buf, sizeof(buf), s.ambient);
            lv_label_set_text(m_figureLbl, buf);
        }
        layoutFigureRow(); // re-glue "\xc2\xb0C" now that the figure's digit count may have changed

        TrendInfo tr = trendInfo(s.slopeCpm);
        lv_label_set_text(m_trendLbl, tr.text);
        const Palette &pal = kPalettes[m_lastPeriod];
        uint32_t dotColor = pal.muted;
        if (tr.kind == TREND_WARM) {
            dotColor = pal.warm;
        } else if (tr.kind == TREND_COOL) {
            dotColor = pal.cool;
        }
        lv_obj_set_style_bg_color(m_trendDot, lv_color_hex(dotColor), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(m_trendDot, LV_OPA_COVER, LV_PART_MAIN);

        Glyph g = pickGlyph(hour, s.ambient, s.slopeCpm);
        setGlyph(g);

        updateForecast(s, hour);
    }

    void destroy() override
    {
        if (m_root) {
            lv_obj_del(m_root);
            m_root = nullptr;
        }
        m_capLbl = m_approxLbl = m_figureLbl = m_unitLbl = m_trendDot = m_trendLbl = m_noteLbl = nullptr;
        m_insightBar = m_insightLbl = m_stripHead = m_traj = m_dipDot = m_recDot = nullptr;
        for (int i = 0; i < 6; i++) {
            m_cells[i] = ForecastCell{};
        }
        for (int i = 0; i < GLYPH_COUNT; i++) {
            m_glyphRoot[i] = nullptr;
        }
        m_lastPeriod = -1;
        m_curGlyph = GLYPH_COUNT;
        m_stripTop = STRIP_TOP;
        m_stripHeadH = 0;
    }

  private:
    // Glues m_unitLbl ("\xc2\xb0C") right after m_figureLbl's own rendered width,
    // whatever the current digit count is (2-digit temps, a leading '-',.) --
    // m_figureLbl was previously a fixed-width box and m_unitLbl sat at a
    // hardcoded (x, y) below-left of it, on its own visual "line" far from the
    // number instead of glued to it. Bottom- aligning the two boxes approximates
    // the approved design's own baseline-aligned flex row without needing real
    // font-baseline metrics.
    void layoutFigureRow()
    {
        lv_obj_update_layout(m_figureLbl);
        lv_coord_t figX = lv_obj_get_x(m_figureLbl);
        lv_coord_t figW = lv_obj_get_width(m_figureLbl);
        lv_coord_t figBottom = lv_obj_get_y(m_figureLbl) + lv_obj_get_height(m_figureLbl);
        lv_obj_update_layout(m_unitLbl);
        lv_coord_t unitH = lv_obj_get_height(m_unitLbl);
        lv_obj_set_pos(m_unitLbl, figX + figW + 6, figBottom - unitH - 6);
    }

    //   Repositions the whole 6-hour strip (trajectory line + every cell's
    //   track/labels) at a given top -- called once from create territory
    //   implicitly via the initial STRIP_TOP-based positions, and again every
    //   update once the insight sentence's real height is known, so a
    //   longer (2-line) insight pushes the strip down instead of overlapping
    //   it. Fill height/position and the dip/recover dots are repositioned
    //   separately in updateForecast itself (they depend on the hourly
    //   data, not just this top).
    void positionStrip(lv_coord_t top)
    {
        m_stripTop = top;
        lv_obj_set_pos(m_traj, STRIP_X, top);
        for (int i = 0; i < 6; i++) {
            ForecastCell &c = m_cells[i];
            int cx = STRIP_X + i * CELL_W;
            lv_obj_set_pos(c.track, c.cellX, top);
            lv_obj_set_pos(c.hourLbl, cx, top + TRACK_H + 1);
            lv_obj_set_pos(c.freqLbl, cx, top + TRACK_H + 15);
            lv_obj_set_pos(c.tempLbl, cx, top + TRACK_H + 33);
        }
    }

    void styleSmallDot(lv_obj_t *dot, int r, uint32_t color)
    {
        lv_obj_set_size(dot, r * 2, r * 2);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(dot, lv_color_hex(color), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(dot, 0, LV_PART_MAIN);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t *makeShape(lv_obj_t *parent, int cx, int cy, int r, bool circle)
    {
        lv_obj_t *o = lv_obj_create(parent);
        lv_obj_set_size(o, r * 2, r * 2);
        lv_obj_set_pos(o, cx - r, cy - r);
        lv_obj_set_style_radius(o, circle ? LV_RADIUS_CIRCLE : 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(o, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN);
        lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
        return o;
    }

    lv_obj_t *makeRay(lv_obj_t *parent, lv_point_t *pts, int x1, int y1, int x2, int y2)
    {
        pts[0] = {(lv_coord_t) x1, (lv_coord_t) y1};
        pts[1] = {(lv_coord_t) x2, (lv_coord_t) y2};
        lv_obj_t *ln = lv_line_create(parent);
        lv_line_set_points(ln, pts, 2);
        lv_obj_set_style_line_width(ln, 5, LV_PART_MAIN);
        lv_obj_set_style_line_rounded(ln, true, LV_PART_MAIN);
        return ln;
    }

    //   Builds the 4 weather glyphs (sun / sun-behind-cloud / moon /
    //   AC-snowflake) as ~150x150 primitive groups, once. Only visibility and
    //   (on palette change) color are ever touched afterwards.
    void buildGlyphs()
    {
        constexpr float kPi = 3.14159265358979323846f;
        for (int g = 0; g < GLYPH_COUNT; g++) {
            m_glyphRoot[g] = lv_obj_create(m_root);
            lv_obj_set_pos(m_glyphRoot[g], 12, 12);
            lv_obj_set_size(m_glyphRoot[g], 150, 150);
            lv_obj_set_style_bg_opa(m_glyphRoot[g], LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(m_glyphRoot[g], 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(m_glyphRoot[g], 0, LV_PART_MAIN);
            lv_obj_clear_flag(m_glyphRoot[g], LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(m_glyphRoot[g], LV_OBJ_FLAG_HIDDEN);
        }

        //   sun: body + 8 rays
        m_sunBody = makeShape(m_glyphRoot[GLYPH_SUN], 75, 75, 38, true);
        lv_obj_set_style_border_width(m_sunBody, 3, LV_PART_MAIN);
        for (int k = 0; k < 8; k++) {
            float a = (float) k * kPi / 4.0f;
            float ci = std::cos(a), si = std::sin(a);
            m_sunRays[k] = makeRay(m_glyphRoot[GLYPH_SUN], m_sunRayPts[k], (int) (75 + 47 * ci), (int) (75 + 47 * si),
                                   (int) (75 + 65 * ci), (int) (75 + 65 * si));
        }

        //   sun behind cloud: small sun + rounded rect + 3 circles
        m_cloudSun = makeShape(m_glyphRoot[GLYPH_CLOUDSUN], 54, 52, 25, true);
        m_cloudParts[0] = lv_obj_create(m_glyphRoot[GLYPH_CLOUDSUN]);
        lv_obj_set_pos(m_cloudParts[0], 26, 80);
        lv_obj_set_size(m_cloudParts[0], 102, 34);
        lv_obj_set_style_radius(m_cloudParts[0], 17, LV_PART_MAIN);
        lv_obj_set_style_border_width(m_cloudParts[0], 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(m_cloudParts[0], 0, LV_PART_MAIN);
        lv_obj_clear_flag(m_cloudParts[0], LV_OBJ_FLAG_SCROLLABLE);
        m_cloudParts[1] = makeShape(m_glyphRoot[GLYPH_CLOUDSUN], 54, 80, 21, true);
        m_cloudParts[2] = makeShape(m_glyphRoot[GLYPH_CLOUDSUN], 86, 74, 26, true);
        m_cloudParts[3] = makeShape(m_glyphRoot[GLYPH_CLOUDSUN], 114, 84, 17, true);

        //   moon: body + "bite" (filled with bg color to cut a crescent)
        m_moonBody = makeShape(m_glyphRoot[GLYPH_MOON], 75, 75, 38, true);
        lv_obj_set_style_border_width(m_moonBody, 3, LV_PART_MAIN);
        m_moonBite = makeShape(m_glyphRoot[GLYPH_MOON], 92, 62, 32, true);

        //   AC snowflake: 3 crossing arms + 6 perpendicular ticks
        for (int k = 0; k < 3; k++) {
            float a = (float) k * kPi / 3.0f;
            float ci = std::cos(a), si = std::sin(a);
            constexpr float len = 42.0f, tick = 11.0f;
            m_flakeArms[k] = makeRay(m_glyphRoot[GLYPH_FLAKE], m_flakeArmPts[k], (int) (75 - len * ci), (int) (75 - len * si),
                                     (int) (75 + len * ci), (int) (75 + len * si));
            lv_obj_set_style_line_width(m_flakeArms[k], 6, LV_PART_MAIN);
            float pi_ = -si, pj = ci;
            for (int sgn = 0; sgn < 2; sgn++) {
                float sign = sgn == 0 ? -1.0f : 1.0f;
                float ax = 75 + sign * len * 0.68f * ci, ay = 75 + sign * len * 0.68f * si;
                int idx = k * 2 + sgn;
                m_flakeTicks[idx] = makeRay(m_glyphRoot[GLYPH_FLAKE], m_flakeTickPts[idx], (int) (ax - pi_ * tick / 2),
                                            (int) (ay - pj * tick / 2), (int) (ax + pi_ * tick / 2), (int) (ay + pj * tick / 2));
                lv_obj_set_style_line_width(m_flakeTicks[idx], 4, LV_PART_MAIN);
                lv_obj_set_style_line_rounded(m_flakeTicks[idx], true, LV_PART_MAIN);
            }
        }
    }

    void setGlyph(Glyph g)
    {
        if (g == m_curGlyph) {
            return;
        }
        m_curGlyph = g;
        for (int i = 0; i < GLYPH_COUNT; i++) {
            if (i == g) {
                lv_obj_clear_flag(m_glyphRoot[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(m_glyphRoot[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    //   Recolors every palette-dependent object (background + text + the 4
    //   glyphs' own fills/strokes). Called only when the time-of-day period
    //   actually changes, not every tick.
    void applyPalette(Period per)
    {
        const Palette &p = kPalettes[per];
        lv_obj_set_style_bg_color(m_root, lv_color_hex(p.bg), LV_PART_MAIN);

        lv_obj_set_style_text_color(m_capLbl, lv_color_hex(p.muted), LV_PART_MAIN);
        lv_obj_set_style_text_color(m_approxLbl, lv_color_hex(p.muted), LV_PART_MAIN);
        lv_obj_set_style_text_color(m_figureLbl, lv_color_hex(p.ink), LV_PART_MAIN);
        lv_obj_set_style_text_color(m_unitLbl, lv_color_hex(p.muted), LV_PART_MAIN);
        lv_obj_set_style_text_color(m_trendLbl, lv_color_hex(p.ink), LV_PART_MAIN);
        lv_obj_set_style_text_color(m_noteLbl, lv_color_hex(p.muted), LV_PART_MAIN);
        lv_obj_set_style_text_color(m_insightLbl, lv_color_hex(p.ink), LV_PART_MAIN);
        lv_obj_set_style_text_color(m_stripHead, lv_color_hex(p.muted), LV_PART_MAIN);
        lv_obj_set_style_line_color(m_traj, lv_color_hex(p.ink), LV_PART_MAIN);
        lv_obj_set_style_bg_color(m_dipDot, lv_color_hex(p.warm), LV_PART_MAIN);
        lv_obj_set_style_bg_color(m_recDot, lv_color_hex(p.cool), LV_PART_MAIN);

        for (int i = 0; i < 6; i++) {
            lv_obj_set_style_bg_color(m_cells[i].track, lv_color_hex(p.muted), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(m_cells[i].track, LV_OPA_20, LV_PART_MAIN);
            lv_obj_set_style_text_color(m_cells[i].hourLbl, lv_color_hex(p.muted), LV_PART_MAIN);
            lv_obj_set_style_text_color(m_cells[i].freqLbl, lv_color_hex(p.ink), LV_PART_MAIN);
            lv_obj_set_style_text_color(m_cells[i].tempLbl, lv_color_hex(p.muted), LV_PART_MAIN);
        }

        lv_obj_set_style_bg_color(m_sunBody, lv_color_hex(p.glyph), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(m_sunBody, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(m_sunBody, lv_color_hex(p.ink), LV_PART_MAIN);
        for (int k = 0; k < 8; k++) {
            lv_obj_set_style_line_color(m_sunRays[k], lv_color_hex(p.glyph), LV_PART_MAIN);
        }
        lv_obj_set_style_bg_color(m_cloudSun, lv_color_hex(p.glyph), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(m_cloudSun, LV_OPA_COVER, LV_PART_MAIN);
        for (int k = 0; k < 4; k++) {
            lv_obj_set_style_bg_color(m_cloudParts[k], lv_color_hex(p.muted), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(m_cloudParts[k], LV_OPA_COVER, LV_PART_MAIN);
        }
        lv_obj_set_style_bg_color(m_moonBody, lv_color_hex(p.glyph), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(m_moonBody, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(m_moonBody, lv_color_hex(p.ink), LV_PART_MAIN);
        lv_obj_set_style_bg_color(m_moonBite, lv_color_hex(p.bg), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(m_moonBite, LV_OPA_COVER, LV_PART_MAIN);
        for (int k = 0; k < 3; k++) {
            lv_obj_set_style_line_color(m_flakeArms[k], lv_color_hex(p.cool), LV_PART_MAIN);
        }
        for (int k = 0; k < 6; k++) {
            lv_obj_set_style_line_color(m_flakeTicks[k], lv_color_hex(p.cool), LV_PART_MAIN);
        }
    }

    //   Forecast: next 6 local hours from UiState::hourFreq/hourAmb (screen's
    //   own copy of the approved design reference's buildForecast/insightText).
    void updateForecast(const UiState &s, int hour)
    {
        struct Cell
        {
            int hour;
            float freq, amb;
            bool valid;
        };
        Cell cells[6];
        int h0 = (hour >= 0) ? hour : 0;
        for (int i = 0; i < 6; i++) {
            int hh = (h0 + i + 1) % 24;
            bool v = s.hourN[hh] > 0;
            cells[i] = {hh, v ? s.hourFreq[hh] : NAN, v ? s.hourAmb[hh] : NAN, v};
        }

        float ceilF = NAN, floorF = NAN;
        bool haveAny = false;
        for (int h = 0; h < 24; h++) {
            if (s.hourN[h] <= 0) {
                continue;
            }
            float f = s.hourFreq[h];
            if (!haveAny) {
                ceilF = floorF = f;
                haveAny = true;
            } else {
                if (f > ceilF) {
                    ceilF = f;
                }
                if (f < floorF) {
                    floorF = f;
                }
            }
        }

        int dipIdx = -1;
        for (int i = 0; i < 6; i++) {
            if (cells[i].valid && (dipIdx < 0 || cells[i].freq < cells[dipIdx].freq)) {
                dipIdx = i;
            }
        }
        int recoverHour = -1;
        float recoverFreq = NAN;
        if (haveAny && dipIdx >= 0) {
            for (int step = 1; step <= 23; step++) {
                int hh = (cells[dipIdx].hour + step) % 24;
                if (s.hourN[hh] > 0 && s.hourFreq[hh] >= ceilF - 6.0f) {
                    recoverHour = hh;
                    recoverFreq = s.hourFreq[hh];
                    break;
                }
            }
        }

        char insight[160];
        bool flatBar = true;
        if (!haveAny) {
            std::snprintf(insight, sizeof(insight), "Still gathering history to forecast the next hours.");
        } else if (ceilF - floorF < 8.0f) {
            std::snprintf(insight, sizeof(insight), "Frequency should stay similar over the next hours, no sharp drops.");
        } else if (dipIdx < 0) {
            std::snprintf(insight, sizeof(insight), "Frequency should vary little over the next hours.");
        } else {
            char dipFreqBuf[16], ceilBuf[16];
            nqfmt::n0(dipFreqBuf, sizeof(dipFreqBuf), cells[dipIdx].freq);
            if (recoverHour >= 0 && recoverHour != cells[dipIdx].hour) {
                nqfmt::n0(ceilBuf, sizeof(ceilBuf), ceilF);
                std::snprintf(insight, sizeof(insight), "at %dh should hold ~%s MHz; after %dh back to ~%s MHz", cells[dipIdx].hour,
                              dipFreqBuf, recoverHour, ceilBuf);
                flatBar = false;
            } else {
                std::snprintf(insight, sizeof(insight),
                              "at %dh should hold around ~%s MHz, no headroom expected over the next hours", cells[dipIdx].hour,
                              dipFreqBuf);
            }
        }
        lv_label_set_text(m_insightLbl, insight);
        const Palette &pal = kPalettes[m_lastPeriod];
        lv_obj_set_style_bg_color(m_insightBar, lv_color_hex(flatBar ? pal.muted : pal.warm), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(m_insightBar, LV_OPA_COVER, LV_PART_MAIN);
        //   Keep the bar spanning the same vertical range as the sentence
        //   next to it (1 line or 2, depending on which insight was picked
        //   above) so the two always stay top-aligned -- previously the bar
        //   had a fixed height computed for the placeholder text only.
        lv_obj_update_layout(m_insightLbl);
        lv_coord_t insightH = lv_obj_get_height(m_insightLbl);
        lv_obj_set_size(m_insightBar, 4, insightH > 16 ? insightH : 16);
        lv_obj_set_pos(m_insightBar, 12, lv_obj_get_y(m_insightLbl));

        // The insight sentence picked above can be 1 or 2 lines depending on which of
        // the (differently long) insight templates matched -- pushing the caption +
        // strip below down by the sentence's REAL height every tick, instead of
        // assuming it always fits in the space the placeholder text needed, is what
        // keeps a long insight from ever running into "Previsao das proximas horas."
        // below it.
        lv_coord_t stripHeadY = lv_obj_get_y(m_insightLbl) + insightH + 8;
        lv_obj_set_pos(m_stripHead, 12, stripHeadY);
        positionStrip(stripHeadY + m_stripHeadH + 6);

        float fmin = s.fmin, fcap = s.freqCap;
        for (int i = 0; i < 6; i++) {
            ForecastCell &c = m_cells[i];
            if (cells[i].valid) {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%dh", cells[i].hour);
                lv_label_set_text(c.hourLbl, buf);
                nqfmt::n0(buf, sizeof(buf), cells[i].freq);
                lv_label_set_text(c.freqLbl, buf);
                nqfmt::n0(buf, sizeof(buf), cells[i].amb);
                char tbuf[20];
                std::snprintf(tbuf, sizeof(tbuf), "%s\xc2\xb0", buf);
                lv_label_set_text(c.tempLbl, tbuf);

                float pct = nqfmt::mapf(cells[i].freq, fmin, fcap, 8.0f, 100.0f);
                lv_coord_t fillH = (lv_coord_t) std::lround(pct / 100.0f * TRACK_H);
                if (fillH < 1) {
                    fillH = 1;
                }
                lv_obj_set_size(c.fill, 14, fillH);
                lv_obj_set_pos(c.fill, c.cellX, m_stripTop + TRACK_H - fillH);
                uint32_t fillColor = pal.muted;
                if (dipIdx == i) {
                    fillColor = pal.warm;
                } else if (recoverHour == cells[i].hour) {
                    fillColor = pal.cool;
                }
                lv_obj_set_style_bg_color(c.fill, lv_color_hex(fillColor), LV_PART_MAIN);
                lv_obj_set_style_bg_opa(c.fill, LV_OPA_COVER, LV_PART_MAIN);

                m_trajPts[i].x = (lv_coord_t) (i * CELL_W + CELL_W / 2);
                m_trajPts[i].y = (lv_coord_t) nqfmt::mapf(cells[i].freq, fmin, fcap, (float) TRACK_H - 2.0f, 2.0f);
            } else {
                lv_label_set_text(c.hourLbl, "--");
                lv_label_set_text(c.freqLbl, "--");
                lv_label_set_text(c.tempLbl, "--");
                lv_obj_set_size(c.fill, 14, 1);
                lv_obj_set_pos(c.fill, c.cellX, m_stripTop + TRACK_H - 1);
                lv_obj_set_style_bg_color(c.fill, lv_color_hex(pal.muted), LV_PART_MAIN);
                lv_obj_set_style_bg_opa(c.fill, LV_OPA_COVER, LV_PART_MAIN);
                m_trajPts[i].x = (lv_coord_t) (i * CELL_W + CELL_W / 2);
                m_trajPts[i].y = (lv_coord_t) (TRACK_H - 2);
            }
        }
        lv_line_set_points(m_traj, m_trajPts, 6);

        if (dipIdx >= 0) {
            lv_obj_set_pos(m_dipDot, (lv_coord_t) (STRIP_X + m_trajPts[dipIdx].x - 4),
                           (lv_coord_t) (m_stripTop + m_trajPts[dipIdx].y - 4));
            lv_obj_clear_flag(m_dipDot, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(m_dipDot, LV_OBJ_FLAG_HIDDEN);
        }
        int recIdx = -1;
        for (int i = 0; i < 6; i++) {
            if (cells[i].valid && cells[i].hour == recoverHour) {
                recIdx = i;
                break;
            }
        }
        (void) recoverFreq;
        if (recIdx >= 0) {
            lv_obj_set_pos(m_recDot, (lv_coord_t) (STRIP_X + m_trajPts[recIdx].x - 4),
                           (lv_coord_t) (m_stripTop + m_trajPts[recIdx].y - 4));
            lv_obj_clear_flag(m_recDot, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(m_recDot, LV_OBJ_FLAG_HIDDEN);
        }
    }

    lv_obj_t *m_root = nullptr;
    lv_obj_t *m_capLbl = nullptr;
    lv_obj_t *m_approxLbl = nullptr;
    lv_obj_t *m_figureLbl = nullptr;
    lv_obj_t *m_unitLbl = nullptr;
    lv_obj_t *m_trendDot = nullptr;
    lv_obj_t *m_trendLbl = nullptr;
    lv_obj_t *m_noteLbl = nullptr;
    lv_obj_t *m_insightBar = nullptr;
    lv_obj_t *m_insightLbl = nullptr;
    lv_obj_t *m_stripHead = nullptr;
    lv_coord_t m_stripHeadH = 0;
    lv_coord_t m_stripTop = STRIP_TOP; // see positionStrip
    lv_obj_t *m_traj = nullptr;
    lv_point_t m_trajPts[6] = {};
    lv_obj_t *m_dipDot = nullptr;
    lv_obj_t *m_recDot = nullptr;
    ForecastCell m_cells[6];

    lv_obj_t *m_glyphRoot[GLYPH_COUNT] = {};
    Glyph m_curGlyph = GLYPH_COUNT;
    lv_obj_t *m_sunBody = nullptr;
    lv_obj_t *m_sunRays[8] = {};
    lv_point_t m_sunRayPts[8][2] = {};
    lv_obj_t *m_cloudSun = nullptr;
    lv_obj_t *m_cloudParts[4] = {};
    lv_obj_t *m_moonBody = nullptr;
    lv_obj_t *m_moonBite = nullptr;
    lv_obj_t *m_flakeArms[3] = {};
    lv_point_t m_flakeArmPts[3][2] = {};
    lv_obj_t *m_flakeTicks[6] = {};
    lv_point_t m_flakeTickPts[6][2] = {};

    int m_lastPeriod = -1;
};

S16SalaScreen s_instance;

} // namespace

Screen *screenGetS16Sala()
{
    return &s_instance;
}
