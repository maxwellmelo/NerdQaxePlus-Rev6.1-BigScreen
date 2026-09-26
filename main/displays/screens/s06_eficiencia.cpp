// Ported from the approved design to LVGL 8.3 (DISPLAY_
// PROFILE_YYSLUPING_480X320). "Eficiência": engineering-notebook style
// J/TH-by-MHz chart plotting the 6 real points measured on this unit's own
// frequency sweep (UiState::sweepMhz/sweepW), with the sweet spot (lowest
// J/TH) marked in green and the machine's current operating point "circled
// in red pen", sliding along the curve as the thermal governor changes
// frequency. Beside it: current efficiency, what 1 TH/s would cost per
// month at the configured tariff, and a sentence spelling out what the
// last 50 MHz of ceiling really cost in watts vs. throughput.
//
// Before this pass: a skeleton
// with a single centered placeholder label -- no chart, no panel, no data.
//
// After: the approved design's own graph-paper palette (parchment #f6efdf bg,
// ink #241f1a, green #3f7a52 for the sweet spot, red #b23a2e for "now"), its
// own type (IBM Plex Mono Bold for every number, Archivo for running text),
// the 6-point curve with sweet-spot/current markers, the 3-row number panel
// and the trade-off sentence. In particular, the ~120-line SVG graph-paper
// grid replaced by a couple dozen lv_line objects at only the major spacing,
// and every original-design string that mixed a bitmap-font-unfriendly
// glyph (the JetBrains Mono value strings embed letters/currency/arrow
// characters the NUM-only IBM Plex Mono glyph set this screen budgets for does
// not contain) split into a mono "digits" label plus a separate Archivo
// "unit/word" label.

#include "../fonts/fonts.h"
#include "../ui_state.h"
#include "nq_fmt.h"
#include "screen.h"
#include "screen_common.h"

#include <cmath>
#include <cstdio>

// New font for this screen : IBM
// Plex Mono Bold, 24 px, digits + ",.-%°" only (~11.5 KB) -- the "ponto
// ideal" and "custaria" readouts. Same family as font_s04_mono_40 (screen
// 04, this screen's "máquina" sibling), generated fresh at a smaller size
// rather than reusing 40px verbatim because those two panel rows are
// visually secondary to the big "eficiência agora" hero, which DOES reuse
// font_s04_mono_40 unchanged (see create below) -- no second mono
// FAMILY is introduced, just a second CUT of the one already paid for by
// screen 04.
LV_FONT_DECLARE(font_s06_mono_24);
// Reused verbatim from screen 04 (main/displays/screens/s04_fluxo_energia.cpp):
// IBM Plex Mono Bold, 40 px, same NUM-only glyph set, declared the exact
// same way (LV_FONT_DECLARE) -- not redeclared under a new name and not
// duplicated as a second.c file.
LV_FONT_DECLARE(font_s04_mono_40);

