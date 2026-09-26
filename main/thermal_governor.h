#pragma once

// Thermal / electrical governor for the NerdQAxe++.
//
// This is a *pure* control law: no I2C, no FreeRTOS, no wall clock, no logging
// and no dynamic allocation. Time enters through GovInputs::nowS. It is a 1:1
// transcription of the Python prototype in governor/governor_core.py (47 host
// tests) so that both implementations can be diffed by hand.
//
// The only ESP specific part is fillJson(), guarded by ESP_PLATFORM, so this
// header also compiles with a plain host g++ for the unit tests in
// test/host_governor/.
//
// Design rules kept from the prototype:
//   * fixed size windows and explicit loops only
//   * the dT/dt slope is taken from the EMA, never from the median
//   * emergencies are evaluated every sample, regular decisions every 10 s
//   * update() always returns the command that should currently be in force,
//     so the caller may re-apply it idempotently

#include <stdint.h>
#include <stddef.h>

#ifdef ESP_PLATFORM
#include "ArduinoJson.h"
#endif

namespace gov {

// ---------------------------------------------------------------------------
// States / actions / limiters
// ---------------------------------------------------------------------------

enum State : uint8_t {
    STATE_STARTUP = 0,
    STATE_RAMP_UP,
    STATE_HOLD,
    STATE_BACKOFF,
    STATE_EMERGENCY,
    STATE_SATURATED_LOW,
    STATE_SENSOR_FAIL,
    STATE_FROZEN,
};

enum Action : uint8_t {
    ACT_HOLD = 0,
    ACT_UP,
    ACT_DOWN,
    ACT_EMERGENCY_DOWN,
    ACT_VTRIM,
};

// Constraint identifiers. LIM_COUNT doubles as "no limiter".
enum Limiter : uint8_t {
    LIM_VR = 0,
    LIM_VRI,
    LIM_PIN,
    LIM_IIN,
    LIM_IOUT,
    LIM_VIN,
    LIM_COUNT,
};
static const uint8_t LIM_NONE = LIM_COUNT;

const char *stateToStr(uint8_t s);
const char *actionToStr(uint8_t a);
const char *limiterToStr(uint8_t l);

// ---------------------------------------------------------------------------
// Optional float (the port of Python's Optional[float])
// ---------------------------------------------------------------------------

struct OptFloat {
    float value;
    bool valid;

    OptFloat() : value(0.0f), valid(false) {}
    explicit OptFloat(float v) : value(v), valid(true) {}

    void clear()
    {
        value = 0.0f;
        valid = false;
    }
    void set(float v)
    {
        value = v;
        valid = true;
    }
};

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

// Sensitivities are "per 6.25 MHz step, measured at the top of the range",
// from a frequency sweep of the real hardware. The thermal response is
// convex, so the top-of-range values are used: that is where the governor
// actually operates.
//
// Measured sensor resolutions, for reference:
//   vrTemp 0.06 C, vrTempInt 0.12 C, pin 0.25 W, iin 0.17 A, vin 0.015 V.
// iin resolution (0.17 A) is coarser than its per-step sensitivity (0.16 A),
// which is why the iin constraint is disabled by default (iinMax = 0).

#define GOV_CURVE_MAX_LEN 64
#define GOV_CURVE_MAX_POINTS 8
#define GOV_REASON_LEN 80

struct GovConfig {
    // --- actuator grid ----------------------------------------------------
    float fmin;
    float freqCap;   // user ceiling, never read back from the miner
    float stepMhz;
    int vmin;
    int voltageCap;  // user ceiling, never read back from the miner
    char curve[GOV_CURVE_MAX_LEN];
    int vTrimStep;
    int vTrimMax;

    // --- timing -----------------------------------------------------------
    float samplePeriodS;
    float decisionPeriodS;
    float startupHoldS;

    // --- filtering --------------------------------------------------------
    float emaTauS;
    float slopeWindowS;
    float predictHorizonS;

