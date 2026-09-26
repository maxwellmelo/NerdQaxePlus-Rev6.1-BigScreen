#pragma once

// ScreenManager: drives the Mining-state screen rodizio (rotation) on the
// big display profile (DISPLAY_PROFILE_YYSLUPING_480X320). Owns which
// registered screen (screen_registry.h) is currently alive, keeping at
// most ONE created at a time (create()/destroy() pair) to save RAM.
//
// ---------------------------------------------------------------------
// LOCKING CONTRACT (read this before touching enter()/exit()/tick()):
//
// DisplayDriver's m_lvglMutex (pthread_mutex_t, PTHREAD_MUTEX_INITIALIZER,
// i.e. NOT recursive) already has an inconsistent-but-working discipline in
// displayDriver.cpp: the "lvgl Timer" task itself calls lv_*/enterState()
// directly, unlocked, because it is the single writer for its own call
// chain; OTHER tasks (e.g. system.cpp's power task) call the public
// wrappers (miningScreen(), portalScreen(), ...) which DO take
// PThreadGuard(m_lvglMutex) before calling into the same enterState()/lv_*
// code. ScreenManager follows the same convention so it composes with it
// instead of deadlocking on the non-recursive mutex:
//
//   - enter()/exit() do NOT take the lock themselves. They are called from
//     DisplayDriver::enterState(), which itself runs either already under
//     PThreadGuard (call arrived via a locked public wrapper) or from the
//     lvgl Timer task's own thread (unlocked, but single-writer-safe,
//     exactly like every other case in enterState()'s switch). Taking the
//     lock again here would self-deadlock in the first case.
//   - tick() DOES take the lock itself, internally, around the part that
//     touches lv_* objects. It is a *new* call site invoked directly from
//     the lvgl Timer task's main loop (not nested under any existing lock),
//     so locking here is both safe and necessary: it is what actually
//     protects the screen's LVGL objects from a concurrent locked call
//     arriving on another task (e.g. miningScreen() called mid-tick).
//
// tick() also builds the fresh UiState snapshot (uiDataFill()) BEFORE
// taking the lock -- uiDataFill() does no LVGL/I2C/network calls, so
// there's no reason to hold the LVGL mutex while it copies ~30 KB.
// ---------------------------------------------------------------------

#include "lvgl.h"
#include <pthread.h>
#include <stdint.h>

struct UiState;
class Screen;

class ScreenManager {
  public:
    ScreenManager() = default;

    // Call once, before the first enter()/tick(). Stores a reference to
    // DisplayDriver's LVGL mutex (ScreenManager does not own it).
    void begin(pthread_mutex_t &lvglMutex);

    // Called from DisplayDriver::enterState() when entering UiState::Mining
    // (big profile only). Picks the first screen enabled by
    // uiRotationMask() and create()s it as a child of `parent`
    // (m_ui->ui_MiningScreen). See locking contract above.
    void enter(lv_obj_t *parent, int64_t nowUs);

    // Called from DisplayDriver::enterState() when LEAVING UiState::Mining
    // (big profile only), so no rodizio screen is left alive while Mining
    // is not the active DisplayDriver state. See locking contract above.
    void exit();

    // Called every lvgl Timer task loop iteration while in UiState::Mining
    // (big profile only). Internally rate-limited to a few Hz -- cheap to
    // call every iteration. `btn1ShortPress` advances the rodizio
    // immediately (manual advance); otherwise it advances on its own after
    // uiRotationSeconds() per screen. See locking contract above.
    void tick(lv_obj_t *parent, int64_t nowUs, bool btn1ShortPress);

    bool active() const { return m_active; }

  private:
    void refreshEnabledList();
    int currentRegistryIdx() const;
    void reselect(int regIdx, bool pickNext);
    Screen *current() const;
    void createCurrent(lv_obj_t *parent);
    void destroyCurrent();

    static constexpr int kMaxScreens = 32;

    pthread_mutex_t *m_lvglMutex = nullptr;

    int m_enabledIdx[kMaxScreens]; // indices into the registry table
    int m_enabledCount = 0;
    int m_cur = -1; // index into m_enabledIdx, or -1 if nothing enabled
    uint32_t m_mask = 0; // rotation mask the enabled list was built from

    int64_t m_lastSwitchUs = 0; // when the current screen was entered
    int64_t m_lastDataUs = 0;   // when we last refreshed data (rate limit)

    bool m_active = false;

    UiState *m_stateBuf = nullptr; // PSRAM, allocated once, reused forever
};
