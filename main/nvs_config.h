#pragma once

// clang-format off

#include <stdint.h>

// Max length 15
#define NVS_CONFIG_WIFI_SSID "wifissid"
#define NVS_CONFIG_WIFI_PASS "wifipass"
#define NVS_CONFIG_HOSTNAME "hostname"
#define NVS_CONFIG_STRATUM_URL "stratumurl"
#define NVS_CONFIG_STRATUM_PORT "stratumport"
#define NVS_CONFIG_STRATUM_USER "stratumuser"
#define NVS_CONFIG_STRATUM_PASS "stratumpass"
#define NVS_CONFIG_STRATUM_ENONCE_SUB "stratumesub"
#define NVS_CONFIG_STRATUM_TLS "stratumtls"
#define NVS_CONFIG_STRATUM_FALLBACK_URL "fbstratumurl"
#define NVS_CONFIG_STRATUM_FALLBACK_PORT "fbstratumport"
#define NVS_CONFIG_STRATUM_FALLBACK_USER "fbstratumuser"
#define NVS_CONFIG_STRATUM_FALLBACK_PASS "fbstratumpass"
#define NVS_CONFIG_STRATUM_FALLBACK_ENONCE_SUB "fbstratumesub"
#define NVS_CONFIG_STRATUM_FALLBACK_TLS "fbstratumtls"
#define NVS_CONFIG_STRATUM_DIFFICULTY "stratumdiff"
#define NVS_CONFIG_STRATUM_KEEPALIVE "stratum_keep"

#define NVS_CONFIG_ASIC_FREQ "asicfrequency"
#define NVS_CONFIG_ASIC_VOLTAGE "asicvoltage"
#define NVS_CONFIG_ASIC_JOB_INTERVAL "asicjobinterval"
#define NVS_CONFIG_FLIP_SCREEN "flipscreen"
#define NVS_CONFIG_INVERT_SCREEN "invertscreen"
#define NVS_CONFIG_INVERT_FAN_POLARITY "invertfanpol"   // kept for downgrade compatibility
#define NVS_CONFIG_AUTO_FAN_POLARITY "autofanpol"       // kept for downgrade compatibility
#define NVS_CONFIG_FAN_PWM_POLARITY "pwmfanpol"         // new setting
#define NVS_CONFIG_AUTO_FAN_SPEED "autofanspeed"
#define NVS_CONFIG_FAN_SPEED "fanspeed"
#define NVS_CONFIG_SELF_TEST "selftest"
#define NVS_CONFIG_AUTO_SCREEN_OFF "autoscreenoff"
#define NVS_CONFIG_OVERHEAT_TEMP "overheat_temp"

#define NVS_CONFIG_INFLUX_ENABLE "influx_enable"
#define NVS_CONFIG_INFLUX_URL "influx_url"
#define NVS_CONFIG_INFLUX_TOKEN "influx_token"
#define NVS_CONFIG_INFLUX_PORT "influx_port"
#define NVS_CONFIG_INFLUX_BUCKET "influx_bucket"
#define NVS_CONFIG_INFLUX_ORG "influx_org"
#define NVS_CONFIG_INFLUX_PREFIX "influx_prefix"

#define NVS_CONFIG_PID_TARGET_TEMP "pid_temp"
#define NVS_CONFIG_PID_P "pid_p"
#define NVS_CONFIG_PID_I "pid_i"
#define NVS_CONFIG_PID_D "pid_d"

// Fan channel 1 (independent second fan, e.g. for VR temp)
#define NVS_CONFIG_FAN1_SPEED    "fan1speed"
#define NVS_CONFIG_FAN1_MODE     "fan1mode"
#define NVS_CONFIG_FAN1_PID_TEMP "fan1_pid_temp"
#define NVS_CONFIG_FAN1_PID_P    "fan1_pid_p"
#define NVS_CONFIG_FAN1_PID_I    "fan1_pid_i"
#define NVS_CONFIG_FAN1_PID_D    "fan1_pid_d"
#define NVS_CONFIG_FAN1_OVERHEAT "fan1_overheat"

// Thermal / electrical governor (all keys <= 15 chars)
#define NVS_CONFIG_GV_ENABLE     "gv_enable"      // 0 = off, 1 = active, 2 = shadow
#define NVS_CONFIG_GV_VR_TARGET  "gv_vr_target"   // degC
#define NVS_CONFIG_GV_VRI_TARGET "gv_vri_target"  // degC
#define NVS_CONFIG_GV_IOUT_MAX   "gv_iout_max"    // A          (0 = constraint off)
#define NVS_CONFIG_GV_VIN_MIN    "gv_vin_min"     // mV         (0 = constraint off)
#define NVS_CONFIG_GV_PIN_MAX    "gv_pin_max"     // W          (0 = constraint off)
#define NVS_CONFIG_GV_IIN_MAX    "gv_iin_max"     // 0.1 A      (0 = constraint off)
#define NVS_CONFIG_GV_FMIN       "gv_fmin"        // MHz
#define NVS_CONFIG_GV_VMIN       "gv_vmin"        // mV
#define NVS_CONFIG_GV_VSAG_MV    "gv_vsag_mv"     // mV
#define NVS_CONFIG_GV_HR_MIN     "gv_hr_min"      // %
#define NVS_CONFIG_GV_CURVE      "gv_curve"       // "f:mV,f:mV,..."

