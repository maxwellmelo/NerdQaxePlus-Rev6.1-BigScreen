#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include "connect.h"

    void MINER_set_wifi_status(wifi_status_t status, uint16_t retry_count);
    void MINER_set_ap_status(bool state);

#ifdef __cplusplus
}
#endif

void self_test(void *pvParameters);

// Selects the `factory` app partition for the next boot. On IDF 5.3.3 this is
// done by erasing the whole otadata partition (see the implementation), so it
// is also the recovery path when an OTA image in ota_0/ota_1 crash-loops.
// Used by the crash-loop guard and by POST /api/system/bootfactory.
esp_err_t switch_to_factory_partition();
