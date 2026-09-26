// Weak fallback implementations of the ui_data.h interface (main/ui_data.h,
// implemented for real in main/ui_data.cpp by a parallel work stream).
//
// Why this file exists: main/ui_data.cpp does not exist yet, so anything
// that calls into main/ui_data.h (this framework's screens, but also other
// parallel code such as main/http_server/handler_system.cpp, which already
// calls uiDataGetEnergySnapshot()/uiDataReloadConfig()) would fail to link
// right now. These __attribute__((weak)) definitions let the WHOLE
// firmware link and run TODAY, with harmless stub behavior (screens show
// the UiState defaults, i.e. "--"/NAN placeholders, screens 1/14/19
// enabled in rotation, energy snapshot reads as NAN/"--" too).
//
// Once main/ui_data.cpp lands with the real (strong, non-weak) definitions,
// the linker prefers those automatically and this file's definitions are
// simply never chosen again -- no further change needed here.
//
// main/ui_data.h is a moving target (owned by a parallel work stream, not
// editable from here): every function it currently declares is stubbed
// below, not just the ones main/displays/** happens to call, so nothing
// else that links against it can silently fail either. If ui_data.h grows
// again before ui_data.cpp lands, add the new function's weak stub here.

#include "../ui_data.h"

__attribute__((weak)) void uiDataInit()
{
    // stub: nothing to allocate yet
}

__attribute__((weak)) void uiDataTick()
{
    // stub: nothing to sample/integrate yet
}

__attribute__((weak)) void uiDataFill(UiState &out)
{
    // stub: defaults only (NAN / 0 / flags false, cfg constants filled) --
    // see uiStateSetDefaults() in ui_state.cpp. Screens must already treat
    // NAN as "--", so this is a safe placeholder for every field.
    uiStateSetDefaults(out);
}

__attribute__((weak)) void uiDataDiaryPush(uint8_t /*kind*/, const char * /*text*/)
{
    // stub: nothing to append to yet
}

__attribute__((weak)) uint32_t uiRotationMask()
{
    // stub: enable exactly the 3 pilot screens (bit N = screen number N)
    return (1u << 1) | (1u << 14) | (1u << 19);
}

__attribute__((weak)) uint16_t uiRotationSeconds()
{
    return 10;
}

__attribute__((weak)) void uiDataReloadConfig()
{
    // stub: nothing cached yet to reload
}

__attribute__((weak)) void uiDataGetEnergySnapshot(float &kwhToday, float &costToday)
{
    // stub: no energy data yet -- NAN (never a fabricated 0.00) so callers
    // that already know to show "--" for NAN keep doing the right thing.
    kwhToday = NAN;
    costToday = NAN;
}
