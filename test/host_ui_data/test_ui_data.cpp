// Host tests for the pure math / data-structure helpers behind the display
// data layer (main/ui_data.h + main/ui_data.cpp).
//
// Builds with a plain g++ (no ESP-IDF headers) inside the espressif/idf
// container, exactly like test/host_governor/test_governor.cpp:
//
// g++ -std=c++17 -Wall -Wextra -O1 -I main
// test/host_ui_data/test_ui_data.cpp -o /tmp/tui && /tmp/tui
//
// Only ui_data.h is included (it pulls in displays/ui_state.h, which is
// plain data on purpose - see its own header comment). main/ui_data.cpp
// itself needs FreeRTOS/NVS/esp_timer and is NOT part of this build; it is
// compiled and checked separately with the xtensa cross compiler (see the
// firmware report). Every function under test here is a `inline` free
// function or template declared directly in ui_data.h, so this file and
// ui_data.cpp share the exact same code, not a re-implementation.

#include "ui_data.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int g_checks = 0;
static int g_failures = 0;

static void check(bool ok, const char *what)
{
    g_checks++;
    if (!ok) {
        g_failures++;
        printf(" FAIL: %s\n", what);
    }
}

static void checkNear(double a, double b, double eps, const char *what)
{
    g_checks++;
    if (!(fabs(a - b) <= eps)) {
        g_failures++;
        printf(" FAIL: %s (got %.10g, want %.10g +/- %.3g)\n", what, a, b, eps);
    }
}

// ---------------------------------------------------------------------------
// uiInterp / uiAmbientFromVr - the measured deltaVR(freq) sweep
// ---------------------------------------------------------------------------

// Same points as main/displays/ui_state.cpp's sweepMhz/sweepW defaults, from
// an active-calibration frequency sweep on the real hardware.
static const float kSweepMhz[6] = {500, 575, 650, 725, 750, 800};
static const float kSweepDVr[6] = {15.2f, 20.8f, 28.0f, 38.2f, 44.3f, 54.4f};

static void test_interp()
{
    printf("uiInterp / uiAmbientFromVr\n");

    // exact table points
    checkNear(uiInterp(kSweepMhz, kSweepDVr, 6, 500.0f), 15.2, 1e-4, "dVr(500) exact");
    checkNear(uiInterp(kSweepMhz, kSweepDVr, 6, 800.0f), 54.4, 1e-4, "dVr(800) exact");
    checkNear(uiInterp(kSweepMhz, kSweepDVr, 6, 650.0f), 28.0, 1e-4, "dVr(650) exact");

    // midpoint between 725 (38.2) and 750 (44.3) -> 41.25
    checkNear(uiInterp(kSweepMhz, kSweepDVr, 6, 737.5f), 41.25, 1e-3, "dVr(737.5) midpoint");

    // clamp below/above the table
    checkNear(uiInterp(kSweepMhz, kSweepDVr, 6, 300.0f), 15.2, 1e-4, "dVr clamps below table");
    checkNear(uiInterp(kSweepMhz, kSweepDVr, 6, 999.0f), 54.4, 1e-4, "dVr clamps above table");

    // ambient = vrTemp - dVr(freq); measured point from the sweep doc:
    // 725 MHz, VR 71.2 C, room ~33 C that afternoon -> dVr=38.2 -> ambient=33.0
    float amb = uiAmbientFromVr(71.2f, 725.0f, kSweepMhz, kSweepDVr, 6);
    checkNear(amb, 33.0, 0.05, "ambient at 725MHz/71.2C ~ 33C room");

    // invalid vrTemp (sensor not available) -> NAN
    check(std::isnan(uiAmbientFromVr(0.0f, 700.0f, kSweepMhz, kSweepDVr, 6)), "ambient NAN when vrTemp invalid");
    check(std::isnan(uiAmbientFromVr(-1.0f, 700.0f, kSweepMhz, kSweepDVr, 6)), "ambient NAN when vrTemp negative");
}

// ---------------------------------------------------------------------------
// uiEmaStep
// ---------------------------------------------------------------------------

