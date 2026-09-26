// tools/sim/src/main.cpp - nqsim: PC screen simulator for the NerdQAxe++
// display-reference designs pilot screens (19-zen, 01-painel, 14-hashrate).
//
// This is the ONLY genuinely new C++ code this task adds. Everything it drives
// -- the 3 Screen implementations, ui_state.h/.cpp, the generated fonts -- is
// the real firmware code, compiled unmodified straight from its real location
// (see./CMakeLists.txt). There is no ScreenManager and no DisplayDriver here:
// per the task's own instructions, this instantiates each Screen directly and
// drives create/update/destroy itself, exactly like ScreenManager does on real
// hardware (see main/displays/screens/screen_manager.cpp) but without needing
// its pthread-mutex / DisplayDriver-state-machine plumbing, none of which
// exists on a PC host.
//
// Flow per (screen, scenario):
// 1. Build a UiState snapshot (uiStateSetDefaults + scenario deltas).
// 2. screen->create(lv_scr_act); screen->update(state).
// 3. Pump lv_timer_handler/lv_tick_inc a few times, then
// lv_refr_now(NULL) to force a synchronous full redraw (no need to
// wait for a real refresh timer on a one-shot render).
// 4. flush_cb (below) has been copying every flushed area into a
// persistent 480x320 RGB565 framebuffer; convert it to RGB888 with
// LVGL's own lv_color_to32 (so the RGB565->RGB888 bit expansion
// matches whatever LV_COLOR_16_SWAP the real lv_conf.h has, instead
// of a hand-rolled, easy-to-get-wrong shift) and write it out with
// lodepng.
// 5. screen->destroy.

#include "lvgl.h"
#include "screen.h"
#include "screen_registry.h"
#include "ui_state.h"

#include "lodepng.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <direct.h>
#define NQSIM_MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#define NQSIM_MKDIR(path) mkdir(path, 0755)
#endif

// screen_registry.cpp's factory functions, declared directly here instead
// of pulling in screen_registry.cpp/.h (which this simulator does not
// need -- see the file-level comment above and the task instructions:
// "for the simulator you can instantiate the screen classes directly").
// Same signatures as screen_registry.cpp -- ordinary C++ linkage, defined
// in the 3 screen.cpp files compiled straight into this executable.

namespace
{

constexpr int DISP_W = 480;
constexpr int DISP_H = 320;

// Persistent "device" framebuffer: flush_cb below writes every flushed
// area into this, regardless of how LVGL chunks the redraw into partial
// areas. Holds the final rendered frame once lv_refr_now returns.
lv_color_t s_framebuffer[DISP_W * DISP_H];

lv_disp_draw_buf_t s_drawBuf;
lv_color_t s_lvglBuf[DISP_W * DISP_H]; // single buffer, screen-sized: no
                                       //  partial-buffer chunking surprises
lv_disp_drv_t s_dispDrv;

void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    int32_t w = area->x2 - area->x1 + 1;
    for (int32_t y = area->y1; y <= area->y2; y++) {
        lv_color_t *dst = &s_framebuffer[y * DISP_W + area->x1];
        lv_color_t *src = &color_p[(y - area->y1) * w];
        memcpy(dst, src, (size_t) w * sizeof(lv_color_t));
    }
    lv_disp_flush_ready(drv);
}

void initLvgl()
{
    lv_init();

    lv_disp_draw_buf_init(&s_drawBuf, s_lvglBuf, nullptr, DISP_W * DISP_H);

    lv_disp_drv_init(&s_dispDrv);
    s_dispDrv.draw_buf = &s_drawBuf;
    s_dispDrv.hor_res = DISP_W;
    s_dispDrv.ver_res = DISP_H;
    s_dispDrv.flush_cb = flush_cb;
    //  Whole buffer == whole screen: ask LVGL to always redraw the full
    //  screen so a one-shot render never leaves a stale corner behind
    //  between scenarios sharing the same lv_scr_act.
    s_dispDrv.full_refresh = 1;
    lv_disp_drv_register(&s_dispDrv);
}

