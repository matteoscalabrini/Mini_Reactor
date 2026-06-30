# HUB Companion Device — Phase 1: Board Bring-up — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port the Waveshare ESP32-S3-Touch-AMOLED-1.75 HUB from `../Barebone` into this repo as a second product (`[env:hub]`) that boots, brings up and power-manages every onboard IC, drives the AMOLED+touch via LVGL, and proves the hardware with an on-device diagnostics screen — no networking, no product UI.

**Architecture:** Approach "C" — modular shell, ported internals. Each IC becomes a thin driver under `src/features/hub/` whose register-level sequences are copied **verbatim** from Barebone; a new `HubRuntime` (analog of the reactor's `AppRuntime`) owns the shared I2C bus and runs an ordered power-up → probe → run → power-down cycle. Pure logic (touch transform, sleep FSM, sensor decodes) is extracted into dependency-free headers and host-unit-tested.

**Tech Stack:** ESP32-S3-N16R8, pioarduino 55.03.39 / Arduino-ESP32 3.3.x / ESP-IDF 5.5.4, C++17, LVGL 8.3.11, PlatformIO, Unity (native host tests).

## Global Constraints

_Every task's requirements implicitly include this section._

- **Platform (verbatim):** `platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.39/platform-espressif32.zip`; Arduino-ESP32 `3.3.10`; ESP-IDF `v5.5.4`. Same family as the reactor env.
- **Board:** `esp32-s3-devkitc1-n16r8` (16 MB flash / 8 MB PSRAM). **Partitions:** `partitions_hub_16mb.csv`.
- **Build tool:** ALWAYS full path `~/.platformio/penv/bin/pio`. Never bare `pio`/`platformio`.
- **pioarduino first-build gotcha:** the first clean build may fail once with `xtensa-esp32s3-elf-g++: command not found`. Benign — re-run the same `pio run`.
- **C++ standard:** gnu++17 fleet-wide.
- **The reactor product (`[env:esp32-s3-devkitc-1]`) must remain byte-unaffected** — only `build_src_filter` is added to it.
- **Power-aware toggle contract:** for each `AppConfig::HubFeatures::kEnable*` flag set `false`, the IC is **powered DOWN** (its sleep register and/or AXP2101 rail cut) at boot, then runtime polling is skipped. AXP2101 is the power authority and always initializes; only its telemetry is gated.
- **Startup logs:** explicit per-IC `[HUB] <ic>: enabled` / `[HUB] <ic>: disabled (powered down)`.
- **No changes outside the `MINI_REACTOR` project.** (Source is *read* from `../Barebone`; nothing is written there.)
- **Source-of-truth config:** all pins/addresses/defaults are `constexpr` in `include/app_config.hpp` (no magic numbers in drivers), mirroring the reactor.

## Porting Convention (read before any task)

This is a **verbatim port** (approach C). For each driver:

1. **Interface (`.hpp`):** written in full in this plan — it is NEW code matching the repo's `Husb238` driver style (`TwoWire&`+address ctor, `begin(...)`, `probe()`, `refreshStatus(State&)`, `Error` enum, `static constexpr` register map).
2. **Implementation internals (`.cpp`):** **copy the register sequence verbatim** from the cited Barebone `file:line-range`, then apply exactly these mechanical transformations:
   - Replace `AppConfig::Pins::kX` / `PIN_SELECT(...)` → `AppConfig::PinoutHub::kX`.
   - Replace `AppConfig::Hub*::kY` Barebone names → the re-homed names defined in Task 1 (a 1:1 rename table is in Task 1).
   - Replace `AppLog::initf(...)`/`AppLog::warnf(...)` → `Serial.printf(...)`.
   - Delete `PRODUCT_MODE`/`BoardProfile`/`kEnableFeatureHub*` gates (gating now lives in `HubRuntime`).
   - Delete references to `hubscene::*`, `MotionLightControl`, `AppRuntime::g_hubSettingsWakeCache`, `Preferences`/NVS, `esp_codec_dev` (Phase 1 uses compile-time defaults; persistence/audio-stack are later phases).
   - Move any pure math out into the cited `*Math`/transform header so it is host-testable.
3. **Do NOT "optimize"** copied register/timing sequences (especially the CO5300 QSPI init and the audio power-down writes).

When a step says *"port X from `path:lines` per the Porting Convention"*, it means: create the file, paste the cited lines, apply the transforms above. That is concrete, not a placeholder.

---

### Task 1: Hub build environment + skeleton (compiles an empty hub firmware)

**Files:**
- Modify: `platformio.ini` (add `[env:hub]`; add `build_src_filter` to `[env:esp32-s3-devkitc-1]`; re-enable `[env:native]`)
- Create: `boards/esp32-s3-devkitc1-n16r8.json` (copy from `../Barebone/boards/esp32-s3-devkitc1-n16r8.json`)
- Create: `partitions_hub_16mb.csv` (copy from `../Barebone/partitions_hub_16mb.csv`)
- Create: `include/hub/lv_conf.h` (port from `../Barebone/include/lv_conf.h`)
- Create: `include/hub/LvglMemory.h` (PSRAM allocator shim)
- Modify: `include/app_config.hpp` (add hub `constexpr` namespaces + toggles)
- Create: `include/features/hub/HubRuntime.hpp`
- Create: `src/features/hub/HubRuntime.cpp`
- Create: `src/features/hub/main_hub.cpp`

**Interfaces:**
- Produces: `namespace HubRuntime { void begin(); void tick(); }` (same shape as `AppRuntime`).
- Produces: `AppConfig::PinoutHub::*`, `AppConfig::Hub::*`, `AppConfig::HubPower::*`, `AppConfig::HubImu::*`, `AppConfig::HubTouch::*`, `AppConfig::HubDisplay::*`, `AppConfig::HubMotionWake::*`, `AppConfig::HubCodec::*`, `AppConfig::HubSleep::*`, `AppConfig::HubFeatures::kEnable*`.

- [ ] **Step 1: Copy the board file and partition table**

```bash
cp ../Barebone/boards/esp32-s3-devkitc1-n16r8.json boards/esp32-s3-devkitc1-n16r8.json
cp ../Barebone/partitions_hub_16mb.csv partitions_hub_16mb.csv
```

- [ ] **Step 2: Add `build_src_filter` to the reactor env so hub code never links into it**

In `platformio.ini`, inside `[env:esp32-s3-devkitc-1]`, add:

```ini
build_src_filter =
    +<*>
    -<features/hub/>
```

- [ ] **Step 3: Add the `[env:hub]` block**

Append to `platformio.ini` (ported from `../Barebone/platformio.ini:106-144`, reshaped — no `PRODUCT_MODE`, hub-only `LV_CONF_PATH`, src filter keeps shared services and drops reactor product features):

```ini
[env:hub]
; Waveshare ESP32-S3-Touch-AMOLED-1.75 companion device.
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.39/platform-espressif32.zip
platform_packages =
    framework-arduinoespressif32@https://github.com/espressif/arduino-esp32/releases/download/3.3.10/esp32-core-3.3.10.tar.xz
    framework-arduinoespressif32-libs@https://github.com/espressif/arduino-esp32/releases/download/3.3.10/esp32-core-3.3.10-libs.tar.xz
    framework-espidf@https://github.com/pioarduino/esp-idf/releases/download/v5.5.4/esp-idf-v5.5.4.tar.xz
board = esp32-s3-devkitc1-n16r8
framework = arduino
board_build.partitions = partitions_hub_16mb.csv
board_build.filesystem = spiffs
board_build.cmake_extra_args = -DEXCLUDE_COMPONENTS=esp-matter
custom_sdkconfig =
    CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y
    CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096
monitor_speed = 115200
upload_speed = 921600
build_unflags = -std=gnu++11
build_flags =
    -std=gnu++17
    -DARDUINO_USB_MODE=1
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DCORE_DEBUG_LEVEL=2
    -I .
    -D LV_CONF_INCLUDE_SIMPLE
    -D LV_CONF_PATH=include/hub/lv_conf.h
lib_deps =
    lvgl/lvgl @ 8.3.11
build_src_filter =
    +<*>
    -<main.cpp>
    -<system/AppRuntime.cpp>
    -<net/>
    -<features/control/>
    -<features/heater/>
    -<features/motor/>
    -<features/sensor/>
    -<features/ui/>
    -<storage/>
    -<power/Husb238.cpp>
```

- [ ] **Step 4: Re-enable the native test env**

Replace the commented `[env:native]` block at the bottom of `platformio.ini` with:

```ini
[env:native]
platform = native
test_framework = unity
build_flags = -std=gnu++17 -Iinclude
```

- [ ] **Step 5: Port `lv_conf.h` and add the PSRAM allocator shim**

`cp ../Barebone/include/lv_conf.h include/hub/lv_conf.h`, then in `include/hub/lv_conf.h` change the custom-allocator include path to the local shim:

```c
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE "hub/LvglMemory.h"
#define LV_MEM_CUSTOM_ALLOC  hubLvglMalloc
#define LV_MEM_CUSTOM_FREE   hubLvglFree
#define LV_MEM_CUSTOM_REALLOC hubLvglRealloc
```

Create `include/hub/LvglMemory.h` (PSRAM-preferred allocator; replaces Barebone's `bbLvgl*`):

```c
#pragma once
#include <stddef.h>
#include "esp_heap_caps.h"
static inline void* hubLvglMalloc(size_t s)            { return heap_caps_malloc(s, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
static inline void  hubLvglFree(void* p)               { heap_caps_free(p); }
static inline void* hubLvglRealloc(void* p, size_t s)  { return heap_caps_realloc(p, s, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
```

- [ ] **Step 6: Add the hub config namespaces to `app_config.hpp`**

Append inside `namespace AppConfig { ... }` (before its closing brace) in `include/app_config.hpp`. Values are copied verbatim from `../Barebone/include/app_config.hpp` (the agent-extracted blocks); names are re-homed (`Pins`→`PinoutHub`). **Rename table** used throughout the port: Barebone `AppConfig::Pins::kHubX` → `AppConfig::PinoutHub::kHubX`; Barebone `AppConfig::Pins::kI2cSda/kI2cScl/kImuInt2/kBootButton` → `AppConfig::PinoutHub::kI2cSda/kI2cScl/kImuInt2/kBootButton`; all `AppConfig::Hub*`/`HubPower`/`HubImu`/`HubTouch`/`HubDisplay`/`HubMotionWake`/`HubCodec`/`HubSleep` names are kept identical.

```cpp
// ── HUB companion device (Waveshare ESP32-S3-Touch-AMOLED-1.75) ───────────────
namespace PinoutHub {
static constexpr uint8_t kI2cSda = 15;
static constexpr uint8_t kI2cScl = 14;
static constexpr uint8_t kBootButton = 0;
static constexpr uint8_t kImuInt2 = 21;            // QMI8658 wake-on-motion
static constexpr uint8_t kHubDisplayCs = 12;
static constexpr uint8_t kHubDisplaySclk = 38;
static constexpr uint8_t kHubDisplayData0 = 4;
static constexpr uint8_t kHubDisplayData1 = 5;
static constexpr uint8_t kHubDisplayData2 = 6;
static constexpr uint8_t kHubDisplayData3 = 7;
static constexpr uint8_t kHubDisplayReset = 39;
static constexpr uint8_t kHubTouchReset = 40;
static constexpr uint8_t kHubTouchInt = 11;        // active-low, EXT1 wake
static constexpr uint8_t kHubI2sMclk = 42;
static constexpr uint8_t kHubI2sBclk = 9;
static constexpr uint8_t kHubI2sWs = 45;
static constexpr uint8_t kHubI2sDout = 8;
static constexpr uint8_t kHubI2sDin = 10;
static constexpr uint8_t kHubAudioAmpEnable = 46;
static constexpr uint8_t kHubSdCmd = 1;
static constexpr uint8_t kHubSdClk = 2;
static constexpr uint8_t kHubSdData0 = 3;
}  // namespace PinoutHub

namespace Hub {
static constexpr uint32_t kI2cClockHz = 400000;
static constexpr uint32_t kPollMs = 200;
static constexpr uint32_t kTouchPollMs = 20;
static constexpr uint32_t kMotionPollMs = 125;
static constexpr uint32_t kProbeRetryMs = 5000;
static constexpr uint8_t kAxp2101Address = 0x34;
static constexpr uint8_t kQmi8658Address = 0x6B;
static constexpr uint8_t kQmi8658AltAddress = 0x6A;
static constexpr uint8_t kPcf85063Address = 0x51;
static constexpr uint8_t kCst9217Address = 0x5A;
static constexpr uint8_t kTca9554Address = 0x20;
static constexpr uint8_t kEs8311Address = 0x18;
static constexpr uint8_t kEs7210Address = 0x40;
static constexpr uint16_t kTouchWidth = 466;
static constexpr uint16_t kTouchHeight = 466;
}  // namespace Hub

namespace HubPower {
static constexpr bool     kChargingEnabled = true;
static constexpr uint16_t kChargeCurrentMa = 1000;
static constexpr uint16_t kChargeVoltageMv = 4200;
static constexpr uint16_t kInputCurrentLimitMa = 1500;
static constexpr uint16_t kInputVoltageLimitMv = 4360;
static constexpr uint16_t kPrechargeCurrentMa = 50;
static constexpr uint16_t kTerminationCurrentMa = 25;
}  // namespace HubPower

namespace HubImu {
static constexpr uint8_t  kAccelRangeG = 4;
static constexpr uint32_t kAccelOdrMilliHz = 250000;
static constexpr uint16_t kGyroRangeDps = 512;
static constexpr uint32_t kGyroOdrMilliHz = 235000;
static constexpr bool     kEnableAccelLpf = false;
static constexpr bool     kEnableGyroLpf = true;
}  // namespace HubImu

namespace HubTouch {
static constexpr bool kSwapXY = true;
static constexpr bool kMirrorX = true;
static constexpr bool kMirrorY = true;
}  // namespace HubTouch

namespace HubDisplay {
static constexpr uint16_t kWidth = 466;
static constexpr uint16_t kHeight = 466;
static constexpr uint8_t  kBrightness = 255;
static constexpr uint8_t  kRotation = 0;
static constexpr uint8_t  kPanelColOffset1 = 6;
static constexpr uint8_t  kPanelRowOffset1 = 0;
static constexpr uint8_t  kDrawBufferDivisor = 8;
static constexpr uint32_t kLvglTickPeriodMs = 10;
static constexpr uint32_t kFramePeriodMs = 33;     // ~30 fps
}  // namespace HubDisplay

namespace HubMotionWake {
static constexpr uint8_t  kThresholdMg = 220;
static constexpr uint8_t  kBlankingSamples = 32;
static constexpr uint8_t  kAccelRangeG = 4;
static constexpr uint32_t kAccelOdrMilliHz = 128000;
}  // namespace HubMotionWake

namespace HubCodec {
static constexpr uint8_t kVolumePercent = 90;
static constexpr uint8_t kMicGainLevel = 3;
static constexpr bool    kMuted = false;
static constexpr bool    kAmpEnabled = false;
}  // namespace HubCodec

namespace HubSleep {
static constexpr uint32_t kIdleArmedAfterMs = 5000;
static constexpr uint32_t kDeepSleepAfterMs = 15000;
static constexpr uint32_t kShortWakeMs = 5000;
static constexpr uint8_t  kStormThreshold = 3;
static constexpr bool     kWakeOnTouchInt = true;
static constexpr bool     kWakeOnImuInt2 = true;
}  // namespace HubSleep

// HUB feature toggles. false => power the IC DOWN at boot, then skip polling.
namespace HubFeatures {
static constexpr bool kEnableDisplay = true;
static constexpr bool kEnableTouch = true;
static constexpr bool kEnableImu = true;          // also gates wake-on-motion
static constexpr bool kEnableRtc = true;
static constexpr bool kEnableIoExpander = true;
static constexpr bool kEnableAudio = false;       // codecs probed then powered down
static constexpr bool kEnableSleep = true;
static constexpr bool kEnablePmicTelemetry = true;
}  // namespace HubFeatures
```

- [ ] **Step 7: Create the `HubRuntime` skeleton**

`include/features/hub/HubRuntime.hpp`:

```cpp
/*
 * HubRuntime.hpp — top-level runtime for the HUB companion device.
 * Owns the shared I2C bus and every onboard-IC driver; begin() runs the
 * ordered power-up/probe/power-down bring-up, tick() runs periodic polling
 * and the LVGL pump. main_hub.cpp delegates entirely here (mirrors AppRuntime).
 * Implementation: src/features/hub/HubRuntime.cpp
 */
#pragma once
namespace HubRuntime {
void begin();   // one-shot bring-up; called once from setup()
void tick();    // one iteration of periodic work; called from loop()
}  // namespace HubRuntime
```

`src/features/hub/HubRuntime.cpp` (skeleton — drivers wired in later tasks):

```cpp
#include "features/hub/HubRuntime.hpp"
#include <Arduino.h>
#include "app_config.hpp"

namespace HubRuntime {

void begin() {
  Serial.begin(AppConfig::kSerialBaud);
  delay(AppConfig::kSerialStartupDelayMs);
  Serial.printf("[HUB] boot — firmware %s\n", AppConfig::kFirmwareVersion);
  Serial.printf("[HUB] free heap %u, PSRAM %u\n",
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
}

void tick() {
  delay(10);
}

}  // namespace HubRuntime
```

- [ ] **Step 8: Create the hub entry point**

`src/features/hub/main_hub.cpp`:

```cpp
#include <Arduino.h>
#include "features/hub/HubRuntime.hpp"

void setup() { HubRuntime::begin(); }
void loop()  { HubRuntime::tick(); }
```

- [ ] **Step 9: Build the hub env (empty firmware) and confirm the reactor still builds**

Run: `~/.platformio/penv/bin/pio run -e hub`
Expected: SUCCESS (`[SUCCESS]`). If the first run prints `xtensa-esp32s3-elf-g++: command not found`, re-run the same command — it then succeeds.

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: SUCCESS, unchanged from before.

- [ ] **Step 10: Commit**

```bash
git add platformio.ini boards/ partitions_hub_16mb.csv include/hub/ include/app_config.hpp include/features/hub/ src/features/hub/
git commit -m "feat(hub): Phase 1 build env + config skeleton (empty firmware boots)"
```

---

### Task 2: I2C bus + AXP2101 power authority

**Files:**
- Create: `include/features/hub/power/Axp2101Math.hpp` (pure decode — host-testable)
- Create: `include/features/hub/power/Axp2101.hpp`
- Create: `src/features/hub/power/Axp2101.cpp`
- Create: `test/test_axp2101_math/test_main.cpp`
- Modify: `include/features/hub/HubRuntime.hpp`, `src/features/hub/HubRuntime.cpp`

**Interfaces:**
- Consumes: `AppConfig::PinoutHub::kI2cSda/kI2cScl`, `AppConfig::Hub::kI2cClockHz/kAxp2101Address`, `AppConfig::HubPower::*`.
- Produces: `Axp2101::State`, `Axp2101::begin(int sda,int scl,uint32_t hz)`, `bool refreshStatus(State&)`, `bool powerDown()`, `bool disableAdcForSleep()`, and free functions `axp::decodeTemperatureC(int16_t)`, `axp::scaleVoltageMv(uint16_t raw)`.

- [ ] **Step 1: Write the failing test for the pure AXP decode logic**

`test/test_axp2101_math/test_main.cpp`:

```cpp
#include <unity.h>
#include "features/hub/power/Axp2101Math.hpp"

void setUp() {}
void tearDown() {}

void test_temperature_decode() {
  // Barebone formula: 22.0 + (7274 - raw)/20.0 ; raw=7274 => 22.0C
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 22.0f, axp::decodeTemperatureC(7274));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 32.0f, axp::decodeTemperatureC(7074)); // (7274-7074)/20=10
}

void test_voltage_scale_passthrough_mv() {
  // 11-bit ADC, 1 mV/LSB nominal
  TEST_ASSERT_EQUAL_UINT16(4200, axp::scaleVoltageMv(4200));
  TEST_ASSERT_EQUAL_UINT16(0, axp::scaleVoltageMv(0));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_temperature_decode);
  RUN_TEST(test_voltage_scale_passthrough_mv);
  return UNITY_END();
}
```

- [ ] **Step 2: Run it to verify it fails (header missing)**

Run: `~/.platformio/penv/bin/pio test -e native -f test_axp2101_math`
Expected: FAIL — `Axp2101Math.hpp: No such file or directory`.

- [ ] **Step 3: Write the pure decode header**

`include/features/hub/power/Axp2101Math.hpp` (extracted verbatim from `../Barebone/src/features/hub/HubFeature.cpp:1749-1797`):

```cpp
#pragma once
#include <stdint.h>
namespace axp {
inline float decodeTemperatureC(int16_t raw) { return 22.0f + (7274.0f - (float)raw) / 20.0f; }
inline uint16_t scaleVoltageMv(uint16_t raw) { return raw; }  // 1 mV/LSB nominal
}  // namespace axp
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `~/.platformio/penv/bin/pio test -e native -f test_axp2101_math`
Expected: PASS (2 tests).

- [ ] **Step 5: Write the `Axp2101` driver header**

`include/features/hub/power/Axp2101.hpp` — driver shaped like `Husb238` (full new code):

```cpp
/*
 * Axp2101.hpp — AXP2101 PMIC/charger driver (power authority).
 * Owns charge configuration, battery/VBUS telemetry, and the low-power
 * paths used before deep sleep. Register sequence ported verbatim from
 * Barebone HubFeature. Implementation: src/features/hub/power/Axp2101.cpp
 */
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "features/hub/power/Axp2101Math.hpp"

class Axp2101 {
 public:
  struct State {
    bool present = false;
    bool ready = false;
    uint8_t chipId = 0;
    bool batteryPresent = false;
    bool vbusPresent = false;
    bool charging = false;
    uint16_t batteryVoltageMv = 0;
    uint16_t vbusVoltageMv = 0;
    uint16_t systemVoltageMv = 0;
    int batteryPercent = -1;
    float temperatureC = NAN;
  };

  explicit Axp2101(TwoWire& wire = Wire, uint8_t address = 0x34);
  bool begin(int sdaPin, int sclPin, uint32_t frequencyHz);  // also starts the bus
  bool probe();
  bool configureCharging();   // applies AppConfig::HubPower::*
  bool refreshStatus(State& out);
  bool disableAdcForSleep();  // stop batt/vbus/sys/temp measurement
  bool powerDown();           // lowest-power state for unused-by-telemetry mode
  const State& state() const { return state_; }
  const char* lastErrorString() const { return lastError_; }

 private:
  static constexpr uint8_t kRegStatus1 = 0x00;
  static constexpr uint8_t kRegStatus2 = 0x01;
  static constexpr uint8_t kRegChipId  = 0x03;
  static constexpr uint8_t kRegAdcCtrl = 0x30;
  static constexpr uint8_t kRegAdcBatt = 0x34;
  static constexpr uint8_t kRegAdcVbus = 0x38;
  static constexpr uint8_t kRegAdcSys  = 0x3A;
  static constexpr uint8_t kRegAdcTemp = 0x3C;
  static constexpr uint8_t kRegBattPct = 0xA4;
  bool readReg(uint8_t reg, uint8_t& v);
  bool writeReg(uint8_t reg, uint8_t v);
  bool readRegs(uint8_t reg, uint8_t* buf, size_t n);
  TwoWire& wire_;
  uint8_t address_;
  State state_;
  const char* lastError_ = "";
};
```

- [ ] **Step 6: Write the `Axp2101` driver implementation (port verbatim)**

`src/features/hub/power/Axp2101.cpp`: implement `readReg/writeReg/readRegs` with the standard Wire pattern from `../Barebone/src/features/hub/HubFeature.cpp` I2C helpers; port `begin()`/`probe()` from `:1619-1642`, `configureCharging()` from `:1644-1727`, `refreshStatus()` from `:1749-1797` (use `axp::decodeTemperatureC`/`axp::scaleVoltageMv`; battery % is a passthrough of reg `0xA4`), `disableAdcForSleep()` from `:1729-1747`, and `powerDown()` from the same ADC-disable + rail logic — all per the Porting Convention. Probe expects chip id `0x4A` at reg `0x03`.

- [ ] **Step 7: Wire AXP2101 into HubRuntime as the first bring-up step**

In `src/features/hub/HubRuntime.cpp`: add `#include "features/hub/power/Axp2101.hpp"`, a file-scope `Axp2101 g_axp;`, and in `begin()` after the banner:

```cpp
  if (g_axp.begin(AppConfig::PinoutHub::kI2cSda, AppConfig::PinoutHub::kI2cScl,
                  AppConfig::Hub::kI2cClockHz)) {
    g_axp.configureCharging();
    Serial.println("[HUB] axp2101: enabled");
  } else {
    Serial.printf("[HUB] axp2101: FAULT (%s)\n", g_axp.lastErrorString());
  }
```

In `tick()`, poll every `AppConfig::Hub::kPollMs` (use a `static uint32_t last;` + `millis()` gate) and, when `AppConfig::HubFeatures::kEnablePmicTelemetry`, call `g_axp.refreshStatus(s)` and `Serial.printf("[HUB] batt %d%% %umV vbus=%d\n", s.batteryPercent, s.batteryVoltageMv, s.vbusPresent);`.

- [ ] **Step 8: Build, then verify on device**

Run: `~/.platformio/penv/bin/pio run -e hub`
Expected: SUCCESS.
On device (`-t upload` then `~/.platformio/penv/bin/pio device monitor`): boot log shows `[HUB] axp2101: enabled` and periodic battery telemetry.

- [ ] **Step 9: Commit**

```bash
git add include/features/hub/power/ src/features/hub/power/ test/test_axp2101_math/ include/features/hub/HubRuntime.hpp src/features/hub/HubRuntime.cpp
git commit -m "feat(hub): AXP2101 PMIC driver + I2C bus bring-up (power authority)"
```

---

### Task 3: CO5300 panel + slim LVGL runtime (HubDisplay)

**Files:**
- Create: `include/features/hub/display/PanelGeometry.hpp` (pure rotation/offset math — host-testable)
- Create: `include/features/hub/display/Co5300Panel.hpp`, `src/features/hub/display/Co5300Panel.cpp`
- Create: `include/features/hub/display/HubDisplay.hpp`, `src/features/hub/display/HubDisplay.cpp`
- Create: `test/test_panel_geometry/test_main.cpp`
- Modify: `src/features/hub/HubRuntime.cpp`

**Interfaces:**
- Consumes: `AppConfig::PinoutHub::kHubDisplay*`, `AppConfig::HubDisplay::*`.
- Produces: `geom::rotationMadctl(uint8_t rot)`, `geom::columnOffset(uint8_t rot)`, `geom::rowOffset(uint8_t rot)`; `Co5300Panel::begin(String&)/flush(const lv_area_t&, const lv_color_t*)/enterSleep()/exitSleep()`; `HubDisplay::begin()/tick()/isReady()`.

- [ ] **Step 1: Write the failing test for panel geometry**

`test/test_panel_geometry/test_main.cpp`:

```cpp
#include <unity.h>
#include "features/hub/display/PanelGeometry.hpp"
void setUp() {} void tearDown() {}
void test_madctl_rotations() {
  TEST_ASSERT_EQUAL_HEX8(0x00, geom::rotationMadctl(0));
  TEST_ASSERT_EQUAL_HEX8(0x02, geom::rotationMadctl(1));
  TEST_ASSERT_EQUAL_HEX8(0x07, geom::rotationMadctl(2));
  TEST_ASSERT_EQUAL_HEX8(0x05, geom::rotationMadctl(3));
}
void test_col_offset_rot0() { TEST_ASSERT_EQUAL_UINT16(6, geom::columnOffset(0)); }
int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_madctl_rotations);
  RUN_TEST(test_col_offset_rot0);
  return UNITY_END();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `~/.platformio/penv/bin/pio test -e native -f test_panel_geometry`
Expected: FAIL — `PanelGeometry.hpp: No such file or directory`.

- [ ] **Step 3: Write the pure geometry header**

`include/features/hub/display/PanelGeometry.hpp` (extracted from `../Barebone/src/features/hubdisplay/HubDisplayPanel.cpp:541-581`, using `AppConfig::HubDisplay::kPanelColOffset1/kPanelRowOffset1`):

```cpp
#pragma once
#include <stdint.h>
#include "app_config.hpp"
namespace geom {
inline uint8_t rotationMadctl(uint8_t rot) {
  switch (rot & 0x3) { case 1: return 0x02; case 2: return 0x07; case 3: return 0x05; default: return 0x00; }
}
inline uint16_t columnOffset(uint8_t rot) { return (rot & 0x1) ? AppConfig::HubDisplay::kPanelRowOffset1
                                                               : AppConfig::HubDisplay::kPanelColOffset1; }
inline uint16_t rowOffset(uint8_t rot)    { return (rot & 0x1) ? AppConfig::HubDisplay::kPanelColOffset1
                                                               : AppConfig::HubDisplay::kPanelRowOffset1; }
}  // namespace geom
```

- [ ] **Step 4: Run to verify it passes**

Run: `~/.platformio/penv/bin/pio test -e native -f test_panel_geometry`
Expected: PASS (2 tests).

- [ ] **Step 5: Port the CO5300 panel driver**

Create `include/features/hub/display/Co5300Panel.hpp` with the public API:

```cpp
#pragma once
#include <Arduino.h>
#include <lvgl.h>
class Co5300Panel {
 public:
  bool begin(String& lastError);
  bool flush(const lv_area_t& area, const lv_color_t* colors);
  bool enterSleep();
  bool exitSleep();
  bool isReady() const { return ready_; }
 private:
  bool ready_ = false;
  // SPI handle + DMA buffers declared per the ported implementation.
};
```

Create `src/features/hub/display/Co5300Panel.cpp` by porting `../Barebone/src/features/hubdisplay/HubDisplayPanel.cpp` **in full** per the Porting Convention: keep `initializeSpi()` (SPI2_HOST, 80 MHz, dual 4096-px DMA buffers), `initializeController()` (CO5300 reset + init sequence), `setAddressWindow()`, the pipelined dual-buffer `flush()` with the RGB565 word-swap (`:224-235`), and `enterSleep()/exitSleep()`. Use `geom::rotationMadctl/columnOffset/rowOffset` for MADCTL+offsets. Drop the `PRODUCT_MODE==5` guard.

- [ ] **Step 6: Port the slim LVGL runtime (no scene engine)**

Create `include/features/hub/display/HubDisplay.hpp`:

```cpp
#pragma once
#include <Arduino.h>
class HubDisplay {
 public:
  bool begin();             // lv_init + draw buffer + driver + tick timer
  void tick();              // lv_timer_handler()
  bool enterSleep();        // panel.enterSleep() + stop tick timer
  bool exitSleep();
  bool isReady() const { return ready_; }
 private:
  bool ready_ = false;
};
```

Create `src/features/hub/display/HubDisplay.cpp` by porting ONLY the LVGL-runtime portions of `../Barebone/src/features/hubdisplay/HubDisplayFeature.cpp`:
- draw-buffer alloc `(466*466)/kDrawBufferDivisor` via `heap_caps_malloc(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)` with internal-SRAM fallback (`:633-660` region),
- `lv_init()`, `lv_disp_draw_buf_init` (single buffer), `lv_disp_drv_init`/register with `hor_res=ver_res=466`, the `flushDisplay` callback delegating to `Co5300Panel::flush` then `lv_disp_flush_ready` (`:274-286`), the even-align `rounderCallback` (`:178-195`), and the `esp_timer` periodic `lv_tick_inc` tick at `kLvglTickPeriodMs` (`:174-176`, `:730-760`).
- **Do NOT port:** `hubscene::*`, `sceneBuf`, gyro/quaternion, gravity, `Inputs`, `VisualState`, gesture handling.
- `tick()` calls `lv_timer_handler()`.

- [ ] **Step 7: Show a hello label and wire into HubRuntime**

In `src/features/hub/HubRuntime.cpp`: add `#include "features/hub/display/HubDisplay.hpp"` + `#include <lvgl.h>`, a file-scope `HubDisplay g_display;`, and in `begin()` (gated on `AppConfig::HubFeatures::kEnableDisplay`):

```cpp
  if (AppConfig::HubFeatures::kEnableDisplay && g_display.begin()) {
    lv_obj_t* label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "MINI-REACTOR HUB");
    lv_obj_center(label);
    Serial.println("[HUB] display: enabled");
  } else {
    Serial.println("[HUB] display: disabled");
  }
```

In `tick()`, call `g_display.tick();` every loop.

- [ ] **Step 8: Build and verify on device**

Run: `~/.platformio/penv/bin/pio run -e hub`
Expected: SUCCESS.
On device: AMOLED shows a centered "MINI-REACTOR HUB" label.

- [ ] **Step 9: Commit**

```bash
git add include/features/hub/display/ src/features/hub/display/ test/test_panel_geometry/ src/features/hub/HubRuntime.cpp
git commit -m "feat(hub): CO5300 panel + slim LVGL runtime (label renders)"
```

---

### Task 4: CST9217 touch + transform

**Files:**
- Create: `include/features/hub/touch/HubTouchTransform.hpp` (pure — host-testable)
- Create: `include/features/hub/touch/Cst9217.hpp`, `src/features/hub/touch/Cst9217.cpp`
- Create: `test/test_touch_transform/test_main.cpp`
- Modify: `src/features/hub/HubRuntime.cpp`, `src/features/hub/display/HubDisplay.cpp` (register LVGL indev)

**Interfaces:**
- Consumes: `AppConfig::PinoutHub::kHubTouchReset/kHubTouchInt`, `AppConfig::Hub::kCst9217Address/kTouchWidth/kTouchHeight`, `AppConfig::HubTouch::*`.
- Produces: `HubTouchTransform::Mapping`, `HubTouchTransform::apply(int16_t&,int16_t&,uint16_t,uint16_t,const Mapping&)`; `Cst9217::TouchPoint`, `Cst9217::begin()/refresh(State&)/readPoint(int16_t& x,int16_t& y,bool& pressed)`.

- [ ] **Step 1: Write the failing transform test**

`test/test_touch_transform/test_main.cpp`:

```cpp
#include <unity.h>
#include "features/hub/touch/HubTouchTransform.hpp"
void setUp() {} void tearDown() {}
void test_identity() {
  int16_t x = 10, y = 20; HubTouchTransform::Mapping m{false,false,false};
  HubTouchTransform::apply(x, y, 466, 466, m);
  TEST_ASSERT_EQUAL_INT16(10, x); TEST_ASSERT_EQUAL_INT16(20, y);
}
void test_swap_and_mirror() {
  int16_t x = 0, y = 0; HubTouchTransform::Mapping m{true,true,true};  // swapXY, mirrorX, mirrorY
  HubTouchTransform::apply(x, y, 466, 466, m);   // swap->(0,0); mirrorX->465; mirrorY->465
  TEST_ASSERT_EQUAL_INT16(465, x); TEST_ASSERT_EQUAL_INT16(465, y);
}
int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_identity);
  RUN_TEST(test_swap_and_mirror);
  return UNITY_END();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `~/.platformio/penv/bin/pio test -e native -f test_touch_transform`
Expected: FAIL — header missing.

- [ ] **Step 3: Port the pure transform header (verbatim math)**

`include/features/hub/touch/HubTouchTransform.hpp` — copy `../Barebone/include/features/hub/HubTouchTransform.hpp:1-39` verbatim (it is already dependency-free: `<utility>` for `std::swap`).

- [ ] **Step 4: Run to verify it passes**

Run: `~/.platformio/penv/bin/pio test -e native -f test_touch_transform`
Expected: PASS (2 tests).

- [ ] **Step 5: Write the `Cst9217` driver header**

`include/features/hub/touch/Cst9217.hpp`:

```cpp
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "features/hub/touch/HubTouchTransform.hpp"
class Cst9217 {
 public:
  struct TouchPoint { bool valid=false; int16_t x=0; int16_t y=0; };
  struct State { bool present=false; bool ready=false; uint16_t chipId=0;
                 uint8_t pointCount=0; TouchPoint points[2]={}; };
  explicit Cst9217(TwoWire& wire = Wire, uint8_t address = 0x5A);
  bool begin();                         // reset + enter cmd mode + read id/resolution
  bool refresh(State& out);             // read+ACK+parse+transform, ~20 ms cadence
  bool readPoint(int16_t& x, int16_t& y, bool& pressed);  // convenience for LVGL indev
  bool enterSleep();
  const State& state() const { return state_; }
 private:
  bool writeReg16(uint16_t reg, const uint8_t* data, size_t n);
  bool readReg16(uint16_t reg, uint8_t* buf, size_t n);
  TwoWire& wire_; uint8_t address_; State state_;
};
```

- [ ] **Step 6: Port the `Cst9217` implementation**

`src/features/hub/touch/Cst9217.cpp`: port `initializeCst9217()` (`../Barebone/src/features/hub/HubFeature.cpp:1970-2047`) into `begin()` (GPIO reset via `kHubTouchReset`, enter debug mode `0xD101`, read resolution `0xD1F8`, chip id `0xD204`, validate `0x9217`/`0x9220`) and `refreshCst9217Status()` (`:2049-2115`) into `refresh()` (read `0xD000` block, ACK `0xAB`, parse points, apply `HubTouchTransform::apply` with `AppConfig::HubTouch::*`). IRQ read via `digitalRead(AppConfig::PinoutHub::kHubTouchInt)`. `readPoint()` returns the first valid point. Per the Porting Convention; drop NVS calibration persistence (use compile-time `AppConfig::HubTouch` defaults).

- [ ] **Step 7: Register the touch as an LVGL input device**

Add to `HubDisplay` a hook to register an indev, OR (simpler) register it from `HubRuntime` after `g_display.begin()`. In `src/features/hub/HubRuntime.cpp` add a file-scope `Cst9217 g_touch;` and an LVGL read callback:

```cpp
static void hubTouchRead(lv_indev_drv_t*, lv_indev_data_t* data) {
  int16_t x, y; bool pressed;
  if (g_touch.readPoint(x, y, pressed)) {
    data->point.x = x; data->point.y = y;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  } else { data->state = LV_INDEV_STATE_RELEASED; }
}
```

In `begin()` (gated on `kEnableTouch`): `g_touch.begin()`, then `static lv_indev_drv_t d; lv_indev_drv_init(&d); d.type = LV_INDEV_TYPE_POINTER; d.read_cb = hubTouchRead; lv_indev_drv_register(&d);` and log `[HUB] touch: enabled`. In `tick()`, call `g_touch.refresh(ts)` on the `kTouchPollMs` cadence.

- [ ] **Step 8: Build and verify on device**

Run: `~/.platformio/penv/bin/pio run -e hub`
Expected: SUCCESS. On device: dragging on the panel moves an LVGL focus / the label can be made draggable; serial logs transformed coordinates within 0..465.

- [ ] **Step 9: Commit**

```bash
git add include/features/hub/touch/ src/features/hub/touch/ test/test_touch_transform/ src/features/hub/HubRuntime.cpp
git commit -m "feat(hub): CST9217 touch driver + transform (LVGL input live)"
```

---

### Task 5: QMI8658 IMU + wake-on-motion

**Files:**
- Create: `include/features/hub/sensor/Qmi8658Math.hpp` (pure decode — host-testable)
- Create: `include/features/hub/sensor/MotionStatus.hpp` (normalized struct, ported)
- Create: `include/features/hub/sensor/Qmi8658.hpp`, `src/features/hub/sensor/Qmi8658.cpp`
- Create: `test/test_qmi8658_math/test_main.cpp`
- Modify: `src/features/hub/HubRuntime.cpp`

**Interfaces:**
- Consumes: `AppConfig::Hub::kQmi8658Address/kQmi8658AltAddress`, `AppConfig::HubImu::*`, `AppConfig::HubMotionWake::*`, `AppConfig::PinoutHub::kImuInt2`.
- Produces: `qmi::decodeS16(const uint8_t*)`, `qmi::accelSensitivityGPerLsb(uint8_t)`, `qmi::gyroSensitivityDpsPerLsb(uint16_t)`, `qmi::decodeTemperatureC(int16_t)`; `MotionSensor::Status`; `Qmi8658::Config`, `Qmi8658::begin(const Config&)/poll(Status&)/enterWakeOnMotionMode(...)`.

- [ ] **Step 1: Write the failing decode test**

`test/test_qmi8658_math/test_main.cpp`:

```cpp
#include <unity.h>
#include "features/hub/sensor/Qmi8658Math.hpp"
void setUp() {} void tearDown() {}
void test_s16_little_endian() {
  uint8_t b[2] = {0x34, 0x12};
  TEST_ASSERT_EQUAL_INT16(0x1234, qmi::decodeS16(b));
}
void test_temp_decode() { TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, qmi::decodeTemperatureC(256)); }
void test_accel_sensitivity() { TEST_ASSERT_FLOAT_WITHIN(1e-7f, 4.0f/32768.0f, qmi::accelSensitivityGPerLsb(4)); }
int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_s16_little_endian);
  RUN_TEST(test_temp_decode);
  RUN_TEST(test_accel_sensitivity);
  return UNITY_END();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `~/.platformio/penv/bin/pio test -e native -f test_qmi8658_math`
