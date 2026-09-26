// Ported from the approved design to LVGL 8.3 (DISPLAY_
// PROFILE_YYSLUPING_480X320). "Hashrate": the classic hashrate-over-time
// chart, with a dashed reference line at the frequency-cap ceiling, thin
// vertical marks where the governor changed frequency, 4 rolling averages
// and a one-line "the heat costs you N TH/s" insight sentence.
//
// Before this pass: a skeleton that
// only showed the newest history sample as plain text and a raw sample
// count -- no chart, no averages row beyond "atual"/"media 1h", no insight
// sentence, generic BIG_* palette.
//
// After: the approved design's own paper-and-ink palette (cream #f6f4ec / ink
// #1b1a17 / ultramarine accent #1f3fe0), an lv_chart LINE series fed from
// UiState::histGhs, a dashed lv_chart-ceiling line, a fixed pool of 26 lv_line
// governor-change marks, the 4 hr1m/hr10m/hr1h/hr1d averages and the pt-BR
// insight sentence. In particular, the dropped "area under the curve" fill and
// the dropped sand-colored heat-cost wedge polygon, neither of which this LVGL
// version can draw for a LINE-type chart (see the comment above buildChart).

#include "../fonts/fonts.h"
#include "../ui_state.h"
#include "nq_fmt.h"
#include "screen.h"
#include "screen_common.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace
{

// ---- 14-hashrate's own palette (the approved design) --
constexpr uint32_t H_PAPEL = 0xf6f4ec;
constexpr uint32_t H_TINTA = 0x1b1a17;
constexpr uint32_t H_CINZA = 0x86827a;
constexpr uint32_t H_LINHA = 0xddd7c8;
constexpr uint32_t H_ULTRA = 0x1f3fe0;
// H_AREIA (#d9b98c, the "heat cost wedge" sand fill) is intentionally
// unused -- see the comment above buildChart for why that shape was
// dropped instead of approximated.

constexpr int CHART_X = 12, CHART_Y = 42;
constexpr int CHART_W = 456, CHART_H = 160;
constexpr int CHART_POINTS = 180; // fixed point budget; see renderChart
constexpr int MAX_TICKS = 26;     // pool size for governor-change marks

class S14HashrateScreen : public Screen {
  public:
    int number() const override
    {
        return 14;
    }
    const char *name() const override
    {
        return "hashrate";
    }

    void create(lv_obj_t *parent) override
    {
        if (m_root) {
            destroy();
        }
        m_root = screenMakeRoot(parent, H_PAPEL);

        screenMakeLabel(m_root, "Hashrate", 12, 12, 200, &font_archivo_bold_20, H_TINTA);
        //   The approved design also has a 1h/6h/24h tab switcher above the chart
        //   (auto-rotating every 6s in the gallery). Task scope explicitly
        //   allows fixing on a single window instead of implementing that
        //   switcher -- see This screen
        //   always shows whatever UiState::histCount currently holds (up to
        //   24h/1440 points), and says so via the axis labels instead of a
        //   tab row.

        buildChart();

        m_axisL = screenMakeLabel(m_root, "--", 12, CHART_Y + CHART_H + 4, 150, &font_archivo_medium_15, H_CINZA);
        m_axisM =
            screenMakeLabel(m_root, "--", 12, CHART_Y + CHART_H + 4, 456, &font_archivo_medium_15, H_CINZA, LV_TEXT_ALIGN_CENTER);
        m_axisR =
            screenMakeLabel(m_root, "now", 318, CHART_Y + CHART_H + 4, 150, &font_archivo_medium_15, H_CINZA, LV_TEXT_ALIGN_RIGHT);

        lv_obj_t *divider = lv_obj_create(m_root);
        lv_obj_set_pos(divider, 12, 232);
        lv_obj_set_size(divider, 456, 1);
        lv_obj_set_style_bg_color(divider, lv_color_hex(H_LINHA), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(divider, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(divider, 0, LV_PART_MAIN);
        lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE);

        //   4 averages (1 min / 10 min / 1 h / 24 h), stacked label-over-value
        //   per column instead of the approved design's single inline row: an inline
        //   "label value" pair needs the label's rendered text width to
        //   place the value right after it, which means measuring text at
        //   runtime (lv_txt_get_size) -- doable, but a stacked layout gets
        //   the same 4-numbers-at-a-glance result with fixed column
        //   coordinates and no runtime measurement.
        static const char *kAvgLabels[4] = {"1 min", "10 min", "1 h", "24 h"};
        constexpr int colX[4] = {12, 127, 242, 357};
        for (int i = 0; i < 4; i++) {
            screenMakeLabel(m_root, kAvgLabels[i], colX[i], 236, 110, &font_archivo_medium_15, H_CINZA);
            m_avgVal[i] = screenMakeLabel(m_root, "--", colX[i], 254, 110, &font_archivo_bold_24, H_TINTA);
        }

        //   One line, like the approved design (its own CSS gives this sentence no
        //   extra height budget either): kept at screenMakeLabel's default
        //   LV_LABEL_LONG_CLIP rather than wrapping, so a pathological value
        //   clips instead of growing past the 320px-tall screen's bottom
        //   margin.
        m_insight = screenMakeLabel(m_root, "", 12, 286, 456, &font_archivo_medium_17, H_TINTA);
        lv_label_set_recolor(m_insight, true);
    }

    void update(const UiState &s) override
    {
        if (!m_root) {
            return;
        }

        char buf[16];
        m_avgKeys[0] = s.hr1m;
        m_avgKeys[1] = s.hr10m;
        m_avgKeys[2] = s.hr1h;
        m_avgKeys[3] = s.hr1d;
        for (int i = 0; i < 4; i++) {
            nqfmt::ths(buf, sizeof(buf), m_avgKeys[i], 2);
            lv_label_set_text(m_avgVal[i], buf);
        }

        renderChart(s);
    }

    void destroy() override
    {
        if (m_root) {
            lv_obj_del(m_root);
            m_root = nullptr;
            m_chartWrap = nullptr;
            m_chart = nullptr;
            m_series = nullptr;
            m_ceilLine = nullptr;
            m_unitTag = nullptr;
            m_ceilLabel = nullptr;
            m_axisL = m_axisM = m_axisR = nullptr;
            for (int i = 0; i < 4; i++) {
                m_avgVal[i] = nullptr;
            }
            m_insight = nullptr;
            for (int i = 0; i < MAX_TICKS; i++) {
                m_tick[i] = nullptr;
            }
        }
    }

  private:
    //   Builds the chart + its overlays once. All children of one
    //   `chartWrap` container so every coordinate below is local to the
    //   456x160 chart area (matches CHART_X/CHART_Y placement of the
    //   container itself).
    //
    // IMPORTANT LVGL-version note (verified by reading the vendored source, not
    // assumed): main/./managed_components/lvgl__lvgl/src/
    // extra/widgets/chart/lv_chart.c's draw_series_line -- the function that
    // draws LV_CHART_TYPE_LINE series -- only issues line draws (lv_draw_line
    // with the LV_PART_ITEMS descriptor) and, optionally, point markers
    // (LV_PART_INDICATOR). It never reads LV_PART_ITEMS's bg_opa/bg_color to fill
    // the area under the curve the way draw_series_bar does for
    // LV_CHART_TYPE_BAR. So the approved design's "area under the line, 15%
    // ultramarine" fill and its sand-colored heat-cost wedge (an arbitrary
    // 2-curve polygon, which no LVGL 8.3 widget draws either) are BOTH dropped
    // here rather than faking them with styles that this LVGL version silently
    // ignores. What's kept: the line itself, the dashed ceiling reference line,
    // and the governor-change tick marks -- the numeric "O calor custa." sentence
    // still carries the same information the wedge did.
    void buildChart()
    {
        lv_obj_t *wrap = lv_obj_create(m_root);
        lv_obj_set_pos(wrap, CHART_X, CHART_Y);
        lv_obj_set_size(wrap, CHART_W, CHART_H);
        lv_obj_set_style_bg_opa(wrap, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(wrap, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(wrap, 0, LV_PART_MAIN);
        lv_obj_clear_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);
        m_chartWrap = wrap;

        m_chart = lv_chart_create(wrap);
        lv_obj_set_pos(m_chart, 0, 0);
        lv_obj_set_size(m_chart, CHART_W, CHART_H);
        lv_chart_set_type(m_chart, LV_CHART_TYPE_LINE);
        lv_chart_set_point_count(m_chart, CHART_POINTS);
        lv_chart_set_div_line_count(m_chart, 0, 0);
        lv_obj_set_style_bg_opa(m_chart, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(m_chart, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(m_chart, 0, LV_PART_MAIN);
        lv_obj_set_style_line_width(m_chart, 2, LV_PART_ITEMS);
        lv_obj_set_style_size(m_chart, 0, LV_PART_INDICATOR); // hide per-point dots
        lv_obj_clear_flag(m_chart, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(m_chart, LV_OBJ_FLAG_CLICKABLE);
        m_series = lv_chart_add_series(m_chart, lv_color_hex(H_ULTRA), LV_CHART_AXIS_PRIMARY_Y);

        //   Baseline rule (static, matches the approved design's fixed bottom axis
        //   line -- never moves, so it is a plain rect, not part of the
        //   chart).
        lv_obj_t *baseline = lv_obj_create(wrap);
        lv_obj_set_pos(baseline, 0, CHART_H - 1);
        lv_obj_set_size(baseline, CHART_W, 1);
        lv_obj_set_style_bg_color(baseline, lv_color_hex(H_LINHA), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(baseline, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(baseline, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(baseline, 0, LV_PART_MAIN);
        lv_obj_clear_flag(baseline, LV_OBJ_FLAG_SCROLLABLE);

        //   Dashed ceiling reference line.
        m_ceilLine = lv_line_create(wrap);
        lv_obj_set_style_line_color(m_ceilLine, lv_color_hex(H_CINZA), LV_PART_MAIN);
        lv_obj_set_style_line_width(m_ceilLine, 1, LV_PART_MAIN);
        lv_obj_set_style_line_dash_width(m_ceilLine, 4, LV_PART_MAIN);
        lv_obj_set_style_line_dash_gap(m_ceilLine, 4, LV_PART_MAIN);
        m_ceilPts[0] = {0, 0};
        m_ceilPts[1] = {CHART_W, 0};
        lv_line_set_points(m_ceilLine, m_ceilPts, 2);

        //   Governor-change tick pool: fixed, never recreated, hidden by
        //   default (opa 0) until renderChart has something to show at
        //   that slot.
        for (int i = 0; i < MAX_TICKS; i++) {
            m_tick[i] = lv_line_create(wrap);
            lv_obj_set_style_line_color(m_tick[i], lv_color_hex(H_TINTA), LV_PART_MAIN);
            lv_obj_set_style_line_width(m_tick[i], 1, LV_PART_MAIN);
            lv_obj_set_style_line_opa(m_tick[i], LV_OPA_TRANSP, LV_PART_MAIN);
            m_tickPts[i][0] = {0, 0};
            m_tickPts[i][1] = {0, CHART_H};
            lv_line_set_points(m_tick[i], m_tickPts[i], 2);
        }

        m_unitTag = screenMakeLabel(wrap, "TH/s", 0, 0, 60, &font_archivo_medium_15, H_CINZA);
        m_ceilLabel = screenMakeLabel(wrap, "", 100, 0, 356, &font_archivo_medium_15, H_CINZA, LV_TEXT_ALIGN_RIGHT);
    }

    //   Downsamples UiState::histGhs/histFreq (1 sample/min, up to
    //   UI_HIST_POINTS=1440 = 24h) into the chart's fixed CHART_POINTS
    //   budget, sets the Y range, repositions the ceiling line and the
    //   governor-change tick pool, and refreshes the averages/insight text.
    void renderChart(const UiState &s)
    {
        int n = s.histCount;
        if (n < 2) {
            for (int i = 0; i < MAX_TICKS; i++) {
                lv_obj_set_style_line_opa(m_tick[i], LV_OPA_TRANSP, LV_PART_MAIN);
            }
            //   Ceiling dashed line must not be drawn with insufficient
            //   history: without this, it kept whatever opacity a PRIOR
            //   renderChart call had left it at (LV_OPA_COVER by default
            //   on the very first call, since buildChart never sets an
            //   initial opa), so a screen freshly rotated into this
            //   scenario showed a stray dashed line flat across y=0 -- the
            //   unset m_ceilPts default -- instead of no line at all.
            lv_obj_set_style_line_opa(m_ceilLine, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_label_set_text(m_ceilLabel, "");
            lv_label_set_text(m_axisL, "--");
            lv_label_set_text(m_axisM, "--");
            lv_label_set_text(m_insight, "Not enough history yet.");
            return;
        }

        int pointCount = n < CHART_POINTS ? n : CHART_POINTS;
        lv_chart_set_point_count(m_chart, pointCount);

        float dataMin = 1e9f, dataMax = -1e9f;
        for (int j = 0; j < pointCount; j++) {
            int idx = (int) ((int64_t) j * n / pointCount);
            float v = s.histGhs[idx];
            if (std::isnan(v)) {
                m_series->y_points[j] = LV_CHART_POINT_NONE;
                continue;
            }
            m_series->y_points[j] = (lv_coord_t) lroundf(v);
            dataMin = std::min(dataMin, v);
            dataMax = std::max(dataMax, v);
        }
        if (dataMax < dataMin) { // every sample was NAN
            dataMin = 0.0f;
            dataMax = 0.0f;
        }

        //   Ceiling: theoretical max throughput AT freqCap, computed the
        //   same way the ASIC driver itself does (GH/s = freq_MHz *
        //   smallCoreCount * asicCount / 1000), NOT scaled from the live
        //   hrExpected-at-current-freq the way this used to work. That
        //   previous approach (ceilGhs = hrExpected * freqCap/freq) silently
        //   baked in hrExpected's own real-world efficiency loss below the
        //   theoretical max, so it under-reported the ceiling: with this
        //   screen's own review scenario (freqCap=800, freq=775,
        //   hrExpected=6300 GH/s) it produced 6300*(800/775) = 6503.2 GH/s
        //   = "6,50 TH/s", when the actual ceiling at freqCap for this
        //   hardware (BM1370, smallCoreCount=2040, asicCount=4) is
        //   800*2040*4/1000 = 6528 GH/s = "6,53 TH/s".
        //
        //   smallCoreCount is a per-ASIC-model hardware constant (see
        //   BM1370_SMALL_CORE_COUNT in components/bm1397/bm1370.cpp; other
        //   models e.g. BM1368/BM1366 have different values). UiState
        //   (main/displays/ui_state.h) does not expose it, and that header
        //   is this task's FIXED CONTRACT -- off limits to edit here.
        //   Screens also must not #include boards/**/asic.h directly (it
        //   needs ESP-IDF/FreeRTOS, which would break this screen's host
        //   build for tools/sim -- see ui_state.h's own top-of-file comment
        //   on why UiState stays plain data with no board coupling).
        //   DISPLAY_PROFILE_YYSLUPING_480X320 (the only profile that
        //   compiles this screen at all -- see main/CMakeLists.txt) is, as
        //   of this writing, only ever built for BOARD=NERDQAXEPLUS2, which
        //   is hardwired to BM1370 (main/boards/nerdqaxeplus2.cpp:
        //   `m_asics = new BM1370`), so this constant is correct for
        //   every board that currently links this screen. It goes stale
        //   ONLY if a future board reuses this same 480x320 display profile
        //   with a different ASIC -- flagged in the final report as a
        //   follow-up: the real long-term fix is a `smallCoreCount` field on
        //   UiState, populated by main/ui_data.cpp (also out of this pass's
        //   edit scope) from board->getAsics->getSmallCoreCount.
        constexpr float kSmallCoreCount = 2040.0f; // BM1370, NERDQAXEPLUS2
        float ceilGhs = NAN;
        bool ceilKnown = !std::isnan(s.freqCap) && s.asicCount > 0;
        if (ceilKnown) {
            ceilGhs = s.freqCap * kSmallCoreCount * (float) s.asicCount / 1000.0f;
            dataMax = std::max(dataMax, ceilGhs);
        }

        float span = (dataMax - dataMin) > 0.0f ? (dataMax - dataMin) : std::max(1.0f, dataMin * 0.05f);
        float yMin = std::max(0.0f, dataMin - span * 0.25f);
        float yMax = dataMax * 1.04f;
        if (yMax <= yMin) {
            yMax = yMin + 1.0f;
        }
        lv_chart_set_range(m_chart, LV_CHART_AXIS_PRIMARY_Y, (lv_coord_t) lroundf(yMin), (lv_coord_t) lroundf(yMax));
        lv_chart_refresh(m_chart);

        auto yAt = [&](float v) -> float { return nqfmt::mapf(v, yMin, yMax, (float) CHART_H, 0.0f); };

        if (ceilKnown) {
            float yCeil = yAt(ceilGhs);
            m_ceilPts[0].y = (lv_coord_t) yCeil;
            m_ceilPts[1].y = (lv_coord_t) yCeil;
            lv_line_set_points(m_ceilLine, m_ceilPts, 2);
            lv_obj_set_style_line_opa(m_ceilLine, LV_OPA_COVER, LV_PART_MAIN);

            float labelTop = yCeil < 16.0f ? yCeil + 6.0f : yCeil - 16.0f;
            lv_obj_set_y(m_ceilLabel, (lv_coord_t) labelTop);
            char ceilBuf[16], capBuf[16], line[96];
            nqfmt::ths(ceilBuf, sizeof(ceilBuf), ceilGhs, 2);
            nqfmt::n0(capBuf, sizeof(capBuf), s.freqCap);
            snprintf(line, sizeof(line), "governor ceiling: %s TH/s (%s MHz)", ceilBuf, capBuf);
            lv_label_set_text(m_ceilLabel, line);
        } else {
            lv_obj_set_style_line_opa(m_ceilLine, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_label_set_text(m_ceilLabel, "");
        }

        //   Governor-change marks: same >=4 MHz threshold and top-26-by-
        //   magnitude selection as the approved design (the approved design reference's TICK_MHZ/
        //   MAX_TICKS), computed on the SAME downsampled index the chart
        //   itself plots at, so a mark always lines up with the chart point
        //   it belongs to.
        int changeCount = 0;
        for (int j = 1; j < pointCount && changeCount < CHART_POINTS; j++) {
            int idxA = (int) ((int64_t) (j - 1) * n / pointCount);
            int idxB = (int) ((int64_t) j * n / pointCount);
            float fa = s.histFreq[idxA], fb = s.histFreq[idxB];
            if (std::isnan(fa) || std::isnan(fb)) {
                continue;
            }
            float d = std::fabs(fb - fa);
            if (d >= 4.0f) {
                m_changeScratch[changeCount].j = j;
                m_changeScratch[changeCount].mag = d;
                changeCount++;
            }
        }
        std::sort(m_changeScratch, m_changeScratch + changeCount, [](const Change &a, const Change &b) { return a.mag > b.mag; });
        int shown = std::min(changeCount, MAX_TICKS);
        std::sort(m_changeScratch, m_changeScratch + shown, [](const Change &a, const Change &b) { return a.j < b.j; });
        for (int i = 0; i < MAX_TICKS; i++) {
            if (i < shown) {
                float x = pointCount > 1 ? (float) m_changeScratch[i].j / (float) (pointCount - 1) * CHART_W : 0.0f;
                m_tickPts[i][0].x = (lv_coord_t) x;
                m_tickPts[i][1].x = (lv_coord_t) x;
                lv_line_set_points(m_tick[i], m_tickPts[i], 2);
                lv_obj_set_style_line_opa(m_tick[i], 72, LV_PART_MAIN); // ~0.28, matches the approved design's stroke-opacity
            } else {
                lv_obj_set_style_line_opa(m_tick[i], LV_OPA_TRANSP, LV_PART_MAIN);
            }
        }

        //   Time axis: derived from the real sample count (1/min), not from
        //   a fixed 1h/6h/24h table -- see the "no window switcher" note in
        //   create.
        char axisBuf[16];
        if (n >= 60) {
            nqfmt::n0(axisBuf, sizeof(axisBuf), n / 60.0f);
            char line[24];
            snprintf(line, sizeof(line), "-%s h", axisBuf);
            lv_label_set_text(m_axisL, line);
            nqfmt::n0(axisBuf, sizeof(axisBuf), n / 120.0f);
            snprintf(line, sizeof(line), "-%s h", axisBuf);
            lv_label_set_text(m_axisM, line);
        } else {
            nqfmt::n0(axisBuf, sizeof(axisBuf), (float) n);
            char line[24];
            snprintf(line, sizeof(line), "-%s min", axisBuf);
            lv_label_set_text(m_axisL, line);
            nqfmt::n0(axisBuf, sizeof(axisBuf), n / 2.0f);
            snprintf(line, sizeof(line), "-%s min", axisBuf);
            lv_label_set_text(m_axisM, line);
        }

        //   Insight sentence: "Heat is costing X TH/s of the Y TH/s ceiling right now."
        char costBuf[16], line[128];
        float costGhs = ceilKnown ? std::max(0.0f, ceilGhs - s.hr1h) : NAN;
        nqfmt::ths(costBuf, sizeof(costBuf), costGhs, 2);
        char ceilBuf2[16];
        nqfmt::ths(ceilBuf2, sizeof(ceilBuf2), ceilGhs, 2);
        snprintf(line, sizeof(line), "Heat is costing #1f3fe0 %s TH/s# of the %s TH/s ceiling right now.", costBuf, ceilBuf2);
        lv_label_set_text(m_insight, line);
    }

    struct Change
    {
        int j;
        float mag;
    };

    lv_obj_t *m_root = nullptr;
    lv_obj_t *m_chartWrap = nullptr;
    lv_obj_t *m_chart = nullptr;
    lv_chart_series_t *m_series = nullptr;
    lv_obj_t *m_ceilLine = nullptr;
    lv_point_t m_ceilPts[2] = {};
    lv_obj_t *m_tick[MAX_TICKS] = {};
    lv_point_t m_tickPts[MAX_TICKS][2] = {};
    lv_obj_t *m_unitTag = nullptr;
    lv_obj_t *m_ceilLabel = nullptr;
    lv_obj_t *m_axisL = nullptr;
    lv_obj_t *m_axisM = nullptr;
    lv_obj_t *m_axisR = nullptr;
    lv_obj_t *m_avgVal[4] = {};
    float m_avgKeys[4] = {};
    lv_obj_t *m_insight = nullptr;

    //   Scratch buffer for the top-N-by-magnitude governor-change scan.
    //   Sized to the chart's own point budget and kept as a member (BSS, not
    //   stack) so renderChart never needs a ~1.4 KB stack array on the
    //   LVGL timer task.
    Change m_changeScratch[CHART_POINTS] = {};
};

S14HashrateScreen s_instance;

} // namespace

Screen *screenGetS14Hashrate()
{
    return &s_instance;
}
