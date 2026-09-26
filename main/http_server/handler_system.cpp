#include "esp_ota_ops.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "ArduinoJson.h"

#include "psram_allocator.h"
#include "global_state.h"
#include "main.h"
#include "nvs_config.h"
#include "sntp.h"
#include "thermal_governor.h"
#include "ui_data.h"
#include "http_cors.h"
#include "http_utils.h"
#include "screens/screen.h"
#include "screens/screen_registry.h"

#include "ping_task.h"

static const char *TAG = "http_system";

#define VR_FREQUENCY_ENABLED

uint64_t getDuplicateHWNonces();

// Defined in main.cpp; global_state.h does not declare it extern and this
// handler is not the place to add that, so it is declared locally here,
// same as getDuplicateHWNonces() right above.
extern SNTP sntp;

/* Simple handler for getting system handler */
esp_err_t GET_system_info(httpd_req_t *req)
{
    // close connection when out of scope
    ConGuard g(http_server, req);

    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    httpd_resp_set_type(req, "application/json");

    // Set CORS headers
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Parse optional start_timestamp parameter
    const uint64_t DEFAULT_HISTORY_SPAN_MS = 3600ULL * 1000ULL;
    const uint64_t MAX_HISTORY_SPAN_MS = 3ULL * 3600ULL * 1000ULL;

    uint64_t start_timestamp = 0;
    uint64_t current_timestamp = 0;
    uint32_t history_limit = 0;
    bool history_requested = false;
    uint64_t history_span_ms = DEFAULT_HISTORY_SPAN_MS;
    char query_str[128];
    if (httpd_req_get_url_query_str(req, query_str, sizeof(query_str)) == ESP_OK) {
        char param[64];
        if (httpd_query_key_value(query_str, "ts", param, sizeof(param)) == ESP_OK) {
            start_timestamp = strtoull(param, NULL, 10);
            if (start_timestamp) {
                history_requested = true;
            }
        }
        if (httpd_query_key_value(query_str, "limit", param, sizeof(param)) == ESP_OK) {
            history_limit = strtoul(param, NULL, 10);
            if (history_limit > 1000) {
                history_limit = 1000;
            }
        }
        if (httpd_query_key_value(query_str, "history_span", param, sizeof(param)) == ESP_OK) {
            history_span_ms = strtoull(param, NULL, 10);
            if (history_span_ms > MAX_HISTORY_SPAN_MS) {
                history_span_ms = MAX_HISTORY_SPAN_MS;
            }
            if (history_span_ms == 0) {
                history_span_ms = DEFAULT_HISTORY_SPAN_MS;
            }
        }
        if (httpd_query_key_value(query_str, "cur", param, sizeof(param)) == ESP_OK) {
            current_timestamp = strtoull(param, NULL, 10);
            ESP_LOGI(TAG, "cur: %llu", current_timestamp);
        }
    }

    Board* board   = SYSTEM_MODULE.getBoard();
    History* history = SYSTEM_MODULE.getHistory();

    PSRAMAllocator allocator;
    JsonDocument doc(&allocator);

    bool shutdown = POWER_MANAGEMENT_MODULE.isShutdown();

    // Get configuration strings from NVS
    char *ssid               = Config::getWifiSSID();
    char *hostname           = Config::getHostname();
    char *stratumURL         = Config::getStratumURL();
    char *stratumUser        = Config::getStratumUser();
    char *fallbackStratumURL = Config::getStratumFallbackURL();
    char *fallbackStratumUser= Config::getStratumFallbackUser();

    // static
    doc["asicCount"]          = board->getAsicCount();
    doc["smallCoreCount"]     = (board->getAsics()) ? board->getAsics()->getSmallCoreCount() : 0;
    doc["deviceModel"]        = board->getDeviceModel();
    doc["hostip"]             = SYSTEM_MODULE.getIPAddress();
    doc["macAddr"]            = SYSTEM_MODULE.getMacAddress();
    doc["wifiRSSI"]           = SYSTEM_MODULE.get_wifi_rssi();

    // dashboard
    doc["power"]              = POWER_MANAGEMENT_MODULE.getPower();
    doc["maxPower"]           = board->getMaxPin();
    doc["minPower"]           = board->getMinPin();
    doc["maxVoltage"]         = board->getMaxVin();
    doc["minVoltage"]         = board->getMinVin();
    doc["current"]            = POWER_MANAGEMENT_MODULE.getCurrent();           // mA (raw)
    doc["currentA"]           = POWER_MANAGEMENT_MODULE.getCurrent() / 1000.0f; // A (UI)
    doc["minCurrentA"]        = board->getMinCurrentA(); // A
    doc["maxCurrentA"]        = board->getMaxCurrentA(); // A
    doc["temp"]               = POWER_MANAGEMENT_MODULE.getChipTempMax();
    doc["vrTemp"]             = POWER_MANAGEMENT_MODULE.getVRTemp();
    doc["vrTempInt"]          = POWER_MANAGEMENT_MODULE.getVRTempInt();
    doc["hashRateTimestamp"]  = history->getCurrentTimestamp();
    // set hashrate values to 0 in shutdown
    doc["hashRate"]           = !shutdown ? SYSTEM_MODULE.getCurrentHashrate() : 0.0;
    doc["hashRate_1m"]        = !shutdown ? history->getCurrentHashrate1m()    : 0.0;
    doc["hashRate_10m"]       = !shutdown ? history->getCurrentHashrate10m()   : 0.0;
    doc["hashRate_1h"]        = !shutdown ? history->getCurrentHashrate1h()    : 0.0;
    doc["hashRate_1d"]        = !shutdown ? history->getCurrentHashrate1d()    : 0.0;
    doc["coreVoltage"]        = board->getAsicVoltageMillis();
    doc["defaultCoreVoltage"] = board->getDefaultAsicVoltageMillis();
    doc["coreVoltageActual"]  = (int) (board->getVout() * 1000.0f);
    doc["fanspeed"]           = POWER_MANAGEMENT_MODULE.getFanPerc();
    doc["manualFanSpeed"]     = Config::getFanSpeed();
    doc["fanrpm"]             = POWER_MANAGEMENT_MODULE.getFanRPM(0);
    doc["fanrpm2"]            = (board->getNumFans() > 1) ? POWER_MANAGEMENT_MODULE.getFanRPM(1) : 0;
    doc["fanspeed2"]          = (board->getNumFans() > 1) ? POWER_MANAGEMENT_MODULE.getFanPerc(1) : 0;
    doc["fanCount"]           = board->getNumFans();


    doc["lastpingrtt"]        = get_last_ping_rtt();
    doc["recentpingloss"]     = get_recent_ping_loss();
    doc["shutdown"]           = POWER_MANAGEMENT_MODULE.isShutdown();
    doc["duplicateHWNonces"]  = getDuplicateHWNonces();

    JsonObject stratum_obj = doc["stratum"].to<JsonObject>();

    // kept for swarm compatibility
    doc["poolDifficulty"]     = STRATUM_MANAGER->getPoolDifficulty();
    doc["networkDifficulty"]  = STRATUM_MANAGER->getNetworkDifficulty();
    doc["foundBlocks"]        = STRATUM_MANAGER->getFoundBlocks();
    doc["totalFoundBlocks"]   = STRATUM_MANAGER->getTotalFoundBlocks();
    doc["sharesAccepted"]     = STRATUM_MANAGER->getSharesAccepted();
    doc["sharesRejected"]     = STRATUM_MANAGER->getSharesRejected();
    doc["bestDiff"]           = STRATUM_MANAGER->getBestDiff();
    doc["bestSessionDiff"]    = STRATUM_MANAGER->getBestSessionDiff();

    STRATUM_MANAGER->getManagerInfoJson(stratum_obj);

    // asic temps
    {
        JsonArray arr = doc["asicTemps"].to<JsonArray>();
        for (int i=0;i<board->getAsicCount();i++) {
            arr.add(board->getChipTemp(i));
        }
    }

    // If history was requested, add the history data as a nested object
    if (!shutdown && history_requested) {
        uint64_t span = history_span_ms;
        uint64_t end_timestamp = start_timestamp + span;
        JsonObject json_history = doc["history"].to<JsonObject>();

        History *history = SYSTEM_MODULE.getHistory();
        history->exportHistoryData(json_history, start_timestamp, end_timestamp, current_timestamp, history_limit);
    }

    // settings
    PidSettings *pid = board->getPidSettings();
    doc["pidTargetTemp"]      = board->isPIDAvailable() ? pid->targetTemp : -1;
    doc["pidP"]               = (float) pid->p / 100.0f;
    doc["pidI"]               = (float) pid->i / 100.0f;
    doc["pidD"]               = (float) pid->d / 100.0f;

    // Per-channel fan settings (new API; ch0 mirrors existing flat fields for compat)
    {
        JsonArray fans = doc["fans"].to<JsonArray>();
        int numFans = board->getNumFans();
        for (int ch = 0; ch < numFans; ch++) {
            PidSettings* fanPid = board->getPidSettings(ch);
            JsonObject fan = fans.add<JsonObject>();
            fan["label"]        = board->getFanLabel(ch);
            fan["mode"]         = Config::getFanMode(ch);
            fan["manualSpeed"]  = Config::getFanManualSpeed(ch);
            fan["overheatTemp"] = Config::getFanOverheatTemp(ch);
            fan["rpm"]          = POWER_MANAGEMENT_MODULE.getFanRPM(ch);
            fan["speedPerc"]    = POWER_MANAGEMENT_MODULE.getFanPerc(ch);
            JsonObject pid_obj  = fan["pid"].to<JsonObject>();
            pid_obj["targetTemp"] = board->isPIDAvailable() ? (int) fanPid->targetTemp : -1;
            pid_obj["p"]          = (float) fanPid->p / 100.0f;
            pid_obj["i"]          = (float) fanPid->i / 100.0f;
            pid_obj["d"]          = (float) fanPid->d / 100.0f;
        }
    }

    doc["hostname"]           = hostname;
    doc["ssid"]               = ssid;
    doc["stratumURL"]         = stratumURL;
    doc["stratumPort"]        = Config::getStratumPortNumber();
    doc["stratumUser"]        = stratumUser;
    doc["stratumEnonceSubscribe"] = Config::isStratumEnonceSubscribe();
    doc["stratumTLS"]         = Config::isStratumTLS();
    doc["fallbackStratumURL"] = fallbackStratumURL;
    doc["fallbackStratumPort"]= Config::getStratumFallbackPortNumber();
    doc["fallbackStratumUser"] = fallbackStratumUser;
    doc["fallbackStratumEnonceSubscribe"] = Config::isStratumFallbackEnonceSubscribe();
    doc["fallbackStratumTLS"] = Config::isStratumFallbackTLS();
    doc["stratumProtocol"]    = Config::getStratumProtocol();
    doc["fallbackStratumProtocol"] = Config::getFallbackStratumProtocol();
    {
        char *sv2_auth = Config::getSV2AuthorityPubkey();
        doc["sv2AuthorityPubkey"] = sv2_auth ? sv2_auth : "";
        safe_free(sv2_auth);
        char *fb_sv2_auth = Config::getFallbackSV2AuthorityPubkey();
        doc["fallbackSv2AuthorityPubkey"] = fb_sv2_auth ? fb_sv2_auth : "";
        safe_free(fb_sv2_auth);
    }
    doc["sv2ChannelType"]     = Config::getSV2ChannelType();
    doc["fallbackSv2ChannelType"] = Config::getFallbackSV2ChannelType();
    doc["voltage"]            = POWER_MANAGEMENT_MODULE.getVoltage();
    doc["frequency"]          = board->getAsicFrequency();
    doc["defaultFrequency"]   = board->getDefaultAsicFrequency();
    doc["jobInterval"]        = board->getAsicJobIntervalMs();
    doc["stratumDifficulty"] = Config::getStratumDifficulty();
    doc["overheat_temp"]      = Config::getOverheatTemp();
    doc["flipscreen"]         = board->isFlipScreenEnabled() ? 1 : 0;
    doc["invertscreen"]       = Config::isInvertScreenEnabled() ? 1 : 0; // unused?
    doc["autoscreenoff"]      = Config::isAutoScreenOffEnabled() ? 1 : 0;
    doc["invertfanpolarity"]  = board->isInvertFanPolarityEnabled() ? 1 : 0;
    doc["autofanspeed"]       = Config::getTempControlMode();
    doc["stratum_keep"]       = Config::isStratumKeepaliveEnabled() ? 1 : 0;
#ifdef VR_FREQUENCY_ENABLED
    doc["vrFrequency"]        = board->getVrFrequency();
    doc["defaultVrFrequency"] = board->getDefaultVrFrequency();
#endif
    doc["otp"]                = Config::isOTPEnabled(); // flag if otp is enabled

    // system screen
    doc["ASICModel"]          = board->getAsicModel();
    doc["uptimeSeconds"]      = (esp_timer_get_time() - SYSTEM_MODULE.getStartTime()) / 1000000;
    doc["lastResetReason"]    = SYSTEM_MODULE.getLastResetReason();
    doc["wifiStatus"]         = SYSTEM_MODULE.getWifiStatus();
    doc["freeHeap"]           = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    doc["freeHeapInt"]        = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    doc["version"]            = esp_app_get_description()->version;
    doc["runningPartition"]   = esp_ota_get_running_partition()->label;

    doc["defaultTheme"]       = board->getDefaultTheme();

    // Thermal governor. `frequency` / `coreVoltage` above stay the saved
    // ceiling; the effective pair lives here and only in RAM.
    {
        JsonObject gobj = doc["governor"].to<JsonObject>();

        // the PM task owns this state: take a snapshot under its lock
        LockGuard pmLock(POWER_MANAGEMENT_MODULE);
        gov::ThermalGovernor *governor = POWER_MANAGEMENT_MODULE.getGovernor();
        const gov::GovDecision *last   = POWER_MANAGEMENT_MODULE.getGovernorDecision();

        if (governor && last) {
            governor->fillJson(gobj, *last, POWER_MANAGEMENT_MODULE.getGovernorMode(),
                               POWER_MANAGEMENT_MODULE.isGovernorForcedOff(),
                               POWER_MANAGEMENT_MODULE.getGovernorLastActionAgeS());
        } else {
            gobj["mode"]      = 0;
            gobj["forcedOff"] = POWER_MANAGEMENT_MODULE.isGovernorForcedOff();
            gobj["state"]     = "OFF";
        }

        // what is really applied right now, whatever the governor decided
        gobj["freq"]       = POWER_MANAGEMENT_MODULE.getEffectiveFrequency();
        gobj["voltage"]    = POWER_MANAGEMENT_MODULE.getEffectiveVoltageMillis();
        gobj["freqCap"]    = board->getAsicFrequency();
        gobj["voltageCap"] = board->getAsicVoltageMillis();
    }

    // Display data layer (main/ui_data.*): rotation config and a lightweight
    // energy snapshot. The full ~30KB UiState is never built here - it is
    // filled directly into a PSRAM buffer by the display task, not over
    // this API - so only the two numbers the "energy" object needs are
    // fetched through uiDataGetEnergySnapshot().
    {
        JsonObject dobj = doc["display"].to<JsonObject>();
        dobj["tarifa"]  = Config::getTarifaCents();
        dobj["scrMask"] = Config::getScrMask();
        dobj["scrSecs"] = Config::getScrSecs();
        char *tz = Config::getTz();
        dobj["tz"] = tz ? tz : "";
        free(tz);

        float kwhToday = 0.0f, costToday = 0.0f;
        uiDataGetEnergySnapshot(kwhToday, costToday);
        JsonObject eobj = doc["energy"].to<JsonObject>();
        eobj["kwhToday"]  = kwhToday;
        eobj["costToday"] = costToday;
    }

    //ESP_LOGI(TAG, "allocs: %d, deallocs: %d, reallocs: %d", allocs, deallocs, reallocs);

    // Serialize the JSON document to a String and send it
    esp_err_t ret = sendJsonResponse(req, doc);
    doc.clear();

    // Free temporary strings
    free(ssid);
    free(hostname);
    free(stratumURL);
    free(stratumUser);
    free(fallbackStratumURL);
    free(fallbackStratumUser);

    return ret;
}



