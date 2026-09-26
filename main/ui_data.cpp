// Data layer for the rotating display screens. main/ui_data.h for the public
// API and for the design notes (in Portuguese, per project convention).
//
// Threading: - uiDataInit runs once from main.cpp, before any task starts. -
// uiDataTick runs every 2 s from PowerManagementTask::task, right after
// readAndPublishPowerTelemetry (i.e. already inside that task's own lock, so
// the m_vin/m_pin/. telemetry it reads via the new getVin/getPout/. getters is
// internally consistent for this cycle). It only touches this module's own
// state (protected by s_mutex), never POWER_MANAGEMENT_MODULE's
// governor/decision objects. - uiDataDiaryPush is called by
// PowerManagementTask on every non-HOLD governor action and on governor on/off
// transitions. - uiDataFill runs from the display side (a different task). It
// takes a short lock on POWER_MANAGEMENT_MODULE (mirrors GET_system_info in
// handler_system.cpp) to read the live governor decision, and a short lock on
// s_mutex to snapshot this module's own rings. Neither lock is held during the
// odds/interpolation math, which is cheap floating point only - no I/O, no
// long loop, while locked.
//
// All internal state (~31 KB, mostly the 24h history rings) lives in one
// block allocated from PSRAM in uiDataInit; nothing of this size is ever
// put on a task stack, matching the hard rule documented in ui_data.h and in
// the project brief (app_main only has 3.5 KB of stack left).

#include "ui_data.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "boards/board.h"
#include "global_state.h"
#include "nvs_config.h"
#include "thermal_governor.h"

static const char *TAG = "ui_data";

// ---------------------------------------------------------------------------
// Measured deltaVR / deltaVRint(freq) sweep from an active-calibration
// frequency sweep on the real hardware (2026-09-19). Same MHz points as the
// sweepMhz/sweepW defaults in main/displays/ui_state.cpp.
// ---------------------------------------------------------------------------
static const float kSweepMhz[UI_SWEEP_POINTS] = {500, 575, 650, 725, 750, 800};
static const float kSweepDVr[UI_SWEEP_POINTS] = {15.2f, 20.8f, 28.0f, 38.2f, 44.3f, 54.4f};
// Kept for parity with the full measured sweep table; not consumed yet -
// UiState only has one "ambient" field, derived from vrTemp/kSweepDVr. Left
// here instead of deleted so a future vrTempInt-based cross-check does not
// have to re-measure it.
static const float kSweepDVri[UI_SWEEP_POINTS] = {19.0f, 25.4f, 34.0f, 46.4f, 54.0f, 65.2f};

#define UI_AMBIENT_TAU_S 120.0f // ambient EMA time constant (~2 min, per spec)
#define UI_SLOPE_TAU_S 300.0f   // slope-of-slope smoothing (5 min) to kill sample noise
#define UI_FAN_W                                                                                                                   \
    3.2f // estimated fan draw, not separately metered (same value the
         // approved design reference uses)
#define UI_PSU_W 120.0f       // nominal PSU rating (12V/10A), fixed hardware spec
#define UI_IOUT_FAULT_A 95.0f // firmware OC fault, fixed (not in NVS)
#define UI_VRI_WARN_C 95.0f   // TPS53647 datasheet OT warn, fixed (not in NVS)
#define UI_NVS_ENERGY_SAVE_PERIOD_MS (30u * 60u * 1000u)
#define UI_DEFAULT_TICK_S 2.0f // nominal power-management loop period, used for the very first tick's dt

// ---------------------------------------------------------------------------
// Internal state - allocated once in PSRAM by uiDataInit. Never on a
// stack.
// ---------------------------------------------------------------------------
struct UiInternal
{
    //   ---- 24h history ring, 1 sample/minute -------------------------------
    int histHead, histCount;
    float histGhs[UI_HIST_POINTS];
    float histFreq[UI_HIST_POINTS];
    float histVr[UI_HIST_POINTS];
    float histAmb[UI_HIST_POINTS];
    float histPinW[UI_HIST_POINTS]; // internal only (avgW24); not exposed as its own screen field

