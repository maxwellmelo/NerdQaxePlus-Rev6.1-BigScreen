#pragma once

esp_err_t GET_system_info(httpd_req_t *req);
esp_err_t PATCH_update_settings(httpd_req_t *req);

esp_err_t GET_system_asic(httpd_req_t *req);
esp_err_t POST_reset_stats(httpd_req_t *req);
esp_err_t POST_boot_factory(httpd_req_t *req);

// Per-screen display duration + power-bill config: time on screen, live
// enable/disable and the currency/price used by the "Power bill" screen.
esp_err_t GET_system_screens(httpd_req_t *req);
esp_err_t PATCH_update_screens(httpd_req_t *req);