esp_err_t PATCH_update_settings(httpd_req_t *req)
{
    // close connection when out of scope
    ConGuard g(http_server, req);

    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    // Set CORS headers
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    if (validateOTP(req) != ESP_OK) {
        return ESP_FAIL;
    }

    PSRAMAllocator allocator;
    JsonDocument doc(&allocator);

    esp_err_t err = getJsonData(req, doc);
    if (err != ESP_OK) {
        return err;
    }

    if (doc["ssid"].is<const char*>()) {
        Config::setWifiSSID(doc["ssid"].as<const char*>());
    }
    if (doc["wifiPass"].is<const char*>()) {
        Config::setWifiPass(doc["wifiPass"].as<const char*>());
    }
    if (doc["hostname"].is<const char*>()) {
        Config::setHostname(doc["hostname"].as<const char*>());
    }
    if (doc["coreVoltage"].is<uint16_t>()) {
        uint16_t coreVoltage = doc["coreVoltage"].as<uint16_t>();
        if (coreVoltage > 0) {
            Config::setAsicVoltage(coreVoltage);
        }
    }
    if (doc["frequency"].is<uint16_t>()) {
        uint16_t frequency = doc["frequency"].as<uint16_t>();
        if (frequency > 0) {
            Config::setAsicFrequency(frequency);
        }
    }
    if (doc["jobInterval"].is<uint16_t>()) {
        uint16_t jobInterval = doc["jobInterval"].as<uint16_t>();
        if (jobInterval > 0) {
            Config::setAsicJobInterval(jobInterval);
        }
    }
    if (doc["stratumDifficulty"].is<uint32_t>()) {
        Config::setStratumDifficulty(doc["stratumDifficulty"].as<uint32_t>());
    }
    if (doc["flipscreen"].is<bool>()) {
        Config::setFlipScreen(doc["flipscreen"].as<bool>());
    }
    if (doc["overheat_temp"].is<uint16_t>()) {
        Config::setOverheatTemp(doc["overheat_temp"].as<uint16_t>());
    }
    if (doc["invertscreen"].is<bool>()) {
        Config::setInvertScreen(doc["invertscreen"].as<bool>());
    }
    if (doc["invertfanpolarity"].is<bool>()) {
        Config::setFanPolarity(doc["invertfanpolarity"].as<bool>());
    }
    if (doc["autofanspeed"].is<uint16_t>()) {
        Config::setTempControlMode(doc["autofanspeed"].as<uint16_t>());
    }
    if (doc["manualFanSpeed"].is<uint16_t>()) {
        Config::setFanSpeed(doc["manualFanSpeed"].as<uint16_t>());
    }
    if (doc["autoscreenoff"].is<bool>()) {
        Config::setAutoScreenOff(doc["autoscreenoff"].as<bool>());
    }
    if (doc["stratum_keep"].is<bool>() || doc["stratum_keep"].is<int>()) {
        bool value = doc["stratum_keep"].as<int>() != 0;
        Config::setStratumKeepaliveEnabled(value);
        ESP_LOGI("system", "stratum_keep updated via WebUI: %s", value ? "ENABLED" : "DISABLED");
    }
    if (doc["pidTargetTemp"].is<uint16_t>()) {
        Config::setPidTargetTemp(doc["pidTargetTemp"].as<uint16_t>());
    }
    if (doc["pidP"].is<float>()) {
        Config::setPidP((uint16_t) (doc["pidP"].as<float>() * 100.0f));
    }
    if (doc["pidI"].is<float>()) {
        Config::setPidI((uint16_t) (doc["pidI"].as<float>() * 100.0f));
    }
    if (doc["pidD"].is<float>()) {
        Config::setPidD((uint16_t) (doc["pidD"].as<float>() * 100.0f));
    }
#ifdef VR_FREQUENCY_ENABLED
    if (doc["vrFrequency"].is<uint32_t>()) {
        Config::setVrFrequency(doc["vrFrequency"].as<uint32_t>());
    }
#endif

    // Per-channel fan settings: fans[0] maps to ch0 NVS keys, fans[1] to ch1 NVS keys
    if (doc["fans"].is<JsonArray>()) {
        JsonArray fans = doc["fans"].as<JsonArray>();
        int ch = 0;
        for (JsonObject fan : fans) {
            if (ch > 1) break;
            if (fan["mode"].is<uint16_t>())
                Config::setFanMode(ch, fan["mode"].as<uint16_t>());
            if (fan["manualSpeed"].is<uint16_t>())
                Config::setFanManualSpeed(ch, fan["manualSpeed"].as<uint16_t>());
            if (fan["overheatTemp"].is<uint16_t>())
                Config::setFanOverheatTemp(ch, fan["overheatTemp"].as<uint16_t>());
            if (fan["pid"].is<JsonObject>()) {
                JsonObject p = fan["pid"].as<JsonObject>();
                if (p["targetTemp"].is<uint16_t>())
                    Config::setFanPidTargetTemp(ch, p["targetTemp"].as<uint16_t>());
                if (p["p"].is<float>())
                    Config::setFanPidP(ch, (uint16_t) (p["p"].as<float>() * 100.0f));
                if (p["i"].is<float>())
                    Config::setFanPidI(ch, (uint16_t) (p["i"].as<float>() * 100.0f));
                if (p["d"].is<float>())
                    Config::setFanPidD(ch, (uint16_t) (p["d"].as<float>() * 100.0f));
            }
            ch++;
        }
    }

    // Thermal governor settings.
    // NOTE: like every other numeric field of this handler, these are matched
    // with is<uint16_t>() - only integers are accepted. A float such as 78.0 is
    // silently ignored (that bit us during the calibration sweep: the frequency
    // was sent as 500.0 and dropped while the voltage went through).
    if (doc["governor"].is<JsonObject>()) {
        JsonObject gv = doc["governor"].as<JsonObject>();
        if (gv["enable"].is<uint16_t>()) {
            Config::setGovEnable(gv["enable"].as<uint16_t>());
        }
        if (gv["vrTarget"].is<uint16_t>()) {
            Config::setGovVrTarget(gv["vrTarget"].as<uint16_t>());
        }
        if (gv["vriTarget"].is<uint16_t>()) {
            Config::setGovVriTarget(gv["vriTarget"].as<uint16_t>());
        }
        if (gv["ioutMax"].is<uint16_t>()) {
            Config::setGovIoutMax(gv["ioutMax"].as<uint16_t>());
        }
        if (gv["vinMin"].is<uint16_t>()) {
            Config::setGovVinMinMv(gv["vinMin"].as<uint16_t>());
        }
        if (gv["pinMax"].is<uint16_t>()) {
            Config::setGovPinMax(gv["pinMax"].as<uint16_t>());
        }
        if (gv["iinMax"].is<uint16_t>()) {
            Config::setGovIinMaxDA(gv["iinMax"].as<uint16_t>());
        }
        if (gv["fmin"].is<uint16_t>()) {
            Config::setGovFmin(gv["fmin"].as<uint16_t>());
        }
        if (gv["vmin"].is<uint16_t>()) {
            Config::setGovVmin(gv["vmin"].as<uint16_t>());
        }
        if (gv["vsagMv"].is<uint16_t>()) {
            Config::setGovVsagMv(gv["vsagMv"].as<uint16_t>());
        }
        if (gv["hrMin"].is<uint16_t>()) {
            Config::setGovHrMinPerc(gv["hrMin"].as<uint16_t>());
        }
        if (gv["curve"].is<const char*>()) {
            Config::setGovCurve(gv["curve"].as<const char*>());
        }
    }

    // Display data layer (main/ui_data.*): tarifa/scr_mask/scr_secs/tz.
    // Same is<uint16_t>()-only style as every other numeric field in this
    // handler (see the governor comment above for why: a float value is
    // silently ignored rather than accepted).
    bool displayConfigChanged = false;
    if (doc["tarifa"].is<uint16_t>()) {
        Config::setTarifaCents(doc["tarifa"].as<uint16_t>());
        displayConfigChanged = true;
    }
    if (doc["scr_mask"].is<uint32_t>()) {
        Config::setScrMask(doc["scr_mask"].as<uint32_t>());
        displayConfigChanged = true;
    }
    if (doc["scr_secs"].is<uint16_t>()) {
        uint16_t secs = doc["scr_secs"].as<uint16_t>();
        if (secs > 0) {
            Config::setScrSecs(secs);
            displayConfigChanged = true;
        }
    }
    bool tzChanged = false;
    if (doc["tz"].is<const char*>()) {
        Config::setTz(doc["tz"].as<const char*>());
        tzChanged = true;
    }

    // save stratum settings
    STRATUM_MANAGER->saveSettings(doc);

    doc.clear();

    // Signal the end of the response
    httpd_resp_send_chunk(req, NULL, 0);

    // Reload settings after update
    Board* board = SYSTEM_MODULE.getBoard();
    board->loadSettings();

    // Reload fan controller settings (picks up both ch0 and ch1 changes)
    POWER_MANAGEMENT_MODULE.getFanController().loadSettings();

    // reload settings of system module (and display)
    SYSTEM_MODULE.loadSettings();

    // reload settings, trigger reconnect if stratum config changed
    STRATUM_MANAGER->loadSettings();

    // Rebuild the governor config: its ceilings come from the board settings we
    // just reloaded, so this has to happen after board->loadSettings().
    POWER_MANAGEMENT_MODULE.reloadGovernorConfig();

    // Display data layer: refresh the in-RAM tarifa/scr_mask/scr_secs cache,
    // and re-apply TZ (no reboot needed) if it changed.
    if (displayConfigChanged) {
        uiDataReloadConfig();
    }
    if (tzChanged) {
        sntp.applyTimezoneFromNvs();
    }

    return ESP_OK;
}

