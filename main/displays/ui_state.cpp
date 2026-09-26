#include "ui_state.h"

#include <stdio.h>
#include <string.h>

void uiStateSetDefaults(UiState &s)
{
    memset(&s, 0, sizeof(s));

    // cfg: the values the firmware ships with (governor defaults, PSU of this unit)
    s.freqCap = 800.0f;
    s.fmin = 500.0f;
    s.mvCap = 1250;
    s.vrTarget = 78.0f;
    s.vrHard = 82.0f;
    s.vrCut = 88.0f;
    s.vriTarget = 90.0f;
    s.vriWarn = 95.0f;
    s.ioutMax = 92.0f;
    s.ioutFault = 95.0f;
    s.psuW = 120.0f;
    s.tarifa = 0.95f;
    snprintf(s.currency, sizeof(s.currency), "R$");

    float *nans[] = {&s.freq, &s.voutMv, &s.hrInst, &s.hr1m, &s.hr10m, &s.hr1h, &s.hr1d, &s.hrExpected, &s.pin, &s.vin,
                     &s.iin, &s.iout, &s.pout, &s.vrLoss, &s.fanW, &s.effJth, &s.psuLoad, &s.vrTemp, &s.vrTempInt,
                     &s.boardTemp, &s.ambient, &s.slopeCpm, &s.headroom, &s.govMargin, &s.govBlockedFreq,
                     &s.sharesPerMin, &s.halvingDays, &s.cyclePos, &s.subsidyBtc, &s.rewardBtc, &s.kwhToday,
                     &s.costToday, &s.avgW24, &s.costDay, &s.costMonth, &s.kwhMonth};
    for (unsigned i = 0; i < sizeof(nans) / sizeof(nans[0]); i++) {
        *nans[i] = NAN;
    }
    for (int i = 0; i < 4; i++) {
        s.chipGhs[i] = NAN;
    }
    s.lastShareAgeS = -1.0f;
    s.sinceBlockS = -1.0f;
    s.govLastActionAgeS = -1.0f;
    s.netDiff = NAN;
    s.poolDiff = NAN;
    s.lastShareDiff = NAN;
    s.netHashrateEHs = NAN;
    s.oddsPerBlock = s.oddsPerDay = s.oddsPerYear = s.expectedYears = s.vsMega = NAN;
    s.asicCount = 4;
    s.halvingAt = 1050000;

    // efficiency sweep measured on this unit's real hardware on 2026-09-19
    static const float mhz[UI_SWEEP_POINTS] = {500, 575, 650, 725, 750, 800};
    static const float w[UI_SWEEP_POINTS] = {62.4f, 76.9f, 91.5f, 111.5f, 120.2f, 133.8f};
    for (int i = 0; i < UI_SWEEP_POINTS; i++) {
        s.sweepMhz[i] = mhz[i];
        s.sweepW[i] = w[i];
    }
}