Expected: FAIL — header missing.

- [ ] **Step 3: Write the pure decode header (verbatim from the standalone driver)**

`include/features/hub/sensor/Qmi8658Math.hpp` (from `../Barebone/src/sensors/Qmi8658.cpp:750-765`):

```cpp
#pragma once
#include <stdint.h>
namespace qmi {
inline int16_t decodeS16(const uint8_t* b) { return (int16_t)(((uint16_t)b[1] << 8) | b[0]); }
inline float decodeTemperatureC(int16_t raw) { return raw / 256.0f; }
inline float accelSensitivityGPerLsb(uint8_t rangeG) { return (float)rangeG / 32768.0f; }
inline float gyroSensitivityDpsPerLsb(uint16_t rangeDps) { return (float)rangeDps / 32768.0f; }
}  // namespace qmi
```

- [ ] **Step 4: Run to verify it passes**

Run: `~/.platformio/penv/bin/pio test -e native -f test_qmi8658_math`
Expected: PASS (3 tests).

- [ ] **Step 5: Port `MotionSensor::Status`**

`include/features/hub/sensor/MotionStatus.hpp` — copy the `MotionSensor::Status` struct definition verbatim from its Barebone home (search `../Barebone/include` for `namespace MotionSensor`/`struct Status`), keeping only POD fields (accel g, gyro dps, temperature, timestamp, validity). Drop any methods that reference `Qmi8658::Status` types not ported.

