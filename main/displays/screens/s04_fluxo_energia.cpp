// Ported from the approved design to LVGL 8.3 (DISPLAY_
// PROFILE_YYSLUPING_480X320). "Fluxo de energia": a floor-plan-style wiring
// diagram of the power path, from the wall plug through the PSU and the
// regulator to the 4 ASICs, plus the heat and fan branches -- line
// thickness proportional to watts, and a pulsing glow standing in for the
// approved design's animated dashed "current flowing" line.
//
// Before this pass: a skeleton
// with a single centered placeholder label, no diagram, no data.
//
// After: the approved design's own petrol-blue schematic palette (#0a2540 bg,
// #eef4fb wire white, #ffb020 amber current, #ff8a1e warn, #ff5252 critical),
// its wiring diagram (9 structural + 9 "current" line pairs, a wall-plug icon,
// a fan icon, a heat-wave icon), the PSU-load hero readout, the PSU capacity
// bar, the regulator/chip boxes and the footer (Vin/Iout/efficiency). In
// particular, the dropped animated stroke-dashoffset (LVGL 8 has no such
// property) replaced by a single lv_anim driving a staggered opacity pulse on
// the 9 "current" lines, and the dropped faint background grid (near-invisible
// at .05 alpha on RGB565, not worth ~20 extra objects).

#include "../fonts/fonts.h"
#include "../ui_state.h"
#include "nq_fmt.h"
#include "screen.h"
#include "screen_common.h"

#include <cmath>
#include <cstdio>

// New font for this screen : IBM Plex Mono Bold, 40 px, digits + ",.-%°" only
// (~19 KB), used for the single "PSU load" percentage readout -- the one place
// on this screen that reads as a tabular-numeric instrument gauge. Declared
// here (not in./fonts/fonts.h) per this task's rule that new screens declare
// their own new fonts locally.
LV_FONT_DECLARE(font_s04_mono_40);