    //   per-local-hour running averages (24 buckets, "last 24h" semantics -
    //   see uiHourBucketPush in ui_data.h)
    float hourGhs[UI_HOURS], hourFreq[UI_HOURS], hourVr[UI_HOURS], hourAmb[UI_HOURS];
    int hourN[UI_HOURS];
    int curHour; // -1 = nothing pushed yet; all four buckets are pushed together every minute

    //   minute accumulator: uiDataTick runs every 2s, the ring commits once
    //   a monotonic minute has elapsed (works even before SNTP syncs)
    double minGhsSum, minFreqSum, minVrSum, minAmbSum, minPinSum;
    int minN;
    int64_t lastMinuteBucket; // floor(uptime_s / 60) of the last commit; -1 = never

    //   ambient estimate + its trend
    float ambEma;          // NAN until seeded
    float vrEmaPrevMinute; // vrTemp EMA value one ring-commit ago, for the raw per-minute delta
    float slopeEma;        // NAN until seeded

    //   energy: dt always comes from esp_timer (monotonic, always available);
    //   the local-midnight reset only fires once the clock is synced.
    double kwhAccum;
    int64_t lastEnergyEpochS; // wall-clock epoch of the last integration; 0 = never (i.e. "counting since boot")
    int64_t lastTickUs;
    bool haveLastTick;
    uint32_t lastNvsSaveMs;

    //   shares
    uint64_t lastSharesAccepted;
    bool haveLastShares;
    UiShare sharesRing[UI_SHARES_RECENT];
    int sharesHead, sharesCount;
    double lastShareDiff;
    int64_t lastShareMonoUs; // esp_timer time of the last accepted share, for lastShareAgeS

    //   block
    uint32_t lastHeight;
    bool haveLastHeight;
    bool everSawBlockChange;
    int64_t lastBlockChangeMonoUs; // esp_timer time of the last observed height change

    //   governor diary
    UiDiaryEntry diaryRing[UI_DIARY_MAX];
    int diaryHead, diaryCount;

    //   rotation / display config, cached from NVS (see uiDataReloadConfig)
    uint32_t scrMask;
    uint16_t scrSecs;
    uint16_t scrDurs[UI_MAX_SCREENS]; // per-screen override, 0 = use scrSecs
};

static UiInternal *S = nullptr;
static SemaphoreHandle_t s_mutex = nullptr;

// Small RAII helper, mirrors LockGuard in power_management_task.h but for a
// plain FreeRTOS mutex.
class UiLock {
  public:
    UiLock()
    {
        if (s_mutex) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
        }
    }
    ~UiLock()
    {
        if (s_mutex) {
            xSemaphoreGive(s_mutex);
        }
    }
};

// ---------------------------------------------------------------------------
// gov:: <-> Ui enum mapping. gov::State starts at STARTUP=0 (no "off" state -
// that is tracked separately as the governor mode); UiGovState prepends
// UI_GOV_OFF=0, so every other value is offset by one. main/thermal_governor.h
// and main/displays/ui_state.h.
// ---------------------------------------------------------------------------
static uint8_t mapGovState(uint8_t govState)
{
    return (uint8_t) (govState + 1);
}

static uint8_t mapGovLimiter(uint8_t govLimiter)
{
    switch (govLimiter) {
    case gov::LIM_VR:
        return UI_LIM_VR;
    case gov::LIM_VRI:
        return UI_LIM_VRI;
    case gov::LIM_PIN:
        return UI_LIM_PIN;
    case gov::LIM_IIN:
        return UI_LIM_IIN;
    case gov::LIM_IOUT:
        return UI_LIM_IOUT;
    case gov::LIM_VIN:
        return UI_LIM_VIN;
    default:
        return UI_LIM_NONE; // gov has no LIM_HASHRATE; UI_LIM_HASHRATE is unreachable from here
    }
}

// ---------------------------------------------------------------------------
// uiDataInit
// ---------------------------------------------------------------------------