namespace
{

// ---- 06-eficiencia's own palette (the approved design) --
constexpr uint32_t E_PAPER = 0xf6efdf;
constexpr uint32_t E_GRID = 0xc9b98a;
constexpr uint32_t E_INK = 0x241f1a;
constexpr uint32_t E_GREEN = 0x3f7a52;
constexpr uint32_t E_RED = 0xb23a2e;

// ---- chart geometry (the approved design's own
// local 288x248 SVG space, offset by the chart div's left:12 top:16 --
// copied verbatim, in ABSOLUTE screen coordinates, so every line lands
// exactly where the approved design puts it) ------------------------------------
constexpr int CHART_OX = 12, CHART_OY = 16;
constexpr int PX0 = CHART_OX + 40, PX1 = CHART_OX + 280; // 52..292
constexpr int PY0 = CHART_OY + 14, PY1 = CHART_OY + 218; // 30..234

constexpr int N_PTS = UI_SWEEP_POINTS; // 6, measured sweep points

// Background "graph paper" + chart axes: 13 vertical (every 40px, 0.480) + 9
// horizontal (every 40px, 0.320) + 2 axis lines = 24 fixed lv_line objects,
// built once and never repositioned.
constexpr int GRID_LINE_MAX = 24;

// the approved design reference: ghs(f) = f * 2040 * 4 / 1000 (GH/s). J/TH == W / (TH/s), and
// TH/s = ghs(f) / 1000 -- so J/TH = w / (f * 2040 * 4 / 1e6), exactly the
// formula the task spec gives.
float jthFor(float mhz, float watts)
{
    float th = mhz * 2040.0f * 4.0f / 1.0e6f;
    return th > 0.0f ? watts / th : NAN;
}

// Glues a "unit" label right after a number label's actual rendered width,
// forcing the layout pass first with lv_obj_update_layout -- same
// technique as s16_sala.cpp's layoutFigureRow (~line 333) and
// s05_quarteto.cpp's layoutValueUnit. Needed because a LV_SIZE_CONTENT
// label's width is only recomputed during the next layout pass, not
// synchronously inside lv_label_set_text; reading it right after changing
// the text without forcing that pass first can return the PREVIOUS frame's
// (or, for a label whose text never changes after create, the fixed box
// width it was created with) rather than the real one -- exactly what left
// a large gap between "500" and "MHz" in the "ponto ideal" readout (see
// a later correction). `gap` matches
// this screen's other tight number/unit pairings.
void positionUnitAfterNumber(lv_obj_t *numLbl, lv_obj_t *unitLbl, int gap)
{
    lv_obj_update_layout(numLbl);
    lv_obj_update_layout(unitLbl);
    lv_coord_t numX = lv_obj_get_x(numLbl);
    lv_coord_t numW = lv_obj_get_width(numLbl);
    lv_coord_t numBottom = lv_obj_get_y(numLbl) + lv_obj_get_height(numLbl);
    lv_coord_t unitH = lv_obj_get_height(unitLbl);
    lv_obj_set_pos(unitLbl, numX + numW + gap, numBottom - unitH + 2);
}

class S06EficienciaScreen : public Screen {
  public:
    int number() const override
    {
        return 6;
    }
    const char *name() const override
    {
        return "eficiencia";
    }

