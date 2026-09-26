// Ported from the approved design to LVGL 8.3 (DISPLAY_PROFILE_
// YYSLUPING_480X320). "Diário": a vertical logbook of the day -- a summary
// of governor ups/downs/emergencies and the fraction of time spent at the
// frequency ceiling, then up to 6 timestamped entries (newest first, a
// small geometric mark per event kind, older entries fading out), styled
// as a machine-room logbook (dark aged paper, brass, a red ledger margin).
//
// Before this pass: a skeleton with a
// single placeholder label, no list, no summary.
//
// After: the approved design's own logbook palette, a FIXED POOL of 6 reused
// row containers (never created/destroyed on update -- only their text/shape/
// opacity change), a per-kind geometric mark (circle/diamond/square/ring), and
// the ups/downs/emergencies + %-at-ceiling summary. Reuses the existing
// Archivo fonts (no new font for this screen -- Why Vollkorn was dropped in
// favor of staying inside the shared ~80 KB font budget for the 4 screens in
// this pass, per the task's own fallback rule).

#include "../fonts/fonts.h"
#include "../ui_state.h"
#include "nq_fmt.h"
#include "screen.h"
#include "screen_common.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace
{

constexpr uint32_t S18_PAPER = 0x1c130d;
constexpr uint32_t S18_INK = 0xefe2c8;
constexpr uint32_t S18_RULE = 0x3b2c22;
constexpr uint32_t S18_MARGIN = 0xa8433a;
constexpr uint32_t S18_BRASS = 0xcaa056;
constexpr uint32_t S18_SEA = 0x5f8a74;

constexpr int ROWS_TOP = 86, ROW_H = 37, ROW_N = 6;
// Opacity by position: the 4 newest stay full-strength, the 5th/6th fade --
// matches the approved design's own OPACITY table.
constexpr lv_opa_t kRowOpa[ROW_N] = {255, 255, 255, 255, 158, 102};

void formatEntryText(char *out, size_t n, const char *text)
{
    if (!text || !text[0]) {
        std::snprintf(out, n, "No details for this entry.");
        return;
    }
    size_t len = std::strlen(text);
    char last = text[len - 1];
    if (last == '.' || last == '!' || last == '?') {
        std::snprintf(out, n, "%s", text);
    } else {
        std::snprintf(out, n, "%s.", text);
    }
}

// One recycled row: a container (opacity fades as a group) holding an hour
// label, a small per-kind mark, and the entry text. Built once in create;
// update only ever changes text/shape/color/opacity/visibility.
struct DiaryRow
{
    lv_obj_t *container = nullptr;
    lv_obj_t *hourLbl = nullptr;
    lv_obj_t *mark = nullptr;
    lv_obj_t *textLbl = nullptr;
};

class S18DiarioScreen : public Screen {
  public:
    int number() const override
    {
        return 18;
    }
    const char *name() const override
    {
        return "diario";
    }

    void create(lv_obj_t *parent) override
    {
        if (m_root) {
            destroy();
        }
        m_root = screenMakeRoot(parent, S18_PAPER);

        //  Ruled paper texture + spine, static, drawn once.
        lv_obj_t *spine = lv_obj_create(m_root);
        lv_obj_set_pos(spine, 0, 0);
        lv_obj_set_size(spine, 5, 320);
        lv_obj_set_style_bg_color(spine, lv_color_hex(S18_RULE), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(spine, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(spine, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(spine, 0, LV_PART_MAIN);
        lv_obj_clear_flag(spine, LV_OBJ_FLAG_SCROLLABLE);

        for (int y = 16; y <= 306; y += 24) {
            lv_obj_t *rl = lv_obj_create(m_root);
            lv_obj_set_pos(rl, 0, y);
            lv_obj_set_size(rl, 480, 1);
            lv_obj_set_style_bg_color(rl, lv_color_hex(S18_RULE), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(rl, 153, LV_PART_MAIN); // ~0.6
            lv_obj_set_style_border_width(rl, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(rl, 0, LV_PART_MAIN);
            lv_obj_clear_flag(rl, LV_OBJ_FLAG_SCROLLABLE);
        }

        lv_obj_t *title = screenMakeLabel(m_root, "Journal", 12, 10, 456, &font_archivo_regular_15, S18_BRASS);
        lv_obj_set_style_text_opa(title, 217, LV_PART_MAIN); // ~0.85

        m_line1 = screenMakeLabel(m_root, "", 12, 30, 456, &font_archivo_medium_17, S18_INK);
        lv_label_set_recolor(m_line1, true);
        m_line2 = screenMakeLabel(m_root, "", 12, 54, 456, &font_archivo_medium_17, S18_INK);
        lv_label_set_recolor(m_line2, true);

        lv_obj_t *divider = lv_obj_create(m_root);
        lv_obj_set_pos(divider, 12, 80);
        lv_obj_set_size(divider, 456, 2);
        lv_obj_set_style_bg_color(divider, lv_color_hex(S18_BRASS), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(divider, 204, LV_PART_MAIN); // ~0.8
        lv_obj_set_style_border_width(divider, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(divider, 0, LV_PART_MAIN);
        lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *margin = lv_obj_create(m_root);
        lv_obj_set_pos(margin, 64, ROWS_TOP);
        lv_obj_set_size(margin, 2, ROW_H * ROW_N);
        lv_obj_set_style_bg_color(margin, lv_color_hex(S18_MARGIN), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(margin, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(margin, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(margin, 0, LV_PART_MAIN);
        lv_obj_clear_flag(margin, LV_OBJ_FLAG_SCROLLABLE);

        for (int i = 0; i < ROW_N; i++) {
            DiaryRow &row = m_rows[i];
            row.container = lv_obj_create(m_root);
            lv_obj_set_pos(row.container, 12, ROWS_TOP + i * ROW_H);
            lv_obj_set_size(row.container, 456, ROW_H - 2);
            lv_obj_set_style_bg_opa(row.container, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(row.container, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(row.container, 0, LV_PART_MAIN);
            lv_obj_clear_flag(row.container, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(row.container, LV_OBJ_FLAG_CLICKABLE);

            row.hourLbl =
                screenMakeLabel(row.container, "--:--", 0, 10, 40, &font_archivo_regular_15, S18_BRASS, LV_TEXT_ALIGN_RIGHT);

            row.mark = lv_obj_create(row.container);
            lv_obj_set_size(row.mark, 10, 10);
            lv_obj_set_pos(row.mark, 49, 12);
            lv_obj_set_style_pad_all(row.mark, 0, LV_PART_MAIN);
            lv_obj_clear_flag(row.mark, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(row.mark, LV_OBJ_FLAG_CLICKABLE);

            row.textLbl = screenMakeLabel(row.container, "", 72, 1, 384, &font_archivo_regular_15, S18_INK);
            lv_label_set_long_mode(row.textLbl, LV_LABEL_LONG_WRAP);
        }
    }

    void update(const UiState &s) override
    {
        if (!m_root) {
            return;
        }

        //  govUps/govDowns/govEmergencies are uint32_t (ui_state.h); cast to
        //  `unsigned` explicitly rather than assume %u matches uint32_t --
        //  on this toolchain's newlib, uint32_t resolves to `long unsigned
        //  int`, which -Werror=format flags against a bare %u.
        char upsW[24], downsW[24], emerW[24];
        std::snprintf(upsW, sizeof(upsW), "%u %s", (unsigned) s.govUps, s.govUps == 1 ? "ramp-up" : "ramp-ups");
        std::snprintf(downsW, sizeof(downsW), "%u %s", (unsigned) s.govDowns, s.govDowns == 1 ? "backoff" : "backoffs");
        std::snprintf(emerW, sizeof(emerW), "%u %s", (unsigned) s.govEmergencies,
                      s.govEmergencies == 1 ? "emergency" : "emergencies");
        char line1[160];
        std::snprintf(line1, sizeof(line1), "Today: #caa056 %s#, #caa056 %s#, #caa056 %s#.", upsW, downsW, emerW);
        lv_label_set_text(m_line1, line1);

        int pct = 0;
        if (s.histCount > 0) {
            float thresh = s.freqCap - 6.0f;
            int hit = 0;
            for (int i = 0; i < s.histCount; i++) {
                if (!std::isnan(s.histFreq[i]) && s.histFreq[i] >= thresh) {
                    hit++;
                }
            }
            pct = (int) ((hit * 100 + s.histCount / 2) / s.histCount);
        }
        char capBuf[16];
        nqfmt::n0(capBuf, sizeof(capBuf), s.freqCap);
        char line2[160];
        std::snprintf(line2, sizeof(line2), "Spent #caa056 %d%%# of the time at the #caa056 %s MHz# ceiling.", pct, capBuf);
        lv_label_set_text(m_line2, line2);

        if (s.diaryCount <= 0) {
            lv_obj_set_style_opa(m_rows[0].container, LV_OPA_COVER, LV_PART_MAIN);
            lv_label_set_text(m_rows[0].hourLbl, "");
            styleMark(m_rows[0].mark, 0xff); // "other" dot
            lv_label_set_text(m_rows[0].textLbl, "Nothing logged yet today.");
            for (int i = 1; i < ROW_N; i++) {
                lv_obj_set_style_opa(m_rows[i].container, LV_OPA_TRANSP, LV_PART_MAIN);
            }
            return;
        }

        int shown = s.diaryCount < ROW_N ? s.diaryCount : ROW_N;
        for (int i = 0; i < ROW_N; i++) {
            DiaryRow &row = m_rows[i];
            if (i >= shown) {
                lv_obj_set_style_opa(row.container, LV_OPA_TRANSP, LV_PART_MAIN);
                continue;
            }
            lv_obj_set_style_opa(row.container, kRowOpa[i], LV_PART_MAIN);

            const UiDiaryEntry &entry = s.diary[i];
            if (entry.epochS != 0) {
                time_t t = (time_t) entry.epochS;
                struct tm tmv{};
                localtime_r(&t, &tmv);
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%02d:%02d", tmv.tm_hour % 24, tmv.tm_min % 60);
                lv_label_set_text(row.hourLbl, buf);
            } else {
                lv_label_set_text(row.hourLbl, "--:--");
            }

            styleMark(row.mark, entry.kind);

            char text[96];
            formatEntryText(text, sizeof(text), entry.text);
            lv_label_set_text(row.textLbl, text);
        }
    }

    void destroy() override
    {
        if (m_root) {
            lv_obj_del(m_root);
            m_root = nullptr;
        }
        m_line1 = m_line2 = nullptr;
        for (int i = 0; i < ROW_N; i++) {
            m_rows[i] = DiaryRow{};
        }
    }

  private:
    //  Per-kind geometric mark, matching the approved design's selo shapes: gov =
    //  filled circle, amb = diamond (sea), share = diamond (brass),
    //  block = filled square (margin red), net = ring (border only),
    //  anything else = small dim dot.
    void styleMark(lv_obj_t *m, uint8_t kind)
    {
        lv_obj_set_style_transform_angle(m, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(m, 0, LV_PART_MAIN);
        lv_obj_set_size(m, 10, 10);
        lv_obj_set_pos(m, 49, 12);
        switch (kind) {
        case UI_DIARY_GOV:
            lv_obj_set_style_radius(m, LV_RADIUS_CIRCLE, LV_PART_MAIN);
            lv_obj_set_style_bg_color(m, lv_color_hex(S18_BRASS), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(m, LV_OPA_COVER, LV_PART_MAIN);
            break;
        case UI_DIARY_AMB:
            lv_obj_set_size(m, 9, 9);
            lv_obj_set_pos(m, 49, 13);
            lv_obj_set_style_radius(m, 0, LV_PART_MAIN);
            lv_obj_set_style_transform_angle(m, 450, LV_PART_MAIN);
            lv_obj_set_style_bg_color(m, lv_color_hex(S18_SEA), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(m, LV_OPA_COVER, LV_PART_MAIN);
            break;
        case UI_DIARY_SHARE:
            lv_obj_set_size(m, 9, 9);
            lv_obj_set_pos(m, 49, 13);
            lv_obj_set_style_radius(m, 0, LV_PART_MAIN);
            lv_obj_set_style_transform_angle(m, 450, LV_PART_MAIN);
            lv_obj_set_style_bg_color(m, lv_color_hex(S18_BRASS), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(m, LV_OPA_COVER, LV_PART_MAIN);
            break;
        case UI_DIARY_BLOCK:
            lv_obj_set_style_radius(m, 0, LV_PART_MAIN);
            lv_obj_set_style_bg_color(m, lv_color_hex(S18_MARGIN), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(m, LV_OPA_COVER, LV_PART_MAIN);
            break;
        case UI_DIARY_NET:
            lv_obj_set_style_radius(m, LV_RADIUS_CIRCLE, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(m, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(m, 2, LV_PART_MAIN);
            lv_obj_set_style_border_color(m, lv_color_hex(S18_INK), LV_PART_MAIN);
            lv_obj_set_style_border_opa(m, 204, LV_PART_MAIN); // ~0.8
            break;
        default:
            lv_obj_set_size(m, 6, 6);
            lv_obj_set_pos(m, 51, 15);
            lv_obj_set_style_radius(m, LV_RADIUS_CIRCLE, LV_PART_MAIN);
            lv_obj_set_style_bg_color(m, lv_color_hex(S18_INK), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(m, 128, LV_PART_MAIN);
            break;
        }
    }

    lv_obj_t *m_root = nullptr;
    lv_obj_t *m_line1 = nullptr;
    lv_obj_t *m_line2 = nullptr;
    DiaryRow m_rows[ROW_N];
};

S18DiarioScreen s_instance;

} // namespace

Screen *screenGetS18Diario()
{
    return &s_instance;
}
