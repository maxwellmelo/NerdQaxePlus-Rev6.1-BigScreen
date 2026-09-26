// Ported from the approved design to LVGL 8.3 (DISPLAY_PROFILE_
// YYSLUPING_480X320). "Relogio": an analog desk clock (Braun/train-station
// spirit) whose outer bezel doubles as a discrete "how close to the
// frequency ceiling" gauge (40 short segments) and whose thin arc under the
// dial shows the regulator's thermal headroom. A calendar block (short
// "Wed, Sep 24"-style date line / big day number) sits to the right, in
// English written out by hand (own abbreviated name tables below -- no
// strftime/locale). A vitals line at the bottom repeats hashrate/
// regulator temperature/governor state. From 22h to 6h the whole screen
// switches to a low-contrast night palette.
//
// Before this pass: a placeholder (see git history /
// screen_registry.cpp) -- one centered "20 Relogio (em construcao)" label
// on a flat dark background, no data, created only so the screen
// rotation/build/simulator wiring already existed before porting started.
//
// After: the approved design's own warm day/night palettes, its 12-tick dial
// with quarter numerals, the 40-segment frequency bezel and the thermal-
// headroom arc, the 3 lv_line hands (repositioned once per update call from
// UiState::epochS via localtime_r -- no lv_anim, no continuous sweep: the
// approved design itself says the firmware only needs 1x/s granularity), the
// calendar block and the vitals line. When UiState::clockValid is false the
// whole dial + calendar are swapped for a plain "--:--" plus a not-synced
// sentence, per the task's hard "never invent a time" rule.

#include "../fonts/fonts.h"
#include "../ui_state.h"
#include "nq_fmt.h"
#include "screen.h"
#include "screen_common.h"

#include <cmath>
#include <cstdio>
#include <ctime>

namespace
{

// ---- geometry (the approved design's mount) ----------
constexpr float CX = 140.0f, CY = 146.0f;
constexpr float R_TICK_OUT = 98.0f, R_TICK_IN_MAJOR = 82.0f, R_TICK_IN_MINOR = 90.0f;
constexpr float R_NUM = 66.0f;
constexpr float RIM_R = 100.0f;
constexpr float R_HEAD = 106.0f;
constexpr int HEAD_A0 = 120, HEAD_A1 = 240; // the approved design angles: 0 = 12 o'clock, clockwise
constexpr int HEAD_W = 5;
constexpr float R_BEZEL = 118.0f;
constexpr int BEZEL_W = 8;
constexpr int BEZEL_N = 40;
constexpr float LEN_H = 46.0f, LEN_M = 72.0f, LEN_S = 84.0f;
constexpr float kPi = 3.14159265358979323846f;

// Calendar block (the approved design's .s20-date: left:276 top:80 width:184).
constexpr int DATE_X = 276, DATE_Y = 80, DATE_W = 184;

// Vitals row (the approved design's .s20-vitals: left:24 right:24 bottom:16 height:24 ->
// on this 320-tall screen, y = 320 - 16 - 24 = 280). Fixed columns (not a
// single free-flowing row) so varying digit widths never collide -- same
// approach as s01_painel's tape readouts and s14_hashrate's average
// columns, learned from a real overlap bug caused by assuming fonts have
// true tabular figures.
constexpr int VIT_Y = 280;
constexpr int HASH_X = 24, HASH_W = 110;
constexpr int SEP1_X = 142;
constexpr int TEMP_X = 154, TEMP_W = 150;
constexpr int SEP2_X = 312;
constexpr int GOV_X = 324, GOV_W = 132; // ends at 456 = 480 - 24, the approved design's right margin

// ---- palettes (the approved design's DAY_PAL / NIGHT_PAL) ------------------------------
struct Palette
{
    uint32_t bg, ink, inkDim, faceLine, accentHash, accentSecond, ringDim;
    uint32_t headGood, headWarn, headBad, emergency;
};
constexpr Palette kDayPal = {0xf3ead8, 0x2a2118, 0x8a7b62, 0xcabb98, 0xc2792f, 0xb1432b,
                             0xe3d7ba, 0x6d8a58, 0xc0862f, 0xbb3d2a, 0xbb3d2a};
constexpr Palette kNightPal = {0x18140e, 0xece1cb, 0x8d7f65, 0x463c2a, 0xc98c4d, 0xc06a4d,
                               0x332c1f, 0x7fa066, 0xcf9a4d, 0xc1543d, 0xc1543d};

// ---- English name tables (NEVER strftime/setlocale -- unreliable on-device,
// see task brief). Indexed exactly like struct tm's own tm_wday/tm_mon.
// Abbreviated (3-letter) forms only: the only place either table is
// rendered is the short "Wed, Sep 24" date line built in update below, so
// spelled-out full names would be dead weight -- see the 2026-09-24 PT->EN
// pass entry in for why full-name arrays
// (this screen's previous kWeekdays/kMonths) were dropped instead of kept
// alongside these. ----
const char *const kWeekdayAbbr[7] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
};
const char *const kMonthAbbr[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
};

