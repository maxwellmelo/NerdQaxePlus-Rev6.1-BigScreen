# Big-screen fork: what's different from XTVDDICT/NerdQaxePlus-Rev6.1-BigScreen

This fork adds a thermal/electrical governor, 15 new rotating display screens
and their backing data/API layer, and a couple of small build-time changes.
Everything below is a single commit added on top of commit `661273b`
("Change image format in README") of `XTVDDICT/NerdQaxePlus-Rev6.1-BigScreen`'s
`main` branch - upstream has no `thermal-governor` branch. That upstream
repository is itself a fork of `shufps/ESP-Miner-NerdQAxePlus` with the
YYSLUPING NerdQAxe++ Rev 6.1 480x320 display profile added.

## a) Thermal governor

A pure control-law module (`main/thermal_governor.h/.cpp`, no I2C/FreeRTOS/
allocation in the core logic) that dynamically adjusts ASIC frequency and
core voltage to keep the voltage regulator's temperature under control,
instead of running a fixed frequency/voltage pair at all times.

- **Target**: regulator temperature (`vrTemp`) at **78 degC**, with a hard
  ceiling of 82 degC (`vrHardDelta = 4`). A second, internal regulator
  temperature sensor (`vrTempInt`) is targeted at 90 degC / hard ceiling 94
  degC. Other constraints (input current, output current, input voltage,
  input power) can each be enabled independently; a limit of 0 disables
  that constraint.
- **Modes** (`PowerManagementTask::m_govMode`):
  - `0` - off: the governor never touches frequency/voltage.
  - `1` - active: the governor's decision is actually applied to the ASICs.
  - `2` - shadow: the governor runs and computes decisions from the real
    telemetry, but the base frequency is read from the miner instead of
    from its own target and nothing is applied - useful for observing what
    it *would* do before trusting it.
- **Decision loop**: samples every ~2 s, evaluates emergencies every
  sample, and makes regular up/down decisions every ~10 s. Signals are
  gated for plausibility, median-filtered, then smoothed with an EMA before
  the control law sees them. Frequency changes ramp by a bounded number of
  steps per decision (bigger, faster steps on the way down under thermal
  stress; smaller, rate-limited steps on the way up).
- **Anti-oscillation / crash-loop protection**:
  - A frequency that just triggered a constraint is "blocked" for a
    cooldown period before the governor will try it again, with a growing
    block duration on repeated offenses.
  - A separate, unrelated safeguard in `main.cpp`: if the device rebooted
    from a crash twice in a row, the governor is forced off *for that boot
    session only* (RAM flag, nothing written to NVS) so a governor-induced
    crash loop cannot repeat forever; it is retried again on the next
    normal boot.
  - Re-enabling the governor while the ASICs are already hashing seeds its
    internal target from the frequency actually applied, instead of
    dropping to the configured minimum and re-ramping from scratch.
- **Config/endpoint**: the governor's tunables (targets, limits, curve,
  timing) are read from NVS and are part of the existing `PATCH
  /api/system` body under a `"governor"` object; `GET /api/system/info`
  returns a `"governor"` object with the live state (mode, current
  decision, margins, filtered readings, up/down/emergency counters). See
  `main/tasks/power_management_task.cpp` for the NVS <-> config wiring.
- **Testing**: the control law is unit-tested on the host (no ESP-IDF
  needed) against a small simulated thermal plant, see `test/host_governor/`.

## b) 15 new 480x320 rotating screens

Fifteen new LVGL screens, cycled by `ScreenManager` in the same rotation as
the existing dashboard, each a self-contained `Screen` implementation under
`main/displays/screens/`:

- **Dashboard** - cockpit-style hashrate hero with scrolling frequency and
  regulator-temperature instrument tapes.
- **Energy flow** - a wiring-diagram view of the power path from the wall
  plug to the ASICs, with line thickness/animation proportional to watts.
- **Quartet** - the 4 ASIC chips as 4 color-coded quadrants with per-chip
  hashrate and a share-accepted flash animation.
- **Efficiency** - a J/TH-by-frequency chart from this unit's own measured
  frequency sweep, with the sweet spot and current operating point marked.
- **Luck** - a raffle-ticket-styled view of solo-mining odds (chance of
  finding a block today/this year) and hashes played since midnight.
- **Halving** - the block-reward halving cycle and difficulty epoch as two
  concentric progress rings.
- **Shares** - a strip-chart of accepted share difficulty against the
  pool's current difficulty, with a stale-share warning.
