#pragma once

// Data layer for the rotating display screens: collects everything a UiState
// needs from the existing modules (power management, governor, hashrate
// monitor, stratum, APIs fetcher, SNTP) plus the new derived data (24 h
// history, energy, estimated room temperature, governor diary, odds).
//
// Threading: uiDataTick runs from the power management task (every 2 s);
// uiDataFill copies into the caller's buffer under a short internal lock and
// is safe to call from the display side. Neither touches I2C or the network.

#include "displays/ui_state.h"

void uiDataInit(); // allocate PSRAM buffers; call once before the tasks start
void uiDataTick(); // sample/integrate; called from the power management loop
void uiDataFill(UiState &out); // snapshot for the screens

// Governor diary: the power management task calls this on every governor action.
void uiDataDiaryPush(uint8_t kind, const char *text);

// Rotation config (NVS): bit n = screen number n enabled; seconds per screen.
uint32_t uiRotationMask();
uint16_t uiRotationSeconds();

// Per-screen display duration: `screenId`'s own configured value if set
// (>0), otherwise the global default (uiRotationSeconds). Backs
// ScreenManager::tick (main/displays/screens/screen_manager.cpp) and
// GET /api/system/screens's per-screen "secs" (main/http_server/
// handler_system.cpp). The actual own-vs-default decision is the pure
// uiResolveScreenSecs below so it can be unit-tested on the host without
// NVS/FreeRTOS; this wrapper only adds the thread-safe cache lookup.
uint16_t uiRotationScreenSecs(int screenId);

// Re-reads tarifa/scr_mask/scr_secs from NVS into the in-RAM cache used by
// uiRotationMask/uiRotationSeconds/uiDataFill. Call after PATCH
// /api/system writes any of them (mirrors reloadGovernorConfig).
void uiDataReloadConfig();

// Lightweight energy snapshot (kWh "today" and its cost at the current
// tarifa) for GET /api/system/info's "energy" object, without building a
// full ~30 KB UiState on the HTTP task's stack just to read two numbers.
void uiDataGetEnergySnapshot(float &kwhToday, float &costToday);

// ---------------------------------------------------------------------------
// Pure math / data-structure helpers used by ui_data.cpp — NO ESP-IDF, NO
// FreeRTOS, NO NVS, NO dynamic allocation. Kept here (instead of in
// ui_data.cpp) so test/host_ui_data/test_ui_data.cpp can `#include
// "ui_data.h"` and exercise them with a plain host g++, exactly the same
// functions the firmware runs. See
// ---------------------------------------------------------------------------

#include <math.h>
#include <string.h>
#include <time.h>

// Linear interpolation over a table sorted by ascending x; clamps to the
// first/last row outside the range. Used for the deltaVR/deltaVRint(freq)
// sweep, same shape as NQ.model.dVr in the approved design reference.
inline float uiInterp(const float *xs, const float *ys, int n, float x)
{
    if (n <= 0) {
        return NAN;
    }
    if (n == 1 || x <= xs[0]) {
        return ys[0];
    }
    for (int i = 1; i < n; i++) {
        if (x <= xs[i]) {
            float t = (x - xs[i - 1]) / (xs[i] - xs[i - 1]);
            return ys[i - 1] + (ys[i] - ys[i - 1]) * t;
        }
    }
    return ys[n - 1];
}

// Estimated room temperature: vrTemp minus the regulator's rise-over-ambient
// at this frequency (measured sweep). NAN if vrTemp is not a plausible
// reading (i.e. the sensor/telemetry is not available yet).
inline float uiAmbientFromVr(float vrTemp, float freqMhz, const float *sweepMhz, const float *sweepDVr, int n)
{
    if (!(vrTemp > 0.0f)) {
        return NAN;
    }
    return vrTemp - uiInterp(sweepMhz, sweepDVr, n, freqMhz);
}

// One EMA step with time constant tauS; snaps straight to `value` when
// `prev` is NAN (filter not seeded yet) so callers don't need a separate
// "first sample" branch.
inline float uiEmaStep(float prev, float value, float dtS, float tauS)
{
    if (isnan(prev) || !(tauS > 0.0f) || !(dtS > 0.0f)) {
        return value;
    }
    float a = 1.0f - expf(-dtS / tauS);
    return prev + (value - prev) * a;
}

// kWh accumulated after dtS seconds at pinW watts (0 if pin/dt are not
// plausible, so a momentary bad reading never contributes negative energy).
inline double uiEnergyIntegrate(double kwhAccum, float pinW, float dtS)
{
    if (!(pinW > 0.0f) || !(dtS > 0.0f)) {
        return kwhAccum;
    }
    return kwhAccum + (double) pinW * (double) dtS / 3600000.0;
}

