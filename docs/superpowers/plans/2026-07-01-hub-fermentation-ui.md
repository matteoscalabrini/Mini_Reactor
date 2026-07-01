# HUB Fermentation UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the placeholder HUB `EspNowScreen` with the real fermentation UI: a state-colored monitor, a details carousel, tap-to-confirm Start/Pause/Stop, restyled pairing, and cross-layout touch calibration — all in the brand look on the round 466×466 AMOLED.

**Architecture:** Screens are dumb view-renderers under `src/features/hub/ui/*`; each takes a plain `hubui::Model` view-struct and exposes button intents as consume-latches (the `EspNowScreen::pairPressed()` pattern). A tiny `HubUi` coordinator owns the swipe carousel (HOME ↔ DETAILS), page dots, and confirm overlays, and routes consumed intents back to `HubRuntime`, which is the only place that touches `HubLink`. No screen depends on `HubLink` — modularity preserved. Pure formatting/color logic lives in a header-only `HubViewModel` unit-tested on the native host.

**Tech Stack:** C++17, Arduino-ESP32 3.3.10 / ESP-IDF 5.5.4 (`[env:hub]`), LVGL 8.3.11 (lean config: LABEL + IMG + `lv_obj` + `lv_anim` only), Unity native tests (`[env:native]`), `lv_font_conv` for the Doto subset font, LVGL image converter for the wordmark.

## Global Constraints

- IDF 5.5.x / Arduino-ESP32 3.3.x only (`Claude.md` mandate); build with `~/.platformio/penv/bin/pio` (full path, never bare `pio`).
- Modular + feature-toggle driven: product UI lives under `include/features/hub/ui/*` + `src/features/hub/ui/*`; no direct cross-feature deps (screens ↔ link only via `HubRuntime`).
- Toggle contract: `HubFeatures::kEnableDisplay == false` → no screens created; `kEnableEspNow == false` → BringupScreen path unchanged; calibration gated by `kEnableTouchCalibration`.
- Stay within the lean LVGL widget set — **do not** enable `LV_USE_ARC/BAR/METER/CHART/SLIDER`.
- Brand palette (verbatim): cyan `#00b4d8`, red `#e22626` (STOP) / `#ff4d4f` (fault hero), slate `#6e8595`, amber `#ffb020`, done-green `#37d67a`, background `#000000`.
- YAGNI, incremental, DRY, TDD for pure logic, frequent commits.
- Keep `Roadmap.md` and `API.md` in sync at the end.

---

## File Structure

- `include/sync/SyncFrames.hpp` (modify) — add `kCmdFlagUseCurrent` bit.
- `test/test_sync_codec/test_main.cpp` (modify) — flag round-trip + distinctness test.
- `include/features/hub/link/HubLink.hpp` + `src/features/hub/link/HubLink.cpp` (modify) — `sendStart()`.
- `src/net/EspNowResponder.cpp` (modify) — RunStart "use current settings" branch.
- `include/features/hub/ui/HubViewModel.hpp` (create) — pure model + color/format helpers (header-only).
- `test/test_hub_viewmodel/test_main.cpp` (create) — native unit tests for the above.
- `src/features/hub/ui/fonts/hub_doto_88.c` + `hub_doto_16.c` (create) — generated Doto subset LVGL fonts.
- `src/features/hub/ui/img/wordmark_white.c` (create) — generated LVGL image of the wordmark.
- `include/features/hub/ui/HubAssets.hpp` (create) — `LV_FONT_DECLARE` / `LV_IMG_DECLARE` + brand color helpers.
- `include/features/hub/ui/HomeScreen.hpp` + `src/features/hub/ui/HomeScreen.cpp` (create).
- `include/features/hub/ui/DetailsScreen.hpp` + `src/features/hub/ui/DetailsScreen.cpp` (create).
- `include/features/hub/ui/PairScreen.hpp` + `src/features/hub/ui/PairScreen.cpp` (create) — replaces `EspNowScreen` unpaired/searching.
- `include/features/hub/ui/HubUi.hpp` + `src/features/hub/ui/HubUi.cpp` (create) — coordinator.
- `src/features/hub/ui/CalibrationScreen.cpp` (modify) — cross-layout reskin.
- `src/features/hub/HubRuntime.cpp` (modify) — build `hubui::Model`, drive `HubUi`, route intents.
- Delete `EspNowScreen.{hpp,cpp}` once `PairScreen`/`HomeScreen` subsume it.
- `Roadmap.md`, `API.md` (modify) — Phase 3 checkbox + START semantics note.

---

## Task 1: Protocol — `kCmdFlagUseCurrent` + hub `sendStart()` + reactor branch

**Files:**
- Modify: `include/sync/SyncFrames.hpp` (Command.flags bits, ~line 42-45)
- Test: `test/test_sync_codec/test_main.cpp`
- Modify: `include/features/hub/link/HubLink.hpp:19`, `src/features/hub/link/HubLink.cpp:157`
- Modify: `src/net/EspNowResponder.cpp:64-67`

**Interfaces:**
- Produces: `synclink::kCmdFlagUseCurrent` (`uint8_t`, `1u << 3`); `HubLink::sendStart()` (void).

- [ ] **Step 1: Add the failing test** — append to `test/test_sync_codec/test_main.cpp` (and register it in the `RUN_TEST` list in `main()`):

```cpp
void test_use_current_flag_is_distinct() {
  // New RunStart "use reactor's current settings" flag must not collide with existing bits.
  TEST_ASSERT_EQUAL_UINT8(1u << 3, kCmdFlagUseCurrent);
  TEST_ASSERT_TRUE((kCmdFlagUseCurrent & kCmdFlagDiscReverse) == 0);
  TEST_ASSERT_TRUE((kCmdFlagUseCurrent & kCmdFlagDiscEnabled) == 0);
  TEST_ASSERT_TRUE((kCmdFlagUseCurrent & kCmdFlagRunStopSave) == 0);
}

void test_command_use_current_round_trip() {
  Command c = {};
  c.hdr = {kProtocolVersion, (uint8_t)MsgType::Command, 5};
  c.opcode = (uint8_t)Opcode::RunStart;
  c.flags = kCmdFlagUseCurrent;
  uint8_t buf[250];
  const size_t n = encode(c, buf, sizeof(buf));
  Command got = {};
  TEST_ASSERT_TRUE(decodeCommand(buf, n, got));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)Opcode::RunStart, got.opcode);
  TEST_ASSERT_TRUE((got.flags & kCmdFlagUseCurrent) != 0);
}
```

Add to `main()`: `RUN_TEST(test_use_current_flag_is_distinct); RUN_TEST(test_command_use_current_round_trip);`

- [ ] **Step 2: Run it, verify it fails**

Run: `~/.platformio/penv/bin/pio test -e native -f test_sync_codec`
Expected: FAIL — `kCmdFlagUseCurrent` not declared.

- [ ] **Step 3: Add the flag** — in `include/sync/SyncFrames.hpp`, after `kCmdFlagRunStopSave` (line ~45):

```cpp
constexpr uint8_t kCmdFlagUseCurrent  = 1u << 3;  // RunStart: ignore payload params, start with reactor's live config
```