- [ ] **Step 6: Port the `Qmi8658` driver**

Copy `../Barebone/include/sensors/Qmi8658.hpp` → `include/features/hub/sensor/Qmi8658.hpp` and `../Barebone/src/sensors/Qmi8658.cpp` → `src/features/hub/sensor/Qmi8658.cpp` (this driver is already class-shaped with `TwoWire&`+address ctor, `begin(Config&)`, `poll(Status&)`, `enterWakeOnMotionMode(WakeOnMotionConfig&)`). Apply the Porting Convention: replace the include of the pure decode with `features/hub/sensor/Qmi8658Math.hpp`, replace `AppConfig::*`/`AppLog::*`, delete `MotionLightControl` hooks. Build the `Config`/`WakeOnMotionConfig` from `AppConfig::HubImu::*` / `AppConfig::HubMotionWake::*`. WHO_AM_I expects `0x05`; probe configured addr then `kQmi8658AltAddress`.

- [ ] **Step 7: Wire into HubRuntime**

In `src/features/hub/HubRuntime.cpp`: file-scope `Qmi8658 g_imu;`. In `begin()` (gated `kEnableImu`): build config, `g_imu.begin(cfg)`, log `[HUB] imu: enabled` (or `disabled (powered down)` when off — in the off branch leave the IMU unconfigured so it stays in its default low-power state). In `tick()`: on `kMotionPollMs` cadence call `g_imu.poll(st)` and log accel/gyro.

