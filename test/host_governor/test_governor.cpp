// Host tests for the ThermalGovernor control law.
//
// Builds with a plain g++ (no ESP-IDF headers) inside the espressif/idf
// container:
//
//   g++ -std=c++17 -Wall -Wextra -O1 -I main
//       test/host_governor/test_governor.cpp main/thermal_governor.cpp
//       -o /tmp/host_governor && /tmp/host_governor
//
// The plant is deliberately simple: P proportional to f * V^2 and a first
// order thermal response with tau = 100 s (the time constant measured on the
// real board). It is calibrated against the two ends of
// the measured sweep (500 MHz / 1080 mV and 800 MHz / 1250 mV) so the numbers
// are in the right ballpark; it is a regression harness, not a digital twin.

#include "thermal_governor.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace gov;

static int g_failures = 0;
static int g_checks = 0;

static void check(bool ok, const char *what)
{
    g_checks++;
    if (!ok) {
        g_failures++;
        printf("  FAIL: %s\n", what);
    }
}

static void checkf(bool ok, const char *fmt, double a, double b)
{
    g_checks++;
    if (!ok) {
        g_failures++;
        printf("  FAIL: ");
        printf(fmt, a, b);
        printf("\n");
    }
}

// ---------------------------------------------------------------------------
// Plant
// ---------------------------------------------------------------------------

struct Plant {
    // measured calibration
    static constexpr float kP = 0.1072f;     // W per (MHz * V^2)
    static constexpr float kRthVr = 0.448f;  // C per W  (VR sensor)
    static constexpr float kRthVri = 0.530f; // C per W  (TPS internal)
    static constexpr float kTau = 100.0f;    // s
    static constexpr float kEff = 0.875f;    // Pout / Pin

    float ambient = 20.0f;
    float tVr = 20.0f;
    float tVri = 20.0f;
    float freq = 500.0f;
    float volts = 1.1f;

    float pin() const
    {
        return kP * freq * volts * volts;
    }
    float vin() const
    {
        return 12.605f - 0.006f * pin();
    }
    float iin() const
    {
        return pin() / vin();
    }
    float iout() const
    {
        return pin() * kEff / volts;
    }
    float hashrate() const
    {
        return freq * 2040.0f * 4.0f / 1000.0f;
    }

    void step(float dt)
    {
        float p = pin();
        float tVrSs = ambient + kRthVr * p;
        float tVriSs = ambient + kRthVri * p;
        float a = 1.0f - expf(-dt / kTau);
        tVr += a * (tVrSs - tVr);
        tVri += a * (tVriSs - tVri);
    }

    void apply(const GovDecision &d)
    {
        freq = d.freq;
        volts = (float) d.mv / 1000.0f;
    }
};

static GovInputs sample(const Plant &p, float now)
{
    GovInputs in;
    in.vrTemp.set(p.tVr);
    in.vrTempInt.set(p.tVri);
    in.pin.set(p.pin());
    in.iin.set(p.iin());
    in.vin.set(p.vin());
    in.iout.set(p.iout());
    in.voutMv.set(p.volts * 1000.0f);
    in.hashrate.set(p.hashrate());
    in.appliedFreq.set(p.freq);
    in.appliedMv.set(p.volts * 1000.0f);
    in.frozen = false;
    in.nowS = now;
    return in;
}

// Runs the closed loop for durationS seconds, returns the last decision.
// maxVr/maxVri accumulate the worst temperatures seen.
static GovDecision run(ThermalGovernor &g, Plant &p, float &now, float durationS, float *maxVr = nullptr,
                       float *maxVri = nullptr)
{
    GovDecision d;
    const float dt = 2.0f;
    int n = (int) (durationS / dt);
    for (int i = 0; i < n; i++) {
        GovInputs in = sample(p, now);
        d = g.update(in);
        p.apply(d);
        p.step(dt);
        now += dt;
        if (maxVr && p.tVr > *maxVr) {
            *maxVr = p.tVr;
        }
        if (maxVri && p.tVri > *maxVri) {
            *maxVri = p.tVri;
        }
    }
    return d;
}

