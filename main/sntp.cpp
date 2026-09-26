#include "esp_log.h"
#include "esp_sntp.h"
#include "global_state.h"
#include "nvs_config.h"
#include "sntp.h"


// ---- SNTP helpers ----
static const char *TAG_TIME = "time";

static bool synchronized = false;

static void time_sync_notification_cb(struct timeval *tv) {
    ESP_LOGI(TAG_TIME, "Time synchronized");
    synchronized = true;
}

SNTP::SNTP() {
    // NOP
}

bool SNTP::isTimeSynced() {
    return (synchronized && now() >= 1722927653);
}

void SNTP::start() {
    // Configure servers (multiple improve robustness)
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "de.pool.ntp.org");
    esp_sntp_setservername(1, "pool.ntp.org");

    // Resync interval (ms). Default ~3600000; 6h is fine for TOTP.
    sntp_set_sync_interval(6 * 60 * 60 * 1000UL);

    // Immediate time update is simplest for embedded
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);

    // Optional: notification callback
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);

    // Timezone: was hardcoded to Europe/Berlin (CET/CEST); this unit runs in
    // Brazil, so the default is now America/Fortaleza-ish (UTC-3, no DST
    // since 2019: POSIX "<-03>3"), and it is configurable via the NVS "tz"
    // key (see Config::getTz()/setTz() in nvs_config.h) so a PATCH
    // /api/system update can change it without a firmware rebuild.
    applyTimezoneFromNvs();

    // Start SNTP
    esp_sntp_init();

    ESP_LOGI(TAG_TIME, "SNTP started");
}

void SNTP::applyTimezoneFromNvs() {
    char *tz = Config::getTz();
    setenv("TZ", tz && tz[0] ? tz : "<-03>3", 1);
    tzset();
    ESP_LOGI(TAG_TIME, "timezone set to %s", tz && tz[0] ? tz : "<-03>3");
    free(tz);
}

bool SNTP::waitForInitialSync(int timeout_ms) {
    // Wait until time is set, but don't block forever
    const int step = 200; // ms
    int waited = 0;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && waited < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(step));
        waited += step;
    }
    time_t now = 0;
    time(&now);
    if (now < 1609459200) { // 2021-01-01 (coarse sanity)
        return false;
    }
    return true;
}

void SNTP::logLocalTime() {
    time_t now = 0; time(&now);
    struct tm tm_info; localtime_r(&now, &tm_info);
    char buf[64]; strftime(buf, sizeof(buf), "%F %T %Z", &tm_info);
    ESP_LOGI(TAG_TIME, "LocaL time: %s", buf);
}