- [ ] **Step 8: Build and verify on device**

Run: `~/.platformio/penv/bin/pio run -e hub`
Expected: SUCCESS. On device: tilting the board changes the logged accel values.

- [ ] **Step 9: Commit**

```bash
git add include/features/hub/sensor/ src/features/hub/sensor/ test/test_qmi8658_math/ src/features/hub/HubRuntime.cpp
git commit -m "feat(hub): QMI8658 IMU driver + wake-on-motion config"
```

---

### Task 6: PCF85063 RTC

**Files:**
- Create: `include/features/hub/rtc/Pcf85063Bcd.hpp` (pure BCD — host-testable)
- Create: `include/features/hub/rtc/Pcf85063.hpp`, `src/features/hub/rtc/Pcf85063.cpp`
- Create: `test/test_pcf85063_bcd/test_main.cpp`
- Modify: `src/features/hub/HubRuntime.cpp`

**Interfaces:**
- Consumes: `AppConfig::Hub::kPcf85063Address`.
- Produces: `rtc::bcdToUint(uint8_t)`, `rtc::uintToBcd(uint8_t)`; `Pcf85063::DateTime`, `Pcf85063::begin()/refresh(DateTime&)/setDateTime(const DateTime&)`.

- [ ] **Step 1: Write the failing BCD test**

