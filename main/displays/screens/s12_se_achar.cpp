// Ported from the approved design to LVGL 8.3 (DISPLAY_PROFILE_
// YYSLUPING_480X320). "Se achar": the block reward as a typographic poster,
// not a dashboard -- flat Bitcoin orange, one huge number, the honest odds
// printed right underneath so the dramatic number never hides the truth.
//
// Before this pass: a placeholder
// with a plain dark-gray screenMakeRoot and one centered "12 Se achar (em
// construcao)" label -- no palette, no UiState reads, update a no-op. Only
// there so the screen already existed in the rotation/build/simulator before
// the visual port started. Preserved factory name: screenGetS12SeAchar.
//
// After: the approved design's own flat Bitcoin-orange poster palette, a new
// "Anton" bitmap cut for the reward hero number + "BTC" unit tag, and the
// honesty block + footer in the Archivo cuts already shared by every other
// ported screen. Every simplification made against the approved design (most
// importantly: NO R$ value -- a deliberate product decision, this build only ever
// shows BTC) and the anti-overlap layout choices (the approved design's own
// diagonal "cut corner" composition is exactly the kind of collision this port
// was told to avoid).

#include "../fonts/fonts.h"
#include "../ui_state.h"
#include "nq_fmt.h"
#include "screen.h"
#include "screen_common.h"

#include <cmath>
#include <cstdio>

// ---- new fonts for this screen only: "Anton", glyph budget deliberately
// minimal per the task's explicit instruction -- digits + comma/period/ hyphen
// (for the "--" no-data fallback) for the hero reward number, and just the 3
// letters of "BTC" for the smaller unit tag. Declared locally (not in
// main/displays/fonts/fonts.h, which is this task's off-limits shared
// contract, edited by other agents porting 07/10/11 in parallel) -- same
// pattern s07_sorte.cpp/s10_halving.cpp already use for their own new/ reused-
// but-undeclared fonts. LV_FONT_DECLARE expands to a plain `extern const
// lv_font_t.;`; no extern "C" wrapper is needed here (a non- template global
// variable declaration is not name-mangled by the Itanium C++ ABI the same way
// an overloadable function would be), matching s07_sorte.cpp's own un-wrapped
// LV_FONT_DECLARE usage.
LV_FONT_DECLARE(font_s12_anton_118); // hero reward number, 118px, digits+,.- only (~46.7 KB)
LV_FONT_DECLARE(font_s12_anton_56);  // "BTC" unit tag, 56px, B/T/C only (~6.6 KB)

namespace
{

// ---- 12-se-achar's own palette (the approved design) --
constexpr uint32_t S12_BG = 0xf2a900;         // flat Bitcoin orange, edge to edge
constexpr uint32_t S12_INK = 0x14110b;        // near-black ink (hero number, top line, footer)
constexpr uint32_t S12_HONEST_BG = 0xfff3da;  // honesty block background (the approved design's.s12-honest)
constexpr uint32_t S12_HONEST_INK = 0x3a2f14; // honesty block text

class S12SeAcharScreen : public Screen {
  public:
    int number() const override
    {
        return 12;
    }
    const char *name() const override
    {
        return "se_achar";
    }

