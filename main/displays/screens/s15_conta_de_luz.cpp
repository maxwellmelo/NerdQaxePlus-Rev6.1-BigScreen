// Ported from the approved design to LVGL 8.3 (DISPLAY_
// PROFILE_YYSLUPING_480X320). "Conta de luz": a thermal-printer receipt of
// what the miner is costing to run -- today's items, the month's
// projection, tariff, cost per TH/s, a lamp-count equivalence and a
// highlighted total, exactly like the paper that would come out of a real
// till.
//
// Before this pass: a skeleton
// with a single placeholder label, no coupon, no energy data shown at all.
//
// After: the approved design's own cream-paper/brick-red receipt palette, a
// single new monospaced font family (Courier Prime -- the approved design's
// own "this is the most literal way to say printed receipt" choice) for every
// line of text, the 6 itemized rows, the lamp-equivalence note, the hero total
// (single-size simplification, same technique as s19_zen.cpp's hero), and a
// serrated tear edge drawn with ONE lv_line (not hundreds of objects).

#include "../ui_state.h"
#include "nq_fmt.h"
#include "screen.h"
#include "screen_common.h"

#include <cmath>
#include <cstdio>
#include <ctime>

// New fonts for this screen (see tools/fonts/generate-fonts.ps1 / this
// screen's own doc for the exact glyph ranges and size rationale):
// - regular_16: every line of body text (labels, values, note, header,
// total label) -- a hand-picked ASCII + minimal pt-BR accent subset
// (a,a-circumflex,a-tilde,c-cedilla,e-circumflex: the only accented
// letters this screen's fixed vocabulary actually uses), not the full
// Latin-1 pt-BR range other screens need for free-form text.
// - bold_64: the hero total's digits (+ comma, + hyphen for the "--"
// no-data state) -- meets the CONTRATO.md >=64px hero-number rule.
LV_FONT_DECLARE(font_s15_courier_prime_regular_16);
LV_FONT_DECLARE(font_s15_courier_prime_bold_64);

namespace
{

constexpr uint32_t S15_PAPER = 0xf3ecd6;
constexpr uint32_t S15_INK = 0x352d22;
constexpr uint32_t S15_ACCENT = 0xa13d2c;
constexpr uint32_t S15_NOTE = 0x6f6552;
constexpr uint32_t S15_RULE = 0xcdbf9d;
constexpr uint32_t S15_MECANISMO = 0x2a241c;

constexpr int MARGIN_L = 20, MARGIN_R = 20;
constexpr int ROW_TOP = 46, ROW_H = 20;

void brl(char *buf, size_t n, float v, const char *currency)
{
    if (std::isnan(v)) {
        std::snprintf(buf, n, "--");
        return;
    }
    char num[24];
    nqfmt::n2(num, sizeof(num), v);
    std::snprintf(buf, n, "%s %s", currency, num);
}

class S15ContaDeLuzScreen : public Screen {
  public:
    int number() const override
    {
        return 15;
    }
    const char *name() const override
    {
        return "conta_de_luz";
    }