// Pumps LVGL enough to guarantee a finished, synchronous render, then
// snapshots s_framebuffer (RGB565, per lv_conf.h's LV_COLOR_DEPTH 16) into
// an RGB888 buffer via LVGL's own lv_color_to32 -- see the file-level
// comment on why that (not a hand-rolled shift) is used.
std::vector<unsigned char> renderToRgba()
{
    for (int i = 0; i < 6; i++) {
        lv_tick_inc(LV_DISP_DEF_REFR_PERIOD);
        lv_timer_handler();
    }
    lv_refr_now(nullptr);

    std::vector<unsigned char> rgba((size_t) DISP_W * DISP_H * 4);
    for (int i = 0; i < DISP_W * DISP_H; i++) {
        lv_color32_t px;
        px.full = lv_color_to32(s_framebuffer[i]);
        rgba[(size_t) i * 4 + 0] = px.ch.red;
        rgba[(size_t) i * 4 + 1] = px.ch.green;
        rgba[(size_t) i * 4 + 2] = px.ch.blue;
        rgba[(size_t) i * 4 + 3] = 255;
    }
    return rgba;
}

bool ensureDir(const std::string &path)
{
    //  Best-effort, parent-by-parent mkdir -- the output path is always a
    //  short, known, explicit path (CLI arg / README default), not
    //  arbitrary user input, so a simple split is enough here.
    std::string cur;
    for (size_t i = 0; i <= path.size(); i++) {
        if (i == path.size() || path[i] == '/' || path[i] == '\\') {
            if (!cur.empty() && cur != ".") {
                NQSIM_MKDIR(cur.c_str());
            }
            if (i < path.size()) {
                cur += path[i];
            }
        } else {
            cur += path[i];
        }
    }
    return true;
}

void writePng(const std::string &outDir, const char *fileName, const std::vector<unsigned char> &rgba)
{
    std::string path = outDir + "/" + fileName;
    unsigned err = lodepng::encode(path, rgba, (unsigned) DISP_W, (unsigned) DISP_H, LCT_RGBA, 8);
    if (err) {
        std::fprintf(stderr, "ERROR lodepng writing %s: %u %s\n", path.c_str(), err, lodepng_error_text(err));
        std::exit(1);
    }
    std::fprintf(stdout, " -> %s\n", path.c_str());
}

// ---------------------------------------------------------------------
// Scenarios (task spec, section "CENARIOS"). Each builds a full UiState
// via uiStateSetDefaults (all-NAN/0 + the real firmware's cfg defaults,
// see main/displays/ui_state.cpp) and then overrides only the fields the
// scenario cares about. Only real UiState fields are used (see
// main/displays/ui_state.h) -- nothing invented.
// ---------------------------------------------------------------------

// Fills the 24h history (UiState::histGhs/histFreq/histCount) with the
// "normal" scenario's simplified two-shift pattern: first half of the
// samples at 788 MHz (madrugada/overnight), second half at 756 MHz
// (tarde/afternoon), each with a hashrate that scales with frequency
// (GH/s roughly proportional to MHz, same approximation s14_hashrate.cpp
// itself uses for its "ceiling at freqCap" estimate).
void fillHistoryTwoShift(UiState &s)
{
    constexpr int N = UI_HIST_POINTS; // 1440 = 24h @ 1/min
    constexpr float FREQ_NIGHT = 788.0f, FREQ_DAY = 756.0f;
    constexpr float GHS_AT_775 = 6300.0f; // matches the "normal" hero value below
    s.histCount = N;
    for (int i = 0; i < N; i++) {
        float freq = (i < N / 2) ? FREQ_NIGHT : FREQ_DAY;
        s.histFreq[i] = freq;
        s.histGhs[i] = GHS_AT_775 * (freq / 775.0f);
    }
}