    void create(lv_obj_t *parent) override
    {
        if (m_root) {
            destroy();
        }
        m_root = screenMakeRoot(parent, S12_BG);

        //   Top context line: short, sentence case (no CAIXA ALTA banner --
        //   CONTRATO.md explicitly flags all-caps labels "acima de tudo" as a
        //   generated-screen tic to avoid). Sets up the huge number below as
        //   the answer to "quanto".
        screenMakeLabel(m_root, "the reward, if it happens", 16, 14, 448, &font_archivo_bold_20, S12_INK);

        //   Hero: reward in BTC, huge Anton digits. The approved design places the
        //   "BTC" unit diagonally overlapping the number's own bounding box
        //   (left:262 top:102 against a number starting at left:-4 top:-4) --
        //   exactly the composition the task brief calls out as the approved design's
        //   own historical overlap defect. Ported as a STACKED layout instead
        //   (unit directly under the number, both left-aligned): this makes
        //   the unit's position independent of how wide the number itself
        //   renders, so no realistic reward value can ever make the two
        //   collide -- see the.md write-up for the worst-case width math
        //   (font_s12_anton_118's own adv_w table).
        m_hero = screenMakeLabel(m_root, "--", 14, 36, 400, &font_s12_anton_118, S12_INK);
        m_unit = screenMakeLabel(m_root, "BTC", 16, 160, 200, &font_s12_anton_56, S12_INK);

        //   Honesty block: pale card over the orange field, matching the
        //   the approved design's.s12-honest. Two short pt-BR sentences, normal case.
        m_honestBox = lv_obj_create(m_root);
        lv_obj_set_pos(m_honestBox, 16, 214);
        lv_obj_set_size(m_honestBox, 448, 62);
        lv_obj_set_style_bg_color(m_honestBox, lv_color_hex(S12_HONEST_BG), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(m_honestBox, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(m_honestBox, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(m_honestBox, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(m_honestBox, 8, LV_PART_MAIN);
        lv_obj_clear_flag(m_honestBox, LV_OBJ_FLAG_SCROLLABLE);

        //  Both lines kept short enough to stay on ONE row each at their fixed 436px
        //  width (checked against font_archivo_medium_17's / font_archivo_regular_15's
        //  own worst-case adv_w sums -- see the.md write-up), so the box's fixed 62px
        //  height never depends on runtime text wrapping.
        m_line1 = screenMakeLabel(m_honestBox, "", 6, 4, 436, &font_archivo_medium_17, S12_HONEST_INK);
        screenMakeLabel(m_honestBox, "every block is a new draw", 6, 28, 436, &font_archivo_regular_15, S12_HONEST_INK);

        //   Footer: uptime-derived facts. Neither depends on netValid (uptimeS
        //   is local to the device, not the network/lottery block), so these
        //   show real numbers even when the network fields above fall back to
        //   "--".
        m_footLeft = screenMakeLabel(m_root, "", 16, 290, 300, &font_archivo_regular_15, S12_INK);
        m_footRight = screenMakeLabel(m_root, "", 244, 290, 220, &font_archivo_regular_15, S12_INK, LV_TEXT_ALIGN_RIGHT);
    }

    void update(const UiState &s) override
    {
        if (!m_root) {
            return;
        }

        //   ---- hero: reward in BTC only. A deliberate product decision: this
        //   build NEVER shows a R$ conversion, even though the approved design
        //   does -- simplification kept
        //   deliberately, not a missing feature. rewardBtc lives in the
        //   "network / lottery" section of UiState, gated by netValid same as
        //   the approved design's odds.rewardBtc; NAN (netValid == false) shows "--",
        //   never an invented number.
        if (std::isnan(s.rewardBtc)) {
            lv_label_set_text(m_hero, "--");
        } else {
            char buf[48];
            nqfmt::num(buf, sizeof(buf), s.rewardBtc, 3);
            lv_label_set_text(m_hero, buf);
        }

        //   ---- honesty line 1: expected wait, in years. Same netValid gate.
        if (std::isnan(s.expectedYears)) {
            lv_label_set_text(m_line1, "average wait: -- (no network data)");
        } else {
            char yearsBuf[48], line[96];
            nqfmt::n0(yearsBuf, sizeof(yearsBuf), (float) s.expectedYears);
            snprintf(line, sizeof(line), "average wait: %s years", yearsBuf);
            lv_label_set_text(m_line1, line);
        }

        //   ---- footer: uptime-derived, always real (never gated by
        //   netValid). "days mining" = floor(uptimeS / 86400); "draws
        //   played" = floor(uptimeS / 600) -- each block is a ~10 min draw,
        //   per the task brief.
        if (std::isnan(s.uptimeS)) {
            lv_label_set_text(m_footLeft, "mining for --");
            lv_label_set_text(m_footRight, "-- draws played");
        } else {
            float days = std::floor(s.uptimeS / 86400.0f);
            float draws = std::floor(s.uptimeS / 600.0f);
            char daysBuf[48], drawsBuf[48], left[96], right[96];
            nqfmt::n0(daysBuf, sizeof(daysBuf), days);
            nqfmt::n0(drawsBuf, sizeof(drawsBuf), draws);
            snprintf(left, sizeof(left), "mining for %s days", daysBuf);
            snprintf(right, sizeof(right), "%s draws played", drawsBuf);
            lv_label_set_text(m_footLeft, left);
            lv_label_set_text(m_footRight, right);
        }
    }

    void destroy() override
    {
        if (m_root) {
            lv_obj_del(m_root);
            m_root = nullptr;
            m_hero = nullptr;
            m_unit = nullptr;
            m_honestBox = nullptr;
            m_line1 = nullptr;
            m_footLeft = nullptr;
            m_footRight = nullptr;
        }
    }

  private:
    lv_obj_t *m_root = nullptr;
    lv_obj_t *m_hero = nullptr;
    lv_obj_t *m_unit = nullptr;
    lv_obj_t *m_honestBox = nullptr;
    lv_obj_t *m_line1 = nullptr;
    lv_obj_t *m_footLeft = nullptr;
    lv_obj_t *m_footRight = nullptr;
};

S12SeAcharScreen s_instance;

} // namespace

Screen *screenGetS12SeAchar()
{
    return &s_instance;
}
