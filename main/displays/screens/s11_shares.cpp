// Ported from the approved design to LVGL 8.3 (DISPLAY_PROFILE_
// YYSLUPING_480X320). "Shares": a seismograph strip-chart -- every accepted
// share draws a spike whose height is proportional to log10(its difficulty),
// against a horizontal baseline at the pool's own difficulty. A fixed panel
// on the right carries the instrument's numbers (shares/min, accepted,
// rejected, acceptance rate, biggest spike in the visible window). If no
// share has arrived in a while (or the pool disconnects), the pen goes flat
// and a warning explains why.
//
// Before this pass: a skeleton with a
// single centered "em construcao" label -- no chart, no panel, no data.
//
// After: the approved design's own "warm graph paper" palette (#e7e2d8
// background, brick-red pen #b23a2e), an lv_line-based spike trace rebuilt
// every update from UiState::recent[] (NOT lv_chart -- see the comment above
// m_penLine's creation in create for why), a static grid + dashed baseline +
// pen-tip dot, the 5-row instrument panel (IBM Plex Mono Bold digits reused
// verbatim from screen 06, zero new font bytes for this screen), up to 2
// "recorde" annotations, and the "sem share ha X" warning.

#include "../fonts/fonts.h"
#include "../ui_state.h"
#include "nq_fmt.h"
#include "screen.h"
#include "screen_common.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

// Reused verbatim from screen 06 (main/displays/screens/s06_eficiencia.cpp):
// IBM Plex Mono Bold, 24 px, digits + ",.-%deg" only (~11.5 KB on disk,
// already paid for and linked by screens 04/06). This screen's own budget
// for NEW fonts is therefore 0 bytes: the approved design asks for "Space Mono"
// 400/700 ~25 px for its panel numbers, and this already-generated NUM-glyph
// mono cut at 24 px is close enough (1 px) to be indistinguishable at
// viewing distance, exactly the kind of reuse CONTRATO.md/the task brief
// asks for before generating anything new. Declared the same way
// (LV_FONT_DECLARE) other screens already reuse it, not redeclared under a
// new name and not duplicated as a second.c file.
LV_FONT_DECLARE(font_s06_mono_24);