`test/test_pcf85063_bcd/test_main.cpp`:

```cpp
#include <unity.h>
#include "features/hub/rtc/Pcf85063Bcd.hpp"
void setUp() {} void tearDown() {}
void test_bcd_to_uint() { TEST_ASSERT_EQUAL_UINT8(42, rtc::bcdToUint(0x42)); }
void test_uint_to_bcd() { TEST_ASSERT_EQUAL_HEX8(0x42, rtc::uintToBcd(42)); }
void test_roundtrip() { for (uint8_t v=0; v<60; ++v) TEST_ASSERT_EQUAL_UINT8(v, rtc::bcdToUint(rtc::uintToBcd(v))); }
int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_bcd_to_uint);
  RUN_TEST(test_uint_to_bcd);
  RUN_TEST(test_roundtrip);
  return UNITY_END();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `~/.platformio/penv/bin/pio test -e native -f test_pcf85063_bcd`
Expected: FAIL — header missing.

- [ ] **Step 3: Write the pure BCD header**

`include/features/hub/rtc/Pcf85063Bcd.hpp` (from `../Barebone/src/features/hub/HubFeature.cpp:297-298`):

```cpp
#pragma once
#include <stdint.h>
namespace rtc {
inline uint8_t bcdToUint(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
inline uint8_t uintToBcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
}  // namespace rtc
```

- [ ] **Step 4: Run to verify it passes**

Run: `~/.platformio/penv/bin/pio test -e native -f test_pcf85063_bcd`
Expected: PASS (3 tests).

- [ ] **Step 5: Write the `Pcf85063` driver header**

`include/features/hub/rtc/Pcf85063.hpp`:

```cpp
#pragma once
#include <Arduino.h>
#include <Wire.h>
class Pcf85063 {
 public:
  struct DateTime { uint16_t year=2000; uint8_t month=1, day=1, weekday=0, hours=0, minutes=0, seconds=0; bool running=false; };
  explicit Pcf85063(TwoWire& wire = Wire, uint8_t address = 0x51);
  bool begin();                       // probe + set 24h mode + start clock
  bool refresh(DateTime& out);        // read 7 BCD time bytes
  bool setDateTime(const DateTime& dt);
  const DateTime& state() const { return state_; }
 private:
  static constexpr uint8_t kRegCtrl1 = 0x00;
  static constexpr uint8_t kRegTime  = 0x04;  // sec,min,hr,day,wday,mon,year
  bool readReg(uint8_t reg, uint8_t* buf, size_t n);
  bool writeReg(uint8_t reg, const uint8_t* buf, size_t n);
  TwoWire& wire_; uint8_t address_; DateTime state_;
};
```

- [ ] **Step 6: Port the `Pcf85063` implementation**

`src/features/hub/rtc/Pcf85063.cpp`: port `initializePcf85063()` (`../Barebone/src/features/hub/HubFeature.cpp:1908-1942`) into `begin()` (probe `0x51`, RAM r/w check at `0x03`, clear CTRL1 bits `0x02`+`0x20` for 24h+run), `refreshPcf85063Status()` (`:1944-1968`) into `refresh()` (read `0x04` block, decode with `rtc::bcdToUint`, `running = (ctrl1 & 0x20)==0`), and `setRtcDateTime()` (`:904-994`, the I2C write portion only) into `setDateTime()` (encode with `rtc::uintToBcd`). Drop NVS/`time()`/network sync.

- [ ] **Step 7: Wire into HubRuntime**

File-scope `Pcf85063 g_rtc;`. In `begin()` (gated `kEnableRtc`): `g_rtc.begin()` + log `[HUB] rtc: enabled` (off branch logs `disabled`). In `tick()` (peripheral cadence): `g_rtc.refresh(dt)` and log `HH:MM:SS`.

- [ ] **Step 8: Build and verify on device**

Run: `~/.platformio/penv/bin/pio run -e hub`
Expected: SUCCESS. On device: RTC time logged and advancing each second.

- [ ] **Step 9: Commit**

```bash
git add include/features/hub/rtc/ src/features/hub/rtc/ test/test_pcf85063_bcd/ src/features/hub/HubRuntime.cpp
git commit -m "feat(hub): PCF85063 RTC driver"
```

---

### Task 7: TCA9554 IO expander

**Files:**
- Create: `include/features/hub/io/Tca9554.hpp`, `src/features/hub/io/Tca9554.cpp`
- Modify: `src/features/hub/HubRuntime.cpp`

**Interfaces:**
- Consumes: `AppConfig::Hub::kTca9554Address`.
- Produces: `Tca9554::State`, `Tca9554::begin()/refresh(State&)/configure(uint8_t out,uint8_t pol,uint8_t cfg)`.

- [ ] **Step 1: Write the driver header**

`include/features/hub/io/Tca9554.hpp`:

```cpp
#pragma once
#include <Arduino.h>
#include <Wire.h>
class Tca9554 {
 public:
  struct State { bool present=false; bool ready=false; uint8_t input=0, output=0, polarity=0, config=0xFF; };
  explicit Tca9554(TwoWire& wire = Wire, uint8_t address = 0x20);
  bool begin();                                          // probe + default config
  bool configure(uint8_t output, uint8_t polarity, uint8_t config);
  bool refresh(State& out);                              // read all 4 regs
  const State& state() const { return state_; }
 private:
  static constexpr uint8_t kRegInput=0x00, kRegOutput=0x01, kRegPolarity=0x02, kRegConfig=0x03;
  bool readReg(uint8_t reg, uint8_t& v);
  bool writeReg(uint8_t reg, uint8_t v);
  TwoWire& wire_; uint8_t address_; State state_;
};
```

- [ ] **Step 2: Port the implementation**

`src/features/hub/io/Tca9554.cpp`: port `initializeTca9554()`/`configureTca9554()`/`refreshTca9554Status()` (`../Barebone/src/features/hub/HubFeature.cpp:2117-2158`) per the Porting Convention. `begin()` probes `0x20` then `configure(0x00, 0x00, 0xFF)` (all inputs, no inversion) unless a different default is needed; `refresh()` reads regs `0x00..0x03`.

- [ ] **Step 3: Wire into HubRuntime**

File-scope `Tca9554 g_io;`. In `begin()` (gated `kEnableIoExpander`): `g_io.begin()` + log `[HUB] ioexp: enabled`/`disabled`. In `tick()` (peripheral cadence): `g_io.refresh(s)` and log `input=0x%02X`.

- [ ] **Step 4: Build and verify on device**

Run: `~/.platformio/penv/bin/pio run -e hub`
Expected: SUCCESS. On device: `[HUB] ioexp: enabled` and a plausible input byte.

- [ ] **Step 5: Commit**

```bash
git add include/features/hub/io/ src/features/hub/io/ src/features/hub/HubRuntime.cpp
git commit -m "feat(hub): TCA9554 IO expander driver"
```

---

### Task 8: ES8311 + ES7210 audio — probe then power down

**Files:**
- Create: `include/features/hub/audio/Es8311.hpp`, `src/features/hub/audio/Es8311.cpp`
- Create: `include/features/hub/audio/Es7210.hpp`, `src/features/hub/audio/Es7210.cpp`
- Modify: `src/features/hub/HubRuntime.cpp`

**Interfaces:**
- Consumes: `AppConfig::Hub::kEs8311Address/kEs7210Address`, `AppConfig::HubFeatures::kEnableAudio`.
- Produces: `Es8311::begin()/powerDown()/present()`, `Es7210::begin()/powerDown()/present()`.

- [ ] **Step 1: Write the ES8311 driver header**

`include/features/hub/audio/Es8311.hpp`:

```cpp
#pragma once
#include <Arduino.h>
#include <Wire.h>
class Es8311 {
 public:
  explicit Es8311(TwoWire& wire = Wire, uint8_t address = 0x18);
  bool begin();        // probe only (no I2S — Phase 1)
  bool powerDown();    // verbatim low-power register sequence
  bool present() const { return present_; }
 private:
  bool writeReg(uint8_t reg, uint8_t v);
  TwoWire& wire_; uint8_t address_; bool present_=false;
};
```

- [ ] **Step 2: Port the ES8311 implementation (verbatim power-down)**

`src/features/hub/audio/Es8311.cpp`: `begin()` = probe `0x18` (`../Barebone/src/features/hub/HubFeature.cpp:2160-2174`, probe portion only — DO NOT port `configureEs8311`, that is I2S/playback for Phase 2). `powerDown()` = the verbatim register writes from `:2233-2250` (DAC vol 0, ADC gain 0, ADC/DAC/clock/bias power-down). Keep the `kEs8311*Reg` constants as `static constexpr` in the .cpp.

- [ ] **Step 3: Write + port the ES7210 driver**

`include/features/hub/audio/Es7210.hpp` (same shape as `Es8311`, address `0x40`). `src/features/hub/audio/Es7210.cpp`: `begin()` = probe `0x40`; `powerDown()` = verbatim writes from `../Barebone/src/features/hub/HubFeature.cpp:2278-2325` (`powerDownEs7210`: all mic power regs `0xFF`, analog `0xC0`, clock-off `0x7F`, power-down `0x07`).

- [ ] **Step 4: Wire into HubRuntime (probe → power down)**

File-scope `Es8311 g_codec; Es7210 g_mic;`. In `begin()`:

```cpp
  if (AppConfig::HubFeatures::kEnableAudio) {
    g_codec.begin(); g_mic.begin();
    Serial.println("[HUB] audio: enabled");   // Phase 1 still doesn't stream; placeholder for Phase 2
  } else {
    bool c = g_codec.begin(), m = g_mic.begin();
    if (c) g_codec.powerDown();
    if (m) g_mic.powerDown();
    Serial.printf("[HUB] es8311: %s, es7210: %s (powered down)\n",
                  c ? "present" : "absent", m ? "present" : "absent");
  }
```

- [ ] **Step 5: Build and verify on device**

Run: `~/.platformio/penv/bin/pio run -e hub`
Expected: SUCCESS. On device: `[HUB] es8311: present, es7210: present (powered down)`.

- [ ] **Step 6: Commit**

```bash
git add include/features/hub/audio/ src/features/hub/audio/ src/features/hub/HubRuntime.cpp
git commit -m "feat(hub): ES8311/ES7210 probe + verbatim power-down"
```

---

### Task 9: HubSleepLogic — pure FSM + deep-sleep entry/wake

**Files:**
- Create: `include/features/hub/sleep/HubSleepLogic.hpp`, `src/features/hub/sleep/HubSleepLogic.cpp`
- Create: `test/test_hub_sleep_logic/test_main.cpp`
- Modify: `src/features/hub/HubRuntime.cpp`

**Interfaces:**
- Consumes: `AppConfig::HubSleep::*`, `AppConfig::PinoutHub::kHubTouchInt/kImuInt2`, `AppConfig::HubFeatures::kEnableSleep`.
- Produces: `hubsleep::SleepInputs`, `hubsleep::shouldEnterDeepSleep(const SleepInputs&)`, `hubsleep::WakeSource`, `hubsleep::StormState`, `hubsleep::evaluateStorm(StormState&, WakeSource, bool, uint32_t)`.

- [ ] **Step 1: Write the failing FSM test**

`test/test_hub_sleep_logic/test_main.cpp`:

```cpp
#include <unity.h>
#include "features/hub/sleep/HubSleepLogic.hpp"
void setUp() {} void tearDown() {}
void test_external_power_suppresses_sleep() {
  hubsleep::SleepInputs in; in.externalPowerPresent=true; in.idleMs=100000; in.idleThresholdMs=15000;
  TEST_ASSERT_FALSE(hubsleep::shouldEnterDeepSleep(in));
}
void test_touch_suppresses_sleep() {
  hubsleep::SleepInputs in; in.touchActive=true; in.idleMs=100000; in.idleThresholdMs=15000;
  TEST_ASSERT_FALSE(hubsleep::shouldEnterDeepSleep(in));
}
void test_idle_triggers_sleep() {
  hubsleep::SleepInputs in; in.idleMs=20000; in.idleThresholdMs=15000;
  TEST_ASSERT_TRUE(hubsleep::shouldEnterDeepSleep(in));
}
void test_storm_suppresses_after_threshold() {
  hubsleep::StormState s;
  hubsleep::StormVerdict v;
  for (int i=0;i<3;i++) v = hubsleep::evaluateStorm(s, hubsleep::WakeSource::kTouch, false, 100);
  TEST_ASSERT_TRUE(v.suppressSource);  // 3 consecutive short wakes
}
int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_external_power_suppresses_sleep);
  RUN_TEST(test_touch_suppresses_sleep);
  RUN_TEST(test_idle_triggers_sleep);
  RUN_TEST(test_storm_suppresses_after_threshold);
  return UNITY_END();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `~/.platformio/penv/bin/pio test -e native -f test_hub_sleep_logic`
