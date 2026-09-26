#include "global_state.h"
#include "ui_helpers.h"
#include "ui.h"

#include "displayDriver.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "../macros.h"


#pragma GCC diagnostic ignored "-Wdeprecated-enum-enum-conversion"

UI::UI() {
    m_last_screen_change_time = 0;
}

static const char *TAG="ui";

#ifdef DISPLAY_PROFILE_YYSLUPING_480X320
namespace {

constexpr uint32_t BIG_BG = 0x0E1113;
constexpr uint32_t BIG_HEADER = 0x181E22;
constexpr uint32_t BIG_PANEL = 0x1E2529;
constexpr uint32_t BIG_PANEL_ALT = 0x252E33;
constexpr uint32_t BIG_TEXT = 0xF4F7F8;
constexpr uint32_t BIG_MUTED = 0xA8B3B9;
constexpr uint32_t BIG_GREEN = 0x58D68D;
constexpr uint32_t BIG_ORANGE = 0xF7931A;
constexpr uint32_t BIG_CYAN = 0x54C7EC;

void bigScreenBase(lv_obj_t *screen)
{
    lv_obj_set_style_bg_color(screen, lv_color_hex(BIG_BG), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *bigPanel(lv_obj_t *screen, int x, int y, int width, int height, uint32_t color)
{
    lv_obj_t *panel = lv_obj_create(screen);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, width, height);
    lv_obj_set_style_bg_color(panel, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_background(panel);
    return panel;
}

lv_obj_t *bigText(lv_obj_t *parent, const char *text, int x, int y, int width,
                  const lv_font_t *font, uint32_t color, lv_text_align_t align = LV_TEXT_ALIGN_LEFT)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(label, align, LV_PART_MAIN | LV_STATE_DEFAULT);
    return label;
}

void bigValue(lv_obj_t *label, int x, int y, int width, const lv_font_t *font,
              uint32_t color, lv_text_align_t align = LV_TEXT_ALIGN_LEFT)
{
    lv_obj_set_align(label, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, LV_SIZE_CONTENT);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(label, align, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void bigRemoveLegacyImage(lv_obj_t *&image)
{
    if (image != nullptr) {
        lv_obj_del(image);
        image = nullptr;
    }
}

} // namespace

// Typographic splash for the big-screen (480x320) profile.
//
// Before: both splash screens drew a per-theme splash PNG
// (Theme::getInitscreen2()/getSplashscreen2(), zoomed/antialiased via
// lv_img_set_zoom/lv_img_set_antialias) picked up from `themes.c`.
//
// After: `themes.c` (~1.14 MB of legacy per-board theme PNGs across 9
// themes) is excluded from the link entirely on this profile (see
// main/CMakeLists.txt and themes.h), since none of the 480x320 screens use
// Theme images any more. So instead of an image we delete the legacy
// lv_img object (bigRemoveLegacyImage, same helper the other big-screen
// layouts already use) and draw the "NerdQAxe++" wordmark as plain LVGL
// text, in the same neutral system palette (BIG_BG/BIG_TEXT/BIG_MUTED)
// used by the rest of this profile's chrome.
//
// Font: deliberately ui_font_OpenSansBold45, the same built-in bitmap font
// already used elsewhere in this file (e.g. hashrate/BTC price), NOT the
// new custom display fonts (Instrument Serif / Saira Condensed / Archivo)
// another in-flight agent may still be generating - swapping the splash
// title to a custom font later is a one-line change once those .c font
// files exist and are wired into CMakeLists.txt.
void UI::applyBigScreenSplashLayout(lv_obj_t *screen, lv_obj_t *&image, const char *subtitle)
{
    bigScreenBase(screen);
    bigRemoveLegacyImage(image);

    bigText(screen, "NerdQAxe++", 0, 118, 480, &ui_font_OpenSansBold45, BIG_TEXT, LV_TEXT_ALIGN_CENTER);
    if (subtitle != nullptr) {
        bigText(screen, subtitle, 0, 188, 480, &ui_font_OpenSansBold14, BIG_MUTED, LV_TEXT_ALIGN_CENTER);
    }
}

void UI::applyBigScreenPortalLayout()
{
    bigScreenBase(ui_PortalScreen);
    bigRemoveLegacyImage(ui_Image1);

    lv_obj_t *header = bigPanel(ui_PortalScreen, 0, 0, 480, 58, BIG_HEADER);
    bigText(header, "NERDQAXE++", 18, 14, 220, &ui_font_OpenSansBold24, BIG_TEXT);
    bigText(header, "SETUP MODE", 300, 18, 160, &ui_font_OpenSansBold14, BIG_CYAN, LV_TEXT_ALIGN_RIGHT);

    lv_obj_t *body = bigPanel(ui_PortalScreen, 0, 58, 480, 262, BIG_PANEL);
    bigText(body, "CONNECT TO WI-FI", 28, 40, 424, &ui_font_OpenSansBold14, BIG_MUTED, LV_TEXT_ALIGN_CENTER);
    bigText(body, "Network", 28, 91, 424, &ui_font_OpenSansBold13, BIG_MUTED, LV_TEXT_ALIGN_CENTER);
    bigText(body, "Open 192.168.4.1 after connecting", 28, 174, 424,
            &ui_font_OpenSansBold14, BIG_TEXT, LV_TEXT_ALIGN_CENTER);

    bigValue(ui_lbSSID, 30, 166, 420, &ui_font_OpenSansBold24, BIG_GREEN, LV_TEXT_ALIGN_CENTER);
}

void UI::applyBigScreenMiningLayout()
{
    bigScreenBase(ui_MiningScreen);
    bigRemoveLegacyImage(ui_Image2);

    lv_obj_t *header = bigPanel(ui_MiningScreen, 0, 0, 480, 52, BIG_HEADER);
    bigText(header, "NERDQAXE++", 16, 12, 190, &ui_font_OpenSansBold24, BIG_TEXT);
    bigText(header, "ASIC", 210, 18, 48, &ui_font_OpenSansBold13, BIG_MUTED);

    lv_obj_t *hashPanel = bigPanel(ui_MiningScreen, 0, 52, 306, 158, BIG_BG);
    bigText(hashPanel, "HASHRATE", 18, 16, 160, &ui_font_OpenSansBold14, BIG_MUTED);
    bigText(hashPanel, "UPTIME", 18, 112, 80, &ui_font_OpenSansBold13, BIG_MUTED);

    lv_obj_t *telemetry = bigPanel(ui_MiningScreen, 306, 52, 174, 158, BIG_PANEL_ALT);
    bigText(telemetry, "TEMP C", 14, 15, 74, &ui_font_OpenSansBold13, BIG_MUTED);
    bigText(telemetry, "FAN RPM", 14, 48, 74, &ui_font_OpenSansBold13, BIG_MUTED);
    bigText(telemetry, "POWER", 14, 81, 74, &ui_font_OpenSansBold13, BIG_MUTED);
    bigText(telemetry, "BEST DIFF", 14, 114, 82, &ui_font_OpenSansBold13, BIG_MUTED);

    lv_obj_t *electrical = bigPanel(ui_MiningScreen, 0, 210, 480, 110, BIG_PANEL);
    bigText(electrical, "INPUT", 16, 18, 96, &ui_font_OpenSansBold13, BIG_MUTED);
    bigText(electrical, "VCORE", 136, 18, 96, &ui_font_OpenSansBold13, BIG_MUTED);
    bigText(electrical, "CURRENT", 256, 18, 96, &ui_font_OpenSansBold13, BIG_MUTED);
    bigText(electrical, "EFF. J/TH", 376, 18, 94, &ui_font_OpenSansBold13, BIG_MUTED);

    bigValue(ui_lbASIC, 260, 15, 92, &ui_font_OpenSansBold14, BIG_GREEN);
    bigValue(ui_lbIP, 350, 17, 114, &lv_font_montserrat_14, BIG_CYAN, LV_TEXT_ALIGN_RIGHT);
    bigValue(ui_lbHashrate, 18, 84, 274, &ui_font_OpenSansBold45, BIG_TEXT);
    bigValue(ui_lbTime, 18, 178, 270, &ui_font_OpenSansBold14, BIG_GREEN);
    bigValue(ui_lbTemp, 398, 65, 66, &ui_font_OpenSansBold24, BIG_TEXT, LV_TEXT_ALIGN_RIGHT);
    bigValue(ui_lbRPM, 388, 98, 76, &ui_font_OpenSansBold24, BIG_TEXT, LV_TEXT_ALIGN_RIGHT);
    bigValue(ui_lbPower, 380, 131, 84, &ui_font_OpenSansBold24, BIG_ORANGE, LV_TEXT_ALIGN_RIGHT);
    bigValue(ui_lbBestDifficulty, 382, 164, 82, &ui_font_OpenSansBold24, BIG_TEXT, LV_TEXT_ALIGN_RIGHT);
    bigValue(ui_lbVinput, 16, 254, 104, &ui_font_OpenSansBold24, BIG_TEXT);
    bigValue(ui_lbVcore, 136, 254, 104, &ui_font_OpenSansBold24, BIG_TEXT);
    bigValue(ui_lbIntensidad, 256, 254, 104, &ui_font_OpenSansBold24, BIG_TEXT);
    bigValue(ui_lbEficiency, 376, 254, 88, &ui_font_OpenSansBold24, BIG_GREEN);
}

void UI::applyBigScreenSettingsLayout()
{
    bigScreenBase(ui_SettingsScreen);
    bigRemoveLegacyImage(ui_Image4);

    lv_obj_t *header = bigPanel(ui_SettingsScreen, 0, 0, 480, 52, BIG_HEADER);
    bigText(header, "MINER SETTINGS", 16, 12, 230, &ui_font_OpenSansBold24, BIG_TEXT);
    bigText(header, "IP", 282, 18, 34, &ui_font_OpenSansBold13, BIG_MUTED);
    bigText(header, "POOL", 404, 18, 44, &ui_font_OpenSansBold13, BIG_MUTED);

    lv_obj_t *tuning = bigPanel(ui_SettingsScreen, 0, 52, 240, 175, BIG_PANEL);
    bigText(tuning, "CORE VOLTAGE mV", 18, 15, 204, &ui_font_OpenSansBold13, BIG_MUTED);
    bigText(tuning, "FREQUENCY MHz", 18, 70, 204, &ui_font_OpenSansBold13, BIG_MUTED);
    bigText(tuning, "FAN MODE / PERCENT", 18, 125, 204, &ui_font_OpenSansBold13, BIG_MUTED);

    lv_obj_t *pool = bigPanel(ui_SettingsScreen, 240, 52, 240, 175, BIG_PANEL_ALT);
    bigText(pool, "POOL HOST", 18, 15, 204, &ui_font_OpenSansBold13, BIG_MUTED);
    bigText(pool, "PORT", 18, 91, 204, &ui_font_OpenSansBold13, BIG_MUTED);

    lv_obj_t *status = bigPanel(ui_SettingsScreen, 0, 227, 480, 93, BIG_BG);
    bigText(status, "HASHRATE", 18, 13, 138, &ui_font_OpenSansBold13, BIG_MUTED);
    bigText(status, "BEST DIFF", 176, 13, 128, &ui_font_OpenSansBold13, BIG_MUTED);
    bigText(status, "SHARES A/R", 334, 13, 128, &ui_font_OpenSansBold13, BIG_MUTED);

    bigValue(ui_lbIPSet, 316, 17, 82, &lv_font_montserrat_14, BIG_CYAN, LV_TEXT_ALIGN_RIGHT);
    bigValue(ui_lbPoolNr, 448, 15, 20, &ui_font_OpenSansBold14, BIG_GREEN, LV_TEXT_ALIGN_RIGHT);
    bigValue(ui_lbVcoreSet, 18, 86, 204, &ui_font_OpenSansBold24, BIG_TEXT);
    bigValue(ui_lbFreqSet, 18, 141, 204, &ui_font_OpenSansBold24, BIG_TEXT);
    bigValue(ui_lbFanSet, 18, 196, 204, &ui_font_OpenSansBold24, BIG_GREEN);
    bigValue(ui_lbPoolSet, 258, 86, 204, &ui_font_OpenSansBold24, BIG_TEXT);
    lv_label_set_long_mode(ui_lbPoolSet, LV_LABEL_LONG_DOT);
    bigValue(ui_lbPortSet, 258, 162, 204, &ui_font_OpenSansBold24, BIG_TEXT);
    bigValue(ui_lbHashrateSet, 18, 263, 138, &ui_font_OpenSansBold24, BIG_TEXT);
    bigValue(ui_lbBestDifficultySet, 176, 263, 128, &ui_font_OpenSansBold24, BIG_TEXT);
    bigValue(ui_lbShares, 334, 263, 128, &ui_font_OpenSansBold24, BIG_GREEN);
}

void UI::applyBigScreenBTCLayout()
{
    bigScreenBase(ui_BTCScreen);
    bigRemoveLegacyImage(ui_ImgBTCscreen);

    lv_obj_t *header = bigPanel(ui_BTCScreen, 0, 0, 480, 52, BIG_HEADER);
    bigText(header, "BITCOIN MARKET", 16, 12, 260, &ui_font_OpenSansBold24, BIG_TEXT);
    bigText(header, "BTC / USD", 336, 18, 128, &ui_font_OpenSansBold14, BIG_ORANGE, LV_TEXT_ALIGN_RIGHT);

    lv_obj_t *price = bigPanel(ui_BTCScreen, 0, 52, 480, 168, BIG_BG);
    bigText(price, "CURRENT PRICE", 20, 26, 440, &ui_font_OpenSansBold14, BIG_MUTED, LV_TEXT_ALIGN_CENTER);

    lv_obj_t *miner = bigPanel(ui_BTCScreen, 0, 220, 240, 100, BIG_PANEL);
    bigText(miner, "MINER HASHRATE", 18, 17, 204, &ui_font_OpenSansBold13, BIG_MUTED, LV_TEXT_ALIGN_CENTER);
    lv_obj_t *thermal = bigPanel(ui_BTCScreen, 240, 220, 240, 100, BIG_PANEL_ALT);
    bigText(thermal, "CHIP TEMP C", 18, 17, 204, &ui_font_OpenSansBold13, BIG_MUTED, LV_TEXT_ALIGN_CENTER);

    bigValue(ui_lblBTCPrice, 20, 112, 440, &ui_font_OpenSansBold45, BIG_ORANGE, LV_TEXT_ALIGN_CENTER);
    bigValue(ui_lblHashPrice, 18, 263, 204, &ui_font_OpenSansBold24, BIG_TEXT, LV_TEXT_ALIGN_CENTER);
    bigValue(ui_lblTempPrice, 258, 263, 204, &ui_font_OpenSansBold24, BIG_GREEN, LV_TEXT_ALIGN_CENTER);
}

void UI::applyBigScreenGlobalStatsLayout()
{
    bigScreenBase(ui_GlobalStats);
    bigRemoveLegacyImage(ui_Image5);

    lv_obj_t *header = bigPanel(ui_GlobalStats, 0, 0, 480, 52, BIG_HEADER);
    bigText(header, "BITCOIN NETWORK", 16, 12, 280, &ui_font_OpenSansBold24, BIG_TEXT);

    lv_obj_t *fees = bigPanel(ui_GlobalStats, 0, 52, 480, 66, BIG_PANEL_ALT);
    bigText(fees, "FEES SAT/VB", 16, 9, 110, &ui_font_OpenSansBold13, BIG_MUTED);
    bigText(fees, "LOW", 158, 9, 76, &ui_font_OpenSansBold13, BIG_MUTED, LV_TEXT_ALIGN_CENTER);
    bigText(fees, "MED", 272, 9, 76, &ui_font_OpenSansBold13, BIG_MUTED, LV_TEXT_ALIGN_CENTER);
    bigText(fees, "HIGH", 386, 9, 76, &ui_font_OpenSansBold13, BIG_MUTED, LV_TEXT_ALIGN_CENTER);

    lv_obj_t *network = bigPanel(ui_GlobalStats, 0, 118, 480, 94, BIG_PANEL);
    bigText(network, "DIFFICULTY", 18, 14, 204, &ui_font_OpenSansBold13, BIG_MUTED);
    bigText(network, "NETWORK HASH EH/S", 258, 14, 204, &ui_font_OpenSansBold13, BIG_MUTED);

    lv_obj_t *chain = bigPanel(ui_GlobalStats, 0, 212, 480, 108, BIG_BG);
    bigText(chain, "BLOCK", 18, 14, 130, &ui_font_OpenSansBold13, BIG_MUTED);
    bigText(chain, "HALVING", 174, 14, 130, &ui_font_OpenSansBold13, BIG_MUTED);
    bigText(chain, "BLOCKS LEFT", 330, 14, 132, &ui_font_OpenSansBold13, BIG_MUTED);

    bigValue(ui_lbllowFee, 158, 81, 76, &ui_font_OpenSansBold24, BIG_GREEN, LV_TEXT_ALIGN_CENTER);
    bigValue(ui_lblmedFee, 272, 81, 76, &ui_font_OpenSansBold24, BIG_ORANGE, LV_TEXT_ALIGN_CENTER);
    bigValue(ui_lblhighFee, 386, 81, 76, &ui_font_OpenSansBold24, BIG_TEXT, LV_TEXT_ALIGN_CENTER);
    bigValue(ui_lblDifficulty, 18, 154, 204, &ui_font_OpenSansBold24, BIG_TEXT);
    bigValue(ui_lblGlobalHash, 258, 154, 204, &ui_font_OpenSansBold24, BIG_CYAN);
    bigValue(ui_lblBlock, 18, 259, 130, &ui_font_OpenSansBold24, BIG_TEXT);
    bigValue(ui_lblHalvingPercent, 174, 259, 130, &ui_font_OpenSansBold24, BIG_ORANGE);
    bigValue(ui_lblBlocksToHalving, 330, 259, 132, &ui_font_OpenSansBold24, BIG_TEXT);
}
#endif

///////////////////// FUNCTIONS ////////////////////

void on_screen_loaded(lv_event_t * e)
{
    DisplayDriver* driver = static_cast<DisplayDriver*>(lv_event_get_user_data(e));
    driver->setScreenAnimationRunning(false);
}

///////////////////// SCREENS ////////////////////

void UI::splash1ScreenInit(void)
{
    ui_Splash1 = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_Splash1, LV_OBJ_FLAG_SCROLLABLE); /// Flags

    ui_imgSplash1 = lv_img_create(ui_Splash1);
    lv_img_set_src(ui_imgSplash1, m_theme->getInitscreen2());
    lv_obj_set_width(ui_imgSplash1, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_imgSplash1, LV_SIZE_CONTENT); /// 1
    lv_obj_set_align(ui_imgSplash1, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_imgSplash1, LV_OBJ_FLAG_ADV_HITTEST);  /// Flags
    lv_obj_clear_flag(ui_imgSplash1, LV_OBJ_FLAG_SCROLLABLE); /// Flags

    // lv_obj_add_event_cb(ui_Splash1, ui_event_Splash1, LV_EVENT_ALL, NULL);

    // Liberar memoria de imágenes no utilizadas
    lv_img_cache_invalidate_src(m_theme->getSplashscreen2());

#ifdef DISPLAY_PROFILE_YYSLUPING_480X320
    {
        // Dynamic firmware version subtitle under the "NerdQAxe++" title,
        // read from the app descriptor (same source handler_system.cpp
        // uses for its "version" field) rather than a hardcoded string.
        const esp_app_desc_t *desc = esp_app_get_description();
        char versionLabel[40];
        snprintf(versionLabel, sizeof(versionLabel), "v%s", desc->version);
        applyBigScreenSplashLayout(ui_Splash1, ui_imgSplash1, versionLabel);
    }
#endif
}

void UI::splash2ScreenInit(void)
{
    ui_Splash2 = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_Splash2, LV_OBJ_FLAG_SCROLLABLE); /// Flags

    ui_Image1 = lv_img_create(ui_Splash2);
    lv_img_set_src(ui_Image1, m_theme->getSplashscreen2());
    lv_obj_set_width(ui_Image1, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_Image1, LV_SIZE_CONTENT); /// 1
    lv_obj_set_align(ui_Image1, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_Image1, LV_OBJ_FLAG_ADV_HITTEST);  /// Flags
    lv_obj_clear_flag(ui_Image1, LV_OBJ_FLAG_SCROLLABLE); /// Flags

    ui_lbConnect = lv_label_create(ui_Splash2);
    lv_obj_set_width(ui_lbConnect, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_lbConnect, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_lbConnect, -31);
    lv_obj_set_y(ui_lbConnect, -40);
    lv_obj_set_align(ui_lbConnect, LV_ALIGN_RIGHT_MID);
    lv_label_set_text(ui_lbConnect, "Connecting...");
    lv_obj_set_style_text_color(ui_lbConnect, lv_color_hex(0xDEDADE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lbConnect, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_lbConnect, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lbConnect, &ui_font_OpenSansBold13, LV_PART_MAIN | LV_STATE_DEFAULT);

    // lv_obj_add_event_cb(ui_Splash2, ui_event_Splash2, LV_EVENT_ALL, NULL);

    // Liberar memoria de imágenes no utilizadas
    lv_img_cache_invalidate_src(m_theme->getInitscreen2());

#ifdef DISPLAY_PROFILE_YYSLUPING_480X320
    // Second splash stage: same typographic title as splash1, with the
    // existing "Connecting..." label repurposed as the status subtitle
    // (recentered here instead of its legacy right-of-logo position, since
    // there is no more logo image to sit next to).
    applyBigScreenSplashLayout(ui_Splash2, ui_Image1, nullptr);
    bigValue(ui_lbConnect, 0, 188, 480, &ui_font_OpenSansBold14, BIG_MUTED, LV_TEXT_ALIGN_CENTER);
#endif
}

void UI::portalScreenInit(void)
{
    ui_PortalScreen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_PortalScreen, LV_OBJ_FLAG_SCROLLABLE); /// Flags

    ui_Image1 = lv_img_create(ui_PortalScreen);
    lv_img_set_src(ui_Image1, m_theme->getPortalscreen());
    lv_obj_set_width(ui_Image1, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_Image1, LV_SIZE_CONTENT); /// 1
    lv_obj_set_align(ui_Image1, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_Image1, LV_OBJ_FLAG_ADV_HITTEST);  /// Flags
    lv_obj_clear_flag(ui_Image1, LV_OBJ_FLAG_SCROLLABLE); /// Flags

    ui_lbSSID = lv_label_create(ui_PortalScreen);
    lv_obj_set_width(ui_lbSSID, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_lbSSID, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_lbSSID, 75);
    lv_obj_set_y(ui_lbSSID, 52);
    lv_obj_set_align(ui_lbSSID, LV_ALIGN_LEFT_MID);
    lv_label_set_text(ui_lbSSID, "NERDAXE_XXXX");
    lv_obj_set_style_text_color(ui_lbSSID, lv_color_hex(0xDEDADE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lbSSID, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_lbSSID, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lbSSID, &ui_font_OpenSansBold13, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(ui_PortalScreen, on_screen_loaded, LV_EVENT_SCREEN_LOADED, m_display);

    // lv_obj_add_event_cb(ui_Splash2, ui_event_Splash2, LV_EVENT_ALL, NULL);

    // Liberar memoria de imágenes no utilizadas
    lv_img_cache_invalidate_src(m_theme->getInitscreen2());

#ifdef DISPLAY_PROFILE_YYSLUPING_480X320
    applyBigScreenPortalLayout();
#endif
}

void UI::miningScreenInit(void)
{
    ui_MiningScreen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_MiningScreen, LV_OBJ_FLAG_SCROLLABLE); /// Flags

    ui_Image2 = lv_img_create(ui_MiningScreen);
    lv_img_set_src(ui_Image2, m_theme->getMiningscreen2());
    lv_obj_set_width(ui_Image2, LV_SIZE_CONTENT);  /// 320
    lv_obj_set_height(ui_Image2, LV_SIZE_CONTENT); /// 170
    lv_obj_set_align(ui_Image2, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_Image2, LV_OBJ_FLAG_ADV_HITTEST);  /// Flags
    lv_obj_clear_flag(ui_Image2, LV_OBJ_FLAG_SCROLLABLE); /// Flags

    ui_lbVinput = lv_label_create(ui_MiningScreen);
    lv_obj_set_width(ui_lbVinput, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_lbVinput, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_lbVinput, 234);
    lv_obj_set_y(ui_lbVinput, -34);
    lv_obj_set_align(ui_lbVinput, LV_ALIGN_LEFT_MID);
    lv_label_set_text(ui_lbVinput, "5V");
    lv_obj_set_style_text_color(ui_lbVinput, lv_color_hex(0xDEDADE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lbVinput, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_lbVinput, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lbVinput, &ui_font_OpenSansBold14, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lbVcore = lv_label_create(ui_MiningScreen);
    lv_obj_set_width(ui_lbVcore, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_lbVcore, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_lbVcore, 234);
    lv_obj_set_y(ui_lbVcore, -12);
    lv_obj_set_align(ui_lbVcore, LV_ALIGN_LEFT_MID);
    lv_label_set_text(ui_lbVcore, "1200mV");
    lv_obj_set_style_text_color(ui_lbVcore, lv_color_hex(0xDEDEDE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lbVcore, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_lbVcore, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lbVcore, &ui_font_OpenSansBold14, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lbIntensidad = lv_label_create(ui_MiningScreen);
    lv_obj_set_width(ui_lbIntensidad, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_lbIntensidad, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_lbIntensidad, 234);
    lv_obj_set_y(ui_lbIntensidad, 10);
    lv_obj_set_align(ui_lbIntensidad, LV_ALIGN_LEFT_MID);
    lv_label_set_text(ui_lbIntensidad, "2.344mA");
    lv_obj_set_style_text_color(ui_lbIntensidad, lv_color_hex(0xDEDEDE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lbIntensidad, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_lbIntensidad, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lbIntensidad, &ui_font_OpenSansBold14, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lbPower = lv_label_create(ui_MiningScreen);
    lv_obj_set_width(ui_lbPower, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_lbPower, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_lbPower, 234);
    lv_obj_set_y(ui_lbPower, 32);
    lv_obj_set_align(ui_lbPower, LV_ALIGN_LEFT_MID);
    lv_label_set_text(ui_lbPower, "0W");
    lv_obj_set_style_text_color(ui_lbPower, lv_color_hex(0xDEDEDE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lbPower, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_lbPower, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lbPower, &ui_font_OpenSansBold14, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lbEficiency = lv_label_create(ui_MiningScreen);
    lv_obj_set_width(ui_lbEficiency, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_lbEficiency, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_lbEficiency, -43);
    lv_obj_set_y(ui_lbEficiency, 61);
    lv_obj_set_align(ui_lbEficiency, LV_ALIGN_RIGHT_MID);
    lv_label_set_text(ui_lbEficiency, "12.4");
    lv_obj_set_style_text_color(ui_lbEficiency, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lbEficiency, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_lbEficiency, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lbEficiency, &ui_font_DigitalNumbers16, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lbTemp = lv_label_create(ui_MiningScreen);
    lv_obj_set_width(ui_lbTemp, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_lbTemp, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_lbTemp, -139);
    lv_obj_set_y(ui_lbTemp, 24);
    lv_obj_set_align(ui_lbTemp, LV_ALIGN_RIGHT_MID);
    lv_label_set_text(ui_lbTemp, "48");
    lv_obj_set_style_text_color(ui_lbTemp, lv_color_hex(0xDEDADE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lbTemp, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_lbTemp, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lbTemp, &ui_font_OpenSansBold14, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lbTime = lv_label_create(ui_MiningScreen);
    lv_obj_set_width(ui_lbTime, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_lbTime, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_lbTime, -190);
    lv_obj_set_y(ui_lbTime, 0);
    lv_obj_set_align(ui_lbTime, LV_ALIGN_RIGHT_MID);
    lv_label_set_text(ui_lbTime, "1d 2h 5m");
    lv_obj_set_style_text_color(ui_lbTime, lv_color_hex(0xDEEE00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lbTime, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_lbTime, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lbTime, &ui_font_OpenSansBold13, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lbIP = lv_label_create(ui_MiningScreen);
    lv_obj_set_width(ui_lbIP, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_lbIP, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_lbIP, -16);
    lv_obj_set_y(ui_lbIP, -77);
    lv_obj_set_align(ui_lbIP, LV_ALIGN_CENTER);
    lv_label_set_text(ui_lbIP, "192.168.1.200");
    lv_obj_set_style_text_color(ui_lbIP, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lbIP, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_lbIP, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lbIP, &lv_font_montserrat_10, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lbBestDifficulty = lv_label_create(ui_MiningScreen);
    lv_obj_set_width(ui_lbBestDifficulty, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_lbBestDifficulty, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_lbBestDifficulty, 34);
    lv_obj_set_y(ui_lbBestDifficulty, 21);
    lv_obj_set_align(ui_lbBestDifficulty, LV_ALIGN_LEFT_MID);
    lv_label_set_text(ui_lbBestDifficulty, "22M");
    lv_obj_set_style_text_color(ui_lbBestDifficulty, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lbBestDifficulty, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_lbBestDifficulty, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lbBestDifficulty, &ui_font_OpenSansBold14, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lbHashrate = lv_label_create(ui_MiningScreen);
    lv_obj_set_width(ui_lbHashrate, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_lbHashrate, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_lbHashrate, -208);
    lv_obj_set_y(ui_lbHashrate, 59);
    lv_obj_set_align(ui_lbHashrate, LV_ALIGN_RIGHT_MID);
    lv_label_set_text(ui_lbHashrate, "500,0");
    lv_obj_set_style_text_color(ui_lbHashrate, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lbHashrate, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_lbHashrate, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lbHashrate, &ui_font_DigitalNumbers28, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lbRPM = lv_label_create(ui_MiningScreen);
    lv_obj_set_width(ui_lbRPM, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_lbRPM, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_lbRPM, 20);
    lv_obj_set_y(ui_lbRPM, -9);
    lv_obj_set_align(ui_lbRPM, LV_ALIGN_CENTER);
    lv_label_set_text(ui_lbRPM, "5000");
    lv_obj_set_style_text_color(ui_lbRPM, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lbRPM, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_lbRPM, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lbRPM, &ui_font_OpenSansBold13, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lbASIC = lv_label_create(ui_MiningScreen);
    lv_obj_set_width(ui_lbASIC, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_lbASIC, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_lbASIC, 111);
    lv_obj_set_y(ui_lbASIC, -66);
    lv_obj_set_align(ui_lbASIC, LV_ALIGN_CENTER);
    lv_label_set_text(ui_lbASIC, m_board->getAsicModel());
    lv_obj_set_style_text_color(ui_lbASIC, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lbASIC, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_lbASIC, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lbASIC, &ui_font_OpenSansBold14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(ui_MiningScreen, on_screen_loaded, LV_EVENT_SCREEN_LOADED, m_display);

    // lv_obj_add_event_cb(ui_MiningScreen, ui_event_MiningScreen, LV_EVENT_ALL, NULL);
#ifdef DISPLAY_PROFILE_YYSLUPING_480X320
    applyBigScreenMiningLayout();
#endif
}
void UI::settingsScreenInit(void)
{
    ui_SettingsScreen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_SettingsScreen, LV_OBJ_FLAG_SCROLLABLE); /// Flags

    ui_Image4 = lv_img_create(ui_SettingsScreen);
    lv_img_set_src(ui_Image4, m_theme->getSettingsscreen());
    lv_obj_set_width(ui_Image4, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_Image4, LV_SIZE_CONTENT); /// 1
    lv_obj_set_align(ui_Image4, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_Image4, LV_OBJ_FLAG_ADV_HITTEST);  /// Flags
    lv_obj_clear_flag(ui_Image4, LV_OBJ_FLAG_SCROLLABLE); /// Flags

    ui_lbIPSet = lv_label_create(ui_SettingsScreen);
    lv_obj_set_width(ui_lbIPSet, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_lbIPSet, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_lbIPSet, -16);
    lv_obj_set_y(ui_lbIPSet, -77);
    lv_obj_set_align(ui_lbIPSet, LV_ALIGN_CENTER);
    lv_label_set_text(ui_lbIPSet, "192.168.1.200");
    lv_obj_set_style_text_color(ui_lbIPSet, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lbIPSet, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_lbIPSet, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lbIPSet, &lv_font_montserrat_10, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lbBestDifficultySet = lv_label_create(ui_SettingsScreen);
    lv_obj_set_width(ui_lbBestDifficultySet, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_lbBestDifficultySet, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_lbBestDifficultySet, 34);
    lv_obj_set_y(ui_lbBestDifficultySet, 21);
    lv_obj_set_align(ui_lbBestDifficultySet, LV_ALIGN_LEFT_MID);
    lv_label_set_text(ui_lbBestDifficultySet, "22M");
    lv_obj_set_style_text_color(ui_lbBestDifficultySet, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lbBestDifficultySet, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_lbBestDifficultySet, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lbBestDifficultySet, &ui_font_OpenSansBold14, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lbPoolNr = lv_label_create(ui_SettingsScreen);
    lv_obj_set_width(ui_lbPoolNr, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_lbPoolNr, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_lbPoolNr, 210);
    lv_obj_set_y(ui_lbPoolNr, -46);
    lv_obj_set_align(ui_lbPoolNr, LV_ALIGN_LEFT_MID);
    lv_label_set_text(ui_lbPoolNr, "");
    lv_obj_set_style_text_color(ui_lbPoolNr, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lbPoolNr, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_lbPoolNr, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lbPoolNr, &ui_font_OpenSansBold14, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lbVcoreSet = lv_label_create(ui_SettingsScreen);
    lv_obj_set_width(ui_lbVcoreSet, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_lbVcoreSet, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_lbVcoreSet, 43);
    lv_obj_set_y(ui_lbVcoreSet, -45);
    lv_obj_set_align(ui_lbVcoreSet, LV_ALIGN_LEFT_MID);
    lv_label_set_text(ui_lbVcoreSet, "1200mV");
    lv_obj_set_style_text_color(ui_lbVcoreSet, lv_color_hex(0xDEDADE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lbVcoreSet, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_lbVcoreSet, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lbVcoreSet, &ui_font_OpenSansBold14, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lbFreqSet = lv_label_create(ui_SettingsScreen);
    lv_obj_set_width(ui_lbFreqSet, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_lbFreqSet, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_lbFreqSet, 43);
    lv_obj_set_y(ui_lbFreqSet, -25);
    lv_obj_set_align(ui_lbFreqSet, LV_ALIGN_LEFT_MID);
    lv_label_set_text(ui_lbFreqSet, "485");
    lv_obj_set_style_text_color(ui_lbFreqSet, lv_color_hex(0xDEDADE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lbFreqSet, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_lbFreqSet, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lbFreqSet, &ui_font_OpenSansBold14, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lbFanSet = lv_label_create(ui_SettingsScreen);
    lv_obj_set_width(ui_lbFanSet, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_lbFanSet, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_lbFanSet, 43);
    lv_obj_set_y(ui_lbFanSet, -5);
    lv_obj_set_align(ui_lbFanSet, LV_ALIGN_LEFT_MID);
    lv_label_set_text(ui_lbFanSet, "AUTO");
    lv_obj_set_style_text_color(ui_lbFanSet, lv_color_hex(0xDEDADE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lbFanSet, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_lbFanSet, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lbFanSet, &ui_font_OpenSansBold14, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lbPoolSet = lv_label_create(ui_SettingsScreen);
    lv_obj_set_width(ui_lbPoolSet, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_lbPoolSet, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_lbPoolSet, 169);
    lv_obj_set_y(ui_lbPoolSet, -9);
    lv_obj_set_align(ui_lbPoolSet, LV_ALIGN_LEFT_MID);
    lv_label_set_text(ui_lbPoolSet, "public-pool.io");
    lv_obj_set_style_text_color(ui_lbPoolSet, lv_color_hex(0xDEDADE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lbPoolSet, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_lbPoolSet, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lbPoolSet, &ui_font_OpenSansBold13, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lbHashrateSet = lv_label_create(ui_SettingsScreen);
    lv_obj_set_width(ui_lbHashrateSet, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_lbHashrateSet, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_lbHashrateSet, -208);
    lv_obj_set_y(ui_lbHashrateSet, 59);
    lv_obj_set_align(ui_lbHashrateSet, LV_ALIGN_RIGHT_MID);
    lv_label_set_text(ui_lbHashrateSet, "500,0");
    lv_obj_set_style_text_color(ui_lbHashrateSet, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lbHashrateSet, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_lbHashrateSet, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lbHashrateSet, &ui_font_DigitalNumbers28, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lbShares = lv_label_create(ui_SettingsScreen);
    lv_obj_set_width(ui_lbShares, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_lbShares, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_lbShares, -23);
    lv_obj_set_y(ui_lbShares, 58);
    lv_obj_set_align(ui_lbShares, LV_ALIGN_RIGHT_MID);
    lv_label_set_text(ui_lbShares, "0/0");
    lv_obj_set_style_text_color(ui_lbShares, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lbShares, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_lbShares, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lbShares, &ui_font_OpenSansBold14, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lbPortSet = lv_label_create(ui_SettingsScreen);
    lv_obj_set_width(ui_lbPortSet, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_lbPortSet, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_lbPortSet, 211);
    lv_obj_set_y(ui_lbPortSet, 13);
    lv_obj_set_align(ui_lbPortSet, LV_ALIGN_LEFT_MID);
    lv_label_set_text(ui_lbPortSet, "3333");
    lv_obj_set_style_text_color(ui_lbPortSet, lv_color_hex(0xDEDADE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lbPortSet, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_lbPortSet, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lbPortSet, &ui_font_OpenSansBold13, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(ui_SettingsScreen, on_screen_loaded, LV_EVENT_SCREEN_LOADED, m_display);

#ifdef DISPLAY_PROFILE_YYSLUPING_480X320
    applyBigScreenSettingsLayout();
#endif
}

void UI::logScreenInit(void)
{
    ui_LogScreen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_LogScreen, LV_OBJ_FLAG_SCROLLABLE);

    // Create a black background
    lv_obj_set_style_bg_color(ui_LogScreen, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_LogScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Create a label for the log text
    ui_LogLabel = lv_label_create(ui_LogScreen);
    lv_label_set_text(ui_LogLabel, "");
    lv_obj_set_style_text_color(ui_LogLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_LogLabel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_LogLabel, &ui_font_OpenSansBold14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_width(ui_LogLabel, lv_pct(100)); // Set label width to 100% of the parent
    lv_obj_set_style_text_align(ui_LogLabel, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(ui_LogLabel, LV_ALIGN_TOP_LEFT, 0, 0);
}

void UI::bTCScreenInit(void)
{
    ui_BTCScreen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_BTCScreen, LV_OBJ_FLAG_SCROLLABLE); /// Flags
    lv_obj_set_style_bg_color(ui_BTCScreen, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_BTCScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_ImgBTCscreen = lv_img_create(ui_BTCScreen);
    lv_img_set_src(ui_ImgBTCscreen, m_theme->getBtcscreen());
    lv_obj_set_width(ui_ImgBTCscreen, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_ImgBTCscreen, LV_SIZE_CONTENT); /// 1
    lv_obj_set_align(ui_ImgBTCscreen, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_ImgBTCscreen, LV_OBJ_FLAG_ADV_HITTEST);  /// Flags
    lv_obj_clear_flag(ui_ImgBTCscreen, LV_OBJ_FLAG_SCROLLABLE); /// Flags

    ui_lblBTCPrice = lv_label_create(ui_BTCScreen);
    lv_obj_set_width(ui_lblBTCPrice, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_lblBTCPrice, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_lblBTCPrice, 30);
    lv_obj_set_y(ui_lblBTCPrice, 47);
    lv_obj_set_align(ui_lblBTCPrice, LV_ALIGN_LEFT_MID);
    lv_label_set_text(ui_lblBTCPrice, "0$");
    lv_obj_set_style_text_color(ui_lblBTCPrice, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lblBTCPrice, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lblBTCPrice, &ui_font_OpenSansBold45, LV_PART_MAIN | LV_STATE_DEFAULT);

    /*ui_lblPriceInc = lv_label_create(ui_BTCScreen);
    lv_obj_set_width(ui_lblPriceInc, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_lblPriceInc, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(ui_lblPriceInc, 193);
    lv_obj_set_y(ui_lblPriceInc, 49);
    lv_obj_set_align(ui_lblPriceInc, LV_ALIGN_LEFT_MID);
    lv_label_set_text(ui_lblPriceInc, "2%");
    lv_obj_set_style_text_color(ui_lblPriceInc, lv_color_hex(0x07FF2A), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lblPriceInc, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lblPriceInc, &ui_font_OpenSansBold14, LV_PART_MAIN | LV_STATE_DEFAULT);*/

    ui_lblHashPrice = lv_label_create(ui_BTCScreen);
    lv_obj_set_width(ui_lblHashPrice, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_lblHashPrice, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_lblHashPrice, 223);
    lv_obj_set_y(ui_lblHashPrice, -63);
    lv_obj_set_align(ui_lblHashPrice, LV_ALIGN_LEFT_MID);
    lv_label_set_text(ui_lblHashPrice, "500,0");
    lv_obj_set_style_text_color(ui_lblHashPrice, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lblHashPrice, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_lblHashPrice, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lblHashPrice, &ui_font_OpenSansBold24, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lblTempPrice = lv_label_create(ui_BTCScreen);
    lv_obj_set_width(ui_lblTempPrice, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_lblTempPrice, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_lblTempPrice, 261);
    lv_obj_set_y(ui_lblTempPrice, -18);
    lv_obj_set_align(ui_lblTempPrice, LV_ALIGN_LEFT_MID);
    lv_label_set_text(ui_lblTempPrice, "24");
    lv_obj_set_style_text_color(ui_lblTempPrice, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lblTempPrice, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_lblTempPrice, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lblTempPrice, &ui_font_OpenSansBold24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(ui_BTCScreen, on_screen_loaded, LV_EVENT_SCREEN_LOADED, m_display);

#ifdef DISPLAY_PROFILE_YYSLUPING_480X320
    applyBigScreenBTCLayout();
#endif
}

void UI::globalStatsScreenInit(void)
{
    ui_GlobalStats = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_GlobalStats, LV_OBJ_FLAG_SCROLLABLE);      /// Flags

    ui_Image5 = lv_img_create(ui_GlobalStats);
    lv_img_set_src(ui_Image5, m_theme->getGlobalstats());
    lv_obj_set_width(ui_Image5, LV_SIZE_CONTENT);   /// 321
    lv_obj_set_height(ui_Image5, LV_SIZE_CONTENT);    /// 170
    lv_obj_set_align(ui_Image5, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_Image5, LV_OBJ_FLAG_ADV_HITTEST);     /// Flags
    lv_obj_clear_flag(ui_Image5, LV_OBJ_FLAG_SCROLLABLE);      /// Flags

    ui_lblHalvingPercent = lv_label_create(ui_GlobalStats);
    lv_obj_set_width(ui_lblHalvingPercent, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_lblHalvingPercent, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(ui_lblHalvingPercent, -64);
    lv_obj_set_y(ui_lblHalvingPercent, 36);
    lv_obj_set_align(ui_lblHalvingPercent, LV_ALIGN_CENTER);
    lv_label_set_text(ui_lblHalvingPercent, "95%");
    lv_obj_set_style_text_color(ui_lblHalvingPercent, lv_color_hex(0xC6C6C5), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lblHalvingPercent, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_lblHalvingPercent, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lblHalvingPercent, &ui_font_OpenSansBold14, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lblBlock = lv_label_create(ui_GlobalStats);
    lv_obj_set_width(ui_lblBlock, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_lblBlock, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(ui_lblBlock, -37);
    lv_obj_set_y(ui_lblBlock, 67);
    lv_obj_set_align(ui_lblBlock, LV_ALIGN_RIGHT_MID);
    lv_label_set_text(ui_lblBlock, "881.557");
    lv_obj_set_style_text_color(ui_lblBlock, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lblBlock, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_lblBlock, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lblBlock, &ui_font_OpenSansBold24, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lblBlocksToHalving = lv_label_create(ui_GlobalStats);
    lv_obj_set_width(ui_lblBlocksToHalving, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_lblBlocksToHalving, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(ui_lblBlocksToHalving, -97);
    lv_obj_set_y(ui_lblBlocksToHalving, 68);
    lv_obj_set_align(ui_lblBlocksToHalving, LV_ALIGN_CENTER);
    lv_label_set_text(ui_lblBlocksToHalving, "210.000");
    lv_obj_set_style_text_color(ui_lblBlocksToHalving, lv_color_hex(0xC6C6C5), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lblBlocksToHalving, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_lblBlocksToHalving, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lblBlocksToHalving, &ui_font_OpenSansBold24, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lblDifficulty = lv_label_create(ui_GlobalStats);
    lv_obj_set_width(ui_lblDifficulty, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_lblDifficulty, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(ui_lblDifficulty, -40);
    lv_obj_set_y(ui_lblDifficulty, -11);
    lv_obj_set_align(ui_lblDifficulty, LV_ALIGN_RIGHT_MID);
    lv_label_set_text(ui_lblDifficulty, "81T");
    lv_obj_set_style_text_color(ui_lblDifficulty, lv_color_hex(0xC6C6C5), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lblDifficulty, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_lblDifficulty, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lblDifficulty, &ui_font_OpenSansBold24, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lblGlobalHash = lv_label_create(ui_GlobalStats);
    lv_obj_set_width(ui_lblGlobalHash, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_lblGlobalHash, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(ui_lblGlobalHash, -39);
    lv_obj_set_y(ui_lblGlobalHash, 30);
    lv_obj_set_align(ui_lblGlobalHash, LV_ALIGN_RIGHT_MID);
    lv_label_set_text(ui_lblGlobalHash, "751,45");
    lv_obj_set_style_text_color(ui_lblGlobalHash, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lblGlobalHash, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_lblGlobalHash, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lblGlobalHash, &ui_font_OpenSansBold24, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lbllowFee = lv_label_create(ui_GlobalStats);
    lv_obj_set_width(ui_lbllowFee, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_lbllowFee, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(ui_lbllowFee, 47);
    lv_obj_set_y(ui_lbllowFee, -64);
    lv_obj_set_align(ui_lbllowFee, LV_ALIGN_CENTER);
    lv_label_set_text(ui_lbllowFee, "2");
    lv_obj_set_style_text_color(ui_lbllowFee, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lbllowFee, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_lbllowFee, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lbllowFee, &ui_font_OpenSansBold13, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lblmedFee = lv_label_create(ui_GlobalStats);
    lv_obj_set_width(ui_lblmedFee, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_lblmedFee, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(ui_lblmedFee, 89);
    lv_obj_set_y(ui_lblmedFee, -64);
    lv_obj_set_align(ui_lblmedFee, LV_ALIGN_CENTER);
    lv_label_set_text(ui_lblmedFee, "200");
    lv_obj_set_style_text_color(ui_lblmedFee, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lblmedFee, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_lblmedFee, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lblmedFee, &ui_font_OpenSansBold13, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_lblhighFee = lv_label_create(ui_GlobalStats);
    lv_obj_set_width(ui_lblhighFee, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_lblhighFee, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(ui_lblhighFee, 138);
    lv_obj_set_y(ui_lblhighFee, -64);
    lv_obj_set_align(ui_lblhighFee, LV_ALIGN_CENTER);
    lv_label_set_text(ui_lblhighFee, "1000");
    lv_obj_set_style_text_color(ui_lblhighFee, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_lblhighFee, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_lblhighFee, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_lblhighFee, &ui_font_OpenSansBold13, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(ui_GlobalStats, on_screen_loaded, LV_EVENT_SCREEN_LOADED, m_display);

#ifdef DISPLAY_PROFILE_YYSLUPING_480X320
    applyBigScreenGlobalStatsLayout();
#endif
}
void UI::createQRScreen(uint8_t *buf, int size) {
    if (!buf || size <= 0) {
        ESP_LOGE(TAG, "No QR to draw");
        return;
    }

    const int quiet = 4;
#ifdef DISPLAY_PROFILE_YYSLUPING_480X320
    const int max_px = 240;
#else
    const int max_px = 160;
#endif
    const int n     = size;                          // modules per side
    const int scale = std::max(2, max_px / (n + 2*quiet));
    const int img   = (n + 2*quiet) * scale;         // final pixels per side
    const size_t bytes = (size_t)img * img * sizeof(lv_color_t);

    // initialize once
    if (!ui_qrScreen) {
        ui_qrScreen = lv_obj_create(NULL);
        lv_obj_clear_flag(ui_qrScreen, LV_OBJ_FLAG_SCROLLABLE);

        // black background
        lv_obj_set_style_bg_color(ui_qrScreen, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(ui_qrScreen, 255, LV_PART_MAIN);

        // create canvas for QR code
        m_qr_canvas_buf = (lv_color_t*) MALLOC(bytes);
        if (!m_qr_canvas_buf) {
            ESP_LOGE(TAG, "QR canvas alloc failed: %dx%d = %u bytes", img, img, (unsigned)bytes);
            return;
        }
        m_qr_canvas_w = img;

        m_qr_canvas = lv_canvas_create(ui_qrScreen);
        lv_canvas_set_buffer(m_qr_canvas, m_qr_canvas_buf, img, img, LV_IMG_CF_TRUE_COLOR);

        // Position QR canvas on the right side of the screen
        lv_obj_align(m_qr_canvas, LV_ALIGN_RIGHT_MID, -10, 0); // 10 px margin from right, slightly up

        // Create label with instructions (left side of the screen)
        lv_obj_t *label = lv_label_create(ui_qrScreen);
        lv_label_set_text(label,"Scan this QR code with your Authenticator App.\n\nPress any button to cancel.");
        lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_text_font(label, &ui_font_OpenSansBold14, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);

#ifdef DISPLAY_PROFILE_YYSLUPING_480X320
        const int screen_w = 480;
#else
        const int screen_w = 320;
#endif
        const int label_area_w = screen_w / 2;

        lv_obj_set_width(label, label_area_w - 20); // small margin inside left half
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

        // Align label relative to the parent (screen)
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 10, 0); // start in left half, 10 px margin
        lv_obj_add_event_cb(ui_qrScreen, on_screen_loaded, LV_EVENT_SCREEN_LOADED, m_display);
    }

    // white background
    lv_draw_rect_dsc_t bg; lv_draw_rect_dsc_init(&bg);
    bg.bg_color = lv_color_white();
    lv_canvas_draw_rect(m_qr_canvas, 0, 0, img, img, &bg);

    // draw black modules
    lv_draw_rect_dsc_t blk; lv_draw_rect_dsc_init(&blk);
    blk.bg_color = lv_color_black();
    blk.border_opa = LV_OPA_TRANSP;

    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            if (!qrcodegen_getModule(buf, x, y)) continue;
            const int px = (quiet + x) * scale;
            const int py = (quiet + y) * scale;
            lv_canvas_draw_rect(m_qr_canvas, px, py, scale, scale, &blk);
        }
    }
}

void UI::powerOffScreenInit(void)
{
    // Create a new blank screen
    ui_PowerOffScreen = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_PowerOffScreen, LV_OBJ_FLAG_SCROLLABLE);

    // Black background
    lv_obj_set_style_bg_color(ui_PowerOffScreen, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui_PowerOffScreen, 255, LV_PART_MAIN);

    // "It's now safe to turn off your computer" image
    LV_IMG_DECLARE(ui_img_safe_png); // Make sure your C array is declared in ui_img_safe_png.c
    lv_obj_t *img = lv_img_create(ui_PowerOffScreen);
    lv_img_set_src(img, &ui_img_safe_png);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);  // Center of screen
#ifdef DISPLAY_PROFILE_YYSLUPING_480X320
    lv_img_set_zoom(img, 384);
    lv_img_set_antialias(img, true);
#endif
    lv_obj_add_event_cb(ui_PowerOffScreen, on_screen_loaded, LV_EVENT_SCREEN_LOADED, m_display);
}



void UI::destroyQRScreen() {
}


// Function to show the overlay with an error message and custom colors
void UI::showErrorOverlay(const char *error_message, uint32_t error_code)
{
    // Get the currently active screen
    lv_obj_t *current_screen = lv_scr_act();

    // Create a container for the overlay
    ui_errOverlayContainer = lv_obj_create(current_screen);
    lv_obj_set_size(ui_errOverlayContainer, 278, 80); // Set the size of the overlay box
    lv_obj_align(ui_errOverlayContainer, LV_ALIGN_CENTER, 0, -20); // Center the overlay on the screen

    // Disable scrollbars for the container
    lv_obj_clear_flag(ui_errOverlayContainer, LV_OBJ_FLAG_SCROLLABLE);

    // Set background color and border style
    lv_obj_set_style_bg_color(ui_errOverlayContainer, lv_color_hex(0x111111), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_errOverlayContainer, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_errOverlayContainer, lv_color_hex(0xe60000), LV_PART_MAIN | LV_STATE_DEFAULT);

    // Create the first label for the error message
    lv_obj_t *error_label = lv_label_create(ui_errOverlayContainer);
    lv_obj_set_width(error_label, LV_SIZE_CONTENT);  // Adjust width based on content
    lv_obj_set_height(error_label, LV_SIZE_CONTENT); // Adjust height based on content
    lv_obj_set_x(error_label, 0); // Center horizontally
    lv_obj_set_y(error_label, 0); // Align slightly below the top
    lv_obj_set_align(error_label, LV_ALIGN_TOP_MID); // Align top-middle
    lv_label_set_text(error_label, error_message); // Set the error message text
    lv_obj_set_style_text_color(error_label, lv_color_hex(0xe60000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(error_label, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    //lv_obj_set_style_text_font(error_label, &lv_font_unscii_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(error_label, &ui_font_vt323_35, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(error_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Create the second label for the Guru Meditation Error
    lv_obj_t *code_label = lv_label_create(ui_errOverlayContainer);
    lv_obj_set_width(code_label, LV_SIZE_CONTENT);  // Adjust width based on content
    lv_obj_set_height(code_label, LV_SIZE_CONTENT); // Adjust height based on content
    lv_obj_set_x(code_label, 0); // Center horizontally
    lv_obj_set_y(code_label, 0); // Align slightly above the bottom
    lv_obj_set_align(code_label, LV_ALIGN_BOTTOM_MID); // Align bottom-middle

    // Format the error code message
    char error_code_message[64];
    snprintf(error_code_message, sizeof(error_code_message), "Guru Meditation #%08X", (int) error_code);
    lv_label_set_text(code_label, error_code_message); // Set the error code message
    lv_obj_set_style_text_color(code_label, lv_color_hex(0xe60000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(code_label, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    //lv_obj_set_style_text_font(code_label, &lv_font_unscii_8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(code_label, &ui_font_vt323_21, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(code_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

#ifdef DISPLAY_PROFILE_YYSLUPING_480X320
    lv_obj_set_size(ui_errOverlayContainer, 420, 126);
    lv_obj_align(ui_errOverlayContainer, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_width(error_label, 390);
    lv_label_set_long_mode(error_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_y(error_label, 6);
    lv_obj_set_width(code_label, 390);
    lv_obj_set_y(code_label, -6);
#endif
}

void UI::hideErrorOverlay()
{
    if (ui_errOverlayContainer != NULL) {
        lv_obj_del(ui_errOverlayContainer); // Delete the overlay object and its children
        ui_errOverlayContainer = NULL;     // Clear the pointer to avoid dangling references
    }
}

// Function to show the overlay with a centered image
void UI::showImageOverlay(const lv_img_dsc_t *image)
{
    // Get the currently active screen
    lv_obj_t *current_screen = lv_scr_act();

    // Create a container for the overlay
    ui_imageOverlayContainer = lv_obj_create(current_screen);
    lv_obj_set_size(ui_imageOverlayContainer, LV_SIZE_CONTENT, LV_SIZE_CONTENT); // Size will fit image
    lv_obj_align(ui_imageOverlayContainer, LV_ALIGN_CENTER, 0, 0); // Center the overlay on the screen

    // Disable scrollbars for the container
    lv_obj_clear_flag(ui_imageOverlayContainer, LV_OBJ_FLAG_SCROLLABLE);

    // Optional: make background transparent or keep style minimal
    lv_obj_set_style_bg_opa(ui_imageOverlayContainer, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(ui_imageOverlayContainer, 0, LV_PART_MAIN);

    // Create an image inside the container
    lv_obj_t *img = lv_img_create(ui_imageOverlayContainer);
    lv_img_set_src(img, image);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0); // Center the image inside the container
}

void UI::hideImageOverlay()
{
    if (ui_imageOverlayContainer != NULL) {
        lv_obj_del(ui_imageOverlayContainer);
        ui_imageOverlayContainer = NULL;
    }
}

void UI::init(Board* board, DisplayDriver *display)
{
    m_board = board;
    m_theme = board->getTheme();
    m_display = display;

    lv_disp_t *dispp = lv_disp_get_default();
    lv_theme_t *m_theme =
        lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), false, LV_FONT_DEFAULT);
    lv_disp_set_theme(dispp, m_theme);

    splash1ScreenInit();
    splash2ScreenInit();
    portalScreenInit();
    miningScreenInit();
    settingsScreenInit();
    bTCScreenInit();
    globalStatsScreenInit();
    // ui_LogScreen_init();

    lv_disp_load_scr(ui_Splash1);
}