UiState scenarioNormal()
{
    UiState s;
    uiStateSetDefaults(s);

    s.freq = 775.0f;
    s.mv = 1240;
    s.hrInst = 6320.0f;
    s.hr1m = 6310.0f;
    s.hr10m = 6250.0f;
    s.hr1h = 6180.0f;
    s.hr1d = 6090.0f;
    s.hrExpected = 6300.0f;
    s.vrTemp = 76.8f;
    s.vrTempInt = 81.5f;
    s.boardTemp = 52.0f;
    s.pin = 127.0f;
    s.pout = 122.0f;
    s.effJth = s.pin / (s.hr1h / 1000.0f);
    s.headroom = s.vrTarget - s.vrTemp;
    s.govState = UI_GOV_HOLD;
    s.wifiConnected = true;
    s.poolConnected = true;
    std::snprintf(s.ssid, sizeof(s.ssid), "NerdQAxe-LAN");
    std::snprintf(s.ip, sizeof(s.ip), "10.0.0.42");

    //  ---- hourly averages (screens 13, 16): reconstructed from the same
    //  day-shape the approved design reference's Sim.prototype.ambientAt uses (coolest ~03h,
    //  hottest ~15h, Teresina-ish house with no AC) and the governor's own
    //  frequency response to that ambient -- colder room means more thermal
    //  headroom means a higher frequency, matching the measured overnight
    //  ~800 MHz / afternoon ~750 MHz pattern (added here, not previously
    //  populated: every hourGhs/
    //  hourFreq/hourVr/hourAmb/hourN entry was 0 before this pass, which
    //  s13/s16 correctly read as "no data for that hour" -- see
    //  ui_state.h's own "hourN[h]==0" rule -- so the 24-slice ring and the
    //  6h forecast strip had nothing to show).
    for (int h = 0; h < UI_HOURS; h++) {
        float amb = 26.2f + 5.6f * std::cos((float) (h - 15) / 24.0f * 2.0f * 3.14159265358979323846f);
        float freq = 790.0f - (amb - 20.6f) * (40.0f / 11.2f); // ~790 MHz coldest hour.. ~750 MHz hottest hour
        s.hourAmb[h] = amb;
        s.hourFreq[h] = freq;
        s.hourGhs[h] = 6300.0f * freq / 775.0f;
        s.hourVr[h] = s.vrTarget - (800.0f - freq) * 0.15f;
        s.hourN[h] = 60;
    }

    //  ---- wall clock (screens 13, 15, 18) ---------------------------------
    time_t nowT = std::time(nullptr);
    struct tm tmNow{};
    localtime_r(&nowT, &tmNow);
    s.clockValid = true;
    s.epochS = (int64_t) nowT;

    //  ---- ambient / trend (screen 16): matches the hourly ring at the
    //  CURRENT local hour, so the "Sala" screen's live reading agrees with
    //  screen 13's ring instead of using an unrelated fixed hour. ----------
    s.ambient = s.hourAmb[tmNow.tm_hour % 24];
    s.slopeCpm = 0.06f; // mild warming trend (illustrative, not derived from anything else)

    //  ---- energy (screen 15) -----------------------------------------------
    s.energyValid = true;
    s.kwhToday = 2.9f;
    s.costToday = s.kwhToday * s.tarifa;
    s.avgW24 = s.pin;
    s.costDay = s.avgW24 * 24.0f / 1000.0f * s.tarifa;
    s.costMonth = s.avgW24 * 24.0f * 30.0f / 1000.0f * s.tarifa;
    s.kwhMonth = s.avgW24 * 24.0f * 30.0f / 1000.0f;

    //  ---- governor diary + counters (screen 18) -----------------------------
    //  govUps/govDowns/govEmergencies were 0 (never set anywhere in this
    //  scenario), which is indistinguishable from "governor never acted" --
    //  filled with the same order-of-magnitude the approved design's own 24h
    //  simulator prefill uses (the approved design reference's Sim.prototype.prefill: ups=61,
    //  downs=38, emergencies=0) so the summary line reads as a normal day,
    //  not an empty one. diary[]/diaryCount were empty too (UI_DIARY_MAX
    //  slots, all zeroed by uiStateSetDefaults' memset) -- filled with 6
    //  entries, newest first, one per UiDiaryKind so every mark shape shows
    //  up at least once, phrased as complete pt-BR sentences (see
    //  s18_diario.cpp's own doc for why the approved design's regex-based sentence
    //  rewrite was simplified to "use the entry text as already written").
    s.govUps = 61;
    s.govDowns = 38;
    s.govEmergencies = 0;
    s.diaryCount = 6;
    int64_t nowS = s.epochS;
    auto setDiary = [&](int i, int64_t ageS, uint8_t kind, const char *text) {
        s.diary[i].epochS = nowS - ageS;
        s.diary[i].kind = kind;
        std::snprintf(s.diary[i].text, UI_DIARY_TEXT, "%s", text);
    };
    setDiary(0, 5 * 60, UI_DIARY_GOV, "Ramped up to 780 MHz, the regulator had headroom.");
    setDiary(1, 42 * 60, UI_DIARY_AMB, "The room started warming up.");
    setDiary(2, 96 * 60, UI_DIARY_SHARE, "Hit a session record: difficulty 184 M.");
    setDiary(3, 150 * 60, UI_DIARY_NET, "Pool responded in 4 ms, connection stable.");
    setDiary(4, 210 * 60, UI_DIARY_GOV, "Backed off to 750 MHz, the regulator was tightening up.");
    setDiary(5, 260 * 60, UI_DIARY_BLOCK, "New block on the network: height 967,563.");

    //  ---- power details (screen 04) and per-chip hashrate (screen 05) -------
    //  Same operating point as the rest of this scenario; numbers from the
    //  HTML simulator (the approved design reference) at ~775 MHz.
    s.vin = 11.85f;
    s.iin = s.pin / s.vin;
    s.iout = 89.0f;
    s.voutMv = 1238.0f;
    s.fanW = 3.2f;
    s.vrLoss = s.pin - s.pout - s.fanW;
    s.psuLoad = s.pin / s.psuW; // > 1: this unit's 120 W supply is overloaded
    s.fanPct = 100;
    s.fanRpm = 2720;
    s.asicCount = 4;
    const float chipBal[4] = {1.012f, 1.018f, 0.981f, 0.989f};
    for (int i = 0; i < 4; i++) {
        s.chipGhs[i] = s.hr1m / 4.0f * chipBal[i];
    }

    //  ---- network / lottery (screens 07, 10, 11, 12) -------------------------
    //  Values computed exactly like the approved design reference (and ui_data.cpp) do, from the
    //  real network difficulty of 2026-09 and this scenario's hashrate.
    s.netValid = true;
    s.netDiff = 1.2745e14;
    s.poolDiff = 32768.0;
    s.height = 967564;
    s.sinceBlockS = 212.0f;
    s.epochPos = s.height % 2016;
    s.epochLeft = 2016 - s.epochPos;
    s.halvingAt = 1050000;
    s.halvingLeft = s.halvingAt - s.height;
    s.halvingDays = (float) s.halvingLeft * 600.0f / 86400.0f;
    s.cyclePos = (float) (s.height - 840000) / 210000.0f;
    s.subsidyBtc = 3.125f;
    s.rewardBtc = 3.165f;
    s.netHashrateEHs = s.netDiff * 4294967296.0 / 600.0 / 1e18;
    {
        double pBlock = (double) s.hr1m * 1e9 / (s.netDiff * 4294967296.0 / 600.0);
        s.oddsPerBlock = pBlock;
        s.oddsPerDay = 1.0 - std::pow(1.0 - pBlock, 144.0);
        s.oddsPerYear = 1.0 - std::pow(1.0 - s.oddsPerDay, 365.0);
        s.expectedYears = 1.0 / (s.oddsPerDay * 365.0);
        s.vsMega = s.oddsPerDay * 50063860.0;
    }

    //  ---- shares (screen 11) ---------------------------------------------------
    s.sharesAccepted = 11954;
    s.sharesRejected = 7;
    s.sharesPerMin = (float) ((double) s.hr1m * 1e9 / (s.poolDiff * 4294967296.0) * 60.0);
    s.bestSession = 1.84e8;
    s.bestEver = 4.29e9;
    {
        //  ~25 minutes of shares at the expected rate (one every ~22 s), with
        //  difficulties drawn from the real share distribution (poolDiff / U);
        //  deterministic pseudo-random so the PNG is reproducible.
        uint32_t seed = 12345;
        auto rnd = [&] {
            seed = seed * 1664525u + 1013904223u;
            return ((seed >> 8) & 0xFFFFFF) / 16777216.0 + 1e-6;
        };
        int n = 0;
        double age = 900.0; // 48 shares x ~22 s ends near "now"
        while (age > 4.0 && n < UI_SHARES_RECENT) {
            s.recent[n].epochS = s.epochS - (int64_t) age;
            s.recent[n].diff = s.poolDiff / rnd();
            n++;
            age -= -22.0 * std::log(rnd());
        }
        s.recentCount = n;
        s.lastShareDiff = s.recent[n - 1].diff;
        s.lastShareAgeS = (float) (s.epochS - s.recent[n - 1].epochS);
    }

    //  ---- connectivity / uptime -------------------------------------------------
    s.rssi = -52;
    s.pingMs = 4;
    s.uptimeS = 3.2f * 86400.0f;
    std::snprintf(s.poolHost, sizeof(s.poolHost), "pool.example.com");

    fillHistoryTwoShift(s);
    return s;
}