// Display data layer (main/ui_data.*): energy tariff, screen rotation, timezone.
#define NVS_CONFIG_TARIFA        "tarifa"         // cents/kWh, u16
#define NVS_CONFIG_SCR_MASK      "scr_mask"       // bit N = screen N enabled, u64 (32 bits used)
#define NVS_CONFIG_SCR_SECS      "scr_secs"       // seconds per screen, u16
#define NVS_CONFIG_SCR_DURS      "scr_durs"       // per-screen override, blob of NVS_SCR_DURS_COUNT u16 (0 = use scr_secs)
#define NVS_CONFIG_CURRENCY      "currency"       // power-bill currency symbol, string (e.g. "R$")
#define NVS_CONFIG_TZ            "tz"             // POSIX TZ string, e.g. "<-03>3"

// Kept in sync with UI_MAX_SCREENS (main/ui_data.h) by convention, not by a
// shared header: nvs_config.h must not depend on ui_data.h.
#define NVS_SCR_DURS_COUNT 32

// Internal bookkeeping for the kWh-today counter (main/ui_data.cpp), so it
// survives a reboot on the same local day. Not user-facing.
#define NVS_CONFIG_UI_KWH_WH     "ui_kwh_wh"      // accumulated Wh "today", u64
#define NVS_CONFIG_UI_KWH_EPOCH  "ui_kwh_ep"      // wall-clock epoch of that value, u64

#define NVS_CONFIG_ALERT_DISCORD_WATCHDOG_ENABLE "alrt_disc_en"
#define NVS_CONFIG_ALERT_DISCORD_URL    "alrt_disc_url"
#define NVS_CONFIG_ALERT_DISCORD_BLOCK_FOUND_ENABLE "alrt_disc_bf_en"
#define NVS_CONFIG_ALERT_DISCORD_BEST_DIFF "alrt_disc_bd_en"

#define NVS_CONFIG_SHOW_BLOCK_FOUND_ENABLE "block_found_en"

#define NVS_CONFIG_SWARM "swarmconfig"

#define NVS_CONFIG_VR_FREQUENCY "vr_frequency"

// device global stats
#define NVS_TOTAL_FOUND_BLOCKS "totalblocks"
#define NVS_CONFIG_BEST_DIFF "bestdiff"


// OTP
#define NVS_CONFIG_OTP_SECRET "otp_secret"
#define NVS_CONFIG_OTP_ENABLED "otp_enabled"
#define NVS_CONFIG_OTP_LAST_STEP "otp_last_step"
#define NVS_CONFIG_OTP_USED_MASK "otp_used_mask"
#define NVS_CONFIG_OTP_SESSION_KEY "otp_sess_key"
#define NVS_CONFIG_OTP_BOOT_ID "otp_boot_id"

#define NVS_CONFIG_POOL_MODE_BALANCE "pool_balance"
#define NVS_CONFIG_POOL_MODE "pool_mode"

// Stratum V2
#define NVS_CONFIG_STRATUM_PROTOCOL "sv2_proto"
#define NVS_CONFIG_SV2_AUTHORITY_PUBKEY "sv2_auth_pk"
#define NVS_CONFIG_SV2_CHANNEL_TYPE "sv2_chan_type"
#define NVS_CONFIG_FB_STRATUM_PROTOCOL "fbsv2_proto"
#define NVS_CONFIG_FB_SV2_AUTHORITY_PUBKEY "fbsv2_authpk"
#define NVS_CONFIG_FB_SV2_CHANNEL_TYPE "fbsv2_chtype"

#if defined(CONFIG_FAN_MODE_MANUAL)
#define CONFIG_AUTO_FAN_SPEED_VALUE 0
#elif defined(CONFIG_FAN_MODE_CLASSIC)
#define CONFIG_AUTO_FAN_SPEED_VALUE 1
#elif defined(CONFIG_FAN_MODE_PID)
#define CONFIG_AUTO_FAN_SPEED_VALUE 2
#endif

#ifdef CONFIG_STRATUM_KEEPALIVE_DEFAULT
#define CONFIG_KEEPALIVE_VALUE 1
#else
#define CONFIG_KEEPALIVE_VALUE 0
#endif