    void create(lv_obj_t *parent) override
    {
        if (m_root) {
            destroy();
        }
        m_root = screenMakeRoot(parent, S15_PAPER);

        screenMakeLabel(m_root, "Power bill", MARGIN_L, 8, 240, &font_s15_courier_prime_regular_16, S15_INK);
        m_timeLbl =
            screenMakeLabel(m_root, "--:--", 300, 8, 160, &font_s15_courier_prime_regular_16, S15_ACCENT, LV_TEXT_ALIGN_RIGHT);

        makeDashedRule(32);

        static const char *kLabels[6] = {
            "Usage today", "Cost today", "Average power (24 h)", "Month projection", "Rate (adjustable)", "Cost per TH/s (month)",
        };
        for (int i = 0; i < 6; i++) {
            int y = ROW_TOP + i * ROW_H;
            screenMakeLabel(m_root, kLabels[i], MARGIN_L, y, 240, &font_s15_courier_prime_regular_16, S15_INK);
            m_values[i] = screenMakeLabel(m_root, "--", 240, y, 480 - MARGIN_R - 240, &font_s15_courier_prime_regular_16, S15_INK,
                                          LV_TEXT_ALIGN_RIGHT);
        }

        m_noteLbl = screenMakeLabel(m_root, "", MARGIN_L, ROW_TOP + 6 * ROW_H + 6, 480 - MARGIN_L - MARGIN_R,
                                    &font_s15_courier_prime_regular_16, S15_NOTE);
        lv_label_set_long_mode(m_noteLbl, LV_LABEL_LONG_WRAP);

        lv_obj_t *ruleTotal = lv_obj_create(m_root);
        lv_obj_set_pos(ruleTotal, MARGIN_L, 212);
        lv_obj_set_size(ruleTotal, 480 - MARGIN_L - MARGIN_R, 2);
        lv_obj_set_style_bg_color(ruleTotal, lv_color_hex(S15_ACCENT), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(ruleTotal, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(ruleTotal, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(ruleTotal, 0, LV_PART_MAIN);
        lv_obj_clear_flag(ruleTotal, LV_OBJ_FLAG_SCROLLABLE);

        screenMakeLabel(m_root, "Total this month (projected)", MARGIN_L, 218, 300, &font_s15_courier_prime_regular_16, S15_ACCENT);

        m_curLbl = screenMakeLabel(m_root, "", MARGIN_L, 256, 44, &font_s15_courier_prime_regular_16, S15_ACCENT);
        m_heroLbl = screenMakeLabel(m_root, "--", 62, 236, 400, &font_s15_courier_prime_bold_64, S15_ACCENT);

        buildTear();
    }

    void update(const UiState &s) override
    {
        if (!m_root) {
            return;
        }

        if (s.clockValid) {
            time_t t = (time_t) s.epochS;
            struct tm tmv{};
            localtime_r(&t, &tmv);
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%02d:%02d", tmv.tm_hour % 24, tmv.tm_min % 60);
            lv_label_set_text(m_timeLbl, buf);
        } else {
            lv_label_set_text(m_timeLbl, "--:--");
        }

        char buf[32];
        if (s.energyValid) {
            nqfmt::n2(buf, sizeof(buf), s.kwhToday);
            char line[40];
            std::snprintf(line, sizeof(line), "%s kWh", buf);
            lv_label_set_text(m_values[0], line);

            brl(line, sizeof(line), s.costToday, s.currency);
            lv_label_set_text(m_values[1], line);

            nqfmt::n0(buf, sizeof(buf), s.avgW24);
            std::snprintf(line, sizeof(line), "%s W", buf);
            lv_label_set_text(m_values[2], line);

            nqfmt::n0(buf, sizeof(buf), s.kwhMonth);
            std::snprintf(line, sizeof(line), "%s kWh", buf);
            lv_label_set_text(m_values[3], line);
        } else {
            for (int i = 0; i < 4; i++) {
                lv_label_set_text(m_values[i], "--");
            }
        }

        //  Tariff is a cfg constant (NVS-backed), never NAN -- shown
        //  regardless of energyValid, matching the approved design reference reading
        //  `cfg.tarifa` directly rather than through `state.energy`.
        char tarifaLine[48];
        brl(buf, sizeof(buf), s.tarifa, s.currency);
        std::snprintf(tarifaLine, sizeof(tarifaLine), "%s /kWh", buf);
        lv_label_set_text(m_values[4], tarifaLine);

        bool haveThs = s.energyValid && !std::isnan(s.hr1m) && s.hr1m > 0.0f;
        if (haveThs) {
            float ths = s.hr1m / 1000.0f;
            float custoThs = s.costMonth / ths;
            char line[40];
            brl(buf, sizeof(buf), custoThs, s.currency);
            std::snprintf(line, sizeof(line), "%s /mo", buf);
            lv_label_set_text(m_values[5], line);
        } else {
            lv_label_set_text(m_values[5], "--");
        }

        //  Household equivalence: common LED bulb ~9 W.
        if (s.energyValid && !std::isnan(s.avgW24)) {
            int lamps = (int) std::lround(s.avgW24 / 9.0f);
            if (lamps < 1) {
                lamps = 1;
            }
            char note[96];
            std::snprintf(note, sizeof(note), "Same as %d LED bulb%s (9 W) on all day.", lamps, lamps == 1 ? "" : "s");
            lv_label_set_text(m_noteLbl, note);
        } else {
            lv_label_set_text(m_noteLbl, "Same as -- LED bulbs (9 W) on all day.");
        }

        //  Hero total: month projection, R$ -- single font size for the
        //  whole "117,80" string (see s19_zen.cpp's hero for the same
        //  simplification and why: no second smaller-digit cut was
        //  generated just for the cents).
        if (s.energyValid && !std::isnan(s.costMonth)) {
            char num[24];
            nqfmt::n2(num, sizeof(num), s.costMonth);
            lv_label_set_text(m_heroLbl, num);
            lv_label_set_text(m_curLbl, s.currency);
        } else {
            lv_label_set_text(m_heroLbl, "--");
            lv_label_set_text(m_curLbl, "");
        }
    }

    void destroy() override
    {
        if (m_root) {
            lv_obj_del(m_root);
            m_root = nullptr;
        }
        m_timeLbl = m_noteLbl = m_curLbl = m_heroLbl = nullptr;
        for (int i = 0; i < 6; i++) {
            m_values[i] = nullptr;
        }
    }

  private:
    void makeDashedRule(int y)
    {
        lv_obj_t *rule = lv_line_create(m_root);
        lv_obj_set_style_line_color(rule, lv_color_hex(S15_RULE), LV_PART_MAIN);
        lv_obj_set_style_line_width(rule, 1, LV_PART_MAIN);
        lv_obj_set_style_line_dash_width(rule, 4, LV_PART_MAIN);
        lv_obj_set_style_line_dash_gap(rule, 4, LV_PART_MAIN);
        m_rulePts[0] = {MARGIN_L, (lv_coord_t) y};
        m_rulePts[1] = {480 - MARGIN_R, (lv_coord_t) y};
        lv_line_set_points(rule, m_rulePts, 2);
    }

    //  Serrated tear: ONE lv_line tracing a fixed zigzag (never rebuilt),
    //  drawn over a plain dark "printer mechanism" bar. This is an outline,
    //  not a filled cut-out shape -- LVGL 8's allowed primitive set has no
    //  filled arbitrary polygon (CONTRATO.md permits stroked line/polyline,
    //  flat fills and rounded rects, nothing else), so the approved design's filled
    //  SVG <polygon> tear becomes a stroked zigzag "tear line" instead of
    //  hundreds of small triangle objects.
    void buildTear()
    {
        lv_obj_t *mecanismo = lv_obj_create(m_root);
        lv_obj_set_pos(mecanismo, 0, 308);
        lv_obj_set_size(mecanismo, 480, 12);
        lv_obj_set_style_bg_color(mecanismo, lv_color_hex(S15_MECANISMO), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(mecanismo, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(mecanismo, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(mecanismo, 0, LV_PART_MAIN);
        lv_obj_clear_flag(mecanismo, LV_OBJ_FLAG_SCROLLABLE);

        constexpr int kTeeth = 16;
        constexpr int kW = 480;
        constexpr int kH = 12;
        int n = 0;
        m_tearPts[n++] = {0, 0};
        for (int i = 1; i < kTeeth; i++) {
            lv_coord_t x = (lv_coord_t) (i * kW / kTeeth);
            lv_coord_t y = (i % 2 == 0) ? (lv_coord_t) (kH * 0.4f) : (lv_coord_t) kH;
            m_tearPts[n++] = {x, y};
        }
        m_tearPts[n++] = {kW, 0};

        lv_obj_t *tear = lv_line_create(m_root);
        lv_obj_set_pos(tear, 0, 300);
        lv_obj_set_style_line_color(tear, lv_color_hex(S15_PAPER), LV_PART_MAIN);
        lv_obj_set_style_line_width(tear, 3, LV_PART_MAIN);
        lv_obj_set_style_line_rounded(tear, true, LV_PART_MAIN);
        lv_line_set_points(tear, m_tearPts, n);
    }

    lv_obj_t *m_root = nullptr;
    lv_obj_t *m_timeLbl = nullptr;
    lv_obj_t *m_values[6] = {};
    lv_obj_t *m_noteLbl = nullptr;
    lv_obj_t *m_curLbl = nullptr;
    lv_obj_t *m_heroLbl = nullptr;
    lv_point_t m_rulePts[2] = {};
    lv_point_t m_tearPts[18] = {};
};

S15ContaDeLuzScreen s_instance;

} // namespace

Screen *screenGetS15ContaDeLuz()
{
    return &s_instance;
}
