#include <string.h>

#include "esp_attr.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_task_wdt.h"
#include "mbedtls/platform.h"
#include "nvs_flash.h"

#include "apis_task.h"
#include "asic_jobs.h"
#include "asic_result_task.h"
#include "boards/board.h"
#include "boards/nerdaxe.h"
#include "boards/nerdaxegamma.h"
#include "boards/nerdeko.h"
#include "boards/nerdhaxegamma.h"
#include "boards/nerdoctaxegamma.h"
#include "boards/nerdoctaxeplus.h"
#include "boards/nerdqaxeplus.h"
#include "boards/nerdqaxeplus2.h"
#include "boards/nerdqx.h"
#include "create_jobs_task.h"
#include "discord.h"
#include "global_state.h"
#include "hashrate_monitor_task.h"
#include "history.h"
#include "http_server.h"
#include "influx_task.h"
#include "macros.h"
#include "main.h"
#include "nvs_config.h"
#include "otp/otp.h"
#include "ping_task.h"
#include "serial.h"
#include "stratum/stratum_manager_fallback.h"
#include "stratum/stratum_manager_dual_pool.h"
#include "system.h"
#include "ui_data.h"
#include "wifi_health.h"
#include "guards.h"
#include "utils.h"

#define STRATUM_WATCHDOG_TIMEOUT_SECONDS 3600

System SYSTEM_MODULE;

PowerManagementTask POWER_MANAGEMENT_MODULE;
HashrateMonitor HASHRATE_MONITOR;

StratumManager *STRATUM_MANAGER = nullptr;
APIsFetcher APIs_FETCHER;
FactoryOTAUpdate FACTORY_OTA_UPDATER;

DiscordAlerter discordAlerter;

AsicJobs asicJobs;

OTP otp;
SNTP sntp;

static const char *TAG = "nerd*axe";

#ifndef CONFIG_SPIRAM
#error "firmware will not work without psram"
#endif

// ---------------------------------------------------------------------------
// Delivery safety net: crash-loop guard
// ---------------------------------------------------------------------------
//
// Counts consecutive reboots caused by a panic or a watchdog. RTC_NOINIT_ATTR
// survives a software reset but not a power cycle, so the magic validates the
// counter before it is trusted.
//
//   >= 2 crashes -> the governor is forced off for this session (RAM only,
//                   NVS untouched; reported as governor.forcedOff in the API)
//   >= 4 crashes -> the factory app is selected and the device reboots
//
// The counter is cleared after 10 minutes of stable uptime.

#define CRASH_GUARD_MAGIC 0x4E514747u // "NQGG"
#define CRASH_GUARD_GOVERNOR_OFF 2
#define CRASH_GUARD_FACTORY 4
#define CRASH_GUARD_STABLE_US (600ll * 1000000ll)

RTC_NOINIT_ATTR static uint32_t s_crashGuardMagic;
RTC_NOINIT_ATTR static uint32_t s_crashGuardCount;

esp_err_t switch_to_factory_partition()
{
    const esp_partition_t *factory =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (!factory) {
        ESP_LOGE(TAG, "no factory partition found");
        return ESP_ERR_NOT_FOUND;
    }

    // esp_ota_set_boot_partition() first validates the image and, for the
    // factory subtype, simply erases the whole otadata partition
    // (esp-idf 5.3.3, components/app_update/esp_ota_ops.c, lines 499-507:
    //  "if set boot partition to factory bin ,just format ota info partition"
    //  -> esp_partition_erase_range(otadata, 0, otadata->size)), which makes
    // the bootloader fall back to factory. An invalid factory image returns
    // ESP_ERR_OTA_VALIDATE_FAILED and erases nothing.
    esp_err_t err = esp_ota_set_boot_partition(factory);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition(factory) failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGW(TAG, "otadata erased - the next boot will run the factory app");
    return ESP_OK;
}

