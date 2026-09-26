// Ported from the approved design to LVGL 8.3
// (DISPLAY_PROFILE_YYSLUPING_480X320). "24 horas": a sundial-style ring of
// 24 slices, one per hour of the day -- each slice's outer radius is that
// hour's average hashrate (UiState::hourGhs) and its flat color is that
// hour's estimated room temperature (UiState::hourAmb), in 5 fixed steps.
// Noon sits at the top, midnight at the bottom, like a real sundial, so the
// owner reads the day's heat curve by looking at the ring from top to
// bottom without needing a legend to know "the afternoon is hot".
//
// Before this pass: a skeleton
// with a single placeholder label, no ring, no data at all.
//
// After: the approved design's own indigo-night palette, 24 lv_arc slices (one
// object per hour, resized/recolored ONLY when UiState::hourGhs/hourAmb/hourN
// actually change -- not every update tick), a highlighted slice marking the
// current hour (replacing the approved design's separate rotating triangle
// pointer -- see the comment above positionHighlight), 2 reused best/worst
// dots, and a legend column that -- unlike the approved design, where it
// shares the same x/width box as the best/worst side panel -- gets its own
// non-overlapping vertical slot.
//
// 2026-09-22 visual-QA pass: the center disc (hour/hero/insight) was too
// small and the insight sentence lived inside it, both fixed -- see the
// comments at the ring-geometry constants and around m_heroLbl/m_insightLbl
// in create.

#include "../fonts/fonts.h"
#include "../ui_state.h"
#include "nq_fmt.h"
#include "screen.h"
#include "screen_common.h"

#include <cmath>
#include <cstdio>
#include <ctime>

// Digits-only Fraunces Bold hero font (64px), generated for this pass and
// shared with s16_sala.cpp's own hero figure -- Why ONE font file serves both
// screens (font budget) instead of two near-identical cuts.
LV_FONT_DECLARE(font_s13_s16_fraunces_bold_64);

namespace
{

// ---- 13-vinte-quatro-horas' own palette (the approved design at 13-*.js) --
constexpr uint32_t S13_BG = 0x12162c;
constexpr uint32_t S13_INK = 0xf4ead7;
constexpr uint32_t S13_HOUR = 0xcfc3e4;
constexpr uint32_t S13_MUTED = 0x8a83a6;
constexpr uint32_t S13_INSIGHT = 0xb7ac82;
constexpr uint32_t S13_NEUTRAL = 0x3a3960; // slice color when that hour has no data yet
constexpr uint32_t S13_COLORS[5] = {0x1c2350, 0x54629c, 0xe7c17a, 0xd8823f, 0xa8432c};
constexpr float S13_THRESH[4] = {22.0f, 27.0f, 32.0f, 36.0f}; // degC cut points between the 5 colors

// ---- ring geometry (the approved design: CX 160 CY 162, R_IN 98, R_MIN 106,
// R_MAX 130) -- Shrunk/recentered slightly so the right column (best/worst +
// legend) gets its own non-overlapping space -- see the file header comment.
// R_IN was initially shrunk further than the approved design's own 98 (down to
// 86) to buy even more right-column room, but that squeezed the center disc
// too hard: the hero hashrate/hour/insight content no longer fit inside it
// without touching the slices (2024-visual-QA pass). Restored close to the
// approved design's own R_IN so the center has room again -- The before/after.
constexpr int CX = 158, CY = 150;
constexpr float R_IN = 98.0f, R_MIN = 104.0f, R_MAX = 122.0f, R_TICK = 134.0f;
constexpr float HALF = 6.0f; // half-width in degrees of each 15-degree slice (leaves a gap between slices)
constexpr float PI_F = 3.14159265358979323846f;

uint32_t ambColor(float amb)
{
    if (std::isnan(amb)) {
        return S13_NEUTRAL;
    }
    for (int i = 0; i < 4; i++) {
        if (amb <= S13_THRESH[i]) {
            return S13_COLORS[i];
        }
    }
    return S13_COLORS[4];
}

// The approved design's own angle convention: 0 deg = 12 o'clock (noon at the TOP),
// clockwise, hour h at 180 + h*15 (so h=0/midnight lands at 6 o'clock,
// bottom). hourAngleDeg/polar mirror the approved design's own hourAngle/polar helpers.
float hourAngleDeg(float h)
{
    return 180.0f + h * 15.0f;
}

// LVGL's lv_arc angle convention is 0 deg = 3 o'clock, clockwise -- 90 deg
// off from the approved design's. Converts once, normalized into [0, 360).
float lvAngle(float mockupDeg)
{
    float a = std::fmod(mockupDeg - 90.0f, 360.0f);
    if (a < 0.0f) {
        a += 360.0f;
    }
    return a;
}

void polar(float r, float aDeg, float &x, float &y)
{
    float rad = (aDeg - 90.0f) * PI_F / 180.0f;
    x = (float) CX + r * std::cos(rad);
    y = (float) CY + r * std::sin(rad);
}

class S13VinteQuatroHorasScreen : public Screen {
  public:
    int number() const override
    {
        return 13;
    }
    const char *name() const override
    {
        return "vinte_quatro_horas";
    }

