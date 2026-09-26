#include "thermal_governor.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace gov {

// ---------------------------------------------------------------------------
// Name tables
// ---------------------------------------------------------------------------

const char *stateToStr(uint8_t s)
{
    switch (s) {
    case STATE_STARTUP: return "STARTUP";
    case STATE_RAMP_UP: return "RAMP_UP";
    case STATE_HOLD: return "HOLD";
    case STATE_BACKOFF: return "BACKOFF";
    case STATE_EMERGENCY: return "EMERGENCY";
    case STATE_SATURATED_LOW: return "SATURATED_LOW";
    case STATE_SENSOR_FAIL: return "SENSOR_FAIL";
    case STATE_FROZEN: return "FROZEN";
    default: return "?";
    }
}

const char *actionToStr(uint8_t a)
{
    switch (a) {
    case ACT_HOLD: return "HOLD";
    case ACT_UP: return "UP";
    case ACT_DOWN: return "DOWN";
    case ACT_EMERGENCY_DOWN: return "EMERGENCY_DOWN";
    case ACT_VTRIM: return "VTRIM";
    default: return "?";
    }
}

const char *limiterToStr(uint8_t l)
{
    switch (l) {
    case LIM_VR: return "vr_temp";
    case LIM_VRI: return "vr_temp_int";
    case LIM_PIN: return "pin";
    case LIM_IIN: return "iin";
    case LIM_IOUT: return "iout";
    case LIM_VIN: return "vin";
    default: return "none";
    }
}

// ---------------------------------------------------------------------------
// Voltage curve
// ---------------------------------------------------------------------------

bool parseCurve(const char *spec, float *freqs, float *mvs, int maxPoints, int *countOut)
{
    int n = 0;
    const char *p = spec;

    if (!spec) {
        return false;
    }

    while (*p && n < maxPoints) {
        // skip separators / whitespace
        while (*p == ',' || *p == ' ' || *p == '\t') {
            p++;
        }
        if (!*p) {
            break;
        }
        char *end = NULL;
        float f = strtof(p, &end);
        if (end == p) {
            return false;
        }
        p = end;
        while (*p == ' ') {
            p++;
        }
        if (*p != ':') {
            return false;
        }
        p++;
        float mv = strtof(p, &end);
        if (end == p) {
            return false;
        }
        p = end;
        freqs[n] = f;
        mvs[n] = mv;
        n++;
    }

    if (n < 2) {
        return false;
    }

    // insertion sort by frequency (n <= 8)
    for (int i = 1; i < n; i++) {
        float kf = freqs[i];
        float km = mvs[i];
        int j = i;
        while (j > 0 && freqs[j - 1] > kf) {
            freqs[j] = freqs[j - 1];
            mvs[j] = mvs[j - 1];
            j--;
        }
        freqs[j] = kf;
        mvs[j] = km;
    }

    *countOut = n;
    return true;
}

// Piecewise-linear interpolation, clamped outside the outer breakpoints.
float curveMv(const float *freqs, const float *mvs, int count, float freq)
{
    if (count <= 0) {
        return 0.0f;
    }
    if (freq <= freqs[0]) {
        return mvs[0];
    }
    if (freq >= freqs[count - 1]) {
        return mvs[count - 1];
    }
    for (int i = 1; i < count; i++) {
        float f1 = freqs[i];
        if (freq <= f1) {
            float f0 = freqs[i - 1];
            float v0 = mvs[i - 1];
            float v1 = mvs[i];
            if (f1 == f0) {
                return v1;
            }
            return v0 + (v1 - v0) * (freq - f0) / (f1 - f0);
        }
    }
    return mvs[count - 1];
}

// The PMIC takes 5 mV granularity; always round up so we never undervolt.
int roundUp5mv(float mv)
{
    return (int) (ceilf(mv / 5.0f - 1e-6f) * 5.0f);
}

// ---------------------------------------------------------------------------
// GovConfig
// ---------------------------------------------------------------------------

void GovConfig::setDefaults()
{
    fmin = 500.0f;
    freqCap = 800.0f;
    stepMhz = 6.25f;
    vmin = 1100;
    voltageCap = 1250;
    snprintf(curve, sizeof(curve), "500:1100,725:1210,800:1250");
    vTrimStep = 10;
    vTrimMax = 30;

    samplePeriodS = 2.0f;
    decisionPeriodS = 10.0f;
    startupHoldS = 60.0f;

    emaTauS = 30.0f;
    slopeWindowS = 60.0f;
    predictHorizonS = 120.0f; // ~1.2x the measured thermal constant (100 s)

    tempLo = 5.0f;
    tempHi = 120.0f;
    tempMaxDelta = 15.0f;
    ioutLo = 0.0f;
    ioutHi = 120.0f;
    vinLo = 9.0f;
    vinHi = 14.0f;
    pinLo = 0.0f;
    pinHi = 200.0f;
    iinLo = 0.0f;
    iinHi = 20.0f;
    resyncAfter = 3;

    vrTarget = 78.0f;
    vrHardDelta = 4.0f;  // hard ceiling 82 C
    vriTarget = 90.0f;
    vriHardDelta = 4.0f; // hard ceiling 94 C
    pinMax = 0.0f;       // disabled by default (the "column A" limiter config was picked)
    iinMax = 0.0f;       // disabled by default (resolution 0.17 A > 0.16 A/step)
    vinMin = 11.5f;
    ioutMax = 92.0f;     // firmware OC fault is at 95 A
    vsagMv = 50.0f;
    hrMin = 0.88f;

    // measured per 6.25 MHz step at the top of the range
    sensVr = 1.3f;
    sensVri = 1.5f;
    sensPin = 1.85f;
    sensIin = 0.16f;
    sensIout = 1.03f;
    sensVin = -0.012f;

    upMarginSteps = 1.5f;
    upDoubleMarginSteps = 6.0f;
    upFastPeriodS = 20.0f;
    upSlowPeriodS = 120.0f;
    upFastThermalC = 8.0f;
    downMaxSteps = 4;

    emgMinSteps = 4;
    emgMaxSteps = 8;
    emgIoutOver = 2.0f; // emergency at ioutMax + 2 (OC fault at 95 A)
    emgVinUnder = 0.2f;
    emgPinRatio = 1.10f;
    emgVsagSamples = 2;
    emgPeriodS = 6.0f;
    emgStateHoldS = 10.0f;
    vsagGraceS = 6.0f;

    thermalBackoffS = 300.0f;
    blockElectricalS = 600.0f;
    blockThermalS = 1800.0f;
    blockMaxS = 7200.0f;
    blockClearMargin = 5.0f;

    hrWindowS = 180.0f;
    hrStableS = 90.0f;
    hrActionGapS = 90.0f;
    hrDownSteps = 2;
    vtrimMarginSteps = 1.0f;
    smallCores = 2040;
    asicCount = 4;

    sensorSingleFailS = 10.0f;
    sensorBothFailS = 30.0f;
    overheatVoutMv = 100.0f;

    shadow = false;
}

