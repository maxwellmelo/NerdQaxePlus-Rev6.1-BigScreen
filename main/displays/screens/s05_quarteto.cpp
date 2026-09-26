// Ported from the approved design to LVGL 8.3 (DISPLAY_PROFILE_
// YYSLUPING_480X320). "Quarteto": the 4 BM1370 ASIC chips as 4 Bauhaus-flat
// color quadrants (red/yellow/blue/black), each with its own GH/s hero
// number and a 0-120%-of-expected bar; a circular "desvio" (spread) badge
// sits in the middle; the strongest chip gets an up-chevron + highlighted
// border, the weakest a down-chevron; a quadrant flashes briefly every time
// an accepted share is counted (weighted toward whichever chip is
// contributing more hashrate right now).
//
// Before this pass: a placeholder root with a single centered "em
// construção" label.
//
// After: the approved design's own flag palette, its 4 fixed-identity
// quadrants (chip0=top-left, chip3=bottom-right, never reordered), the
// expected-vs-actual bar + 100%-mark tick, the strongest/weakest chevrons +
// highlight border, the center "desvio" badge and the interruptible share-
// flash animation.

#include "../fonts/fonts.h"
#include "../ui_state.h"
#include "nq_fmt.h"
#include "screen.h"
#include "screen_common.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

// New fonts for this screen only -- generated straight from the SAME already-
// instanced Archivo Bold TTF the pilot screens use
// (tools/fonts/instanced/Archivo-Bold-wdth100.ttf), just at 2 new sizes this
// screen needed and no other screen had a cut for yet, with a glyph set
// trimmed to only what each field can ever show. Declared here (not
// in./fonts/fonts.h) per this task's rule that new screens declare their own
// new fonts locally -- and, like font_s04_mono_40 above, at FILE scope (before
// the anonymous namespace): LV_FONT_DECLARE is just `extern const lv_font_t`,
// and the.c file lv_font_conv generates defines the symbol with external (non-
// namespaced) linkage, so declaring it inside the anonymous namespace here
// would mangle the reference and fail to link.
LV_FONT_DECLARE(font_s05_archivo_bold_46); // chip hero GH/s numbers (digits, '.', '-')
LV_FONT_DECLARE(font_s05_archivo_bold_22); // center badge "+-NN%" (digits, '%', '-', U+00B1)