namespace
{

// ---- 11-shares' own palette (the approved design's CSS) -
constexpr uint32_t S11_BG = 0xe7e2d8;           // warm paper
constexpr uint32_t S11_INK = 0x2c2a22;          // main text / values
constexpr uint32_t S11_PANEL_BG = 0xded7c7;     // right panel fill
constexpr uint32_t S11_PANEL_BORDER = 0x55503f; // panel left edge + baseline
constexpr uint32_t S11_LABEL = 0x5a5540;        // row labels
constexpr uint32_t S11_BASELINE_LBL = 0x6a6450; // "referencia do pool" caption
constexpr uint32_t S11_WARN = 0x8a3226;         // "sem share ha..." warning
constexpr uint32_t S11_PEN = 0xb23a2e;          // spike trace / annotations
constexpr uint32_t S11_GRID = 0xcfc8b8;         // background grid lines

// ---- chart geometry (from the approved design's own SVG coordinates: viewBox
// 0 0 336 320, i.e. the chart occupies the left 336 px of the 480-wide
// screen, panel takes the remaining 144 px) -----------------------------
constexpr int X0 = 14, X1 = 322;
constexpr int YBASE = 268, YTOP = 46;
constexpr float WINDOW_S = 100.0f; // seconds of history shown at once
constexpr int GRID_Y0 = 44, GRID_Y1 = 292, GRID_STEP = 24;
constexpr int GRID_COUNT = (GRID_Y1 - GRID_Y0) / GRID_STEP + 1; // 11

// One share -> up to 3 pen points (down-tick, spike, down-tick) plus the 2
// fixed end anchors. UI_SHARES_RECENT (48, ui_state.h) bounds recentCount,
// so this is a hard compile-time ceiling, never a runtime allocation.
constexpr int MAX_PEN_POINTS = 2 + 3 * UI_SHARES_RECENT;

constexpr int PANEL_X = 336;
constexpr int ROW_X = 346, ROW_W = 124; // matches the approved design's left:346/right:10

struct RowY
{
    int label, value;
};
// Matches the approved design's row(top,.) calls (14 / 74 / 134 / 194 / 252)
// with the value sitting ~18 px under its label (13 px label + ~2 px margin in
// the approved design; our reused 15 px label font needs a hair more).
constexpr RowY kRowY[5] = {
    {14, 32}, {74, 92}, {134, 152}, {194, 212}, {252, 270},
};

// v (a share/pool difficulty) -> "184" / "18,4" / "1,23" plus a magnitude
// letter ("", "k", "M", "G", "T", "P", "E"), mirroring the approved design reference's
// NQ.fmt.diff but split in two so the digits can render in the mono NUM
// font while the (non-digit) unit letter renders in an existing Archivo
// label -- same split screen 06 already uses for this exact reason (the
// NUM-only mono cut has no letters at all). NaN -> num="--", unit="".
struct DiffParts
{
    char num[16];
    char unit[2];
};

DiffParts formatDiffParts(double v)
{
    DiffParts out{};
    if (std::isnan(v)) {
        std::snprintf(out.num, sizeof(out.num), "--");
        return out;
    }
    static const char kUnits[] = {0, 'k', 'M', 'G', 'T', 'P', 'E'};
    double vv = v;
    int i = 0;
    while (vv >= 1000.0 && i < 6) {
        vv /= 1000.0;
        i++;
    }
    int decimals = (vv >= 100.0) ? 0 : (vv >= 10.0) ? 1 : 2;
    nqfmt::num(out.num, sizeof(out.num), (float) vv, decimals);
    if (i > 0) {
        out.unit[0] = kUnits[i];
        out.unit[1] = '\0';
    }
    return out;
}

// seconds -> "12 min 05 s" / "3 h 04 min" / "45 s". Simplified from
// the approved design reference's F.dur (no "days" tier): the warning only ever fires past
// lastShareAgeS > 180 s, so seconds/minutes covers the common case per the
// task brief, and the hours tier keeps a multi-hour outage legible instead
// of a raw 3-digit minute count.
void formatDurShort(char *buf, size_t n, float s)
{
    if (s < 0.0f) {
        s = 0.0f;
    }
    int totalS = (int) (s + 0.5f);
    int h = totalS / 3600;
    int m = (totalS % 3600) / 60;
    int ss = totalS % 60;
    if (h > 0) {
        std::snprintf(buf, n, "%d h %02d min", h, m);
    } else if (m > 0) {
        std::snprintf(buf, n, "%d min %02d s", m, ss);
    } else {
        std::snprintf(buf, n, "%d s", ss);
    }
}

class S11SharesScreen : public Screen {
  public:
    int number() const override
    {
        return 11;
    }
    const char *name() const override
    {
        return "shares";
    }