static uint32_t crash_guard_update(esp_reset_reason_t reason)
{
    if (s_crashGuardMagic != CRASH_GUARD_MAGIC) {
        s_crashGuardMagic = CRASH_GUARD_MAGIC;
        s_crashGuardCount = 0;
    }

    bool crashed = (reason == ESP_RST_PANIC) || (reason == ESP_RST_INT_WDT) || (reason == ESP_RST_TASK_WDT) ||
                   (reason == ESP_RST_WDT);
    if (crashed) {
        s_crashGuardCount++;
        ESP_LOGW(TAG, "crash-loop guard: %lu consecutive crash reboots", (unsigned long) s_crashGuardCount);
    }

    if (s_crashGuardCount >= CRASH_GUARD_FACTORY) {
        ESP_LOGE(TAG, "crash-loop guard: %d crashes - falling back to the factory app", CRASH_GUARD_FACTORY);
        // clear first so a failed attempt cannot turn into a reboot loop
        s_crashGuardCount = 0;
        if (switch_to_factory_partition() == ESP_OK) {
            esp_restart();
        }
    }

    return s_crashGuardCount;
}

static void crash_guard_clear_when_stable(int64_t bootUs)
{
    static bool cleared = false;
    if (cleared || s_crashGuardCount == 0) {
        return;
    }
    if (esp_timer_get_time() - bootUs < CRASH_GUARD_STABLE_US) {
        return;
    }
    s_crashGuardCount = 0;
    cleared = true;
    ESP_LOGI(TAG, "crash-loop guard: 10 min of stable uptime, counter reset");
}

uint64_t now_ms()
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    // Combine seconds + microseconds → milliseconds
    return (uint64_t) tv.tv_sec * 1000ULL + tv.tv_usec / 1000ULL;
}

uint32_t now()
{
    return (uint32_t) (now_ms() / 1000ull);
}

bool is_time_synced(void)
{
    return (sntp.isTimeSynced() && now() >= 1609459200);
}

static void setup_wifi()
{
    // pull the wifi credentials and hostname out of NVS
    char *wifi_ssid = Config::getWifiSSID();
    char *wifi_pass = Config::getWifiPass();
    char *hostname  = Config::getHostname();

    // release on exit of scope (RAII)
    MemoryGuard gSsid(wifi_ssid);
    MemoryGuard gWifiPass(wifi_pass);
    MemoryGuard gHostname(hostname);

    // copy the wifi ssid to the global state
    SYSTEM_MODULE.setSsid(wifi_ssid);

    // init and connect to wifi (asynchronous setup)
    wifi_init(wifi_ssid, wifi_pass, hostname);

    // start rest server (needed in AP fallback for config)
    start_rest_server(NULL);

    // initial blocking wait: CONNECTED or FAIL
    EventBits_t bits = wifi_connect();

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to SSID: %s", wifi_ssid);
        SYSTEM_MODULE.setWifiStatus("Connected!");
        return;
    }

    // Initial connect failed: stay in AP fallback, keep waiting for STA recovery
    ESP_LOGW(TAG, "Initial WiFi connect failed. Staying in AP fallback and waiting for STA recovery...");

    for (;;) {
        // Wait up to 30s for STA to come up (retries run in background)
        EventBits_t w = wifi_wait_connected_ms(pdMS_TO_TICKS(30000));

        if (w & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "STA recovered and connected to SSID: %s", wifi_ssid);
            SYSTEM_MODULE.setWifiStatus("Connected!");
            return; // continue normal init after this
        }

        // Heartbeat while waiting in AP mode
        ESP_LOGI(TAG, "Still waiting for STA to connect... (AP is available for config)");
    }
}

// Function to configure the Task Watchdog Timer (TWDT)
void initWatchdog()
{
    // Initialize the Task Watchdog Timer configuration
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = STRATUM_WATCHDOG_TIMEOUT_SECONDS * 1000, // Convert seconds to milliseconds
        .idle_core_mask = 0,                                   // No specific core
        .trigger_panic = true                                  // Enable panic on timeout
    };

    // Initialize the Task Watchdog Timer with the configuration
    esp_err_t result = esp_task_wdt_init(&wdt_config);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize watchdog: %d\n", result);
    }
}

StratumManager* newStratumManager() {
    int mode = (int) Config::getPoolMode();
    switch (mode) {
    case 0:
        return new StratumManagerFallback();
    case 1:
        return new StratumManagerDualPool();
    default:
        ESP_LOGE(TAG, "invalid pool mode %d", (int) mode);
        return nullptr;
    }
}

// Custom calloc function that allocates from PSRAM
void *psram_calloc(size_t num, size_t size)
{
    void *ptr = CALLOC(num, size);
    if (!ptr) {
        ESP_LOGE(TAG, "PSRAM allocation failed! Falling back to internal RAM.");
        ptr = heap_caps_calloc(num, size, MALLOC_CAP_DEFAULT);
    }
    return ptr;
}