void uiDataInit()
{
    if (S) {
        return; // already initialized
    }

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "failed to create mutex - display data layer disabled");
        return;
    }

    S = (UiInternal *) heap_caps_malloc(sizeof(UiInternal), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!S) {
        ESP_LOGE(TAG, "PSRAM allocation of %u bytes failed - display data layer disabled", (unsigned) sizeof(UiInternal));
        vSemaphoreDelete(s_mutex);
        s_mutex = nullptr;
        return;
    }
    memset(S, 0, sizeof(UiInternal));

    S->curHour = -1;
    S->lastMinuteBucket = -1;
    S->ambEma = NAN;
    S->vrEmaPrevMinute = NAN;
    S->slopeEma = NAN;
    S->lastShareDiff = NAN;

    //   Restore the kWh-today counter. If the device rebooted on the same
    //   local day, this keeps the total intact; on a different day (or if the
    //   clock was never synced last session) uiDataTick will start counting
    //   from the value below - which is then either corrected to 0 once the
    //   clock re-syncs and we detect a local-midnight crossing, or, if it was
    //   simply never saved, just 0.
    uint64_t savedWh = Config::getUiEnergyWh();
    uint64_t savedEpoch = Config::getUiEnergyEpochS();
    S->kwhAccum = (double) savedWh / 1000.0;
    S->lastEnergyEpochS = (int64_t) savedEpoch;

    uiDataReloadConfig();

    ESP_LOGI(TAG, "initialized, %u bytes in PSRAM", (unsigned) sizeof(UiInternal));
}

void uiDataReloadConfig()
{
    uint32_t mask = Config::getScrMask();
    uint16_t secs = Config::getScrSecs();
    if (secs == 0) {
        secs = 10;
    }
    uint16_t durs[UI_MAX_SCREENS];
    Config::getScrDurations(durs);
    if (!S) {
        return;
    }
    UiLock lock;
    S->scrMask = mask;
    S->scrSecs = secs;
    memcpy(S->scrDurs, durs, sizeof(S->scrDurs));
}

uint32_t uiRotationMask()
{
    if (!S) {
        return Config::getScrMaskDefault();
    }
    UiLock lock;
    return S->scrMask;
}

uint16_t uiRotationSeconds()
{
    if (!S) {
        return 10;
    }
    UiLock lock;
    return S->scrSecs;
}

uint16_t uiRotationScreenSecs(int screenId)
{
    uint16_t defaultSecs = uiRotationSeconds();
    if (!S || screenId < 0 || screenId >= UI_MAX_SCREENS) {
        return defaultSecs;
    }
    uint16_t ownSecs;
    {
        UiLock lock;
        ownSecs = S->scrDurs[screenId];
    }
    return uiResolveScreenSecs(ownSecs, defaultSecs);
}

// ---------------------------------------------------------------------------
// uiDataTick - called every 2s from PowerManagementTask, right after
// readAndPublishPowerTelemetry. No I/O other than an occasional NVS write
// (energy persistence, rate-limited to once per 30 min).
// ---------------------------------------------------------------------------