    void create(lv_obj_t *parent) override
    {
        if (m_root) {
            destroy();
        }
        m_root = screenMakeRoot(parent, S11_BG);

        //   ---- static chart backdrop: grid, dashed baseline, pen-tip dot ---
        for (int i = 0; i < GRID_COUNT; i++) {
            lv_obj_t *g = lv_obj_create(m_root);
            lv_obj_set_pos(g, 0, GRID_Y0 + i * GRID_STEP);
            lv_obj_set_size(g, 336, 1);
            lv_obj_set_style_bg_color(g, lv_color_hex(S11_GRID), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(g, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_width(g, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(g, 0, LV_PART_MAIN);
            lv_obj_clear_flag(g, LV_OBJ_FLAG_SCROLLABLE);
        }

        m_baselineLine = lv_line_create(m_root);
        lv_obj_set_pos(m_baselineLine, 0, 0);
        lv_obj_set_size(m_baselineLine, 336, 320);
        lv_obj_set_style_line_color(m_baselineLine, lv_color_hex(S11_PANEL_BORDER), LV_PART_MAIN);
        lv_obj_set_style_line_width(m_baselineLine, 2, LV_PART_MAIN);
        lv_obj_set_style_line_dash_width(m_baselineLine, 5, LV_PART_MAIN);
        lv_obj_set_style_line_dash_gap(m_baselineLine, 4, LV_PART_MAIN);
        m_baselinePts[0] = {0, YBASE};
        m_baselinePts[1] = {336, YBASE};
        lv_line_set_points(m_baselineLine, m_baselinePts, 2);

        //   Pen: rebuilt every update from UiState::recent[]. lv_line (not
        //   lv_chart) because the approved design's trace is not a regular time series
        //   -- each share contributes 3 irregularly-spaced points (down-tick,
        //   spike, down-tick) so the pen visually snaps back to the baseline
        //   between shares, which lv_chart's fixed-point-count model cannot
        //   express (its X spacing is uniform by point index, not by time).
        //   A plain lv_line with a hand-built lv_point_t buffer reproduces the
        //   the approved design's own SVG <polyline> approach directly.
        m_penLine = lv_line_create(m_root);
        lv_obj_set_pos(m_penLine, 0, 0);
        lv_obj_set_size(m_penLine, 336, 320);
        lv_obj_set_style_line_color(m_penLine, lv_color_hex(S11_PEN), LV_PART_MAIN);
        lv_obj_set_style_line_width(m_penLine, 2, LV_PART_MAIN);
        lv_obj_set_style_line_rounded(m_penLine, true, LV_PART_MAIN);
        m_penPts[0] = {X0, YBASE};
        m_penPts[1] = {X1, YBASE};
        m_penCount = 2;
        lv_line_set_points(m_penLine, m_penPts, m_penCount);

        m_tipDot = lv_obj_create(m_root);
        lv_obj_set_pos(m_tipDot, X1 - 3, YBASE - 3);
        lv_obj_set_size(m_tipDot, 7, 7);
        lv_obj_set_style_bg_color(m_tipDot, lv_color_hex(S11_PEN), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(m_tipDot, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(m_tipDot, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(m_tipDot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_pad_all(m_tipDot, 0, LV_PART_MAIN);
        lv_obj_clear_flag(m_tipDot, LV_OBJ_FLAG_SCROLLABLE);

        //  Right-aligned and hugging the pen-tip/baseline marker (X1, YBASE) instead
        //  of spanning almost the full chart width from the left edge -- the old
        //  16.316 span put the caption directly in the trace's most common spike band
        //  across nearly the whole window. Narrowing it to end at X1 (where the
        //  baseline dot already sits) moves it out of that band without touching any
        //  chart geometry (bug 2, see ).
        m_baselineLabel = screenMakeLabel(m_root, "pool reference: --", X1 - 150, 250, 150, &font_archivo_regular_15,
                                          S11_BASELINE_LBL, LV_TEXT_ALIGN_RIGHT);

        //   Warning (hidden unless noShare -- see update). Two lines fit
        //   comfortably in the ~206 px the approved design budgets (left:16 right:144).
        m_warnLabel = lv_label_create(m_root);
        lv_obj_set_pos(m_warnLabel, 16, 108);
        lv_obj_set_size(m_warnLabel, 320, 48);
        lv_label_set_long_mode(m_warnLabel, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(m_warnLabel, &font_archivo_regular_15, LV_PART_MAIN);
        lv_obj_set_style_text_color(m_warnLabel, lv_color_hex(S11_WARN), LV_PART_MAIN);
        lv_obj_set_style_text_align(m_warnLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_text(m_warnLabel, "");
        lv_obj_add_flag(m_warnLabel, LV_OBJ_FLAG_HIDDEN);

        //   Up to 2 "recorde" annotations: a short tick + a small label, both
        //   built once and hidden by default (screenMakeLabel/lv_obj_create
        //   above the pen so they always paint on top -- created last).
        for (int i = 0; i < 2; i++) {
            m_ann[i].tick = lv_obj_create(m_root);
            lv_obj_set_size(m_ann[i].tick, 2, 8);
            lv_obj_set_style_bg_color(m_ann[i].tick, lv_color_hex(S11_PEN), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(m_ann[i].tick, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_width(m_ann[i].tick, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(m_ann[i].tick, 0, LV_PART_MAIN);
            lv_obj_clear_flag(m_ann[i].tick, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(m_ann[i].tick, LV_OBJ_FLAG_HIDDEN);

            m_ann[i].label = screenMakeLabel(m_root, "record", 0, 0, 60, &font_archivo_medium_15, S11_PEN, LV_TEXT_ALIGN_CENTER);
            lv_obj_add_flag(m_ann[i].label, LV_OBJ_FLAG_HIDDEN);
        }

        //   ---- right panel -----------------------------------------------
        lv_obj_t *border = lv_obj_create(m_root);
        lv_obj_set_pos(border, PANEL_X, 0);
        lv_obj_set_size(border, 2, 320);
        lv_obj_set_style_bg_color(border, lv_color_hex(S11_PANEL_BORDER), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(border, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(border, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(border, 0, LV_PART_MAIN);
        lv_obj_clear_flag(border, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *panelBg = lv_obj_create(m_root);
        lv_obj_set_pos(panelBg, PANEL_X + 2, 0);
        lv_obj_set_size(panelBg, 480 - PANEL_X - 2, 320);
        lv_obj_set_style_bg_color(panelBg, lv_color_hex(S11_PANEL_BG), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(panelBg, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(panelBg, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(panelBg, 0, LV_PART_MAIN);
        lv_obj_clear_flag(panelBg, LV_OBJ_FLAG_SCROLLABLE);

        buildRow(kRowY[0], "shares per minute");
        m_perMinVal = buildValue(kRowY[0].value);
        buildRow(kRowY[1], "accepted");
        m_acceptedVal = buildValue(kRowY[1].value);
        buildRow(kRowY[2], "rejected");
        m_rejectedVal = buildValue(kRowY[2].value);
        buildRow(kRowY[3], "acceptance rate");
        m_rateVal = buildValue(kRowY[3].value);
        buildRow(kRowY[4], "window peak");
        m_peakVal = screenMakeLabel(m_root, "--", ROW_X, kRowY[4].value, 68, &font_s06_mono_24, S11_INK);
        // Baseline-align the "k"/"M"/. suffix with the mono digits above:
        // font_s06_mono_24 is line_height=22/base_line=4 (top-to-baseline 18 px),
        // font_archivo_regular_15 is line_height=16/base_line=3 (top-to-baseline 13
        // px), so the unit label's top must sit 18-13 = 5 px below the value label's
        // top for the two baselines to coincide. This was +8 (3 px too low), which is
        // exactly why the unit looked like a separate, lower element instead of
        // reading as one "85,4k" (bug 1, see ).
        m_peakUnit = screenMakeLabel(m_root, "", ROW_X + 70, kRowY[4].value + 5, 46, &font_archivo_regular_15, S11_INK);
    }

    void update(const UiState &s) override
    {
        if (!m_root) {
            return;
        }

        //   ---- validity / effective pool-diff (never text-displayed unless
        //   real) ------------------------------------------------------------
        bool poolDiffValid = !std::isnan(s.poolDiff) && s.poolDiff > 0.0;
        //   The approved design's own `net.poolDiff || 32768` fallback: used ONLY to give
        //   the chart a sane vertical scale/baseline when the real value is
        //   unknown, never printed as if it were a live reading (that part of
        //   the approved design is intentionally NOT ported, because this task's
        //   "never invent a number" rule is stricter than the approved design's
        //   own JS falsy-fallback).
        float poolDiffScale = poolDiffValid ? (float) s.poolDiff : 32768.0f;

        int n = std::min(s.recentCount, (int) UI_SHARES_RECENT);
        n = std::max(n, 0);

        int64_t nowRef = 0;
        bool haveNow = false;
        if (s.clockValid) {
            nowRef = s.epochS;
            haveNow = true;
        } else if (n > 0 && s.recent[n - 1].epochS > 0) {
            nowRef = s.recent[n - 1].epochS; // newest sample as a fallback "now"
            haveNow = true;
        }
        int64_t windowStart = nowRef - (int64_t) WINDOW_S;

        //   ---- windowed shares + y-scale + text-safe peak -------------------
        double maxDiffScale = poolDiffScale;
        double maxDiffText = poolDiffValid ? s.poolDiff : NAN;
        int wStart = -1, wEnd = -1; // inclusive index range into s.recent[], ascending (oldest first)
        if (haveNow) {
            for (int i = 0; i < n; i++) {
                const UiShare &r = s.recent[i];
                if (r.epochS <= 0 || r.epochS < windowStart || r.epochS > nowRef) {
                    continue;
                }
                if (wStart < 0) {
                    wStart = i;
                }
                wEnd = i;
                if (std::isnan(maxDiffText) || r.diff > maxDiffText) {
                    maxDiffText = r.diff;
                }
                if (r.diff > maxDiffScale) {
                    maxDiffScale = r.diff;
                }
            }
        }
        bool haveWindowShares = wStart >= 0;

        float logLo = std::log10(std::max(1.0f, poolDiffScale * 0.4f));
        float logHi = std::log10(std::max(poolDiffScale * 1.3f, (float) maxDiffScale));
        if (logHi <= logLo) {
            logHi = logLo + 0.5f;
        }
        auto yFor = [&](double diff) -> lv_coord_t {
            float lv = std::log10((float) std::max(1.0, diff));
            return (lv_coord_t) nqfmt::mapf(lv, logLo, logHi, (float) YBASE, (float) YTOP);
        };
        auto xFor = [&](int64_t epochS) -> float {
            float rel = (float) (epochS - windowStart); // 0..WINDOW_S, safe from epochS's own magnitude
            return nqfmt::mapf(rel, 0.0f, WINDOW_S, (float) X0, (float) X1);
        };

        //   ---- "no share" gate: never happened yet (-1 sentinel, ui_state.h),
        //   too long ago, or the pool link itself is down. The task's ">180s"
        //   rule is extended to also cover "-1 = never" (an unelapsed, not a
        //   zero, duration) -- see
        bool everShared = s.lastShareAgeS >= 0.0f;
        bool noShare = !everShared || s.lastShareAgeS > 180.0f || !s.poolConnected;

        //   ---- pen trace ------------------------------------------------------
        m_penCount = 0;
        lv_coord_t ann[2][2] = {}; // [slot][x,y]
        bool annValid[2] = {false, false};
        if (noShare || !haveWindowShares) {
            m_penPts[m_penCount++] = {X0, YBASE};
            m_penPts[m_penCount++] = {X1, YBASE};
        } else {
            m_penPts[m_penCount++] = {X0, YBASE};
            for (int i = wStart; i <= wEnd; i++) {
                const UiShare &r = s.recent[i];
                if (r.epochS <= 0 || r.epochS < windowStart || r.epochS > nowRef) {
                    continue;
                }
                float x = xFor(r.epochS);
                lv_coord_t y = yFor(r.diff);
                lv_coord_t xL = (lv_coord_t) nqfmt::clampf(x - 3.0f, (float) X0, (float) X1);
                lv_coord_t xR = (lv_coord_t) nqfmt::clampf(x + 3.0f, (float) X0, (float) X1);
                if (m_penCount + 3 <= MAX_PEN_POINTS) {
                    m_penPts[m_penCount++] = {xL, YBASE};
                    m_penPts[m_penCount++] = {(lv_coord_t) x, y};
                    m_penPts[m_penCount++] = {xR, YBASE};
                }
                //   Record candidates: diff at/above the session's best is,
                //   by definition, the share that set (or tied) it.
                //   bestSession<=0 means "not populated yet" (ui_state.h has
                //   no separate valid flag for it; 0 is not a physically
                //   possible share difficulty) -- treated as unknown, so no
                //   annotation is ever invented from a zeroed field.
                if (s.bestSession > 0.0 && r.diff >= s.bestSession) {
                    ann[0][0] = ann[1][0];
                    ann[0][1] = ann[1][1];
                    annValid[0] = annValid[1];
                    ann[1][0] = (lv_coord_t) x;
                    ann[1][1] = y;
                    annValid[1] = true;
                }
            }
            m_penPts[m_penCount++] = {X1, YBASE};
        }
        lv_line_set_points(m_penLine, m_penPts, m_penCount);

        for (int i = 0; i < 2; i++) {
            if (annValid[i]) {
                lv_coord_t ax = ann[i][0], ay = ann[i][1];
                lv_obj_set_pos(m_ann[i].tick, ax - 1, ay - 10);
                lv_obj_clear_flag(m_ann[i].tick, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_pos(m_ann[i].label, ax - 30, ay - 26);
                lv_obj_clear_flag(m_ann[i].label, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(m_ann[i].tick, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(m_ann[i].label, LV_OBJ_FLAG_HIDDEN);
            }
        }

        //   ---- warning ---------------------------------------------------
        if (noShare) {
            char durBuf[24], msg[160];
            //   Plain hyphen, not an em dash: font_archivo_regular_15's
            //   generated glyph set (tools/fonts/generate-fonts.ps1's $FULL
            //   range) is ASCII + Latin-1 pt-BR only, no U+2014 -- an em dash
            //   rendered as a missing-glyph tofu box when first tried here
            //   (caught during simulator validation).
            const char *cause = s.poolConnected ? "check the connection" : "pool disconnected";
            if (!everShared) {
                std::snprintf(msg, sizeof(msg), "no shares yet - %s", cause);
            } else {
                formatDurShort(durBuf, sizeof(durBuf), s.lastShareAgeS);
                std::snprintf(msg, sizeof(msg), "no share for %s - %s", durBuf, cause);
            }
            lv_label_set_text(m_warnLabel, msg);
            lv_obj_clear_flag(m_warnLabel, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(m_warnLabel, LV_OBJ_FLAG_HIDDEN);
        }

        //   ---- baseline caption --------------------------------------------
        DiffParts poolParts = formatDiffParts(poolDiffValid ? s.poolDiff : NAN);
        char baseLine[48];
        if (poolParts.unit[0]) {
            std::snprintf(baseLine, sizeof(baseLine), "pool reference: %s %s", poolParts.num, poolParts.unit);
        } else {
            std::snprintf(baseLine, sizeof(baseLine), "pool reference: %s", poolParts.num);
        }
        lv_label_set_text(m_baselineLabel, baseLine);

        //   ---- panel ---------------------------------------------------------
        char buf[24];
        nqfmt::n1(buf, sizeof(buf), s.sharesPerMin);
        lv_label_set_text(m_perMinVal, buf);

        nqfmt::n0(buf, sizeof(buf), (float) s.sharesAccepted);
        lv_label_set_text(m_acceptedVal, buf);

        nqfmt::n0(buf, sizeof(buf), (float) s.sharesRejected);
        lv_label_set_text(m_rejectedVal, buf);

        uint64_t total = s.sharesAccepted + s.sharesRejected;
        if (total == 0) {
            lv_label_set_text(m_rateVal, "--");
        } else {
            float rate = (float) s.sharesAccepted / (float) total * 100.0f;
            char rbuf[16];
            nqfmt::n1(rbuf, sizeof(rbuf), rate);
            std::snprintf(buf, sizeof(buf), "%s%%", rbuf);
            lv_label_set_text(m_rateVal, buf);
        }

        DiffParts peakParts = formatDiffParts(maxDiffText);
        lv_label_set_text(m_peakVal, peakParts.num);
        lv_label_set_text(m_peakUnit, peakParts.unit);
    }

    void destroy() override
    {
        if (m_root) {
            lv_obj_del(m_root);
            m_root = nullptr;
            m_penLine = nullptr;
            m_baselineLine = nullptr;
            m_tipDot = nullptr;
            m_baselineLabel = nullptr;
            m_warnLabel = nullptr;
            m_perMinVal = nullptr;
            m_acceptedVal = nullptr;
            m_rejectedVal = nullptr;
            m_rateVal = nullptr;
            m_peakVal = nullptr;
            m_peakUnit = nullptr;
            m_ann[0] = Ann{};
            m_ann[1] = Ann{};
            m_penCount = 0;
        }
    }

  private:
    struct Ann
    {
        lv_obj_t *tick = nullptr;
        lv_obj_t *label = nullptr;
    };

    void buildRow(const RowY &ry, const char *label)
    {
        screenMakeLabel(m_root, label, ROW_X, ry.label, ROW_W, &font_archivo_regular_15, S11_LABEL);
    }

    lv_obj_t *buildValue(int y)
    {
        return screenMakeLabel(m_root, "--", ROW_X, y, ROW_W, &font_s06_mono_24, S11_INK);
    }

    lv_obj_t *m_root = nullptr;
    lv_obj_t *m_baselineLine = nullptr;
    lv_point_t m_baselinePts[2] = {};
    lv_obj_t *m_penLine = nullptr;
    lv_point_t m_penPts[MAX_PEN_POINTS] = {};
    int m_penCount = 0;
    lv_obj_t *m_tipDot = nullptr;
    lv_obj_t *m_baselineLabel = nullptr;
    lv_obj_t *m_warnLabel = nullptr;
    Ann m_ann[2];

    lv_obj_t *m_perMinVal = nullptr;
    lv_obj_t *m_acceptedVal = nullptr;
    lv_obj_t *m_rejectedVal = nullptr;
    lv_obj_t *m_rateVal = nullptr;
    lv_obj_t *m_peakVal = nullptr;
    lv_obj_t *m_peakUnit = nullptr;
};

S11SharesScreen s_instance;

} // namespace

Screen *screenGetS11Shares()
{
    return &s_instance;
}