float GovConfig::expectedHashrate(float freq) const
{
    return freq * (float) smallCores * (float) asicCount / 1000.0f;
}

// ---------------------------------------------------------------------------
// MedianWindow
// ---------------------------------------------------------------------------

void MedianWindow::init(int n)
{
    if (n < 1) {
        n = 1;
    }
    if (n > GOV_MEDIAN_MAX_N) {
        n = GOV_MEDIAN_MAX_N;
    }
    m_n = n;
    reset();
}

void MedianWindow::reset()
{
    m_count = 0;
    m_head = 0;
}

void MedianWindow::push(float v)
{
    m_buf[m_head] = v;
    m_head = (m_head + 1) % m_n;
    if (m_count < m_n) {
        m_count++;
    }
}

OptFloat MedianWindow::value() const
{
    if (m_count == 0) {
        return OptFloat();
    }
    float tmp[GOV_MEDIAN_MAX_N];
    // the ring holds the same *set* of samples Python's list would hold
    int start = (m_head - m_count + m_n * 2) % m_n;
    for (int i = 0; i < m_count; i++) {
        tmp[i] = m_buf[(start + i) % m_n];
    }
    for (int i = 1; i < m_count; i++) {
        float key = tmp[i];
        int j = i;
        while (j > 0 && tmp[j - 1] > key) {
            tmp[j] = tmp[j - 1];
            j--;
        }
        tmp[j] = key;
    }
    return OptFloat(tmp[m_count / 2]);
}

// ---------------------------------------------------------------------------
// Ema
// ---------------------------------------------------------------------------

void Ema::init(float tauS)
{
    m_tau = (tauS > 0.0f) ? tauS : 1.0f;
    m_v = 0.0f;
    m_t = 0.0f;
    m_hasV = false;
    m_hasT = false;
}

float Ema::push(float v, float nowS)
{
    if (!m_hasV || !m_hasT || nowS <= m_t) {
        m_v = v;
    } else {
        float alpha = 1.0f - expf(-(nowS - m_t) / m_tau);
        m_v += alpha * (v - m_v);
    }
    m_hasV = true;
    m_hasT = true;
    m_t = nowS;
    return m_v;
}

OptFloat Ema::value() const
{
    return m_hasV ? OptFloat(m_v) : OptFloat();
}

// ---------------------------------------------------------------------------
// SlopeWindow
// ---------------------------------------------------------------------------

void SlopeWindow::init(float windowS)
{
    m_window = (windowS > 0.0f) ? windowS : 1.0f;
    m_head = 0;
    m_count = 0;
}

float SlopeWindow::at(int i, const float *buf) const
{
    int start = (m_head - m_count + GOV_SLOPE_MAX_N * 2) % GOV_SLOPE_MAX_N;
    return buf[(start + i) % GOV_SLOPE_MAX_N];
}

void SlopeWindow::push(float v, float nowS)
{
    m_t[m_head] = nowS;
    m_v[m_head] = v;
    m_head = (m_head + 1) % GOV_SLOPE_MAX_N;
    if (m_count < GOV_SLOPE_MAX_N) {
        m_count++;
    }
    // drop everything older than the window, but always keep >3 samples
    float cutoff = nowS - m_window;
    while (m_count > 3 && at(0, m_t) < cutoff) {
        m_count--;
    }
}

float SlopeWindow::value() const
{
    int n = m_count;
    if (n < 3) {
        return 0.0f;
    }
    float t0 = at(0, m_t);
    float span = at(n - 1, m_t) - t0;
    // Not enough history yet: assume flat rather than extrapolate noise.
    if (span < m_window * 0.5f) {
        return 0.0f;
    }
    float st = 0.0f, sv = 0.0f, stt = 0.0f, stv = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = at(i, m_t) - t0;
        float v = at(i, m_v);
        st += t;
        sv += v;
        stt += t * t;
        stv += t * v;
    }
    float den = (float) n * stt - st * st;
    if (fabsf(den) < 1e-9f) {
        return 0.0f;
    }
    return ((float) n * stv - st * sv) / den;
}

// ---------------------------------------------------------------------------
// SignalChannel
// ---------------------------------------------------------------------------

void SignalChannel::init(float lo, float hi, int medianN, int fastN, bool useMaxDelta, float maxDelta, bool useEma,
                         float emaTauS, bool useSlope, float slopeWindowS, int resyncAfter)
{
    m_lo = lo;
    m_hi = hi;
    m_useMaxDelta = useMaxDelta;
    m_maxDelta = maxDelta;
    m_resyncAfter = (resyncAfter >= 1) ? resyncAfter : 1;
    m_med.init(medianN);
    m_fast.init(fastN);
    m_useEma = useEma;
    m_useSlope = useSlope;
    m_ema.init(emaTauS);
    m_slope.init(slopeWindowS);
    m_lastAccepted = 0.0f;
    m_hasLastAccepted = false;
    m_rejectRun = 0;
    m_lastOkS = 0.0f;
    m_hasLastOk = false;
    m_firstSeenS = 0.0f;
    m_hasFirstSeen = false;
}