const char *govLabel(uint8_t st)
{
    switch (st) {
    case UI_GOV_STARTUP:
        return "starting";
    case UI_GOV_RAMP_UP:
        return "ramping up";
    case UI_GOV_HOLD:
        return "stable";
    case UI_GOV_BACKOFF:
        return "backing off";
    case UI_GOV_EMERGENCY:
        return "emergency";
    case UI_GOV_SATURATED_LOW:
        return "at minimum";
    case UI_GOV_SENSOR_FAIL:
        return "sensor fault";
    case UI_GOV_FROZEN:
        return "frozen";
    case UI_GOV_OFF:
    default:
        return "off";
    }
}

// Point on the dial at the approved design angle `angleDeg` (0 = 12 o'clock, clockwise),
// radius `r` from the dial center -- same trigonometry as the approved design's own
// `(a - 90) * PI/180` conversion in setLine/the tick/bezel loops.
inline lv_point_t dialPoint(float cx, float cy, float r, float angleDeg)
{
    float rad = (angleDeg - 90.0f) * kPi / 180.0f;
    return {(lv_coord_t) lroundf(cx + r * cosf(rad)), (lv_coord_t) lroundf(cy + r * sinf(rad))};
}

lv_obj_t *makeLine(lv_obj_t *parent, lv_point_t *pts, uint32_t color, int width, bool rounded)
{
    lv_obj_t *l = lv_line_create(parent);
    lv_line_set_points(l, pts, 2);
    lv_obj_set_style_line_color(l, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_line_width(l, width, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(l, rounded, LV_PART_MAIN);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

// Group container: a transparent full-screen lv_obj so a whole visual group
// (the dial, the calendar block) can be shown/hidden with a single
// lv_obj_add_flag/clear_flag(LV_OBJ_FLAG_HIDDEN) call instead of touching
// every child object -- LVGL skips drawing a hidden object's subtree, so
// this costs nothing extra while hidden.
lv_obj_t *makeGroup(lv_obj_t *parent)
{
    lv_obj_t *g = lv_obj_create(parent);
    lv_obj_set_pos(g, 0, 0);
    lv_obj_set_size(g, 480, 320);
    lv_obj_set_style_bg_opa(g, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(g, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(g, 0, LV_PART_MAIN);
    lv_obj_clear_flag(g, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(g, LV_OBJ_FLAG_CLICKABLE);
    return g;
}

class S20RelogioScreen : public Screen {
  public:
    int number() const override
    {
        return 20;
    }
    const char *name() const override
    {
        return "relogio";
    }

    void create(lv_obj_t *parent) override
    {
        if (m_root) {
            destroy();
        }
        m_root = screenMakeRoot(parent, kDayPal.bg);
        m_night = -1;      // force the first update to apply a palette
        m_clockValid = -1; // force the first update to pick a group

        buildFace();
        buildDate();
        buildNotSynced();
        buildVitals();
    }

    void update(const UiState &s) override
    {
        if (!m_root) {
            return;
        }

        bool clockValid = s.clockValid;
        struct tm tmv = {};
        if (clockValid) {
            time_t t = (time_t) s.epochS;
            localtime_r(&t, &tmv);
        }
        bool isNight = clockValid && (tmv.tm_hour >= 22 || tmv.tm_hour < 6);

        if (m_clockValid != (clockValid ? 1 : 0)) {
            m_clockValid = clockValid ? 1 : 0;
            if (clockValid) {
                lv_obj_clear_flag(m_faceGroup, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(m_dateGroup, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(m_notSyncedGroup, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(m_faceGroup, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(m_dateGroup, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(m_notSyncedGroup, LV_OBJ_FLAG_HIDDEN);
            }
        }

        if (m_night != (isNight ? 1 : 0)) {
            m_night = isNight ? 1 : 0;
            applyPalette(isNight ? kNightPal : kDayPal);
        }

        if (clockValid) {
            //  Short English date line ("Wed, Sep 24" -- a deliberately chosen style,
            //  2026-09-24 PT->EN pass): weekday abbr + ", " + month abbr + numeric day,
            //  all in the one top label (m_weekday) that used to hold just the pt-BR
            //  weekday name. The bottom label (m_month, used to hold "de setembro") is no
            //  longer populated -- the big hero digit below already repeats the day
            //  number, so the short combined line does not need a second, now-redundant
            //  "de %s"-style month line under it. The before/after.
            char dateBuf[24];
            snprintf(dateBuf, sizeof(dateBuf), "%s, %s %d", kWeekdayAbbr[tmv.tm_wday], kMonthAbbr[tmv.tm_mon], tmv.tm_mday);
            lv_label_set_text(m_weekday, dateBuf);
            char dayBuf[4];
            snprintf(dayBuf, sizeof(dayBuf), "%d", tmv.tm_mday);
            lv_label_set_text(m_day, dayBuf);

            renderHands(tmv);
        }

        //   Frequency bezel: fraction of the governor's ceiling in use. NAN
        //   freq/fmin/freqCap fall back to nqfmt::mapf's low end (a visual
        //   "almost nothing lit" position, same convention s19_zen's bar
        //   uses) -- this is a decorative gauge position, never a printed
        //   number, so it does not violate the "NAN -> '--' " text rule.
        const Palette &pal = isNight ? kNightPal : kDayPal;
        float frac = nqfmt::mapf(s.freq, s.fmin, s.freqCap, 0.04f, 1.0f);
        int lit = (int) lroundf(frac * BEZEL_N);
        uint32_t hashColor = (s.govState == UI_GOV_EMERGENCY) ? pal.emergency : pal.accentHash;
        for (int i = 0; i < BEZEL_N; i++) {
            lv_obj_set_style_line_color(m_bezel[i], lv_color_hex(i < lit ? hashColor : pal.ringDim), LV_PART_MAIN);
        }

        //   Thermal headroom arc.
        float hfrac = nqfmt::clampf(nqfmt::mapf(s.headroom, -12.0f, 32.0f, 0.02f, 1.0f), 0.02f, 1.0f);
        lv_arc_set_value(m_headArc, (int16_t) lroundf(hfrac * 1000.0f));
        uint32_t headColor = pal.headGood;
        if (!std::isnan(s.headroom)) {
            headColor = (s.headroom < 2.0f) ? pal.headBad : (s.headroom < 10.0f) ? pal.headWarn : pal.headGood;
        }
        lv_obj_set_style_arc_color(m_headArc, lv_color_hex(headColor), LV_PART_INDICATOR);

        //   Vitals line.
        char buf[24];
        nqfmt::ths(buf, sizeof(buf), s.hr1m, 2);
        char hashLine[32];
        snprintf(hashLine, sizeof(hashLine), "%s TH/s", buf);
        lv_label_set_text(m_hashLbl, hashLine);

        nqfmt::n0(buf, sizeof(buf), s.vrTemp);
        char tempLine[40];
        snprintf(tempLine, sizeof(tempLine),
                 "%s \xc2\xb0"
                 "C regulator",
                 buf);
        lv_label_set_text(m_tempLbl, tempLine);

        lv_label_set_text(m_govLbl, govLabel(s.govState));
        lv_obj_set_style_text_color(m_govLbl, lv_color_hex(s.govState == UI_GOV_EMERGENCY ? pal.emergency : pal.inkDim),
                                    LV_PART_MAIN);
    }

    void destroy() override
    {
        if (m_root) {
            lv_obj_del(m_root);
            m_root = nullptr;
            m_faceGroup = m_dateGroup = m_notSyncedGroup = nullptr;
            m_rim = nullptr;
            m_headArc = nullptr;
            m_hourHand = m_minHand = m_secHand = m_pivot = nullptr;
            m_weekday = m_day = m_month = nullptr;
            m_hashLbl = m_tempLbl = m_govLbl = nullptr;
            m_sep1 = m_sep2 = nullptr;
            for (int i = 0; i < 12; i++) {
                m_tick[i] = nullptr;
            }
            for (int i = 0; i < 4; i++) {
                m_numeral[i] = nullptr;
            }
            for (int i = 0; i < BEZEL_N; i++) {
                m_bezel[i] = nullptr;
            }
        }
    }

  private:
    //   ---- build (once): the analog dial + bezel + headroom arc + hands ----
    void buildFace()
    {
        m_faceGroup = makeGroup(m_root);

        m_rim = lv_obj_create(m_faceGroup);
        lv_obj_set_size(m_rim, (lv_coord_t) (RIM_R * 2), (lv_coord_t) (RIM_R * 2));
        lv_obj_set_pos(m_rim, (lv_coord_t) (CX - RIM_R), (lv_coord_t) (CY - RIM_R));
        lv_obj_set_style_radius(m_rim, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(m_rim, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(m_rim, 1, LV_PART_MAIN);
        lv_obj_set_style_pad_all(m_rim, 0, LV_PART_MAIN);
        lv_obj_clear_flag(m_rim, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(m_rim, LV_OBJ_FLAG_CLICKABLE);

        //   12 hour ticks: thicker/longer at 12/3/6/9 (same "major" rule as
        //   the approved design's `i % 3 === 0`).
        for (int i = 0; i < 12; i++) {
            bool major = (i % 3) == 0;
            float a = i * 30.0f;
            float rIn = major ? R_TICK_IN_MAJOR : R_TICK_IN_MINOR;
            m_tickPts[i][0] = dialPoint(CX, CY, R_TICK_OUT, a);
            m_tickPts[i][1] = dialPoint(CX, CY, rIn, a);
            m_tick[i] = makeLine(m_faceGroup, m_tickPts[i], kDayPal.faceLine, major ? 4 : 2, true);
        }

        //   Quarter numerals (12, 3, 6, 9) -- digits only, reuses the
        //   existing 24px Archivo Bold cut generated for 14-hashrate's
        //   averages row (font_archivo_bold_24, glyph set NUM). No new font.
        static const int kHours[4] = {0, 3, 6, 9};
        static const char *const kNumText[4] = {"12", "3", "6", "9"};
        for (int i = 0; i < 4; i++) {
            float a = kHours[i] * 30.0f;
            lv_point_t p = dialPoint(CX, CY, R_NUM, a);
            m_numeral[i] = screenMakeLabel(m_faceGroup, kNumText[i], p.x - 15, p.y - 13, 30, &font_archivo_bold_24, kDayPal.ink,
                                           LV_TEXT_ALIGN_CENTER);
        }

        //  Thermal headroom arc: bottom quadrant, grows clockwise from HEAD_A0 towards
        //  HEAD_A1 as headroom improves. ONE lv_arc with a transparent track
        //  (LV_PART_MAIN) and only the indicator visible -- the approved design never
        //  draws a "spent" background for this arc either, only the lit part.
        //  s10_halving.cpp's makeRingArc for the same rotation(270) trick that makes
        //  lv_arc's own angle 0 land on 12 o'clock, which happens to make its angle
        //  numbers equal the approved design's own angle convention 1:1 (both then
        //  measure clockwise from 12 o'clock), so HEAD_A0/HEAD_A1 are used verbatim.
        m_headArc = lv_arc_create(m_faceGroup);
        lv_obj_remove_style(m_headArc, nullptr, LV_PART_KNOB);
        lv_obj_clear_flag(m_headArc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(m_headArc, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(m_headArc, (lv_coord_t) (R_HEAD * 2), (lv_coord_t) (R_HEAD * 2));
        lv_obj_set_pos(m_headArc, (lv_coord_t) (CX - R_HEAD), (lv_coord_t) (CY - R_HEAD));
        lv_obj_set_style_pad_all(m_headArc, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(m_headArc, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_arc_set_bg_angles(m_headArc, HEAD_A0, HEAD_A1);
        lv_arc_set_rotation(m_headArc, 270);
        lv_arc_set_range(m_headArc, 0, 1000);
        lv_arc_set_mode(m_headArc, LV_ARC_MODE_NORMAL);
        lv_obj_set_style_arc_opa(m_headArc, LV_OPA_TRANSP, LV_PART_MAIN); // no track
        lv_obj_set_style_arc_width(m_headArc, HEAD_W, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(m_headArc, lv_color_hex(kDayPal.headGood), LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(m_headArc, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(m_headArc, true, LV_PART_INDICATOR);
        lv_arc_set_value(m_headArc, 20);

        //   Discrete hashrate bezel: 40 short segments around the whole
        //   dial. The approved design draws these as true SVG arcs
        //   (`NQ.arc(CX,CY,R_BEZEL,a0,a1)`); LVGL 8.3 has no cheap way to
        //   draw 40 independent short arc segments (an lv_arc widget is a
        //   "track + one indicator" pair, not a discrete tick gauge), so
        //   each segment is approximated by a straight chord between its two
        //   endpoints instead -- at R_BEZEL=118px with a ~6.6deg segment the
        //   curvature is under a pixel, invisible at this size, and a
        //   straight 2-point lv_line is exactly what s01_painel/s14_hashrate
        //   already use for short marks. Documented in
        //
        float stepDeg = 360.0f / BEZEL_N, gapDeg = 2.4f, segDeg = stepDeg - gapDeg;
        for (int k = 0; k < BEZEL_N; k++) {
            float a0 = k * stepDeg, a1 = a0 + segDeg;
            m_bezelPts[k][0] = dialPoint(CX, CY, R_BEZEL, a0);
            m_bezelPts[k][1] = dialPoint(CX, CY, R_BEZEL, a1);
            m_bezel[k] = makeLine(m_faceGroup, m_bezelPts[k], kDayPal.ringDim, BEZEL_W, false);
        }

        //   Hands + pivot. Points are recomputed in renderHands; the
        //   arrays here just give lv_line something valid to point at until
        //   the first update (lv_line_set_points stores the POINTER it is
        //   given, so the backing array must be a member, not a local --
        //   same rule s04_fluxo_energia.cpp/s06_eficiencia.cpp document).
        m_hourPts[0] = {(lv_coord_t) CX, (lv_coord_t) CY};
        m_hourPts[1] = dialPoint(CX, CY, LEN_H, 0);
        m_hourHand = makeLine(m_faceGroup, m_hourPts, kDayPal.ink, 5, true);

        m_minPts[0] = {(lv_coord_t) CX, (lv_coord_t) CY};
        m_minPts[1] = dialPoint(CX, CY, LEN_M, 0);
        m_minHand = makeLine(m_faceGroup, m_minPts, kDayPal.ink, 4, true);

        m_secPts[0] = {(lv_coord_t) CX, (lv_coord_t) CY};
        m_secPts[1] = dialPoint(CX, CY, LEN_S, 0);
        m_secHand = makeLine(m_faceGroup, m_secPts, kDayPal.accentSecond, 2, true);

        m_pivot = lv_obj_create(m_faceGroup);
        lv_obj_set_size(m_pivot, 10, 10);
        lv_obj_set_pos(m_pivot, (lv_coord_t) (CX - 5), (lv_coord_t) (CY - 5));
        lv_obj_set_style_radius(m_pivot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(m_pivot, lv_color_hex(kDayPal.accentSecond), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(m_pivot, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(m_pivot, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(m_pivot, 0, LV_PART_MAIN);
        lv_obj_clear_flag(m_pivot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(m_pivot, LV_OBJ_FLAG_CLICKABLE);
    }

    //   ---- build (once): calendar block ------------------------------------
    void buildDate()
    {
        m_dateGroup = makeGroup(m_root);
        //   Weekday/date line: short English "Wed, Sep 24" style (2026-09-24
        //   PT->EN pass -- see ), reusing the
        //   existing 17px Archivo Medium cut (font_archivo_medium_17,
        //   generated for 14-hashrate's insight sentence) instead of a new
        //   Space Grotesk font -- see the.md for why Space Grotesk (the
        //   the approved design's own pick) was not worth adding. ASCII-only now, but kept
        //   on this FULL-glyph-set font rather than switching to a digits/
        //   ASCII-only cut, since no flash budget is gained mid-pass.
        m_weekday = screenMakeLabel(m_dateGroup, "--", DATE_X, DATE_Y, DATE_W, &font_archivo_medium_17, kDayPal.inkDim);

        //   Day-of-month hero: reuses font_saira_condensed_bold_108 (digits
        //   only), already generated for 01-painel's hashrate hero. Bigger
        //   than the approved design's own 76px Space Grotesk, but well inside the
        //   184px-wide date column for any 1-2 digit day.
        m_day = screenMakeLabel(m_dateGroup, "--", DATE_X, DATE_Y + 30, DATE_W, &font_saira_condensed_bold_108, kDayPal.ink);

        //   m_month used to hold "de setembro" (pt-BR "of <month>"); the
        //   short combined date line above now carries the month itself
        //   ("Wed, Sep 24"), so this label is created (position/size
        //   untouched, per this pass's scope) but deliberately left empty --
        //   see the update comment at the date-line snprintf.
        m_month = screenMakeLabel(m_dateGroup, "", DATE_X, DATE_Y + 150, DATE_W, &font_archivo_medium_17, kDayPal.inkDim);
    }

    //   ---- build (once): "clock not synced yet" fallback --------------------
    void buildNotSynced()
    {
        m_notSyncedGroup = makeGroup(m_root);
        lv_obj_add_flag(m_notSyncedGroup, LV_OBJ_FLAG_HIDDEN);

        //   "--:--" needs a colon, which the digit-only NUM font sets do not
        //   include (see tools/fonts/generate-fonts.ps1's NUM range) -- this
        //   reuses font_archivo_bold_20 (FULL glyph set, ASCII incl. ':'),
        //   generated for 14-hashrate's title, at zero extra flash cost.
        screenMakeLabel(m_notSyncedGroup, "--:--", 40, 118, 200, &font_archivo_bold_20, kDayPal.ink, LV_TEXT_ALIGN_CENTER);

        lv_obj_t *msg = lv_label_create(m_notSyncedGroup);
        lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
        lv_obj_set_pos(msg, 40, 160);
        lv_obj_set_size(msg, 200, 60);
        lv_obj_set_style_text_font(msg, &font_archivo_regular_15, LV_PART_MAIN);
        lv_obj_set_style_text_color(msg, lv_color_hex(kDayPal.inkDim), LV_PART_MAIN);
        lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_text(msg, "clock not yet synced");
    }

    //   ---- build (once): vitals line ----------------------------------------
    void buildVitals()
    {
        m_hashLbl = screenMakeLabel(m_root, "--", HASH_X, VIT_Y, HASH_W, &font_archivo_regular_15, kDayPal.inkDim);

        m_sep1 = lv_obj_create(m_root);
        lv_obj_set_pos(m_sep1, SEP1_X, VIT_Y + 4);
        lv_obj_set_size(m_sep1, 1, 16);
        lv_obj_set_style_bg_color(m_sep1, lv_color_hex(kDayPal.inkDim), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(m_sep1, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(m_sep1, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(m_sep1, 0, LV_PART_MAIN);
        lv_obj_clear_flag(m_sep1, LV_OBJ_FLAG_SCROLLABLE);

        m_tempLbl = screenMakeLabel(m_root, "--", TEMP_X, VIT_Y, TEMP_W, &font_archivo_regular_15, kDayPal.inkDim);

        m_sep2 = lv_obj_create(m_root);
        lv_obj_set_pos(m_sep2, SEP2_X, VIT_Y + 4);
        lv_obj_set_size(m_sep2, 1, 16);
        lv_obj_set_style_bg_color(m_sep2, lv_color_hex(kDayPal.inkDim), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(m_sep2, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(m_sep2, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(m_sep2, 0, LV_PART_MAIN);
        lv_obj_clear_flag(m_sep2, LV_OBJ_FLAG_SCROLLABLE);

        m_govLbl = screenMakeLabel(m_root, "--", GOV_X, VIT_Y, GOV_W, &font_archivo_regular_15, kDayPal.inkDim);
    }

    //   Repositions the 3 hands from a local calendar broken-down time.
    //   Called at most once per update (no lv_anim, no sub-second
    //   interpolation) -- exactly the "reduced motion" branch the approved design
    //   itself falls back to for real hardware (the approved design's
    //   `NQ.reducedMotion` path recomputes from `getSeconds` only).
    void renderHands(const struct tm &tmv)
    {
        int h12 = tmv.tm_hour % 12;
        float hourAngle = (h12 + tmv.tm_min / 60.0f + tmv.tm_sec / 3600.0f) * 30.0f;
        float minAngle = (tmv.tm_min + tmv.tm_sec / 60.0f) * 6.0f;
        float secAngle = tmv.tm_sec * 6.0f;

        m_hourPts[1] = dialPoint(CX, CY, LEN_H, hourAngle);
        lv_line_set_points(m_hourHand, m_hourPts, 2);
        m_minPts[1] = dialPoint(CX, CY, LEN_M, minAngle);
        lv_line_set_points(m_minHand, m_minPts, 2);
        m_secPts[1] = dialPoint(CX, CY, LEN_S, secAngle);
        lv_line_set_points(m_secHand, m_secPts, 2);
    }

    // Applies one full palette (day or night) to every element whose color
    // depends on it. Called only when the day/night flag actually flips, like the
    // approved design's own `if (isNight !== night) applyPalette(.)`.
    void applyPalette(const Palette &p)
    {
        lv_obj_set_style_bg_color(m_root, lv_color_hex(p.bg), LV_PART_MAIN);
        lv_obj_set_style_border_color(m_rim, lv_color_hex(p.faceLine), LV_PART_MAIN);
        for (int i = 0; i < 12; i++) {
            lv_obj_set_style_line_color(m_tick[i], lv_color_hex(p.faceLine), LV_PART_MAIN);
        }
        for (int i = 0; i < 4; i++) {
            lv_obj_set_style_text_color(m_numeral[i], lv_color_hex(p.ink), LV_PART_MAIN);
        }
        lv_obj_set_style_line_color(m_hourHand, lv_color_hex(p.ink), LV_PART_MAIN);
        lv_obj_set_style_line_color(m_minHand, lv_color_hex(p.ink), LV_PART_MAIN);
        lv_obj_set_style_line_color(m_secHand, lv_color_hex(p.accentSecond), LV_PART_MAIN);
        lv_obj_set_style_bg_color(m_pivot, lv_color_hex(p.accentSecond), LV_PART_MAIN);

        lv_obj_set_style_text_color(m_weekday, lv_color_hex(p.inkDim), LV_PART_MAIN);
        lv_obj_set_style_text_color(m_day, lv_color_hex(p.ink), LV_PART_MAIN);
        lv_obj_set_style_text_color(m_month, lv_color_hex(p.inkDim), LV_PART_MAIN);

        lv_obj_set_style_text_color(m_hashLbl, lv_color_hex(p.inkDim), LV_PART_MAIN);
        lv_obj_set_style_text_color(m_tempLbl, lv_color_hex(p.inkDim), LV_PART_MAIN);
        lv_obj_set_style_bg_color(m_sep1, lv_color_hex(p.inkDim), LV_PART_MAIN);
        lv_obj_set_style_bg_color(m_sep2, lv_color_hex(p.inkDim), LV_PART_MAIN);
        //   m_govLbl's color also depends on govState (emergency), so it is
        //   finished off in update, not here.
    }

    lv_obj_t *m_root = nullptr;
    lv_obj_t *m_faceGroup = nullptr, *m_dateGroup = nullptr, *m_notSyncedGroup = nullptr;

    lv_obj_t *m_rim = nullptr;
    lv_obj_t *m_tick[12] = {};
    lv_point_t m_tickPts[12][2] = {};
    lv_obj_t *m_numeral[4] = {};
    lv_obj_t *m_headArc = nullptr;
    lv_obj_t *m_bezel[BEZEL_N] = {};
    lv_point_t m_bezelPts[BEZEL_N][2] = {};
    lv_obj_t *m_hourHand = nullptr, *m_minHand = nullptr, *m_secHand = nullptr, *m_pivot = nullptr;
    lv_point_t m_hourPts[2] = {}, m_minPts[2] = {}, m_secPts[2] = {};

    lv_obj_t *m_weekday = nullptr, *m_day = nullptr, *m_month = nullptr;

    lv_obj_t *m_hashLbl = nullptr, *m_tempLbl = nullptr, *m_govLbl = nullptr;
    lv_obj_t *m_sep1 = nullptr, *m_sep2 = nullptr;

    int m_night = -1;      // -1 unknown, 0 day, 1 night
    int m_clockValid = -1; // -1 unknown, 0 invalid, 1 valid
};

S20RelogioScreen s_instance;

} // namespace

Screen *screenGetS20Relogio()
{
    return &s_instance;
}