UiState scenarioEmergencia()
{
    UiState s = scenarioNormal();
    s.vrTemp = 83.0f;
    s.vrTempInt = 91.0f;
    s.govState = UI_GOV_EMERGENCY;
    s.freq = 650.0f;                    // governor already backed off in response
    s.headroom = s.vrTarget - s.vrTemp; // negative: over target
    return s;
}

UiState scenarioWifiCaido()
{
    UiState s = scenarioNormal();
    s.wifiConnected = false;
    s.poolConnected = false;
    s.rssi = 0;
    s.ssid[0] = '\0';
    s.ip[0] = '\0';
    return s;
}

UiState scenarioNan()
{
    UiState s;
    uiStateSetDefaults(s); // already NAN's every "live reading" field
    s.govState = UI_GOV_SENSOR_FAIL;
    s.wifiConnected = false;
    s.poolConnected = false;
    //  headroom = vrTarget(finite) - vrTemp(NAN) is NAN automatically (IEEE
    //  754 propagation), matching what uiStateSetDefaults already left in
    //  s.headroom -- restated here for clarity, not strictly necessary.
    s.headroom = s.vrTarget - s.vrTemp;
    s.histCount = 0; // no history yet: exercises s14_hashrate's "n < 2"
    //  early-out path ("Ainda sem histórico suficiente.")
    return s;
}