- [ ] **Step 4: Run it, verify it passes**

Run: `~/.platformio/penv/bin/pio test -e native -f test_sync_codec`
Expected: PASS (all cases).

- [ ] **Step 5: Add `HubLink::sendStart()`** — declare in `include/features/hub/link/HubLink.hpp` after `sendStop();` (line 18):

```cpp
  void sendStart();             // RUN_START using the reactor's current settings
```

Implement in `src/features/hub/link/HubLink.cpp` after `sendStop()` (line ~163):

```cpp
void HubLink::sendStart() {
  Command c = {};
  c.hdr = {kProtocolVersion, (uint8_t)MsgType::Command, link_.nextSeq()};
  c.opcode = (uint8_t)Opcode::RunStart;
  c.flags = kCmdFlagUseCurrent;   // reactor starts with its live setpoint/rpm, run-until-stopped
  send(c);
}
```

- [ ] **Step 6: Reactor "use current" branch** — in `src/net/EspNowResponder.cpp`, replace the `case Opcode::RunStart:` block (lines 64-67):

```cpp
    case Opcode::RunStart:
      if (c.flags & kCmdFlagUseCurrent) {
        // HUB-initiated start: no params on the hub — use the reactor's live config.
        const ReactorTelemetry t = reactor_.telemetry();
        web_.cmdRunStart(t.setpointC, reactor_.rpmSetpoint(), 0, "");  // 0 = run until stopped
      } else {
        web_.cmdRunStart(decFixed(c.targetC_c, kScaleTempC),
                         decFixed((int16_t)c.rpm_c, kScaleRpm), c.durationMin, c.name);
      }
      break;
```

- [ ] **Step 7: Build both firmwares**

Run: `~/.platformio/penv/bin/pio run -e hub && ~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: both link green (re-run once if first hub build prints `xtensa-esp32s3-elf-g++: command not found` — benign pioarduino toolchain install).

- [ ] **Step 8: Commit**

```bash
git add include/sync/SyncFrames.hpp test/test_sync_codec/test_main.cpp include/features/hub/link/HubLink.hpp src/features/hub/link/HubLink.cpp src/net/EspNowResponder.cpp
git commit -m "feat(espnow): kCmdFlagUseCurrent — hub START uses reactor's current settings"
```

---

## Task 2: `HubViewModel` — pure model + color/format helpers (native TDD)

**Files:**
- Create: `include/features/hub/ui/HubViewModel.hpp`
- Test: `test/test_hub_viewmodel/test_main.cpp`

**Interfaces:**
- Produces:
  - `struct hubui::Rgb { uint8_t r, g, b; };`
  - `enum class hubui::HeroState { AtTarget, Heating, Fault };`
  - `hubui::HeroState hubui::heroState(bool tempValid, bool safety, bool probeFault, bool heaterActive);`
  - `hubui::Rgb hubui::heroColor(HeroState);`
  - `const char* hubui::formatHms(uint32_t totalSec, char* buf, size_t n);` → `"6h 02"`
  - `const char* hubui::formatRemaining(int32_t sec, char* buf, size_t n);` → `"until stop"` when `-1`
  - `struct hubui::Model { ... };` (the shared view-struct all screens consume — fields listed below)

- [ ] **Step 1: Write the failing test** — create `test/test_hub_viewmodel/test_main.cpp`:

```cpp
#include <unity.h>
#include "features/hub/ui/HubViewModel.hpp"
using namespace hubui;

void setUp() {} void tearDown() {}

void test_hero_state_priority() {
  // fault (safety) beats heating
  TEST_ASSERT_EQUAL(HeroState::Fault,   heroState(true,  true,  false, true));
  // invalid temp is a fault
  TEST_ASSERT_EQUAL(HeroState::Fault,   heroState(false, false, false, false));
  // probe fault is a fault
  TEST_ASSERT_EQUAL(HeroState::Fault,   heroState(true,  false, true,  false));
  // heating when valid and no fault
  TEST_ASSERT_EQUAL(HeroState::Heating, heroState(true,  false, false, true));
  // steady/at-target otherwise
  TEST_ASSERT_EQUAL(HeroState::AtTarget,heroState(true,  false, false, false));
}

void test_hero_colors() {
  Rgb c = heroColor(HeroState::AtTarget);
  TEST_ASSERT_EQUAL_UINT8(0x00, c.r); TEST_ASSERT_EQUAL_UINT8(0xb4, c.g); TEST_ASSERT_EQUAL_UINT8(0xd8, c.b);
  Rgb a = heroColor(HeroState::Heating);
  TEST_ASSERT_EQUAL_UINT8(0xff, a.r); TEST_ASSERT_EQUAL_UINT8(0xb0, a.g); TEST_ASSERT_EQUAL_UINT8(0x20, a.b);
  Rgb f = heroColor(HeroState::Fault);
  TEST_ASSERT_EQUAL_UINT8(0xff, f.r); TEST_ASSERT_EQUAL_UINT8(0x4d, f.g); TEST_ASSERT_EQUAL_UINT8(0x4f, f.b);
}

void test_format_hms() {
  char b[16];
  TEST_ASSERT_EQUAL_STRING("6h 02",  formatHms(6*3600 + 2*60 + 9, b, sizeof(b)));
  TEST_ASSERT_EQUAL_STRING("0h 00",  formatHms(0, b, sizeof(b)));
  TEST_ASSERT_EQUAL_STRING("41h 58", formatHms(41*3600 + 58*60, b, sizeof(b)));
}