Expected: FAIL — header missing.

- [ ] **Step 3: Port the pure FSM header + impl (verbatim logic)**

`include/features/hub/sleep/HubSleepLogic.hpp` + `src/features/hub/sleep/HubSleepLogic.cpp`: copy the pure functions from `../Barebone/include/power/HubSleepLogic.hpp` + `../Barebone/src/power/HubSleepLogic.cpp` — `SleepInputs`, `shouldEnterDeepSleep`, `WakeSource`, `StormState`, `StormVerdict`, `evaluateStorm`, `kStormThreshold`, `kShortWakeMs`. **Phase 1 keeps only these pure pieces** plus the `WakeSource` enum; the `WakeService`/`stageForService` staging (ESP-NOW/WiFi/BLE/WebServer) is Phase 2 — omit it. Use `AppConfig::HubSleep::*` for thresholds.

- [ ] **Step 4: Run to verify it passes**

Run: `~/.platformio/penv/bin/pio test -e native -f test_hub_sleep_logic`
Expected: PASS (4 tests).

- [ ] **Step 5: Add the deep-sleep entry/wake glue to HubRuntime**

In `src/features/hub/HubRuntime.cpp` add (gated on `AppConfig::HubFeatures::kEnableSleep`):
- a `static uint32_t g_lastActivityMs` updated whenever a touch point is seen;
- in `tick()`, build `hubsleep::SleepInputs` (externalPowerPresent from `g_axp.state().vbusPresent`, touchActive from touch state, `idleMs = millis()-g_lastActivityMs`, `idleThresholdMs = AppConfig::HubSleep::kDeepSleepAfterMs`); when `shouldEnterDeepSleep` is true, call a local `enterDeepSleep()`:

