// Ported from the approved design to LVGL 8.3 (DISPLAY_PROFILE_
// YYSLUPING_480X320). "Painel": cockpit-style main dashboard -- a big
// hashrate hero on the left, and two vertical PFD-style "tapes" (frequency
// MHz, regulator temperature degC) on the right that scroll behind a fixed
// central reading window, with the governor's thermal limits marked
// directly on the temperature tape.
//
// Before this pass: a skeleton with 3
// plain labels in a single generic panel -- no tapes, no hero typography,
// generic BIG_* palette.
//
// After: the approved design's own cockpit palette (petrol blue / ivory /
// amber / danger red / steel) and its own type (Saira Condensed hero + tape
// readouts, Archivo labels), the two scrolling tapes (fixed pool of reused
// lv_obj/lv_label, repositioned every update -- no lv_anim, per the task's
// "2-4 Hz update is enough" guidance), the 3 colored limit marks on the
// temperature tape, and the footer (power / efficiency / governor state).

#include "../fonts/fonts.h"
#include "../ui_state.h"
#include "nq_fmt.h"
#include "screen.h"
#include "screen_common.h"

#include <cmath>
#include <cstdio>

namespace
{

// ---- 01-painel's own palette (the approved design) ------
constexpr uint32_t P_BG = 0x0a1f2c;
constexpr uint32_t P_PANEL = 0x123244;
constexpr uint32_t P_PANEL_EDGE = 0x1c4258;
constexpr uint32_t P_SPINE = 0x2c4a58;
constexpr uint32_t P_IVORY = 0xf4ecd8;
constexpr uint32_t P_AMBER = 0xe0a542;
constexpr uint32_t P_DANGER = 0xcf4a3e;
constexpr uint32_t P_STEEL = 0x93a9ae;

// ---- tape geometry (shared by both tapes; values from the approved design's CSS) --
constexpr int VIEW_W = 100;
constexpr int VIEW_H = 216;
constexpr float VIEW_C = VIEW_H / 2.0f; // 108: vertical center of the tape window
constexpr int TAPE_TOP = 12;
constexpr int VIEW_Y = TAPE_TOP + 28; // 40
constexpr int MHZ_TAPE_X = 256;
constexpr int VR_TAPE_X = 368;
constexpr float MHZ_SCALE = 1.44f; // px per MHz (half-window = 75 MHz)
constexpr float VR_SCALE = 4.32f;  // px per degC (half-window = 25 degC)

// Neutral rest position for each tape when its live value is NAN: the
// midpoint of the tape's own fixed tick range, NOT a config fallback
// (fmin/vrTarget) that happens to look like a plausible live reading.
// Used only to park the ruler/ticks at a fixed "home" position -- the
// readout window itself shows "--" in this case (see update), never a
// number, so this constant never reaches the screen as text.
constexpr float MHZ_NEUTRAL = 650.0f; // mid of MHZ_TICKS' 400..900 range
constexpr float VR_NEUTRAL = 70.0f;   // mid of VR_TICKS' 15..125 range

// Fixed reference scale for each tape's ruler (built once at create,
// positions recomputed every update -- the ruler's own numbers never
// change, only where they sit relative to the current value).
constexpr float MHZ_TICKS[] = {400, 450, 500, 550, 600, 650, 700, 750, 800, 850, 900};
constexpr int MHZ_TICK_COUNT = sizeof(MHZ_TICKS) / sizeof(MHZ_TICKS[0]);
constexpr float VR_TICKS[] = {15, 25, 35, 45, 55, 65, 75, 85, 95, 105, 115, 125};
constexpr int VR_TICK_COUNT = sizeof(VR_TICKS) / sizeof(VR_TICKS[0]);
constexpr int MAX_TICKS = 12; // pool size (fits both tapes; see nq_fmt-style
                              //   "reuse a fixed pool" note in class body)

const char *govWord(uint8_t st)
{
    switch (st) {
    case UI_GOV_STARTUP:
        return "Starting";
    case UI_GOV_RAMP_UP:
        return "Ramping up";
    case UI_GOV_HOLD:
        return "Holding";
    case UI_GOV_BACKOFF:
        return "Backing off";
    case UI_GOV_EMERGENCY:
        return "Emergency";
    case UI_GOV_SATURATED_LOW:
        return "At minimum";
    //   The approved design's simulator only ever produces the 6 states above; the
    //   real firmware's governor also has these 3 (see ui_state.h's
    //   UiGovState) with no the approved design wording to copy, so English text was
    //   added here to keep the rule "no invented English/blank state"
    //   honest for the real hardware.
    case UI_GOV_SENSOR_FAIL:
        return "Sensor fault";
    case UI_GOV_FROZEN:
        return "Frozen";
    case UI_GOV_OFF:
    default:
        return "Off";
    }
}

uint32_t govColor(uint8_t st)
{
    if (st == UI_GOV_EMERGENCY) {
        return P_DANGER;
    }
    if (st == UI_GOV_BACKOFF || st == UI_GOV_SATURATED_LOW) {
        return P_AMBER;
    }
    return P_IVORY;
}

uint32_t freqColor(uint8_t st)
{
    if (st == UI_GOV_EMERGENCY) {
        return P_DANGER;
    }
    if (st == UI_GOV_BACKOFF) {
        return P_AMBER;
    }
    return P_IVORY;
}

// NaN comparisons are always false in C++ (same as JS), so an unknown vr
// (NAN) falls through every `>=` check below and lands on the neutral
// P_IVORY color -- the same "don't invent a warning you can't back up"
// behavior nq_fmt.h documents for clampf/mapf, just relying on the
// language's own NaN semantics here instead of an explicit isnan guard.
uint32_t tempColor(float vr, float target, float hard, float cut, uint8_t st)
{
    if (st == UI_GOV_EMERGENCY || vr >= cut || vr >= hard) {
        return P_DANGER;
    }
    if (vr >= target) {
        return P_AMBER;
    }
    return P_IVORY;
}

// One vertical instrument tape: a clipped "view" box with a fixed pool of
// tick labels (repositioned every update, never recreated) and a central
// readout window whose border color reflects the current state.
struct Tape
{
    lv_obj_t *view = nullptr;
    lv_obj_t *readout = nullptr;
    lv_obj_t *readoutVal = nullptr;
    lv_obj_t *tick[MAX_TICKS] = {};
    const float *tickValues = nullptr;
    int tickCount = 0;
};

class S01PainelScreen : public Screen {
  public:
    int number() const override
    {
        return 1;
    }
    const char *name() const override
    {
        return "painel";
    }