void test_format_remaining() {
  char b[16];
  TEST_ASSERT_EQUAL_STRING("until stop", formatRemaining(-1, b, sizeof(b)));
  TEST_ASSERT_EQUAL_STRING("0h 05",      formatRemaining(5*60, b, sizeof(b)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_hero_state_priority);
  RUN_TEST(test_hero_colors);
  RUN_TEST(test_format_hms);
  RUN_TEST(test_format_remaining);
  return UNITY_END();
}
```

- [ ] **Step 2: Run it, verify it fails**

Run: `~/.platformio/penv/bin/pio test -e native -f test_hub_viewmodel`
Expected: FAIL — header not found / symbols undefined.

- [ ] **Step 3: Create the header** — `include/features/hub/ui/HubViewModel.hpp`:

```cpp
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>

// Pure, header-only view-model for the HUB UI: no LVGL/Arduino deps so it links
// into the native host tests. Screens consume `Model`; helpers turn telemetry
// into brand colours and display strings.
namespace hubui {

struct Rgb { uint8_t r, g, b; };

enum class HeroState : uint8_t { AtTarget, Heating, Fault };

inline HeroState heroState(bool tempValid, bool safety, bool probeFault, bool heaterActive) {
  if (!tempValid || safety || probeFault) return HeroState::Fault;
  if (heaterActive)                        return HeroState::Heating;
  return HeroState::AtTarget;
}

inline Rgb heroColor(HeroState s) {
  switch (s) {
    case HeroState::Fault:   return {0xff, 0x4d, 0x4f};
    case HeroState::Heating: return {0xff, 0xb0, 0x20};
    default:                 return {0x00, 0xb4, 0xd8};
  }
}

inline const char* formatHms(uint32_t totalSec, char* buf, size_t n) {
  const unsigned h = (unsigned)(totalSec / 3600UL);
  const unsigned m = (unsigned)((totalSec % 3600UL) / 60UL);
  std::snprintf(buf, n, "%uh %02u", h, m);
  return buf;
}

inline const char* formatRemaining(int32_t sec, char* buf, size_t n) {
  if (sec < 0) { std::snprintf(buf, n, "until stop"); return buf; }
  return formatHms((uint32_t)sec, buf, n);
}

// The single view-struct HubRuntime fills and every screen renders.
struct Model {
  // link / power
  bool     linked      = false;
  uint8_t  sweepChannel= 0;
  uint8_t  batteryPct  = 0;
  uint16_t batteryMv   = 0;
  bool     charging    = false;
  // run + process
  bool     runActive   = false;
  bool     motorPaused = false;
  bool     fullHold    = false;
  bool     safetyTripped = false;
  bool     probeFault  = false;
  bool     heaterActive= false;
  bool     tempValid   = false;
  float    tempC       = 0.0f;
  float    setpointC   = 0.0f;
  float    heaterPct   = 0.0f;
  float    rpm         = 0.0f;
  float    heaterTempC = 0.0f;  bool heaterTempValid = false;
  float    processMaxC = 0.0f;
  int16_t  load        = 0;     bool loadValid = false;
  uint32_t elapsedSec  = 0;
  int32_t  remainingSec= -1;
};

}  // namespace hubui
```

- [ ] **Step 4: Run it, verify it passes**

Run: `~/.platformio/penv/bin/pio test -e native -f test_hub_viewmodel`
Expected: PASS (4 tests).

- [ ] **Step 5: Commit**

```bash
git add include/features/hub/ui/HubViewModel.hpp test/test_hub_viewmodel/test_main.cpp
git commit -m "feat(hub-ui): pure HubViewModel — hero color-state + time formatters (native-tested)"
```

---

## Task 3: Brand fonts + wordmark asset

**Files:**
- Create: `src/features/hub/ui/fonts/hub_doto_88.c`, `src/features/hub/ui/fonts/hub_doto_16.c`
- Create: `src/features/hub/ui/img/wordmark_white.c`
- Create: `include/features/hub/ui/HubAssets.hpp`
- Modify: `include/hub/lv_conf.h` (enable `LV_FONT_MONTSERRAT_14` as the text fallback)

**Interfaces:**
- Produces: `lv_font_t hub_doto_88` (hero, subset), `hub_doto_16` (small values), `lv_img_dsc_t wordmark_white`; convenience `lv_color_t hubui::lv_cyan()/lv_amber()/lv_red()/lv_slate()/lv_green()` and `lv_color_t hubui::toLv(hubui::Rgb)` in `HubAssets.hpp`.

> **CHECKPOINT — external tooling.** This task shells out to `lv_font_conv` (npm) and the LVGL image converter, which may not be installed and may need a Doto TTF. If either is unavailable, pause and confirm with the reviewer before proceeding (interim fallback: enable `LV_FONT_MONTSERRAT_28`/`_48` and use them for the hero so screens build, then swap in Doto later).

- [ ] **Step 1: Get the Doto TTF** — the repo ships `data/fonts/doto.woff2`. Convert to TTF for the font tool:

```bash
pip install fonttools
~/.platformio/penv/bin/python -c "from fontTools.ttLib import TTFont; f=TTFont('data/fonts/doto.woff2'); f.flavor=None; f.save('/tmp/doto.ttf')"
```

- [ ] **Step 2: Install the LVGL font converter**

```bash
npm install -g lv_font_conv
```

- [ ] **Step 3: Generate the hero font (subset: digits + `. - ° C`)**

```bash
npx lv_font_conv --font /tmp/doto.ttf --size 88 --bpp 4 --format lvgl \
  --range 0x2D,0x2E,0x30-0x39,0x43,0xB0 \
  --lv-font-name hub_doto_88 -o src/features/hub/ui/fonts/hub_doto_88.c --force-fast-kern-format
```

- [ ] **Step 4: Generate the small value font (subset: ASCII digits/letters for DETAILS)**

```bash
npx lv_font_conv --font /tmp/doto.ttf --size 16 --bpp 4 --format lvgl \
  --range 0x20-0x7E,0xB0 \
  --lv-font-name hub_doto_16 -o src/features/hub/ui/fonts/hub_doto_16.c --force-fast-kern-format
```

- [ ] **Step 5: Generate the wordmark image** — from `data/wordmark.svg` render a white PNG (height ~40 px, transparent bg) then convert with the LVGL image tool to a `LV_IMG_CF_TRUE_COLOR_ALPHA` C array named `wordmark_white`:

```bash
# rasterize (rsvg-convert or Inkscape); adjust height to taste
rsvg-convert -h 40 -b none data/wordmark.svg -o /tmp/wordmark_white.png
# recolor to white (ImageMagick): keep alpha, force RGB white
magick /tmp/wordmark_white.png -channel RGB -fuzz 100% -fill white -opaque black /tmp/wordmark_white.png
# convert to LVGL C array (LVGLImage.py from the lvgl repo, or the online tool)
python3 LVGLImage.py --ofmt C --cf TRUECOLOR_ALPHA --name wordmark_white \
  -o src/features/hub/ui/img /tmp/wordmark_white.png
```

- [ ] **Step 6: Declare the assets** — create `include/features/hub/ui/HubAssets.hpp`:

```cpp
#pragma once
#include <lvgl.h>
#include "features/hub/ui/HubViewModel.hpp"

LV_FONT_DECLARE(hub_doto_88);
LV_FONT_DECLARE(hub_doto_16);
LV_IMG_DECLARE(wordmark_white);

namespace hubui {
inline lv_color_t toLv(Rgb c) { return lv_color_make(c.r, c.g, c.b); }
inline lv_color_t lv_cyan()  { return lv_color_make(0x00, 0xb4, 0xd8); }
inline lv_color_t lv_amber() { return lv_color_make(0xff, 0xb0, 0x20); }
inline lv_color_t lv_red()   { return lv_color_make(0xe2, 0x26, 0x26); }
inline lv_color_t lv_redHero(){return lv_color_make(0xff, 0x4d, 0x4f); }
inline lv_color_t lv_slate() { return lv_color_make(0x6e, 0x85, 0x95); }
inline lv_color_t lv_green() { return lv_color_make(0x37, 0xd6, 0x7a); }
inline lv_color_t lv_muted() { return lv_color_make(0x94, 0xa4, 0xad); }
}  // namespace hubui
```

- [ ] **Step 7: Enable the text fallback font** — in `include/hub/lv_conf.h` set `#define LV_FONT_MONTSERRAT_14 1` (used for instructions/labels where Doto's segmented glyphs hurt readability).

- [ ] **Step 8: Build the hub firmware**

Run: `~/.platformio/penv/bin/pio run -e hub`
Expected: green; the three generated `.c` files compile (they are under `src/`, already inside `build_src_filter`).

- [ ] **Step 9: Commit**

```bash
git add src/features/hub/ui/fonts src/features/hub/ui/img include/features/hub/ui/HubAssets.hpp include/hub/lv_conf.h
git commit -m "feat(hub-ui): embed Doto subset fonts + white MINI REACTOR wordmark image"
```

---

## Task 4: HomeScreen (monitor + IDLE + SAFETY + control latches)

**Files:**
- Create: `include/features/hub/ui/HomeScreen.hpp`, `src/features/hub/ui/HomeScreen.cpp`

**Interfaces:**
- Consumes: `hubui::Model`, `HubAssets.hpp` fonts/colors/wordmark.
- Produces: `HomeScreen::create(lv_obj_t* parent)`; `HomeScreen::update(const hubui::Model&)`; latches `bool HomeScreen::consumeStop()`, `consumeStart()`, `consumePause()` (each true once after its button is tapped). PAUSE button relabels to RESUME when `motorPaused||fullHold`.

- [ ] **Step 1: Header** — `include/features/hub/ui/HomeScreen.hpp`:

```cpp
#pragma once
#include <lvgl.h>
#include "features/hub/ui/HubViewModel.hpp"

namespace HomeScreen {
void create(lv_obj_t* parent);          // builds the home page inside `parent`
void update(const hubui::Model& m);
bool consumeStop();                       // STOP tapped (raises confirm at coordinator)
bool consumeStart();                      // START tapped (idle state)
bool consumePause();                      // PAUSE/RESUME tapped
}  // namespace HomeScreen
```

- [ ] **Step 2: Implementation** — `src/features/hub/ui/HomeScreen.cpp`. Build the strip (`●` dot + wordmark img + battery%), name+elapsed label, hero temp label (font `hub_doto_88`), subline, and two `lv_obj` buttons (PAUSE ghost, STOP red) with click latches. `update()` sets the hero color via `hubui::toLv(hubui::heroColor(hubui::heroState(...)))`, swaps IDLE (single cyan START) / SAFETY (red strip + red hero + STOP only) / RUNNING presentation:

```cpp
#include "features/hub/ui/HomeScreen.hpp"
#include "features/hub/ui/HubAssets.hpp"
#include <cstdio>

namespace HomeScreen {
namespace {
lv_obj_t *s_dot, *s_batt, *s_name, *s_hero, *s_sub, *s_pauseBtn, *s_pauseLbl, *s_stopBtn, *s_startBtn;
volatile bool s_stop=false, s_start=false, s_pause=false;
void onStop (lv_event_t*){ s_stop=true; }
void onStart(lv_event_t*){ s_start=true; }
void onPause(lv_event_t*){ s_pause=true; }

lv_obj_t* mkBtn(lv_obj_t* p, lv_coord_t x, const char* txt, lv_color_t bg, lv_color_t fg, bool ghost, lv_event_cb_t cb, lv_obj_t** lblOut=nullptr) {
  lv_obj_t* b = lv_obj_create(p);
  lv_obj_remove_style_all(b);
  lv_obj_set_size(b, 150, 56);
  lv_obj_align(b, LV_ALIGN_BOTTOM_MID, x, -60);
  lv_obj_set_style_radius(b, 28, 0);
  lv_obj_set_style_bg_opa(b, ghost ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(b, bg, 0);
  lv_obj_set_style_border_width(b, ghost ? 2 : 0, 0);
  lv_obj_set_style_border_color(b, bg, 0);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* l = lv_label_create(b); lv_label_set_text(l, txt);
  lv_obj_set_style_text_color(l, fg, 0); lv_obj_center(l);
  if (lblOut) *lblOut = l;
  return b;
}
}  // namespace

void create(lv_obj_t* parent) {
  lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
  lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

  // top strip
  s_dot = lv_obj_create(parent); lv_obj_remove_style_all(s_dot);
  lv_obj_set_size(s_dot, 10, 10); lv_obj_set_style_radius(s_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(s_dot, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(s_dot, hubui::lv_cyan(), 0);
  lv_obj_align(s_dot, LV_ALIGN_TOP_LEFT, 96, 54);
  lv_obj_t* wm = lv_img_create(parent); lv_img_set_src(wm, &wordmark_white);
  lv_obj_align(wm, LV_ALIGN_TOP_MID, 0, 48);
  s_batt = lv_label_create(parent); lv_obj_set_style_text_color(s_batt, hubui::lv_muted(), 0);
  lv_obj_set_style_text_font(s_batt, &lv_font_montserrat_14, 0);
  lv_obj_align(s_batt, LV_ALIGN_TOP_RIGHT, -96, 50);

  s_name = lv_label_create(parent); lv_obj_set_style_text_color(s_name, hubui::lv_slate(), 0);
  lv_obj_set_style_text_font(s_name, &lv_font_montserrat_14, 0);
  lv_obj_align(s_name, LV_ALIGN_CENTER, 0, -80);

  s_hero = lv_label_create(parent); lv_obj_set_style_text_font(s_hero, &hub_doto_88, 0);
  lv_obj_set_style_text_color(s_hero, hubui::lv_cyan(), 0); lv_obj_align(s_hero, LV_ALIGN_CENTER, 0, -10);

  s_sub = lv_label_create(parent); lv_obj_set_style_text_color(s_sub, hubui::lv_muted(), 0);
  lv_obj_set_style_text_font(s_sub, &lv_font_montserrat_14, 0);
  lv_obj_align(s_sub, LV_ALIGN_CENTER, 0, 44);

  s_pauseBtn = mkBtn(parent, -80, "PAUSE", hubui::lv_slate(), hubui::lv_slate(), true, onPause, &s_pauseLbl);
  s_stopBtn  = mkBtn(parent,  80, "STOP",  hubui::lv_red(),  lv_color_white(), false, onStop);
  s_startBtn = mkBtn(parent,   0, "START", hubui::lv_cyan(), lv_color_black(), false, onStart);
}

void update(const hubui::Model& m) {
  char b[64];
  // battery + link dot
  snprintf(b, sizeof(b), "%u%%", (unsigned)m.batteryPct); lv_label_set_text(s_batt, b);
  lv_obj_set_style_bg_color(s_dot, m.safetyTripped ? hubui::lv_redHero() : hubui::lv_cyan(), 0);

  const bool safety = m.safetyTripped;
  const bool idle   = !m.runActive && !safety;

  // hero color
  const hubui::HeroState st = hubui::heroState(m.tempValid, m.safetyTripped, m.probeFault, m.heaterActive);
  lv_obj_set_style_text_color(s_hero, idle ? hubui::lv_slate() : hubui::toLv(hubui::heroColor(st)), 0);
  if (m.tempValid || idle) snprintf(b, sizeof(b), "%.1f°", idle ? m.tempC : m.tempC);
  else                     snprintf(b, sizeof(b), "--°");
  lv_label_set_text(s_hero, b);

  if (safety) {
    lv_label_set_text(s_name, "OVER-TEMP CUTOFF");
    lv_obj_set_style_text_color(s_name, hubui::lv_redHero(), 0);
    lv_label_set_text(s_sub, "heater forced off · check reactor");
  } else if (idle) {
    lv_label_set_text(s_name, "NO RUN");
    lv_obj_set_style_text_color(s_name, hubui::lv_slate(), 0);
    lv_label_set_text(s_sub, "ambient · ready");
  } else {
    char e[16]; hubui::formatHms(m.elapsedSec, e, sizeof(e));
    snprintf(b, sizeof(b), "SOURDOUGH · %s", e);   // name TODO from telemetry when populated
    lv_label_set_text(s_name, b);
    lv_obj_set_style_text_color(s_name, hubui::lv_slate(), 0);
    snprintf(b, sizeof(b), "target %.1f° · heat %.0f%% · %.0f rpm", m.setpointC, m.heaterPct, m.rpm);
    lv_label_set_text(s_sub, b);
  }

  // button visibility
  const bool showRunCtl = m.runActive && !safety;
  lv_obj_add_flag(s_startBtn, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(s_pauseBtn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_stopBtn,  LV_OBJ_FLAG_HIDDEN);
  if (idle)            lv_obj_clear_flag(s_startBtn, LV_OBJ_FLAG_HIDDEN);
  else if (safety)     lv_obj_clear_flag(s_stopBtn,  LV_OBJ_FLAG_HIDDEN);
  else if (showRunCtl){ lv_obj_clear_flag(s_pauseBtn, LV_OBJ_FLAG_HIDDEN); lv_obj_clear_flag(s_stopBtn, LV_OBJ_FLAG_HIDDEN);
                        lv_label_set_text(s_pauseLbl, (m.motorPaused||m.fullHold) ? "RESUME" : "PAUSE"); }
}

bool consumeStop() { if(!s_stop) return false; s_stop=false; return true; }
bool consumeStart(){ if(!s_start)return false; s_start=false;return true; }
bool consumePause(){ if(!s_pause)return false; s_pause=false;return true; }
}  // namespace HomeScreen
```

- [ ] **Step 3: Build**

Run: `~/.platformio/penv/bin/pio run -e hub`
Expected: green. (Rendering verified on-device in Task 6.)

- [ ] **Step 4: Commit**

```bash
git add include/features/hub/ui/HomeScreen.hpp src/features/hub/ui/HomeScreen.cpp
git commit -m "feat(hub-ui): HomeScreen — state-colored hero, IDLE/SAFETY states, Start/Pause/Stop latches"
```

---

## Task 5: DetailsScreen (carousel page 2)

**Files:**
- Create: `include/features/hub/ui/DetailsScreen.hpp`, `src/features/hub/ui/DetailsScreen.cpp`

**Interfaces:**
- Consumes: `hubui::Model`, `HubAssets.hpp`.
- Produces: `DetailsScreen::create(lv_obj_t* parent)`; `DetailsScreen::update(const hubui::Model&)`.

- [ ] **Step 1: Header** — `include/features/hub/ui/DetailsScreen.hpp`:

```cpp
#pragma once
#include <lvgl.h>
#include "features/hub/ui/HubViewModel.hpp"
namespace DetailsScreen {
void create(lv_obj_t* parent);
void update(const hubui::Model& m);
}  // namespace DetailsScreen
```

- [ ] **Step 2: Implementation** — six key→value rows (Heater, Process max, Load, Remaining, Battery, Link) as label pairs; same black bg + top wordmark strip. `src/features/hub/ui/DetailsScreen.cpp`:

```cpp
#include "features/hub/ui/DetailsScreen.hpp"
#include "features/hub/ui/HubAssets.hpp"
#include <cstdio>

namespace DetailsScreen {
namespace { lv_obj_t* s_val[6]; const char* kKeys[6] = {"Heater","Process max","Load (SG)","Remaining","Battery","Link"}; }

void create(lv_obj_t* parent) {
  lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
  lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* wm = lv_img_create(parent); lv_img_set_src(wm, &wordmark_white);
  lv_obj_align(wm, LV_ALIGN_TOP_MID, 0, 48);
  for (int i=0;i<6;i++){
    lv_obj_t* k = lv_label_create(parent);
    lv_obj_set_style_text_font(k, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(k, hubui::lv_muted(), 0);
    lv_label_set_text(k, kKeys[i]); lv_obj_align(k, LV_ALIGN_TOP_LEFT, 96, 110 + i*34);
    s_val[i] = lv_label_create(parent);
    lv_obj_set_style_text_font(s_val[i], &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_val[i], lv_color_white(), 0);
    lv_obj_align(s_val[i], LV_ALIGN_TOP_RIGHT, -96, 110 + i*34);
  }
}

void update(const hubui::Model& m) {
  char b[24];
  if (m.heaterTempValid) snprintf(b,sizeof(b),"%.1f°", m.heaterTempC); else snprintf(b,sizeof(b),"--");
  lv_label_set_text(s_val[0], b);
  snprintf(b,sizeof(b),"%.1f°", m.processMaxC); lv_label_set_text(s_val[1], b);
  if (m.loadValid) snprintf(b,sizeof(b),"%d", (int)m.load); else snprintf(b,sizeof(b),"--");
  lv_label_set_text(s_val[2], b);
  { char r[16]; hubui::formatRemaining(m.remainingSec, r, sizeof(r)); lv_label_set_text(s_val[3], r); }
  snprintf(b,sizeof(b),"%.2fV %u%%", m.batteryMv/1000.0f, (unsigned)m.batteryPct); lv_label_set_text(s_val[4], b);
  snprintf(b,sizeof(b),"ch%u · %s", (unsigned)m.sweepChannel, m.linked ? "ok" : "lost"); lv_label_set_text(s_val[5], b);
}
}  // namespace DetailsScreen
```

- [ ] **Step 3: Build** — `~/.platformio/penv/bin/pio run -e hub` → green.

- [ ] **Step 4: Commit**

```bash
git add include/features/hub/ui/DetailsScreen.hpp src/features/hub/ui/DetailsScreen.cpp
git commit -m "feat(hub-ui): DetailsScreen — deeper telemetry list (carousel page 2)"
```

---

## Task 6: HubUi coordinator — carousel, page dots, confirm overlays + HubRuntime wiring

**Files:**
- Create: `include/features/hub/ui/HubUi.hpp`, `src/features/hub/ui/HubUi.cpp`
- Modify: `src/features/hub/HubRuntime.cpp`

**Interfaces:**
- Consumes: `HomeScreen`, `DetailsScreen`, `PairScreen` (Task 7), `hubui::Model`.
- Produces:
  - `HubUi::begin();` — creates two page containers + a top-layer overlay container + a page-dot pair, registers a screen gesture cb.
  - `HubUi::update(const hubui::Model&, bool paired, HubUi::Mode);` where `enum class Mode { Pairing, Normal };`
  - Intent latches routed by HubRuntime: `bool HubUi::consumeStop(); consumeStart(); consumePause(); consumePair();` — STOP/START are only returned **after** the confirm overlay's YES is pressed.

- [ ] **Step 1: Header** — `include/features/hub/ui/HubUi.hpp`:

```cpp
#pragma once
#include <lvgl.h>
#include "features/hub/ui/HubViewModel.hpp"
namespace HubUi {
enum class Mode : uint8_t { Pairing, Normal };
void begin();
void update(const hubui::Model& m, Mode mode, uint8_t sweepChannel);
bool consumeStop();     // confirmed stop
bool consumeStart();    // confirmed start
bool consumePause();
bool consumePair();     // pairing: PAIR tapped
}  // namespace HubUi
```

- [ ] **Step 2: Implementation** — `src/features/hub/ui/HubUi.cpp`. Two full-screen child containers on `lv_scr_act()` (home, details); horizontal swipe toggles which is on top and updates the two page dots; a hidden overlay container renders the STOP/START confirm (CANCEL / YES) and gates the outgoing intent. When `Mode::Pairing`, hide the carousel and show `PairScreen`.

```cpp
#include "features/hub/ui/HubUi.hpp"
#include "features/hub/ui/HomeScreen.hpp"
#include "features/hub/ui/DetailsScreen.hpp"
#include "features/hub/ui/PairScreen.hpp"
#include "features/hub/ui/HubAssets.hpp"

namespace HubUi {
namespace {
lv_obj_t *s_home, *s_details, *s_pair, *s_dotL, *s_dotR, *s_overlay, *s_ovTitle, *s_ovName;
int  s_page = 0;               // 0 home, 1 details
bool s_confirmStop=false, s_confirmStart=false;
volatile bool s_outStop=false, s_outStart=false, s_outPause=false, s_outPair=false;

void showPage(int p) {
  s_page = p;
  lv_obj_clear_flag(p==0 ? s_home : s_details, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag  (p==0 ? s_details : s_home, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_bg_color(s_dotL, p==0 ? hubui::lv_cyan() : hubui::lv_slate(), 0);
  lv_obj_set_style_bg_color(s_dotR, p==1 ? hubui::lv_cyan() : hubui::lv_slate(), 0);
}
void onGesture(lv_event_t*) {
  lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_get_act());
  if (d==LV_DIR_LEFT  && s_page==0) showPage(1);
  if (d==LV_DIR_RIGHT && s_page==1) showPage(0);
}
void openConfirm(const char* title, bool stop) {
  s_confirmStop = stop; s_confirmStart = !stop;
  lv_label_set_text(s_ovTitle, title);
  lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}
void onCancel(lv_event_t*){ s_confirmStop=s_confirmStart=false; lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN); }
void onYes(lv_event_t*){
  if (s_confirmStop)  s_outStop=true;
  if (s_confirmStart) s_outStart=true;
  s_confirmStop=s_confirmStart=false; lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}
lv_obj_t* dot(lv_obj_t* p, lv_coord_t x){ lv_obj_t* d=lv_obj_create(p); lv_obj_remove_style_all(d);
  lv_obj_set_size(d,8,8); lv_obj_set_style_radius(d,LV_RADIUS_CIRCLE,0); lv_obj_set_style_bg_opa(d,LV_OPA_COVER,0);
  lv_obj_align(d, LV_ALIGN_BOTTOM_MID, x, -132); return d; }
}  // namespace

void begin() {
  lv_obj_t* scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_add_event_cb(scr, onGesture, LV_EVENT_GESTURE, nullptr);

  s_home = lv_obj_create(scr); lv_obj_remove_style_all(s_home);
  lv_obj_set_size(s_home, 466, 466); HomeScreen::create(s_home);
  s_details = lv_obj_create(scr); lv_obj_remove_style_all(s_details);
  lv_obj_set_size(s_details, 466, 466); DetailsScreen::create(s_details);
  s_pair = lv_obj_create(scr); lv_obj_remove_style_all(s_pair);
  lv_obj_set_size(s_pair, 466, 466); PairScreen::create(s_pair);

  s_dotL = dot(scr, -8); s_dotR = dot(scr, 8);

  // overlay (hidden)
  s_overlay = lv_obj_create(scr); lv_obj_remove_style_all(s_overlay);
  lv_obj_set_size(s_overlay, 466, 466);
  lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0); lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
  s_ovTitle = lv_label_create(s_overlay); lv_obj_set_style_text_font(s_ovTitle,&lv_font_montserrat_14,0);
  lv_obj_set_style_text_color(s_ovTitle, lv_color_white(), 0); lv_obj_align(s_ovTitle, LV_ALIGN_CENTER, 0, -60);
  s_ovName = lv_label_create(s_overlay); lv_obj_set_style_text_color(s_ovName, hubui::lv_slate(), 0);
  lv_obj_align(s_ovName, LV_ALIGN_CENTER, 0, -20);
  { lv_obj_t* c=lv_obj_create(s_overlay); lv_obj_remove_style_all(c); lv_obj_set_size(c,150,56);
    lv_obj_align(c,LV_ALIGN_BOTTOM_MID,-80,-60); lv_obj_set_style_radius(c,28,0);
    lv_obj_set_style_border_width(c,2,0); lv_obj_set_style_border_color(c,hubui::lv_slate(),0);
    lv_obj_add_flag(c,LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(c,onCancel,LV_EVENT_CLICKED,nullptr);
    lv_obj_t* l=lv_label_create(c); lv_label_set_text(l,"CANCEL"); lv_obj_set_style_text_color(l,hubui::lv_slate(),0); lv_obj_center(l); }
  { lv_obj_t* y=lv_obj_create(s_overlay); lv_obj_remove_style_all(y); lv_obj_set_size(y,150,56);
    lv_obj_align(y,LV_ALIGN_BOTTOM_MID,80,-60); lv_obj_set_style_radius(y,28,0);
    lv_obj_set_style_bg_opa(y,LV_OPA_COVER,0); lv_obj_set_style_bg_color(y,hubui::lv_red(),0);
    lv_obj_add_flag(y,LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(y,onYes,LV_EVENT_CLICKED,nullptr);
    lv_obj_t* l=lv_label_create(y); lv_label_set_text(l,"YES"); lv_obj_set_style_text_color(l,lv_color_white(),0); lv_obj_center(l); }
  lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);

  showPage(0);
}

void update(const hubui::Model& m, Mode mode, uint8_t sweepChannel) {
  const bool pairing = (mode == Mode::Pairing);
  // pair screen vs carousel
  if (pairing) { lv_obj_clear_flag(s_pair, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_home, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(s_details, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_dotL, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(s_dotR, LV_OBJ_FLAG_HIDDEN);
    PairScreen::update(m.linked, sweepChannel);
    if (PairScreen::consumePair()) s_outPair = true;
    return;
  }
  lv_obj_add_flag(s_pair, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(s_dotL, LV_OBJ_FLAG_HIDDEN); lv_obj_clear_flag(s_dotR, LV_OBJ_FLAG_HIDDEN);
  showPage(s_page);
  HomeScreen::update(m); DetailsScreen::update(m);

  if (HomeScreen::consumeStop())  openConfirm("STOP RUN?", true);
  if (HomeScreen::consumeStart()) openConfirm("START RUN?", false);
  if (HomeScreen::consumePause()) s_outPause = true;
}

bool consumeStop() { if(!s_outStop) return false; s_outStop=false; return true; }
bool consumeStart(){ if(!s_outStart)return false; s_outStart=false;return true; }
bool consumePause(){ if(!s_outPause)return false; s_outPause=false;return true; }
bool consumePair() { if(!s_outPair) return false; s_outPair=false; return true; }
}  // namespace HubUi
```

- [ ] **Step 3: Wire into HubRuntime** — in `src/features/hub/HubRuntime.cpp`: replace the `EspNowScreen` include/create/update path. In `createNormalScreen()`, call `HubUi::begin()` (when EspNow enabled) instead of `EspNowScreen::create()`. In the 250 ms UI refresh block, build a `hubui::Model` from `g_link.latest()` + `g_axp.state()`, decode flags, call `HubUi::update(model, paired?Normal:Pairing, g_link.sweepChannel())`, then route intents:

```cpp
// after HubUi::update(...)
if (HubUi::consumePair())  g_link.startPairing();
if (HubUi::consumeStop())  g_link.sendStop();
if (HubUi::consumeStart()) g_link.sendStart();
if (HubUi::consumePause()) g_link.sendPause(g_link.latest().flags & synclink::kFlagMotorPaused ? 0 : 2);
```

Model build (replace the `EspNowScreen::View` block):

```cpp
const synclink::Telemetry& t = g_link.latest();
const auto& axp = g_axp.state();
hubui::Model mdl;
mdl.linked        = g_link.linkAlive();
mdl.sweepChannel  = g_link.sweepChannel();
mdl.batteryPct    = axp.batteryPercent; mdl.batteryMv = axp.batteryVoltageMv; mdl.charging = axp.charging;
mdl.runActive     = (t.flags & synclink::kFlagRunActive);
mdl.motorPaused   = (t.flags & synclink::kFlagMotorPaused);
mdl.fullHold      = (t.flags & synclink::kFlagFullHold);
mdl.safetyTripped = (t.flags & synclink::kFlagSafetyTripped);
mdl.probeFault    = (t.flags & synclink::kFlagProbeFault);
mdl.heaterActive  = (t.flags & synclink::kFlagHeaterActive);
mdl.tempValid     = (t.tempC_c != synclink::kNullI16);
mdl.tempC         = synclink::decFixed(t.tempC_c, synclink::kScaleTempC);
mdl.setpointC     = synclink::decFixed(t.setpointC_c, synclink::kScaleTempC);
mdl.heaterPct     = t.heaterPct_h / synclink::kScaleHeaterPct;
mdl.rpm           = synclink::decFixed((int16_t)t.rpm_c, synclink::kScaleRpm);
mdl.heaterTempValid = (t.heaterTempC_d != synclink::kNullI16);
mdl.heaterTempC   = synclink::decFixed(t.heaterTempC_d, synclink::kScaleHeaterC);
mdl.processMaxC   = synclink::decFixed(t.processMaxC_d, synclink::kScaleHeaterC);
mdl.load          = t.load; mdl.loadValid = (t.load != synclink::kNullI16);
mdl.elapsedSec    = t.elapsedSec;
mdl.remainingSec  = t.remainingSec;
const HubUi::Mode uimode = (g_link.state()==HubLink::State::Paired) ? HubUi::Mode::Normal : HubUi::Mode::Pairing;
HubUi::update(mdl, uimode, g_link.sweepChannel());
```

- [ ] **Step 4: Build**

Run: `~/.platformio/penv/bin/pio run -e hub`
Expected: green.

- [ ] **Step 5: On-device smoke test** — flash and verify: home renders, swipe HOME↔DETAILS flips page dots, PAUSE/STOP appear during a run, STOP raises the confirm, CANCEL/YES behave, hero color tracks a heat cycle.

Run: `~/.platformio/penv/bin/pio run -e hub -t upload`

- [ ] **Step 6: Commit**

```bash
git add include/features/hub/ui/HubUi.hpp src/features/hub/ui/HubUi.cpp src/features/hub/HubRuntime.cpp
git commit -m "feat(hub-ui): HubUi coordinator — swipe carousel, page dots, Stop/Start confirm overlays"
```

---

## Task 7: PairScreen (unpaired + searching, restyled)

**Files:**
- Create: `include/features/hub/ui/PairScreen.hpp`, `src/features/hub/ui/PairScreen.cpp`

**Interfaces:**
- Produces: `PairScreen::create(lv_obj_t* parent)`; `PairScreen::update(bool searching, uint8_t sweepChannel)`; `bool PairScreen::consumePair()`.

- [ ] **Step 1: Header**

```cpp
#pragma once
#include <lvgl.h>
namespace PairScreen {
void create(lv_obj_t* parent);
void update(bool searching, uint8_t sweepChannel);
bool consumePair();
}  // namespace PairScreen
```

- [ ] **Step 2: Implementation** — centered wordmark; unpaired: "NOT PAIRED" + instruction + cyan PAIR button (click latch); searching: an `lv_anim`-driven expanding ring (`lv_obj` circle whose size/opacity animate) around a cyan hub dot + "SEARCHING · CH n" + Cancel. `src/features/hub/ui/PairScreen.cpp`:

```cpp
#include "features/hub/ui/PairScreen.hpp"
#include "features/hub/ui/HubAssets.hpp"
#include <cstdio>

namespace PairScreen {
namespace {
lv_obj_t *s_state, *s_instr, *s_pairBtn, *s_ring, *s_chan;
volatile bool s_pair=false;
void onPair(lv_event_t*){ s_pair=true; }
void ringExec(void* obj, int32_t v){ lv_obj_set_size((lv_obj_t*)obj, v, v);
  lv_obj_set_style_opa((lv_obj_t*)obj, 255 - (v*255/140), 0); lv_obj_center((lv_obj_t*)obj); }
}  // namespace

void create(lv_obj_t* parent) {
  lv_obj_set_style_bg_color(parent, lv_color_black(), 0); lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
  lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* wm = lv_img_create(parent); lv_img_set_src(wm, &wordmark_white); lv_obj_align(wm, LV_ALIGN_TOP_MID, 0, 60);

  s_ring = lv_obj_create(parent); lv_obj_remove_style_all(s_ring); lv_obj_set_style_radius(s_ring, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(s_ring, 2, 0); lv_obj_set_style_border_color(s_ring, hubui::lv_cyan(), 0);
  lv_obj_align(s_ring, LV_ALIGN_CENTER, 0, -20);
  lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, s_ring); lv_anim_set_exec_cb(&a, ringExec);
  lv_anim_set_values(&a, 30, 140); lv_anim_set_time(&a, 1600); lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&a);

  s_state = lv_label_create(parent); lv_obj_set_style_text_color(s_state, hubui::lv_slate(), 0);
  lv_obj_set_style_text_font(s_state, &lv_font_montserrat_14, 0); lv_obj_align(s_state, LV_ALIGN_CENTER, 0, 30);
  s_chan = lv_label_create(parent); lv_obj_set_style_text_color(s_chan, hubui::lv_cyan(), 0);
  lv_obj_align(s_chan, LV_ALIGN_CENTER, 0, 56);
  s_instr = lv_label_create(parent); lv_obj_set_style_text_color(s_instr, hubui::lv_muted(), 0);
  lv_obj_set_style_text_font(s_instr, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(s_instr, LV_TEXT_ALIGN_CENTER, 0); lv_obj_align(s_instr, LV_ALIGN_CENTER, 0, 90);

  s_pairBtn = lv_obj_create(parent); lv_obj_remove_style_all(s_pairBtn); lv_obj_set_size(s_pairBtn, 170, 58);
  lv_obj_align(s_pairBtn, LV_ALIGN_BOTTOM_MID, 0, -70); lv_obj_set_style_radius(s_pairBtn, 28, 0);
  lv_obj_set_style_bg_opa(s_pairBtn, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(s_pairBtn, hubui::lv_cyan(), 0);
  lv_obj_add_flag(s_pairBtn, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(s_pairBtn, onPair, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* pl = lv_label_create(s_pairBtn); lv_label_set_text(pl, "PAIR"); lv_obj_set_style_text_color(pl, lv_color_black(), 0); lv_obj_center(pl);
}

void update(bool searching, uint8_t sweepChannel) {
  char b[24];
  if (searching) {
    lv_obj_clear_flag(s_ring, LV_OBJ_FLAG_HIDDEN); lv_obj_clear_flag(s_chan, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_pairBtn, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_state, "SEARCHING");
    snprintf(b, sizeof(b), "CH %u", (unsigned)sweepChannel); lv_label_set_text(s_chan, b);
    lv_label_set_text(s_instr, "Open the reactor's\npairing window.");
  } else {
    lv_obj_add_flag(s_ring, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(s_chan, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_pairBtn, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_state, "NOT PAIRED");
    lv_label_set_text(s_instr, "Tap PAIR, then press\n'Pair HUB' on the reactor.");
  }
}

bool consumePair() { if(!s_pair) return false; s_pair=false; return true; }
}  // namespace PairScreen
```

Note the searching/unpaired split maps to `HubLink::State`: `state()==Searching` → `searching=true`; `Unpaired` → `false`. Update the HubUi `Mode::Pairing` branch to pass `g_link.state()==HubLink::State::Searching` as the `searching` arg (adjust `PairScreen::update` call + `HubUi::update` signature to take a `bool searching`).

- [ ] **Step 3: Build** — `~/.platformio/penv/bin/pio run -e hub` → green.

- [ ] **Step 4: Delete the obsolete `EspNowScreen`** — remove `include/features/hub/ui/EspNowScreen.hpp` and `src/features/hub/ui/EspNowScreen.cpp`; drop its include + `EspNowScreen::pairPressed()` usage from `HubRuntime.cpp` (now handled by `HubUi::consumePair()`). Re-build.

- [ ] **Step 5: Commit**

```bash
git rm include/features/hub/ui/EspNowScreen.hpp src/features/hub/ui/EspNowScreen.cpp
git add include/features/hub/ui/PairScreen.hpp src/features/hub/ui/PairScreen.cpp src/features/hub/HubRuntime.cpp include/features/hub/ui/HubUi.hpp src/features/hub/ui/HubUi.cpp
git commit -m "feat(hub-ui): PairScreen (radar search) replaces EspNowScreen"
```

---

## Task 8: CalibrationScreen reskin — cross layout, centered wordmark, done/pending states

**Files:**
- Modify: `src/features/hub/ui/CalibrationScreen.cpp`

**Interfaces:**
- Unchanged public API (`create/showStep/close`); `showStep(index,count,x,y)` now also updates the four sibling target markers' done/active/pending styling. The solver still drives target order.

- [ ] **Step 1: Rework `create()`** — centered `wordmark_white` image; "Touch the dots" title above it and `STEP n / n` (Doto 16) below it; build **four** round target markers (`lv_obj`, `LV_RADIUS_CIRCLE`, ~48 px) at the cardinal cross positions derived from the panel size (N/E/S/W), each holding a small number label. Keep a static array `lv_obj_t* s_targets[4]`.

- [ ] **Step 2: Rework `showStep()`** — style each of the four markers by comparison to `stepIndex`: `< index` → done (transparent, green ring, check); `== index` → active (cyan fill + a pulsing `lv_anim` ring); `> index` → pending (dashed slate). Keep the existing `s_step` text update. (The actual tapped `targetX/targetY` from the solver still positions the touch expectation; the four visual markers are fixed cross anchors.)

- [ ] **Step 3: Build** — `~/.platformio/penv/bin/pio run -e hub` → green.

- [ ] **Step 4: On-device** — trigger first-boot calibration (or reactor `POST /espnow/recalibrate`): confirm the four cross dots, active pulse, done checks, wordmark centered.

- [ ] **Step 5: Commit**

```bash
git add src/features/hub/ui/CalibrationScreen.cpp
git commit -m "feat(hub-ui): calibration reskin — cross targets, centered wordmark, done/pending states"
```

---

## Task 9: Docs + full validation

**Files:**
- Modify: `Roadmap.md`, `API.md`

- [ ] **Step 1: Roadmap** — flip `- [ ] Phase 3 — redesigned fermentation monitoring/control UI (LVGL)` to `- [x]` and add a one-line summary of the screens shipped.

- [ ] **Step 2: API.md** — under the ESP-NOW / command section, note: hub **START** sends `RunStart` with `kCmdFlagUseCurrent`; the reactor begins a run using its current setpoint/rpm, run-until-stopped (no hub-side params).

- [ ] **Step 3: Full build matrix**

```bash
~/.platformio/penv/bin/pio test -e native
~/.platformio/penv/bin/pio run -e hub
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```

Expected: native tests PASS; both firmwares green.

- [ ] **Step 4: Toggle-off build** — temporarily set `HubFeatures::kEnableDisplay=false`, `~/.platformio/penv/bin/pio run -e hub` (green, no screens), then revert.

- [ ] **Step 5: Commit**

```bash
git add Roadmap.md API.md
git commit -m "docs(hub): Phase 3 UI shipped — roadmap + API START semantics"
```

---

## Self-Review Notes

- **Spec coverage:** HOME (T4), DETAILS (T5), state-colored hero (T2+T4), STOP/START confirm (T6), IDLE/SAFETY (T4), pairing (T7), calibration cross layout (T8), START=current settings (T1), lean-widget constraint (no arc/meter enabled — T3 only adds fonts+img), modularity (screens link-free, routed via HubRuntime — T6), toggles (T9 step 4). Covered.
- **Placeholders:** the HomeScreen name uses a literal `"SOURDOUGH"` because reactor telemetry currently sends `name[0]='\0'` (see `EspNowResponder::sendTelemetry` line 127); flagged inline as `TODO from telemetry when populated` — acceptable since the field exists and is wired, only the reactor-side population is out of scope. All other steps carry real code.
- **Type consistency:** `hubui::Model`, `heroState/heroColor`, screen `create(lv_obj_t*)/update(const hubui::Model&)`, and consume-latch names are used identically across T4–T6. `HubUi::update` gains a `bool searching` in T7 — apply that signature change when T7 lands (noted in T7 step 2).
- **Risk:** T3 external tooling is the one checkpoint; interim Montserrat fallback documented.