```cpp
static void enterDeepSleep() {
  Serial.println("[HUB] entering deep sleep");
  g_display.enterSleep();
  if (AppConfig::HubFeatures::kEnableImu) g_imu.enterWakeOnMotionMode(/*cfg from HubMotionWake*/);
  g_axp.disableAdcForSleep();
  uint64_t mask = 0;
  if (AppConfig::HubSleep::kWakeOnTouchInt) mask |= (1ULL << AppConfig::PinoutHub::kHubTouchInt);
  if (AppConfig::HubSleep::kWakeOnImuInt2)  mask |= (1ULL << AppConfig::PinoutHub::kImuInt2);
  esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ALL_LOW);
  esp_deep_sleep_start();
}
```

(Include `<esp_sleep.h>`.) On boot, `begin()` may log `esp_sleep_get_wakeup_cause()` for visibility. Phase-2 staged wake (ESP-NOW/WiFi) is out of scope.

- [ ] **Step 6: Build and verify on device**

Run: `~/.platformio/penv/bin/pio run -e hub`
Expected: SUCCESS. On device (USB unplugged so VBUS absent): after ~15 s idle the device logs `entering deep sleep` and the panel goes dark; a tap or a shake wakes it (fresh boot banner).

- [ ] **Step 7: Commit**

```bash
git add include/features/hub/sleep/ src/features/hub/sleep/ test/test_hub_sleep_logic/ src/features/hub/HubRuntime.cpp
git commit -m "feat(hub): HubSleepLogic FSM + deep-sleep entry/wake"
```

---

### Task 10: HubRuntime orchestration + power-aware toggle contract

**Files:**
- Modify: `src/features/hub/HubRuntime.cpp` (consolidate bring-up order, scheduler, per-IC power-down-on-disable, explicit logs)

**Interfaces:**
- Consumes: every driver from Tasks 2–9 and all `AppConfig::HubFeatures::kEnable*` flags.
- Produces: a deterministic bring-up sequence and cadenced `tick()` scheduler.

- [ ] **Step 1: Enforce the bring-up ORDER in `begin()`**

Reorder `begin()` so initialization runs exactly: **AXP2101 (rails) → Display → Touch → IMU → RTC → IO expander → Audio → Sleep arming**. AXP2101 must complete before any other IC is probed (rails must be up). This matches `../Barebone` `probeNow()` order (AXP first).

- [ ] **Step 2: Implement the power-down-on-disable contract for every IC**

For each peripheral, when its `AppConfig::HubFeatures::kEnable*` is `false`, call the driver's power-down path instead of skipping silently:
- `kEnableDisplay==false` → don't `lv_init`; if panel powered, `g_display.enterSleep()`. Log `display: disabled`.
- `kEnableTouch==false` → `g_touch.begin(); g_touch.enterSleep();` Log `touch: disabled (powered down)`.
- `kEnableImu==false` → leave IMU in default low-power (do not configure high-ODR); Log `imu: disabled`.
- `kEnableRtc==false` → `rtc: disabled` (RTC keeps timekeeping on its own rail; only polling skipped).
- `kEnableIoExpander==false` → `ioexp: disabled`.
- `kEnableAudio==false` → already powers ES8311/ES7210 down (Task 8).
- `kEnablePmicTelemetry==false` → AXP2101 still inits + `configureCharging()`, but skip `refreshStatus()` polling.

- [ ] **Step 3: Implement the cadenced scheduler in `tick()`**

Single non-blocking `tick()` (no long `delay()`), each block gated by its own `static uint32_t last` + `millis()`:
- LVGL pump `g_display.tick()` every loop (the `esp_timer` tick drives `lv_tick_inc`);
- touch `g_touch.refresh()` every `AppConfig::Hub::kTouchPollMs` (20 ms);
- IMU `g_imu.poll()` every `AppConfig::Hub::kMotionPollMs` (125 ms);
- peripherals (AXP telemetry, RTC, IO) every `AppConfig::Hub::kPollMs` (200 ms);
- sleep evaluation every loop.

