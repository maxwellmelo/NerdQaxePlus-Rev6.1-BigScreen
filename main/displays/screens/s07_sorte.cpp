// Ported from the approved design to LVGL 8.3 (DISPLAY_PROFILE_
// YYSLUPING_480X320). "Sorte": the screen IS a paper raffle ticket -- a
// serrated stub on the left with the block height as the ticket's "serial
// number", a solo-mining stamp, and the hero: "1 em X milhoes" (chance of
// finding a block today), the Mega-Sena comparison, the chance for the
// year, how many "tickets" (hashes) were played since midnight, and a
// decorative barcode drawn from the block height's own digits.
//
// Before this pass: a skeleton with one
// centered "em construcao" label, no palette, no data.
//
// After: the approved design's own paper/ink palette (cream #f1e6c8 / ink
// #2c2113 / stamp red #a3372b), its own hero ("1 em X milhoes", Saira
// Condensed Bold 108px reused from 01-painel -- no new font), a minimal brand-
// new typewriter font (Special Elite, 13px, ONLY the 10 uppercase glyphs the
// stamp text actually uses) for the "MINERANDO SOLO" stamp, and the barcode
// drawn as plain rectangles (no barcode font at all). Both the approved
// design's rotated stub text AND its rotated stamp text ended up as plain
// horizontal labels -- the stub because LVGL 8 has no vertical-writing-mode
// equivalent, the stamp because transform_angle on a label was tested here and
// made the label's own text stop drawing entirely (not just look bad) --
// one of several simplifications made against the approved design.

#include "../fonts/fonts.h"
#include "../ui_state.h"
#include "nq_fmt.h"
#include "screen.h"
#include "screen_common.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

// New-for-this-screen font: Special Elite, 13px (contract's own text-size
// floor -- "nada abaixo de 13px"), bitmap 4bpp, ONLY the 10 uppercase glyphs
// used by "MINERANDO" / "SOLO" (A D E I L M N O R S -- The exact lv_font_conv
// command and the resulting size). Declared here directly (LV_FONT_DECLARE)
// instead of in./fonts/fonts.h: that header is this task's fixed contract
// (off-limits to edit -- other agents are porting screens 10/11/12 in parallel
// and share it), so a screen-local extern is how a NEW font gets used without
// editing it, exactly like screen_common.h's own top-of-file note anticipates
// for screen-specific helpers.
LV_FONT_DECLARE(font_s07_special_elite_13);