// True if nowEpochS fell on a different local calendar day than prevEpochS.
// Goes through localtime_r (which honours TZ, including DST) rather than a
// fixed 86400 s modulus.
inline bool uiCrossedLocalMidnight(int64_t prevEpochS, int64_t nowEpochS)
{
    if (prevEpochS <= 0 || nowEpochS <= 0) {
        return false;
    }
    time_t a = (time_t) prevEpochS;
    time_t b = (time_t) nowEpochS;
    struct tm ta{};
    struct tm tb{};
    localtime_r(&a, &ta);
    localtime_r(&b, &tb);
    return ta.tm_year != tb.tm_year || ta.tm_yday != tb.tm_yday;
}

// Lottery odds, replicated 1:1 from the approved design reference
// (NET_DIFF * 2^32 / 600 = network hashes/s; 144 blocks/day; Mega-Sena odds
// 1 in 50 063 860).
struct UiOdds
{
    double perBlock, perDay, perYear, expectedYears, vsMega;
};
inline UiOdds uiComputeOdds(double hashrateGhs, double netDiff)
{
    UiOdds o{NAN, NAN, NAN, NAN, NAN};
    if (!(hashrateGhs > 0.0) || !(netDiff > 0.0)) {
        return o;
    }
    double networkHps = netDiff * 4294967296.0 / 600.0;
    o.perBlock = (hashrateGhs * 1.0e9) / networkHps;
    o.perDay = 1.0 - pow(1.0 - o.perBlock, 144.0);
    o.perYear = 1.0 - pow(1.0 - o.perDay, 365.0);
    o.expectedYears = (o.perDay > 0.0) ? 1.0 / (o.perDay * 365.0) : NAN;
    o.vsMega = o.perDay * 50063860.0;
    return o;
}

// Fixed-size ring buffer helpers (push + unroll), shared by the 24 h
// history, the recent-shares ring and the governor diary. `head` is the
// index the NEXT push writes to; `count` saturates at `cap`. No allocation:
// `ring` and `out` are caller-owned fixed arrays.
template <typename T> inline void uiRingPush(T *ring, int cap, int &head, int &count, const T &value)
{
    if (cap <= 0) {
        return;
    }
    ring[head] = value;
    head = (head + 1) % cap;
    if (count < cap) {
        count++;
    }
}

// Unrolls oldest-first into `out` (out must hold `count` elements).
template <typename T> inline void uiRingUnrollOldestFirst(const T *ring, int cap, int head, int count, T *out)
{
    if (cap <= 0 || count <= 0) {
        return;
    }
    int oldest = (head - count + cap * 2) % cap;
    for (int i = 0; i < count; i++) {
        out[i] = ring[(oldest + i) % cap];
    }
}

// Unrolls newest-first into `out` (out must hold `count` elements).
template <typename T> inline void uiRingUnrollNewestFirst(const T *ring, int cap, int head, int count, T *out)
{
    if (cap <= 0 || count <= 0) {
        return;
    }
    for (int i = 0; i < count; i++) {
        int idx = (head - 1 - i + cap * 4) % cap;
        out[i] = ring[idx];
    }
}

// ---------------------------------------------------------------------------
// Per-screen display duration (GET/PATCH /api/system/screens) - pure rules
// shared by ScreenManager::tick, the HTTP handler and
// test/host_ui_data/test_ui_data.cpp. and
// ---------------------------------------------------------------------------

// Screen numbers are 0.31 (same domain as scr_mask's bits); this is also the
// size of the scr_durs NVS blob (see NVS_SCR_DURS_COUNT in nvs_config.h - kept
// in sync by convention, not by a shared header, since nvs_config.h must not
// depend on ui_data.h).
#define UI_MAX_SCREENS 32

// API-level bounds for both the per-screen "secs" and "defaultSecs".
#define UI_SCREEN_SECS_MIN 3
#define UI_SCREEN_SECS_MAX 600

// Resolves how many seconds a screen should stay on screen: its own
// configured duration if set (ownSecs > 0), otherwise the global default.
// Pure - no I/O - so the display rotation and the tests share the exact
// same rule ("own duration wins if configured, else fall back to default").
inline uint16_t uiResolveScreenSecs(uint16_t ownSecs, uint16_t defaultSecs)
{
    return (ownSecs > 0) ? ownSecs : defaultSecs;
}

// One proposed change from a PATCH /api/system/screens request body's
// "screens" array. `id` is always meaningful; hasEnabled/hasSecs say
// whether that item touches "enabled"/"secs" at all (a partial patch is
// allowed to send only one of the two per screen).
struct UiScreenPatchItem
{
    int id;
    bool hasEnabled;
    bool enabled;
    bool hasSecs;
    uint16_t secs;
};

enum UiScreenPatchError
{
    UI_SCREEN_PATCH_OK = 0,
    UI_SCREEN_PATCH_UNKNOWN_ID,   // items[i].id is not a registered screen
    UI_SCREEN_PATCH_SECS_RANGE,   // items[i].secs outside [minSecs, maxSecs]
    UI_SCREEN_PATCH_NONE_ENABLED, // applying the patch would disable every registered screen
};