- [ ] **Step 4: Build the default config**

Run: `~/.platformio/penv/bin/pio run -e hub`
Expected: SUCCESS; on device, boot log shows all seven `[HUB] <ic>: enabled` lines in the correct order.

- [ ] **Step 5: Verify the toggle/power-down contract builds with EACH flag off**

For each flag, set it to `false` in `include/app_config.hpp`, build, then restore. (These are the Global-Constraint toggle builds.)

```bash
for f in kEnableDisplay kEnableTouch kEnableImu kEnableRtc kEnableIoExpander kEnableSleep kEnablePmicTelemetry; do
  echo "=== building with HubFeatures::$f = false ===";
done
```

Procedure per flag: edit the flag to `false`, run `~/.platformio/penv/bin/pio run -e hub` (Expected: SUCCESS, log shows that IC `disabled`), then revert the edit. `kEnableAudio` is already `false` by default (and building it `true` must also succeed).

- [ ] **Step 6: Commit**

```bash
git add src/features/hub/HubRuntime.cpp
git commit -m "feat(hub): ordered bring-up + power-aware toggle scheduler"
```

---

### Task 11: Bring-up / diagnostics screen

**Files:**
- Create: `include/features/hub/ui/BringupScreen.hpp`, `src/features/hub/ui/BringupScreen.cpp`
- Modify: `src/features/hub/HubRuntime.cpp`

**Interfaces:**
- Consumes: driver `State` snapshots (AXP/QMI/PCF/CST/TCA + audio present flags) + free heap/PSRAM.
- Produces: `BringupScreen::create()` (builds the LVGL screen) and `BringupScreen::update(const Snapshot&)`.

- [ ] **Step 1: Define the screen + snapshot interface**

`include/features/hub/ui/BringupScreen.hpp`:

```cpp
#pragma once
#include <stdint.h>
namespace BringupScreen {
struct IcStatus { const char* name; uint8_t state; };  // 0=off/grey,1=ok/green,2=error/red
struct Snapshot {
  IcStatus ics[7];                 // axp,qmi,pcf,cst,tca,es8311,es7210
  int batteryPercent; uint16_t batteryMv; bool charging; bool vbus;
  int16_t touchX, touchY; bool touchPressed;
  float accelX, accelY, accelZ;
  uint8_t rtcHours, rtcMinutes, rtcSeconds;
  uint32_t freeHeap, freePsram;
  const char* firmware;
};
void create();                     // build the LVGL widget tree once
void update(const Snapshot& s);    // refresh labels/colors
}  // namespace BringupScreen
```

- [ ] **Step 2: Implement the LVGL screen**

`src/features/hub/ui/BringupScreen.cpp`: build (in `create()`) a column of 7 IC rows (label + colored dot), a battery line, a touch-test target, an IMU line, an RTC line, and a system line, using only LVGL widgets enabled in `lv_conf.h` (`label`, `canvas`/basic objects). `update()` sets each dot color from `IcStatus.state` (grey/green/red) and refreshes text. No new widgets beyond what `lv_conf.h` enables.

- [ ] **Step 3: Replace the hello-label with the bring-up screen**

In `src/features/hub/HubRuntime.cpp`: after display init, call `BringupScreen::create()` instead of the temporary label. In `tick()` (on a ~250 ms cadence), assemble a `BringupScreen::Snapshot` from the driver states and call `BringupScreen::update(snap)`.

- [ ] **Step 4: Build and verify on device**

Run: `~/.platformio/penv/bin/pio run -e hub`
Expected: SUCCESS. On device: the diagnostics screen shows all present ICs green, live battery %, the touch target tracking taps, live accel values, advancing RTC time, and heap/PSRAM.

- [ ] **Step 5: Commit**

```bash
git add include/features/hub/ui/ src/features/hub/ui/ src/features/hub/HubRuntime.cpp
git commit -m "feat(hub): LVGL bring-up diagnostics screen"
```

---

### Task 12: Docs + final validation

**Files:**
- Create: `docs/PINOUT_HUB.md`
- Modify: `Roadmap.md` (new HUB section + Phase-1 checkboxes)
- Modify: `README.md` (note the second product/env)

**Interfaces:** none (documentation + validation).

- [ ] **Step 1: Write `docs/PINOUT_HUB.md`**

Document the Waveshare ESP32-S3-Touch-AMOLED-1.75 GPIO map from `AppConfig::PinoutHub` (mirror the structure of `docs/PINOUT.md`): I2C bus (SDA15/SCL14), the 7 I2C ICs + addresses, display QSPI pins, touch reset/IRQ, audio I2S pins, IMU INT2, SD pins, boot button.

- [ ] **Step 2: Add a HUB section to `Roadmap.md`**

Add a new top-level section `## HUB (companion device)` with a Phase-1 checklist, marking shipped items `[x]`:

```markdown
## HUB (companion device — Waveshare ESP32-S3-Touch-AMOLED-1.75)
Ported from ../Barebone. See docs/superpowers/specs/2026-06-30-hub-board-bringup-design.md.
- [x] Phase 1 — board bring-up: `[env:hub]`, all IC drivers, power-aware toggles, sleep, diagnostics screen
  - [x] AXP2101 PMIC (power authority + battery telemetry)
  - [x] CO5300 AMOLED + slim LVGL runtime
  - [x] CST9217 touch + transform
  - [x] QMI8658 IMU + wake-on-motion
  - [x] PCF85063 RTC
  - [x] TCA9554 IO expander
  - [x] ES8311/ES7210 probe + power-down
  - [x] HubSleepLogic deep-sleep FSM
- [ ] Phase 2 — ESP-NOW peer link + reactor binding (both firmwares)
- [ ] Phase 3 — redesigned fermentation monitoring/control UI (LVGL)
```

- [ ] **Step 3: Note the second product in `README.md`**

Add a short "HUB companion device" note under the build section: a second env `~/.platformio/penv/bin/pio run -e hub` (Waveshare AMOLED touch controller, partitions `partitions_hub_16mb.csv`), Phase 1 = board bring-up; link `docs/PINOUT_HUB.md` and the design spec.

- [ ] **Step 4: Run the full validation checklist (Claude.md)**

```bash
~/.platformio/penv/bin/pio run -e hub                    # Expected: SUCCESS
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1     # Expected: SUCCESS (reactor unaffected)
~/.platformio/penv/bin/pio test -e native                # Expected: ALL tests PASS (incl. new hub suites)
```

Confirm: each `HubFeatures::kEnable*` flag still builds when `false` (Task 10 Step 5). Confirm the partition table is new ⇒ first flash of the HUB board is a full flash (Partition Change Rule).

- [ ] **Step 5: Commit**

```bash
git add docs/PINOUT_HUB.md Roadmap.md README.md
git commit -m "docs(hub): PINOUT_HUB, Roadmap HUB section, README note; Phase 1 validation"
```

---

## Self-Review

**1. Spec coverage:**
- New `[env:hub]` on the pinned platform → Task 1. ✓
- Import ALL IC drivers (display, touch, AXP2101, IMU, RTC, IO expander, audio) → Tasks 2–8. ✓
- `HubSleepLogic` + deep sleep → Task 9. ✓
- Power-aware toggle contract (power DOWN on disable) → Task 10 (per-IC) + per-driver `powerDown()`/`enterSleep()` in Tasks 4/8/9. ✓
- AXP2101 always-on power authority; only telemetry gated → Task 2 + Task 10 Step 2. ✓
- Ordered bring-up (AXP first) + cooperative scheduler → Task 10. ✓
- Diagnostics bring-up screen → Task 11. ✓
- Graceful degradation (probe present/absent/error, never halt) → driver `begin()` returns bool, HubRuntime logs FAULT and continues; screen shows red. ✓
- Host tests for pure logic (touch transform, sleep FSM, IMU/AXP/BCD decode) → Tasks 2,3,4,5,6,9. ✓
- Reactor unaffected (only `build_src_filter` added) → Task 1 Step 2 + Task 12 Step 4. ✓
- Docs (PINOUT_HUB, Roadmap, README) → Task 12; no API.md change (no HTTP surface). ✓
- New partition ⇒ full reflash → Task 12 Step 4. ✓
- Out-of-scope (ESP-NOW, fermentation UI, audio streaming, NVS persistence) → explicitly deferred in Tasks 8/9 and Self-Review. ✓

**2. Placeholder scan:** Verbatim-port steps cite exact `../Barebone` `file:line-range` + the mechanical transform list (Porting Convention) — concrete, not "implement later". All test steps contain full test code; all interface headers are complete. The only literal "placeholder" word is in Task 8 Step 4's `audio: enabled` comment, which is an intentional Phase-2 marker on a branch that does nothing in Phase 1 (audio default-off).

**3. Type consistency:** Driver names are consistent across tasks (`Axp2101`, `Co5300Panel`, `HubDisplay`, `Cst9217`, `Qmi8658`, `Pcf85063`, `Tca9554`, `Es8311`, `Es7210`); pure-logic namespaces (`axp`, `geom`, `qmi`, `rtc`, `hubsleep`, `HubTouchTransform`) match between their defining task and their tests; `AppConfig::HubFeatures::kEnable*` names are identical in Task 1 (definition), Task 10 (consumption), and the Global Constraints.