static void test_ema()
{
    printf("uiEmaStep\n");

    // seeding: prev = NAN -> snaps straight to value
    float v = uiEmaStep(NAN, 42.0f, 2.0f, 120.0f);
    checkNear(v, 42.0, 1e-6, "ema seeds from NAN");

    // after 1 tau, ~63.2% of the way from prev to value
    float after1Tau = uiEmaStep(0.0f, 100.0f, 120.0f, 120.0f);
    checkNear(after1Tau, 100.0 * (1.0 - exp(-1.0)), 1e-3, "ema ~63.2% after one tau");

    // many small steps settle at the same place as one big step (linear
    // system - not exactly path independent for EMA with fixed dt chaining,
    // but should converge close to target after several taus)
    float acc = 0.0f;
    for (int i = 0; i < 600; i++) { // 600 * 2s = 1200s = 10 tau
        acc = uiEmaStep(acc, 100.0f, 2.0f, 120.0f);
    }
    checkNear(acc, 100.0, 0.1, "ema converges to target after 10 tau");
}

// ---------------------------------------------------------------------------
// uiEnergyIntegrate - 125W for 24h = 3.0 kWh
// ---------------------------------------------------------------------------

static void test_energy()
{
    printf("uiEnergyIntegrate\n");

    double kwh = uiEnergyIntegrate(0.0, 125.0f, 86400.0f);
    checkNear(kwh, 3.0, 1e-9, "125W * 24h = 3.0kWh (single step)");

    // same total via 2s ticks (matches the real uiDataTick cadence)
    double acc = 0.0;
    for (int i = 0; i < 43200; i++) { // 43200 * 2s = 86400s
        acc = uiEnergyIntegrate(acc, 125.0f, 2.0f);
    }
    checkNear(acc, 3.0, 1e-6, "125W * 24h in 2s ticks = 3.0kWh");

    // implausible readings never subtract or corrupt the accumulator
    double kept = uiEnergyIntegrate(1.5, -5.0f, 2.0f);
    checkNear(kept, 1.5, 1e-9, "negative pin is ignored");
    kept = uiEnergyIntegrate(1.5, 100.0f, 0.0f);
    checkNear(kept, 1.5, 1e-9, "zero dt is ignored");
}

// ---------------------------------------------------------------------------
// uiCrossedLocalMidnight
// ---------------------------------------------------------------------------

static void test_midnight()
{
    printf("uiCrossedLocalMidnight\n");
    setenv("TZ", "<-03>3", 1);
    tzset();

    // Build "2026-09-23 00:00:00 local" with mktime (which honours TZ), so
    // the test never depends on a hand-computed epoch offset.
    struct tm mid{};
    mid.tm_year = 2026 - 1900;
    mid.tm_mon = 9 - 1;
    mid.tm_mday = 23;
    mid.tm_hour = 0;
    mid.tm_min = 0;
    mid.tm_sec = 0;
    mid.tm_isdst = -1;
    time_t midnightLocal = mktime(&mid);
    check(midnightLocal != (time_t) -1, "mktime resolved the local midnight (sanity check)");

    int64_t justBefore = (int64_t) midnightLocal - 60; // 23:59:00 on the 22nd
    int64_t justAfter = (int64_t) midnightLocal + 60;  // 00:01:00 on the 23rd
    check(uiCrossedLocalMidnight(justBefore, justAfter), "crosses local midnight");
    check(!uiCrossedLocalMidnight(justBefore, justBefore + 30), "does not cross within the same minute");
    check(!uiCrossedLocalMidnight(0, justAfter), "invalid prev epoch never reports a crossing");
}

// ---------------------------------------------------------------------------
// uiComputeOdds - 6.31 TH/s ~ 1 in 1.0 million per day (netDiff matches the
// display-reference designs simulator's NET_DIFF constant)
// ---------------------------------------------------------------------------

static void test_odds()
{
    printf("uiComputeOdds\n");

    const double netDiff = 1.2745e14;
    UiOdds o = uiComputeOdds(6310.0, netDiff);

    double oneInDay = 1.0 / o.perDay;
    checkNear(oneInDay / 1.0e6, 1.0, 0.01, "6.31 TH/s ~ 1 in 1.0 million per day");

    // internal consistency with the per-block number
    checkNear(o.perDay, 1.0 - pow(1.0 - o.perBlock, 144.0), 1e-15, "perDay derives from perBlock");
    checkNear(o.perYear, 1.0 - pow(1.0 - o.perDay, 365.0), 1e-15, "perYear derives from perDay");
    checkNear(o.expectedYears, 1.0 / (o.perDay * 365.0), 1e-6, "expectedYears = 1/(perDay*365)");
    checkNear(o.vsMega, o.perDay * 50063860.0, 1e-9, "vsMega uses the Mega-Sena 1/50063860 odds");

    // no data -> NAN, never a fabricated number
    UiOdds bad = uiComputeOdds(0.0, netDiff);
    check(std::isnan(bad.perBlock) && std::isnan(bad.perDay), "zero hashrate -> NAN odds");
    UiOdds bad2 = uiComputeOdds(6310.0, 0.0);
    check(std::isnan(bad2.perBlock), "zero netDiff -> NAN odds (net not valid yet)");
}