static GovConfig baseConfig()
{
    GovConfig c;
    c.setDefaults();
    c.freqCap = 800.0f;
    c.voltageCap = 1250;
    return c;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_curve_and_grid()
{
    printf("test_curve_and_grid\n");
    float f[GOV_CURVE_MAX_POINTS], mv[GOV_CURVE_MAX_POINTS];
    int n = 0;
    check(parseCurve("500:1100,725:1210,800:1250", f, mv, GOV_CURVE_MAX_POINTS, &n), "curve parses");
    check(n == 3, "curve has 3 points");
    check(fabsf(curveMv(f, mv, n, 500.0f) - 1100.0f) < 0.01f, "curve at 500 == 1100");
    check(fabsf(curveMv(f, mv, n, 400.0f) - 1100.0f) < 0.01f, "curve clamps below");
    check(fabsf(curveMv(f, mv, n, 900.0f) - 1250.0f) < 0.01f, "curve clamps above");
    check(fabsf(curveMv(f, mv, n, 762.5f) - 1230.0f) < 0.5f, "curve interpolates mid segment");
    check(!parseCurve("500:1100", f, mv, GOV_CURVE_MAX_POINTS, &n), "single point rejected");
    check(!parseCurve("garbage", f, mv, GOV_CURVE_MAX_POINTS, &n), "garbage rejected");
    check(roundUp5mv(1201.0f) == 1205, "roundUp5mv rounds up");
    check(roundUp5mv(1200.0f) == 1200, "roundUp5mv keeps exact multiples");

    ThermalGovernor g;
    check(g.configure(baseConfig()), "base config accepted");
    GovConfig bad = baseConfig();
    bad.voltageCap = 1000; // below vmin
    check(!g.configure(bad), "voltageCap below vmin rejected");
    bad = baseConfig();
    bad.sensVr = 0.0f;
    check(!g.configure(bad), "zero sensitivity rejected");
    bad = baseConfig();
    snprintf(bad.curve, sizeof(bad.curve), "nope");
    check(!g.configure(bad), "bad curve rejected");
}

static void test_convergence()
{
    printf("test_convergence\n");
    ThermalGovernor g;
    GovConfig c = baseConfig();
    g.configure(c);

    Plant p;
    p.ambient = 20.0f;
    p.tVr = 20.0f;
    p.tVri = 20.0f;
    float now = 0.0f;
    float maxVr = 0.0f, maxVri = 0.0f;

    // 60 s startup hold: must stay at fmin
    GovDecision d = run(g, p, now, 50.0f, &maxVr, &maxVri);
    checkf(fabsf(d.freq - c.fmin) < 0.01f, "startup stays at fmin: got %.2f want %.2f", d.freq, c.fmin);
    check(d.state == STATE_STARTUP, "startup state");

    // 6 h of closed loop
    d = run(g, p, now, 6.0f * 3600.0f, &maxVr, &maxVri);

    printf("  converged: f=%.2f MHz  v=%d mV  vr=%.2f C  vri=%.2f C  pin=%.1f W  iout=%.1f A  state=%s limiter=%s\n",
           (double) d.freq, d.mv, (double) p.tVr, (double) p.tVri, (double) p.pin(), (double) p.iout(),
           stateToStr(d.state), limiterToStr(d.limiter));
    printf("  peaks: vr=%.2f C vri=%.2f C   counters ups=%u downs=%u emergencies=%u\n", (double) maxVr, (double) maxVri,
           g.countUps(), g.countDowns(), g.countEmergencies());

    checkf(maxVr <= c.vrTarget + c.vrHardDelta, "vrTemp never exceeds the hard ceiling: peak %.2f limit %.2f", maxVr,
           c.vrTarget + c.vrHardDelta);
    checkf(maxVri <= c.vriTarget + c.vriHardDelta, "vrTempInt never exceeds the hard ceiling: peak %.2f limit %.2f", maxVri,
           c.vriTarget + c.vriHardDelta);
    checkf(d.freq > c.fmin + 100.0f, "governor ramped well above fmin: got %.2f want > %.2f", d.freq, c.fmin + 100.0f);
    checkf(d.freq <= c.freqCap, "governor stays under the cap: got %.2f cap %.2f", d.freq, c.freqCap);
    check(g.countEmergencies() == 0, "no emergency during a quiet ramp");
}

static void test_ambient_step()
{
    printf("test_ambient_step\n");
    ThermalGovernor g;
    GovConfig c = baseConfig();
    g.configure(c);

    Plant p;
    float now = 0.0f;
    run(g, p, now, 6.0f * 3600.0f);
    GovDecision settled = run(g, p, now, 600.0f);
    float fWarm = settled.freq;
    printf("  settled at %.2f MHz (vr %.2f C)\n", (double) fWarm, (double) p.tVr);

    // +8 C ambient step
    p.ambient += 8.0f;
    float maxVr = 0.0f, maxVri = 0.0f;
    GovDecision hot = run(g, p, now, 2.0f * 3600.0f, &maxVr, &maxVri);
    printf("  after +8C: f=%.2f MHz vr=%.2f C (peak %.2f) state=%s limiter=%s\n", (double) hot.freq, (double) p.tVr,
           (double) maxVr, stateToStr(hot.state), limiterToStr(hot.limiter));
    checkf(hot.freq < fWarm - 1.0f, "backed off after +8C: %.2f -> %.2f", fWarm, hot.freq);
    checkf(p.tVr < c.vrTarget + c.vrHardDelta, "vrTemp back under the hard ceiling: %.2f limit %.2f", p.tVr,
           c.vrTarget + c.vrHardDelta);
    float fHot = hot.freq;

    // -8 C: back to the original ambient, must climb again
    p.ambient -= 8.0f;
    GovDecision back = run(g, p, now, 4.0f * 3600.0f);
    printf("  after -8C: f=%.2f MHz vr=%.2f C state=%s\n", (double) back.freq, (double) p.tVr, stateToStr(back.state));
    checkf(back.freq > fHot + 1.0f, "recovered after -8C: %.2f -> %.2f", fHot, back.freq);
}

static void test_sensor_failure()
{
    printf("test_sensor_failure\n");
    ThermalGovernor g;
    GovConfig c = baseConfig();
    g.configure(c);

    Plant p;
    float now = 0.0f;
    GovDecision d = run(g, p, now, 3.0f * 3600.0f);
    checkf(d.freq > c.fmin, "ramped before the sensors die: got %.2f fmin %.2f", d.freq, c.fmin);

    // both thermal sensors return nothing for 60 s
    for (int i = 0; i < 30; i++) {
        GovInputs in = sample(p, now);
        in.vrTemp.clear();
        in.vrTempInt.clear();
        d = g.update(in);
        p.apply(d);
        p.step(2.0f);
        now += 2.0f;
    }
    printf("  both sensors invalid -> f=%.2f state=%s reason=\"%s\"\n", (double) d.freq, stateToStr(d.state), d.reason);
    check(d.state == STATE_SENSOR_FAIL, "SENSOR_FAIL state");
    checkf(fabsf(d.freq - c.fmin) < 0.01f, "drops to fmin: got %.2f want %.2f", d.freq, c.fmin);

    // one sensor comes back: must leave SENSOR_FAIL
    for (int i = 0; i < 10; i++) {
        GovInputs in = sample(p, now);
        in.vrTempInt.clear();
        d = g.update(in);
        p.apply(d);
        p.step(2.0f);
        now += 2.0f;
    }
    check(d.state != STATE_SENSOR_FAIL, "single working sensor is enough");
}

static void test_garbage_sample()
{
    printf("test_garbage_sample\n");
    ThermalGovernor g;
    GovConfig c = baseConfig();
    g.configure(c);

    Plant p;
    float now = 0.0f;
    GovDecision before = run(g, p, now, 3.0f * 3600.0f);
    uint32_t downsBefore = g.countDowns();
    uint32_t emgBefore = g.countEmergencies();

    // one implausible reading on each channel, isolated
    GovInputs in = sample(p, now);
    in.vrTemp.set(240.0f);   // above tempHi
    in.vrTempInt.set(-40.0f); // below tempLo
    in.iout.set(999.0f);
    in.vin.set(0.0f);
    GovDecision d = g.update(in);
    p.apply(d);
    p.step(2.0f);
    now += 2.0f;

    printf("  garbage sample -> f=%.2f (was %.2f) action=%s state=%s\n", (double) d.freq, (double) before.freq,
           actionToStr(d.action), stateToStr(d.state));
    checkf(fabsf(d.freq - before.freq) < 0.01f, "isolated garbage does not move the actuator: %.2f -> %.2f", before.freq,
           d.freq);
    check(d.action == ACT_HOLD, "isolated garbage produces no action");
    check(g.countDowns() == downsBefore, "isolated garbage produces no DOWN");
    check(g.countEmergencies() == emgBefore, "isolated garbage produces no EMERGENCY");

    // one clean sample clears the reject run left by the garbage sample
    {
        GovInputs clean = sample(p, now);
        d = g.update(clean);
        p.apply(d);
        p.step(2.0f);
        now += 2.0f;
    }

    // a plausible but jumpy reading is gated too, until the re-sync threshold
    for (int i = 0; i < 2; i++) {
        GovInputs j = sample(p, now);
        j.vrTemp.set(p.tVr + 40.0f); // > tempMaxDelta but inside [tempLo, tempHi]
        d = g.update(j);
        p.apply(d);
        p.step(2.0f);
        now += 2.0f;
    }
    check(g.countEmergencies() == emgBefore, "2 jumpy samples still gated (resyncAfter = 3)");

    // three in a row: the channel re-syncs instead of locking out forever
    for (int i = 0; i < 6; i++) {
        GovInputs j = sample(p, now);
        j.vrTemp.set(p.tVr + 40.0f);
        d = g.update(j);
        p.apply(d);
        p.step(2.0f);
        now += 2.0f;
    }
    check(g.countEmergencies() > emgBefore, "a sustained hot reading eventually fires (channel re-synced)");
}

static void test_frozen()
{
    printf("test_frozen\n");
    ThermalGovernor g;
    GovConfig c = baseConfig();
    g.configure(c);

    Plant p;
    float now = 0.0f;
    GovDecision before = run(g, p, now, 3.0f * 3600.0f);

    // frozen by the caller: hot as it gets, nothing moves
    GovDecision d = before;
    for (int i = 0; i < 300; i++) {
        GovInputs in = sample(p, now);
        in.vrTemp.set(95.0f);
        in.vrTempInt.set(99.0f);
        in.frozen = true;
        d = g.update(in);
        now += 2.0f;
    }
    printf("  frozen -> f=%.2f (was %.2f) state=%s action=%s\n", (double) d.freq, (double) before.freq,
           stateToStr(d.state), actionToStr(d.action));
    check(d.state == STATE_FROZEN, "FROZEN state");
    check(d.action == ACT_HOLD, "frozen never acts");
    checkf(fabsf(d.freq - before.freq) < 0.01f, "frozen keeps the actuator: %.2f -> %.2f", before.freq, d.freq);

    // overheat latch: vset > 0 but vout ~ 0
    GovInputs in = sample(p, now);
    in.voutMv.set(0.0f);
    in.appliedMv.set(1250.0f);
    d = g.update(in);
    check(d.state == STATE_FROZEN, "overheat latch (vout ~ 0) freezes");
    check(d.action == ACT_HOLD, "overheat latch never acts");
}

static void test_saturated_low()
{
    printf("test_saturated_low\n");
    ThermalGovernor g;
    GovConfig c = baseConfig();
    g.configure(c);

    Plant p;
    float now = 0.0f;
    GovDecision d;
    // ramp up first, so the governor really has to walk all the way down
    d = run(g, p, now, 3.0f * 3600.0f);
    checkf(d.freq > c.fmin + 100.0f, "ramped before the heat wave: got %.2f", d.freq, 0.0);

    // permanently over the hard ceiling: the governor drops to fmin and then
    // reports SATURATED_LOW instead of re-firing EMERGENCY_DOWN forever
    for (int i = 0; i < 900; i++) { // 30 min
        GovInputs in = sample(p, now);
        in.vrTemp.set(95.0f);
        in.vrTempInt.set(99.0f);
        d = g.update(in);
        p.apply(d);
        p.step(2.0f);
        now += 2.0f;
    }
    printf("  f=%.2f state=%s emergencies=%u reason=\"%s\"\n", (double) d.freq, stateToStr(d.state), g.countEmergencies(),
           d.reason);
    checkf(fabsf(d.freq - c.fmin) < 0.01f, "sits at fmin: got %.2f want %.2f", d.freq, c.fmin);
    check(d.state == STATE_SATURATED_LOW, "SATURATED_LOW instead of spinning EMERGENCY");
    // fmin=500, cap=800 -> 48 steps; at 8 steps per emergency and 6 s apart the
    // descent takes a handful of actions, not hundreds.
    check(g.countEmergencies() < 20, "emergency actions are bounded while saturated");
}

static void test_emergency_rate_limit()
{
    printf("test_emergency_rate_limit\n");
    ThermalGovernor g;
    GovConfig c = baseConfig();
    c.startupHoldS = 0.0f;
    g.configure(c);

    Plant p;
    float now = 0.0f;
    // prime the filters at a sane temperature so fast() is populated
    for (int i = 0; i < 5; i++) {
        GovInputs in = sample(p, now);
        g.update(in);
        now += 2.0f;
    }
    // force the target up so there is room to fall
    run(g, p, now, 3.0f * 3600.0f);

    float lastEmergency = -1000.0f;
    float minGap = 1e9f;
    for (int i = 0; i < 60; i++) {
        GovInputs in = sample(p, now);
        in.vrTemp.set(90.0f);
        in.vrTempInt.set(95.0f);
        GovDecision d = g.update(in);
        if (d.action == ACT_EMERGENCY_DOWN) {
            float gap = now - lastEmergency;
            if (gap < minGap) {
                minGap = gap;
            }
            lastEmergency = now;
        }
        p.apply(d);
        p.step(2.0f);
        now += 2.0f;
    }
    printf("  smallest gap between emergency actions: %.1f s (limit %.1f s)\n", (double) minGap, (double) c.emgPeriodS);
    checkf(minGap >= c.emgPeriodS, "emergencies are at least emgPeriodS apart: %.1f >= %.1f", minGap, c.emgPeriodS);
}

static void test_vsag_grace()
{
    printf("test_vsag_grace\n");
    ThermalGovernor g;
    GovConfig c = baseConfig();
    c.startupHoldS = 0.0f;
    g.configure(c);

    Plant p;
    float now = 0.0f;

    // The grace window is anchored on the last voltage command, so run until
    // the governor actually moves the actuator before injecting the sag.
    GovDecision d;
    bool acted = false;
    for (int i = 0; i < 20000 && !acted; i++) {
        GovInputs in = sample(p, now);
        d = g.update(in);
        acted = (d.action == ACT_UP);
        p.apply(d);
        p.step(2.0f);
        now += 2.0f;
    }
    check(acted, "governor issued a voltage command to anchor the grace window");
    uint32_t emgBefore = g.countEmergencies();

    // samples at +2 s, +4 s and +6 s are all within vsagGraceS = 6 s
    for (int i = 0; i < 3; i++) {
        GovInputs in = sample(p, now);
        in.voutMv.set(in.appliedMv.value - 100.0f);
        d = g.update(in);
        p.apply(d);
        p.step(2.0f);
        now += 2.0f;
    }
    check(g.countEmergencies() == emgBefore, "sag inside the grace window is ignored");

    // past the grace window, two consecutive sags do fire
    for (int i = 0; i < 8; i++) {
        GovInputs in = sample(p, now);
        in.voutMv.set(in.appliedMv.value - 100.0f);
        d = g.update(in);
        p.apply(d);
        p.step(2.0f);
        now += 2.0f;
    }
    printf("  emergencies %u -> %u, last reason \"%s\"\n", emgBefore, g.countEmergencies(), d.reason);
    check(g.countEmergencies() > emgBefore, "sustained sag past the grace window fires");
}

// Regression: enabling the governor on a running miner dropped it to fmin,
// because the first update() overwrote the seeded target (seen on hardware:
// 750 -> 500 MHz and a 9 min re-ramp).
static void test_seed_target()
{
    printf("test_seed_target\n");
    ThermalGovernor g;
    GovConfig c = baseConfig();
    g.configure(c);
    g.seedTarget(750.0f);

    Plant p;
    float now = 0.0f;
    GovInputs in = sample(p, now);
    GovDecision d = g.update(in);
    printf("  first decision after seedTarget(750): f=%.2f state=%s\n", (double) d.freq, stateToStr(d.state));
    checkf(fabsf(d.freq - 750.0f) < 0.01f, "seeded target survives the first update: %.2f (expected %.2f)", d.freq, 750.0);
    check(d.state == STATE_STARTUP, "seeded start still takes the startup hold");

    // a cold start is unchanged
    ThermalGovernor cold;
    cold.configure(c);
    GovDecision dc = cold.update(in);
    checkf(fabsf(dc.freq - c.fmin) < 0.01f, "cold start begins at fmin: %.2f (expected %.2f)", dc.freq, c.fmin);

    // reset() drops the seed
    g.reset();
    GovDecision dr = g.update(in);
    checkf(fabsf(dr.freq - c.fmin) < 0.01f, "reset() returns to the fmin start: %.2f (expected %.2f)", dr.freq, c.fmin);
}

static void test_disabled_limits()
{
    printf("test_disabled_limits\n");
    ThermalGovernor g;
    GovConfig c = baseConfig();
    c.pinMax = 0.0f;  // disabled
    c.iinMax = 0.0f;  // disabled
    g.configure(c);

    Plant p;
    float now = 0.0f;
    GovDecision d = run(g, p, now, 4.0f * 3600.0f);
    check(!d.margins[LIM_PIN].valid, "pin margin absent when pinMax == 0");
    check(!d.margins[LIM_IIN].valid, "iin margin absent when iinMax == 0");
    checkf(d.freq > c.fmin + 100.0f, "still ramps with pin/iin disabled: got %.2f", d.freq, 0.0);

    // now enable pin at a low limit: it must become the limiter
    ThermalGovernor g2;
    GovConfig c2 = baseConfig();
    c2.pinMax = 100.0f;
    g2.configure(c2);
    Plant p2;
    float now2 = 0.0f;
    GovDecision d2 = run(g2, p2, now2, 6.0f * 3600.0f);
    printf("  pinMax=100W -> f=%.2f pin=%.1f limiter=%s\n", (double) d2.freq, (double) p2.pin(), limiterToStr(d2.limiter));
    checkf(p2.pin() < 110.0f, "pin constraint holds the loop down: %.1f W (limit %.1f W)", p2.pin(), 100.0);
    checkf(d2.freq < d.freq, "pin limit yields a lower frequency than the thermal-only run: %.2f < %.2f", d2.freq, d.freq);
}

int main()
{
    printf("=== ThermalGovernor host tests ===\n");
    test_curve_and_grid();
    test_convergence();
    test_ambient_step();
    test_sensor_failure();
    test_garbage_sample();
    test_frozen();
    test_saturated_low();
    test_emergency_rate_limit();
    test_vsag_grace();
    test_seed_target();
    test_disabled_limits();
    printf("=== %d checks, %d failures ===\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