// Validates a WHOLE PATCH /api/system/screens body against the registered
// screen ids and the mask BEFORE anything is written to NVS/the cache: the
// handler must call this first and, on any error, write nothing at all
// (fail the whole request instead of applying a partial update). Pure - no
// NVS/HTTP - so it is unit-tested here exactly as the firmware runs it.
//
// registeredIds/registeredCount: every screen id from screen_registry.cpp
// (screenRegistryAll). currentMask: the CURRENT scr_mask (bit N = screen
// N enabled), i.e. Config::getScrMask before the patch is applied.
// outBadId (optional): on UI_SCREEN_PATCH_UNKNOWN_ID or
// UI_SCREEN_PATCH_SECS_RANGE, set to the offending item's id so the caller
// can format "Unknown screen id: <id>" itself (this function returns a
// stable enum, not a formatted string, to stay allocation-free).
inline UiScreenPatchError uiValidateScreenPatch(const UiScreenPatchItem *items, int itemCount, const int *registeredIds,
                                                int registeredCount, uint32_t currentMask, uint16_t minSecs, uint16_t maxSecs,
                                                int *outBadId = nullptr)
{
    uint32_t registeredMask = 0;
    for (int i = 0; i < registeredCount; i++) {
        int id = registeredIds[i];
        if (id >= 0 && id < UI_MAX_SCREENS) {
            registeredMask |= (1u << (unsigned) id);
        }
    }

    uint32_t resultMask = currentMask;
    for (int i = 0; i < itemCount; i++) {
        const UiScreenPatchItem &it = items[i];

        bool known = false;
        for (int j = 0; j < registeredCount; j++) {
            if (registeredIds[j] == it.id) {
                known = true;
                break;
            }
        }
        if (!known) {
            if (outBadId)
                *outBadId = it.id;
            return UI_SCREEN_PATCH_UNKNOWN_ID;
        }

        if (it.hasSecs && (it.secs < minSecs || it.secs > maxSecs)) {
            if (outBadId)
                *outBadId = it.id;
            return UI_SCREEN_PATCH_SECS_RANGE;
        }

        if (it.hasEnabled && it.id >= 0 && it.id < UI_MAX_SCREENS) {
            uint32_t bit = 1u << (unsigned) it.id;
            resultMask = it.enabled ? (resultMask | bit) : (resultMask & ~bit);
        }
    }

    if ((resultMask & registeredMask) == 0) {
        return UI_SCREEN_PATCH_NONE_ENABLED;
    }

    return UI_SCREEN_PATCH_OK;
}

// ---------------------------------------------------------------------------
// Power-bill config (GET/PATCH /api/system/screens's "powerBill" object) -
// pure validation shared by the HTTP handler and the host tests.
// ---------------------------------------------------------------------------

// API-level bounds for GET/PATCH /api/system/screens's "powerBill" object.
#define UI_POWERBILL_MIN_PRICE 0.0f
#define UI_POWERBILL_MAX_PRICE 10.0f
#define UI_POWERBILL_CURRENCY_MAX_BYTES 4

// UTF-8 BYTE length check (not code points: "R$" is 2 bytes, the Euro sign
// is 3, the Pound sign is 2) for the currency symbol, with no leading or
// trailing ASCII space. `maxBytes` is the API's fixed limit (4).
inline bool uiValidateCurrencyBytes(const char *s, int maxBytes)
{
    if (!s) {
        return false;
    }
    size_t len = strlen(s);
    if (len < 1 || (int) len > maxBytes) {
        return false;
    }
    if (s[0] == ' ' || s[len - 1] == ' ') {
        return false;
    }
    return true;
}

// Range check for the power-bill price per kWh; NAN/Inf are rejected too
// (a malformed float must never silently become a valid price).
inline bool uiValidatePricePerKwh(float v, float minPrice, float maxPrice)
{
    if (isnan(v) || !isfinite(v)) {
        return false;
    }
    return v >= minPrice && v <= maxPrice;
}

// Per-local-hour running average (24 buckets, index = hour of day 0.23). A
// bucket resets the instant a new occurrence of that hour begins, so it
// naturally holds "the last 24 h" instead of an all-time average. `curHour` is
// caller-owned state (-1 = nothing pushed yet).
inline void uiHourBucketPush(float *hourVal, int *hourN, int &curHour, int hour, float value)
{
    if (hour < 0 || hour > 23) {
        return;
    }
    if (hour != curHour) {
        hourVal[hour] = 0.0f;
        hourN[hour] = 0;
        curHour = hour;
    }
    hourVal[hour] = (hourVal[hour] * (float) hourN[hour] + value) / (float) (hourN[hour] + 1);
    hourN[hour]++;
}
