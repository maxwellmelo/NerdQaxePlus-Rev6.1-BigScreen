#pragma once

class SNTP {
  protected:
    bool waitForInitialSync(int timeout_ms);
  public:
    SNTP();

    void start();
    void logLocalTime();
    bool isTimeSynced();

    // Re-applies the TZ environment variable from NVS ("tz" key, see
    // nvs_config.h). Called once from start(); call again after a PATCH
    // /api/system updates "tz" so the change takes effect without a reboot.
    void applyTimezoneFromNvs();
};