// ---------------------------------------------------------------------------
// uiRingPush / uiRingUnrollOldestFirst / uiRingUnrollNewestFirst
// ---------------------------------------------------------------------------

static void test_ring_float()
{
    printf("uiRingPush/unroll (float, 24h history ring)\n");

    const int cap = 1440;
    static float ring[cap];
    int head = 0, count = 0;

    // push fewer than cap: unroll must return exactly what was pushed, in order
    for (int i = 0; i < 100; i++) {
        uiRingPush<float>(ring, cap, head, count, (float) i);
    }
    check(count == 100, "count == number pushed while under capacity");
    static float out[cap];
    uiRingUnrollOldestFirst<float>(ring, cap, head, count, out);
    bool ok = true;
    for (int i = 0; i < 100; i++) {
        if (out[i] != (float) i)
            ok = false;
    }
    check(ok, "unroll oldest-first matches push order under capacity");

    // push past capacity: oldest entries are evicted, ring wraps
    head = 0;
    count = 0;
    for (int i = 0; i < 1450; i++) {
        uiRingPush<float>(ring, cap, head, count, (float) i);
    }
    check(count == cap, "count saturates at capacity");
    uiRingUnrollOldestFirst<float>(ring, cap, head, count, out);
    check(out[0] == 10.0f, "oldest surviving sample after wraparound is #10 (1450-1440)");
    check(out[cap - 1] == 1449.0f, "newest sample after wraparound is #1449");
}

static void test_ring_shares()
{
    printf("uiRingPush/unroll (UiShare, recent-shares ring)\n");

    const int cap = UI_SHARES_RECENT;
    static UiShare ring[cap];
    int head = 0, count = 0;

    for (int i = 0; i < cap + 5; i++) {
        UiShare s;
        s.epochS = 1000 + i;
        s.diff = 32768.0 * (i + 1);
        uiRingPush<UiShare>(ring, cap, head, count, s);
    }
    check(count == cap, "shares ring saturates at UI_SHARES_RECENT");
    static UiShare out[cap];
    uiRingUnrollOldestFirst<UiShare>(ring, cap, head, count, out);
    check(out[0].epochS == 1005, "oldest surviving share is #5 (5 evicted)");
    check(out[cap - 1].epochS == 1000 + cap + 4, "newest share is the last one pushed");
}

static void test_ring_diary()
{
    printf("uiRingPush/unroll (UiDiaryEntry, governor diary, newest first)\n");

    const int cap = UI_DIARY_MAX;
    static UiDiaryEntry ring[cap];
    int head = 0, count = 0;

    for (int i = 0; i < cap + 3; i++) {
        UiDiaryEntry e{};
        e.epochS = 2000 + i;
        e.kind = UI_DIARY_GOV;
        snprintf(e.text, sizeof(e.text), "entrada %d", i);
        uiRingPush<UiDiaryEntry>(ring, cap, head, count, e);
    }
    check(count == cap, "diary ring saturates at UI_DIARY_MAX");
    static UiDiaryEntry out[cap];
    uiRingUnrollNewestFirst<UiDiaryEntry>(ring, cap, head, count, out);
    check(out[0].epochS == 2000 + cap + 2, "diary[0] is the newest entry pushed");
    check(out[cap - 1].epochS == 2003, "diary[last] is the oldest surviving entry (3 evicted)");
    check(strcmp(out[0].text, "entrada 14") == 0, "diary text of the newest entry round-trips");
}

// ---------------------------------------------------------------------------
// uiHourBucketPush - averages per local hour of day, "last 24h" semantics
// ---------------------------------------------------------------------------