    // --- plausibility -----------------------------------------------------
    float tempLo;
    float tempHi;
    float tempMaxDelta; // per sample
    float ioutLo;
    float ioutHi;
    float vinLo;
    float vinHi;
    float pinLo;
    float pinHi;
    float iinLo;
    float iinHi;
    int resyncAfter; // consecutive delta rejects -> accept (re-sync)

    // --- constraints (a limit of 0 disables the constraint) ---------------
    float vrTarget;
    float vrHardDelta;
    float vriTarget;
    float vriHardDelta;
    float pinMax;
    float iinMax;
    float vinMin;
    float ioutMax;
    float vsagMv;
    float hrMin;

    // --- sensitivities, per 6.25 MHz step, top of the range ---------------
    float sensVr;
    float sensVri;
    float sensPin;
    float sensIin;
    float sensIout;
    float sensVin;

    // --- up / down policy -------------------------------------------------
    float upMarginSteps;
    float upDoubleMarginSteps;
    float upFastPeriodS;
    float upSlowPeriodS;
    float upFastThermalC;
    int downMaxSteps;

    // --- emergency --------------------------------------------------------
    int emgMinSteps;
    int emgMaxSteps;
    float emgIoutOver;
    float emgVinUnder;
    float emgPinRatio;
    int emgVsagSamples;
    float emgPeriodS;      // rate limit between emergency actions
    float emgStateHoldS;
    float vsagGraceS;      // ignore vsag right after a voltage command

    // --- anti-oscillation -------------------------------------------------
    float thermalBackoffS;
    float blockElectricalS;
    float blockThermalS;
    float blockMaxS;
    float blockClearMargin;

    // --- hashrate ---------------------------------------------------------
    float hrWindowS;
    float hrStableS;
    float hrActionGapS;
    int hrDownSteps;
    float vtrimMarginSteps;
    int smallCores;
    int asicCount;

    // --- sensor failure ---------------------------------------------------
    float sensorSingleFailS;
    float sensorBothFailS;
    float overheatVoutMv; // vout ~0 while vset > 0 -> firmware latched

    // --- mode -------------------------------------------------------------
    bool shadow; // base frequency comes from the miner instead of our target

    // Fills every field with the measured NerdQAxe++ defaults.
    void setDefaults();

    // GH/s a healthy chain should deliver at freq.
    float expectedHashrate(float freq) const;
};

// ---------------------------------------------------------------------------
// Inputs / outputs
// ---------------------------------------------------------------------------

struct GovInputs {
    OptFloat vrTemp;
    OptFloat vrTempInt;
    OptFloat pin;
    OptFloat iin;
    OptFloat vin;
    OptFloat iout;
    OptFloat voutMv;
    OptFloat hashrate;    // GH/s
    OptFloat appliedFreq;
    OptFloat appliedMv;
    bool frozen;
    float nowS;

    GovInputs() : frozen(false), nowS(0.0f) {}
};

struct GovDecision {
    float freq;
    int mv;
    uint8_t state;
    uint8_t limiter;
    float margin;
    uint8_t action;
    char reason[GOV_REASON_LEN];

    OptFloat margins[LIM_COUNT];
    int vTrim;
    OptFloat fBlocked;
    OptFloat predVr;
    OptFloat predVri;
    OptFloat hrRatio;

    GovDecision()
        : freq(0.0f), mv(0), state(STATE_STARTUP), limiter(LIM_NONE), margin(0.0f), action(ACT_HOLD), vTrim(0)
    {
        reason[0] = 0;
    }
};

// ---------------------------------------------------------------------------
// Small filters (fixed windows, no allocation)
// ---------------------------------------------------------------------------

#define GOV_MEDIAN_MAX_N 5
#define GOV_SLOPE_MAX_N 64

class MedianWindow {
  public:
    void init(int n);
    void push(float v);
    OptFloat value() const;
    void reset();