bool SignalChannel::push(const OptFloat &raw, float nowS)
{
    if (!m_hasFirstSeen) {
        m_firstSeenS = nowS;
        m_hasFirstSeen = true;
    }

    if (!raw.valid || !isfinite(raw.value) || raw.value < m_lo || raw.value > m_hi) {
        m_rejectRun++;
        return false;
    }

    if (m_useMaxDelta && m_hasLastAccepted && fabsf(raw.value - m_lastAccepted) > m_maxDelta) {
        m_rejectRun++;
        // A long run of "impossible" jumps means our reference is stale;
        // re-sync instead of locking the channel out forever.
        if (m_rejectRun < m_resyncAfter) {
            return false;
        }
        m_med.reset();
        m_fast.reset();
    }

    m_rejectRun = 0;
    m_lastAccepted = raw.value;
    m_hasLastAccepted = true;
    m_lastOkS = nowS;
    m_hasLastOk = true;
    m_med.push(raw.value);
    m_fast.push(raw.value);

    OptFloat med = m_med.value();
    if (med.valid) {
        // The slope is taken from the EMA, not from the median. The median
        // still carries ~0.1 C of residual noise; over a 120 s prediction
        // horizon that alone swings the predicted temperature by more than a
        // full frequency step and makes the loop hunt.
        float smoothed = m_useEma ? m_ema.push(med.value, nowS) : med.value;
        if (m_useSlope) {
            m_slope.push(smoothed, nowS);
        }
    }
    return true;
}

OptFloat SignalChannel::smooth() const
{
    if (m_useEma) {
        OptFloat v = m_ema.value();
        if (v.valid) {
            return v;
        }
    }
    return m_med.value();
}

float SignalChannel::slope() const
{
    return m_useSlope ? m_slope.value() : 0.0f;
}

float SignalChannel::staleFor(float nowS) const
{
    if (m_hasLastOk) {
        return nowS - m_lastOkS;
    }
    if (m_hasFirstSeen) {
        return nowS - m_firstSeenS;
    }
    return 0.0f;
}

bool SignalChannel::usable(float nowS, float graceS) const
{
    return m_med.value().valid && staleFor(nowS) <= graceS;
}

// ---------------------------------------------------------------------------
// ThermalGovernor
// ---------------------------------------------------------------------------

ThermalGovernor::ThermalGovernor()
{
    GovConfig defaults;
    defaults.setDefaults();

    m_cfg = defaults;
    m_curveCount = 0;
    m_countUps = 0;
    m_countDowns = 0;
    m_countEmergencies = 0;
    reset();
    // configure() copies from its argument, so never hand it m_cfg itself
    configure(defaults);
}

void ThermalGovernor::reset()
{
    m_fTarget = m_cfg.fmin;
    m_seeded = false;
    m_vTrim = 0;

    m_t0 = 0.0f;
    m_hasT0 = false;
    m_startupUntil = 0.0f;
    m_lastDecisionS = -1e9f;
    m_lastUpS = -1e9f;
    m_lastFreqChangeS = -1e9f;
    m_lastVoltCmdS = -1e9f;
    m_thermalBackoffUntil = -1e9f;
    m_lastEmergencyS = -1e9f;
    m_emergencyStateUntil = -1e9f;

    m_fBlocked.clear();
    m_blockUntil = 0.0f;
    m_blockCount = 0;
    m_blockForgetAfter = 0.0f;

    m_vsagRun = 0;
    m_hrRatioCached.clear();
    m_lastHrActionS = -1e9f;
    m_state = STATE_STARTUP;

    hrHistClear();
}

void ThermalGovernor::seedTarget(float freq)
{
    reset();
    if (freq < m_cfg.fmin) {
        freq = m_cfg.fmin;
    }
    if (freq > m_cfg.freqCap) {
        freq = m_cfg.freqCap;
    }
    m_fTarget = snap(freq);
    m_seeded = true; // survives the first update(), which would otherwise force fmin
}

bool ThermalGovernor::configure(const GovConfig &cfg)
{
    // The user ceilings must be able to win against the floors, otherwise
    // mvFor()'s max(vmin, ...) would silently override voltageCap.
    if (cfg.voltageCap < cfg.vmin) {
        return false;
    }
    if (cfg.freqCap < cfg.fmin) {
        return false;
    }
    if (cfg.stepMhz <= 0.0f) {
        return false;
    }
    if (cfg.sensVr == 0.0f || cfg.sensVri == 0.0f || cfg.sensPin == 0.0f || cfg.sensIin == 0.0f || cfg.sensIout == 0.0f ||
        cfg.sensVin == 0.0f) {
        return false;
    }

    float f[GOV_CURVE_MAX_POINTS];
    float mv[GOV_CURVE_MAX_POINTS];
    int n = 0;
    if (!parseCurve(cfg.curve, f, mv, GOV_CURVE_MAX_POINTS, &n)) {
        return false;
    }

    m_cfg = cfg;
    for (int i = 0; i < n; i++) {
        m_curveF[i] = f[i];
        m_curveMv[i] = mv[i];
    }
    m_curveCount = n;

    // Signal channels: the plausibility gates come from the config, so they
    // are rebuilt here. That costs the filter history, never an action.
    m_chVr.init(m_cfg.tempLo, m_cfg.tempHi, GOV_MEDIAN_MAX_N, 3, true, m_cfg.tempMaxDelta, true, m_cfg.emaTauS, true,
                m_cfg.slopeWindowS, m_cfg.resyncAfter);
    m_chVri.init(m_cfg.tempLo, m_cfg.tempHi, GOV_MEDIAN_MAX_N, 3, true, m_cfg.tempMaxDelta, true, m_cfg.emaTauS, true,
                 m_cfg.slopeWindowS, m_cfg.resyncAfter);
    m_chPin.init(m_cfg.pinLo, m_cfg.pinHi, GOV_MEDIAN_MAX_N, 3, false, 0.0f, false, 0.0f, false, 0.0f, m_cfg.resyncAfter);
    m_chIin.init(m_cfg.iinLo, m_cfg.iinHi, GOV_MEDIAN_MAX_N, 3, false, 0.0f, false, 0.0f, false, 0.0f, m_cfg.resyncAfter);
    m_chVin.init(m_cfg.vinLo, m_cfg.vinHi, GOV_MEDIAN_MAX_N, 3, false, 0.0f, false, 0.0f, false, 0.0f, m_cfg.resyncAfter);
    m_chIout.init(m_cfg.ioutLo, m_cfg.ioutHi, GOV_MEDIAN_MAX_N, 3, false, 0.0f, false, 0.0f, false, 0.0f, m_cfg.resyncAfter);

    // keep the actuator inside the (possibly new) grid
    if (m_fTarget < m_cfg.fmin) {
        m_fTarget = m_cfg.fmin;
    }
    if (m_fTarget > m_cfg.freqCap) {
        m_fTarget = m_cfg.freqCap;
    }
    return true;
}