struct Scenario
{
    const char *slug;
    UiState (*build)();
};

const Scenario kScenarios[] = {
    {"normal", scenarioNormal},
    {"emergencia", scenarioEmergencia},
    {"wifi-caido", scenarioWifiCaido},
    {"nan", scenarioNan},
};

} // namespace

int main(int argc, char **argv)
{
    std::string outDir = (argc > 1) ? argv[1] : "sim-png";
    ensureDir(outDir);

    initLvgl();
    lv_obj_t *scr = lv_disp_get_scr_act(nullptr);

    size_t screenCount = 0;
    const ScreenRegistryEntry *screens = screenRegistryAll(&screenCount);
    int only = (argc > 2) ? std::atoi(argv[2]) : 0; // optional: render only this screen number
    std::fprintf(stdout, "nqsim: rendering %zu screens x %zu scenarios in %s\n", screenCount,
                 sizeof(kScenarios) / sizeof(kScenarios[0]), outDir.c_str());

    int rendered = 0;
    for (size_t si = 0; si < screenCount; si++) {
        Screen *screen = screens[si].screen;
        if (only && screen->number() != only) {
            continue;
        }
        char slug[48];
        std::snprintf(slug, sizeof(slug), "%02d-%s", screen->number(), screen->name());
        for (const auto &scenario : kScenarios) {
            UiState state = scenario.build();

            screen->create(scr);
            screen->update(state);

            std::vector<unsigned char> rgba = renderToRgba();

            char fileName[128];
            std::snprintf(fileName, sizeof(fileName), "%s_%s.png", slug, scenario.slug);
            writePng(outDir, fileName, rgba);
            rendered++;

            screen->destroy();
        }
    }

    std::fprintf(stdout, "nqsim: %d PNGs written to %s\n", rendered, outDir.c_str());
    return 0;
}
