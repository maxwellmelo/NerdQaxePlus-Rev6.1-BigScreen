#include <algorithm>
#include <math.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "mining.h"
#include "periodic.hpp"

#include "boards/board.h"
#include "fan_controller.h"
#include "global_state.h"
#include "hashrate_monitor_task.h"
#include "influx_task.h"
#include "nvs_config.h"
#include "serial.h"
#include "thermal_governor.h"
#include "ui_data.h"

#define POLL_RATE 2000

// The ASIC ramp blocks the loop for ~100 ms per 6.25 MHz step, so a single
// governor action is capped at 8 steps (= 50 MHz, ~800 ms).
#define GOV_MAX_STEPS_PER_ACTION 8
#define GOV_STEP_MHZ 6.25f

// A frequency increase is only applied once the buck has really reached the
// commanded voltage.
#define GOV_VOUT_CONFIRM_MV 30.0f

static const char *TAG = "power_management";
static const char *GOV_TAG = "governor";

// #define MEASURE_LOOP_TIME

PowerManagementTask::PowerManagementTask()
{
    m_mutex = xSemaphoreCreateRecursiveMutex();
}

void PowerManagementTask::taskWrapper(void *pvParameters)
{
    PowerManagementTask *powerManagementTask = (PowerManagementTask *) pvParameters;
    powerManagementTask->task();
}

void PowerManagementTask::restart()
{
    ESP_LOGW(TAG, "Shutdown requested ...");
    // stops the main task
    lock();

    ESP_LOGW(TAG, "HW lock acquired!");
    // shutdown asics and LDOs before reset
    shutdown();

    ESP_LOGW(TAG, "restart");
    esp_restart();

    // unreachable
    unlock();
}

void PowerManagementTask::shutdown()
{
    lock();
    if (m_board) {
        m_shutdown = true;
        m_board->shutdown();
    }
    unlock();
}

uint16_t PowerManagementTask::getFanRPM(int channel)
{
    return m_fanController.getRPM(channel);
}

void PowerManagementTask::checkVrFrequencyChanged()
{
    static uint32_t lastVrFrequency = 0;

    uint32_t vrFrequency = m_board->getVrFrequency();
    if (vrFrequency != lastVrFrequency) {
        m_board->setVrFrequency(vrFrequency);
        ESP_LOGI(TAG, "setting version rolling frequency to %luHz", vrFrequency);
        lastVrFrequency = vrFrequency;
    }
}


void PowerManagementTask::logChipTemps()
{
    size_t offset = 0;

    // no chip temp to report
    if (m_board->getMaxChipTemp() == 0.0f) {
        return;
    }

    // Iterate through each ASIC and append its count to the log message
    for (int i = 0; i < m_board->getAsicCount(); i++) {
        offset += snprintf(m_logBuffer + offset, sizeof(m_logBuffer) - offset, "%.2f°C / ", m_board->getChipTemp(i));
    }
    if (offset >= 2) {
        m_logBuffer[offset - 2] = 0; // remove trailing slash
    }

    ESP_LOGI(TAG, "chip temperatures: %s", m_logBuffer);
}

void PowerManagementTask::create_job_timer(TimerHandle_t xTimer)
{
    // Retrieve 'this' pointer from timer ID
    PowerManagementTask *task = (PowerManagementTask *) pvTimerGetTimerID(xTimer);
    if (!task) {
        return;
    }
    task->trigger();
}

void PowerManagementTask::trigger()
{
    pthread_mutex_lock(&m_loop_mutex);
    pthread_cond_signal(&m_loop_cond);
    pthread_mutex_unlock(&m_loop_mutex);
}