// Selects the factory app for the next boot and restarts. The running app lives
// in `factory`; an AxeOS OTA writes into ota_0, so factory is the way back.
esp_err_t POST_boot_factory(httpd_req_t *req)
{
    // close connection when out of scope
    ConGuard g(http_server, req);

    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    if (validateOTP(req) != ESP_OK) {
        return ESP_FAIL;
    }

    if (switch_to_factory_partition() != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not select the factory partition");
        return ESP_FAIL;
    }

    ESP_LOGW(TAG, "factory partition selected by API request, restarting");
    httpd_resp_sendstr(req, "Factory partition selected, restarting now!\n");

    vTaskDelay(pdMS_TO_TICKS(1000));
    POWER_MANAGEMENT_MODULE.restart();

    // unreachable
    return ESP_OK;
}

esp_err_t GET_system_asic(httpd_req_t *req)
{
    // close connection when out of scope
    ConGuard g(http_server, req);

    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    httpd_resp_set_type(req, "application/json");

    // CORS
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    Board* board = SYSTEM_MODULE.getBoard();

    PSRAMAllocator allocator;
    JsonDocument doc(&allocator);

    // Basisfelder
    doc["ASICModel"]        = board->getAsicModel();
    doc["deviceModel"]      = board->getDeviceModel();
    doc["asicCount"]        = board->getAsicCount();
    doc["defaultFrequency"] = board->getDefaultAsicFrequency();
    doc["defaultVoltage"]   = board->getDefaultAsicVoltageMillis();
    doc["absMaxFrequency"]  = board->getAbsMaxAsicFrequency();
    doc["absMaxVoltage"]    = board->getAbsMaxAsicVoltageMillis();
    doc["ecoFrequency"]     = board->getEcoAsicFrequency();
    doc["ecoVoltage"]       = board->getEcoAsicVoltageMillis();

    doc["swarmColor"]       = board->getSwarmColorName();

    // frequencyOptions
    {
        JsonArray arr = doc["frequencyOptions"].to<JsonArray>();
        const auto& freqs = board->getFrequencyOptions();
        for (uint32_t f : freqs) { arr.add(f); }
    }

    // voltageOptions
    {
        JsonArray arr = doc["voltageOptions"].to<JsonArray>();
        const auto& volts = board->getVoltageOptions();
        for (uint32_t v : volts) { arr.add(v); }
    }

    esp_err_t ret = sendJsonResponse(req, doc);
    doc.clear();
    return ret;
}