// -- grid helpers -----------------------------------------------------------

float ThermalGovernor::snap(float freq) const
{
    int k = (int) lroundf((freq - m_cfg.fmin) / m_cfg.stepMhz);
    if (k < 0) {
        k = 0;
    }
    float f = m_cfg.fmin + (float) k * m_cfg.stepMhz;
    if (f > m_cfg.freqCap) {
        f = m_cfg.freqCap;
    }
    return f;
}

float ThermalGovernor::rawMvFor(float freq) const
{
    return curveMv(m_curveF, m_curveMv, m_curveCount, freq) + (float) m_vTrim;
}

int ThermalGovernor::mvFor(float freq) const
{
    int mv = roundUp5mv(rawMvFor(freq));
    if (mv < m_cfg.vmin) {
        mv = m_cfg.vmin;
    }
    if (mv > m_cfg.voltageCap) {
        mv = m_cfg.voltageCap;
    }
    return mv;
}

// Highest grid frequency whose curve voltage still fits voltageCap.
float ThermalGovernor::freqCapEffective() const
{
    float f = m_cfg.freqCap;
    while (f > m_cfg.fmin) {
        if (roundUp5mv(rawMvFor(f)) <= m_cfg.voltageCap) {
            break;
        }
        f -= m_cfg.stepMhz;
    }
    return f;
}

// -- hashrate ring ----------------------------------------------------------

void ThermalGovernor::hrHistClear()
{
    m_hrHead = 0;
    m_hrCount = 0;
}

void ThermalGovernor::hrHistPush(float t, float v)
{
    m_hrT[m_hrHead] = t;
    m_hrV[m_hrHead] = v;
    m_hrHead = (m_hrHead + 1) % kHrMax;
    if (m_hrCount < kHrMax) {
        m_hrCount++;
    }
}

// -- public API -------------------------------------------------------------