    void create(lv_obj_t *parent) override
    {
        if (m_root) {
            destroy();
        }
        m_root = screenMakeRoot(parent, S13_BG);

        //  Background reference circles (static, the approved design's own trilha/inner disc).
        lv_obj_t *outerDisc = lv_obj_create(m_root);
        lv_obj_set_size(outerDisc, (lv_coord_t) ((R_MAX + 4) * 2), (lv_coord_t) ((R_MAX + 4) * 2));
        lv_obj_set_pos(outerDisc, (lv_coord_t) (CX - R_MAX - 4), (lv_coord_t) (CY - R_MAX - 4));
        lv_obj_set_style_radius(outerDisc, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(outerDisc, lv_color_hex(0x191d3a), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(outerDisc, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(outerDisc, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(outerDisc, 0, LV_PART_MAIN);
        lv_obj_clear_flag(outerDisc, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *innerDisc = lv_obj_create(m_root);
        lv_obj_set_size(innerDisc, (lv_coord_t) (R_IN * 2), (lv_coord_t) (R_IN * 2));
        lv_obj_set_pos(innerDisc, (lv_coord_t) (CX - R_IN), (lv_coord_t) (CY - R_IN));
        lv_obj_set_style_radius(innerDisc, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(innerDisc, lv_color_hex(S13_BG), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(innerDisc, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(innerDisc, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(innerDisc, 0, LV_PART_MAIN);
        lv_obj_clear_flag(innerDisc, LV_OBJ_FLAG_SCROLLABLE);

        //  24 hour slices, built once, geometry/color only touched by
        //  rebuildRing when the hourly data actually changes.
        for (int h = 0; h < 24; h++) {
            m_slice[h] = makeArc();
        }
        m_highlight = makeArc(); // current-hour marker (see positionHighlight)

        //  Cardinal hour ticks (0h/6h/12h/18h), static.
        static const int kCardinal[4] = {0, 6, 12, 18};
        static const char *kCardinalText[4] = {"0h", "6h", "12h", "18h"};
        for (int i = 0; i < 4; i++) {
            float tx, ty;
            polar(R_TICK, hourAngleDeg((float) kCardinal[i]), tx, ty);
            screenMakeLabel(m_root, kCardinalText[i], (int) tx - 20, (int) ty - 8, 40, &font_archivo_regular_15, S13_MUTED,
                            LV_TEXT_ALIGN_CENTER);
        }

        //  Best/worst markers (reused, hidden until rebuildRing has data).
        m_bestDot = lv_obj_create(m_root);
        styleDot(m_bestDot, 6, S13_INK, S13_INK);
        m_worstDot = lv_obj_create(m_root);
        styleDot(m_worstDot, 5, S13_BG, S13_INK);

        //  Center: current hour + hero hashrate only (see file header comment
        //  on why the insight sentence moved to the right column instead of
        //  staying here). Everything below has to fit inside the R_IN disc
        //  without touching the slice ring -- checked against the actual
        //  rendered PNGs, not just computed by eye (see the.md write-up).
        m_hourLbl = screenMakeLabel(m_root, "--:--", CX - 75, 78, 150, &font_archivo_bold_20, S13_HOUR, LV_TEXT_ALIGN_CENTER);
        m_heroLbl = screenMakeLabel(m_root, "--", CX - 75, 106, 150, &font_s13_s16_fraunces_bold_64, S13_INK, LV_TEXT_ALIGN_CENTER);
        //  The hero keeps the full 64px cut of the shared Fraunces font --
        //  font budget rules out a second (smaller) cut of the same font and
        //  fonts.h itself is off-limits for this pass, and a render-time
        //  zoom transform (tried first) rendered blank in the sim, so this
        //  relies purely on the enlarged R_IN above: "6,31" at 64px measures
        //  ~117px wide, comfortably inside the >=177px-wide chord available
        //  at the hero's y-range now, with no shrink needed at all.
        m_unitLbl = screenMakeLabel(m_root, "TH/s", CX - 75, 204, 150, &font_archivo_regular_15, S13_MUTED, LV_TEXT_ALIGN_CENTER);

        //  Right column: best/worst hour rows.
        m_bestHour = screenMakeLabel(m_root, "--", 328, 16, 130, &font_archivo_bold_20, S13_INK);
        m_bestSub = screenMakeLabel(m_root, "no peak data yet", 328, 40, 140, &font_archivo_regular_15, S13_MUTED);
        lv_label_set_long_mode(m_bestSub, LV_LABEL_LONG_WRAP);
        m_worstHour = screenMakeLabel(m_root, "--", 328, 96, 130, &font_archivo_bold_20, S13_INK);
        m_worstSub = screenMakeLabel(m_root, "no low data yet", 328, 120, 140, &font_archivo_regular_15, S13_MUTED);
        lv_label_set_long_mode(m_worstSub, LV_LABEL_LONG_WRAP);

        //  Insight sentence ("as Xh rende N% mais que as Yh") used to live
        //  centered inside the ring, under the hero -- at 2 wrapped lines it
        //  reached the bottom slices (see the.md write-up). Moved here,
        //  between the best/worst rows and the legend, its own non-
        //  overlapping slot like the legend already gets (file header
        //  comment) -- this also declutters the ring itself.
        m_insightLbl = screenMakeLabel(m_root, insightGatheringText(), 328, 162, 140, &font_archivo_regular_15, S13_INSIGHT);
        lv_label_set_long_mode(m_insightLbl, LV_LABEL_LONG_WRAP);

        //  Legend: its OWN slot, never overlapping the best/worst rows or
        //  the insight sentence above -- see the file header comment on why
        //  this differs from the approved design's own (overlapping) column layout.
        lv_obj_t *legDiv = lv_obj_create(m_root);
        lv_obj_set_pos(legDiv, 328, 210);
        lv_obj_set_size(legDiv, 140, 1);
        lv_obj_set_style_bg_color(legDiv, lv_color_hex(0x2a2c52), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(legDiv, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(legDiv, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(legDiv, 0, LV_PART_MAIN);
        lv_obj_clear_flag(legDiv, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *legCap =
            screenMakeLabel(m_root, "color = room temp. by hour", 328, 222, 140, &font_archivo_regular_15, S13_MUTED);
        lv_label_set_long_mode(legCap, LV_LABEL_LONG_WRAP);

        for (int i = 0; i < 5; i++) {
            lv_obj_t *sw = lv_obj_create(m_root);
            lv_obj_set_pos(sw, 328 + i * 29, 268);
            lv_obj_set_size(sw, 26, 14);
            lv_obj_set_style_bg_color(sw, lv_color_hex(S13_COLORS[i]), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_width(sw, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(sw, 2, LV_PART_MAIN);
            lv_obj_set_style_pad_all(sw, 0, LV_PART_MAIN);
            lv_obj_clear_flag(sw, LV_OBJ_FLAG_SCROLLABLE);
        }
        screenMakeLabel(m_root, "coldest", 328, 288, 90, &font_archivo_regular_15, S13_MUTED);
        screenMakeLabel(m_root, "hottest", 378, 288, 90, &font_archivo_regular_15, S13_MUTED, LV_TEXT_ALIGN_RIGHT);

        m_haveCache = false;
        m_lastHighlightHour = -2;
        m_minGhs = 0.0f;
        m_maxGhs = 0.0f;
        m_haveSpan = false;
    }

    void update(const UiState &s) override
    {
        if (!m_root) {
            return;
        }

        //  Only rebuild the 24-slice ring (and best/worst/insight text) when
        //  the hourly data actually changed -- these averages move roughly
        //  once a minute on real hardware, not at UI refresh rate.
        bool changed = !m_haveCache;
        if (!changed) {
            for (int h = 0; h < 24; h++) {
                if (s.hourN[h] != m_cacheN[h] ||
                    (s.hourN[h] > 0 && (s.hourGhs[h] != m_cacheGhs[h] || s.hourAmb[h] != m_cacheAmb[h]))) {
                    changed = true;
                    break;
                }
            }
        }
        if (changed) {
            rebuildRing(s);
            for (int h = 0; h < 24; h++) {
                m_cacheN[h] = s.hourN[h];
                m_cacheGhs[h] = s.hourGhs[h];
                m_cacheAmb[h] = s.hourAmb[h];
            }
            m_haveCache = true;
        }

        int curHour = -1, curMin = 0;
        if (s.clockValid) {
            time_t t = (time_t) s.epochS;
            struct tm tmv{};
            localtime_r(&t, &tmv);
            curHour = tmv.tm_hour;
            curMin = tmv.tm_min;
        }

        if (curHour >= 0) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%02d:%02d", curHour % 24, curMin % 60);
            lv_label_set_text(m_hourLbl, buf);
        } else {
            lv_label_set_text(m_hourLbl, "--:--");
        }

        if (curHour != m_lastHighlightHour) {
            positionHighlight(curHour);
            m_lastHighlightHour = curHour;
        }

        //  Hero: the approved design reference uses hashrate.m1 == UiState::hr1m.
        if (std::isnan(s.hr1m)) {
            lv_label_set_text(m_heroLbl, "--");
        } else {
            char buf[24];
            nqfmt::ths(buf, sizeof(buf), s.hr1m, 2);
            lv_label_set_text(m_heroLbl, buf);
        }
    }

    void destroy() override
    {
        if (m_root) {
            lv_obj_del(m_root);
            m_root = nullptr;
            for (int h = 0; h < 24; h++) {
                m_slice[h] = nullptr;
            }
            m_highlight = nullptr;
            m_bestDot = m_worstDot = nullptr;
            m_hourLbl = m_heroLbl = m_unitLbl = m_insightLbl = nullptr;
            m_bestHour = m_bestSub = m_worstHour = m_worstSub = nullptr;
        }
        m_haveCache = false;
        m_lastHighlightHour = -2;
    }

  private:
    static const char *insightGatheringText()
    {
        return "Gathering today's history...";
    }

    lv_obj_t *makeArc()
    {
        lv_obj_t *arc = lv_arc_create(m_root);
        lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
        lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(arc, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
        lv_obj_set_style_pad_all(arc, 0, LV_PART_MAIN);
        return arc;
    }

    void styleDot(lv_obj_t *dot, int r, uint32_t fill, uint32_t border)
    {
        lv_obj_set_size(dot, r * 2, r * 2);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(dot, lv_color_hex(fill), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(dot, lv_color_hex(border), LV_PART_MAIN);
        lv_obj_set_style_border_width(dot, (fill == border) ? 0 : 2, LV_PART_MAIN);
        lv_obj_set_style_pad_all(dot, 0, LV_PART_MAIN);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
    }

    void layoutSlice(lv_obj_t *arc, float rOut, float a0Mockup, float a1Mockup, float widthPad, uint32_t color)
    {
        float rc = (rOut + R_IN) * 0.5f + widthPad * 0.5f;
        float thickness = (rOut - R_IN) + widthPad;
        if (thickness < 2.0f) {
            thickness = 2.0f;
        }
        lv_coord_t size = (lv_coord_t) std::lround(rc * 2.0f);
        lv_obj_set_size(arc, size, size);
        lv_obj_set_pos(arc, (lv_coord_t) (CX - size / 2), (lv_coord_t) (CY - size / 2));
        float a0 = lvAngle(a0Mockup);
        float a1 = a0 + (a1Mockup - a0Mockup);
        lv_arc_set_bg_angles(arc, (uint16_t) a0, (uint16_t) a1);
        lv_obj_set_style_arc_width(arc, (lv_coord_t) std::lround(thickness), LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc, lv_color_hex(color), LV_PART_MAIN);
        lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
    }

    //  Recomputes all 24 slice geometries/colors plus best/worst/insight
    //  text. Called only from update when the underlying hourly data
    //  changed (see the change-detection loop there) -- NOT every tick.
    void rebuildRing(const UiState &s)
    {
        float minGhs = 1e9f, maxGhs = -1e9f;
        int bestH = -1, worstH = -1;
        float bestGhs = 0.0f, worstGhs = 0.0f;
        for (int h = 0; h < 24; h++) {
            if (s.hourN[h] <= 0) {
                continue;
            }
            float g = s.hourGhs[h];
            if (g < minGhs) {
                minGhs = g;
            }
            if (g > maxGhs) {
                maxGhs = g;
            }
            if (bestH < 0 || g > bestGhs) {
                bestH = h;
                bestGhs = g;
            }
            if (worstH < 0 || g < worstGhs) {
                worstH = h;
                worstGhs = g;
            }
        }
        bool haveAny = bestH >= 0;
        bool haveSpan = haveAny && (maxGhs - minGhs > 1.0f);
        m_minGhs = haveAny ? minGhs : 0.0f;
        m_maxGhs = haveAny ? maxGhs : 0.0f;
        m_haveSpan = haveSpan;

        for (int h = 0; h < 24; h++) {
            float ghs = (s.hourN[h] > 0) ? s.hourGhs[h] : m_minGhs;
            float amb = (s.hourN[h] > 0) ? s.hourAmb[h] : NAN;
            float rOut = haveSpan ? nqfmt::mapf(ghs, minGhs, maxGhs, R_MIN, R_MAX) : (R_MIN + R_MAX) * 0.5f;
            float a0 = hourAngleDeg((float) h) - HALF;
            float a1 = hourAngleDeg((float) h) + HALF;
            layoutSlice(m_slice[h], rOut, a0, a1, 0.0f, ambColor(amb));
        }

        if (haveAny) {
            float bR = haveSpan ? nqfmt::mapf(bestGhs, minGhs, maxGhs, R_MIN, R_MAX) : (R_MIN + R_MAX) * 0.5f;
            float bx, by;
            polar(bR, hourAngleDeg((float) bestH), bx, by);
            lv_obj_set_pos(m_bestDot, (lv_coord_t) (bx - 6), (lv_coord_t) (by - 6));
            lv_obj_clear_flag(m_bestDot, LV_OBJ_FLAG_HIDDEN);

            float wR = haveSpan ? nqfmt::mapf(worstGhs, minGhs, maxGhs, R_MIN, R_MAX) : (R_MIN + R_MAX) * 0.5f;
            float wx, wy;
            polar(wR, hourAngleDeg((float) worstH), wx, wy);
            lv_obj_set_pos(m_worstDot, (lv_coord_t) (wx - 5), (lv_coord_t) (wy - 5));
            lv_obj_clear_flag(m_worstDot, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(m_bestDot, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(m_worstDot, LV_OBJ_FLAG_HIDDEN);
        }

        if (haveAny && bestH != worstH && worstGhs > 0.0f) {
            float pct = (bestGhs - worstGhs) / worstGhs * 100.0f;
            char pctBuf[16], insight[160];
            nqfmt::n0(pctBuf, sizeof(pctBuf), pct);
            std::snprintf(insight, sizeof(insight), "at %02dh output is %s%% higher than at %02dh.", bestH, pctBuf, worstH);
            lv_label_set_text(m_insightLbl, insight);

            char thsBuf[24], hourBuf[16], sub[72];
            nqfmt::ths(thsBuf, sizeof(thsBuf), bestGhs, 2);
            std::snprintf(hourBuf, sizeof(hourBuf), "%02dh", bestH);
            lv_label_set_text(m_bestHour, hourBuf);
            std::snprintf(sub, sizeof(sub), "%s TH/s, today's peak", thsBuf);
            lv_label_set_text(m_bestSub, sub);

            nqfmt::ths(thsBuf, sizeof(thsBuf), worstGhs, 2);
            std::snprintf(hourBuf, sizeof(hourBuf), "%02dh", worstH);
            lv_label_set_text(m_worstHour, hourBuf);
            std::snprintf(sub, sizeof(sub), "%s TH/s, today's low", thsBuf);
            lv_label_set_text(m_worstSub, sub);
        } else {
            lv_label_set_text(m_insightLbl, insightGatheringText());
            lv_label_set_text(m_bestHour, "--");
            lv_label_set_text(m_bestSub, "no peak data yet");
            lv_label_set_text(m_worstHour, "--");
            lv_label_set_text(m_worstSub, "no low data yet");
        }
    }

    //  Marks the current hour by widening/brightening its own slice's outer
    //  edge instead of the approved design's separate rotating triangle pointer: the
    //  the approved design itself only moves that pointer in discrete per-hour jumps
    //  once `NQ.reducedMotion` is set (no continuous animation), so a
    //  highlighted slice communicates the same "you are here" fact with one
    //  fewer moving part and no per-frame animation to drive.
    void positionHighlight(int hour)
    {
        if (hour < 0 || hour > 23) {
            lv_obj_add_flag(m_highlight, LV_OBJ_FLAG_HIDDEN);
            return;
        }
        lv_obj_clear_flag(m_highlight, LV_OBJ_FLAG_HIDDEN);
        float ghs = (m_cacheN[hour] > 0) ? m_cacheGhs[hour] : m_minGhs;
        float rOut = m_haveSpan ? nqfmt::mapf(ghs, m_minGhs, m_maxGhs, R_MIN, R_MAX) : (R_MIN + R_MAX) * 0.5f;
        float a0 = hourAngleDeg((float) hour) - HALF - 1.0f;
        float a1 = hourAngleDeg((float) hour) + HALF + 1.0f;
        layoutSlice(m_highlight, rOut, a0, a1, 5.0f, S13_INK);
    }

    lv_obj_t *m_root = nullptr;
    lv_obj_t *m_slice[24] = {};
    lv_obj_t *m_highlight = nullptr;
    lv_obj_t *m_bestDot = nullptr;
    lv_obj_t *m_worstDot = nullptr;
    lv_obj_t *m_hourLbl = nullptr;
    lv_obj_t *m_heroLbl = nullptr;
    lv_obj_t *m_unitLbl = nullptr;
    lv_obj_t *m_insightLbl = nullptr;
    lv_obj_t *m_bestHour = nullptr;
    lv_obj_t *m_bestSub = nullptr;
    lv_obj_t *m_worstHour = nullptr;
    lv_obj_t *m_worstSub = nullptr;

    bool m_haveCache = false;
    float m_cacheGhs[24] = {};
    float m_cacheAmb[24] = {};
    int m_cacheN[24] = {};
    int m_lastHighlightHour = -2;
    float m_minGhs = 0.0f;
    float m_maxGhs = 0.0f;
    bool m_haveSpan = false;
};

S13VinteQuatroHorasScreen s_instance;

} // namespace

Screen *screenGetS13VinteQuatroHoras()
{
    return &s_instance;
}