namespace
{

// ---- 07-sorte's own palette (the approved design's CSS) --
constexpr uint32_t S07_BG = 0xf1e6c8;             // cream paper
constexpr uint32_t S07_INK = 0x2c2113;            // ink
constexpr uint32_t S07_MUTED = 0x6b5636;          // ink at ~82% opacity, baked into a solid color (LVGL 8 opa on
                                                  //  text is a per-object 0.255 value, not a CSS-style alpha blend against the
                                                  //  exact paper tone underneath -- a lighter ink tone is simpler and gives the
                                                  //  same "secondary text" read)
constexpr uint32_t S07_CANHOTO_BG = 0xe4d6ae;     // ticket stub background
constexpr uint32_t S07_CANHOTO_BORDER = 0xb8a874; // dashed tear line
constexpr uint32_t S07_CANHOTO_NUM = 0x8a6a22;    // stub serial-number ink
constexpr uint32_t S07_CARIMBO = 0xa3372b;        // stamp red

constexpr int CANHOTO_W = 60;
constexpr int MAIN_X = 76;
constexpr int MAIN_RIGHT = 468; // 480 - 12px safe margin
constexpr int MAIN_W = MAIN_RIGHT - MAIN_X;

constexpr int BARCODE_BARS = 10;
constexpr int BARCODE_X = MAIN_X;
constexpr int BARCODE_Y = 288;
constexpr int BARCODE_H = 20;
constexpr int BARCODE_GAP = 3;

// ---- pt-BR magnitude words for "hashes jogados hoje" ("bilhetes jogados"),
// mirrors the approved design's own local magText helper.
// Accented continuations are split into their own string-literal token
// (project convention, see e.g. s01_painel.cpp's "frequ" "\xc3\xaa" "ncia"):
// a bare \xNN hex escape swallows any hex-digit ASCII character that
// immediately follows it (e.g. "\xc3\xb5es" would misparse as one huge
// escape "\xc3\xb5e" + "s", because 'e' is itself a hex digit) -- ending the
// escape's own literal and starting a fresh one for the next plain text
// sidesteps that trap entirely instead of having to audit each case.
// English grammar note (fixed here after the display-reference designs review found
// "1 in 1.0 millions" / "78.7 quadrillions" on screen): when one of these
// magnitude words directly follows a specific numeral like this ("1.0
// million", "78.7 quadrillion"), it stays SINGULAR no matter the numeral's
// value -- unlike the partitive "millions of X" construction, which this
// screen never uses. So a single word per magnitude is enough; there is no
// plural form to pick between.
struct MagWord
{
    double v;
    const char *word;
};

const MagWord MAGS[] = {
    {1e18, "quintillion"}, {1e15, "quadrillion"}, {1e12, "trillion"}, {1e9, "billion"}, {1e6, "million"}, {1e3, "thousand"},
};
constexpr int MAG_COUNT = sizeof(MAGS) / sizeof(MAGS[0]);

void magText(char *buf, size_t n, double v)
{
    if (!(v > 0.0)) {
        snprintf(buf, n, "0");
        return;
    }
    for (int i = 0; i < MAG_COUNT; i++) {
        if (v >= MAGS[i].v) {
            double val = v / MAGS[i].v;
            char numBuf[nqfmt::kNumBufMax];
            nqfmt::n1(numBuf, sizeof(numBuf), (float) val);
            snprintf(buf, n, "%s %s", numBuf, MAGS[i].word);
            return;
        }
    }
    char numBuf[nqfmt::kNumBufMax];
    nqfmt::n0(numBuf, sizeof(numBuf), (float) v);
    snprintf(buf, n, "%s", numBuf);
}

// hashes played today = hr1m (GH/s) x seconds since local midnight, or since
// boot when the wall clock was never synced -- matches the approved design reference's
// hashesHojeTexto (state.hashrate.d1 there is actually 1-min-average
// hashrate re-read every tick, i.e. the same role UiState::hr1m plays here).
// GH/s -> H/s is x1e9. Computed in double: at ~6.5 TH/s over a full day this
// is ~5.6e17, comfortably exact in a double, and float would only cost
// precision on the display's last significant digit anyway (n1 rounds to
// 1 decimal of a "quintilhao"-scale word regardless).
void hashesHojeTexto(char *buf, size_t n, const UiState &s)
{
    if (std::isnan(s.hr1m)) {
        snprintf(buf, n, "--");
        return;
    }
    double secs;
    if (s.clockValid) {
        time_t t = (time_t) s.epochS;
        struct tm tmv;
        localtime_r(&t, &tmv);
        secs = tmv.tm_hour * 3600.0 + tmv.tm_min * 60.0 + tmv.tm_sec;
    } else {
        secs = std::isnan(s.uptimeS) ? 0.0 : (double) s.uptimeS;
    }
    double hs = (double) s.hr1m * 1e9 * secs;
    magText(buf, n, hs);
}

// Splits NQ.fmt.oneIn(p)'s "1 em X milhoes" into prefix ("1 em") / value
// ("X") / unit ("milhoes") so each piece can use its own font/size, the same
// separation the approved design's own oneInParts helper does. NAN or p<=0 -> "never"
// (never realistic on real hardware while mining, since hashrate > 0
// whenever oddsPerDay is valid at all -- kept only as the honest fallback
// the approved design itself has).
struct OneInParts
{
    bool never;
    char value[nqfmt::kNumBufMax];
    const char *unit;
};

void computeOneIn(double p, OneInParts &out)
{
    if (std::isnan(p) || !(p > 0.0)) {
        out.never = true;
        out.value[0] = '\0';
        out.unit = "";
        return;
    }
    out.never = false;
    double n = 1.0 / p;
    if (n >= 1e9) {
        nqfmt::n1(out.value, sizeof(out.value), (float) (n / 1e9));
        out.unit = "billion";
    } else if (n >= 1e6) {
        nqfmt::n1(out.value, sizeof(out.value), (float) (n / 1e6));
        out.unit = "million";
    } else if (n >= 1e3) {
        nqfmt::n0(out.value, sizeof(out.value), (float) (n / 1e3));
        out.unit = "thousand";
    } else {
        nqfmt::n0(out.value, sizeof(out.value), (float) n);
        out.unit = "";
    }
}

class S07SorteScreen : public Screen {
  public:
    int number() const override
    {
        return 7;
    }
    const char *name() const override
    {
        return "sorte";
    }