GovDecision ThermalGovernor::update(const GovInputs &x)
{
    const GovConfig &c = m_cfg;
    float now = x.nowS;
    OptFloat noMargins[LIM_COUNT];
    OptFloat none;
    char buf[GOV_REASON_LEN];

    if (!m_hasT0) {
        m_t0 = now;
        m_hasT0 = true;
        m_startupUntil = now + c.startupHoldS;
        if (!m_seeded) {
            m_fTarget = c.fmin; // a cold start always begins at fmin
        }
        m_lastFreqChangeS = now;
    }

    ingest(x, now);
    // Cached so every emitted row carries it, not just decision samples.
    m_hrRatioCached = hrRatio();

    // ---- freeze conditions: never actuate blindly --------------------------
    bool overheatLatched =
        x.appliedMv.valid && x.appliedMv.value > 0.0f && x.voutMv.valid && x.voutMv.value < c.overheatVoutMv;
    if (x.frozen || overheatLatched) {
        m_state = STATE_FROZEN;
        return emit(ACT_HOLD, STATE_FROZEN, LIM_NONE, 0.0f,
                    overheatLatched ? "overheat latch (vout~0)" : "frozen by caller", noMargins, none, none, none);
    }

    float base = baseFreq(x);
    m_fTarget = base;

    // ---- sensor availability ----------------------------------------------
    bool vrOk = m_chVr.usable(now, c.sensorSingleFailS);
    bool vriOk = m_chVri.usable(now, c.sensorSingleFailS);
    float vrStale = m_chVr.staleFor(now);
    float vriStale = m_chVri.staleFor(now);
    float bothDeadFor = (vrStale < vriStale) ? vrStale : vriStale;
    if (!vrOk && !vriOk && bothDeadFor > c.sensorBothFailS) {
        m_state = STATE_SENSOR_FAIL;
        m_fTarget = c.fmin;
        snprintf(buf, sizeof(buf), "both thermal sensors invalid for %.0fs -> fmin", (double) bothDeadFor);
        return emit(base > c.fmin ? ACT_DOWN : ACT_HOLD, STATE_SENSOR_FAIL, LIM_NONE, 0.0f, buf, noMargins, none, none, none);
    }

    OptFloat predVr, predVri;
    predictedTemps(vrOk, vriOk, &predVr, &predVri);

    OptFloat margins[LIM_COUNT];
    computeMargins(predVr, predVri, margins);
    uint8_t limiter = LIM_NONE;
    float margin = 0.0f;
    worst(margins, &limiter, &margin);

    maybeClearBlock(now, margin);

    // ---- emergency: evaluated every sample ---------------------------------
    int emgSteps = 0;
    bool emgThermal = false;
    char why[48];
    if (emergencyCheck(x, now, &emgSteps, &emgThermal, why, sizeof(why))) {
        if (base <= c.fmin) {
            // Already at the floor: there is nothing to give. Report the
            // saturation instead of re-firing EMERGENCY_DOWN every cycle, which
            // would spam the log and keep doubling the block timer for a
            // descent that cannot happen.
            m_thermalBackoffUntil = now + c.thermalBackoffS;
            m_state = STATE_SATURATED_LOW;
            snprintf(buf, sizeof(buf), "at fmin, emergency persists (%s)", why);
            return emit(ACT_HOLD, STATE_SATURATED_LOW, limiter, margin, buf, margins, predVr, predVri, none);
        }
        if (now - m_lastEmergencyS >= c.emgPeriodS) {
            m_lastEmergencyS = now;
            m_emergencyStateUntil = now + c.emgStateHoldS;
            setBlock(base, emgThermal, now);
            applySteps(-emgSteps, now);
            m_thermalBackoffUntil = now + c.thermalBackoffS;
            m_state = STATE_EMERGENCY;
            m_countEmergencies++;
            snprintf(buf, sizeof(buf), "EMERGENCY %s -> -%d steps", why, emgSteps);
            return emit(ACT_EMERGENCY_DOWN, STATE_EMERGENCY, limiter, margin, buf, margins, predVr, predVri, none);
        }
        m_state = STATE_EMERGENCY;
        snprintf(buf, sizeof(buf), "emergency rate limit (%s)", why);
        return emit(ACT_HOLD, STATE_EMERGENCY, limiter, margin, buf, margins, predVr, predVri, none);
    }

    // ---- periodic decision -------------------------------------------------
    if (now < m_startupUntil) {
        m_state = STATE_STARTUP;
        snprintf(buf, sizeof(buf), "startup hold (%.0fs left)", (double) (m_startupUntil - now));
        return emit(ACT_HOLD, STATE_STARTUP, limiter, margin, buf, margins, predVr, predVri, none);
    }

    if (now - m_lastDecisionS < c.decisionPeriodS) {
        uint8_t st = idleState(now, base, margin);
        m_state = st;
        return emit(ACT_HOLD, st, limiter, margin, "between decisions", margins, predVr, predVri, none);
    }
    m_lastDecisionS = now;

    // ---- descend -----------------------------------------------------------
    if (margin < 0.0f) {
        if (base <= c.fmin) {
            m_state = STATE_SATURATED_LOW;
            snprintf(buf, sizeof(buf), "at fmin and still over %s limit", limiterToStr(limiter));
            return emit(ACT_HOLD, STATE_SATURATED_LOW, limiter, margin, buf, margins, predVr, predVri, none);
        }
        int steps = (int) ceilf(-margin);
        if (steps < 1) {
            steps = 1;
        }
        if (steps > c.downMaxSteps) {
            steps = c.downMaxSteps;
        }
        if (limiter == LIM_VR || limiter == LIM_VRI) {
            m_thermalBackoffUntil = now + c.thermalBackoffS;
        }
        applySteps(-steps, now);
        m_state = STATE_BACKOFF;
        m_countDowns++;
        snprintf(buf, sizeof(buf), "margin %.2f < 0 on %s -> -%d steps", (double) margin, limiterToStr(limiter), steps);
        return emit(ACT_DOWN, STATE_BACKOFF, limiter, margin, buf, margins, predVr, predVri, none);
    }

    // ---- hashrate degradation ----------------------------------------------
    OptFloat ratio = m_hrRatioCached;
    uint8_t hrAction = ACT_HOLD;
    if (hashrateResponse(ratio, margins, now, base, &hrAction, buf, sizeof(buf))) {
        m_state = (hrAction == ACT_DOWN) ? STATE_BACKOFF : STATE_HOLD;
        if (hrAction == ACT_DOWN) {
            m_countDowns++;
        }
        return emit(hrAction, m_state, limiter, margin, buf, margins, predVr, predVri, ratio);
    }

    // ---- ascend ------------------------------------------------------------
    int upSteps = 0;
    if (tryUp(now, base, margin, margins, predVr, predVri, vrOk, vriOk, &upSteps, buf, sizeof(buf))) {
        applySteps(upSteps, now);
        m_lastUpS = now;
        m_state = STATE_RAMP_UP;
        m_countUps++;
        return emit(ACT_UP, STATE_RAMP_UP, limiter, margin, buf, margins, predVr, predVri, ratio);
    }

    m_state = idleState(now, base, margin);
    snprintf(buf, sizeof(buf), "hold (margin %.2f on %s)", (double) margin, limiterToStr(limiter));
    return emit(ACT_HOLD, m_state, limiter, margin, buf, margins, predVr, predVri, ratio);
}

// -- internals --------------------------------------------------------------

void ThermalGovernor::ingest(const GovInputs &x, float now)
{
    m_chVr.push(x.vrTemp, now);
    m_chVri.push(x.vrTempInt, now);
    m_chPin.push(x.pin, now);
    m_chIin.push(x.iin, now);
    m_chVin.push(x.vin, now);
    m_chIout.push(x.iout, now);

    // hashrate ratio history, normalised by the frequency actually applied
    if (x.hashrate.valid && x.hashrate.value > 0.0f) {
        float fApp = x.appliedFreq.valid ? x.appliedFreq.value : m_fTarget;
        float expected = m_cfg.expectedHashrate(fApp);
        if (expected > 0.0f) {
            hrHistPush(now, x.hashrate.value / expected);
        }
    }
    float cutoff = now - m_cfg.hrWindowS;
    while (m_hrCount > 0) {
        int oldest = (m_hrHead - m_hrCount + kHrMax * 2) % kHrMax;
        if (m_hrT[oldest] >= cutoff) {
            break;
        }
        m_hrCount--;
    }
}

// Shadow mode reasons from what the miner really runs at.
float ThermalGovernor::baseFreq(const GovInputs &x) const
{
    if (m_cfg.shadow && x.appliedFreq.valid && x.appliedFreq.value > 0.0f) {
        float f = (x.appliedFreq.value > m_cfg.fmin) ? x.appliedFreq.value : m_cfg.fmin;
        return snap(f);
    }
    return m_fTarget;
}

void ThermalGovernor::predictedTemps(bool vrOk, bool vriOk, OptFloat *predVr, OptFloat *predVri) const
{
    float h = m_cfg.predictHorizonS;
    predVr->clear();
    predVri->clear();
    if (vrOk) {
        OptFloat v = m_chVr.smooth();
        if (v.valid) {
            predVr->set(v.value + m_chVr.slope() * h);
        }
    }
    if (vriOk) {
        OptFloat v = m_chVri.smooth();
        if (v.valid) {
            predVri->set(v.value + m_chVri.slope() * h);
        }
    }
}