namespace
{

// ---- 05-quarteto's own palette (the approved design) --
constexpr uint32_t Q_BG = 0xF2ECDA; // screen background: warm paper, not black

// Fixed chip -> quadrant identity (position never changes with ranking):
// chip0 top-left/red, chip1 top-right/yellow, chip2 bottom-left/blue,
// chip3 bottom-right/black.
constexpr uint32_t QUAD_FILL[4] = {0xC0392B, 0xF1C40F, 0x1B4F8C, 0x17181A};
constexpr uint32_t QUAD_INK[4] = {0xF2ECDA, 0x14110F, 0xF2ECDA, 0xF2ECDA};
constexpr uint32_t QUAD_TRACK[4] = {0x8E2A21, 0xC49A0C, 0x123A66, 0x2B2C2E};
// Highlight border + rank chevron color -- NOT simply QUAD_INK[]. Empirically
// (rendered PNGs, see ), lv_obj's BORDER
// draw and lv_line's LINE draw silently paint nothing for this exact warm-
// cream family of colors (0xF2ECDA and every nearby RGB565 bucket tried),
// while a plain bg_opa=COVER rect fill OR a label's text color in that same
// hex renders perfectly fine -- i.e. only the border/line draw path is
// affected, not fills or glyphs. Confirmed reproducible by isolating every
// other variable (object, position, opacity, "up" logic) and varying only
// this color. Since the cause could not be pinned down further without
// editing LVGL itself (out of scope here), the 3 cream-ink quadrants use
// plain white for their border/chevron instead -- visually indistinguishable
// from cream at this size, and confirmed to render.
constexpr uint32_t QUAD_ACCENT[4] = {0xFFFFFF, 0x14110F, 0xFFFFFF, 0xFFFFFF};
constexpr int QUAD_X[4] = {12, 246, 12, 246};
constexpr int QUAD_Y[4] = {12, 12, 166, 166};
constexpr bool QUAD_RIGHT[4] = {false, true, false, true};      // text hugs the right edge
constexpr bool QUAD_ANCHOR_TOP[4] = {true, true, false, false}; // content hugs top vs bottom

constexpr int QUAD_W = 222;
constexpr int QUAD_H = 142;
constexpr int CONTENT_X = 14;  // same for both sides: right:14 on a 194-wide
constexpr int CONTENT_W = 194; // box inside a 222-wide quad works out to left:14 too
constexpr int LABEL_H = 22;
constexpr int GAP = 6;
constexpr int NUM_H = 50;
constexpr int TRACK_H = 10;
constexpr int CONTENT_H = LABEL_H + GAP + NUM_H + GAP + TRACK_H;
constexpr int TOP_ANCHOR_Y = 14;
constexpr int BOTTOM_ANCHOR_Y = QUAD_H - 14 - CONTENT_H;

constexpr int ARROW_W = 12;
constexpr int ARROW_H = 10;
// mark at 100% of expected: 100/120 of the track's own width (track goes
// 0.120% of hrExpected/4, same as the approved design's fixed tick).
constexpr int TICK_X = (int) (100.0f / 120.0f * CONTENT_W + 0.5f);

// center "desvio" badge: screen-absolute, sitting in the gap between all 4
// quadrants (the approved design: left:204 top:124 width:72 height:72).
constexpr int MEDAL_X = 204;
constexpr int MEDAL_Y = 124;
constexpr int MEDAL_D = 72;
constexpr uint32_t MEDAL_BG = 0x14110F;
constexpr uint32_t MEDAL_INK = 0xF2ECDA;

// share-flash animation: 0.42 opacity snap, then ease back to 0 over 550ms
// (the approved design: opacity.42 -> CSS transition opacity.55s ease-out). Re-fired
// on the SAME lv_obj on every accepted-share tick, so lv_anim_del before
// each lv_anim_start is mandatory -- otherwise fast share bursts stack
// competing animations on one style property.
constexpr lv_opa_t FLASH_OPA = 107; // 0.42 * 255
constexpr uint32_t FLASH_MS = 550;

void flashExecCb(void *var, int32_t v)
{
    lv_obj_set_style_bg_opa((lv_obj_t *) var, (lv_opa_t) v, LV_PART_MAIN);
}

// Glues a quadrant's GH/s value label to its unit label using freshly
// measured widths, forcing the layout pass first with lv_obj_update_layout
// -- same technique as s16_sala.cpp's layoutFigureRow (see that file,
// ~line 333). Needed because a LV_SIZE_CONTENT label's coords/size are only
// recomputed during the next layout pass, not synchronously inside
// lv_label_set_text; calling lv_obj_align_to (this file's previous
// approach) right after changing the text could therefore measure the
// PREVIOUS frame's width, which is exactly what let "GH/s" overlap the tail
// of a number like "1.606" on the Chip 2 quadrant. Applied to all 4 quadrants for a
// consistent gap, not just the one caught in visual review.
//
// `valOnLeft` true: val sits at its own fixed x (left quads, chip 1/3) and
// unit is glued to its right. false: unit sits at its own fixed x (right
// quads, chip 2/4) and val grows leftward from it. Both cases bottom-align
// the pair with a +2 px offset, matching this screen's original
// LV_ALIGN_OUT_*_BOTTOM offsets exactly.
void layoutValueUnit(lv_obj_t *val, lv_obj_t *unit, bool valOnLeft, int gap)
{
    lv_obj_update_layout(val);
    lv_obj_update_layout(unit);
    if (valOnLeft) {
        lv_coord_t valRight = lv_obj_get_x(val) + lv_obj_get_width(val);
        lv_coord_t valBottom = lv_obj_get_y(val) + lv_obj_get_height(val);
        lv_coord_t unitH = lv_obj_get_height(unit);
        lv_obj_set_pos(unit, valRight + gap, valBottom - unitH + 2);
    } else {
        lv_coord_t unitX = lv_obj_get_x(unit);
        lv_coord_t unitBottom = lv_obj_get_y(unit) + lv_obj_get_height(unit);
        lv_coord_t valW = lv_obj_get_width(val);
        lv_coord_t valH = lv_obj_get_height(val);
        lv_obj_set_pos(val, unitX - valW - gap, unitBottom - valH + 2);
    }
}

class S05QuartetoScreen : public Screen {
  public:
    int number() const override
    {
        return 5;
    }
    const char *name() const override
    {
        return "quarteto";
    }