esp_err_t POST_reset_stats(httpd_req_t *req)
{
    ConGuard g(http_server, req);

    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    STRATUM_MANAGER->resetSessionStats();

    ESP_LOGI(TAG, "Session stats reset by user");
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

// ---------------------------------------------------------------------------
// GET/PATCH /api/system/screens: per-screen display duration + power-bill
// config.
// ---------------------------------------------------------------------------

// English display name for each registered screen id. The registry
// (screen_registry.cpp) does not carry a human-readable label of its own
// (Screen::name() is a short lowercase log identifier, e.g. "conta_de_luz",
// not meant for display) - this is the API's own naming, in English to
// match the web UI (the on-device LVGL screens keep their own, unrelated
// text and are not touched here).
static const char *screenDisplayName(int id)
{
    switch (id) {
    case 1:  return "Dashboard";
    case 4:  return "Energy flow";
    case 5:  return "Quartet";
    case 6:  return "Efficiency";
    case 7:  return "Luck";
    case 10: return "Halving";
    case 11: return "Shares";
    case 12: return "If we hit a block";
    case 13: return "Last 24 hours";
    case 14: return "Hashrate";
    case 15: return "Power bill";
    case 16: return "Room";
    case 18: return "Journal";
    case 19: return "Zen";
    case 20: return "Clock";
    default: return "Screen"; // only reachable if a new screen is registered without updating this table
    }
}

// Shared by GET and PATCH /api/system/screens so both return the exact same
// shape (the PATCH response is "the new state", same contract as the GET).
static void fillScreensJson(JsonDocument &doc)
{
    uint16_t defaultSecs = Config::getScrSecs();
    if (defaultSecs == 0) {
        defaultSecs = 10;
    }
    doc["defaultSecs"] = defaultSecs;
    doc["minSecs"]     = (uint16_t) UI_SCREEN_SECS_MIN;
    doc["maxSecs"]     = (uint16_t) UI_SCREEN_SECS_MAX;

    uint32_t mask = Config::getScrMask();

    size_t count = 0;
    const ScreenRegistryEntry *all = screenRegistryAll(&count);

    JsonArray screens = doc["screens"].to<JsonArray>();
    for (size_t i = 0; i < count; i++) {
        int id = all[i].screen->number();
        JsonObject s = screens.add<JsonObject>();
        s["id"]      = id;
        s["name"]    = screenDisplayName(id);
        s["enabled"] = (id >= 0 && id < UI_MAX_SCREENS) ? ((mask & (1u << (unsigned) id)) != 0) : false;
        s["secs"]    = uiRotationScreenSecs(id);
    }

    JsonObject pb   = doc["powerBill"].to<JsonObject>();
    char *currency  = Config::getCurrency();
    pb["currency"]     = currency ? currency : "R$";
    pb["pricePerKwh"]  = Config::getTarifaPerKwh();
    pb["minPrice"]     = UI_POWERBILL_MIN_PRICE;
    pb["maxPrice"]     = UI_POWERBILL_MAX_PRICE;
    free(currency);
}

esp_err_t GET_system_screens(httpd_req_t *req)
{
    // close connection when out of scope
    ConGuard g(http_server, req);

    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    httpd_resp_set_type(req, "application/json");

    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    PSRAMAllocator allocator;
    JsonDocument doc(&allocator);
    fillScreensJson(doc);

    esp_err_t ret = sendJsonResponse(req, doc);
    doc.clear();
    return ret;
}

esp_err_t PATCH_update_screens(httpd_req_t *req)
{
    // close connection when out of scope
    ConGuard g(http_server, req);

    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    // Set CORS headers
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    if (validateOTP(req) != ESP_OK) {
        return ESP_FAIL;
    }

    PSRAMAllocator allocator;
    JsonDocument doc(&allocator);

    esp_err_t err = getJsonData(req, doc);
    if (err != ESP_OK) {
        return err;
    }

    // ---- Parse the request into plain (NVS/HTTP-free) structures first --
    size_t regCount = 0;
    const ScreenRegistryEntry *all = screenRegistryAll(&regCount);
    int registeredIds[UI_MAX_SCREENS];
    int idCount = 0;
    for (size_t i = 0; i < regCount && idCount < UI_MAX_SCREENS; i++) {
        registeredIds[idCount++] = all[i].screen->number();
    }

    UiScreenPatchItem items[UI_MAX_SCREENS];
    int itemCount = 0;
    if (doc["screens"].is<JsonArray>()) {
        for (JsonObject o : doc["screens"].as<JsonArray>()) {
            if (itemCount >= UI_MAX_SCREENS) {
                break;
            }
            // An item with no valid "id" cannot be matched to a screen;
            // same lenient "ignore the malformed bit" style the rest of
            // this handler uses (see the governor/fans blocks above), so
            // skip it instead of failing the whole request.
            if (!o["id"].is<int>()) {
                continue;
            }
            UiScreenPatchItem &it = items[itemCount++];
            it.id         = o["id"].as<int>();
            it.hasEnabled = o["enabled"].is<bool>();
            it.enabled    = it.hasEnabled ? o["enabled"].as<bool>() : false;
            it.hasSecs    = o["secs"].is<uint16_t>() || o["secs"].is<int>();
            it.secs       = it.hasSecs ? (uint16_t) o["secs"].as<int>() : 0;
        }
    }

    bool hasDefaultSecs = doc["defaultSecs"].is<uint16_t>() || doc["defaultSecs"].is<int>();
    uint16_t newDefaultSecs = hasDefaultSecs ? (uint16_t) doc["defaultSecs"].as<int>() : 0;

    bool hasCurrency = false;
    bool hasPrice = false;
    const char *newCurrency = nullptr;
    float newPrice = 0.0f;
    if (doc["powerBill"].is<JsonObject>()) {
        JsonObject pbIn = doc["powerBill"].as<JsonObject>();
        hasCurrency = pbIn["currency"].is<const char *>();
        newCurrency = hasCurrency ? pbIn["currency"].as<const char *>() : nullptr;
        hasPrice = pbIn["pricePerKwh"].is<float>() || pbIn["pricePerKwh"].is<double>() || pbIn["pricePerKwh"].is<int>();
        newPrice = hasPrice ? pbIn["pricePerKwh"].as<float>() : 0.0f;
    }

    // ---- Validate EVERYTHING before writing anything --------------------
    int badId = -1;
    UiScreenPatchError verr = uiValidateScreenPatch(items, itemCount, registeredIds, idCount, Config::getScrMask(),
                                                     UI_SCREEN_SECS_MIN, UI_SCREEN_SECS_MAX, &badId);
    if (verr == UI_SCREEN_PATCH_UNKNOWN_ID) {
        char msg[48];
        snprintf(msg, sizeof(msg), "Unknown screen id: %d", badId);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, msg);
        return ESP_FAIL;
    }
    if (verr == UI_SCREEN_PATCH_SECS_RANGE) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "secs must be between 3 and 600");
        return ESP_FAIL;
    }
    if (verr == UI_SCREEN_PATCH_NONE_ENABLED) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "at least one screen must remain enabled");
        return ESP_FAIL;
    }

    if (hasDefaultSecs && (newDefaultSecs < UI_SCREEN_SECS_MIN || newDefaultSecs > UI_SCREEN_SECS_MAX)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "defaultSecs must be between 3 and 600");
        return ESP_FAIL;
    }

    if (hasCurrency && !uiValidateCurrencyBytes(newCurrency, UI_POWERBILL_CURRENCY_MAX_BYTES)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "currency must be 1-4 bytes");
        return ESP_FAIL;
    }
    if (hasPrice && !uiValidatePricePerKwh(newPrice, UI_POWERBILL_MIN_PRICE, UI_POWERBILL_MAX_PRICE)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "pricePerKwh must be between 0 and 10");
        return ESP_FAIL;
    }

    // ---- Everything validated: apply it ----------------------------------
    uint32_t newMask = Config::getScrMask();
    uint16_t durs[UI_MAX_SCREENS];
    Config::getScrDurations(durs);
    for (int i = 0; i < itemCount; i++) {
        const UiScreenPatchItem &it = items[i];
        if (it.hasEnabled && it.id >= 0 && it.id < UI_MAX_SCREENS) {
            uint32_t bit = 1u << (unsigned) it.id;
            newMask = it.enabled ? (newMask | bit) : (newMask & ~bit);
        }
        if (it.hasSecs && it.id >= 0 && it.id < UI_MAX_SCREENS) {
            durs[it.id] = it.secs;
        }
    }
    Config::setScrMask(newMask);
    Config::setScrDurations(durs);
    if (hasDefaultSecs) {
        Config::setScrSecs(newDefaultSecs);
    }
    if (hasCurrency) {
        Config::setCurrency(newCurrency);
    }
    if (hasPrice) {
        // Rounded to the nearest cent by setTarifaPerKwh() - see its
        // comment in nvs_config.h for why 4 decimals are not stored.
        Config::setTarifaPerKwh(newPrice);
    }

    doc.clear();

    // Refresh the in-RAM cache the display rotation reads - same function
    // PATCH /api/system uses for scr_mask/scr_secs.
    uiDataReloadConfig();

    // Respond with the new state, same shape as GET /api/system/screens.
    httpd_resp_set_type(req, "application/json");
    PSRAMAllocator respAllocator;
    JsonDocument respDoc(&respAllocator);
    fillScreensJson(respDoc);
    esp_err_t ret = sendJsonResponse(req, respDoc);
    respDoc.clear();
    return ret;
}