void ThermalGovernor::computeMargins(const OptFloat &predVr, const OptFloat &predVri, OptFloat *m) const
{
    const GovConfig &c = m_cfg;
    for (int i = 0; i < LIM_COUNT; i++) {
        m[i].clear();
    }
    if (predVr.valid) {
        m[LIM_VR].set((c.vrTarget - predVr.value) / c.sensVr);
    }
    if (predVri.valid) {
        m[LIM_VRI].set((c.vriTarget - predVri.value) / c.sensVri);
    }
    // A limit of 0 disables the constraint entirely.
    if (c.pinMax > 0.0f) {
        OptFloat v = m_chPin.median();
        if (v.valid) {
            m[LIM_PIN].set((c.pinMax - v.value) / c.sensPin);
        }
    }
    if (c.iinMax > 0.0f) {
        OptFloat v = m_chIin.median();
        if (v.valid) {
            m[LIM_IIN].set((c.iinMax - v.value) / c.sensIin);
        }
    }
    if (c.ioutMax > 0.0f) {
        OptFloat v = m_chIout.median();
        if (v.valid) {
            m[LIM_IOUT].set((c.ioutMax - v.value) / c.sensIout);
        }
    }
    if (c.vinMin > 0.0f) {
        OptFloat v = m_chVin.median();
        if (v.valid) {
            // sensVin is negative: a lower bound expressed with the same formula
            m[LIM_VIN].set((c.vinMin - v.value) / c.sensVin);
        }
    }
}

void ThermalGovernor::worst(const OptFloat *margins, uint8_t *limiter, float *margin)
{
    *limiter = LIM_NONE;
    bool have = false;
    float best = 0.0f;
    for (int i = 0; i < LIM_COUNT; i++) {
        if (!margins[i].valid) {
            continue;
        }
        if (!have || margins[i].value < best) {
            have = true;
            best = margins[i].value;
            *limiter = (uint8_t) i;
        }
    }
    *margin = have ? best : 0.0f;
}

// Returns true when an emergency is active. Uses the median-of-3 path.
bool ThermalGovernor::emergencyCheck(const GovInputs &x, float now, int *stepsOut, bool *thermalOut, char *why,
                                     size_t whyLen)
{
    const GovConfig &c = m_cfg;
    float vrHard = c.vrTarget + c.vrHardDelta;
    float vriHard = c.vriTarget + c.vriHardDelta;

    float worstExcess = -1.0f;
    bool worstThermal = false;
    bool any = false;
    size_t used = 0;
    why[0] = 0;

    // appends "text" to why, joined with '+', and keeps the worst excess
    struct Local {
        static void addWhy(char *dst, size_t cap, size_t *used, const char *text)
        {
            int n;
            if (*used == 0) {
                n = snprintf(dst, cap, "%s", text);
            } else {
                n = snprintf(dst + *used, cap - *used, "+%s", text);
            }
            if (n > 0) {
                *used += (size_t) n;
                if (*used >= cap) {
                    *used = cap - 1;
                }
            }
        }
    };

    char text[32];

    OptFloat vr = m_chVr.fast();
    if (vr.valid && vr.value >= vrHard) {
        float ex = (vr.value - vrHard) / c.sensVr;
        if (ex < 0.0f) {
            ex = 0.0f;
        }
        if (ex > worstExcess) {
            worstExcess = ex;
            worstThermal = true;
        }
        snprintf(text, sizeof(text), "vrTemp %.1f>=%.1f", (double) vr.value, (double) vrHard);
        Local::addWhy(why, whyLen, &used, text);
        any = true;
    }

    OptFloat vri = m_chVri.fast();
    if (vri.valid && vri.value >= vriHard) {
        float ex = (vri.value - vriHard) / c.sensVri;
        if (ex < 0.0f) {
            ex = 0.0f;
        }
        if (ex > worstExcess) {
            worstExcess = ex;
            worstThermal = true;
        }
        snprintf(text, sizeof(text), "vrTempInt %.1f>=%.1f", (double) vri.value, (double) vriHard);
        Local::addWhy(why, whyLen, &used, text);
        any = true;
    }

    OptFloat iout = m_chIout.fast();
    if (c.ioutMax > 0.0f && iout.valid && iout.value >= c.ioutMax + c.emgIoutOver) {
        float ex = (iout.value - c.ioutMax) / c.sensIout;
        if (ex < 0.0f) {
            ex = 0.0f;
        }
        if (ex > worstExcess) {
            worstExcess = ex;
            worstThermal = false;
        }
        snprintf(text, sizeof(text), "iout %.1f", (double) iout.value);
        Local::addWhy(why, whyLen, &used, text);
        any = true;
    }

    OptFloat vin = m_chVin.fast();
    if (c.vinMin > 0.0f && vin.valid && vin.value < c.vinMin - c.emgVinUnder) {
        float ex = (c.vinMin - vin.value) / fabsf(c.sensVin);
        if (ex < 0.0f) {
            ex = 0.0f;
        }
        if (ex > worstExcess) {
            worstExcess = ex;
            worstThermal = false;
        }
        snprintf(text, sizeof(text), "vin %.2f", (double) vin.value);
        Local::addWhy(why, whyLen, &used, text);
        any = true;
    }

    OptFloat pin = m_chPin.fast();
    if (c.pinMax > 0.0f && pin.valid && pin.value > c.pinMax * c.emgPinRatio) {
        float ex = (pin.value - c.pinMax) / c.sensPin;
        if (ex < 0.0f) {
            ex = 0.0f;
        }
        if (ex > worstExcess) {
            worstExcess = ex;
            worstThermal = false;
        }
        snprintf(text, sizeof(text), "pin %.1f", (double) pin.value);
        Local::addWhy(why, whyLen, &used, text);
        any = true;
    }

    // Voltage sag: Vset - Vout, needs N consecutive samples; a grace window
    // after a voltage command avoids false alarms while the PMIC ramps.
    bool sagNow = false;
    float sagMv = 0.0f;
    if (x.appliedMv.valid && x.appliedMv.value > 0.0f && x.voutMv.valid && x.voutMv.value > c.overheatVoutMv) {
        if (now - m_lastVoltCmdS > c.vsagGraceS) {
            sagMv = x.appliedMv.value - x.voutMv.value;
            sagNow = sagMv > c.vsagMv;
        }
    }
    m_vsagRun = sagNow ? m_vsagRun + 1 : 0;
    if (m_vsagRun >= c.emgVsagSamples) {
        if (worstExcess < 0.0f) {
            worstExcess = 0.0f;
            worstThermal = false;
        }
        snprintf(text, sizeof(text), "vsag %.0fmV", (double) sagMv);
        Local::addWhy(why, whyLen, &used, text);
        any = true;
    }

    if (!any) {
        return false;
    }
    if (worstExcess < 0.0f) {
        worstExcess = 0.0f;
    }
    int steps = (int) ceilf(worstExcess);
    if (steps < c.emgMinSteps) {
        steps = c.emgMinSteps;
    }
    if (steps > c.emgMaxSteps) {
        steps = c.emgMaxSteps;
    }
    *stepsOut = steps;
    *thermalOut = worstThermal;
    return true;
}

