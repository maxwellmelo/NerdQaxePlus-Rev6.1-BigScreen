#pragma once

// Interface implemented by every screen of the Mining-state rodizio
// (rotation) on the big display profile (DISPLAY_PROFILE_YYSLUPING_480X320).
//
// One Screen corresponds to one entry in the approved visual design (e.g.
// 19-zen -> s19_zen.cpp, number == 19). ScreenManager (screen_manager.h) owns the
// list of registered screens (screen_registry.cpp), keeps at most ONE of
// them alive (created) at a time to save RAM, and drives create/update/
// destroy under the DisplayDriver's LVGL mutex -- see screen_manager.h
// for the exact locking contract.
//
// No RTTI, no exceptions anywhere in this framework: no dynamic_cast,
// typeid, throw/catch. That matches the rest of the firmware (ESP-IDF
// default build has RTTI/exceptions disabled) and keeps this interface
// cheap (plain vtable dispatch only).

#include "lvgl.h"

struct UiState; // full definition in../ui_state.h; only a pointer/reference
                // is used here so screen.h itself stays a light include.

class Screen {
  public:
    virtual ~Screen() = default;

    // The screen's number in the approved design (e.g. 19 for zen). Used by the registry /
    // rotation mask (uiRotationMask, bit N = screen N enabled).
    virtual int number() const = 0;

    // Short lowercase identifier for logs (e.g. "zen"). Not shown on screen.
    virtual const char *name() const = 0;

    // Builds this screen's LVGL objects as children of `parent`. Called by
    // ScreenManager with the LVGL mutex already held (or from the LVGL task
    // itself, see screen_manager.h). Must be paired with exactly one
    // destroy before create is called again.
    virtual void create(lv_obj_t *parent) = 0;

    // Refreshes this screen's labels/widgets from a freshly filled UiState
    // snapshot. Called with the LVGL mutex held. `s` was populated OUTSIDE
    // the LVGL lock (uiDataFill does no LVGL/I2C/network calls), so this
    // must only touch lv_* objects, never block.
    virtual void update(const UiState &s) = 0;

    // Deletes every LVGL object this screen created (lv_obj_del on the
    // root is enough since everything else is a descendant). Called with
    // the LVGL mutex held. Safe to call even if create was never called
    // (implementations must tolerate that / be idempotent).
    virtual void destroy() = 0;
};