void free_psram(void *ptr)
{
    heap_caps_free(ptr);
}

#if 0
const UBaseType_t max_tasks = 30;
TaskStatus_t task_list[max_tasks];

void monitor_all_task_watermarks() {
    UBaseType_t count = uxTaskGetSystemState(task_list, max_tasks, NULL);

    for (int i = 0; i < count; ++i) {
        ESP_LOGW("TASK_MON", "Task '%s': watermark = %lu words",
            task_list[i].pcTaskName,
            task_list[i].usStackHighWaterMark);

    }
}
#endif


extern "C" void app_main(void)
{
    initWatchdog();

    // use PSRAM because TLS costs a lot of internal RAM
    mbedtls_platform_set_calloc_free(psram_calloc, free_psram);

    ESP_LOGI(TAG, "Welcome to the Nerd*Axe - hack the planet!");
    ESP_ERROR_CHECK(nvs_flash_init());

    int64_t bootUs = esp_timer_get_time();

    // shows and saves last reset reason
    esp_reset_reason_t reason = SYSTEM_MODULE.showLastResetReason();

    // delivery safety net: may not return (falls back to factory on 4 crashes)
    uint32_t crashCount = crash_guard_update(reason);

    // migrate config
    Config::migrate_config();

#ifdef NERDQAXEPLUS
    Board *board = new NerdQaxePlus();
#endif
#ifdef NERDQAXEPLUS2
    Board *board = new NerdQaxePlus2();
#endif
#ifdef NERDOCTAXEPLUS
    Board *board = new NerdOctaxePlus();
#endif
#ifdef NERDAXE
    Board *board = new NerdAxe();
#endif
#ifdef NERDOCTAXEGAMMA
    Board *board = new NerdOctaxeGamma();
#endif
#ifdef NERDAXEGAMMA
    Board *board = new NerdaxeGamma();
#endif
#ifdef NERDHAXEGAMMA
    Board *board = new NerdHaxeGamma();
#endif
#ifdef NERDEKO
    Board *board = new NerdEko();
#endif
#ifdef NERDQX
    Board *board = new NerdQX();
#endif

    // initialize everything non-asic-specific like
    // fan and serial and load settings from nvs
    board->loadSettings();
    board->initBoard();


    SYSTEM_MODULE.setBoard(board);

    size_t total_psram = esp_psram_get_size();
    ESP_LOGI(TAG, "PSRAM found with %dMB", total_psram / (1024 * 1024));
    ESP_LOGI(TAG, "Found Device Model: %s", board->getDeviceModel());
    ESP_LOGI(TAG, "Found Board Version: %d", board->getVersion());

    uint64_t best_diff = Config::getBestDiff();
    bool should_self_test = Config::isSelfTestEnabled();
    if (should_self_test && !best_diff) {
        board->selfTest();
        vTaskDelay(pdMS_TO_TICKS(60 * 60 * 1000));
    }

    STRATUM_MANAGER = newStratumManager();

    if (!STRATUM_MANAGER) {
        ESP_LOGE(TAG, "stratumManager is null! main task suspended.");
        vTaskSuspend(NULL);
    }

    STRATUM_MANAGER->loadSettings();

    // Two crashes in a row: don't let the governor near the hardware in this
    // session. RAM only - the NVS setting is left untouched.
    if (crashCount >= CRASH_GUARD_GOVERNOR_OFF) {
        POWER_MANAGEMENT_MODULE.forceGovernorOff();
    }

    // Display data layer (main/ui_data.cpp): allocates its ~31KB internal
    // state in PSRAM. Must run before POWER_MANAGEMENT_MODULE's task starts,
    // since that task calls uiDataTick() every 2s from its own loop.
    uiDataInit();

    xTaskCreate(SYSTEM_MODULE.taskWrapper, "SYSTEM_task", 4096, &SYSTEM_MODULE, 3, NULL);
    xTaskCreate(POWER_MANAGEMENT_MODULE.taskWrapper, "power mangement", 8192, (void *) &POWER_MANAGEMENT_MODULE, 10, NULL);

    setup_wifi();

    // set the startup_done flag
    SYSTEM_MODULE.setStartupDone();


    // when a username is configured we will continue with startup and start mining
    const char *username = Config::nvs_config_get_string(NVS_CONFIG_STRATUM_USER, NULL); // TODO
    if (username) {
        // wifi is connected, switch the AP off
        wifi_softap_off();

        // start the discord alerter early
        discordAlerter.start();

        // initialize OTP
        if (!otp.init()) {
            ESP_LOGE(TAG, "error init otp");
        }

        // start SNTP
        sntp.start();

        // we only use alerting if we are in a normal operating mode
        if (reason == ESP_RST_TASK_WDT) {
            discordAlerter.sendWatchdogAlert();
        }

        // With the governor active the ASICs come up at gv_fmin with the curve
        // voltage instead of jumping straight to the saved (ceiling) pair. That
        // removes the ~130 W boot spike on a 120 W supply; the governor then
        // holds for 60 s (STARTUP) before it starts ramping.
        {
            int gvFreq = 0;
            int gvMillis = 0;
            if (!POWER_MANAGEMENT_MODULE.isGovernorForcedOff() &&
                PowerManagementTask::governorStartupPoint(board, gvFreq, gvMillis)) {
                ESP_LOGI(TAG, "governor active: starting the ASICs at %dMHz / %dmV (ceiling %dMHz / %dmV)", gvFreq,
                         gvMillis, board->getAsicFrequency(), board->getAsicVoltageMillis());
                board->setInitOperatingPoint(gvFreq, gvMillis);
            }
        }

        // and continue with initialization
        POWER_MANAGEMENT_MODULE.lock();
        if (!board->initAsics()) {
            ESP_LOGE(TAG, "error initializing board %s", board->getDeviceModel());
        }
        POWER_MANAGEMENT_MODULE.unlock();

        xTaskCreate(create_jobs_task, "stratum miner", 8192, NULL, 10, NULL);
        xTaskCreate(ASIC_result_task, "asic result", 8192, NULL, 15, NULL);
        xTaskCreate(influx_task, "influx", 8192, NULL, 1, NULL);
        xTaskCreatePSRAM(APIs_FETCHER.taskWrapper, "apis ticker", 8192, (void *) &APIs_FETCHER, 5, NULL);
        xTaskCreatePSRAM(wifi_monitor_task, "wifi monitor", 4096, NULL, 1, NULL);
        xTaskCreate(FACTORY_OTA_UPDATER.taskWrapper, "ota updater", 8192, (void *) &FACTORY_OTA_UPDATER, 1, NULL);
        xTaskCreate(StratumManager::taskWrapper, "stratum manager", 8192, (void *) STRATUM_MANAGER, 5, NULL);

        if (board->hasHashrateCounter()) {
            HASHRATE_MONITOR.start(board, board->getAsics());
        }
    }
    // char* taskList = (char*) malloc(8192);
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));

        crash_guard_clear_when_stable(bootUs);

        if (POWER_MANAGEMENT_MODULE.isShutdown()) {
            // not needed, we deregister the WDT in the stratumtask on shutdown
            //esp_task_wdt_deinit();
            ESP_LOGW(TAG, "suspended");
            vTaskSuspend(NULL);
        }

        size_t free_internal_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

        if (free_internal_heap < 10000) {
            ESP_LOGW(TAG, "*** WARNING *** Free internal heap: %d bytes", free_internal_heap);
        }
        // monitor_all_task_watermarks();
    }
}

void MINER_set_wifi_status(wifi_status_t status, uint16_t retry_count)
{
    switch (status) {
    case WIFI_CONNECTED: {
        SYSTEM_MODULE.setWifiStatus("Connected!");
        break;
    }
    case WIFI_RETRYING: {
        char buf[20];
        snprintf(buf, sizeof(buf), "Retrying: %d", retry_count);
        SYSTEM_MODULE.setWifiStatus(buf);
        break;
    }
    case WIFI_CONNECT_FAILED: {
        SYSTEM_MODULE.setWifiStatus("Connect Failed!");
        break;
    }
    case WIFI_DISCONNECTED:
    case WIFI_CONNECTING:
    case WIFI_DISCONNECTING: {
        // NOP
        break;
    }
    }
}

void MINER_set_ap_status(bool state) {
    SYSTEM_MODULE.setAPState(state);
}