OptFloat ThermalGovernor::hrRatio() const
{
    const GovConfig &c = m_cfg;
    if (m_hrCount < 5) {
        return OptFloat();
    }
    int oldest = (m_hrHead - m_hrCount + kHrMax * 2) % kHrMax;
    int newest = (m_hrHead - 1 + kHrMax) % kHrMax;
    float span = m_hrT[newest] - m_hrT[oldest];
    if (span < c.hrWindowS * 0.8f) {
        return OptFloat();
    }
    // median of the ratios in the window
    int n = m_hrCount;
    if (n > kHrMax) {
        n = kHrMax;
    }
    float tmp[kHrMax];
    for (int i = 0; i < n; i++) {
        tmp[i] = m_hrV[(oldest + i) % kHrMax];
    }
    for (int i = 1; i < n; i++) {
        float key = tmp[i];
        int j = i;
        while (j > 0 && tmp[j - 1] > key) {
            tmp[j] = tmp[j - 1];
            j--;
        }
        tmp[j] = key;
    }
    return OptFloat(tmp[n / 2]);
}

bool ThermalGovernor::hashrateResponse(const OptFloat &ratio, const OptFloat *margins, float now, float base,
                                       uint8_t *action, char *reason, size_t reasonLen)
{
    const GovConfig &c = m_cfg;
    if (!ratio.valid || ratio.value >= c.hrMin) {
        return false;
    }
    if (now - m_lastFreqChangeS < c.hrStableS) {
        return false;
    }
    if (now - m_lastHrActionS < c.hrActionGapS) {
        return false;
    }

    bool haveRoom = (!margins[LIM_PIN].valid || margins[LIM_PIN].value >= c.vtrimMarginSteps) &&
                    (!margins[LIM_IOUT].valid || margins[LIM_IOUT].value >= c.vtrimMarginSteps);
    m_lastHrActionS = now;

    if (m_vTrim + c.vTrimStep <= c.vTrimMax && haveRoom) {
        m_vTrim += c.vTrimStep;
        m_lastVoltCmdS = now;
        *action = ACT_VTRIM;
        snprintf(reason, reasonLen, "hashrate %.3f < %.2f -> v_trim=+%dmV", (double) ratio.value, (double) c.hrMin, m_vTrim);
        return true;
    }
    if (base <= c.fmin) {
        return false;
    }
    applySteps(-c.hrDownSteps, now);
    *action = ACT_DOWN;
    snprintf(reason, reasonLen, "hashrate %.3f < %.2f, v_trim exhausted -> -%d steps", (double) ratio.value,
             (double) c.hrMin, c.hrDownSteps);
    return true;
}

bool ThermalGovernor::tryUp(float now, float base, float margin, const OptFloat *margins, const OptFloat &predVr,
                            const OptFloat &predVri, bool vrOk, bool vriOk, int *stepsOut, char *reason, size_t reasonLen)
{
    const GovConfig &c = m_cfg;
    if (!vrOk && !vriOk) {
        return false;
    }
    // Every *enabled* electrical constraint must have a reading before we go up.
    // (Python required all three unconditionally; here a limit of 0 disables the
    // constraint, so requiring its margin would mean never ramping.)
    if (c.pinMax > 0.0f && !margins[LIM_PIN].valid) {
        return false;
    }
    if (c.iinMax > 0.0f && !margins[LIM_IIN].valid) {
        return false;
    }
    if (c.vinMin > 0.0f && !margins[LIM_VIN].valid) {
        return false;
    }
    if (margin < c.upMarginSteps) {
        return false;
    }
    if (now < m_thermalBackoffUntil) {
        return false;
    }

    bool haveThermal = false;
    float thermalMarginC = 0.0f;
    if (predVr.valid) {
        thermalMarginC = c.vrTarget - predVr.value;
        haveThermal = true;
    }
    if (predVri.valid) {
        float v = c.vriTarget - predVri.value;
        if (!haveThermal || v < thermalMarginC) {
            thermalMarginC = v;
        }
        haveThermal = true;
    }
    float period = (haveThermal && thermalMarginC > c.upFastThermalC) ? c.upFastPeriodS : c.upSlowPeriodS;
    if (now - m_lastUpS < period) {
        return false;
    }

    int steps = (margin >= c.upDoubleMarginSteps) ? 2 : 1;
    float cap = freqCapEffective();
    float target = base + (float) steps * c.stepMhz;
    if (m_fBlocked.valid) {
        float limit = m_fBlocked.value - c.stepMhz;
        if (target > limit) {
            steps = (int) lroundf((limit - base) / c.stepMhz);
            if (steps < 1) {
                return false;
            }
            target = base + (float) steps * c.stepMhz;
        }
    }
    if (target > cap) {
        steps = (int) lroundf((cap - base) / c.stepMhz);
        if (steps < 1) {
            return false;
        }
    }
    *stepsOut = steps;
    snprintf(reason, reasonLen, "margin %.2f >= %.2f -> +%d step(s)", (double) margin, (double) c.upMarginSteps, steps);
    return true;
}