static void test_hour_buckets()
{
    printf("uiHourBucketPush\n");

    float hourVal[24] = {0};
    int hourN[24] = {0};
    int curHour = -1;

    uiHourBucketPush(hourVal, hourN, curHour, 5, 10.0f);
    uiHourBucketPush(hourVal, hourN, curHour, 5, 20.0f);
    uiHourBucketPush(hourVal, hourN, curHour, 5, 30.0f);
    checkNear(hourVal[5], 20.0, 1e-6, "hour 5 average of 10,20,30 = 20");
    check(hourN[5] == 3, "hour 5 saw 3 samples");

    uiHourBucketPush(hourVal, hourN, curHour, 6, 40.0f);
    uiHourBucketPush(hourVal, hourN, curHour, 6, 50.0f);
    checkNear(hourVal[6], 45.0, 1e-6, "hour 6 average of 40,50 = 45");
    check(hourN[6] == 2, "hour 6 saw 2 samples");
    // hour 5's bucket from before is untouched by hour 6 pushes
    checkNear(hourVal[5], 20.0, 1e-6, "hour 5 unaffected by hour 6 pushes");

    // a day later, hour 5 comes around again: the bucket resets instead of
    // averaging with yesterday's samples (this is what gives "last 24h")
    uiHourBucketPush(hourVal, hourN, curHour, 5, 100.0f);
    checkNear(hourVal[5], 100.0, 1e-6, "hour 5 resets on its next occurrence");
    check(hourN[5] == 1, "hour 5 sample count resets too");
}

// ---------------------------------------------------------------------------
// uiResolveScreenSecs - per-screen display duration (GET/PATCH
// /api/system/screens), "own value wins if configured, else the default"
// ---------------------------------------------------------------------------

static void test_resolve_screen_secs()
{
    printf("uiResolveScreenSecs\n");

    check(uiResolveScreenSecs(0, 10) == 10, "own=0 (unset) falls back to default");
    check(uiResolveScreenSecs(25, 10) == 25, "own>0 wins over the default");
    check(uiResolveScreenSecs(3, 600) == 3, "own at the minimum bound is honoured");
    check(uiResolveScreenSecs(0, 0) == 0, "both unset -> 0 (caller/API layer applies its own floor)");
}

// ---------------------------------------------------------------------------
// uiValidateScreenPatch - PATCH /api/system/screens whole-body validation
// ---------------------------------------------------------------------------

static void test_validate_screen_patch()
{
    printf("uiValidateScreenPatch\n");

    // Same 15 screens as the real screen_registry.cpp / getScrMaskDefault.
    const int registeredIds[] = {1, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15, 16, 18, 19, 20};
    const int registeredCount = (int) (sizeof(registeredIds) / sizeof(registeredIds[0]));
    uint32_t allEnabledMask = 0;
    for (int id : registeredIds)
        allEnabledMask |= (1u << id);

    // valid: enable/disable a couple of screens, tweak secs, still >=1 enabled
    {
        UiScreenPatchItem items[] = {
            {1, true, false, true, 15},
            {4, true, true, false, 0},
        };
        int badId = -1;
        UiScreenPatchError e = uiValidateScreenPatch(items, 2, registeredIds, registeredCount, allEnabledMask, 3, 600, &badId);
        check(e == UI_SCREEN_PATCH_OK, "valid partial patch (disable #1, keep #4, tweak secs) passes");
    }

    // unknown id
    {
        UiScreenPatchItem items[] = {{3, true, true, false, 0}}; // 3 is not registered
        int badId = -1;
        UiScreenPatchError e = uiValidateScreenPatch(items, 1, registeredIds, registeredCount, allEnabledMask, 3, 600, &badId);
        check(e == UI_SCREEN_PATCH_UNKNOWN_ID, "unknown screen id is rejected");
        check(badId == 3, "outBadId reports the offending id (3)");
    }

    // secs below the minimum
    {
        UiScreenPatchItem items[] = {{1, false, false, true, 2}};
        int badId = -1;
        UiScreenPatchError e = uiValidateScreenPatch(items, 1, registeredIds, registeredCount, allEnabledMask, 3, 600, &badId);
        check(e == UI_SCREEN_PATCH_SECS_RANGE, "secs below minSecs (2 < 3) is rejected");
        check(badId == 1, "outBadId reports the offending id (1)");
    }

    // secs above the maximum
    {
        UiScreenPatchItem items[] = {{1, false, false, true, 601}};
        UiScreenPatchError e = uiValidateScreenPatch(items, 1, registeredIds, registeredCount, allEnabledMask, 3, 600, nullptr);
        check(e == UI_SCREEN_PATCH_SECS_RANGE, "secs above maxSecs (601 > 600) is rejected");
    }

    // disabling every registered screen is rejected
    {
        UiScreenPatchItem items[registeredCount];
        int n = 0;
        for (int id : registeredIds) {
            items[n].id = id;
            items[n].hasEnabled = true;
            items[n].enabled = false;
            items[n].hasSecs = false;
            items[n].secs = 0;
            n++;
        }
        UiScreenPatchError e = uiValidateScreenPatch(items, n, registeredIds, registeredCount, allEnabledMask, 3, 600, nullptr);
        check(e == UI_SCREEN_PATCH_NONE_ENABLED, "disabling every registered screen is rejected");
    }

    // disabling all but leaving one untouched (still enabled from currentMask) is fine
    {
        UiScreenPatchItem items[registeredCount];
        int n = 0;
        for (int id : registeredIds) {
            if (id == 20)
                continue; // leave screen 20 untouched -> stays enabled from currentMask
            items[n].id = id;
            items[n].hasEnabled = true;
            items[n].enabled = false;
            items[n].hasSecs = false;
            items[n].secs = 0;
            n++;
        }
        UiScreenPatchError e = uiValidateScreenPatch(items, n, registeredIds, registeredCount, allEnabledMask, 3, 600, nullptr);
        check(e == UI_SCREEN_PATCH_OK, "leaving one registered screen untouched keeps the patch valid");
    }

    // empty patch (e.g. only "defaultSecs" sent) is always valid as long as
    // something is already enabled
    {
        UiScreenPatchError e = uiValidateScreenPatch(nullptr, 0, registeredIds, registeredCount, allEnabledMask, 3, 600, nullptr);
        check(e == UI_SCREEN_PATCH_OK, "an empty screens[] patch is valid");
    }

    // empty patch against an already-all-disabled mask is rejected (can't
    // "do nothing" your way into the forbidden all-disabled state either)
    {
        UiScreenPatchError e = uiValidateScreenPatch(nullptr, 0, registeredIds, registeredCount, 0u, 3, 600, nullptr);
        check(e == UI_SCREEN_PATCH_NONE_ENABLED, "empty patch against an all-disabled mask is still rejected");
    }
}