#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace Config {
    char* nvs_config_get_string(const char* key, const char* default_value);
    void nvs_config_set_string(const char* key, const char* value);
    uint16_t nvs_config_get_u16(const char* key, uint16_t default_value);
    void nvs_config_set_u16(const char* key, uint16_t value);
    bool nvs_config_has_u16(const char* key);
    uint64_t nvs_config_get_u64(const char* key, uint64_t default_value);
    void nvs_config_set_u64(const char* key, uint64_t value);
    bool nvs_config_get_blob(const char* key, void* out, size_t len);
    void nvs_config_set_blob(const char* key, const void* data, size_t len);

    // ---- String Getters ----
    inline char* getWifiSSID() { return nvs_config_get_string(NVS_CONFIG_WIFI_SSID, CONFIG_ESP_WIFI_SSID); }
    inline char* getWifiPass() { return nvs_config_get_string(NVS_CONFIG_WIFI_PASS, CONFIG_ESP_WIFI_PASSWORD); }
    inline char* getHostname() { return nvs_config_get_string(NVS_CONFIG_HOSTNAME, CONFIG_LWIP_LOCAL_HOSTNAME); }
    inline char* getStratumURL() { return nvs_config_get_string(NVS_CONFIG_STRATUM_URL, CONFIG_STRATUM_URL); }
    inline char* getStratumUser() { return nvs_config_get_string(NVS_CONFIG_STRATUM_USER, CONFIG_STRATUM_USER); }
    inline char* getStratumPass() { return nvs_config_get_string(NVS_CONFIG_STRATUM_PASS, CONFIG_STRATUM_PW); }
    inline char* getStratumFallbackURL() { return nvs_config_get_string(NVS_CONFIG_STRATUM_FALLBACK_URL, CONFIG_STRATUM_FALLBACK_URL); }
    inline char* getStratumFallbackUser() { return nvs_config_get_string(NVS_CONFIG_STRATUM_FALLBACK_USER, CONFIG_STRATUM_FALLBACK_USER); }
    inline char* getStratumFallbackPass() { return nvs_config_get_string(NVS_CONFIG_STRATUM_FALLBACK_PASS, CONFIG_STRATUM_FALLBACK_PW); }
    inline char* getInfluxURL() { return nvs_config_get_string(NVS_CONFIG_INFLUX_URL, CONFIG_INFLUX_URL); }
    inline char* getInfluxToken() { return nvs_config_get_string(NVS_CONFIG_INFLUX_TOKEN, CONFIG_INFLUX_TOKEN); }
    inline char* getInfluxBucket() { return nvs_config_get_string(NVS_CONFIG_INFLUX_BUCKET, CONFIG_INFLUX_BUCKET); }
    inline char* getInfluxOrg() { return nvs_config_get_string(NVS_CONFIG_INFLUX_ORG, CONFIG_INFLUX_ORG); }
    inline char* getInfluxPrefix() { return nvs_config_get_string(NVS_CONFIG_INFLUX_PREFIX, CONFIG_INFLUX_PREFIX); }
    inline char* getSwarmConfig() { return nvs_config_get_string(NVS_CONFIG_SWARM, ""); }
    inline char* getDiscordWebhook() { return nvs_config_get_string(NVS_CONFIG_ALERT_DISCORD_URL, CONFIG_ALERT_DISCORD_URL); }

    // ---- String Setters ----
    inline void setWifiSSID(const char* value) { nvs_config_set_string(NVS_CONFIG_WIFI_SSID, value); }
    inline void setWifiPass(const char* value) { nvs_config_set_string(NVS_CONFIG_WIFI_PASS, value); }
    inline void setHostname(const char* value) { nvs_config_set_string(NVS_CONFIG_HOSTNAME, value); }
    inline void setStratumURL(const char* value) { nvs_config_set_string(NVS_CONFIG_STRATUM_URL, value); }
    inline void setStratumUser(const char* value) { nvs_config_set_string(NVS_CONFIG_STRATUM_USER, value); }
    inline void setStratumPass(const char* value) { nvs_config_set_string(NVS_CONFIG_STRATUM_PASS, value); }
    inline void setStratumFallbackURL(const char* value) { nvs_config_set_string(NVS_CONFIG_STRATUM_FALLBACK_URL, value); }
    inline void setStratumFallbackUser(const char* value) { nvs_config_set_string(NVS_CONFIG_STRATUM_FALLBACK_USER, value); }
    inline void setStratumFallbackPass(const char* value) { nvs_config_set_string(NVS_CONFIG_STRATUM_FALLBACK_PASS, value); }
    inline void setInfluxURL(const char* value) { nvs_config_set_string(NVS_CONFIG_INFLUX_URL, value); }
    inline void setInfluxToken(const char* value) { nvs_config_set_string(NVS_CONFIG_INFLUX_TOKEN, value); }
    inline void setInfluxBucket(const char* value) { nvs_config_set_string(NVS_CONFIG_INFLUX_BUCKET, value); }
    inline void setInfluxOrg(const char* value) { nvs_config_set_string(NVS_CONFIG_INFLUX_ORG, value); }
    inline void setInfluxPrefix(const char* value) { nvs_config_set_string(NVS_CONFIG_INFLUX_PREFIX, value); }
    inline void setSwarmConfig(const char* value) { nvs_config_set_string(NVS_CONFIG_SWARM, value); }
    inline void setDiscordWebhook(const char* value) { nvs_config_set_string(NVS_CONFIG_ALERT_DISCORD_URL, value); }

    // ---- uint16_t Getters ----
    inline uint16_t getStratumPortNumber() { return nvs_config_get_u16(NVS_CONFIG_STRATUM_PORT, CONFIG_STRATUM_PORT); }
    inline uint16_t getStratumFallbackPortNumber() { return nvs_config_get_u16(NVS_CONFIG_STRATUM_FALLBACK_PORT, CONFIG_STRATUM_FALLBACK_PORT); }
    inline uint16_t getFanSpeed() { return nvs_config_get_u16(NVS_CONFIG_FAN_SPEED, CONFIG_FAN_SPEED); }
    inline uint16_t getOverheatTemp() { return nvs_config_get_u16(NVS_CONFIG_OVERHEAT_TEMP, CONFIG_OVERHEAT_TEMP); }
    inline uint16_t getInfluxPort() { return nvs_config_get_u16(NVS_CONFIG_INFLUX_PORT, CONFIG_INFLUX_PORT); }
    inline uint16_t getTempControlMode() { return nvs_config_get_u16(NVS_CONFIG_AUTO_FAN_SPEED, CONFIG_AUTO_FAN_SPEED_VALUE); }
    inline uint16_t getPoolMode() { return nvs_config_get_u16(NVS_CONFIG_POOL_MODE, 0); }
    inline uint16_t getPoolBalance() { return nvs_config_get_u16(NVS_CONFIG_POOL_MODE_BALANCE, 50); }

    // ---- uint16_t Setters ----
    inline void setAsicFrequency(uint16_t value) { nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, value); }
    inline void setAsicVoltage(uint16_t value) { nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, value); }
    inline void setAsicJobInterval(uint16_t value) { nvs_config_set_u16(NVS_CONFIG_ASIC_JOB_INTERVAL, value); }
    inline void setStratumPortNumber(uint16_t value) { nvs_config_set_u16(NVS_CONFIG_STRATUM_PORT, value); }
    inline void setStratumFallbackPortNumber(uint16_t value) { nvs_config_set_u16(NVS_CONFIG_STRATUM_FALLBACK_PORT, value); }
    inline void setFanSpeed(uint16_t value) { nvs_config_set_u16(NVS_CONFIG_FAN_SPEED, value); }
    inline void setOverheatTemp(uint16_t value) { nvs_config_set_u16(NVS_CONFIG_OVERHEAT_TEMP, value); }
    inline void setInfluxPort(uint16_t value) { nvs_config_set_u16(NVS_CONFIG_INFLUX_PORT, value); }
    inline void setTempControlMode(uint16_t value) { nvs_config_set_u16(NVS_CONFIG_AUTO_FAN_SPEED, value); }
    inline void setPoolMode(uint16_t value) { nvs_config_set_u16(NVS_CONFIG_POOL_MODE, value); }
    inline void setPoolBalance(uint16_t value) { nvs_config_set_u16(NVS_CONFIG_POOL_MODE_BALANCE, value); }

    inline void setPidTargetTemp(uint16_t value) { nvs_config_set_u16(NVS_CONFIG_PID_TARGET_TEMP, value); }
    inline void setPidP(uint16_t value) { nvs_config_set_u16(NVS_CONFIG_PID_P, value); }
    inline void setPidI(uint16_t value) { nvs_config_set_u16(NVS_CONFIG_PID_I, value); }
    inline void setPidD(uint16_t value) { nvs_config_set_u16(NVS_CONFIG_PID_D, value); }

    // Indexed fan-channel getters (ch=0 → ch0 NVS keys, ch=1 → fan1 NVS keys)
    // ch0 defaults: mode=CONFIG_AUTO_FAN_SPEED_VALUE, speed=CONFIG_FAN_SPEED, overheat=CONFIG_OVERHEAT_TEMP
    // ch1 defaults: mode=3 (linked), speed=100%, overheat=80°C
    inline uint16_t getFanMode(int ch) {
        return ch == 0 ? nvs_config_get_u16(NVS_CONFIG_AUTO_FAN_SPEED, CONFIG_AUTO_FAN_SPEED_VALUE)
                       : nvs_config_get_u16(NVS_CONFIG_FAN1_MODE, 3);
    }
    inline uint16_t getFanManualSpeed(int ch) {
        return ch == 0 ? nvs_config_get_u16(NVS_CONFIG_FAN_SPEED, CONFIG_FAN_SPEED)
                       : nvs_config_get_u16(NVS_CONFIG_FAN1_SPEED, 100);
    }
    inline uint16_t getFanOverheatTemp(int ch) {
        return ch == 0 ? nvs_config_get_u16(NVS_CONFIG_OVERHEAT_TEMP, CONFIG_OVERHEAT_TEMP)
                       : nvs_config_get_u16(NVS_CONFIG_FAN1_OVERHEAT, CONFIG_OVERHEAT_TEMP);
    }
    inline uint16_t getFanPidTargetTemp(int ch, uint16_t d) {
        return ch == 0 ? nvs_config_get_u16(NVS_CONFIG_PID_TARGET_TEMP, d)
                       : nvs_config_get_u16(NVS_CONFIG_FAN1_PID_TEMP, d);
    }
    inline uint16_t getFanPidP(int ch, uint16_t d) {
        return ch == 0 ? nvs_config_get_u16(NVS_CONFIG_PID_P, d)
                       : nvs_config_get_u16(NVS_CONFIG_FAN1_PID_P, d);
    }
    inline uint16_t getFanPidI(int ch, uint16_t d) {
        return ch == 0 ? nvs_config_get_u16(NVS_CONFIG_PID_I, d)
                       : nvs_config_get_u16(NVS_CONFIG_FAN1_PID_I, d);
    }
    inline uint16_t getFanPidD(int ch, uint16_t d) {
        return ch == 0 ? nvs_config_get_u16(NVS_CONFIG_PID_D, d)
                       : nvs_config_get_u16(NVS_CONFIG_FAN1_PID_D, d);
    }

    // Indexed fan-channel setters
    inline void setFanMode(int ch, uint16_t v) {
        if (ch == 0) nvs_config_set_u16(NVS_CONFIG_AUTO_FAN_SPEED, v);
        else         nvs_config_set_u16(NVS_CONFIG_FAN1_MODE, v);
    }
    inline void setFanManualSpeed(int ch, uint16_t v) {
        if (ch == 0) nvs_config_set_u16(NVS_CONFIG_FAN_SPEED, v);
        else         nvs_config_set_u16(NVS_CONFIG_FAN1_SPEED, v);
    }
    inline void setFanOverheatTemp(int ch, uint16_t v) {
        if (ch == 0) nvs_config_set_u16(NVS_CONFIG_OVERHEAT_TEMP, v);
        else         nvs_config_set_u16(NVS_CONFIG_FAN1_OVERHEAT, v);
    }
    inline void setFanPidTargetTemp(int ch, uint16_t v) {
        if (ch == 0) nvs_config_set_u16(NVS_CONFIG_PID_TARGET_TEMP, v);
        else         nvs_config_set_u16(NVS_CONFIG_FAN1_PID_TEMP, v);
    }
    inline void setFanPidP(int ch, uint16_t v) {
        if (ch == 0) nvs_config_set_u16(NVS_CONFIG_PID_P, v);
        else         nvs_config_set_u16(NVS_CONFIG_FAN1_PID_P, v);
    }
    inline void setFanPidI(int ch, uint16_t v) {
        if (ch == 0) nvs_config_set_u16(NVS_CONFIG_PID_I, v);
        else         nvs_config_set_u16(NVS_CONFIG_FAN1_PID_I, v);
    }
    inline void setFanPidD(int ch, uint16_t v) {
        if (ch == 0) nvs_config_set_u16(NVS_CONFIG_PID_D, v);
        else         nvs_config_set_u16(NVS_CONFIG_FAN1_PID_D, v);
    }

    // ---- uint64_t Getters ----
    inline uint64_t getBestDiff() { return nvs_config_get_u64(NVS_CONFIG_BEST_DIFF, 0); }
    inline uint32_t getStratumDifficulty() { return (uint32_t) nvs_config_get_u64(NVS_CONFIG_STRATUM_DIFFICULTY, CONFIG_STRATUM_DIFFICULTY); }
    inline uint32_t getTotalFoundBlocks() { return (uint32_t) nvs_config_get_u64(NVS_TOTAL_FOUND_BLOCKS, 0); }

    // ---- uint64_t Setters ----
    inline void setBestDiff(uint64_t value) { nvs_config_set_u64(NVS_CONFIG_BEST_DIFF, value); }
    inline void setStratumDifficulty(uint32_t value) { nvs_config_set_u64(NVS_CONFIG_STRATUM_DIFFICULTY, value); }
    inline void setTotalFoundBlocks(uint32_t value) { nvs_config_set_u64(NVS_TOTAL_FOUND_BLOCKS, value); }
    inline void setVrFrequency(uint32_t value) { nvs_config_set_u64(NVS_CONFIG_VR_FREQUENCY, value); }

    // ---- Boolean Getters (Stored as uint16_t but used as bool) ----
    inline bool isInvertScreenEnabled() { return nvs_config_get_u16(NVS_CONFIG_INVERT_SCREEN, 0) != 0; } // todo unused?
    inline bool isSelfTestEnabled() { return nvs_config_get_u16(NVS_CONFIG_SELF_TEST, 0) != 0; }
    inline bool isAutoScreenOffEnabled() { return nvs_config_get_u16(NVS_CONFIG_AUTO_SCREEN_OFF, CONFIG_AUTO_SCREEN_OFF_VALUE) != 0; }
    inline bool isInfluxEnabled() { return nvs_config_get_u16(NVS_CONFIG_INFLUX_ENABLE, CONFIG_INFLUX_ENABLE_VALUE) != 0; }
    inline bool isDiscordWatchdogAlertEnabled() { return nvs_config_get_u16(NVS_CONFIG_ALERT_DISCORD_WATCHDOG_ENABLE, CONFIG_ALERT_DISCORD_WATCHDOG_ENABLE_VALUE) != 0; }
    inline bool isDiscordBlockFoundAlertEnabled() { return nvs_config_get_u16(NVS_CONFIG_ALERT_DISCORD_BLOCK_FOUND_ENABLE, CONFIG_ALERT_DISCORD_BLOCK_FOUND_ENABLE_VALUE) != 0; }
    inline bool isDiscordBestDiffAlertEnabled() { return nvs_config_get_u16(NVS_CONFIG_ALERT_DISCORD_BEST_DIFF, CONFIG_ALERT_DISCORD_BEST_DIFF_ENABLE_VALUE) != 0; }
    inline bool isStratumKeepaliveEnabled() { return nvs_config_get_u16(NVS_CONFIG_STRATUM_KEEPALIVE, CONFIG_STRATUM_KEEPALIVE_ENABLE_VALUE) != 0; }
    inline bool isStratumEnonceSubscribe() { return nvs_config_get_u16(NVS_CONFIG_STRATUM_ENONCE_SUB, CONFIG_STRATUM_ENONCE_SUBSCRIBE_VALUE) != 0; }
    inline bool isStratumFallbackEnonceSubscribe() { return nvs_config_get_u16(NVS_CONFIG_STRATUM_FALLBACK_ENONCE_SUB, CONFIG_STRATUM_FALLBACK_ENONCE_SUBSCRIBE_VALUE) != 0; }
    inline bool isStratumTLS() { return nvs_config_get_u16(NVS_CONFIG_STRATUM_TLS, CONFIG_STRATUM_TLS_VALUE) != 0; }
    inline bool isStratumFallbackTLS() { return nvs_config_get_u16(NVS_CONFIG_STRATUM_FALLBACK_TLS, CONFIG_STRATUM_FALLBACK_TLS_VALUE) != 0; }
    inline bool isShowBlockFoundEnabled() { return nvs_config_get_u16(NVS_CONFIG_SHOW_BLOCK_FOUND_ENABLE, CONFIG_SHOW_BLOCK_FOUND_ENABLE_VALUE) != 0; }

    // Stratum V2
    inline uint16_t getStratumProtocol() { return nvs_config_get_u16(NVS_CONFIG_STRATUM_PROTOCOL, 0); }
    inline void setStratumProtocol(uint16_t value) { nvs_config_set_u16(NVS_CONFIG_STRATUM_PROTOCOL, value); }
    inline uint16_t getFallbackStratumProtocol() { return nvs_config_get_u16(NVS_CONFIG_FB_STRATUM_PROTOCOL, 0); }
    inline void setFallbackStratumProtocol(uint16_t value) { nvs_config_set_u16(NVS_CONFIG_FB_STRATUM_PROTOCOL, value); }
    inline char* getSV2AuthorityPubkey() { return nvs_config_get_string(NVS_CONFIG_SV2_AUTHORITY_PUBKEY, ""); }
    inline void setSV2AuthorityPubkey(const char* value) { nvs_config_set_string(NVS_CONFIG_SV2_AUTHORITY_PUBKEY, value); }
    inline char* getFallbackSV2AuthorityPubkey() { return nvs_config_get_string(NVS_CONFIG_FB_SV2_AUTHORITY_PUBKEY, ""); }
    inline void setFallbackSV2AuthorityPubkey(const char* value) { nvs_config_set_string(NVS_CONFIG_FB_SV2_AUTHORITY_PUBKEY, value); }
    inline uint16_t getSV2ChannelType() { return nvs_config_get_u16(NVS_CONFIG_SV2_CHANNEL_TYPE, 0); }
    inline void setSV2ChannelType(uint16_t value) { nvs_config_set_u16(NVS_CONFIG_SV2_CHANNEL_TYPE, value); }
    inline uint16_t getFallbackSV2ChannelType() { return nvs_config_get_u16(NVS_CONFIG_FB_SV2_CHANNEL_TYPE, 0); }
    inline void setFallbackSV2ChannelType(uint16_t value) { nvs_config_set_u16(NVS_CONFIG_FB_SV2_CHANNEL_TYPE, value); }

    // ---- Boolean Setters ----
    inline void setFlipScreen(bool value) { nvs_config_set_u16(NVS_CONFIG_FLIP_SCREEN, value ? 1 : 0); }
    inline void setInvertScreen(bool value) { nvs_config_set_u16(NVS_CONFIG_INVERT_SCREEN, value ? 1 : 0); }
    inline void setFanPolarity(bool value) { nvs_config_set_u16(NVS_CONFIG_FAN_PWM_POLARITY, value ? 1 : 0); }
    inline void setSelfTest(bool value) { nvs_config_set_u16(NVS_CONFIG_SELF_TEST, value ? 1 : 0); }
    inline void setAutoScreenOff(bool value) { nvs_config_set_u16(NVS_CONFIG_AUTO_SCREEN_OFF, value ? 1 : 0); }
    inline void setInfluxEnabled(bool value) { nvs_config_set_u16(NVS_CONFIG_INFLUX_ENABLE, value ? 1 : 0); }
    inline void setDiscordWatchdogAlertEnabled(bool value) { nvs_config_set_u16(NVS_CONFIG_ALERT_DISCORD_WATCHDOG_ENABLE, value ? 1 : 0); }
    inline void setDiscordAlertBlockFoundEnabled(bool value) { nvs_config_set_u16(NVS_CONFIG_ALERT_DISCORD_BLOCK_FOUND_ENABLE, value ? 1 : 0); }
    inline void setDiscordAlertBestDiffEnabled(bool value) { nvs_config_set_u16(NVS_CONFIG_ALERT_DISCORD_BEST_DIFF, value ? 1 : 0); }
    inline void setStratumKeepaliveEnabled(bool value) { nvs_config_set_u16(NVS_CONFIG_STRATUM_KEEPALIVE, value ? 1 : 0); }
    inline void setStratumEnonceSubscribe(bool value) { nvs_config_set_u16(NVS_CONFIG_STRATUM_ENONCE_SUB, value ? 1 : 0); }
    inline void setStratumFallbackEnonceSubscribe(bool value) { nvs_config_set_u16(NVS_CONFIG_STRATUM_FALLBACK_ENONCE_SUB, value ? 1 : 0); }
    inline void setStratumTLS(bool value) { nvs_config_set_u16(NVS_CONFIG_STRATUM_TLS, value ? 1 : 0); }
    inline void setStratumFallbackTLS(bool value) { nvs_config_set_u16(NVS_CONFIG_STRATUM_FALLBACK_TLS, value ? 1 : 0); }
    inline void setShowBlockFoundEnabled(bool value) { nvs_config_set_u16(NVS_CONFIG_SHOW_BLOCK_FOUND_ENABLE, value ? 1 : 0); }

    // with board specific default values
    inline uint16_t getAsicFrequency(uint16_t d) { return nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ, d); }
    inline uint16_t getAsicVoltage(uint16_t d) { return nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, d); }
    inline uint16_t getAsicJobInterval(uint16_t d) { return nvs_config_get_u16(NVS_CONFIG_ASIC_JOB_INTERVAL, d); }
    inline bool isFlipScreenEnabled(bool d) { return nvs_config_get_u16(NVS_CONFIG_FLIP_SCREEN, d ? 1 : 0) != 0; }
    inline bool isFanPolarity(bool d) { return nvs_config_get_u16(NVS_CONFIG_FAN_PWM_POLARITY, d ? 1 : 0) != 0; }
    inline uint16_t getPidTargetTemp(uint16_t d) { return nvs_config_get_u16(NVS_CONFIG_PID_TARGET_TEMP, d); }
    inline uint16_t getPidP(uint16_t d) { return nvs_config_get_u16(NVS_CONFIG_PID_P, d); }
    inline uint16_t getPidI(uint16_t d) { return nvs_config_get_u16(NVS_CONFIG_PID_I, d); }
    inline uint16_t getPidD(uint16_t d) { return nvs_config_get_u16(NVS_CONFIG_PID_D, d); }
    inline uint32_t getVrFrequency(uint32_t d) { return (uint32_t) nvs_config_get_u64(NVS_CONFIG_VR_FREQUENCY, d); }

    // OTP Replay-Protection state (last_step + 3-bit mask)
    inline void getOTPReplayState(int64_t& base_step, uint8_t& mask) {
        base_step = (int64_t) nvs_config_get_u64(NVS_CONFIG_OTP_LAST_STEP, 0ULL);
        mask      = (uint8_t) nvs_config_get_u16(NVS_CONFIG_OTP_USED_MASK, 0);
    }

    inline void setOTPReplayState(int64_t base_step, uint8_t mask) {
        nvs_config_set_u64(NVS_CONFIG_OTP_LAST_STEP, (uint64_t) base_step);
        nvs_config_set_u16(NVS_CONFIG_OTP_USED_MASK, (uint16_t)(mask & 0x07));
    }

    inline void setOTPSecret(const char* value) { nvs_config_set_string(NVS_CONFIG_OTP_SECRET, value); }
    inline char* getOTPSecret() { return nvs_config_get_string(NVS_CONFIG_OTP_SECRET, ""); }

    inline void setOTTBootId(uint32_t boot_id) { nvs_config_set_u64(NVS_CONFIG_OTP_BOOT_ID, boot_id); }
    inline uint32_t getOTPBootId() { return (uint32_t) nvs_config_get_u64(NVS_CONFIG_OTP_BOOT_ID, 0); }

    inline void setOTPSessionKey(const char* value) { nvs_config_set_string(NVS_CONFIG_OTP_SESSION_KEY, value); }
    inline char* getOTPSessionKey() { return nvs_config_get_string(NVS_CONFIG_OTP_SESSION_KEY, ""); }

    inline void setOTPEnabled(bool value) { nvs_config_set_u16(NVS_CONFIG_OTP_ENABLED, value ? 1 : 0); }
    inline bool isOTPEnabled() { return nvs_config_get_u16(NVS_CONFIG_OTP_ENABLED, 0) != 0; }

    // ---- Thermal / electrical governor ----
    // Defaults mirror a calibration sweep measured on the real hardware. A
    // limit of 0 disables that constraint; the governor itself is off by default.
    inline uint16_t getGovEnable()    { return nvs_config_get_u16(NVS_CONFIG_GV_ENABLE, 0); }
    inline uint16_t getGovVrTarget()  { return nvs_config_get_u16(NVS_CONFIG_GV_VR_TARGET, 78); }
    inline uint16_t getGovVriTarget() { return nvs_config_get_u16(NVS_CONFIG_GV_VRI_TARGET, 90); }
    inline uint16_t getGovIoutMax()   { return nvs_config_get_u16(NVS_CONFIG_GV_IOUT_MAX, 92); }
    inline uint16_t getGovVinMinMv()  { return nvs_config_get_u16(NVS_CONFIG_GV_VIN_MIN, 11500); }
    inline uint16_t getGovPinMax()    { return nvs_config_get_u16(NVS_CONFIG_GV_PIN_MAX, 0); }
    inline uint16_t getGovIinMaxDA()  { return nvs_config_get_u16(NVS_CONFIG_GV_IIN_MAX, 0); }
    inline uint16_t getGovFmin()      { return nvs_config_get_u16(NVS_CONFIG_GV_FMIN, 500); }
    inline uint16_t getGovVmin()      { return nvs_config_get_u16(NVS_CONFIG_GV_VMIN, 1100); }
    inline uint16_t getGovVsagMv()    { return nvs_config_get_u16(NVS_CONFIG_GV_VSAG_MV, 50); }
    inline uint16_t getGovHrMinPerc() { return nvs_config_get_u16(NVS_CONFIG_GV_HR_MIN, 88); }
    inline char*    getGovCurve()     { return nvs_config_get_string(NVS_CONFIG_GV_CURVE, "500:1100,725:1210,800:1250"); }

    inline void setGovEnable(uint16_t v)    { nvs_config_set_u16(NVS_CONFIG_GV_ENABLE, v); }
    inline void setGovVrTarget(uint16_t v)  { nvs_config_set_u16(NVS_CONFIG_GV_VR_TARGET, v); }
    inline void setGovVriTarget(uint16_t v) { nvs_config_set_u16(NVS_CONFIG_GV_VRI_TARGET, v); }
    inline void setGovIoutMax(uint16_t v)   { nvs_config_set_u16(NVS_CONFIG_GV_IOUT_MAX, v); }
    inline void setGovVinMinMv(uint16_t v)  { nvs_config_set_u16(NVS_CONFIG_GV_VIN_MIN, v); }
    inline void setGovPinMax(uint16_t v)    { nvs_config_set_u16(NVS_CONFIG_GV_PIN_MAX, v); }
    inline void setGovIinMaxDA(uint16_t v)  { nvs_config_set_u16(NVS_CONFIG_GV_IIN_MAX, v); }
    inline void setGovFmin(uint16_t v)      { nvs_config_set_u16(NVS_CONFIG_GV_FMIN, v); }
    inline void setGovVmin(uint16_t v)      { nvs_config_set_u16(NVS_CONFIG_GV_VMIN, v); }
    inline void setGovVsagMv(uint16_t v)    { nvs_config_set_u16(NVS_CONFIG_GV_VSAG_MV, v); }
    inline void setGovHrMinPerc(uint16_t v) { nvs_config_set_u16(NVS_CONFIG_GV_HR_MIN, v); }
    inline void setGovCurve(const char* v)  { nvs_config_set_string(NVS_CONFIG_GV_CURVE, v); }

    // ---- Display data layer (main/ui_data.*) ----
    // tarifa: cents/kWh (u16, so it also has NO decimals lost like a float
    // would in NVS); the display/API divide by 100 to get R$/kWh.
    inline uint16_t getTarifaCents()  { return nvs_config_get_u16(NVS_CONFIG_TARIFA, 95); }
    inline void     setTarifaCents(uint16_t v) { nvs_config_set_u16(NVS_CONFIG_TARIFA, v); }

    // scr_mask: bit N (N = screen number) = 1 means screen N rotates in.
    // Default: screens 1,4,5,6,7,10,11,12,13,14,15,16,18,19,20.
    inline uint32_t getScrMaskDefault() {
        uint32_t m = 0;
        const uint8_t screens[] = {1, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15, 16, 18, 19, 20};
        for (uint8_t s : screens) m |= (1u << s);
        return m;
    }
    inline uint32_t getScrMask() { return (uint32_t) nvs_config_get_u64(NVS_CONFIG_SCR_MASK, getScrMaskDefault()); }
    inline void     setScrMask(uint32_t v) { nvs_config_set_u64(NVS_CONFIG_SCR_MASK, v); }

    inline uint16_t getScrSecs() { return nvs_config_get_u16(NVS_CONFIG_SCR_SECS, 10); }
    inline void     setScrSecs(uint16_t v) { nvs_config_set_u16(NVS_CONFIG_SCR_SECS, v); }

    // Per-screen duration override, 0 = "use scr_secs" for that slot. Stored
    // as one fixed-size blob (not one NVS key per screen) so a single
    // get/set pair covers all NVS_SCR_DURS_COUNT slots. Falls back to all-0
    // (every screen uses the global default) if the blob is missing or was
    // written by an older firmware with a different size.
    inline void getScrDurations(uint16_t out[NVS_SCR_DURS_COUNT]) {
        if (!nvs_config_get_blob(NVS_CONFIG_SCR_DURS, out, sizeof(uint16_t) * NVS_SCR_DURS_COUNT)) {
            for (int i = 0; i < NVS_SCR_DURS_COUNT; i++) out[i] = 0;
        }
    }
    inline void setScrDurations(const uint16_t in[NVS_SCR_DURS_COUNT]) {
        nvs_config_set_blob(NVS_CONFIG_SCR_DURS, in, sizeof(uint16_t) * NVS_SCR_DURS_COUNT);
    }

    // Power-bill currency symbol (GET/PATCH /api/system/screens's
    // "powerBill.currency"), e.g. "R$", "$", "€".
    inline char* getCurrency() { return nvs_config_get_string(NVS_CONFIG_CURRENCY, "R$"); }
    inline void  setCurrency(const char* v) { nvs_config_set_string(NVS_CONFIG_CURRENCY, v); }

    // Convenience wrapper: tarifa is stored as integer cents/kWh (see
    // getTarifaCents above); the power-bill API works in the same
    // currency's whole units per kWh.
    inline float getTarifaPerKwh() { return (float) getTarifaCents() / 100.0f; }
    inline void  setTarifaPerKwh(float v) { setTarifaCents((uint16_t) (v * 100.0f + 0.5f)); }

    inline char* getTz() { return nvs_config_get_string(NVS_CONFIG_TZ, "<-03>3"); }
    inline void  setTz(const char* v) { nvs_config_set_string(NVS_CONFIG_TZ, v); }

    // internal: kWh-today persistence (written at most every 30 min by
    // uiDataTick(), see main/ui_data.cpp)
    inline uint64_t getUiEnergyWh()      { return nvs_config_get_u64(NVS_CONFIG_UI_KWH_WH, 0); }
    inline void     setUiEnergyWh(uint64_t v)    { nvs_config_set_u64(NVS_CONFIG_UI_KWH_WH, v); }
    inline uint64_t getUiEnergyEpochS()  { return nvs_config_get_u64(NVS_CONFIG_UI_KWH_EPOCH, 0); }
    inline void     setUiEnergyEpochS(uint64_t v) { nvs_config_set_u64(NVS_CONFIG_UI_KWH_EPOCH, v); }

    void migrate_config();
}