- **If we hit a block** - a poster-style screen showing the block reward
  and the honest odds of it happening.
- **Last 24 hours** - a 24-slice ring of the day's hashrate and estimated
  room temperature, one slice per hour.
- **Hashrate** - hashrate over time with rolling averages and a
  frequency-ceiling reference line.
- **Power bill** - a receipt-styled breakdown of what the miner is costing
  to run, in the configured currency and price per kWh.
- **Room** - the miner used as a room thermometer: estimated ambient
  temperature, trend, and a short forecast strip.
- **Journal** - a logbook of governor actions (ups/downs/emergencies) and
  time spent at the frequency ceiling.
- **Zen** - just the hashrate, large, plus a thermal-headroom bar.
- **Clock** - an analog clock whose bezel doubles as a frequency-ceiling
  gauge and whose dial arc shows thermal headroom.

All of them read from a single `UiState` snapshot filled by a new data
layer, `main/ui_data.h/.cpp` (`uiDataInit`/`uiDataTick`/`uiDataFill`), which
collects everything the screens need from the existing modules (power
management, governor, hashrate monitor, stratum, SNTP) plus derived data
that did not exist before: 24 h history, energy/cost tracking, estimated
room temperature, a governor action diary, and lottery odds. `uiDataTick`
runs every 2 s from the power management task; `uiDataFill` is safe to call
from the display side without touching I2C or the network.

## c) Screens API and AxeOS page

`GET`/`PATCH /api/system/screens` (`main/http_server/handler_system.cpp`)
exposes and configures the rotation:

- **`GET`** returns each registered screen's id, display name, whether it
  is enabled, and its time-on-screen in seconds, plus the global default
  duration and its allowed range, and the power-bill currency/price used by
  the "Power bill" screen.
- **`PATCH`** accepts any subset of `screens` (per-screen enable/duration),
  `defaultSecs`, and `powerBill` (`currency`/`pricePerKwh`) - only what
  actually changed needs to be sent. The whole request is validated before
  anything is written: an unknown screen id, an out-of-range duration, or a
  patch that would disable every screen fails the request with nothing
  applied, rather than partially updating the rotation.

A new "Screens" page in AxeOS (`main/http_server/axe-os/src/app/pages/screens/`)
lets the screen rotation, per-screen timing, and power-bill settings be
changed live from the web UI, without rebuilding firmware.

## d) `CONFIG_LV_USE_FONT_COMPRESSED=y`

Added to `sdkconfig.defaults`. All 11+ generated bitmap fonts under
`main/displays/fonts/` are built with `lv_font_conv`'s RLE bitmap
compression (`--bpp 4`, default compression on), so their font descriptors
already have `bitmap_format = LV_FONT_FMT_TXT_COMPRESSED`. Without this
flag, LVGL's decoder refuses to decode that format and silently renders an
empty glyph for every label using any of these fonts - no crash, no build
error, just missing text on-device. Turning it on costs no extra flash for
glyph data (already compressed either way); the only added cost is the
small RLE-decompressor code path, which is compiled in regardless once any
font uses it.

## e) Building

Firmware (Docker, ESP-IDF 5.3.3), from the repository root:

```bash
docker run --rm -v "$PWD":/project -w /project \
  -e BOARD=NERDQAXEPLUS2 -e DISPLAY_PROFILE=YYSLUPING_480X320 \
  -e IDF_GIT_SAFE_DIR=/project \
  espressif/idf:v5.3.3 \
  idf.py -B build-16mb -DSDKCONFIG=build-16mb/sdkconfig \
    -DSDKCONFIG_DEFAULTS=sdkconfig.defaults -DBUILD_WEB=OFF app
```

On Windows PowerShell, the same command works with Docker Desktop running
(no `MSYS_NO_PATHCONV` needed outside of Git Bash).

PC screen simulator (renders every screen to PNG without hardware) and host
unit tests (thermal governor control law, display data layer math) do not
need the ASIC/board headers and build much faster - see
`tools/sim/README.md`, `test/host_governor/test_governor.cpp` and
`test/host_ui_data/test_ui_data.cpp` for the exact host `g++`/CMake
commands.

## f) Use at your own risk

This fork changes frequency/voltage control and adds an autonomous
governor that actively drives the ASICs' operating point. While it
includes plausibility checks, filtering, rate limiting and a crash-loop
guard, **overclocking and automated voltage/frequency control carry a real
risk of instability, overheating, or hardware damage**. Test with the
governor in shadow mode first, keep an eye on regulator and chip
temperatures, and use this at your own risk.
