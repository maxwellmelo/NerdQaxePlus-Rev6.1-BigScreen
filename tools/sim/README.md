# nqsim - PC screen simulator

Renders 480x320 PNGs of the big-screen display's rotating screens (LVGL
8.3.11), built from the REAL code in `main/displays/screens/*.cpp` (compiled
directly from its original location, not a copy), so screens can be
reviewed visually without real hardware.

## Running from scratch (a single Docker command)

From the repository root, with Docker running:

```bash
docker run --rm \
  -v "$PWD":/project \
  -v "$PWD/sim-out":/mockups \
  -w /project \
  espressif/idf:v5.3.3 \
  bash -c "cd tools/sim && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target nqsim -j\$(nproc) && ./build/nqsim /mockups"
```

On Windows PowerShell:

```powershell
docker run --rm -v "${PWD}:/project" -v "${PWD}/sim-out:/mockups" -w /project espressif/idf:v5.3.3 bash -c "cd tools/sim && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target nqsim -j$(nproc) && ./build/nqsim /mockups"
```

This:
1. Builds the vendored LVGL 8.3.11 in `managed_components/lvgl__lvgl` using
   the REAL `main/displays/lv_conf.h` (unmodified).
2. Builds every registered screen (`main/displays/screens/s*.cpp`),
   `ui_state.cpp`/`ui_data.cpp` and the generated fonts, all directly from
   `main/displays/` (no copies - any change there is picked up on the next
   simulator build).
3. Runs `./build/nqsim <output-dir> [screen-number]`, which renders every
   registered screen against a handful of scenarios (normal operation,
   thermal emergency, Wi-Fi down, missing/NAN data) and writes one PNG per
   screen/scenario pair to `<output-dir>` (`/mockups` above, mapped to
   `./sim-out` on the host).

**Only build the `nqsim` target** (not a bare `cmake --build build`): LVGL's
own `CMakeLists.txt` also declares `lvgl_examples`/`lvgl_demos` targets that
`nqsim` does not need.

No step here touches the local network, real hardware, `esptool`, or a
serial port - it is C/C++ compilation plus in-memory rendering only.

## Scenarios

Each registered screen is rendered once per scenario in `src/main.cpp`,
built from a `UiState` (`main/displays/ui_state.h`):

| Scenario | Simulated condition |
|---|---|
| `normal` | normal operation: representative frequency/hashrate/temperature/power and a populated 24h history |
| `emergencia` | thermal emergency: high regulator temperature, `govState = UI_GOV_EMERGENCY` (warning colors) |
| `wifi-caido` | Wi-Fi/pool down: `wifiConnected = false`, `poolConnected = false` |
| `nan` | missing data: several fields are `NAN` and history is empty; every numeric reading must show as `--`, never a fabricated value, via `nq_fmt.h` |

Output files follow `<NN>-<name>_<scenario>.png`, e.g. `19-zen_normal.png`,
`19-zen_emergencia.png`, one set per registered screen.

## Technical notes

- **No SDL, no window**: the simulator's "display driver" (`src/main.cpp`)
  is just an LVGL `flush_cb` that copies each drawn area into an in-memory
  480x320 framebuffer; at the end that framebuffer is converted to RGB888
  (via LVGL's own `lv_color_to32()`, not a hand-rolled bit shift) and saved
  with `lodepng` (vendored in `third_party/lodepng/`, MIT/zlib license, see
  `third_party/lodepng/LICENSE`).
- **`lv_conf.h`**: the simulator uses the REAL `main/displays/lv_conf.h`
  unmodified (LVGL's CMake `LV_CONF_PATH` points straight at it). The one
  real incompatibility (`LV_MEM_CUSTOM_INCLUDE <lvgl_psram.h>` requiring
  ESP-IDF's `heap_caps_malloc`/PSRAM) is resolved by `host_shim/lvgl_psram.h`,
  which provides the same 3 functions (`lv_psram_alloc/free/realloc`) using
  plain host `malloc`/`free`/`realloc`. The real `main/` header of the same
  name (which needs ESP-IDF) is never added to the simulator's include path,
  so there is no ambiguity about which one gets used.
- **`LV_COLOR_16_SWAP`**: `main/displays/lv_conf.h` sets this to `0`; the
  simulator uses that same value unmodified. This gives a faithful preview
  of drawing/colors for review, but is not necessarily a byte-for-byte
  match of what the real SPI panel receives if `sdkconfig`
  (`CONFIG_LV_COLOR_16_SWAP=y`) has a real effect at runtime - worth
  confirming against a photo of the real display if colors ever look off.
- **`screen_common.h`**: its `#include "../ui.h"` only exists to declare
  legacy fonts that current screens no longer use. That include is guarded
  behind `#ifndef NQSIM_HOST_BUILD` (a macro defined only by this
  simulator's CMake), because `ui.h` pulls in board/ASIC headers that need
  real ESP-IDF and make no sense on a host PC. The real firmware build is
  unaffected (`NQSIM_HOST_BUILD` is never defined there).
- **No `ScreenManager`/`DisplayDriver`**: `src/main.cpp` calls each
  registered screen's factory function directly and invokes
  `create()`/`update()`/`destroy()` itself, instead of going through the
  firmware's rotation manager.

## Known limitations

- **`LV_COLOR_16_SWAP`** (see above): whether it matters on real hardware
  (`sdkconfig`'s `CONFIG_LV_COLOR_16_SWAP=y` vs. `0` in `lv_conf.h`) is a
  pre-existing question independent of the simulator; confirm against a
  photo of the real display if needed.
- **`wifi-caido` looks identical to `normal`** for screens that never read
  `wifiConnected`/`poolConnected` - that is expected, not a simulator bug.
