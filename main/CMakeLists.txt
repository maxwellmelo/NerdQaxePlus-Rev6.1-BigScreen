# Mining-state screen rodizio (main/displays/screens/): hardcoded to the
# 480x320 layout and only ever wired up from displayDriver.cpp under
# #ifdef DISPLAY_PROFILE_YYSLUPING_480X320, so it is only worth compiling
# (and only worth the flash) on that profile. ui_state.cpp/ui_data_weak.cpp
# stay OUT of this list and always in SRCS below: they are plain data
# structures/link-safety stubs with nothing 480x320-specific, and
# main/http_server/handler_system.cpp (built for every profile) already
# depends on the ui_data.h symbols ui_data_weak.cpp stubs.
set(SCREEN_ROTATION_SRCS "")
if(NERDQAXE_DISPLAY_PROFILE STREQUAL "YYSLUPING_480X320")
    # Screens (sNN_*.cpp) and their bitmap fonts are picked up by glob, so
    # porting a screen or adding a font never touches this file (several
    # screens are ported in parallel). CONFIGURE_DEPENDS re-runs the glob on
    # every build.
    file(GLOB NQ_SCREEN_SRCS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/displays/screens/s[0-9][0-9]_*.cpp")
    file(GLOB NQ_FONT_SRCS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/displays/fonts/*.c")
    list(APPEND SCREEN_ROTATION_SRCS
        "./displays/screens/screen_registry.cpp"
        "./displays/screens/screen_manager.cpp"
        ${NQ_SCREEN_SRCS}
        ${NQ_FONT_SRCS}
    )
endif()

# Legacy theme image set (main/displays/images/themes/themes.c): ~1.14 MB of
# PNG image data for 9 legacy themes x 7 images each, #include'd as a single
# translation unit. The 480x320 (YYSLUPING) profile's screens (screens/*.cpp)
# don't use Theme images at all, so on that profile themes.c is excluded from
# SRCS entirely to save flash/binary size. Theme.h gives every Theme member a
# `= nullptr` default and the ThemeXxx subclass constructor bodies are
# `#ifndef DISPLAY_PROFILE_YYSLUPING_480X320`-guarded (empty on this profile),
# so nothing in the 480x320 build still references the ui_img_* symbols that
# only exist inside themes.c, and it is safe to drop from the link.
set(THEMES_SRCS "")
if(NOT NERDQAXE_DISPLAY_PROFILE STREQUAL "YYSLUPING_480X320")
    list(APPEND THEMES_SRCS
        "./displays/images/themes/themes.c"
    )
endif()

idf_component_register(
SRCS
    "main.cpp"
    "nvs_config.cpp"
    "system.cpp"
    "sntp.cpp"
    "utils.cpp"
    "thermal_governor.cpp"
    "./ui_data.cpp"
    "boards/board.cpp"
    "boards/nerdaxe.cpp"
    "boards/nerdaxegamma.cpp"
    "boards/nerdqaxeplus.cpp"
    "boards/nerdqaxeplus2.cpp"
    "boards/nerdoctaxeplus.cpp"
    "boards/nerdoctaxegamma.cpp"
    "boards/nerdhaxegamma.cpp"
    "boards/nerdqx.cpp"
    "boards/nerdeko.cpp"
    "boards/drivers/EMC2302.cpp"
    "boards/drivers/TMP1075.cpp"
    "boards/drivers/TPS53647.cpp"
    "boards/drivers/TPS53667.cpp"
    "boards/drivers/nerdaxe/DS4432U.cpp"
    "boards/drivers/nerdaxe/INA260.cpp"
    "boards/drivers/nerdaxe/EMC2101.cpp"
    "boards/drivers/nerdaxe/TPS546.cpp"
    "boards/drivers/nerdaxe/adc.cpp"
    "boards/drivers/i2c_master.cpp"
    "boards/drivers/tmp451_mux.cpp"
    "history.cpp"
    "discord.cpp"
    "fan_controller.cpp"
    "./pid/PID_v1_bc.cpp"
    "./pid/pid_timer.cpp"
    "./http_server/http_server.cpp"
    "./http_server/http_cors.cpp"
    "./http_server/http_utils.cpp"
    "./http_server/http_websocket.cpp"
    "./http_server/handler_influx.cpp"
    "./http_server/handler_alert.cpp"
    "./http_server/handler_swarm.cpp"
    "./http_server/handler_system.cpp"
    "./http_server/handler_ota.cpp"
    "./http_server/handler_otp.cpp"
    "./http_server/handler_restart.cpp"
    "./http_server/handler_shutdown.cpp"
    "./http_server/handler_file.cpp"
    "./http_server/handler_ota_factory.cpp"
    "./self_test/self_test.cpp"
    "./stratum/stratum_api.cpp"
    "./stratum/stratum_transport.cpp"
    "./stratum/stratum_config.cpp"
    "./stratum/stratum_task.cpp"
    "./stratum/stratum_manager.cpp"
    "./stratum/stratum_manager_fallback.cpp"
    "./stratum/stratum_manager_dual_pool.cpp"
    "./stratum/stratum_transport_noise.cpp"
    "./stratum/stratum_task_v2.cpp"
    "./tasks/create_jobs_task.cpp"
    "./tasks/create_jobs_sv2.cpp"
    "./tasks/mining_info_v2.cpp"
    "./tasks/asic_result_task.cpp"
    "./tasks/influx_task.cpp"
    "./tasks/ping_task.cpp"
    "./tasks/power_management_task.cpp"
    "./tasks/hashrate_monitor_task.cpp"
    "./tasks/apis_task.cpp"
    "./tasks/wifi_health.cpp"
    "./displays/displayDriver.cpp"
    "./displays/ui.cpp"
    "./displays/ui_ipc.cpp"
    "./displays/button.cpp"
    "./displays/ui_helpers.cpp"
    "./displays/ui_state.cpp"
    "./displays/ui_data_weak.cpp"
    ${SCREEN_ROTATION_SRCS}
    "./displays/images/ui_font_DigitalNumbers16.c"
    "./displays/images/ui_font_DigitalNumbers28.c"
    "./displays/images/ui_font_DigitalNumbers16.c"
    "./displays/images/ui_font_OpenSansBold13.c"
    "./displays/images/ui_font_OpenSansBold14.c"
    "./displays/images/ui_font_OpenSansBold24.c"
    "./displays/images/ui_font_OpenSansBold45.c"
    "./displays/images/ui_font_vt323_21.c"
    "./displays/images/ui_font_vt323_35.c"
    "./displays/images/ui_img_found_block_png.c"
    "./displays/images/ui_img_safe_png.c"
    ${THEMES_SRCS}
    "./otp/otp.cpp"
    "./otp/qrcodegen.cpp"



INCLUDE_DIRS
    "."
    "tasks"
    "http_server"
    "displays"
    "displays/images"
    "boards"
    "boards/drivers"
    "../components/connect/include"
    "../components/dns_server/include"
    "../components/bm1397/include"
    "../components/influx/include"

REQUIRES
    "app_update"
    "driver"
    "esp_adc"
    "esp_app_format"
    "esp_event"
    "esp_http_server"
    "esp_http_client"
    "esp_https_ota"
    "esp_netif"
    "esp_timer"
    "esp_wifi"
    "esp_lcd"
    "esp_psram"
    "json"
    "tcp_transport"
    "nvs_flash"
    "spiffs"
    "vfs"
    "lvgl"
    "lwip"
    "esp-tls"
    "stratum_v2"
    "libsecp256k1"

)

if(NERDQAXE_DISPLAY_PROFILE STREQUAL "YYSLUPING_480X320")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE DISPLAY_PROFILE_YYSLUPING_480X320)
endif()


set(WEB_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/http_server/axe-os")

option(BUILD_WEB "Enable web UI build" ON)

if(BUILD_WEB)
    message(STATUS "Web UI build enabled")

    if("$ENV{GITHUB_ACTIONS}" STREQUAL "true")
        spiffs_create_partition_image(www ${WEB_SRC_DIR}/dist/axe-os FLASH_IN_PROJECT)
    else()
        find_program(NPM_EXECUTABLE npm)
        if(NOT NPM_EXECUTABLE AND NOT EXISTS ${WEB_SRC_DIR}/dist)
            message(FATAL_ERROR "npm is not found! Please install it to proceed.")
        endif()

        ExternalProject_Add(
            web_ui_dist
            PREFIX ${CMAKE_BINARY_DIR}/web_ui_dist
            SOURCE_DIR ${WEB_SRC_DIR}
            DOWNLOAD_COMMAND ""
            CONFIGURE_COMMAND ${NPM_EXECUTABLE} i
            BUILD_COMMAND ${NPM_EXECUTABLE} run build
            INSTALL_COMMAND ""
            BUILD_ALWAYS OFF
            BUILD_IN_SOURCE TRUE
        )

        add_dependencies(${COMPONENT_LIB} web_ui_dist)

        spiffs_create_partition_image(www ${WEB_SRC_DIR}/dist/axe-os FLASH_IN_PROJECT DEPENDS web_ui_dist)
    endif()
else()
    message(STATUS "Web UI build disabled – using prebuilt dist/ directory.")
    spiffs_create_partition_image(www ${WEB_SRC_DIR}/dist/axe-os FLASH_IN_PROJECT)
endif()
