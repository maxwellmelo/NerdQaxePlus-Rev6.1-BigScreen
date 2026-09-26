#pragma once

// Snapshot of everything the rotating screens show. Filled once per cycle
// OUTSIDE the LVGL task (no I2C, no network, no long locks while drawing) and
// copied under a short lock; screens only ever read it.
//
// Plain data on purpose: this header also compiles on the host (PC screen
// simulator in tools/sim), so no ESP-IDF, FreeRTOS or LVGL includes here.
//
// Field names follow the HTML reference designs' `state` (the approved design reference)
// so each screen can be ported line by line. A value that is not available
// yet is NAN (floats) or 0 with its *_valid flag false; screens must show "--"
// instead of inventing a number.

#include <math.h>
#include <stdint.h>

#define UI_HIST_POINTS 1440 // 24 h at 1 sample/min
#define UI_HOURS 24
#define UI_DIARY_MAX 12
#define UI_DIARY_TEXT 72
#define UI_SHARES_RECENT 48
#define UI_SWEEP_POINTS 6

enum UiGovState : uint8_t
{
    UI_GOV_OFF = 0,
    UI_GOV_STARTUP,
    UI_GOV_RAMP_UP,
    UI_GOV_HOLD,
    UI_GOV_BACKOFF,
    UI_GOV_EMERGENCY,
    UI_GOV_SATURATED_LOW,
    UI_GOV_SENSOR_FAIL,
    UI_GOV_FROZEN,
};

enum UiGovLimiter : uint8_t
{
    UI_LIM_NONE = 0,
    UI_LIM_VR,
    UI_LIM_VRI,
    UI_LIM_IOUT,
    UI_LIM_PIN,
    UI_LIM_IIN,
    UI_LIM_VIN,
    UI_LIM_HASHRATE
};

enum UiDiaryKind : uint8_t
{
    UI_DIARY_GOV = 0,
    UI_DIARY_AMB,
    UI_DIARY_SHARE,
    UI_DIARY_BLOCK,
    UI_DIARY_NET
};

struct UiDiaryEntry
{
    int64_t epochS;           // wall clock (seconds since 1970); 0 if the clock was not synced yet
    uint8_t kind;             // UiDiaryKind
    char text[UI_DIARY_TEXT]; // pt-BR, UTF-8, e.g. "Subiu para 775 MHz"
};

struct UiShare
{
    int64_t epochS;
    double diff;
};

struct UiState
{
    //   ---- limits / config (cfg) -------------------------------------------
    float freqCap, fmin;           // MHz (saved pair = ceiling; governor floor)
    int mvCap;                     // mV
    float vrTarget, vrHard, vrCut; // regulator external sensor: target, hard ceiling, firmware cut-off
    float vriTarget, vriWarn;      // regulator internal sensor
    float ioutMax, ioutFault;      // A
    float psuW;                    // nominal PSU power (120)
    float tarifa;                  // price/kWh (NVS "tarifa", cent resolution)
    char currency[8];              // power-bill currency symbol (NVS "currency", UTF-8, e.g. "R$", "$", "\xe2\x82\xac")

    //   ---- operating point ---------------------------------------------------
    float freq;   // effective MHz
    int mv;       // commanded mV
    float voutMv; // measured core mV

    //   ---- hashrate (GH/s) ---------------------------------------------------
    float hrInst, hr1m, hr10m, hr1h, hr1d, hrExpected;
    float chipGhs[4];
    int asicCount;

    //   ---- power -------------------------------------------------------------
    float pin, vin, iin, iout, pout, vrLoss, fanW;
    float effJth;  // W per TH/s
    float psuLoad; // pin / psuW

    //   ---- temperatures ------------------------------------------------------
    float vrTemp, vrTempInt, boardTemp;
    float ambient;  // ESTIMATED room temperature (NAN if unknown)
    float slopeCpm; // vrTemp trend, degC per minute (smoothed)
    float headroom; // vrTarget - vrTemp

    //   ---- fan ---------------------------------------------------------------
    int fanPct, fanRpm;

    //   ---- governor ----------------------------------------------------------
    uint8_t govMode;    // 0 off, 1 on, 2 shadow
    uint8_t govState;   // UiGovState
    uint8_t govLimiter; // UiGovLimiter
    float govMargin;    // steps of 6.25 MHz (<0 = over the limit)
    uint32_t govUps, govDowns, govEmergencies;
    float govBlockedFreq;  // NAN if none
    uint8_t govLastAction; // 0 HOLD, 1 UP, 2 DOWN, 3 EMERGENCY_DOWN, 4 VTRIM
    char govLastReason[UI_DIARY_TEXT];
    float govLastActionAgeS; // -1 if never

    //   ---- shares ------------------------------------------------------------
    uint64_t sharesAccepted, sharesRejected;
    double lastShareDiff;
    float lastShareAgeS;              // -1 if none yet
    float sharesPerMin;               // expected from hashrate and pool difficulty
    UiShare recent[UI_SHARES_RECENT]; // oldest first
    int recentCount;
    double bestSession, bestEver;

    //   ---- network / lottery -------------------------------------------------
    bool netValid; // block height / difficulty known
    double netDiff;
    double poolDiff;
    uint32_t height;
    float sinceBlockS;            // -1 if unknown
    uint32_t epochPos, epochLeft; // difficulty epoch (2016 blocks)
    uint32_t halvingAt, halvingLeft;
    float halvingDays;
    float cyclePos; // 0..1 within the 210 000-block cycle
    float subsidyBtc, rewardBtc;
    double netHashrateEHs;
    //   odds (computed on the device from hashrate and netDiff)
    double oddsPerBlock, oddsPerDay, oddsPerYear, expectedYears, vsMega;

    //   ---- energy ------------------------------------------------------------
    bool energyValid;
    float kwhToday, costToday, avgW24, costDay, costMonth, kwhMonth;

    //   ---- connectivity ------------------------------------------------------
    bool wifiConnected, poolConnected;
    int rssi;
    char ssid[33];
    char ip[16];
    char poolHost[64];
    int pingMs;
    float uptimeS;
    char fw[40];

    //   ---- wall clock --------------------------------------------------------
    bool clockValid;
    int64_t epochS; // local time already applied via TZ when converted with localtime_r

    //   ---- measured efficiency sweep (screen 06) -----------------------------
    float sweepMhz[UI_SWEEP_POINTS], sweepW[UI_SWEEP_POINTS];

    //   ---- history (screens 13, 14, 16) --------------------------------------
    //   ring already unrolled: index 0 = oldest; histCount valid points, 1/min
    int histCount;
    float histGhs[UI_HIST_POINTS];
    float histFreq[UI_HIST_POINTS];
    float histVr[UI_HIST_POINTS];
    float histAmb[UI_HIST_POINTS];
    //  averages per hour of day (index = local hour 0.23); hourN[h] = samples used
    float hourGhs[UI_HOURS], hourFreq[UI_HOURS], hourVr[UI_HOURS], hourAmb[UI_HOURS];
    int hourN[UI_HOURS];

    //   ---- governor diary (screen 18), newest first ----------------------------
    UiDiaryEntry diary[UI_DIARY_MAX];
    int diaryCount;
};

// UiState is ~30 KB: never put one on a task stack. Allocate with
// heap_caps_malloc(., MALLOC_CAP_SPIRAM) (or a static in PSRAM).
void uiStateSetDefaults(UiState &s); // all NAN / 0 / flags false, cfg constants filled