    void create(lv_obj_t *parent) override
    {
        if (m_root) {
            destroy();
        }
        m_root = screenMakeRoot(parent, S07_BG);

        //   ---- ticket stub (canhoto), left strip, full height ----------------
        lv_obj_t *canhoto = lv_obj_create(m_root);
        lv_obj_set_pos(canhoto, 0, 0);
        lv_obj_set_size(canhoto, CANHOTO_W, 320);
        lv_obj_set_style_bg_color(canhoto, lv_color_hex(S07_CANHOTO_BG), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(canhoto, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(canhoto, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(canhoto, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(canhoto, 0, LV_PART_MAIN);
        lv_obj_clear_flag(canhoto, LV_OBJ_FLAG_SCROLLABLE);

        //   Dashed "tear line" at the stub's right edge. The approved design's stub
        //   text is CSS `writing-mode:vertical-rl` (true vertical reading
        //   direction, not a rotated horizontal string) -- LVGL 8 has no
        //   equivalent layout mode, and rotating a whole horizontal label 90
        //   deg with transform_angle makes each CHARACTER sideways (readable
        //   only by tilting your head), which is a much worse result than the
        //   the approved design's actual vertical text. So the stub below is plain
        //   HORIZONTAL text (short label + a serial number sized to always
        //   fit on one line) instead -- this is the task's own sanctioned
        //   fallback. The
        //   carimbo (stamp) below was ALSO tried with transform_angle (a much
        //   cheaper, one-time use: 2 small static labels, no per-frame cost)
        //   and rejected too, but for a different, worse reason -- see the
        //   comment above the "MINERANDO"/"SOLO" labels below and
        //
        m_canhotoLine = lv_line_create(m_root);
        lv_obj_set_style_line_color(m_canhotoLine, lv_color_hex(S07_CANHOTO_BORDER), LV_PART_MAIN);
        lv_obj_set_style_line_width(m_canhotoLine, 2, LV_PART_MAIN);
        lv_obj_set_style_line_dash_width(m_canhotoLine, 4, LV_PART_MAIN);
        lv_obj_set_style_line_dash_gap(m_canhotoLine, 4, LV_PART_MAIN);
        m_canhotoLinePts[0] = {0, 0};
        m_canhotoLinePts[1] = {0, 320};
        lv_line_set_points(m_canhotoLine, m_canhotoLinePts, 2);
        lv_obj_set_pos(m_canhotoLine, CANHOTO_W, 0);

        screenMakeLabel(canhoto, "SERIES", 0, 132, CANHOTO_W, &font_archivo_medium_15, S07_INK, LV_TEXT_ALIGN_CENTER);
        //   Serial number: font_saira_condensed_semibold_24 (used until this
        //   fix) needs ~14-17px/digit -- a real 6-7 digit block height (e.g.
        //   967564, already past 900k as of 2026) is 90-115px wide, more than
        //   double the 56px column, so LV_LABEL_LONG_WRAP (the mode this label
        //   used to force) wrapped it mid-digit ("96756" / "4"), illegible and
        //   never caught before because the simulator's "normal" scenario
        //   didn't populate a real height yet. Fix: drop to
        //   font_saira_condensed_semibold_15 (already generated for 01-painel,
        //   no new font needed) instead of hand-splitting into deliberate
        //   lines -- its digit glyphs are ~4.5-7.1px wide (see the.adv_w
        //   table in font_saira_condensed_semibold_15.c), so even the
        //   worst-case 7-digit stretch "9999999" is ~50px, comfortably under
        //   the 56px column on ONE line. Long mode is left at
        //   screenMakeLabel's own default (LV_LABEL_LONG_CLIP, no wrap
        //   override) as a defensive backstop: if height ever grows past what
        //   fits (8+ digits, centuries away), it clips instead of silently
        //   wrapping mid-number again.
        m_canhotoNum = screenMakeLabel(canhoto, "--", 2, 158, CANHOTO_W - 4, &font_saira_condensed_semibold_15, S07_CANHOTO_NUM,
                                       LV_TEXT_ALIGN_CENTER);

        //   ---- top row: title + solo-mining stamp -----------------------------
        screenMakeLabel(m_root, "DAILY TICKET", MAIN_X, 10, 220, &font_archivo_bold_20, S07_INK);

        constexpr int CARIMBO_D = 78;
        lv_obj_t *carimbo = lv_obj_create(m_root);
        lv_obj_set_pos(carimbo, MAIN_RIGHT - CARIMBO_D, 4);
        lv_obj_set_size(carimbo, CARIMBO_D, CARIMBO_D);
        lv_obj_set_style_bg_opa(carimbo, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_color(carimbo, lv_color_hex(S07_CARIMBO), LV_PART_MAIN);
        lv_obj_set_style_border_width(carimbo, 3, LV_PART_MAIN);
        lv_obj_set_style_radius(carimbo, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_pad_all(carimbo, 0, LV_PART_MAIN);
        lv_obj_clear_flag(carimbo, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(carimbo, LV_OBJ_FLAG_CLICKABLE);

        //   The circle itself is radially symmetric, so it would look
        //   identical rotated or not -- only the two text lines inside would
        //   actually need to tilt to read as a crooked ink stamp, the same
        //   -8deg the approved design applies to the whole carimbo div. TESTED in this
        //   sim: lv_obj_set_style_transform_angle on either label (own
        //   pivot centered on the label's own small box, exactly as
        //   documented for LVGL 8 style-based rotation) does not just
        //   misplace or alias the glyphs -- it makes the label's own text
        //   stop being drawn AT ALL in this build (confirmed by toggling the
        //   angle 0 vs -80 with everything else identical: 0 shows the text,
        //   -80 shows an empty circle). That is a worse failure mode than the
        //   "serrilhado/lento" the task asks to watch for, so both stamp
        //   lines are plain horizontal text, matching the stub's own fallback
        //   above -- see
        screenMakeLabel(carimbo, "MINING", 0, 26, CARIMBO_D, &font_s07_special_elite_13, S07_CARIMBO, LV_TEXT_ALIGN_CENTER);
        screenMakeLabel(carimbo, "SOLO", 0, 40, CARIMBO_D, &font_s07_special_elite_13, S07_CARIMBO, LV_TEXT_ALIGN_CENTER);

        //   ---- hero: "1 em X milhoes" -----------------------------------------
        screenMakeLabel(m_root, "your chance of finding a block today", MAIN_X, 78, 340, &font_archivo_regular_15, S07_MUTED);

        //   Hero digits reuse 01-painel's already-generated 108px Saira
        //   Condensed Bold (line_height 92px, well above the >=64px
        //   hero-number rule) instead of generating a new size closer to the
        //   the approved design's 64px: zero extra flash cost, and the only tradeoff is a
        //   bigger-than-the approved design hero digit, not a smaller one -- see
        //
        m_heroPrefix = screenMakeLabel(m_root, "1 in", MAIN_X, 156, 66, &font_archivo_bold_20, S07_INK);
        m_heroNum =
            screenMakeLabel(m_root, "--", MAIN_X + 74, 98, MAIN_RIGHT - (MAIN_X + 74), &font_saira_condensed_bold_108, S07_INK);
        m_heroUnit = screenMakeLabel(m_root, "", MAIN_X, 192, MAIN_W, &font_archivo_regular_19, S07_INK);

        //   ---- compare / stats --------------------------------------------------
        m_compare = screenMakeLabel(m_root, "", MAIN_X, 216, MAIN_W, &font_archivo_regular_15, S07_INK);
        lv_label_set_long_mode(m_compare, LV_LABEL_LONG_WRAP);

        m_statsYear = screenMakeLabel(m_root, "", MAIN_X, 250, MAIN_W, &font_archivo_regular_15, S07_MUTED);
        m_statsHashes = screenMakeLabel(m_root, "", MAIN_X, 268, MAIN_W, &font_archivo_regular_15, S07_MUTED);
        lv_label_set_long_mode(m_statsHashes, LV_LABEL_LONG_WRAP);

        //   ---- footer: decorative barcode, drawn with plain rectangles ---------
        //   (no barcode font at all -- see the task's own guidance that this
        //   is both cheaper and more contract-faithful than a decorative
        //   font). Fixed pool of BARCODE_BARS lv_obj bars, repositioned/
        //   resized every update from the block height's own digits, never
        //   recreated. A separate "--" label covers the !netValid case so the
        //   footer never silently shows a barcode encoding invented data.
        for (int i = 0; i < BARCODE_BARS; i++) {
            lv_obj_t *bar = lv_obj_create(m_root);
            lv_obj_set_style_bg_color(bar, lv_color_hex(S07_INK), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(bar, 0, LV_PART_MAIN);
            lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
            m_bar[i] = bar;
        }
        m_barcodeNA = screenMakeLabel(m_root, "--", BARCODE_X, BARCODE_Y, 200, &font_archivo_regular_15, S07_MUTED);
    }

    void update(const UiState &s) override
    {
        if (!m_root) {
            return;
        }

        //   net.height / odds all come from the same "netValid" gate (see
        //   ui_state.h and this task's rule: !netValid -> "--" everywhere,
        //   never an invented number). Each field is still individually
        //   isnan-checked below as a second line of defense against a
        //   partially-populated snapshot.
        bool netOk = s.netValid;

        //   ---- canhoto: block height as the ticket's serial number ------------
        if (netOk) {
            char digits[16];
            snprintf(digits, sizeof(digits), "%u", (unsigned) s.height);
            lv_label_set_text(m_canhotoNum, digits);
        } else {
            lv_label_set_text(m_canhotoNum, "--");
        }

        //   ---- hero -------------------------------------------------------------
        bool oddsOk = netOk && !std::isnan(s.oddsPerDay);
        if (oddsOk) {
            OneInParts parts;
            computeOneIn(s.oddsPerDay, parts);
            if (parts.never) {
                lv_label_set_text(m_heroPrefix, "");
                lv_label_set_text(m_heroNum, "0");
                lv_label_set_text(m_heroUnit, "never");
            } else {
                lv_label_set_text(m_heroPrefix, "1 in");
                lv_label_set_text(m_heroNum, parts.value);
                lv_label_set_text(m_heroUnit, parts.unit);
            }
        } else {
            lv_label_set_text(m_heroPrefix, "");
            lv_label_set_text(m_heroNum, "--");
            lv_label_set_text(m_heroUnit, "");
        }

        //   ---- compare (vs Mega-Sena) --------------------------------------------
        //   Lowercase ascii "x" instead of the "x" (multiplication sign,
        //   U+00D7) the approved design uses: that codepoint is not in the Latin-1
        //   subset already generated for font_archivo_regular_15 (see
        //   tools/fonts/generate-fonts.ps1's $FULL range), and adding one
        //   extra glyph would mean regenerating an EXISTING shared font
        //   (off-limits -- other screens use the exact same.c file) just for
        //   one symbol. "48x" also reads naturally in pt-BR. The trailing
        //   "com um jogo simples" from the approved design's sentence is dropped too:
        //   shorter text leaves headroom so this WRAP-mode label can never
        //   grow past 2 lines and crowd the stats line below it, even if
        //   vsMega is unexpectedly large -- see
        if (netOk && !std::isnan(s.vsMega)) {
            char nBuf[nqfmt::kNumBufMax], line[220];
            nqfmt::n0(nBuf, sizeof(nBuf), (float) s.vsMega);
            snprintf(line, sizeof(line), "= %sx the chance of winning the lottery jackpot today", nBuf);
            lv_label_set_text(m_compare, line);
        } else {
            lv_label_set_text(m_compare, "Waiting for network data.");
        }

        //   ---- stats: chance in the year + hashes played today -----------------
        if (netOk && !std::isnan(s.oddsPerYear)) {
            char pBuf[nqfmt::kNumBufMax], line[96];
            nqfmt::n2(pBuf, sizeof(pBuf), (float) (s.oddsPerYear * 100.0));
            snprintf(line, sizeof(line), "this year: %s%%", pBuf);
            lv_label_set_text(m_statsYear, line);
        } else {
            lv_label_set_text(m_statsYear, "this year: --");
        }

        char hashesBuf[96], hashesLine[150];
        hashesHojeTexto(hashesBuf, sizeof(hashesBuf), s);
        snprintf(hashesLine, sizeof(hashesLine), "tickets played today: %s", hashesBuf);
        lv_label_set_text(m_statsHashes, hashesLine);

        //   ---- barcode: bars derived from the block height's own digits --------
        if (netOk) {
            lv_obj_add_flag(m_barcodeNA, LV_OBJ_FLAG_HIDDEN);
            char digits[16];
            int len = snprintf(digits, sizeof(digits), "%u", (unsigned) s.height);
            if (len <= 0) {
                len = 1;
                digits[0] = '0';
            }
            int x = BARCODE_X;
            for (int i = 0; i < BARCODE_BARS; i++) {
                int d = digits[i % len] - '0';
                if (d < 0 || d > 9) {
                    d = 0;
                }
                int w = 2 + (d % 4) * 2; // 2,4,6,8 px -- decorative, not a real symbology
                lv_obj_clear_flag(m_bar[i], LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_pos(m_bar[i], x, BARCODE_Y);
                lv_obj_set_size(m_bar[i], w, BARCODE_H);
                x += w + BARCODE_GAP;
            }
        } else {
            lv_obj_clear_flag(m_barcodeNA, LV_OBJ_FLAG_HIDDEN);
            for (int i = 0; i < BARCODE_BARS; i++) {
                lv_obj_add_flag(m_bar[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    void destroy() override
    {
        if (m_root) {
            lv_obj_del(m_root);
            m_root = nullptr;
            m_canhotoLine = nullptr;
            m_canhotoNum = nullptr;
            m_heroPrefix = nullptr;
            m_heroNum = nullptr;
            m_heroUnit = nullptr;
            m_compare = nullptr;
            m_statsYear = nullptr;
            m_statsHashes = nullptr;
            m_barcodeNA = nullptr;
            for (int i = 0; i < BARCODE_BARS; i++) {
                m_bar[i] = nullptr;
            }
        }
    }

  private:
    lv_obj_t *m_root = nullptr;
    lv_obj_t *m_canhotoLine = nullptr;
    lv_point_t m_canhotoLinePts[2] = {};
    lv_obj_t *m_canhotoNum = nullptr;
    lv_obj_t *m_heroPrefix = nullptr;
    lv_obj_t *m_heroNum = nullptr;
    lv_obj_t *m_heroUnit = nullptr;
    lv_obj_t *m_compare = nullptr;
    lv_obj_t *m_statsYear = nullptr;
    lv_obj_t *m_statsHashes = nullptr;
    lv_obj_t *m_bar[BARCODE_BARS] = {};
    lv_obj_t *m_barcodeNA = nullptr;
};

S07SorteScreen s_instance;

} // namespace

Screen *screenGetS07Sorte()
{
    return &s_instance;
}