namespace
{

// ---- 04-fluxo-energia's own palette (the approved design) --
constexpr uint32_t F_BG = 0x0a2540;
constexpr uint32_t F_PANEL = 0x123a5e;
constexpr uint32_t F_WIRE = 0xeef4fb;
constexpr uint32_t F_MUTED = 0x9fc4e8;
constexpr uint32_t F_CURRENT = 0xffb020;
constexpr uint32_t F_WARN = 0xff8a1e;
constexpr uint32_t F_CRIT = 0xff5252;
// Neutral (non-warn/crit) hero/fonte border color: WIRE at ~40% opacity,
// reproduced with LV_OPA_40 on a WIRE-colored border rather than a
// pre-blended hex constant, so it still reads correctly against the panel
// background exactly like the approved design's rgba(238,244,251,.4).
constexpr lv_opa_t F_BORDER_NEUTRAL_OPA = LV_OPA_40;

// ---- schematic geometry (the approved design's own
// key points, copied verbatim so every wire lands exactly where the approved design
// puts it) ------------------------------------------------------------------
constexpr int TRUNK_Y = 190;
constexpr int TOMADA_X1 = 27, FONTE_X0 = 90, FONTE_X1 = 190, REG_X0 = 250, REG_X1 = 330;
constexpr int JUNC_X = 360;
constexpr int CHIP_X0 = 406;
constexpr int CHIP_CY[4] = {104, 156, 208, 260};
constexpr int FAN_X = 290, FAN_ICON_CY = 50, FAN_WIRE_Y1 = 64, REG_TOP_Y = 160;
constexpr int HEAT_X = 290, HEAT_WIRE_Y1 = 220, HEAT_WIRE_Y2 = 234, HEAT_ICON_Y0 = 234, HEAT_ICON_Y1 = 258;

// One branch = one structural (thin, always F_WIRE) line + one "current"
// (thicker, colored, width/opacity driven by live watts) line drawn right
// on top of it, both sharing the same two endpoints. Order matches the
// the approved design's branches[] array (tomada->fonte, fonte->reg, reg->junc, 4x
// junc->chip, heat, fan) so BRANCH_* indices below line up 1:1 with it.
constexpr int BRANCH_COUNT = 9;
enum BranchIdx
{
    BR_TRUNK_A = 0,
    BR_TRUNK_B = 1,
    BR_STEM = 2,
    BR_DIAG0 = 3,
    BR_HEAT = 7,
    BR_FAN = 8
};

void branchEndpoints(int i, lv_point_t &p0, lv_point_t &p1)
{
    switch (i) {
    case BR_TRUNK_A:
        p0 = {TOMADA_X1, TRUNK_Y};
        p1 = {FONTE_X0, TRUNK_Y};
        return;
    case BR_TRUNK_B:
        p0 = {FONTE_X1, TRUNK_Y};
        p1 = {REG_X0, TRUNK_Y};
        return;
    case BR_STEM:
        p0 = {REG_X1, TRUNK_Y};
        p1 = {JUNC_X, TRUNK_Y};
        return;
    case BR_HEAT:
        p0 = {HEAT_X, HEAT_WIRE_Y1};
        p1 = {HEAT_X, HEAT_WIRE_Y2};
        return;
    case BR_FAN:
        p0 = {FAN_X, REG_TOP_Y};
        p1 = {FAN_X, FAN_WIRE_Y1};
        return;
    default: // BR_DIAG0..BR_DIAG0+3
        p0 = {JUNC_X, TRUNK_Y};
        p1 = {CHIP_X0, (lv_coord_t) CHIP_CY[i - BR_DIAG0]};
        return;
    }
}

// the approved design reference's widthFor(w) = NQ.clamp(3 + w*0.28, 3, 44). NaN-safe for free:
// nqfmt::clampf treats NaN as "use the low end", so an unread watt value
// degrades to the minimum 3 px line rather than inventing a thickness --
// though update additionally hides the whole "current" line when its
// source value is unread (see below), so this fallback is a belt-and-braces
// default, not the primary NaN guard.
float widthFor(float w)
{
    return nqfmt::clampf(3.0f + w * 0.28f, 3.0f, 44.0f);
}

lv_obj_t *makeFlatBox(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg, uint32_t borderColor, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(o, lv_color_hex(borderColor), LV_PART_MAIN);
    lv_obj_set_style_border_width(o, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(o, radius, LV_PART_MAIN);
    lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

lv_obj_t *makeLine(lv_obj_t *parent, lv_point_t *pts, uint32_t color, int width)
{
    lv_obj_t *l = lv_line_create(parent);
    lv_line_set_points(l, pts, 2);
    lv_obj_set_style_line_color(l, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_line_width(l, width, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(l, true, LV_PART_MAIN);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

class S04FluxoEnergiaScreen : public Screen {
  public:
    int number() const override
    {
        return 4;
    }
    const char *name() const override
    {
        return "fluxo_energia";
    }

    void create(lv_obj_t *parent) override
    {
        if (m_root) {
            destroy();
        }
        m_root = screenMakeRoot(parent, F_BG);

        // Deliberately no background grid: the approved design's is a 40x40 px
        // linear-gradient texture at 5% alpha (display-reference designs'
        // own.s04-grid), which RGB565 banding plus this screen's small font sizes
        // would render as visual noise rather than a readable grid -- and CONTRATO.md
        // itself warns against subtle gradients on this panel. Dropped rather than
        // approximated with ~20 extra faint lv_line objects for a barely-visible
        // effect.

        //  ---- wall plug icon --------------------------------------------
        //  Backing storage for the 2 pin lines MUST be a class member, not a
        //  local/stack array: lv_line_set_points (called inside makeLine)
        //  only stores the POINTER it is given, never a copy. A local array
        //  here is out of scope (and its stack slot free to be reused by any
        //  later call) the moment create returns, so LVGL would read back
        //  garbage coordinates on the next redraw -- this was exactly the
        //  cause of a stray diagonal line rendered near the screen's top-left
        //  corner. m_structPts/m_curPts/m_heatPts/m_fanBladePts below already
        //  followed this rule; only these 2 pin lines did not.
        makeFlatBox(m_root, 13, 178, 14, 24, F_PANEL, F_WIRE, 2);
        m_plugPts[0][0] = {18, 182};
        m_plugPts[0][1] = {18, 192};
        makeLine(m_root, m_plugPts[0], F_WIRE, 2);
        m_plugPts[1][0] = {23, 182};
        m_plugPts[1][1] = {23, 192};
        makeLine(m_root, m_plugPts[1], F_WIRE, 2);

        //  ---- 9 structural + 9 "current" branch lines ---------------------
        for (int i = 0; i < BRANCH_COUNT; i++) {
            lv_point_t p0, p1;
            branchEndpoints(i, p0, p1);
            m_structPts[i][0] = p0;
            m_structPts[i][1] = p1;
            m_curPts[i][0] = p0;
            m_curPts[i][1] = p1;
            m_structLine[i] = makeLine(m_root, m_structPts[i], F_WIRE, 2);
            m_curLine[i] = makeLine(m_root, m_curPts[i], F_CURRENT, 4);
        }

        //  ---- heat icon: 3 upward zigzags (heat waves) ---------------------
        const int hx[3] = {280, 290, 300};
        for (int i = 0; i < 3; i++) {
            lv_point_t *pts = m_heatPts[i];
            pts[0] = {(lv_coord_t) (hx[i] - 4), HEAT_ICON_Y1};
            pts[1] = {(lv_coord_t) (hx[i] + 4), HEAT_ICON_Y0 + 12};
            pts[2] = {(lv_coord_t) (hx[i] - 4), HEAT_ICON_Y0};
            lv_obj_t *l = lv_line_create(m_root);
            lv_line_set_points(l, pts, 3);
            lv_obj_set_style_line_color(l, lv_color_hex(F_WIRE), LV_PART_MAIN);
            lv_obj_set_style_line_width(l, 2, LV_PART_MAIN);
            lv_obj_set_style_line_rounded(l, true, LV_PART_MAIN);
            lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
        }

        //  ---- fan icon: circle + 3 blades -----------------------------------
        lv_obj_t *fanBody = lv_obj_create(m_root);
        lv_obj_set_pos(fanBody, FAN_X - 14, FAN_ICON_CY - 14);
        lv_obj_set_size(fanBody, 28, 28);
        lv_obj_set_style_bg_color(fanBody, lv_color_hex(F_PANEL), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(fanBody, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(fanBody, lv_color_hex(F_WIRE), LV_PART_MAIN);
        lv_obj_set_style_border_width(fanBody, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(fanBody, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_pad_all(fanBody, 0, LV_PART_MAIN);
        lv_obj_clear_flag(fanBody, LV_OBJ_FLAG_SCROLLABLE);
        const float bladeDx[3] = {0.0f, 10.4f, -10.4f};
        const float bladeDy[3] = {-12.0f, 6.0f, 6.0f};
        for (int i = 0; i < 3; i++) {
            lv_point_t *pts = m_fanBladePts[i];
            pts[0] = {FAN_X, FAN_ICON_CY};
            pts[1] = {(lv_coord_t) (FAN_X + bladeDx[i]), (lv_coord_t) (FAN_ICON_CY + bladeDy[i])};
            lv_obj_t *l = lv_line_create(m_root);
            lv_line_set_points(l, pts, 2);
            lv_obj_set_style_line_color(l, lv_color_hex(F_WIRE), LV_PART_MAIN);
            lv_obj_set_style_line_width(l, 2, LV_PART_MAIN);
            lv_obj_set_style_line_rounded(l, true, LV_PART_MAIN);
            lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
        }

        // ---- hero: "PSU load" ---------------------------------------- Box height
        // (88 -> 96) and the 3 labels' y positions give each of the
        // title/number/subtitle a real vertical gap instead of relying on eyeballed y
        // offsets: font_archivo_regular_15's line_height is 16 px and
        // font_s04_mono_40's is 34 px (see fonts/font_*.c), so the title (y=8)
        // actually occupies y=8.24, and the previous y=21 for the number started 3 px
        // INSIDE that box, overlapping the title's own text -- worst with a 3-digit
        // reading like "106%" (confirmed by a later visual review and fixed
        // here). Still comfortably inside the box below (the next element in this column, "fonte",
        // only starts at y=150, i.e. absolute y=12+96=108 leaves a 42 px gap -- no
        // collision risk from the 8 px height increase).
        m_hero = makeFlatBox(m_root, 12, 12, 140, 96, F_BG, F_WIRE, 4);
        lv_obj_set_style_bg_opa(m_hero, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_opa(m_hero, F_BORDER_NEUTRAL_OPA, LV_PART_MAIN);
        screenMakeLabel(m_hero, "PSU load", 10, 8, 120, &font_archivo_regular_15, F_MUTED);
        m_heroNum = screenMakeLabel(m_hero, "--", 8, 30, 126, &font_s04_mono_40, F_WIRE);
        m_heroSub = screenMakeLabel(m_hero, "--", 10, 74, 122, &font_archivo_regular_15, F_MUTED);

        //  ---- fonte (PSU) box: capacity bar ----------------------------------
        m_fonte = makeFlatBox(m_root, FONTE_X0, 150, FONTE_X1 - FONTE_X0, 80, F_PANEL, F_WIRE, 3);
        screenMakeLabel(m_fonte, "PSU", 10, 8, 80, &font_archivo_regular_15, F_WIRE);
        m_track = makeFlatBox(m_fonte, 10, 36, 80, 8, 0x1a4570, F_WIRE, 2);
        m_fill = lv_obj_create(m_track);
        lv_obj_set_pos(m_fill, 0, 0);
        lv_obj_set_size(m_fill, 0, 8);
        lv_obj_set_style_bg_color(m_fill, lv_color_hex(F_CURRENT), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(m_fill, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(m_fill, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(m_fill, 2, LV_PART_MAIN);
        lv_obj_set_style_pad_all(m_fill, 0, LV_PART_MAIN);
        lv_obj_clear_flag(m_fill, LV_OBJ_FLAG_SCROLLABLE);
        m_fonteCap = screenMakeLabel(m_fonte, "--", 10, 53, 80, &font_archivo_regular_15, F_MUTED);

        //  ---- regulador box ---------------------------------------------------
        lv_obj_t *reg = makeFlatBox(m_root, REG_X0, 160, REG_X1 - REG_X0, 60, F_PANEL, F_WIRE, 3);
        screenMakeLabel(reg, "regulator", 2, 22, REG_X1 - REG_X0 - 4, &font_archivo_regular_15, F_WIRE, LV_TEXT_ALIGN_CENTER);

        //  ---- fan / heat tags -------------------------------------------------
        m_fanTag = screenMakeLabel(m_root, "", FAN_X - 70, 14, 140, &font_archivo_regular_15, F_MUTED, LV_TEXT_ALIGN_CENTER);
        lv_label_set_recolor(m_fanTag, true);
        m_heatTag = screenMakeLabel(m_root, "", HEAT_X - 65, 264, 130, &font_archivo_regular_15, F_MUTED, LV_TEXT_ALIGN_CENTER);
        lv_label_set_recolor(m_heatTag, true);

        //  ---- "4 chips" aggregate -----------------------------------------
        screenMakeLabel(m_root, "4 chips", 394, 14, 74, &font_archivo_regular_15, F_WIRE, LV_TEXT_ALIGN_CENTER);
        m_aggL2 = screenMakeLabel(m_root, "--", 394, 30, 74, &font_archivo_regular_15, F_MUTED, LV_TEXT_ALIGN_CENTER);

        //  ---- 4 chip boxes ------------------------------------------------
        static const char *kChipNames[4] = {"chip 1", "chip 2", "chip 3", "chip 4"};
        for (int i = 0; i < 4; i++) {
            lv_obj_t *b = makeFlatBox(m_root, CHIP_X0, CHIP_CY[i] - 18, 58, 36, F_PANEL, F_WIRE, 3);
            screenMakeLabel(b, kChipNames[i], 0, 11, 58, &font_archivo_regular_15, F_MUTED, LV_TEXT_ALIGN_CENTER);
        }

        //  ---- footer: Vin / Iout / eficiência -------------------------------
        m_footVin = screenMakeLabel(m_root, "", 12, 292, 150, &font_archivo_regular_15, F_MUTED);
        lv_label_set_recolor(m_footVin, true);
        m_footIout = screenMakeLabel(m_root, "", 165, 292, 150, &font_archivo_regular_15, F_MUTED, LV_TEXT_ALIGN_CENTER);
        lv_label_set_recolor(m_footIout, true);
        m_footEff = screenMakeLabel(m_root, "", 318, 292, 150, &font_archivo_regular_15, F_MUTED, LV_TEXT_ALIGN_RIGHT);
        lv_label_set_recolor(m_footEff, true);

        //  ---- pulse animation: a single lv_anim drives a staggered
        //  opacity "breathing" on the 9 current-carrying lines, standing in
        //  for the approved design's stroke-dashoffset scroll (not reproducible in
        //  LVGL 8 -- see the note above buildChart-equivalent comment at
        //  the top of this file and CONTRATO.md's own lvgl.notas for this
        //  screen). ONE anim object, created here and removed in destroy,
        //  never leaked across screen switches.
        lv_anim_init(&m_pulseAnim);
        lv_anim_set_var(&m_pulseAnim, this);
        lv_anim_set_exec_cb(&m_pulseAnim, pulseExecCb);
        lv_anim_set_values(&m_pulseAnim, 0, 1000);
        lv_anim_set_time(&m_pulseAnim, 1400);
        lv_anim_set_repeat_count(&m_pulseAnim, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&m_pulseAnim, lv_anim_path_linear);
        lv_anim_start(&m_pulseAnim);
    }

    void update(const UiState &s) override
    {
        if (!m_root) {
            return;
        }

        bool loadValid = !std::isnan(s.psuLoad);
        float psuLoad = loadValid ? s.psuLoad : 0.0f;

        //  stage: 0 ok, 1 warn (>=90%), 2 crit (>=100%, shown in red per task spec)
        int stage = 0;
        if (loadValid) {
            if (psuLoad >= 1.0f) {
                stage = 2;
            } else if (psuLoad >= 0.9f) {
                stage = 1;
            }
        }
        uint32_t heroCol = stage == 2 ? F_CRIT : stage == 1 ? F_WARN : F_WIRE;
        uint32_t fillCol = stage == 2 ? F_CRIT : stage == 1 ? F_WARN : F_CURRENT;
        uint32_t lineCol = stage == 2 ? F_CRIT : F_CURRENT; // trunk current lines: only 2-state
        uint32_t borderCol = stage == 2 ? F_CRIT : stage == 1 ? F_WARN : F_WIRE;

        //  hero: carga da fonte
        if (!loadValid) {
            lv_label_set_text(m_heroNum, "--");
        } else {
            char pctBuf[16], line[24];
            nqfmt::n0(pctBuf, sizeof(pctBuf), psuLoad * 100.0f);
            snprintf(line, sizeof(line), "%s%%", pctBuf);
            lv_label_set_text(m_heroNum, line);
        }
        lv_obj_set_style_text_color(m_heroNum, lv_color_hex(heroCol), LV_PART_MAIN);

        char pinBuf[16], psuWBuf[16], subLine[48];
        nqfmt::n0(pinBuf, sizeof(pinBuf), s.pin);
        nqfmt::n0(psuWBuf, sizeof(psuWBuf), s.psuW);
        snprintf(subLine, sizeof(subLine), "%s of %s W", pinBuf, psuWBuf);
        lv_label_set_text(m_heroSub, subLine);
        lv_obj_set_style_text_color(m_heroSub, lv_color_hex(stage == 0 ? F_MUTED : heroCol), LV_PART_MAIN);

        lv_obj_set_style_border_color(m_hero, lv_color_hex(borderCol), LV_PART_MAIN);
        lv_obj_set_style_border_opa(m_hero, stage == 0 ? F_BORDER_NEUTRAL_OPA : (lv_opa_t) LV_OPA_COVER, LV_PART_MAIN);

        //  fonte: capacity bar (clampf(NaN,0,1) -> 0, so an unread pin just
        //  shows an empty bar rather than a guessed fill)
        float frac = nqfmt::clampf(psuLoad, 0.0f, 1.0f);
        lv_coord_t trackW = 80; // matches m_track's own width (see create)
        lv_obj_set_width(m_fill, (lv_coord_t) (frac * trackW));
        lv_obj_set_style_bg_color(m_fill, lv_color_hex(fillCol), LV_PART_MAIN);
        char capBuf[16], capLine[32];
        nqfmt::n0(capBuf, sizeof(capBuf), s.psuW);
        snprintf(capLine, sizeof(capLine), "%s W max.", capBuf);
        lv_label_set_text(m_fonteCap, capLine);
        lv_obj_set_style_border_color(m_fonte, lv_color_hex(borderCol), LV_PART_MAIN);
        lv_obj_set_style_border_opa(m_fonte, stage == 0 ? F_BORDER_NEUTRAL_OPA : (lv_opa_t) LV_OPA_COVER, LV_PART_MAIN);

        //  trunk (tomada->fonte->regulador): thickness + color by pin
        bool pinValid = !std::isnan(s.pin);
        setBranch(BR_TRUNK_A, s.pin, pinValid, lineCol);
        setBranch(BR_TRUNK_B, s.pin, pinValid, lineCol);

        //  stem (regulator->junction): thickness by pout
        bool poutValid = !std::isnan(s.pout);
        setBranch(BR_STEM, s.pout, poutValid, F_CURRENT);

        //  4 rays to the chips: visual approximation = pout/4 each (no
        //  per-chip watt field in UiState -- see the header comment at the
        //  top of the approved design, ported
        //  verbatim: this is the approved design's own approximation, not one added
        //  here).
        float perChip = poutValid ? s.pout / 4.0f : NAN;
        for (int i = 0; i < 4; i++) {
            setBranch(BR_DIAG0 + i, perChip, poutValid, F_CURRENT);
        }
        char perChipBuf[16], aggLine[32];
        nqfmt::n0(perChipBuf, sizeof(perChipBuf), perChip);
        snprintf(aggLine, sizeof(aggLine), "~%s W each", perChipBuf);
        lv_label_set_text(m_aggL2, aggLine);

        //  heat branch
        bool heatValid = !std::isnan(s.vrLoss);
        setBranch(BR_HEAT, s.vrLoss, heatValid, F_CURRENT);
        char heatBuf[16], heatLine[48];
        nqfmt::n0(heatBuf, sizeof(heatBuf), s.vrLoss);
        snprintf(heatLine, sizeof(heatLine), "#eef4fb %s W# as heat", heatBuf);
        lv_label_set_text(m_heatTag, heatLine);

        //  fan branch
        bool fanValid = !std::isnan(s.fanW);
        setBranch(BR_FAN, s.fanW, fanValid, F_CURRENT);
        char fanBuf[16], fanLine[48];
        nqfmt::n0(fanBuf, sizeof(fanBuf), s.fanW);
        snprintf(fanLine, sizeof(fanLine), "#eef4fb %s W# to the fan", fanBuf);
        lv_label_set_text(m_fanTag, fanLine);

        //  footer: Vin / Iout / efficiency
        char vinBuf[16], vinLine[48];
        nqfmt::n1(vinBuf, sizeof(vinBuf), s.vin);
        snprintf(vinLine, sizeof(vinLine), "input #eef4fb %s V#", vinBuf);
        lv_label_set_text(m_footVin, vinLine);

        char ioutBuf[16], ioutLine[48];
        nqfmt::n1(ioutBuf, sizeof(ioutBuf), s.iout);
        snprintf(ioutLine, sizeof(ioutLine), "output #eef4fb %s A#", ioutBuf);
        lv_label_set_text(m_footIout, ioutLine);

        char effBuf[16], effLine[48];
        nqfmt::n1(effBuf, sizeof(effBuf), s.effJth);
        snprintf(effLine, sizeof(effLine), "#eef4fb %s J/TH#", effBuf);
        lv_label_set_text(m_footEff, effLine);
    }

    void destroy() override
    {
        if (m_root) {
            lv_anim_del(this, pulseExecCb);
            lv_obj_del(m_root);
            m_root = nullptr;
            m_hero = m_heroNum = m_heroSub = nullptr;
            m_fonte = m_track = m_fill = m_fonteCap = nullptr;
            m_fanTag = m_heatTag = m_aggL2 = nullptr;
            m_footVin = m_footIout = m_footEff = nullptr;
            for (int i = 0; i < BRANCH_COUNT; i++) {
                m_structLine[i] = nullptr;
                m_curLine[i] = nullptr;
                m_branchHidden[i] = false;
            }
        }
    }

  private:
    //  Sets one branch's "current" line width/color/visibility for the
    //  current tick. `valid` false means the source UiState field is NaN
    //  (not yet available): the line is hidden outright rather than drawn
    //  at a guessed thickness, so this screen never implies current is
    //  flowing through a reading it does not actually have.
    void setBranch(int i, float watts, bool valid, uint32_t color)
    {
        m_branchHidden[i] = !valid;
        if (!valid) {
            lv_obj_add_flag(m_curLine[i], LV_OBJ_FLAG_HIDDEN);
            return;
        }
        lv_obj_clear_flag(m_curLine[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_line_width(m_curLine[i], (lv_coord_t) lroundf(widthFor(watts)), LV_PART_MAIN);
        lv_obj_set_style_line_color(m_curLine[i], lv_color_hex(color), LV_PART_MAIN);
    }

    //  exec_cb for the single pulse animation: staggers a sine-wave opacity
    //  "breath" across the 9 current lines by branch index, so the glow
    //  visually travels outward from the plug towards the chips/heat/fan
    //  instead of every branch pulsing in lockstep -- a cheap stand-in for
    //  the approved design's moving dashes using exactly one lv_anim.
    static void pulseExecCb(void *var, int32_t v)
    {
        auto *self = static_cast<S04FluxoEnergiaScreen *>(var);
        constexpr float kTwoPi = 6.28318530718f;
        float phase = (float) v / 1000.0f * kTwoPi;
        for (int i = 0; i < BRANCH_COUNT; i++) {
            if (self->m_branchHidden[i] || !self->m_curLine[i]) {
                continue;
            }
            float stagger = (float) i * (kTwoPi / (float) BRANCH_COUNT);
            float s = sinf(phase + stagger);
            lv_opa_t opa = (lv_opa_t) (180.0f + 75.0f * s); // breathes ~105..255
            lv_obj_set_style_line_opa(self->m_curLine[i], opa, LV_PART_MAIN);
        }
    }

    lv_obj_t *m_root = nullptr;
    lv_obj_t *m_hero = nullptr, *m_heroNum = nullptr, *m_heroSub = nullptr;
    lv_obj_t *m_fonte = nullptr, *m_track = nullptr, *m_fill = nullptr, *m_fonteCap = nullptr;
    lv_obj_t *m_fanTag = nullptr, *m_heatTag = nullptr, *m_aggL2 = nullptr;
    lv_obj_t *m_footVin = nullptr, *m_footIout = nullptr, *m_footEff = nullptr;

    lv_point_t m_structPts[BRANCH_COUNT][2] = {};
    lv_point_t m_curPts[BRANCH_COUNT][2] = {};
    lv_obj_t *m_structLine[BRANCH_COUNT] = {};
    lv_obj_t *m_curLine[BRANCH_COUNT] = {};
    bool m_branchHidden[BRANCH_COUNT] = {};

    lv_point_t m_heatPts[3][3] = {};
    lv_point_t m_fanBladePts[3][2] = {};
    lv_point_t m_plugPts[2][2] = {}; // backing storage for the wall-plug's 2 pin lines (see create)

    lv_anim_t m_pulseAnim = {};
};

S04FluxoEnergiaScreen s_instance;

} // namespace

Screen *screenGetS04FluxoEnergia()
{
    return &s_instance;
}