void uiDataTick()
{
    if (!S) {
        return;
    }

    int64_t nowUs = esp_timer_get_time();
    float freq = POWER_MANAGEMENT_MODULE.getEffectiveFrequency();
    float vrTemp = POWER_MANAGEMENT_MODULE.getVRTemp();
    float pinW = POWER_MANAGEMENT_MODULE.getPower();
    float hrInst = HASHRATE_MONITOR.getSmoothedTotalChipHashrate();

    //   ---- dt (always monotonic, always available) --------------------------
    float dtS = UI_DEFAULT_TICK_S;
    {
        UiLock lock;
        if (S->haveLastTick) {
            dtS = (float) (nowUs - S->lastTickUs) / 1.0e6f;
            if (!(dtS > 0.0f) || dtS > 60.0f) {
                dtS = UI_DEFAULT_TICK_S; // clock jump or first-ever tick: don't corrupt the integral
            }
        }
        S->lastTickUs = nowUs;
        S->haveLastTick = true;
    }

    //   ---- ambient estimate + slope ------------------------------------------
    float ambientNow = uiAmbientFromVr(vrTemp, freq, kSweepMhz, kSweepDVr, UI_SWEEP_POINTS);
    {
        UiLock lock;
        S->ambEma = uiEmaStep(S->ambEma, ambientNow, dtS, UI_AMBIENT_TAU_S);
    }

    //   ---- energy: integrate pin, reset kWh-today at local midnight ---------
    {
        UiLock lock;
        S->kwhAccum = uiEnergyIntegrate(S->kwhAccum, pinW, dtS);

        if (is_time_synced()) {
            int64_t nowEpoch = (int64_t) now;
            if (S->lastEnergyEpochS > 0 && uiCrossedLocalMidnight(S->lastEnergyEpochS, nowEpoch)) {
                S->kwhAccum = 0.0;
                ESP_LOGI(TAG, "local midnight crossed - kWh-today counter reset");
            }
            S->lastEnergyEpochS = nowEpoch;
        }
        //   clock not synced: lastEnergyEpochS stays whatever it was (0 on a
        //   fresh boot with nothing restored), so the counter simply never
        //   resets - i.e. it counts since boot, as documented in ui_data.h.

        uint32_t nowMs = (uint32_t) (nowUs / 1000);
        if (nowMs - S->lastNvsSaveMs >= UI_NVS_ENERGY_SAVE_PERIOD_MS) {
            S->lastNvsSaveMs = nowMs;
            uint64_t wh = (uint64_t) (S->kwhAccum * 1000.0 + 0.5);
            uint64_t epoch = (uint64_t) (S->lastEnergyEpochS > 0 ? S->lastEnergyEpochS : 0);
            //   NVS I/O happens here, briefly, while s_mutex is held; this is
            //   the one exception to "never hold the mutex during I/O" and it
            //   is deliberately rate-limited to twice an hour so flash wear
            //   and the (short) blocking time stay negligible against the 2s
            //   tick period.
            Config::setUiEnergyWh(wh);
            Config::setUiEnergyEpochS(epoch);
        }
    }

    //   ---- 24h history + per-hour averages, committed once a minute ---------
    {
        UiLock lock;
        S->minN++;
        S->minGhsSum += hrInst;
        S->minFreqSum += freq;
        if (!isnan(vrTemp) && vrTemp > 0.0f) {
            S->minVrSum += vrTemp;
        }
        if (!isnan(S->ambEma)) {
            S->minAmbSum += S->ambEma;
        }
        S->minPinSum += pinW;

        int64_t minuteBucket = nowUs / 1000000LL / 60LL;
        if (S->lastMinuteBucket < 0) {
            S->lastMinuteBucket = minuteBucket;
        } else if (minuteBucket != S->lastMinuteBucket) {
            S->lastMinuteBucket = minuteBucket;
            float n = (float) (S->minN > 0 ? S->minN : 1);
            float avgGhs = (float) (S->minGhsSum / n);
            float avgFreq = (float) (S->minFreqSum / n);
            float avgVr = (S->minVrSum > 0.0) ? (float) (S->minVrSum / n) : NAN;
            float avgAmb = (S->minAmbSum != 0.0) ? (float) (S->minAmbSum / n) : NAN;
            float avgPin = (float) (S->minPinSum / n);

            uiRingPush<float>(S->histGhs, UI_HIST_POINTS, S->histHead, S->histCount, avgGhs);
            //   The other rings (+ the internal pin ring) share one cursor:
            //   write them directly at the slot histGhs's push just used,
            //   instead of re-running uiRingPush (which would advance the
            //   shared head/count four more times).
            int pushHead = (S->histHead - 1 + UI_HIST_POINTS) % UI_HIST_POINTS;
            S->histFreq[pushHead] = avgFreq;
            S->histVr[pushHead] = avgVr;
            S->histAmb[pushHead] = avgAmb;
            S->histPinW[pushHead] = avgPin;

            //   slope of the smoothed vrTemp, in degC/min, itself smoothed
            //   (EMA of differences, not raw per-sample noise)
            if (!isnan(avgVr)) {
                if (!isnan(S->vrEmaPrevMinute)) {
                    float rawSlope = avgVr - S->vrEmaPrevMinute; // degC over ~1 minute
                    S->slopeEma = uiEmaStep(S->slopeEma, rawSlope, 60.0f, UI_SLOPE_TAU_S);
                }
                S->vrEmaPrevMinute = avgVr;
            }

            if (is_time_synced()) {
                time_t t = (time_t) now;
                struct tm tmNow{};
                localtime_r(&t, &tmNow);
                uiHourBucketPush(S->hourGhs, S->hourN, S->curHour, tmNow.tm_hour, avgGhs);
                //   curHour is shared: push the rest into the SAME hour index
                //   uiHourBucketPush just selected, without letting it reset
                //   again (it only resets on an actual hour change).
                int h = tmNow.tm_hour;
                int n2 = S->hourN[h]; // already incremented by the call above
                float prevWeight = (float) (n2 - 1);
                S->hourFreq[h] = (S->hourFreq[h] * prevWeight + avgFreq) / (float) n2;
                if (!isnan(avgVr)) {
                    S->hourVr[h] = (S->hourVr[h] * prevWeight + avgVr) / (float) n2;
                }
                if (!isnan(avgAmb)) {
                    S->hourAmb[h] = (S->hourAmb[h] * prevWeight + avgAmb) / (float) n2;
                }
            }

            S->minN = 0;
            S->minGhsSum = S->minFreqSum = S->minVrSum = S->minAmbSum = S->minPinSum = 0.0;
        }
    }

    //   ---- shares: detect new accepted shares, push into the recent ring ----
    {
        uint64_t accepted = STRATUM_MANAGER ? STRATUM_MANAGER->getSharesAccepted() : 0;
        uint32_t poolDiff = STRATUM_MANAGER ? STRATUM_MANAGER->getPoolDifficulty() : 0;
        UiLock lock;
        if (S->haveLastShares && accepted > S->lastSharesAccepted) {
            uint64_t delta = accepted - S->lastSharesAccepted;
            if (delta > (uint64_t) UI_SHARES_RECENT) {
                delta = UI_SHARES_RECENT; // bounded loop; only the ring size worth of entries can matter anyway
            }
            int64_t epoch = is_time_synced() ? (int64_t) now : 0;
            for (uint64_t i = 0; i < delta; i++) {
                UiShare sh;
                sh.epochS = epoch;
                //   No per-share difficulty source exists on this firmware
                //   (StratumManager only tracks best/best-session diff); the
                //   pool difficulty is the documented fallback, same as the
                //   recent[] entries below.
                sh.diff = (double) poolDiff;
                uiRingPush<UiShare>(S->sharesRing, UI_SHARES_RECENT, S->sharesHead, S->sharesCount, sh);
            }
            S->lastShareDiff = (double) poolDiff;
            S->lastShareMonoUs = nowUs;
        }
        S->lastSharesAccepted = accepted;
        S->haveLastShares = true;
    }

    //   ---- block: detect a height change, track time since it happened ------
    {
        uint32_t height = APIs_FETCHER.getBlockHeight();
        UiLock lock;
        if (height != 0) {
            if (S->haveLastHeight && height != S->lastHeight) {
                S->everSawBlockChange = true;
                S->lastBlockChangeMonoUs = nowUs;
            }
            S->lastHeight = height;
            S->haveLastHeight = true;
        }
    }
}