    void create(lv_obj_t *parent) override
    {
        if (m_root) {
            destroy();
        }
        m_root = screenMakeRoot(parent, P_BG);

        //   ---- hashrate hero (left) ---------------------------------------
        //   Same simplification as 19-zen: the approved design's hero splits into a
        //   big integer part + a `<small>` (54px) decimal part; only ONE
        //   Saira Condensed Bold size was generated for this hero
        //   (font_saira_condensed_bold_108), so the whole "6,31" string
        //   renders at 108px in one label instead of shrinking the decimal.
        m_heroNum = screenMakeLabel(m_root, "--", 14, 36, 320, &font_saira_condensed_bold_108, P_IVORY);
        //   The approved design asks for 16px Archivo Regular; the generated cut is 15px
        //   (font_archivo_regular_15, the closest available -- see
        //   main/displays/fonts/fonts.h) -- 1px substitution, not a font
        //   this build doesn't have.
        screenMakeLabel(m_root, "terahashes per second", 16, 140, 220, &font_archivo_regular_15, P_STEEL);

        buildTape(m_mhz, MHZ_TAPE_X, "frequency", "MHz", MHZ_TICKS, MHZ_TICK_COUNT);
        buildTape(m_vr, VR_TAPE_X, "regulator",
                  "\xc2\xb0"
                  "C",
                  VR_TICKS, VR_TICK_COUNT);

        //   MHz tape's own extra: a ceiling ("bug") marker at freqCap. The
        //   the approved design draws a CSS border-triangle; CONTRATO.md's allowed LVGL
        //   primitives are flat fill / rounded rect / border / line / arc /
        //   bar / text -- no arbitrary triangle -- so this is a small filled
        //   rectangle instead, which is both simpler AND more contract-
        //   faithful than trying to fake a triangle with 3-point lv_line.
        m_mhzBug = lv_obj_create(m_mhz.view);
        lv_obj_set_size(m_mhzBug, 6, 10);
        lv_obj_set_style_bg_color(m_mhzBug, lv_color_hex(P_AMBER), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(m_mhzBug, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(m_mhzBug, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(m_mhzBug, 1, LV_PART_MAIN);
        lv_obj_set_style_pad_all(m_mhzBug, 0, LV_PART_MAIN);
        lv_obj_clear_flag(m_mhzBug, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(m_mhzBug, VIEW_W - 34 - 6, (int) (VIEW_C - 5.0f));

        m_mhzBugTag = screenMakeLabel(m_mhz.view, "", 50, (int) (VIEW_C - 8.0f), 46, &font_saira_condensed_semibold_15, P_AMBER,
                                      LV_TEXT_ALIGN_RIGHT);
        buildReadout(m_mhz, "MHz");

        //   VR tape's own extra: 3 colored limit marks (target / hard ceiling
        //   / firmware cutoff), each a thin bar + a value tag.
        buildMark(m_vrAlvo);
        buildMark(m_vrTeto);
        buildMark(m_vrCorte);
        buildReadout(m_vr, "\xc2\xb0"
                           "C");

        //   ---- footer: power / efficiency / governor state ------------------
        constexpr int LABEL_Y = 264, VALUE_Y = 283;
        screenMakeLabel(m_root, "power", 16, LABEL_Y, 140, &font_archivo_regular_15, P_STEEL);
        m_potVal = screenMakeLabel(m_root, "--", 16, VALUE_Y, 140, &font_saira_condensed_semibold_24, P_IVORY);

        screenMakeLabel(m_root, "efficiency", 180, LABEL_Y, 120, &font_archivo_regular_15, P_STEEL, LV_TEXT_ALIGN_CENTER);
        m_effVal =
            screenMakeLabel(m_root, "--", 180, VALUE_Y, 120, &font_saira_condensed_semibold_24, P_IVORY, LV_TEXT_ALIGN_CENTER);

        screenMakeLabel(m_root, "governor", 340, LABEL_Y, 124, &font_archivo_regular_15, P_STEEL, LV_TEXT_ALIGN_RIGHT);
        m_govVal =
            screenMakeLabel(m_root, "--", 340, VALUE_Y, 124, &font_saira_condensed_semibold_24, P_IVORY, LV_TEXT_ALIGN_RIGHT);
    }

    void update(const UiState &s) override
    {
        if (!m_root) {
            return;
        }

        //   hero: the approved design reference uses hashrate.m1 == UiState::hr1m
        if (std::isnan(s.hr1m)) {
            lv_label_set_text(m_heroNum, "--");
        } else {
            char buf[24];
            nqfmt::ths(buf, sizeof(buf), s.hr1m, 2);
            lv_label_set_text(m_heroNum, buf);
        }

        //   NAN in freq/vrTemp means "no reading yet" -- never invent one.
        //   The tape rests at a fixed neutral center (MHZ_NEUTRAL/VR_NEUTRAL,
        //   not a config fallback that happens to look like a plausible
        //   reading) so the ruler stays put instead of jumping to 0, but the
        //   readout window -- the part that claims to show a CURRENT value --
        //   shows "--" and drops back to the neutral steel color, and the
        //   MHz ceiling bug (which only makes sense relative to a real
        //   current position) is hidden entirely. This is the fix for the
        //   bug where the NAN scenario showed "500 MHz"/"78 degC" as if
        //   those were live readings (they were fmin/vrTarget, silently
        //   substituted).
        bool freqValid = !std::isnan(s.freq);
        bool vrValid = !std::isnan(s.vrTemp);
        float freq = freqValid ? s.freq : MHZ_NEUTRAL;
        float vr = vrValid ? s.vrTemp : VR_NEUTRAL;

        layTape(m_mhz, freq, MHZ_SCALE);
        if (freqValid) {
            char freqBuf[16];
            nqfmt::n0(freqBuf, sizeof(freqBuf), freq);
            lv_label_set_text(m_mhz.readoutVal, freqBuf);
            lv_obj_set_style_border_color(m_mhz.readout, lv_color_hex(freqColor(s.govState)), LV_PART_MAIN);
        } else {
            lv_label_set_text(m_mhz.readoutVal, "--");
            lv_obj_set_style_border_color(m_mhz.readout, lv_color_hex(P_STEEL), LV_PART_MAIN);
        }

        if (freqValid) {
            lv_obj_clear_flag(m_mhzBug, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(m_mhzBugTag, LV_OBJ_FLAG_HIDDEN);
            float bugY = nqfmt::clampf(VIEW_C + (freq - s.freqCap) * MHZ_SCALE, 6.0f, (float) VIEW_H - 6.0f);
            lv_obj_set_y(m_mhzBug, (lv_coord_t) (bugY - 5));
            lv_obj_set_y(m_mhzBugTag, (lv_coord_t) (bugY - 8));
            char capBuf[16];
            nqfmt::n0(capBuf, sizeof(capBuf), s.freqCap);
            lv_label_set_text(m_mhzBugTag, capBuf);
        } else {
            lv_obj_add_flag(m_mhzBug, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(m_mhzBugTag, LV_OBJ_FLAG_HIDDEN);
        }

        layTape(m_vr, vr, VR_SCALE);
        if (vrValid) {
            char vrBuf[16];
            nqfmt::n0(vrBuf, sizeof(vrBuf), vr);
            lv_label_set_text(m_vr.readoutVal, vrBuf);
            uint32_t vrColor = tempColor(vr, s.vrTarget, s.vrHard, s.vrCut, s.govState);
            lv_obj_set_style_border_color(m_vr.readout, lv_color_hex(vrColor), LV_PART_MAIN);
        } else {
            lv_label_set_text(m_vr.readoutVal, "--");
            lv_obj_set_style_border_color(m_vr.readout, lv_color_hex(P_STEEL), LV_PART_MAIN);
        }

        //   Target/hard/cut marks are fixed config thresholds (not live
        //   readings), so they stay visible either way -- this is exactly
        //   the "background scale" the task allows tapes to keep showing.
        //   When vr is unknown they are simply drawn relative to the
        //   neutral center instead of a real temperature.
        layMark(m_vrAlvo, vr, s.vrTarget, P_STEEL);
        layMark(m_vrTeto, vr, s.vrHard, P_AMBER);
        layMark(m_vrCorte, vr, s.vrCut, P_DANGER);

        //   footer
        char potBuf[16];
        nqfmt::n0(potBuf, sizeof(potBuf), s.pin);
        char potLine[24];
        snprintf(potLine, sizeof(potLine), "%s W", potBuf);
        lv_label_set_text(m_potVal, potLine);

        char effBuf[16];
        nqfmt::n1(effBuf, sizeof(effBuf), s.effJth);
        char effLine[24];
        snprintf(effLine, sizeof(effLine), "%s J/TH", effBuf);
        lv_label_set_text(m_effVal, effLine);

        lv_label_set_text(m_govVal, govWord(s.govState));
        lv_obj_set_style_text_color(m_govVal, lv_color_hex(govColor(s.govState)), LV_PART_MAIN);
    }

    void destroy() override
    {
        if (m_root) {
            lv_obj_del(m_root);
            m_root = nullptr;
            m_heroNum = nullptr;
            m_mhz = Tape{};
            m_vr = Tape{};
            m_mhzBug = nullptr;
            m_mhzBugTag = nullptr;
            m_vrAlvo = Mark{};
            m_vrTeto = Mark{};
            m_vrCorte = Mark{};
            m_potVal = nullptr;
            m_effVal = nullptr;
            m_govVal = nullptr;
        }
    }

  private:
    struct Mark
    {
        lv_obj_t *bar = nullptr;
        lv_obj_t *tag = nullptr;
    };

    void buildTape(Tape &t, int tapeX, const char *title, const char *unit, const float *values, int count)
    {
        screenMakeLabel(m_root, title, tapeX + 2, TAPE_TOP, 96, &font_archivo_regular_15, P_STEEL);

        t.view = lv_obj_create(m_root);
        lv_obj_set_pos(t.view, tapeX, VIEW_Y);
        lv_obj_set_size(t.view, VIEW_W, VIEW_H);
        lv_obj_set_style_bg_color(t.view, lv_color_hex(P_PANEL), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(t.view, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(t.view, lv_color_hex(P_PANEL_EDGE), LV_PART_MAIN);
        lv_obj_set_style_border_width(t.view, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(t.view, 6, LV_PART_MAIN);
        lv_obj_set_style_pad_all(t.view, 0, LV_PART_MAIN);
        lv_obj_clear_flag(t.view, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *spine = lv_obj_create(t.view);
        lv_obj_set_pos(spine, 40, 0);
        lv_obj_set_size(spine, 1, VIEW_H);
        lv_obj_set_style_bg_color(spine, lv_color_hex(P_SPINE), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(spine, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(spine, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(spine, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(spine, 0, LV_PART_MAIN);
        lv_obj_clear_flag(spine, LV_OBJ_FLAG_SCROLLABLE);

        t.tickValues = values;
        t.tickCount = count < MAX_TICKS ? count : MAX_TICKS;
        for (int i = 0; i < t.tickCount; i++) {
            t.tick[i] = screenMakeLabel(t.view, "", 28, 0, 68, &font_saira_condensed_semibold_15, P_STEEL);
        }
        //   Readout is built separately, by buildReadout, AFTER each tape's
        //   own extra marks (the MHz ceiling bug / the VR target-hard-cut
        //   marks) so it ends up LAST in z-order == visually on top of
        //   everything else in the tape. That matches the approved design's explicit
        //   `.s01-readout{z-index:3}` (it sits above the ticks AND the bug
        //   even though the bug is appended to the DOM after it) -- LVGL 8
        //   plain lv_obj has no z-index property, so paint order (last
        //   child created = topmost) is the only way to reproduce that here.
    }

    void buildReadout(Tape &t, const char *unit)
    {
        t.readout = lv_obj_create(t.view);
        lv_obj_set_pos(t.readout, 6, 88);
        lv_obj_set_size(t.readout, VIEW_W - 12, 40);
        lv_obj_set_style_bg_color(t.readout, lv_color_hex(P_BG), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(t.readout, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(t.readout, lv_color_hex(P_IVORY), LV_PART_MAIN);
        lv_obj_set_style_border_width(t.readout, 2, LV_PART_MAIN);
        lv_obj_set_style_radius(t.readout, 4, LV_PART_MAIN);
        lv_obj_set_style_pad_all(t.readout, 0, LV_PART_MAIN);
        lv_obj_clear_flag(t.readout, LV_OBJ_FLAG_SCROLLABLE);

        t.readoutVal = screenMakeLabel(t.readout, "--", 2, 6, 54, &font_saira_condensed_bold_28, P_IVORY, LV_TEXT_ALIGN_RIGHT);
        screenMakeLabel(t.readout, unit, 60, 13, 26, &font_archivo_regular_15, P_STEEL);
    }

    void buildMark(Mark &m)
    {
        m.bar = lv_obj_create(m_vr.view);
        lv_obj_set_pos(m.bar, 4, 0);
        lv_obj_set_size(m.bar, VIEW_W - 8, 3);
        lv_obj_set_style_bg_opa(m.bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(m.bar, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(m.bar, 1, LV_PART_MAIN);
        lv_obj_set_style_pad_all(m.bar, 0, LV_PART_MAIN);
        lv_obj_clear_flag(m.bar, LV_OBJ_FLAG_SCROLLABLE);

        m.tag = screenMakeLabel(m_vr.view, "", 50, 0, 46, &font_saira_condensed_semibold_15, P_STEEL, LV_TEXT_ALIGN_RIGHT);
    }

    //  Repositions every tick label of a tape so the value `center` sits at the
    //  view's vertical middle. Ticks whose value ends up far outside the visible
    //  0.VIEW_H band are hidden (LVGL already clips the `view` container's
    //  children to its bounds, so this is a perf-only guard against needlessly
    //  drawing off-screen labels, not a correctness requirement).
    void layTape(Tape &t, float center, float scale)
    {
        for (int i = 0; i < t.tickCount; i++) {
            float y = VIEW_C + (center - t.tickValues[i]) * scale - 7.0f;
            if (y < -20.0f || y > VIEW_H + 20.0f) {
                lv_obj_add_flag(t.tick[i], LV_OBJ_FLAG_HIDDEN);
                continue;
            }
            lv_obj_clear_flag(t.tick[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_y(t.tick[i], (lv_coord_t) y);
            char buf[8];
            nqfmt::n0(buf, sizeof(buf), t.tickValues[i]);
            lv_label_set_text(t.tick[i], buf);
        }
    }

    void layMark(Mark &m, float center, float value, uint32_t color)
    {
        float y = nqfmt::clampf(VIEW_C + (center - value) * VR_SCALE, 6.0f, (float) VIEW_H - 6.0f);
        lv_obj_set_y(m.bar, (lv_coord_t) y);
        lv_obj_set_style_bg_color(m.bar, lv_color_hex(color), LV_PART_MAIN);
        lv_obj_set_y(m.tag, (lv_coord_t) (y - 16.0f));
        lv_obj_set_style_text_color(m.tag, lv_color_hex(color), LV_PART_MAIN);
        char buf[8];
        nqfmt::n0(buf, sizeof(buf), value);
        lv_label_set_text(m.tag, buf);
    }

    lv_obj_t *m_root = nullptr;
    lv_obj_t *m_heroNum = nullptr;
    Tape m_mhz;
    Tape m_vr;
    lv_obj_t *m_mhzBug = nullptr;
    lv_obj_t *m_mhzBugTag = nullptr;
    Mark m_vrAlvo, m_vrTeto, m_vrCorte;
    lv_obj_t *m_potVal = nullptr;
    lv_obj_t *m_effVal = nullptr;
    lv_obj_t *m_govVal = nullptr;
};

S01PainelScreen s_instance;

} // namespace

Screen *screenGetS01Painel()
{
    return &s_instance;
}
