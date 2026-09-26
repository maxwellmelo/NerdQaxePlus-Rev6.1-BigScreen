#pragma once

// Static table of every screen registered for the Mining-state rodizio on
// the big display profile. See screen_registry.cpp for how to add a new
// screen.

#include <stddef.h>

class Screen;

struct ScreenRegistryEntry {
    Screen *screen;
};

// Returns the full table (not filtered by uiRotationMask()) and its count.
// ScreenManager filters it against uiRotationMask() itself.
const ScreenRegistryEntry *screenRegistryAll(size_t *count);