bool PowerManagementTask::startTimer()
{
    // Create the timer
    m_timer = xTimerCreate(TAG, pdMS_TO_TICKS(POLL_RATE), pdTRUE, (void *) this, create_job_timer);

    if (m_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create timer");
        return false;
    }

    // Start the timer
    if (xTimerStart(m_timer, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start timer");
        return false;
    }
    return true;
}

void PowerManagementTask::readAndPublishPowerTelemetry()
{
    if (!m_board->isBuckInitialized()) {
        return;
    }

    static Periodic every_15s(sec_to_us(15), /*start_immediately=*/true);

    // request buck telemetry
    if (every_15s.due()) {
        m_board->requestBuckTelemtry();
    }

    float vin = m_board->getVin();
    float iin = m_board->getIin();
    float pin = m_board->getPin();
    float pout = m_board->getPout();
    float vout = m_board->getVout();
    float iout = m_board->getIout();

    m_vrTemp = m_board->getVRTemp();
    m_vrTempInt = m_board->getVRTempInt();

    // keep the raw readings: the governor and the direction-aware sequencer
    // both consume them, no extra I2C traffic
    m_vin = vin;
    m_iin = iin;
    m_pin = pin;
    m_vout = vout;
    m_iout = iout;
    m_pout = pout;

    ESP_LOGI(TAG, "vin: %.2f, iin: %.2f, pin: %.2f, vout: %.2f, iout: %.2f, pout: %.2f, vr-temp: %.2f, vr-temp-int: %.2f", vin, iin,
             pin, vout, iout, pout, m_vrTemp, m_vrTempInt);

    influx_task_set_pwr(vin, iin, pin, vout, iout, pout);

    // currently only implemented for boards with TPS536x7
    uint32_t status = 0;
    Board::Error error = m_board->getFault(&status);
    if (error != Board::Error::NONE) {
        SYSTEM_MODULE.setBoardError(error, status);
        m_board->setVoltage(0.0);
    }

    m_voltage = vin * 1000.0;
    m_current = iin * 1000.0;
    m_power = pin;
}

// Nothing may touch the buck or the PLL while the ASICs are down, while a
// board error is latched or while an OTA runs (OTA calls shutdown() and holds
// our lock). The existing overheat handler stays untouched as the last barrier;
// this gate only makes sure we never re-enable the buck behind its back.
bool PowerManagementTask::settingsChangeAllowed()
{
    // not available when asics are shutdown (this also covers a running OTA)
    if (m_shutdown) {
        return false;
    }

    // don't change frequency or voltage if asics haven't been initialized
    if (!m_board->isInitialized()) {
        return false;
    }

    if (m_board->isShutdown()) {
        return false;
    }

    Board::Error err = SYSTEM_MODULE.getBoardError();
    if (err != Board::Error::NONE) {
        if (!m_settingsBlockedLogged) {
            ESP_LOGW(TAG, "board error latched (%s) - frequency/voltage changes are blocked", Board::errorToStr(err));
            m_settingsBlockedLogged = true;
        }
        return false;
    }
    m_settingsBlockedLogged = false;
    return true;
}

// The operating point we want to be at right now. With the governor off (or
// forced off, or still shadowing) this is exactly the saved pair, i.e. the
// upstream behaviour.
void PowerManagementTask::desiredOperatingPoint(float &freq, int &millis)
{
    bool governorActive = (m_govMode == 1) && !m_govForcedOff && m_govConfigured && m_governor && m_govLast;
    bool fromGovernor = governorActive && (m_govLast->freq > 0.0f);

    // Governor on but no decision yet (first cycle after boot or after being
    // enabled): hold what is applied. Falling through to the saved pair here
    // would jump towards the ceiling for one cycle.
    if (governorActive && !fromGovernor && m_appliedFreq > 0.0f) {
        freq = m_appliedFreq;
        millis = m_appliedVoltageMillis;
        return;
    }

    if (!fromGovernor) {
        freq = (float) m_board->getAsicFrequency();
        millis = m_board->getAsicVoltageMillis();
        return;
    }

    freq = m_govLast->freq;

    // One action is at most 8 steps of 6.25 MHz; the ASIC ramp blocks this task
    // 100 ms per step. Manual API changes are deliberately NOT clamped here.
    float maxDelta = (float) GOV_MAX_STEPS_PER_ACTION * GOV_STEP_MHZ;
    if (freq > m_appliedFreq + maxDelta) {
        freq = m_appliedFreq + maxDelta;
    } else if (freq < m_appliedFreq - maxDelta) {
        freq = m_appliedFreq - maxDelta;
    }

    // keep the voltage on the curve for the frequency we are really applying
    millis = m_governor->mvFor(freq);
}

// Direction aware, one transition per cycle:
//   going up   -> voltage first, frequency only after vout confirmed the ramp
//   going down -> frequency first, voltage on the next cycle
// The previous implementation always did voltage then frequency in the same
// cycle, which undervolts the ASICs on the way down.
void PowerManagementTask::applyAsicSettings()
{
    if (!settingsChangeAllowed()) {
        return;
    }

    // seed from what initAsics() actually applied
    if (m_appliedFreq <= 0.0f) {
        m_appliedFreq = (float) m_board->getInitFrequency();
        m_appliedVoltageMillis = m_board->getInitVoltageMillis();
        ESP_LOGI(TAG, "operating point seeded at %.2fMHz / %dmV", m_appliedFreq, m_appliedVoltageMillis);
    }

    float desiredFreq = 0.0f;
    int desiredMv = 0;
    desiredOperatingPoint(desiredFreq, desiredMv);

    bool freqDiff = fabsf(desiredFreq - m_appliedFreq) > 0.01f;
    bool mvDiff = (desiredMv != m_appliedVoltageMillis);

    if (freqDiff || mvDiff) {
        bool goingUp = (desiredFreq > m_appliedFreq + 0.01f) || (!freqDiff && desiredMv > m_appliedVoltageMillis);

        if (goingUp) {
            if (mvDiff) {
                ESP_LOGI(TAG, "up: vcore %d -> %dmV first", m_appliedVoltageMillis, desiredMv);
                if (m_board->setVoltage((float) desiredMv / 1000.0f)) {
                    m_appliedVoltageMillis = desiredMv;
                    m_waitingVoutRamp = freqDiff;
                } else {
                    ESP_LOGE(TAG, "setting vcore to %dmV was rejected", desiredMv);
                }
            } else {
                // voltage already in place: only raise the frequency once the
                // buck really delivers it
                float minVout = ((float) m_appliedVoltageMillis - GOV_VOUT_CONFIRM_MV) / 1000.0f;
                if (m_vout >= minVout) {
                    m_waitingVoutRamp = false;
                    ESP_LOGI(TAG, "up: asic frequency %.2f -> %.2fMHz (vout %.3fV)", m_appliedFreq, desiredFreq, m_vout);
                    if (m_board->setAsicFrequency(desiredFreq)) {
                        m_appliedFreq = desiredFreq;
                    } else {
                        ESP_LOGE(TAG, "pll setting not found for %.2fMHz", desiredFreq);
                        m_appliedFreq = desiredFreq; // don't retry forever
                    }
                } else if (!m_waitingVoutRamp) {
                    m_waitingVoutRamp = true;
                    ESP_LOGW(TAG, "waiting for vout %.3fV to reach %.3fV before raising the frequency", m_vout, minVout);
                }
            }
        } else {
            if (freqDiff) {
                ESP_LOGI(TAG, "down: asic frequency %.2f -> %.2fMHz first", m_appliedFreq, desiredFreq);
                if (m_board->setAsicFrequency(desiredFreq)) {
                    m_appliedFreq = desiredFreq;
                } else {
                    ESP_LOGE(TAG, "pll setting not found for %.2fMHz", desiredFreq);
                    m_appliedFreq = desiredFreq;
                }
            } else {
                ESP_LOGI(TAG, "down: vcore %d -> %dmV", m_appliedVoltageMillis, desiredMv);
                if (m_board->setVoltage((float) desiredMv / 1000.0f)) {
                    m_appliedVoltageMillis = desiredMv;
                } else {
                    ESP_LOGE(TAG, "setting vcore to %dmV was rejected", desiredMv);
                }
            }
        }
    }

    // check if version rolling frequency changed
    checkVrFrequencyChanged();
}

// ---------------------------------------------------------------------------
// Thermal governor
// ---------------------------------------------------------------------------

bool PowerManagementTask::loadGovernorConfig(Board *board, gov::GovConfig &cfg, int &mode)
{
    cfg.setDefaults();

    mode = (int) Config::getGovEnable();
    if (mode < 0 || mode > 2) {
        mode = 0;
    }

    // the saved pair is the ceiling; the governor never writes it back
    cfg.freqCap = (float) board->getAsicFrequency();
    cfg.voltageCap = board->getAsicVoltageMillis();

    cfg.fmin = (float) Config::getGovFmin();
    cfg.vmin = (int) Config::getGovVmin();
    cfg.vrTarget = (float) Config::getGovVrTarget();
    cfg.vriTarget = (float) Config::getGovVriTarget();
    cfg.ioutMax = (float) Config::getGovIoutMax();
    cfg.vinMin = (float) Config::getGovVinMinMv() / 1000.0f;
    cfg.pinMax = (float) Config::getGovPinMax();
    cfg.iinMax = (float) Config::getGovIinMaxDA() / 10.0f;
    cfg.vsagMv = (float) Config::getGovVsagMv();
    cfg.hrMin = (float) Config::getGovHrMinPerc() / 100.0f;
    cfg.shadow = (mode == 2);

    // Never aim above the firmware's own VR cut-off: fan channel 1 compares
    // vrTemp against fan1_overheat and kills the buck there.
    uint16_t fan1Overheat = Config::getFanOverheatTemp(1);
    if (fan1Overheat > 5 && cfg.vrTarget > (float) fan1Overheat - 5.0f) {
        cfg.vrTarget = (float) fan1Overheat - 5.0f;
    }
    // hard ceilings: VR = target + 4, VRint = 94
    cfg.vriHardDelta = 94.0f - cfg.vriTarget;
    if (cfg.vriHardDelta < 0.0f) {
        cfg.vriHardDelta = 0.0f;
    }

    cfg.asicCount = board->getAsicCount();
    if (board->getAsics()) {
        cfg.smallCores = (int) board->getAsics()->getSmallCoreCount();
    }

    char *curve = Config::getGovCurve();
    if (curve) {
        snprintf(cfg.curve, sizeof(cfg.curve), "%s", curve);
        free(curve);
    }

    // sanity: the ceilings must be able to win against the floors
    if (cfg.freqCap < cfg.fmin || cfg.voltageCap < cfg.vmin) {
        return false;
    }
    return true;
}

bool PowerManagementTask::governorStartupPoint(Board *board, int &freqMhz, int &voltageMillis)
{
    gov::GovConfig cfg;
    int mode = 0;
    if (!loadGovernorConfig(board, cfg, mode) || mode != 1) {
        return false;
    }

    // This runs on the main task, whose stack is only 3.5 KB. A ThermalGovernor
    // carries > 2 KB of filter history, and building one here overflowed the
    // stack and panicked on every cold boot with the governor enabled (the
    // crash-loop guard then forced it off). Only the curve is needed, so use
    // the free helpers; same result as ThermalGovernor::mvFor().
    float f[GOV_CURVE_MAX_POINTS];
    float mv[GOV_CURVE_MAX_POINTS];
    int n = 0;
    if (!gov::parseCurve(cfg.curve, f, mv, GOV_CURVE_MAX_POINTS, &n)) {
        return false;
    }
    int startMv = gov::roundUp5mv(gov::curveMv(f, mv, n, cfg.fmin));
    if (startMv < cfg.vmin) {
        startMv = cfg.vmin;
    }
    if (startMv > cfg.voltageCap) {
        startMv = cfg.voltageCap;
    }
    freqMhz = (int) cfg.fmin;
    voltageMillis = startMv;
    return true;
}

void PowerManagementTask::reloadGovernorConfig()
{
    LockGuard g(*this);

    int prevMode = m_govConfigured ? m_govMode : 0;

    if (!m_governor) {
        m_governor = new gov::ThermalGovernor();
        m_govLast = new gov::GovDecision();
    }

    Board *board = m_board ? m_board : SYSTEM_MODULE.getBoard();
    if (!board) {
        ESP_LOGE(GOV_TAG, "no board yet - governor stays disabled");
        m_govMode = 0;
        m_govConfigured = false;
        return;
    }

    gov::GovConfig cfg;
    int mode = 0;
    if (!loadGovernorConfig(board, cfg, mode)) {
        ESP_LOGE(GOV_TAG, "invalid configuration - governor disabled");
        m_govMode = 0;
        m_govConfigured = false;
        return;
    }

    if (!m_governor->configure(cfg)) {
        ESP_LOGE(GOV_TAG, "configuration rejected (curve/caps) - governor disabled");
        m_govMode = 0;
        m_govConfigured = false;
        return;
    }

    // Switched on while the ASICs are already hashing: continue from the
    // operating point really applied instead of dropping to fmin. On a cold
    // boot nothing is applied yet, so the fmin start is kept.
    if (mode == 1 && prevMode != 1 && m_appliedFreq > 0.0f) {
        m_governor->seedTarget(m_appliedFreq);
        *m_govLast = gov::GovDecision(); // drop any stale (shadow) decision
        ESP_LOGI(GOV_TAG, "enabled at runtime - starting from the applied %.2fMHz", m_appliedFreq);
    }

    m_govMode = mode;
    m_govConfigured = true;

    // Governor diary (screen 18): only the on/off transition of the
    // *active* mode is worth a line - flipping in/out of shadow mode never
    // touches the hardware.
    if ((prevMode == 1) != (m_govMode == 1)) {
        uiDataDiaryPush(UI_DIARY_GOV, (m_govMode == 1) ? "Governor on" : "Governor off");
    }

    ESP_LOGI(GOV_TAG, "mode=%d cap=%.0fMHz/%dmV fmin=%.0f vrTarget=%.1f vriTarget=%.1f ioutMax=%.0f vinMin=%.2f "
                      "pinMax=%.0f iinMax=%.1f curve=%s",
             m_govMode, cfg.freqCap, cfg.voltageCap, cfg.fmin, cfg.vrTarget, cfg.vriTarget, cfg.ioutMax, cfg.vinMin,
             cfg.pinMax, cfg.iinMax, cfg.curve);
}

void PowerManagementTask::forceGovernorOff()
{
    m_govForcedOff = true;
    ESP_LOGW(GOV_TAG, "governor forced off for this session (crash-loop guard)");
}

// Builds the inputs from the readings already taken this cycle (no extra I2C)
// and runs the control law. In shadow mode (2) the decision is computed and
// reported but never applied.
void PowerManagementTask::runGovernor()
{
    if (!m_governor || !m_govConfigured || m_govMode == 0 || m_govForcedOff) {
        return;
    }
    if (!m_board->isBuckInitialized()) {
        return;
    }

    gov::GovInputs in;
    float now = (float) (esp_timer_get_time() / 1000000.0);

    if (m_vrTemp > 0.0f) {
        in.vrTemp.set(m_vrTemp);
    }
    if (m_vrTempInt > 0.0f) {
        in.vrTempInt.set(m_vrTempInt);
    }
    if (m_pin > 0.0f) {
        in.pin.set(m_pin);
    }
    if (m_iin > 0.0f) {
        in.iin.set(m_iin);
    }
    if (m_vin > 0.0f) {
        in.vin.set(m_vin);
    }
    if (m_iout > 0.0f) {
        in.iout.set(m_iout);
    }
    in.voutMv.set(m_vout * 1000.0f);

    float hashrate = HASHRATE_MONITOR.getSmoothedTotalChipHashrate();
    if (hashrate > 0.0f) {
        in.hashrate.set(hashrate);
    }

    if (m_appliedFreq > 0.0f) {
        in.appliedFreq.set(m_appliedFreq);
        in.appliedMv.set((float) m_appliedVoltageMillis);
    }

    // Never actuate blindly: a latched board error, a shutdown or an
    // un-initialized ASIC freezes the control law too.
    in.frozen = !settingsChangeAllowed();
    in.nowS = now;

    gov::GovDecision d = m_governor->update(in);

    // INFO only on actions, otherwise the 2 s loop would spam the log
    if (d.action != gov::ACT_HOLD) {
        m_govLastActionS = now;
        ESP_LOGI(GOV_TAG, "%s %s -> %.2fMHz/%dmV limiter=%s margin=%.2f (%s)%s", gov::actionToStr(d.action),
                 gov::stateToStr(d.state), d.freq, d.mv, gov::limiterToStr(d.limiter), d.margin, d.reason,
                 (m_govMode == 2) ? " [shadow]" : "");
    } else if (d.state != m_govLast->state) {
        ESP_LOGI(GOV_TAG, "state %s -> %s (%s)", gov::stateToStr(m_govLast->state), gov::stateToStr(d.state), d.reason);
    }

    *m_govLast = d;

    // Governor diary (screen 18, main/displays): a short pt-BR line per
    // real action. Shadow mode never touches the hardware, so it is not
    // logged here (it would just confuse the "what actually happened"
    // diary). No allocation, fixed small buffer, matches the "no malloc/new
    // in the 2s path" rule.
    if (d.action != gov::ACT_HOLD && m_govMode == 1) {
        const char *limiterPt = "regulator";
        switch (d.limiter) {
        case gov::LIM_VR: limiterPt = "regulator"; break;
        case gov::LIM_VRI: limiterPt = "regulator (internal)"; break;
        case gov::LIM_PIN: limiterPt = "input power"; break;
        case gov::LIM_IIN: limiterPt = "input current"; break;
        case gov::LIM_IOUT: limiterPt = "output current"; break;
        case gov::LIM_VIN: limiterPt = "input voltage"; break;
        default: limiterPt = "limit"; break;
        }
        char diaryText[96];
        switch (d.action) {
        case gov::ACT_UP:
            snprintf(diaryText, sizeof(diaryText), "Ramped up to %.0f MHz", d.freq);
            break;
        case gov::ACT_DOWN:
            snprintf(diaryText, sizeof(diaryText), "Backed off to %.0f MHz (%s above target)", d.freq, limiterPt);
            break;
        case gov::ACT_EMERGENCY_DOWN:
            snprintf(diaryText, sizeof(diaryText), "Emergency: dropped to %.0f MHz (regulator at %.0f °C)", d.freq, m_vrTemp);
            break;
        case gov::ACT_VTRIM:
            snprintf(diaryText, sizeof(diaryText), "Fine voltage trim (%d mV)", d.vTrim);
            break;
        default:
            diaryText[0] = 0;
            break;
        }
        if (diaryText[0]) {
            uiDataDiaryPush(UI_DIARY_GOV, diaryText);
        }
    }
}

float PowerManagementTask::getEffectiveFrequency()
{
    return (m_appliedFreq > 0.0f) ? m_appliedFreq : (float) m_board->getAsicFrequency();
}

int PowerManagementTask::getEffectiveVoltageMillis()
{
    return (m_appliedVoltageMillis > 0) ? m_appliedVoltageMillis : m_board->getAsicVoltageMillis();
}

int PowerManagementTask::getGovernorMode()
{
    if (m_govForcedOff || !m_govConfigured) {
        return 0;
    }
    return m_govMode;
}

float PowerManagementTask::getGovernorLastActionAgeS()
{
    if (m_govLastActionS <= 0.0f) {
        return -1.0f;
    }
    return (float) (esp_timer_get_time() / 1000000.0) - m_govLastActionS;
}

void PowerManagementTask::requestChipTemps()
{
    // temperature measurements don't work before ASICs
    // are initialized
    if (!m_board->isInitialized()) {
        return;
    }

    m_board->requestChipTemps();
}

void PowerManagementTask::task()
{
    m_board = SYSTEM_MODULE.getBoard();

    // use manual invert polarity setting
    bool invert = m_board->isInvertFanPolarityEnabled();

    m_board->setFanPolarity(invert);

    m_fanController.init(m_board, POLL_RATE);

    // one-time allocation, before the loop starts
    reloadGovernorConfig();

    vTaskDelay(pdMS_TO_TICKS(1000));
    startTimer();

    uint64_t last_time = esp_timer_get_time();
    while (1) {
        pthread_mutex_lock(&m_loop_mutex);
        pthread_cond_wait(&m_loop_cond, &m_loop_mutex); // Wait for the timer
        pthread_mutex_unlock(&m_loop_mutex);

        uint64_t start = esp_timer_get_time();
        lock();

        applyAsicSettings();

        // request chip temps
        requestChipTemps();

        logChipTemps();

        readAndPublishPowerTelemetry();

        // Display data layer (main/ui_data.cpp): history/energy/ambient/
        // shares/block bookkeeping, right after fresh telemetry and before
        // the governor runs (uiDataTick() does not touch the governor).
        uiDataTick();

        // Run the control law on the readings of this cycle. The decision is
        // applied by applyAsicSettings() at the top of the next cycle, which
        // keeps the loop shape identical to upstream; 2 s of latency is
        // irrelevant against a 100 s thermal constant.
        runGovernor();

        // collect temperatures
        // get the max of all asic measuring temp sensors
        float tmp1075Max = 0.0f;
        for (int i = 0; i < m_board->getNumTempSensors(); i++) {
            float tmp = m_board->getTemperature(i);
            if (tmp) {
                ESP_LOGI(TAG, "Temperature %d: %.2f C", i, tmp);
            }
            tmp1075Max = std::max(tmp1075Max, tmp);
        }

        // get max temp of all chips
        // returns 0 if not available on the hardware
        float intChipTempMax = m_board->getMaxChipTemp();

#ifdef NERDQAXEPLUS
        // NQ+ needs special care - the reading of chip internal temp sensors is way
        // too slow for the PID, so we need to stay compatible.
        // we use the max temp of board temp sensors and ASICs
        // note: m_chipTempMax is not mutexed, single assignment required
        m_chipTempMax = std::max(tmp1075Max, intChipTempMax);
#else
        // on other devices that have the TMUX like the QX we only use
        // the chip temps for the PID
        // note: m_chipTempMax is not mutexed, single assignment required
        m_chipTempMax = intChipTempMax ? intChipTempMax : tmp1075Max;
#endif

        influx_task_set_temperature(m_chipTempMax, m_vrTemp);

        // Run fan controller (reads RPM, drives fans, updates overheat flags)
        m_fanController.update(m_chipTempMax, m_vrTemp);

        // Shutdown if any fan channel reports overheat
        if (m_fanController.isOverheated(0) || m_fanController.isOverheated(1)) {
            uint32_t status = ((uint32_t) m_chipTempMax << 24) | ((uint32_t) m_fanController.getOverheatTemp(0) << 16) |
                              ((uint32_t) m_vrTemp << 8) | ((uint32_t) m_fanController.getOverheatTemp(1));

            // over temperature — ASIC takes priority over VReg-only
            Board::Error overheatErr = Board::Error::VREG_TEMP_FAULT;
            if (m_fanController.isOverheated(0)) overheatErr = Board::Error::TEMP_FAULT;
            SYSTEM_MODULE.setBoardError(overheatErr, status);

            // disables the buck
            m_board->setVoltage(0.0);
            ESP_LOGE(TAG, "System overheated (chip=%.1f°C/thresh=%d°C vr=%.2f°C/thresh=%d°C) - Shutting down asic voltage",
                     m_chipTempMax, m_fanController.getOverheatTemp(0), m_vrTemp, m_fanController.getOverheatTemp(1));
        }
        influx_set_fan(m_fanController.getSpeedPerc(0), (float) m_fanController.getRPM(0), m_fanController.getSpeedPerc(1),
                       (float) m_fanController.getRPM(1));
        unlock();
#ifdef MEASURE_LOOP_TIME
        // checks if loop takes too much time
        uint64_t end = esp_timer_get_time();
        uint64_t duration = (end - start) / 1000llu;
        uint64_t interval = (start - last_time) / 1000llu;
        if (duration > POLL_RATE) {
            ESP_LOGE(TAG, "loop taking more then %dms (%llums, interval: %llu)", POLL_RATE, duration, interval);
        }
        last_time = start;
#endif
    }
}