// ---------------------------------------------------------------------------
// uiDataDiaryPush
// ---------------------------------------------------------------------------

void uiDataDiaryPush(uint8_t kind, const char *text)
{
    if (!S || !text) {
        return;
    }
    UiDiaryEntry e{};
    e.epochS = is_time_synced() ? (int64_t) now : 0;
    e.kind = kind;
    snprintf(e.text, sizeof(e.text), "%s", text);

    UiLock lock;
    uiRingPush<UiDiaryEntry>(S->diaryRing, UI_DIARY_MAX, S->diaryHead, S->diaryCount, e);
}

// ---------------------------------------------------------------------------
// uiDataGetEnergySnapshot - lightweight, for GET /api/system/info
// ---------------------------------------------------------------------------

void uiDataGetEnergySnapshot(float &kwhToday, float &costToday)
{
    if (!S) {
        kwhToday = NAN;
        costToday = NAN;
        return;
    }
    double kwh;
    {
        UiLock lock;
        kwh = S->kwhAccum;
    }
    kwhToday = (float) kwh;
    costToday = kwhToday * ((float) Config::getTarifaCents() / 100.0f);
}

// ---------------------------------------------------------------------------
// uiDataFill
// ---------------------------------------------------------------------------

void uiDataFill(UiState &out)
{
    uiStateSetDefaults(out);

    Board *board = SYSTEM_MODULE.getBoard();
    if (!board) {
        return; // nothing else can be filled safely yet (e.g. very early boot)
    }

    //   ---- config -------------------------------------------------------
    out.tarifa = Config::getTarifaPerKwh();
    {
        char *currency = Config::getCurrency();
        snprintf(out.currency, sizeof(out.currency), "%s", currency ? currency : "R$");
        free(currency);
    }
    out.psuW = UI_PSU_W;
    out.ioutFault = UI_IOUT_FAULT_A;
    {
        uint16_t vrCut = Config::getFanOverheatTemp(1);
        if (vrCut > 0) {
            out.vrCut = (float) vrCut;
        }
    }
    out.asicCount = board->getAsicCount();
    if (out.asicCount <= 0 || out.asicCount > 4) {
        out.asicCount = 4;
    }

    //   ---- operating point ------------------------------------------------
    out.freq = POWER_MANAGEMENT_MODULE.getEffectiveFrequency();
    out.mv = POWER_MANAGEMENT_MODULE.getEffectiveVoltageMillis();
    out.voutMv = POWER_MANAGEMENT_MODULE.getVout() * 1000.0f;

    //   ---- hashrate ---------------------------------------------------------
    out.hrInst = HASHRATE_MONITOR.getSmoothedTotalChipHashrate();
    History *history = SYSTEM_MODULE.getHistory();
    if (history) {
        out.hr1m = (float) history->getCurrentHashrate1m();
        out.hr10m = (float) history->getCurrentHashrate10m();
        out.hr1h = (float) history->getCurrentHashrate1h();
        out.hr1d = (float) history->getCurrentHashrate1d();
    }
    for (int i = 0; i < 4; i++) {
        out.chipGhs[i] = (i < out.asicCount) ? HASHRATE_MONITOR.getChipHashrate(i) : NAN;
    }

    //   ---- power --------------------------------------------------------
    out.pin = POWER_MANAGEMENT_MODULE.getPower();
    out.vin = POWER_MANAGEMENT_MODULE.getVin();
    out.iin = POWER_MANAGEMENT_MODULE.getIin();
    out.iout = POWER_MANAGEMENT_MODULE.getIout();
    out.pout = POWER_MANAGEMENT_MODULE.getPout();
    out.fanW = UI_FAN_W;
    if (out.pin > 0.0f && out.pout > 0.0f) {
        out.vrLoss = out.pin - out.pout - UI_FAN_W;
        if (out.vrLoss < 0.0f) {
            out.vrLoss = 0.0f; // measurement noise can make pin < pout+fan momentarily
        }
    }
    if (out.pin > 0.0f && out.hr1m > 0.0f) {
        out.effJth = out.pin / (out.hr1m / 1000.0f); // W per TH/s (hr1m is GH/s)
    }
    out.psuLoad = (UI_PSU_W > 0.0f) ? out.pin / UI_PSU_W : NAN;

    //   ---- temperatures -----------------------------------------------------
    out.vrTemp = POWER_MANAGEMENT_MODULE.getVRTemp();
    out.vrTempInt = POWER_MANAGEMENT_MODULE.getVRTempInt();
    out.boardTemp = POWER_MANAGEMENT_MODULE.getChipTempMax();
    if (S) {
        UiLock lock;
        out.ambient = S->ambEma;
        out.slopeCpm = S->slopeEma;
    }
    //   headroom needs vrTarget, which the governor block below fills in; it
    //   is computed once, after that block.

    //   ---- fan ----------------------------------------------------------
    out.fanPct = POWER_MANAGEMENT_MODULE.getFanPerc();
    out.fanRpm = POWER_MANAGEMENT_MODULE.getFanRPM(0);

    //   ---- governor (needs PowerManagementTask's own lock) -------------------
    {
        LockGuard pmLock(POWER_MANAGEMENT_MODULE);
        gov::ThermalGovernor *governor = POWER_MANAGEMENT_MODULE.getGovernor();
        const gov::GovDecision *last = POWER_MANAGEMENT_MODULE.getGovernorDecision();
        int mode = POWER_MANAGEMENT_MODULE.getGovernorMode();
        out.govMode = (uint8_t) mode;

        if (governor) {
            const gov::GovConfig &cfg = governor->config();
            out.freqCap = cfg.freqCap;
            out.fmin = cfg.fmin;
            out.mvCap = cfg.voltageCap;
            out.vrTarget = cfg.vrTarget;
            out.vrHard = cfg.vrTarget + cfg.vrHardDelta;
            out.vriTarget = cfg.vriTarget;
            out.vriWarn = cfg.vriTarget + cfg.vriHardDelta;
            if (out.vriWarn <= out.vriTarget) {
                out.vriWarn = UI_VRI_WARN_C;
            }
            out.ioutMax = cfg.ioutMax;
            out.hrExpected = cfg.expectedHashrate(out.freq);
            out.govUps = governor->countUps();
            out.govDowns = governor->countDowns();
            out.govEmergencies = governor->countEmergencies();
        }

        if (mode == 0) {
            out.govState = UI_GOV_OFF;
        } else if (last) {
            out.govState = mapGovState(last->state);
        }
        if (last) {
            out.govLimiter = mapGovLimiter(last->limiter);
            out.govMargin = last->margin;
            out.govBlockedFreq = last->fBlocked.valid ? last->fBlocked.value : NAN;
            out.govLastAction = last->action; // gov::Action and UiState's govLastAction comment agree 1:1
            //   last->reason (GOV_REASON_LEN=80) can be longer than
            //   govLastReason (UI_DIARY_TEXT=72); an explicit precision lets
            //   the compiler prove the snprintf can never truncate silently
            //   past what it already bounds (-Werror=format-truncation).
            snprintf(out.govLastReason, sizeof(out.govLastReason), "%.*s", (int) sizeof(out.govLastReason) - 1, last->reason);
        }
        out.govLastActionAgeS = POWER_MANAGEMENT_MODULE.getGovernorLastActionAgeS();
    }
    out.headroom = out.vrTarget - out.vrTemp;

    //   ---- shares -------------------------------------------------------
    if (STRATUM_MANAGER) {
        out.sharesAccepted = STRATUM_MANAGER->getSharesAccepted();
        out.sharesRejected = STRATUM_MANAGER->getSharesRejected();
        out.poolDiff = (double) STRATUM_MANAGER->getPoolDifficulty();
        out.bestSession = (double) STRATUM_MANAGER->getBestSessionDiff();
    }
    out.bestEver = (double) Config::getBestDiff();
    if (out.poolDiff > 0.0 && out.hrInst > 0.0f) {
        out.sharesPerMin = (float) ((double) out.hrInst * 1.0e9 / (out.poolDiff * 4294967296.0) * 60.0);
    }
    if (S) {
        UiLock lock;
        out.lastShareDiff = S->lastShareDiff;
        if (S->sharesCount > 0) {
            out.lastShareAgeS = (float) (esp_timer_get_time() - S->lastShareMonoUs) / 1.0e6f;
        }
        out.recentCount = S->sharesCount;
        uiRingUnrollOldestFirst<UiShare>(S->sharesRing, UI_SHARES_RECENT, S->sharesHead, S->sharesCount, out.recent);
    }

    //   ---- network / lottery ---------------------------------------------
    out.height = APIs_FETCHER.getBlockHeight();
    //   APIsFetcher stores the difficulty already divided by 1e12 ("T", see
    //   apis_task.cpp); every UiState consumer (uiComputeOdds) expects the raw
    //   difficulty, so scale it back. Without this the per-block chance came
    //   out > 1 and the Luck screen showed "0 never".
    out.netDiff = (double) APIs_FETCHER.getNetDifficulty() * 1.0e12;
    out.netHashrateEHs = (double) APIs_FETCHER.getNetHash() / 1.0e18;
    out.netValid = (out.height != 0 && out.netDiff > 0.0);
    if (out.netValid) {
        out.epochPos = out.height % 2016;
        out.epochLeft = 2016 - out.epochPos;
        out.halvingLeft = APIs_FETCHER.getBlocksToHalving();
        out.halvingAt = out.height + out.halvingLeft;
        out.halvingDays = (float) out.halvingLeft * 600.0f / 86400.0f;
        out.cyclePos = 1.0f - (float) out.halvingLeft / 210000.0f;
        out.subsidyBtc = 3.125f;
        //   Fee reward is an approximation, not derived from mempool data
        //   (which this firmware does not fetch): +0.04 BTC, same constant
        //   the approved design reference uses (FEES_BTC).
        out.rewardBtc = out.subsidyBtc + 0.04f;

        UiOdds odds = uiComputeOdds(out.hrInst, out.netDiff);
        out.oddsPerBlock = odds.perBlock;
        out.oddsPerDay = odds.perDay;
        out.oddsPerYear = odds.perYear;
        out.expectedYears = odds.expectedYears;
        out.vsMega = odds.vsMega;
    }
    if (S) {
        UiLock lock;
        if (S->everSawBlockChange) {
            out.sinceBlockS = (float) (esp_timer_get_time() - S->lastBlockChangeMonoUs) / 1.0e6f;
        } // else stays -1 (uiStateSetDefaults), "no change observed yet"
    }

    //   ---- energy ---------------------------------------------------------
    if (S) {
        double kwh, avgWSum = NAN;
        int histCount;
        {
            UiLock lock;
            kwh = S->kwhAccum;
            histCount = S->histCount;
            if (histCount > 0) {
                double sum = 0.0;
                for (int i = 0; i < histCount; i++) {
                    sum += S->histPinW[i];
                }
                avgWSum = sum / histCount;
            }
        }
        out.energyValid = true;
        out.kwhToday = (float) kwh;
        float tarifaReais = out.tarifa;
        out.costToday = out.kwhToday * tarifaReais;
        if (!isnan(avgWSum)) {
            out.avgW24 = (float) avgWSum;
            out.costDay = out.avgW24 * 24.0f / 1000.0f * tarifaReais;
            out.costMonth = out.avgW24 * 24.0f * 30.0f / 1000.0f * tarifaReais;
            out.kwhMonth = out.avgW24 * 24.0f * 30.0f / 1000.0f;
        }
    }

    //   ---- connectivity -----------------------------------------------------
    out.wifiConnected = (strcmp(SYSTEM_MODULE.getWifiStatus(), "Connected!") == 0);
    out.poolConnected = STRATUM_MANAGER ? STRATUM_MANAGER->isAnyConnected() : false;
    out.rssi = SYSTEM_MODULE.get_wifi_rssi();
    snprintf(out.ssid, sizeof(out.ssid), "%s", SYSTEM_MODULE.getSsid());
    snprintf(out.ip, sizeof(out.ip), "%s", SYSTEM_MODULE.getIPAddress());
    {
        char *stratumUrl = Config::getStratumURL();
        snprintf(out.poolHost, sizeof(out.poolHost), "%s", stratumUrl ? stratumUrl : "");
        free(stratumUrl);
    }
    out.pingMs = (int) get_last_ping_rtt;
    out.uptimeS = (float) ((esp_timer_get_time() - SYSTEM_MODULE.getStartTime()) / 1000000.0);
    {
        const esp_app_desc_t *desc = esp_app_get_description();
        if (desc) {
            snprintf(out.fw, sizeof(out.fw), "%s", desc->version);
        }
    }

    //   ---- wall clock -------------------------------------------------------
    out.clockValid = is_time_synced();
    out.epochS = out.clockValid ? (int64_t) now : 0;

    //   ---- history (unrolled oldest-first) + per-hour averages + diary ------
    if (S) {
        UiLock lock;
        out.histCount = S->histCount;
        uiRingUnrollOldestFirst<float>(S->histGhs, UI_HIST_POINTS, S->histHead, S->histCount, out.histGhs);
        uiRingUnrollOldestFirst<float>(S->histFreq, UI_HIST_POINTS, S->histHead, S->histCount, out.histFreq);
        uiRingUnrollOldestFirst<float>(S->histVr, UI_HIST_POINTS, S->histHead, S->histCount, out.histVr);
        uiRingUnrollOldestFirst<float>(S->histAmb, UI_HIST_POINTS, S->histHead, S->histCount, out.histAmb);

        memcpy(out.hourGhs, S->hourGhs, sizeof(out.hourGhs));
        memcpy(out.hourFreq, S->hourFreq, sizeof(out.hourFreq));
        memcpy(out.hourVr, S->hourVr, sizeof(out.hourVr));
        memcpy(out.hourAmb, S->hourAmb, sizeof(out.hourAmb));
        memcpy(out.hourN, S->hourN, sizeof(out.hourN));

        out.diaryCount = S->diaryCount;
        uiRingUnrollNewestFirst<UiDiaryEntry>(S->diaryRing, UI_DIARY_MAX, S->diaryHead, S->diaryCount, out.diary);
    }
}
