#include "screen_manager.h"

#include "screen.h"
#include "screen_registry.h"
#include "../ui_state.h"
#include "../../ui_data.h"

#include "esp_heap_caps.h" // MALLOC_CAP_SPIRAM et al, needed before macros.h
#include "../../macros.h"  // PThreadGuard

// Data refresh (uiDataFill + relabel) is rate-limited to a few Hz; the
// lvgl Timer task loop calls tick() far more often than that (every LVGL
// idle wait, which can be sub-millisecond during animations), and there is
// no point re-copying the ~30 KB UiState snapshot on every single
// iteration.
static constexpr int64_t kDataIntervalUs = 300 * 1000; // ~3 Hz

void ScreenManager::begin(pthread_mutex_t &lvglMutex)
{
    m_lvglMutex = &lvglMutex;
}

void ScreenManager::refreshEnabledList()
{
    size_t count = 0;
    const ScreenRegistryEntry *all = screenRegistryAll(&count);
    uint32_t mask = uiRotationMask();

    m_mask = mask;
    m_enabledCount = 0;
    for (size_t i = 0; i < count && m_enabledCount < kMaxScreens; i++) {
        int number = all[i].screen->number();
        if (number >= 0 && number < 32 && (mask & (1u << number))) {
            m_enabledIdx[m_enabledCount++] = (int) i;
        }
    }
    m_cur = (m_enabledCount > 0) ? 0 : -1;
}

Screen *ScreenManager::current() const
{
    if (m_cur < 0 || m_cur >= m_enabledCount) {
        return nullptr;
    }
    size_t count = 0;
    const ScreenRegistryEntry *all = screenRegistryAll(&count);
    int idx = m_enabledIdx[m_cur];
    if (idx < 0 || (size_t) idx >= count) {
        return nullptr;
    }
    return all[idx].screen;
}

void ScreenManager::createCurrent(lv_obj_t *parent)
{
    Screen *s = current();
    if (s) {
        s->create(parent);
    }
}

void ScreenManager::destroyCurrent()
{
    Screen *s = current();
    if (s) {
        s->destroy();
    }
}

void ScreenManager::enter(lv_obj_t *parent, int64_t nowUs)
{
    refreshEnabledList();
    m_lastSwitchUs = nowUs;
    m_lastDataUs = 0; // force an immediate data refresh on the first tick()
    m_active = true;
    createCurrent(parent);
}

void ScreenManager::exit()
{
    destroyCurrent();
    m_active = false;
    m_cur = -1;
    m_enabledCount = 0;
}

// Registry index of the screen on display, or -1.
int ScreenManager::currentRegistryIdx() const
{
    return (m_cur >= 0 && m_cur < m_enabledCount) ? m_enabledIdx[m_cur] : -1;
}

// Rebuilds the enabled list from the current mask and points m_cur at the
// screen with registry index regIdx if it is still enabled; otherwise (or
// when pickNext is set) at the first enabled screen AFTER regIdx, wrapping.
void ScreenManager::reselect(int regIdx, bool pickNext)
{
    refreshEnabledList(); // sets m_mask, m_cur = 0 (or -1)
    if (m_enabledCount <= 0 || regIdx < 0) {
        return;
    }
    for (int i = 0; i < m_enabledCount; i++) {
        if (m_enabledIdx[i] == regIdx && !pickNext) {
            m_cur = i;
            return;
        }
        if (m_enabledIdx[i] > regIdx) {
            m_cur = i;
            return;
        }
    }
    m_cur = 0; // wrap around
}

void ScreenManager::tick(lv_obj_t *parent, int64_t nowUs, bool btn1ShortPress)
{
    if (!m_active) {
        return;
    }

    // Screens enabled/disabled from the web page (PATCH /api/system/screens)
    // take effect live: the enabled list is rebuilt whenever the mask
    // changes, and the screen on display is left right away if it was
    // switched off.
    uint32_t mask = uiRotationMask();
    if (mask != m_mask) {
        PThreadGuard lock(*m_lvglMutex);
        int regIdx = currentRegistryIdx();
        bool curStillOn = false;
        Screen *cs = current();
        if (cs) {
            int n = cs->number();
            curStillOn = n >= 0 && n < 32 && (mask & (1u << n));
        }
        if (curStillOn) {
            reselect(regIdx, false); // keep showing it, just re-index
        } else {
            destroyCurrent();
            reselect(regIdx, true);
            m_lastSwitchUs = nowUs;
            createCurrent(parent);
            m_lastDataUs = 0; // fill the new screen on this tick
        }
    }

    if (m_enabledCount <= 0) {
        return;
    }

    // Per-screen display time: the CURRENT screen's own duration if
    // configured (see GET/PATCH /api/system/screens and
    // uiResolveScreenSecs() in ui_data.h), falling back to the global
    // scr_secs default. current() can be null very briefly right after
    // enter() on an empty rotation; uiRotationScreenSecs(-1) then simply
    // returns the global default.
    Screen *curScreen = current();
    uint16_t rotS = uiRotationScreenSecs(curScreen ? curScreen->number() : -1);
    bool rotationDue = rotS > 0 && (nowUs - m_lastSwitchUs) >= (int64_t) rotS * 1000000LL;
    bool dataDue = (nowUs - m_lastDataUs) >= kDataIntervalUs;
    bool advance = btn1ShortPress || rotationDue;

    if (!advance && !dataDue) {
        return; // nothing to do yet; avoid the PSRAM copy + lock for no reason
    }

    // Snapshot fresh data OUTSIDE the LVGL lock: uiDataFill() does no
    // LVGL/I2C/network call, so there is no reason to serialize it with the
    // display task.
    if (!m_stateBuf) {
        m_stateBuf = (UiState *) heap_caps_malloc(sizeof(UiState), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (m_stateBuf) {
        uiDataFill(*m_stateBuf);
        m_lastDataUs = nowUs;
    }

    PThreadGuard lock(*m_lvglMutex);

    if (advance && m_enabledCount > 1) {
        destroyCurrent();
        m_cur = (m_cur + 1) % m_enabledCount;
        m_lastSwitchUs = nowUs;
        createCurrent(parent);
    } else if (advance) {
        // Only one screen enabled: nothing to switch to, just reset the timer.
        m_lastSwitchUs = nowUs;
    }

    if (m_stateBuf) {
        Screen *s = current();
        if (s) {
            s->update(*m_stateBuf);
        }
    }
}