    void create(lv_obj_t *parent) override
    {
        if (m_root) {
            destroy();
        }
        m_root = screenMakeRoot(parent, E_PAPER);
        m_gridUsed = 0;

        //  ---- background: graph-paper grid, MAJOR lines only ---------------
        //  The approved design draws ~120 SVG lines (every 8 px, two opacities) for a
        //  literal millimeter-paper texture. CONTRATO.md / this task both
        //  rule that out for LVGL ("nunca centenas de objetos de grade").
        //  Kept: one line every 40 px (the approved design's own "major" spacing),
        //  full-bleed 480x320 -- 13 vertical + 9 horizontal lv_line objects,
        //  enough to still read as graph paper at arm's length without the
        //  per-redraw cost of a few hundred.
        for (int x = 0; x <= 480; x += 40) {
            addLine(x, 0, x, 320, E_GRID, 1, 90);
        }
        for (int y = 0; y <= 320; y += 40) {
            addLine(0, y, 480, y, E_GRID, 1, 90);
        }

        //  ---- chart axes (fixed) --------------------------------------------
        addLine(PX0, PY0, PX0, PY1, E_INK, 1, LV_OPA_COVER);
        addLine(PX0, PY1, PX1, PY1, E_INK, 1, LV_OPA_COVER);

        m_axisTag = screenMakeLabel(m_root, "J/TH", CHART_OX + 4, CHART_OY + 2, 60, &font_archivo_regular_15, E_INK);
        //  Right edge ends 4 px left of the y-axis (PX0-4), same gap the
        //  the approved design's own right-aligned SVG text keeps from the axis line --
        //  narrow enough that the sweet-spot marker at fMin (which sits
        //  exactly ON the axis, since 500 MHz is the chart's left edge)
        //  never overlaps the tick number above/below it.
        m_axisTop = screenMakeLabel(m_root, "--", CHART_OX, CHART_OY + 14, PX0 - 4 - CHART_OX, &font_archivo_regular_15, E_INK,
                                    LV_TEXT_ALIGN_RIGHT);
        m_axisBot = screenMakeLabel(m_root, "--", CHART_OX, CHART_OY + 196, PX0 - 4 - CHART_OX, &font_archivo_regular_15, E_INK,
                                    LV_TEXT_ALIGN_RIGHT);
        m_axisFMin = screenMakeLabel(m_root, "--", PX0 - 2, PY1 + 6, 60, &font_archivo_regular_15, E_INK);
        m_axisFMid = screenMakeLabel(m_root, "--", PX0, PY1 + 6, PX1 - PX0, &font_archivo_regular_15, E_INK, LV_TEXT_ALIGN_CENTER);
        m_axisFMax = screenMakeLabel(m_root, "--", PX1 - 58, PY1 + 6, 60, &font_archivo_regular_15, E_INK, LV_TEXT_ALIGN_RIGHT);

        // ---- 6 sweep points + polyline: built lazily, once, in the first update
        // (their data -- UiState::sweepMhz/sweepW -- is a fixed calibration table
        // that never changes at runtime, but create has no UiState to read it from).
        // buildCurveOnce.
        m_curveBuilt = false;

        //  current-point marker (moves every update; hidden when freq/eff
        //  are not yet known -- never guessed).
        m_curRing = lv_obj_create(m_root);
        lv_obj_set_size(m_curRing, 13, 13);
        lv_obj_set_style_bg_opa(m_curRing, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_color(m_curRing, lv_color_hex(E_RED), LV_PART_MAIN);
        lv_obj_set_style_border_width(m_curRing, 2, LV_PART_MAIN);
        lv_obj_set_style_radius(m_curRing, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_pad_all(m_curRing, 0, LV_PART_MAIN);
        lv_obj_clear_flag(m_curRing, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(m_curRing, LV_OBJ_FLAG_CLICKABLE);

        m_curDot = lv_obj_create(m_root);
        lv_obj_set_size(m_curDot, 4, 4);
        lv_obj_set_style_bg_color(m_curDot, lv_color_hex(E_RED), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(m_curDot, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(m_curDot, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(m_curDot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_pad_all(m_curDot, 0, LV_PART_MAIN);
        lv_obj_clear_flag(m_curDot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(m_curDot, LV_OBJ_FLAG_CLICKABLE);

        //  ---- right panel: 3 stacked rows ------------------------------------
        constexpr int PANEL_X = 312;
        buildDot(m_dotNow, PANEL_X, 16, E_RED);
        screenMakeLabel(m_root, "efficiency now", PANEL_X + 15, 16, 130, &font_archivo_regular_15, E_INK);
        m_valNow = screenMakeLabel(m_root, "--", PANEL_X, 34, 118, &font_s04_mono_40, E_INK);
        m_valNowUnit = screenMakeLabel(m_root, "J/TH", PANEL_X, 78, 100, &font_archivo_regular_15, E_INK);
        addDivider(PANEL_X, 94, 156);

        buildDot(m_dotIdeal, PANEL_X, 100, E_GREEN);
        screenMakeLabel(m_root, "sweet spot", PANEL_X + 15, 100, 130, &font_archivo_regular_15, E_INK);
        m_valIdeal = screenMakeLabel(m_root, "--", PANEL_X, 120, 76, &font_s06_mono_24, E_INK);
        lv_obj_set_width(m_valIdeal, LV_SIZE_CONTENT); // auto width -- see positionUnitAfterNumber
        m_valIdealUnit = screenMakeLabel(m_root, "MHz", PANEL_X + 58, 128, 60, &font_archivo_regular_15, E_INK);
        positionUnitAfterNumber(m_valIdeal, m_valIdealUnit, 6);
        m_subIdeal = screenMakeLabel(m_root, "--", PANEL_X, 150, 156, &font_archivo_regular_15, E_INK);
        addDivider(PANEL_X, 178, 156);

        screenMakeLabel(m_root, "1 TH/s would cost", PANEL_X, 188, 156, &font_archivo_regular_15, E_INK);
        // Placeholder text only -- overwritten every update with the owner's
        // configured currency symbol (UiState::currency, NVS "currency"), never left
        // hardcoded to "R$". update.
        m_costPrefix = screenMakeLabel(m_root, "R$", PANEL_X, 208, 30, &font_archivo_regular_15, E_INK);
        m_valCost = screenMakeLabel(m_root, "--", PANEL_X + 28, 204, 128, &font_s06_mono_24, E_INK);
        m_subCost = screenMakeLabel(m_root, "per month, at current rate", PANEL_X, 236, 156, &font_archivo_regular_15, E_INK);

        //  ---- trade-off sentence (bottom strip) -------------------------------
        //  Built directly (not via screenMakeLabel, which hardcodes
        //  LV_LABEL_LONG_CLIP): the full sentence does not fit one 456 px
        //  line at 15 px, so it wraps to 2 lines here, matching the
        //  the approved design's own 38 px-tall `.s06-tradeoff` box (which also expects
        //  the text to wrap, not to be clipped mid-word).
        m_tradeoff = lv_label_create(m_root);
        lv_label_set_long_mode(m_tradeoff, LV_LABEL_LONG_WRAP);
        lv_obj_set_pos(m_tradeoff, 12, 270);
        lv_obj_set_size(m_tradeoff, 456, LV_SIZE_CONTENT);
        lv_obj_set_style_text_font(m_tradeoff, &font_archivo_regular_15, LV_PART_MAIN);
        lv_obj_set_style_text_color(m_tradeoff, lv_color_hex(E_INK), LV_PART_MAIN);
        lv_label_set_text(m_tradeoff, "");
    }

    void update(const UiState &s) override
    {
        if (!m_root) {
            return;
        }

        if (!m_curveBuilt) {
            buildCurveOnce(s);
        }

        //  ---- current point: freq + live efficiency. Neither field may be
        //  invented -- NAN in either one hides the whole marker AND blanks
        //  the hero readout, rather than falling back to a computed/
        //  theoretical estimate the way the approved design's simulator-only
        //  fallbackEff does (the approved design never sees a real NAN; real
        //  hardware does, and ui_state.h's contract is explicit: NAN must
        //  become "--", never a guessed number).
        bool haveNow = !std::isnan(s.freq) && !std::isnan(s.effJth) && m_fMax > m_fMin && m_yHi > m_yLo;
        if (haveNow) {
            float x = mapX(s.freq);
            float y = mapY(s.effJth);
            lv_obj_set_pos(m_curRing, (lv_coord_t) lroundf(x - 6.5f), (lv_coord_t) lroundf(y - 6.5f));
            lv_obj_set_pos(m_curDot, (lv_coord_t) lroundf(x - 2.0f), (lv_coord_t) lroundf(y - 2.0f));
            lv_obj_clear_flag(m_curRing, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(m_curDot, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(m_curRing, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(m_curDot, LV_OBJ_FLAG_HIDDEN);
        }

        //  Owner-configured currency symbol (NVS "currency" via
        //  UiState::currency), replacing what used to be a fixed "R$"
        //  literal -- see create's placeholder comment.
        lv_label_set_text(m_costPrefix, s.currency);

        char numBuf[nqfmt::kNumBufMax];
        if (std::isnan(s.effJth)) {
            lv_label_set_text(m_valNow, "--");
        } else {
            nqfmt::n1(numBuf, sizeof(numBuf), s.effJth);
            lv_label_set_text(m_valNow, numBuf);
        }

        //  "1 TH/s custaria": eff (J/TH == W per TH/s) x 24h x 30d / 1000
        //  (Wh->kWh) x tarifa. tarifa is a config constant (never NAN in
        //  this contract), eff is the only NAN-able input.
        if (std::isnan(s.effJth)) {
            lv_label_set_text(m_valCost, "--");
        } else {
            float costMonth = s.effJth * 24.0f * 30.0f / 1000.0f * s.tarifa;
            nqfmt::n2(numBuf, sizeof(numBuf), costMonth);
            lv_label_set_text(m_valCost, numBuf);
        }
    }

    void destroy() override
    {
        if (m_root) {
            lv_obj_del(m_root);
            m_root = nullptr;
            m_axisTag = m_axisTop = m_axisBot = nullptr;
            m_axisFMin = m_axisFMid = m_axisFMax = nullptr;
            m_curRing = m_curDot = nullptr;
            m_dotNow.rect = m_dotIdeal.rect = nullptr;
            m_valNow = m_valNowUnit = nullptr;
            m_valIdeal = m_valIdealUnit = m_subIdeal = nullptr;
            m_costPrefix = m_valCost = m_subCost = nullptr;
            m_tradeoff = nullptr;
            m_curveBuilt = false;
            m_gridUsed = 0;
            m_fMin = m_fMax = m_yLo = m_yHi = 0.0f;
        }
    }

  private:
    struct Dot
    {
        lv_obj_t *rect = nullptr;
    };

    //  Adds one fixed structural line (background grid / chart axes). Point
    //  storage comes from m_gridPts (a member array, same lifetime as
    //  m_root) because lv_line_set_points stores the POINTER it is given,
    //  not a copy -- a local/stack array here would dangle the moment this
    //  function returned.
    lv_obj_t *addLine(int x0, int y0, int x1, int y1, uint32_t color, int width, lv_opa_t opa)
    {
        lv_point_t *p = m_gridPts[m_gridUsed++];
        p[0] = {(lv_coord_t) x0, (lv_coord_t) y0};
        p[1] = {(lv_coord_t) x1, (lv_coord_t) y1};
        lv_obj_t *l = lv_line_create(m_root);
        lv_line_set_points(l, p, 2);
        lv_obj_set_style_line_color(l, lv_color_hex(color), LV_PART_MAIN);
        lv_obj_set_style_line_width(l, width, LV_PART_MAIN);
        lv_obj_set_style_line_opa(l, opa, LV_PART_MAIN);
        lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
        return l;
    }

    //  Thin flat-color divider bar (same pattern as s14-hashrate's baseline
    //  rule) -- simpler and cheaper than an lv_line for a strip that is
    //  never repositioned.
    void addDivider(int x, int y, int w)
    {
        lv_obj_t *d = lv_obj_create(m_root);
        lv_obj_set_pos(d, x, y);
        lv_obj_set_size(d, w, 1);
        lv_obj_set_style_bg_color(d, lv_color_hex(E_INK), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(d, 56, LV_PART_MAIN); // ~.22, matches the approved design's.s06-div opacity
        lv_obj_set_style_border_width(d, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(d, 0, LV_PART_MAIN);
        lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE);
    }

    void buildDot(Dot &d, int x, int y, uint32_t color)
    {
        d.rect = lv_obj_create(m_root);
        lv_obj_set_pos(d.rect, x, y + 3);
        lv_obj_set_size(d.rect, 9, 9);
        lv_obj_set_style_bg_color(d.rect, lv_color_hex(color), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(d.rect, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(d.rect, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(d.rect, 2, LV_PART_MAIN);
        lv_obj_set_style_pad_all(d.rect, 0, LV_PART_MAIN);
        lv_obj_clear_flag(d.rect, LV_OBJ_FLAG_SCROLLABLE);
    }

    float mapX(float f) const
    {
        float c = nqfmt::clampf(f, m_fMin, m_fMax);
        float t = m_fMax > m_fMin ? (c - m_fMin) / (m_fMax - m_fMin) : 0.0f;
        return (float) PX0 + t * (float) (PX1 - PX0);
    }

    float mapY(float j) const
    {
        float c = nqfmt::clampf(j, m_yLo, m_yHi);
        float t = m_yHi > m_yLo ? (m_yHi - c) / (m_yHi - m_yLo) : 0.0f;
        return (float) PY0 + t * (float) (PY1 - PY0);
    }

    //  Builds the 6-point polyline + markers + axis scale labels exactly
    //  once (UiState::sweepMhz/sweepW is a fixed per-unit calibration
    //  table, never updated at runtime -- see ui_state.cpp's
    //  uiStateSetDefaults). Only the current-point marker and the two
    //  live numeric readouts change after this.
    void buildCurveOnce(const UiState &s)
    {
        float jth[N_PTS];
        m_fMin = s.sweepMhz[0];
        m_fMax = s.sweepMhz[0];
        float jMin = NAN, jMax = NAN;
        for (int i = 0; i < N_PTS; i++) {
            jth[i] = jthFor(s.sweepMhz[i], s.sweepW[i]);
            m_fMin = std::fmin(m_fMin, s.sweepMhz[i]);
            m_fMax = std::fmax(m_fMax, s.sweepMhz[i]);
            if (std::isnan(jMin) || jth[i] < jMin) {
                jMin = jth[i];
            }
            if (std::isnan(jMax) || jth[i] > jMax) {
                jMax = jth[i];
            }
        }
        float pad = (jMax - jMin) * 0.08f;
        if (!(pad > 0.0f)) {
            pad = 0.5f;
        }
        m_yLo = jMin - pad;
        m_yHi = jMax + pad;

        int sweetIdx = 0;
        for (int i = 1; i < N_PTS; i++) {
            if (jth[i] < jth[sweetIdx]) {
                sweetIdx = i;
            }
        }

        //  polyline (points stored in m_polyPts -- a member array, see
        //  addLine's comment on why lv_line needs storage that outlives
        //  the call that sets it)
        for (int i = 0; i < N_PTS; i++) {
            m_polyPts[i].x = (lv_coord_t) lroundf(mapX(s.sweepMhz[i]));
            m_polyPts[i].y = (lv_coord_t) lroundf(mapY(jth[i]));
        }
        lv_obj_t *poly = lv_line_create(m_root);
        lv_line_set_points(poly, m_polyPts, N_PTS);
        lv_obj_set_style_line_color(poly, lv_color_hex(E_INK), LV_PART_MAIN);
        lv_obj_set_style_line_width(poly, 2, LV_PART_MAIN);
        lv_obj_set_style_line_rounded(poly, true, LV_PART_MAIN);
        lv_obj_clear_flag(poly, LV_OBJ_FLAG_CLICKABLE);

        //  6 point markers (sweet spot = green + ink border, size 8; the
        //  rest = plain ink squares, size 6)
        for (int i = 0; i < N_PTS; i++) {
            bool sweet = (i == sweetIdx);
            int sz = sweet ? 8 : 6;
            lv_obj_t *sq = lv_obj_create(m_root);
            lv_obj_set_size(sq, sz, sz);
            lv_obj_set_pos(sq, m_polyPts[i].x - sz / 2, m_polyPts[i].y - sz / 2);
            lv_obj_set_style_bg_color(sq, lv_color_hex(sweet ? E_GREEN : E_INK), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(sq, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_color(sq, lv_color_hex(E_INK), LV_PART_MAIN);
            lv_obj_set_style_border_width(sq, sweet ? 1 : 0, LV_PART_MAIN);
            lv_obj_set_style_radius(sq, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(sq, 0, LV_PART_MAIN);
            lv_obj_clear_flag(sq, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(sq, LV_OBJ_FLAG_CLICKABLE);
        }

        //  Current-point marker must render ON TOP of the curve/markers
        //  just built: move it to the end of m_root's child list (paint
        //  order == z-order in LVGL 8, see s01-painel's own note on this).
        lv_obj_move_foreground(m_curRing);
        lv_obj_move_foreground(m_curDot);

        //  axis scale labels (fixed once the sweep table is known)
        char buf[nqfmt::kNumBufMax];
        nqfmt::n1(buf, sizeof(buf), jMax);
        lv_label_set_text(m_axisTop, buf);
        nqfmt::n1(buf, sizeof(buf), jMin);
        lv_label_set_text(m_axisBot, buf);
        nqfmt::n0(buf, sizeof(buf), m_fMin);
        lv_label_set_text(m_axisFMin, buf);
        nqfmt::n0(buf, sizeof(buf), m_fMax);
        lv_label_set_text(m_axisFMax, buf);
        char midBuf[64];
        nqfmt::n0(buf, sizeof(buf), (m_fMin + m_fMax) / 2.0f);
        snprintf(midBuf, sizeof(midBuf), "%s MHz", buf);
        lv_label_set_text(m_axisFMid, midBuf);

        //  "ponto ideal" (sweet spot): fixed calibration data, always
        //  known -- but nqfmt::n0/n1 already degrade to "--" on NAN for
        //  free, so no extra guard is needed here.
        nqfmt::n0(buf, sizeof(buf), s.sweepMhz[sweetIdx]);
        lv_label_set_text(m_valIdeal, buf);
        positionUnitAfterNumber(m_valIdeal, m_valIdealUnit, 6);
        char subBuf[64];
        nqfmt::n1(buf, sizeof(buf), jth[sweetIdx]);
        //  "\xc2\xb7" (U+00B7 middle dot) is not in the Archivo FULL glyph
        //  set generated for this project (see tools/fonts/generate-
        //  fonts.ps1's $FULL range) -- an ASCII hyphen stands in for the
        //  the approved design's "·" separator, which also happens to match
        //  CONTRATO.md's own style guidance (it warns against the
        //  "word — fragment" typographic tic elsewhere).
        snprintf(subBuf, sizeof(subBuf), "%s J/TH - minimum", buf);
        lv_label_set_text(m_subIdeal, subBuf);

        //  trade-off sentence: last two sweep points, exactly as measured.
        if (N_PTS >= 2) {
            float fa = s.sweepMhz[N_PTS - 2], fb = s.sweepMhz[N_PTS - 1];
            float dW = s.sweepW[N_PTS - 1] - s.sweepW[N_PTS - 2];
            float thA = fa * 2040.0f * 4.0f / 1.0e6f, thB = fb * 2040.0f * 4.0f / 1.0e6f;
            float dTh = thB - thA;
            char dMhzBuf[nqfmt::kNumBufMax], faBuf[nqfmt::kNumBufMax], fbBuf[nqfmt::kNumBufMax], dwBuf[nqfmt::kNumBufMax],
                dthBuf[nqfmt::kNumBufMax], line[320];
            nqfmt::n0(dMhzBuf, sizeof(dMhzBuf), fb - fa);
            nqfmt::n0(faBuf, sizeof(faBuf), fa);
            nqfmt::n0(fbBuf, sizeof(fbBuf), fb);
            nqfmt::n1(dwBuf, sizeof(dwBuf), dW);
            nqfmt::n2(dthBuf, sizeof(dthBuf), dTh);
            //  "->" replaces the approved design's "→" (U+2192), also outside the
            //  Archivo FULL glyph set generated for this project.
            //
            //  Explicit "\n" before "para entregar" instead of relying only
            //  on LV_LABEL_LONG_WRAP's automatic break: lv_conf.h (out of
            //  this task's scope) sets LV_TXT_BREAK_CHARS to include ','
            //  project-wide, so when this sentence's automatic wrap point
            //  lands close to the decimal number at the end ("0,41 TH/s"),
            //  LVGL's scan-backwards-for-a-break-char logic can pick that
            //  comma as the line break -- splitting "0," from "41" (see
            //  a later correction, with
            //  the exact PNG crop showing this). This manual break sits
            //  between two whole clauses, never adjacent to a formatted
            //  number ("50 MHz", "(750->800)", "13,6 W" all stay on line 1;
            //  "0,41 TH/s a mais." stays whole on line 2), so neither half
            //  can be split mid-number regardless of where the auto-wrap
            //  (still LV_LABEL_LONG_WRAP, kept as a safety net for an
            //  unusually wide line) would have landed. Verified against all
            //  4 simulator scenarios (normal/emergencia/wifi-caido/nan) to
            //  stay under the label's 456 px width on both resulting lines.
            snprintf(line, sizeof(line), "The last %s MHz (%s->%s) cost %s W more\nto deliver only %s TH/s more.", dMhzBuf, faBuf,
                     fbBuf, dwBuf, dthBuf);
            lv_label_set_text(m_tradeoff, line);
        }

        m_curveBuilt = true;
    }

    lv_obj_t *m_root = nullptr;
    lv_obj_t *m_axisTag = nullptr;
    lv_obj_t *m_axisTop = nullptr, *m_axisBot = nullptr;
    lv_obj_t *m_axisFMin = nullptr, *m_axisFMid = nullptr, *m_axisFMax = nullptr;
    lv_obj_t *m_curRing = nullptr, *m_curDot = nullptr;
    Dot m_dotNow, m_dotIdeal;
    lv_obj_t *m_valNow = nullptr, *m_valNowUnit = nullptr;
    lv_obj_t *m_valIdeal = nullptr, *m_valIdealUnit = nullptr, *m_subIdeal = nullptr;
    lv_obj_t *m_costPrefix = nullptr, *m_valCost = nullptr, *m_subCost = nullptr;
    lv_obj_t *m_tradeoff = nullptr;

    bool m_curveBuilt = false;
    float m_fMin = 0.0f, m_fMax = 0.0f, m_yLo = 0.0f, m_yHi = 0.0f;
    lv_point_t m_polyPts[N_PTS] = {};
    lv_point_t m_gridPts[GRID_LINE_MAX][2] = {};
    int m_gridUsed = 0;
};

S06EficienciaScreen s_instance;

} // namespace

Screen *screenGetS06Eficiencia()
{
    return &s_instance;
}