  private:
    float m_buf[GOV_MEDIAN_MAX_N] = {0.0f};
    int m_n = GOV_MEDIAN_MAX_N;
    int m_count = 0;
    int m_head = 0; // next write position (ring, oldest gets overwritten)
};

class Ema {
  public:
    void init(float tauS);
    float push(float v, float nowS);
    OptFloat value() const;

  private:
    float m_tau = 30.0f;
    float m_v = 0.0f;
    float m_t = 0.0f;
    bool m_hasV = false;
    bool m_hasT = false;
};

// Least-squares dv/dt over a moving time window.
class SlopeWindow {
  public:
    void init(float windowS);
    void push(float v, float nowS);
    float value() const;

  private:
    float m_t[GOV_SLOPE_MAX_N] = {0.0f};
    float m_v[GOV_SLOPE_MAX_N] = {0.0f};
    int m_head = 0;
    int m_count = 0;
    float m_window = 60.0f;

    float at(int i, const float *buf) const;
};

// Plausibility gate -> median(5) -> optional EMA + slope, plus median(3).
class SignalChannel {
  public:
    void init(float lo, float hi, int medianN, int fastN, bool useMaxDelta, float maxDelta, bool useEma, float emaTauS,
              bool useSlope, float slopeWindowS, int resyncAfter);

    bool push(const OptFloat &raw, float nowS);

    OptFloat median() const
    {
        return m_med.value();
    }
    OptFloat fast() const
    {
        return m_fast.value();
    }
    OptFloat smooth() const;
    float slope() const;
    float staleFor(float nowS) const;
    bool usable(float nowS, float graceS) const;

  private:
    float m_lo = 0.0f;
    float m_hi = 0.0f;
    bool m_useMaxDelta = false;
    float m_maxDelta = 0.0f;
    int m_resyncAfter = 3;

    MedianWindow m_med;
    MedianWindow m_fast;
    Ema m_ema;
    SlopeWindow m_slope;
    bool m_useEma = false;
    bool m_useSlope = false;

    float m_lastAccepted = 0.0f;
    bool m_hasLastAccepted = false;
    int m_rejectRun = 0;

    float m_lastOkS = 0.0f;
    bool m_hasLastOk = false;
    float m_firstSeenS = 0.0f;
    bool m_hasFirstSeen = false;
};

// ---------------------------------------------------------------------------
// Governor
// ---------------------------------------------------------------------------

class ThermalGovernor {
  public:
    ThermalGovernor();

    // (Re)configures the governor. Returns false and keeps the previous config
    // if cfg is inconsistent (caps below floors, zero sensitivity, bad curve).
    // The signal channels are re-initialized (their gates come from the config)
    // so a live reload costs ~10 s of filter history; the actuator state and the
    // timers survive. An empty filter only makes the loop hold, never act.
    bool configure(const GovConfig &cfg);

    // Drops every filter and timer; the next update() restarts at fmin with a
    // fresh startup hold.
    void reset();

    // Starts from the operating point the miner is really running at instead of
    // fmin. Used when the governor is switched on while the ASICs are already
    // hashing: dropping to fmin and re-ramping would cost ~13 min of hashrate
    // for nothing. A cold boot keeps starting at fmin.
    void seedTarget(float freq);

    const GovConfig &config() const
    {
        return m_cfg;
    }

    GovDecision update(const GovInputs &x);

    // Voltage that the curve (plus the current v_trim) asks for at freq,
    // rounded up to 5 mV and clamped to [vmin, voltageCap].
    int mvFor(float freq) const;

    // Counters, for the API.
    uint32_t countUps() const
    {
        return m_countUps;
    }
    uint32_t countDowns() const
    {
        return m_countDowns;
    }
    uint32_t countEmergencies() const
    {
        return m_countEmergencies;
    }