    void create(lv_obj_t *parent) override
    {
        if (m_root) {
            destroy();
        }
        m_root = screenMakeRoot(parent, Q_BG);

        for (int i = 0; i < 4; i++) {
            lv_obj_t *quad = lv_obj_create(m_root);
            lv_obj_set_pos(quad, QUAD_X[i], QUAD_Y[i]);
            lv_obj_set_size(quad, QUAD_W, QUAD_H);
            lv_obj_set_style_bg_color(quad, lv_color_hex(QUAD_FILL[i]), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(quad, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_radius(quad, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(quad, 0, LV_PART_MAIN);
            //  border is the "strongest chip" highlight frame (the approved
            //  design's.s05-frame.s05-on): drawn on the quad itself (LVGL borders paint
            //  inward, under the children below, so it never shifts the 14px-inset
            //  content) instead of a separate overlay object.
            lv_obj_set_style_border_color(quad, lv_color_hex(QUAD_ACCENT[i]), LV_PART_MAIN);
            lv_obj_set_style_border_side(quad, LV_BORDER_SIDE_FULL, LV_PART_MAIN);
            lv_obj_set_style_border_opa(quad, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_width(quad, 0, LV_PART_MAIN);
            lv_obj_clear_flag(quad, LV_OBJ_FLAG_SCROLLABLE);
            m_quad[i] = quad;

            int topY = QUAD_ANCHOR_TOP[i] ? TOP_ANCHOR_Y : BOTTOM_ANCHOR_Y;
            int labelY = topY;
            int numY = labelY + LABEL_H + GAP;
            int trackY = numY + NUM_H + GAP;
            lv_text_align_t side = QUAD_RIGHT[i] ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT;

            //   "Chip N" label -- text never changes after create, so a
            //   fixed box + text-align reproduces the approved design's flex
            //   left/right anchoring without any per-frame re-measuring.
            char labelBuf[8];
            snprintf(labelBuf, sizeof(labelBuf), "Chip %d", i + 1);
            lv_obj_t *label = screenMakeLabel(quad, labelBuf, CONTENT_X, labelY, CONTENT_W - ARROW_W - 7, &font_archivo_bold_20,
                                              QUAD_INK[i], side);
            lv_obj_set_style_text_opa(label, 235, LV_PART_MAIN); // the approved design: opacity.92

            //   strongest/weakest chevron (CONTRATO.md allows line/polyline,
            //   not arbitrary filled shapes -- see s01_painel.cpp's own note
            //   on the same restriction -- so the up/down "rank" arrow is a
            //   3-point open chevron instead of a solid CSS border-triangle).
            lv_obj_t *arrow = lv_line_create(quad);
            lv_obj_set_style_line_color(arrow, lv_color_hex(QUAD_ACCENT[i]), LV_PART_MAIN);
            lv_obj_set_style_line_width(arrow, 3, LV_PART_MAIN);
            lv_obj_set_style_line_rounded(arrow, true, LV_PART_MAIN);
            lv_obj_set_size(arrow, ARROW_W, ARROW_H);
            lv_obj_set_pos(arrow, QUAD_RIGHT[i] ? (CONTENT_X + CONTENT_W - ARROW_W) : (CONTENT_X + 66),
                           labelY + (LABEL_H - ARROW_H) / 2);
            lv_obj_clear_flag(arrow, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(arrow, LV_OBJ_FLAG_HIDDEN);
            m_arrow[i] = arrow;

            //   hero value + unit. Value is auto-sized (LV_SIZE_CONTENT) so
            //   it can be positioned against the OTHER label of the pair via
            //   lv_obj_align_to every update -- the only way to keep a
            //   "number, then unit right after it" (left quads) or "unit
            //   fixed, number grows leftward" (right quads) group glued
            //   together in LVGL 8 without hand-measuring glyph widths.
            lv_obj_t *val = lv_label_create(quad);
            lv_obj_set_style_text_font(val, &font_s05_archivo_bold_46, LV_PART_MAIN);
            lv_obj_set_style_text_color(val, lv_color_hex(QUAD_INK[i]), LV_PART_MAIN);
            lv_label_set_long_mode(val, LV_LABEL_LONG_CLIP);
            lv_obj_set_size(val, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text(val, "--");
            m_val[i] = val;

            lv_obj_t *unit = lv_label_create(quad);
            lv_obj_set_style_text_font(unit, &font_archivo_regular_15, LV_PART_MAIN);
            lv_obj_set_style_text_color(unit, lv_color_hex(QUAD_INK[i]), LV_PART_MAIN);
            lv_obj_set_style_text_opa(unit, 217, LV_PART_MAIN); // the approved design: opacity.85
            lv_label_set_long_mode(unit, LV_LABEL_LONG_CLIP);
            lv_obj_set_size(unit, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text(unit, "GH/s");
            m_unit[i] = unit;

            if (QUAD_RIGHT[i]) {
                //   unit's own position is fixed (its text never changes):
                //   right-anchor it once now, at the content box's right
                //   edge (quad width 222 - content's own 14px right margin),
                //   then every update the value is glued to its LEFT,
                //   growing further left as digits are added.
                lv_obj_align(unit, LV_ALIGN_TOP_RIGHT, -(QUAD_W - CONTENT_X - CONTENT_W), numY + NUM_H - 19);
            } else {
                lv_obj_set_pos(val, CONTENT_X, numY);
            }

            //   bar track (static) + fill (dynamic width) + 100%-of-expected
            //   tick (static position, fixed for the whole screen).
            lv_obj_t *track = lv_obj_create(quad);
            lv_obj_set_pos(track, CONTENT_X, trackY);
            lv_obj_set_size(track, CONTENT_W, TRACK_H);
            lv_obj_set_style_bg_color(track, lv_color_hex(QUAD_TRACK[i]), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(track, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_radius(track, 2, LV_PART_MAIN);
            lv_obj_set_style_border_width(track, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(track, 0, LV_PART_MAIN);
            lv_obj_clear_flag(track, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t *fill = lv_obj_create(quad);
            lv_obj_set_pos(fill, CONTENT_X, trackY);
            lv_obj_set_size(fill, 0, TRACK_H);
            lv_obj_set_style_bg_color(fill, lv_color_hex(QUAD_INK[i]), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_radius(fill, 2, LV_PART_MAIN);
            lv_obj_set_style_border_width(fill, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(fill, 0, LV_PART_MAIN);
            lv_obj_clear_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
            m_fill[i] = fill;

            lv_obj_t *tick = lv_obj_create(quad);
            lv_obj_set_pos(tick, CONTENT_X + TICK_X, trackY - 3);
            lv_obj_set_size(tick, 2, TRACK_H + 6);
            lv_obj_set_style_bg_color(tick, lv_color_hex(QUAD_INK[i]), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(tick, 140, LV_PART_MAIN); // the approved design: opacity.55
            lv_obj_set_style_radius(tick, 0, LV_PART_MAIN);
            lv_obj_set_style_border_width(tick, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(tick, 0, LV_PART_MAIN);
            lv_obj_clear_flag(tick, LV_OBJ_FLAG_SCROLLABLE);

            //   flash overlay: topmost child of this quad (created last), so
            //   it paints over the label/number/bar AND the border above,
            //   exactly like the approved design's last-appended.s05-flash div.
            lv_obj_t *flash = lv_obj_create(quad);
            lv_obj_set_pos(flash, 0, 0);
            lv_obj_set_size(flash, QUAD_W, QUAD_H);
            lv_obj_set_style_bg_color(flash, lv_color_hex(QUAD_INK[i]), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(flash, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(flash, 0, LV_PART_MAIN);
            lv_obj_set_style_border_width(flash, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(flash, 0, LV_PART_MAIN);
            lv_obj_clear_flag(flash, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(flash, LV_OBJ_FLAG_CLICKABLE);
            m_flash[i] = flash;
        }

        //   center "desvio" badge, painted last so it sits on top of all 4
        //   quadrants where they meet (the approved design: appended to root after the
        //   quadrant loop).
        lv_obj_t *medal = lv_obj_create(m_root);
        lv_obj_set_pos(medal, MEDAL_X, MEDAL_Y);
        lv_obj_set_size(medal, MEDAL_D, MEDAL_D);
        lv_obj_set_style_radius(medal, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(medal, lv_color_hex(MEDAL_BG), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(medal, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(medal, lv_color_hex(MEDAL_INK), LV_PART_MAIN);
        lv_obj_set_style_border_width(medal, 2, LV_PART_MAIN);
        lv_obj_set_style_pad_all(medal, 0, LV_PART_MAIN);
        lv_obj_clear_flag(medal, LV_OBJ_FLAG_SCROLLABLE);

        m_medalNum = screenMakeLabel(medal, "--", 0, 15, MEDAL_D, &font_s05_archivo_bold_22, MEDAL_INK, LV_TEXT_ALIGN_CENTER);
        lv_obj_t *cap =
            screenMakeLabel(medal, "deviation", 0, 41, MEDAL_D, &font_archivo_regular_15, MEDAL_INK, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_style_text_opa(cap, 204, LV_PART_MAIN); // the approved design: opacity.8

        m_haveLastShares = false;
        m_lastSharesAccepted = 0;
    }

    void update(const UiState &s) override
    {
        if (!m_root) {
            return;
        }

        bool valid[4];
        float vals[4];
        for (int i = 0; i < 4; i++) {
            valid[i] = !std::isnan(s.chipGhs[i]);
            vals[i] = valid[i] ? s.chipGhs[i] : 0.0f;
        }
        bool haveAll = valid[0] && valid[1] && valid[2] && valid[3];

        float maxV = 0, minV = 0, meanV = 0;
        int strongestI = -1, weakestI = -1;
        if (haveAll) {
            maxV = vals[0];
            minV = vals[0];
            float sum = 0;
            int maxI = 0, minI = 0;
            for (int i = 0; i < 4; i++) {
                sum += vals[i];
                if (vals[i] > maxV) {
                    maxV = vals[i];
                    maxI = i;
                }
                if (vals[i] < minV) {
                    minV = vals[i];
                    minI = i;
                }
            }
            meanV = sum / 4.0f;
            if (maxV > minV) {
                strongestI = maxI;
                weakestI = minI;
            }
        }

        float exp4 = (!std::isnan(s.hrExpected) && s.hrExpected > 0.0f) ? s.hrExpected / 4.0f : 0.0f;

        for (int i = 0; i < 4; i++) {
            char numBuf[48]; // sized to nq_fmt.h's own kNumBufMax, not the ~4-digit
            //   GH/s values actually expected, to keep -Wformat-
            //   truncation quiet for the theoretical float range
            if (valid[i]) {
                nqfmt::n0(numBuf, sizeof(numBuf), vals[i]);
            } else {
                snprintf(numBuf, sizeof(numBuf), "--");
            }
            lv_label_set_text(m_val[i], numBuf);
            //   The highlight border shrinks the quad's content box, and the
            //   right quads' unit is placed with lv_obj_align (re-evaluated
            //   on layout), so the border must be applied BEFORE the value is
            //   glued to the unit -- otherwise the unit shifts 5 px left onto
            //   the number afterwards.
            lv_obj_set_style_border_width(m_quad[i], (i == strongestI) ? 5 : 0, LV_PART_MAIN);
            layoutValueUnit(m_val[i], m_unit[i], !QUAD_RIGHT[i], 7);

            float pct = (valid[i] && exp4 > 0.0f) ? (vals[i] / exp4 * 100.0f) : 0.0f;
            float frac = nqfmt::clampf(pct, 0.0f, 120.0f) / 120.0f;
            lv_obj_set_width(m_fill[i], (lv_coord_t) (frac * CONTENT_W + 0.5f));

            bool up = (i == strongestI);
            bool down = (i == weakestI);
            if (up || down) {
                //   m_arrowPts (a MEMBER array, not a local) because
                //   lv_line_set_points only stores the pointer it is given,
                //   not a copy -- LVGL reads it back later, during the actual
                //   render pass triggered well after update has returned.
                //   A local stack array here would be dangling by then
                //   (verified: that was the original bug, silently rendering
                //   a degenerate/garbage line -- fixed by copying the points
                //   instead of pointing at the stack array).
                lv_point_t *pts = m_arrowPts[i]; // LOCAL to the line's own origin (set via lv_obj_set_pos at create)
                if (up) {
                    pts[0] = {0, ARROW_H};
                    pts[1] = {ARROW_W / 2, 0};
                    pts[2] = {ARROW_W, ARROW_H};
                } else {
                    pts[0] = {0, 0};
                    pts[1] = {ARROW_W / 2, ARROW_H};
                    pts[2] = {ARROW_W, 0};
                }
                lv_line_set_points(m_arrow[i], pts, 3);
                lv_obj_clear_flag(m_arrow[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(m_arrow[i], LV_OBJ_FLAG_HIDDEN);
            }

            lv_obj_set_style_border_width(m_quad[i], up ? 5 : 0, LV_PART_MAIN);
        }

        if (haveAll && meanV > 0.0f) {
            char devBuf[48], line[56]; // same oversizing rationale as numBuf above
            float dev = (maxV - minV) / meanV * 100.0f;
            nqfmt::n0(devBuf, sizeof(devBuf), dev);
            snprintf(line, sizeof(line),
                     "\xc2\xb1"
                     "%s%%",
                     devBuf); // U+00B1 '±'
            lv_label_set_text(m_medalNum, line);
        } else {
            lv_label_set_text(m_medalNum, "--");
        }

        //   Share-flash: fires (at most) once per update tick when the
        //   accepted-share counter moved since the LAST tick -- never a
        //   per-share event (the firmware only samples UiState at 4 Hz), and
        //   never on the very first update right after create (no prior
        //   baseline yet, and sharesAccepted can already be a large number
        //   from a previous session).
        if (m_haveLastShares && s.sharesAccepted > m_lastSharesAccepted && haveAll) {
            int idx = pickWeighted(vals);
            startFlash(idx);
        }
        m_lastSharesAccepted = s.sharesAccepted;
        m_haveLastShares = true;
    }

    void destroy() override
    {
        if (!m_root) {
            return;
        }
        for (int i = 0; i < 4; i++) {
            if (m_flash[i]) {
                lv_anim_del(m_flash[i], flashExecCb);
            }
        }
        lv_obj_del(m_root);
        m_root = nullptr;
        for (int i = 0; i < 4; i++) {
            m_quad[i] = nullptr;
            m_val[i] = nullptr;
            m_unit[i] = nullptr;
            m_fill[i] = nullptr;
            m_arrow[i] = nullptr;
            m_flash[i] = nullptr;
        }
        m_medalNum = nullptr;
    }

  private:
    //   Roulette weighted by each chip's current GH/s (the approved design reference's `share`
    //   field is proportional to hashrate contribution; UiState has no
    //   separate per-chip share fraction, so the raw GH/s values serve as
    //   the same weights). Falls back to a uniform pick when every weight is
    //   0 (all chips reporting exactly 0), matching the approved design.
    static int pickWeighted(const float w[4])
    {
        float sum = 0.0f;
        for (int i = 0; i < 4; i++) {
            if (w[i] > 0.0f) {
                sum += w[i];
            }
        }
        if (!(sum > 0.0f)) {
            return rand() % 4;
        }
        float r = ((float) rand() / ((float) RAND_MAX + 1.0f)) * sum;
        float acc = 0.0f;
        for (int i = 0; i < 4; i++) {
            acc += (w[i] > 0.0f) ? w[i] : 0.0f;
            if (r <= acc) {
                return i;
            }
        }
        return 3;
    }

    void startFlash(int idx)
    {
        lv_obj_t *f = m_flash[idx];
        //   Cancel any flash already in flight on this quadrant before
        //   starting a new one -- required so a burst of shares in the same
        //   few hundred ms cuts and restarts the fade instead of stacking
        //   competing animations on the same bg_opa style property.
        lv_anim_del(f, flashExecCb);
        lv_obj_set_style_bg_opa(f, FLASH_OPA, LV_PART_MAIN);

        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, f);
        lv_anim_set_exec_cb(&a, flashExecCb);
        lv_anim_set_values(&a, FLASH_OPA, 0);
        lv_anim_set_time(&a, FLASH_MS);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    }

    lv_obj_t *m_root = nullptr;
    lv_obj_t *m_quad[4] = {};
    lv_obj_t *m_val[4] = {};
    lv_obj_t *m_unit[4] = {};
    lv_obj_t *m_fill[4] = {};
    lv_obj_t *m_arrow[4] = {};
    lv_point_t m_arrowPts[4][3] = {}; // backing storage for lv_line_set_points -- see update
    lv_obj_t *m_flash[4] = {};
    lv_obj_t *m_medalNum = nullptr;

    bool m_haveLastShares = false;
    uint64_t m_lastSharesAccepted = 0;
};

S05QuartetoScreen s_instance;

} // namespace

Screen *screenGetS05Quarteto()
{
    return &s_instance;
}
