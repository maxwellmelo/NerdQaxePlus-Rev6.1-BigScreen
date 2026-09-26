#pragma once

#include <pthread.h>
#include "boards/board.h"
#include "fan_controller.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

// Only forward declarations here: thermal_governor.h drags in ArduinoJson on
// the ESP target and this header is pulled in by global_state.h, i.e. by most
// of the firmware.
namespace gov {
class ThermalGovernor;
struct GovConfig;
struct GovDecision;
} // namespace gov


template <class T>
class LockGuard {
public:
    LockGuard(T& obj) : m_obj(obj) { m_obj.lock(); }
    ~LockGuard() { m_obj.unlock(); }
private:
    T& m_obj;
};

class PowerManagementTask {
  protected:
    pthread_mutex_t m_loop_mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t m_loop_cond = PTHREAD_COND_INITIALIZER;

    SemaphoreHandle_t m_mutex;
    TimerHandle_t m_timer;

    char m_logBuffer[256]{};
    float m_chipTempMax = 0;
    float m_vrTemp = 0;
    float m_vrTempInt = 0;
    float m_voltage = 0;
    float m_power = 0;
    float m_current = 0;
    bool m_shutdown = false;
    FanController m_fanController;
    Board* m_board = nullptr;

    // Raw buck telemetry of the last cycle. Until the governor existed these
    // values only went to the log; the control law and the direction-aware
    // sequencer both need them, so they are kept.
    float m_vin = 0;
    float m_iin = 0;
    float m_pin = 0;
    float m_vout = 0;
    float m_iout = 0;
    float m_pout = 0;

    // ---- thermal governor -------------------------------------------------
    // Allocated once when the task starts, never in the 2 s loop.
    gov::ThermalGovernor *m_governor = nullptr;
    gov::GovDecision *m_govLast = nullptr;
    int m_govMode = 0;             // 0 = off, 1 = active, 2 = shadow
    bool m_govForcedOff = false;   // crash-loop guard, RAM only
    bool m_govConfigured = false;
    float m_govLastActionS = 0;    // esp_timer seconds of the last non-HOLD action

    // ---- direction-aware setting application ------------------------------
    // The effective operating point lives in RAM only; the saved pair is the
    // ceiling. NVS is never written by the governor.
    float m_appliedFreq = 0;       // 0 = nothing applied yet
    int m_appliedVoltageMillis = 0;
    bool m_waitingVoutRamp = false;
    bool m_settingsBlockedLogged = false;

    void checkVrFrequencyChanged();
    void readAndPublishPowerTelemetry();
    void applyAsicSettings();
    void runGovernor();
    bool settingsChangeAllowed();
    void desiredOperatingPoint(float &freq, int &millis);
    void task();

    bool startTimer();
    void trigger();

    void logChipTemps();
    void requestChipTemps();

  public:
    PowerManagementTask();

    // synchronized rebooting to now mess up i2c comms
    void restart();

    static void taskWrapper(void *pvParameters);
    static void create_job_timer(TimerHandle_t xTimer);

    float getPower()
    {
        return m_power;
    };
    float getVoltage()
    {
        return m_voltage;
    };
    float getCurrent()
    {
        return m_current;
    };
    float getChipTempMax()
    {
        return m_chipTempMax;
    };
    float getVRTemp()
    {
        return m_vrTemp;
    };
    float getVRTempInt()
    {
        return m_vrTempInt;
    }

    // Raw buck telemetry of the last cycle (V/A/W), same fields already kept
    // in m_vin/m_iin/m_pin/m_vout/m_iout/m_pout for the governor and the
    // direction-aware sequencer. Added so the display data layer
    // (main/ui_data.cpp) does not need to duplicate this state; getPower(),
    // getVoltage() (mV-scaled) and getCurrent() (mA-scaled) above already
    // exposed vin/iin/pin in a different unit, these three fill the gap for
    // vout/iout/pout.
    float getVin()
    {
        return m_vin;
    }
    float getIin()
    {
        return m_iin;
    }
    float getVout()
    {
        return m_vout;
    }
    float getIout()
    {
        return m_iout;
    }
    float getPout()
    {
        return m_pout;
    }

    uint16_t getFanRPM(int channel);

    uint16_t getFanPerc(int ch = 0)
    {
        return m_fanController.getSpeedPerc(ch);
    };

    FanController& getFanController()
    {
        return m_fanController;
    }

    void lock() {
        xSemaphoreTakeRecursive(m_mutex, portMAX_DELAY);
    }

    void unlock() {
        xSemaphoreGiveRecursive(m_mutex);
    }

    void shutdown();

    bool isShutdown() {
        return m_shutdown;
    }

    // ---- governor, called from the HTTP task ------------------------------

    // Rebuilds the governor configuration from NVS. Safe to call at any time.
    void reloadGovernorConfig();

    // Disables the governor for this session only (crash-loop guard). Never
    // touches NVS.
    void forceGovernorOff();

    bool isGovernorForcedOff() {
        return m_govForcedOff;
    }

    // Effective operating point (RAM only). With the governor off these equal
    // the saved ceiling.
    float getEffectiveFrequency();
    int getEffectiveVoltageMillis();
    int getGovernorMode();

    // For the HTTP handler: read these only while holding lock().
    gov::ThermalGovernor *getGovernor() {
        return m_governor;
    }
    const gov::GovDecision *getGovernorDecision() {
        return m_govLast;
    }
    // Seconds since the last non-HOLD governor action (-1 if there was none).
    float getGovernorLastActionAgeS();

    // Builds a GovConfig from NVS + board limits. Static so main.cpp can ask
    // for the startup operating point before the task is running.
    static bool loadGovernorConfig(Board *board, gov::GovConfig &cfg, int &mode);

    // Startup operating point when the governor is active (mode 1). Returns
    // false when the governor is off or shadowing, in which case the board must
    // start at the saved pair as before.
    static bool governorStartupPoint(Board *board, int &freqMhz, int &voltageMillis);
};