    // Filtered signals, for the API.
    OptFloat filteredVrTemp() const
    {
        return m_chVr.smooth();
    }
    OptFloat filteredVrTempInt() const
    {
        return m_chVri.smooth();
    }
    OptFloat filteredPin() const
    {
        return m_chPin.median();
    }
    OptFloat filteredIin() const
    {
        return m_chIin.median();
    }
    OptFloat filteredVin() const
    {
        return m_chVin.median();
    }
    OptFloat filteredIout() const
    {
        return m_chIout.median();
    }
    float slopeVr() const
    {
        return m_chVr.slope();
    }

#ifdef ESP_PLATFORM
    // Serializes the governor state into a JSON object. mode/forcedOff are
    // owned by the caller (the PM task) because they are not control law.
    void fillJson(JsonObject obj, const GovDecision &last, int mode, bool forcedOff, float lastActionAgeS) const;
#endif

  private:
    GovConfig m_cfg;

    // parsed voltage curve
    float m_curveF[GOV_CURVE_MAX_POINTS];
    float m_curveMv[GOV_CURVE_MAX_POINTS];
    int m_curveCount;

    SignalChannel m_chVr;
    SignalChannel m_chVri;
    SignalChannel m_chPin;
    SignalChannel m_chIin;
    SignalChannel m_chVin;
    SignalChannel m_chIout;

    // actuator state
    float m_fTarget;
    bool m_seeded = false; // seedTarget() was called: the first update() must not force fmin
    int m_vTrim;

    // timers (all driven by nowS)
    float m_t0;
    bool m_hasT0;
    float m_startupUntil;
    float m_lastDecisionS;
    float m_lastUpS;
    float m_lastFreqChangeS;
    float m_lastVoltCmdS;
    float m_thermalBackoffUntil;
    float m_lastEmergencyS;
    float m_emergencyStateUntil;

    // blocking
    OptFloat m_fBlocked;
    float m_blockUntil;
    int m_blockCount;
    float m_blockForgetAfter;

    // misc
    int m_vsagRun;
    OptFloat m_hrRatioCached;
    float m_lastHrActionS;
    uint8_t m_state;

    // hashrate ratio history (fixed ring: 180 s window at 2 s = 90 samples)
    static const int kHrMax = 128;
    float m_hrT[kHrMax];
    float m_hrV[kHrMax];
    int m_hrHead;
    int m_hrCount;

    uint32_t m_countUps;
    uint32_t m_countDowns;
    uint32_t m_countEmergencies;

    // grid helpers
    float snap(float freq) const;
    float rawMvFor(float freq) const;
    float freqCapEffective() const;

    // internals
    void ingest(const GovInputs &x, float now);
    float baseFreq(const GovInputs &x) const;
    void predictedTemps(bool vrOk, bool vriOk, OptFloat *predVr, OptFloat *predVri) const;
    void computeMargins(const OptFloat &predVr, const OptFloat &predVri, OptFloat *margins) const;
    static void worst(const OptFloat *margins, uint8_t *limiter, float *margin);
    bool emergencyCheck(const GovInputs &x, float now, int *steps, bool *thermal, char *why, size_t whyLen);
    OptFloat hrRatio() const;
    bool hashrateResponse(const OptFloat &ratio, const OptFloat *margins, float now, float base, uint8_t *action,
                          char *reason, size_t reasonLen);
    bool tryUp(float now, float base, float margin, const OptFloat *margins, const OptFloat &predVr, const OptFloat &predVri,
               bool vrOk, bool vriOk, int *stepsOut, char *reason, size_t reasonLen);
    void applySteps(int steps, float now);
    void setBlock(float freq, bool thermal, float now);
    void maybeClearBlock(float now, float margin);
    uint8_t idleState(float now, float base, float margin) const;
    GovDecision emit(uint8_t action, uint8_t state, uint8_t limiter, float margin, const char *reason,
                     const OptFloat *margins, const OptFloat &predVr, const OptFloat &predVri, const OptFloat &hrRatioArg);

    void hrHistClear();
    void hrHistPush(float t, float v);
};

// Exposed for the host tests.
bool parseCurve(const char *spec, float *freqs, float *mvs, int maxPoints, int *countOut);
float curveMv(const float *freqs, const float *mvs, int count, float freq);
int roundUp5mv(float mv);

} // namespace gov