// ---------------------------------------------------------------------------
// uiValidateCurrencyBytes / uiValidatePricePerKwh - PATCH
// /api/system/screens's "powerBill" object
// ---------------------------------------------------------------------------

static void test_validate_powerbill()
{
    printf("uiValidateCurrencyBytes / uiValidatePricePerKwh\n");

    check(uiValidateCurrencyBytes("R$", 4), "\"R$\" (2 bytes) is valid");
    check(uiValidateCurrencyBytes("$", 4), "\"$\" (1 byte) is valid");
    check(uiValidateCurrencyBytes("\xc2\xa3", 4), "GBP sign (2-byte UTF-8) is valid");
    check(uiValidateCurrencyBytes("\xe2\x82\xac", 4), "EUR sign (3-byte UTF-8) is valid");
    check(!uiValidateCurrencyBytes("", 4), "empty currency is rejected");
    check(!uiValidateCurrencyBytes("TOOLONG", 4), "currency over 4 bytes is rejected");
    check(!uiValidateCurrencyBytes(" R$", 4), "leading space is rejected");
    check(!uiValidateCurrencyBytes("R$ ", 4), "trailing space is rejected");
    check(!uiValidateCurrencyBytes(nullptr, 4), "null currency is rejected");

    check(uiValidatePricePerKwh(0.95f, 0.0f, 10.0f), "0.95 is within [0,10]");
    check(uiValidatePricePerKwh(0.0f, 0.0f, 10.0f), "0.0 (minPrice) is inclusive");
    check(uiValidatePricePerKwh(10.0f, 0.0f, 10.0f), "10.0 (maxPrice) is inclusive");
    check(!uiValidatePricePerKwh(-0.01f, 0.0f, 10.0f), "negative price is rejected");
    check(!uiValidatePricePerKwh(10.01f, 0.0f, 10.0f), "price above maxPrice is rejected");
    check(!uiValidatePricePerKwh(NAN, 0.0f, 10.0f), "NAN price is rejected");
    check(!uiValidatePricePerKwh(INFINITY, 0.0f, 10.0f), "Inf price is rejected");
}

int main()
{
    test_interp();
    test_ema();
    test_energy();
    test_midnight();
    test_odds();
    test_ring_float();
    test_ring_shares();
    test_ring_diary();
    test_hour_buckets();
    test_resolve_screen_secs();
    test_validate_screen_patch();
    test_validate_powerbill();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