void ThermalGovernor::applySteps(int steps, float now)
{
    const GovConfig &c = m_cfg;
    float old = m_fTarget;
    float f = m_fTarget + (float) steps * c.stepMhz;
    float cap = freqCapEffective();
    if (f > cap) {
        f = cap;
    }
    if (f < c.fmin) {
        f = c.fmin;
    }
    f = snap(f);
    if (f != old) {
        m_fTarget = f;
        m_lastFreqChangeS = now;
        m_lastVoltCmdS = now; // voltage follows the curve on every move
        hrHistClear();        // ratios across a change are not comparable
    }
}

void ThermalGovernor::setBlock(float freq, bool thermal, float now)
{
    const GovConfig &c = m_cfg;
    if (now > m_blockForgetAfter) {
        m_blockCount = 0;
    }
    m_blockCount++;
    float base = thermal ? c.blockThermalS : c.blockElectricalS;
    float dur = base * powf(2.0f, (float) (m_blockCount - 1));
    if (dur > c.blockMaxS) {
        dur = c.blockMaxS;
    }
    m_fBlocked.set(freq);
    m_blockUntil = now + dur;
    m_blockForgetAfter = now + 2.0f * dur;
}

void ThermalGovernor::maybeClearBlock(float now, float margin)
{
    if (!m_fBlocked.valid) {
        return;
    }
    if (now >= m_blockUntil || margin >= m_cfg.blockClearMargin) {
        m_fBlocked.clear();
    }
}

uint8_t ThermalGovernor::idleState(float now, float base, float margin) const
{
    const GovConfig &c = m_cfg;
    if (now < m_emergencyStateUntil) {
        return STATE_EMERGENCY;
    }
    if (base <= c.fmin && margin < 0.0f) {
        return STATE_SATURATED_LOW;
    }
    if (now < m_thermalBackoffUntil) {
        return STATE_BACKOFF;
    }
    if (now - m_lastUpS < c.upFastPeriodS) {
        return STATE_RAMP_UP;
    }
    return STATE_HOLD;
}

GovDecision ThermalGovernor::emit(uint8_t action, uint8_t state, uint8_t limiter, float margin, const char *reason,
                                  const OptFloat *margins, const OptFloat &predVr, const OptFloat &predVri,
                                  const OptFloat &hrRatioArg)
{
    GovDecision d;
    d.freq = m_fTarget;
    d.mv = mvFor(m_fTarget);
    d.state = state;
    d.limiter = limiter;
    d.margin = margin;
    d.action = action;
    snprintf(d.reason, sizeof(d.reason), "%s", reason ? reason : "");
    for (int i = 0; i < LIM_COUNT; i++) {
        d.margins[i] = margins[i];
    }
    d.vTrim = m_vTrim;
    d.fBlocked = m_fBlocked;
    d.predVr = predVr;
    d.predVri = predVri;
    d.hrRatio = hrRatioArg.valid ? hrRatioArg : m_hrRatioCached;
    return d;
}

// ---------------------------------------------------------------------------
// JSON (ESP only)
// ---------------------------------------------------------------------------

#ifdef ESP_PLATFORM

static void addOpt(JsonObject obj, const char *key, const gov::OptFloat &v)
{
    if (v.valid) {
        obj[key] = v.value;
    } else {
        obj[key] = nullptr; // ArduinoJson 7 has a converter for nullptr_t
    }
}

void ThermalGovernor::fillJson(JsonObject obj, const GovDecision &last, int mode, bool forcedOff, float lastActionAgeS) const
{
    obj["mode"] = mode;
    obj["forcedOff"] = forcedOff;
    obj["state"] = stateToStr(last.state);
    obj["limiter"] = limiterToStr(last.limiter);
    obj["freq"] = last.freq;
    obj["voltage"] = last.mv;
    obj["freqCap"] = m_cfg.freqCap;
    obj["voltageCap"] = m_cfg.voltageCap;
    addOpt(obj, "blockedFreq", last.fBlocked);
    obj["vTrim"] = last.vTrim;
    obj["lastAction"] = actionToStr(last.action);
    obj["lastReason"] = last.reason;
    obj["lastActionAgeS"] = lastActionAgeS;
    obj["margin"] = last.margin;

    JsonObject flt = obj["filtered"].to<JsonObject>();
    addOpt(flt, "vrTemp", m_chVr.smooth());
    addOpt(flt, "vrTempInt", m_chVri.smooth());
    addOpt(flt, "pin", m_chPin.median());
    addOpt(flt, "iin", m_chIin.median());
    addOpt(flt, "vin", m_chVin.median());
    addOpt(flt, "iout", m_chIout.median());
    flt["slope"] = m_chVr.slope();
    addOpt(flt, "predVr", last.predVr);
    addOpt(flt, "predVrInt", last.predVri);
    addOpt(flt, "hrRatio", last.hrRatio);

    JsonObject mg = obj["margins"].to<JsonObject>();
    for (int i = 0; i < LIM_COUNT; i++) {
        addOpt(mg, limiterToStr((uint8_t) i), last.margins[i]);
    }

    JsonObject cnt = obj["counters"].to<JsonObject>();
    cnt["ups"] = m_countUps;
    cnt["downs"] = m_countDowns;
    cnt["emergencies"] = m_countEmergencies;

    JsonObject cfg = obj["cfg"].to<JsonObject>();
    cfg["vrTarget"] = m_cfg.vrTarget;
    cfg["vrHard"] = m_cfg.vrTarget + m_cfg.vrHardDelta;
    cfg["vriTarget"] = m_cfg.vriTarget;
    cfg["vriHard"] = m_cfg.vriTarget + m_cfg.vriHardDelta;
    cfg["ioutMax"] = m_cfg.ioutMax;
    cfg["vinMin"] = m_cfg.vinMin;
    cfg["pinMax"] = m_cfg.pinMax;
    cfg["iinMax"] = m_cfg.iinMax;
    cfg["fmin"] = m_cfg.fmin;
    cfg["vmin"] = m_cfg.vmin;
    cfg["vsagMv"] = m_cfg.vsagMv;
    cfg["hrMin"] = m_cfg.hrMin;
    cfg["curve"] = m_cfg.curve;
}

#endif // ESP_PLATFORM

} // namespace gov
