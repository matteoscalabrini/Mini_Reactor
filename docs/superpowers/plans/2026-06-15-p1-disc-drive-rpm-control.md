# Phase 1 — Disc-Drive rpm Control Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the motor's vague "percent of top speed" with true disc **rpm** control (target 8 rpm), and migrate the firmware's control surface to the versioned **`/api/v1`** contract from the design spec.

**Architecture:** Pure rpm↔VACTUAL math is extracted into a header-only, host-testable unit (`RpmKinematics`) and unit-tested with Unity on a PlatformIO `native` env. `Tmc2209Motor` gains `setRpm()`. `Reactor` switches its run/persistence/telemetry from `motorPercent` to `rpm` and owns the disc drive params. `WebInterface` + `AppRuntime` move every route under `/api/v1`, emit the nested telemetry object from spec §4 (P1 fields), add `/disc`, and drop the old `/api/*` routes. The Python mock server is rewritten to mirror v1.

**Tech Stack:** C++17 / Arduino-ESP32 / PlatformIO, TMCStepper, ArduinoJson v7, ESPAsyncWebServer, Unity (native tests), Python/aiohttp (mock).

**Spec:** `docs/superpowers/specs/2026-06-15-reactor-firmware-api-design.md` (this plan = Phase 1 / §10 P1).

---

## Baseline assumptions

- The current working tree (uncommitted firmware refactor: DS18B20 liquid probe + NTC safety) is the starting point. **Before Task 1**, confirm it builds and commit it as a baseline:
  - Run: `pio run -e esp32-s3-devkitc-1` → Expected: `SUCCESS`.
  - `git add -A && git commit -m "chore: baseline working tree before P1"`.
- **Known interim state:** P1 drops the legacy `/api/*` routes and changes the telemetry shape, so the *existing* dark UI in `data/` will not function against the firmware until the later UI phase. This is expected and acceptable per the spec (firmware-first). The **mock server** (Task 6) is the way to exercise the v1 contract in the interim.
- All `pio` commands assume the PlatformIO CLI is on `PATH`. If not, use `~/.platformio/penv/bin/pio`.
- **Persistence note (deviation from spec §7):** the spec calls for a single `ConfigStore`. In P1 we reuse `Reactor`'s existing NVS `Preferences` for the rpm setpoint + disc params, and **defer the `ConfigStore` consolidation to P3/P4**, when PID gains and calibration coefficients actually need a shared home. Building it now would be premature abstraction (YAGNI).

## File structure

| File | Responsibility | Action |
|---|---|---|
| `include/motor/RpmKinematics.hpp` | Pure rpm↔microstep-Hz↔VACTUAL math + rpm clamp. No Arduino/TMC deps. | Create |
| `test/test_rpm_kinematics/test_main.cpp` | Unity host tests for `RpmKinematics`. | Create |
| `platformio.ini` | Add `[env:native]` test env. | Modify |
| `include/motor/Tmc2209Motor.hpp` / `.cpp` | Add `setRpm`, `setDirection`, drive-state getters, `stepsPerRev`. | Modify |
| `include/app_config.hpp` | rpm constants, firmware version, default setpoint 36 °C, CSV header. | Modify |
| `include/control/Reactor.hpp` / `.cpp` | `motorPercent`→`rpm`; own disc params; rpm telemetry/CSV. | Modify |
| `src/system/AppRuntime.cpp` | rpm config wiring; rewrite `buildStatusJson()` to the §4 nested shape. | Modify |
| `include/net/WebInterface.hpp` | `Pending` struct: rpm + disc fields. | Modify |
| `src/net/WebInterface.cpp` | `/api/v1` routes, `/disc`, error envelope, drop `/api/*`. | Modify |
| `tools/mock_server.py` | Rewrite to mirror `/api/v1` + nested telemetry + rpm + `/disc`. | Modify |

> **Embedded testing note:** Only `RpmKinematics` is host-unit-testable (Task 1, true TDD red/green). Hardware-coupled code (TMC/Async/NVS) is verified by **compile** (`pio run`) plus **contract checks against the mock** (Task 6) — there is no host harness for it. This is called out honestly per task rather than faked with unrunnable tests.

---

### Task 1: RpmKinematics (pure math, TDD)

**Files:**
- Create: `include/motor/RpmKinematics.hpp`
- Create: `test/test_rpm_kinematics/test_main.cpp`
- Modify: `platformio.ini`

- [ ] **Step 1: Add the native test env to `platformio.ini`**

Append this block to `platformio.ini` (after the existing `[env:esp32-s3-devkitc-1]` block):

```ini
[env:native]
platform = native
test_framework = unity
build_flags = -std=gnu++17 -Iinclude
```

(`test_build_src` defaults to false, so the Arduino `src/` tree is **not** compiled for native tests — only `test/` + the header under test.)

- [ ] **Step 2: Write the failing test**

Create `test/test_rpm_kinematics/test_main.cpp`:

```cpp
#include <unity.h>
#include "motor/RpmKinematics.hpp"

void setUp() {}
void tearDown() {}

// 8 rpm, 200 full steps/rev, 16 microsteps → (8/60)*200*16 = 426.667 µsteps/s
void test_rpm_to_microstep_hz() {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 426.6667f,
      RpmKinematics::rpmToMicrostepHz(8.0f, 200, 16));
}

// 426.667 µsteps/s → VACTUAL = round(426.667 * 2^24 / 12e6) = 597
void test_rpm_to_vactual_8rpm() {
  TEST_ASSERT_EQUAL_INT32(597, RpmKinematics::rpmToVactual(8.0f, 200, 16));
}

void test_rpm_to_vactual_zero_is_zero() {
  TEST_ASSERT_EQUAL_INT32(0, RpmKinematics::rpmToVactual(0.0f, 200, 16));
}

void test_clamp_rpm() {
  TEST_ASSERT_EQUAL_FLOAT(0.0f, RpmKinematics::clampRpm(0.0f, 0.5f, 30.0f));   // stop allowed
  TEST_ASSERT_EQUAL_FLOAT(0.5f, RpmKinematics::clampRpm(0.2f, 0.5f, 30.0f));   // up to min
  TEST_ASSERT_EQUAL_FLOAT(30.0f, RpmKinematics::clampRpm(50.0f, 0.5f, 30.0f)); // down to max
  TEST_ASSERT_EQUAL_FLOAT(8.0f, RpmKinematics::clampRpm(8.0f, 0.5f, 30.0f));   // in range
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_rpm_to_microstep_hz);
  RUN_TEST(test_rpm_to_vactual_8rpm);
  RUN_TEST(test_rpm_to_vactual_zero_is_zero);
  RUN_TEST(test_clamp_rpm);
  return UNITY_END();
}
```

- [ ] **Step 3: Run the test to verify it fails (header missing)**

Run: `pio test -e native`
Expected: FAIL/ERROR — `motor/RpmKinematics.hpp: No such file or directory`.

- [ ] **Step 4: Create the implementation**

Create `include/motor/RpmKinematics.hpp`:

```cpp
/*
 * RpmKinematics.hpp — pure disc-rpm ↔ TMC2209 VACTUAL conversions.
 *
 * Header-only and dependency-free (no Arduino/TMCStepper) so the math is
 * host-unit-testable. Direct 1:1 drivetrain: disc rpm == motor rpm.
 *   µsteps/s = (rpm / 60) * stepsPerRev * microsteps
 *   VACTUAL  = round(µsteps/s * 2^24 / fCLK)        (fCLK = 12 MHz internal)
 */
#pragma once

#include <cmath>
#include <cstdint>

namespace RpmKinematics {

inline float rpmToMicrostepHz(float rpm, uint16_t stepsPerRev, uint16_t microsteps) {
  return (rpm / 60.0f) * static_cast<float>(stepsPerRev) * static_cast<float>(microsteps);
}

inline int32_t microstepHzToVactual(float microstepHz, float fclkHz = 12000000.0f) {
  return static_cast<int32_t>(std::lround(microstepHz * 16777216.0f / fclkHz));
}

inline int32_t rpmToVactual(float rpm, uint16_t stepsPerRev, uint16_t microsteps,
                            float fclkHz = 12000000.0f) {
  return microstepHzToVactual(rpmToMicrostepHz(rpm, stepsPerRev, microsteps), fclkHz);
}

// 0 (or negative) means "stopped"; any positive rpm is clamped into [minRpm,maxRpm].
inline float clampRpm(float rpm, float minRpm, float maxRpm) {
  if (rpm <= 0.0f) return 0.0f;
  if (rpm < minRpm) return minRpm;
  if (rpm > maxRpm) return maxRpm;
  return rpm;
}

}  // namespace RpmKinematics
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `pio test -e native`
Expected: PASS — `4 Tests 0 Failures 0 Ignored ... OK`.

- [ ] **Step 6: Commit**

```bash
git add platformio.ini include/motor/RpmKinematics.hpp test/test_rpm_kinematics/test_main.cpp
git commit -m "feat(motor): add host-tested RpmKinematics (rpm↔VACTUAL)"
```

---

### Task 2: Tmc2209Motor.setRpm + drive-state tracking

**Files:**
- Modify: `include/motor/Tmc2209Motor.hpp`
- Modify: `src/motor/Tmc2209Motor.cpp`

- [ ] **Step 1: Add `stepsPerRev`, new methods, and getters to the header**

In `include/motor/Tmc2209Motor.hpp`, add `stepsPerRev` to `Config` (after `microsteps`):

```cpp
    uint16_t microsteps = 16;
    float topSpeedMicrostepHz = 3200.0f;  // legacy setSpeedPercent mapping
    uint16_t stepsPerRev = 200;           // full steps/rev (direct 1:1 drivetrain)
```

In the `public:` section, after `setSpeedPercent(...)`, add:

```cpp
  /* setRpm() — Spin the disc at an absolute rpm (0 = stop). Uses the current
   * microsteps + stepsPerRev; honours the direction set by setDirection(). */
  void setRpm(float rpm);

  /* setDirection() — false = cw (forward), true = ccw (reverse). Re-applies the
   * current speed in the new direction if already spinning. */
  void setDirection(bool reverse);

  // Drive-state getters (for telemetry).
  float    rpm() const { return lastRpm_; }
  bool     reversed() const { return reverse_; }
  bool     enabledState() const { return enabled_; }
  uint16_t currentMilliamps() const { return currentMa_; }
  uint16_t microstepsValue() const { return cfg_.microsteps; }
```

In the `private:` section, add members (after `TMC2209Stepper driver_;`):

```cpp
  float lastRpm_ = 0.0f;
  bool reverse_ = false;
  bool enabled_ = false;
  uint16_t currentMa_ = 600;
```

- [ ] **Step 2: Update the implementation**

In `src/motor/Tmc2209Motor.cpp`, add the include near the top (after the existing include):

```cpp
#include "motor/Tmc2209Motor.hpp"

#include "motor/RpmKinematics.hpp"
```

Make `enable()` track state:

```cpp
void Tmc2209Motor::enable(bool on) {
  enabled_ = on;
  digitalWrite(cfg_.pinEnable, on ? LOW : HIGH);  // active-low
}
```

Make `setCurrentMilliamps()` and `setMicrosteps()` mirror state (and re-apply speed when microsteps change so rpm stays correct):

```cpp
void Tmc2209Motor::setCurrentMilliamps(uint16_t milliamps) {
  currentMa_ = milliamps;
  driver_.rms_current(milliamps);
}

void Tmc2209Motor::setMicrosteps(uint16_t microsteps) {
  cfg_.microsteps = microsteps;
  driver_.microsteps(microsteps);
  if (lastRpm_ > 0.0f) setRpm(lastRpm_);  // keep rpm constant across resolution change
}
```

Add the new methods (after `setSpeedPercent`):

```cpp
void Tmc2209Motor::setRpm(float rpm) {
  lastRpm_ = (rpm < 0.0f) ? 0.0f : rpm;
  const int32_t v =
      RpmKinematics::rpmToVactual(lastRpm_, cfg_.stepsPerRev, cfg_.microsteps);
  spin(reverse_ ? -v : v);  // spin() sets the shaft (direction) bit from the sign
}

void Tmc2209Motor::setDirection(bool reverse) {
  reverse_ = reverse;
  if (lastRpm_ > 0.0f) setRpm(lastRpm_);
}
```

- [ ] **Step 3: Compile**

Run: `pio run -e esp32-s3-devkitc-1`
Expected: `SUCCESS` (no callers changed yet; `setSpeedPercent` still present and used by `Reactor`).

- [ ] **Step 4: Commit**

```bash
git add include/motor/Tmc2209Motor.hpp src/motor/Tmc2209Motor.cpp
git commit -m "feat(motor): add setRpm/setDirection + drive-state getters"
```

---

### Task 3: AppConfig — rpm constants, firmware version, defaults, CSV header

**Files:**
- Modify: `include/app_config.hpp`
- Modify: `src/system/AppRuntime.cpp` (wire `stepsPerRev`)

- [ ] **Step 1: Add a firmware-version constant**

In `include/app_config.hpp`, just under `namespace AppConfig {`, add:

```cpp
// API + firmware version reported in telemetry.
static constexpr const char* kFirmwareVersion = "1.0.0";
```

- [ ] **Step 2: Add `kStepsPerRev` to the Motor namespace**

In the `namespace Motor` block, after `kMicrosteps`:

```cpp
static constexpr uint16_t kStepsPerRev = 200;  // full steps/rev, direct 1:1 drivetrain
```

- [ ] **Step 3: Default the bath setpoint to the operating point**

Change the `Thermal` default setpoint:

```cpp
static constexpr float kDefaultSetpointC = 36.0f;  // bacterial-cellulose operating point
```

- [ ] **Step 4: Replace the Process speed default with rpm + range**

Replace the body of `namespace Process` with:

```cpp
namespace Process {
static constexpr float    kDefaultRpm      = 8.0f;   // disc operating speed
static constexpr float    kMinRpm          = 0.5f;   // allowed run range
static constexpr float    kMaxRpm          = 30.0f;
static constexpr uint16_t kDefaultDurationMin = 0;   // 0 = run until stopped
static constexpr const char* kPrefsNamespace  = "reactor";
}  // namespace Process
```

- [ ] **Step 5: Bump the CSV header to rpm + load + safety**

Change `Sd::kLogHeader`:

```cpp
static constexpr const char* kLogHeader  =
    "t_ms,running,liquid_c,heater_c,setpoint_c,heater_pct,rpm,load,fault,safety";
```

- [ ] **Step 6: Wire `stepsPerRev` into the motor config builder**

In `src/system/AppRuntime.cpp`, in `makeMotorConfig()`, after `c.microsteps = ...;`:

```cpp
  c.stepsPerRev = AppConfig::Motor::kStepsPerRev;
```

- [ ] **Step 7: Compile**

Run: `pio run -e esp32-s3-devkitc-1`
Expected: **FAIL** — `makeReactorConfig` still references `AppConfig::Process::kDefaultMotorPercent` and `Reactor::Config::defaultMotorPercent`. This is expected; Task 4 fixes the `Reactor` side. (If you prefer a green commit here, do Task 4 before compiling/committing — Tasks 3 and 4 form one compile unit.)

- [ ] **Step 8: Commit (with Task 4) — see Task 4 Step 8.**

---

### Task 4: Reactor — convert motorPercent → rpm and own disc params

**Files:**
- Modify: `include/control/Reactor.hpp`
- Modify: `src/control/Reactor.cpp`
- Modify: `src/system/AppRuntime.cpp` (config builder + telemetry field rename)
- Modify: `src/net/WebInterface.cpp` (call sites only)

- [ ] **Step 1: Update `ReactorTelemetry` and `Reactor` in the header**

In `include/control/Reactor.hpp`, in `struct ReactorTelemetry`, replace:

```cpp
  float motorPercent = 0.0f;
```

with:

```cpp
  float rpm = 0.0f;          // disc rpm (0 when stopped)
```

Replace `struct Config` with:

```cpp
  struct Config {
    float defaultRpm = 8.0f;
    float minRpm = 0.5f;
    float maxRpm = 30.0f;
    float defaultSetpointC = 36.0f;
    uint16_t defaultDurationMin = 0;
    uint16_t defaultDiscCurrentMa = 600;
    uint16_t defaultDiscMicrosteps = 16;
    bool defaultDiscReverse = false;
    const char* prefsNamespace = "reactor";
  };
```

Replace the `start`, `setMotorPercent` declarations and add disc setters + a getter:

```cpp
  /* start() — Begin a run with target temp, disc rpm, and optional duration. */
  void start(float targetC, float rpm, uint16_t durationMin);

  void stop();
  void update();
  bool running() const { return running_; }

  /* Live setpoint changes (apply immediately if running; always persisted). */
  void setTargetC(float celsius);
  void setRpm(float rpm);
  float rpmSetpoint() const { return rpm_; }

  /* Disc drive parameters (apply to the motor; current/microsteps/direction persist). */
  void setDiscCurrentMa(uint16_t milliamps);
  void setDiscMicrosteps(uint16_t microsteps);
  void setDiscReverse(bool reverse);
  void setDiscEnabled(bool on);
```

In `private:` members, replace `float motorPercent_ = 40.0f;` with:

```cpp
  float rpm_ = 8.0f;
  uint16_t discCurrentMa_ = 600;
  uint16_t discMicrosteps_ = 16;
  bool discReverse_ = false;
```

- [ ] **Step 2: Rewrite `Reactor.cpp`**

Replace the full contents of `src/control/Reactor.cpp` with:

```cpp
/*
 * Reactor.cpp — fermentation run orchestration (rpm-based disc drive).
 * See include/control/Reactor.hpp and AppConfig::Process.
 */

#include "control/Reactor.hpp"

#include <math.h>

#include "motor/RpmKinematics.hpp"

Reactor::Reactor(ThermalController& thermal, Tmc2209Motor& motor,
                 const Config& config)
    : thermal_(thermal), motor_(motor), cfg_(config) {
  targetC_ = config.defaultSetpointC;
  rpm_ = config.defaultRpm;
  durationMin_ = config.defaultDurationMin;
  discCurrentMa_ = config.defaultDiscCurrentMa;
  discMicrosteps_ = config.defaultDiscMicrosteps;
  discReverse_ = config.defaultDiscReverse;
}

void Reactor::begin() {
  prefs_.begin(cfg_.prefsNamespace, false);
  targetC_ = prefs_.getFloat("targetC", cfg_.defaultSetpointC);
  rpm_ = prefs_.getFloat("rpm", cfg_.defaultRpm);
  durationMin_ = prefs_.getUShort("durMin", cfg_.defaultDurationMin);
  discCurrentMa_ = prefs_.getUShort("discMa", cfg_.defaultDiscCurrentMa);
  discMicrosteps_ = prefs_.getUShort("discUs", cfg_.defaultDiscMicrosteps);
  discReverse_ = prefs_.getBool("discRev", cfg_.defaultDiscReverse);
  thermal_.setSetpoint(targetC_);
  // Apply persisted drive params to the (already-begun) motor.
  motor_.setCurrentMilliamps(discCurrentMa_);
  motor_.setMicrosteps(discMicrosteps_);
  motor_.setDirection(discReverse_);
}

void Reactor::persist() {
  prefs_.putFloat("targetC", targetC_);
  prefs_.putFloat("rpm", rpm_);
  prefs_.putUShort("durMin", durationMin_);
  prefs_.putUShort("discMa", discCurrentMa_);
  prefs_.putUShort("discUs", discMicrosteps_);
  prefs_.putBool("discRev", discReverse_);
}

void Reactor::start(float targetC, float rpm, uint16_t durationMin) {
  targetC_ = targetC;
  rpm_ = RpmKinematics::clampRpm(rpm, cfg_.minRpm, cfg_.maxRpm);
  durationMin_ = durationMin;
  persist();

  running_ = true;
  startMs_ = millis();

  thermal_.setSetpoint(targetC_);
  thermal_.enable(true);
  motor_.enable(true);
  motor_.setCurrentMilliamps(discCurrentMa_);
  motor_.setMicrosteps(discMicrosteps_);
  motor_.setDirection(discReverse_);
  motor_.setRpm(rpm_);
}

void Reactor::stop() {
  running_ = false;
  thermal_.enable(false);
  motor_.stop();
}

void Reactor::update() {
  if (!running_) return;
  if (durationMin_ > 0) {
    const uint32_t elapsedMs = millis() - startMs_;
    if (elapsedMs >= static_cast<uint32_t>(durationMin_) * 60000UL) {
      stop();
    }
  }
}

void Reactor::setTargetC(float celsius) {
  targetC_ = celsius;
  thermal_.setSetpoint(celsius);
  persist();
}

void Reactor::setRpm(float rpm) {
  rpm_ = RpmKinematics::clampRpm(rpm, cfg_.minRpm, cfg_.maxRpm);
  if (running_) motor_.setRpm(rpm_);
  persist();
}

void Reactor::setDiscCurrentMa(uint16_t milliamps) {
  if (milliamps < 100) milliamps = 100;
  if (milliamps > 1500) milliamps = 1500;
  discCurrentMa_ = milliamps;
  motor_.setCurrentMilliamps(discCurrentMa_);
  persist();
}

void Reactor::setDiscMicrosteps(uint16_t microsteps) {
  discMicrosteps_ = microsteps;
  motor_.setMicrosteps(microsteps);
  persist();
}

void Reactor::setDiscReverse(bool reverse) {
  discReverse_ = reverse;
  motor_.setDirection(reverse);
  persist();
}

void Reactor::setDiscEnabled(bool on) {
  motor_.enable(on);
  if (!on) motor_.stop();
  else if (running_) motor_.setRpm(rpm_);
}

ReactorTelemetry Reactor::telemetry() const {
  ReactorTelemetry t;
  t.running = running_;
  t.liquidTempC = thermal_.temperatureC();
  t.heaterTempC = thermal_.heaterTempC();
  t.setpointC = thermal_.setpoint();
  t.heaterDutyPct = thermal_.dutyPercent();
  t.rpm = running_ ? rpm_ : 0.0f;
  t.sensorFault = thermal_.sensorFault();
  t.safetyTripped = thermal_.safetyTripped();
  t.durationMin = durationMin_;
  if (running_) {
    t.elapsedSec = (millis() - startMs_) / 1000UL;
    if (durationMin_ > 0) {
      const uint32_t totalSec = static_cast<uint32_t>(durationMin_) * 60UL;
      t.remainingSec = (t.elapsedSec < totalSec) ? (totalSec - t.elapsedSec) : 0;
    }
  }
  return t;
}

String Reactor::csvRow() const {
  const ReactorTelemetry t = telemetry();
  // t_ms,running,liquid_c,heater_c,setpoint_c,heater_pct,rpm,load,fault,safety
  char buf[160];
  snprintf(buf, sizeof(buf), "%lu,%d,%.2f,%.2f,%.2f,%.1f,%.2f,%d,%d,%d",
           (unsigned long)millis(), t.running ? 1 : 0,
           isnan(t.liquidTempC) ? 0.0f : t.liquidTempC,
           isnan(t.heaterTempC) ? 0.0f : t.heaterTempC, t.setpointC,
           t.heaterDutyPct, t.rpm, /*load*/ 0, t.sensorFault ? 1 : 0,
           t.safetyTripped ? 1 : 0);
  return String(buf);
}
```

- [ ] **Step 3: Update `makeReactorConfig()` in `AppRuntime.cpp`**

Replace the `makeReactorConfig()` body with:

```cpp
Reactor::Config makeReactorConfig() {
  Reactor::Config c;
  c.defaultRpm = AppConfig::Process::kDefaultRpm;
  c.minRpm = AppConfig::Process::kMinRpm;
  c.maxRpm = AppConfig::Process::kMaxRpm;
  c.defaultSetpointC = AppConfig::Thermal::kDefaultSetpointC;
  c.defaultDurationMin = AppConfig::Process::kDefaultDurationMin;
  c.defaultDiscCurrentMa = AppConfig::Motor::kCurrentMilliamps;
  c.defaultDiscMicrosteps = AppConfig::Motor::kMicrosteps;
  c.defaultDiscReverse = false;
  c.prefsNamespace = AppConfig::Process::kPrefsNamespace;
  return c;
}
```

- [ ] **Step 4: Fix the telemetry field rename in the (soon-replaced) `buildStatusJson`**

In `src/system/AppRuntime.cpp`, find `r["motorPct"] = t.motorPercent;` and change it to:

```cpp
  r["rpm"] = t.rpm;
```

(Task 5 rewrites this function entirely; this keeps it compiling in between.)

- [ ] **Step 5: Fix the `Reactor` call sites in `WebInterface.cpp`**

In `src/net/WebInterface.cpp`, in `applyPending()`, change the two calls:

```cpp
  if (p.runStart) reactor_.start(p.runTargetC, p.runMotorPct, p.runDurMin);
```
stays as-is for now (the `runMotorPct` field is renamed in Task 5), **but** change:

```cpp
  if (p.setMotor) reactor_.setMotorPercent(p.setMotorPct);
```
to:

```cpp
  if (p.setMotor) reactor_.setRpm(p.setMotorPct);
```

- [ ] **Step 6: Compile**

Run: `pio run -e esp32-s3-devkitc-1`
Expected: `SUCCESS`.

- [ ] **Step 7: Run native tests (ensure nothing broke the header)**

Run: `pio test -e native`
Expected: PASS (4 tests).

- [ ] **Step 8: Commit (Tasks 3 + 4 together)**

```bash
git add include/app_config.hpp include/control/Reactor.hpp src/control/Reactor.cpp \
        src/system/AppRuntime.cpp src/net/WebInterface.cpp
git commit -m "feat(reactor): drive by rpm (8 rpm default) instead of motor percent"
```

---

### Task 5: `/api/v1` migration + nested telemetry + `/disc`

**Files:**
- Modify: `include/net/WebInterface.hpp`
- Modify: `src/net/WebInterface.cpp`
- Modify: `src/system/AppRuntime.cpp`

- [ ] **Step 1: Update the `Pending` struct**

In `include/net/WebInterface.hpp`, replace the `struct Pending { ... } pending_;` block with:

```cpp
  struct Pending {
    bool runStart = false;
    float runTargetC = 0;
    float runRpm = 0;
    uint16_t runDurMin = 0;
    bool runStop = false;

    bool setTarget = false;
    float setTargetC = 0;
    bool setRpm = false;
    float setRpmVal = 0;

    bool discRpm = false;     float discRpmVal = 0;
    bool discCurrent = false; uint16_t discCurrentMa = 0;
    bool discMicro = false;   uint16_t discMicrosteps = 0;
    bool discDir = false;     bool discReverse = false;
    bool discEnable = false;  bool discEnableVal = false;

    bool wifiConnect = false;
    String wifiSsid;
    String wifiPass;
    bool wifiForget = false;
    bool wifiScan = false;
    bool logClear = false;
  } pending_;
```

- [ ] **Step 2: Rewrite `registerRoutes()` + `applyPending()` for `/api/v1`**

In `src/net/WebInterface.cpp`, replace the `registerRoutes()` and `applyPending()` functions with the following. Add a shared error/ok helper above `registerRoutes()`:

```cpp
static void sendOk(AsyncWebServerRequest* req) { sendJson(req, "{\"ok\":true}"); }

static void sendError(AsyncWebServerRequest* req, int status, const char* code,
                      const char* msg) {
  String body = String("{\"ok\":false,\"error\":{\"code\":\"") + code +
                "\",\"message\":\"" + msg + "\"}}";
  AsyncWebServerResponse* resp = req->beginResponse(status, "application/json", body);
  resp->addHeader("Access-Control-Allow-Origin", "*");
  req->send(resp);
}

void WebInterface::registerRoutes() {
  // ── WebSocket ──
  ws_->onEvent([this](AsyncWebSocket*, AsyncWebSocketClient* client,
                      AwsEventType type, void*, uint8_t*, size_t) {
    if (type == WS_EVT_CONNECT) {
      xSemaphoreTake(mutex_, portMAX_DELAY);
      const String snapshot = statusJson_;
      xSemaphoreGive(mutex_);
      client->text(snapshot);
    }
  });
  server_->addHandler(ws_);

  // ── GET status ──
  server_->on("/api/v1/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const String body = statusJson_;
    xSemaphoreGive(mutex_);
    sendJson(req, body);
  });

  // ── GET wifi scan ──
  server_->on("/api/v1/wifi/scan", HTTP_GET, [this](AsyncWebServerRequest* req) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    pending_.wifiScan = true;
    const String body = scanJson_;
    xSemaphoreGive(mutex_);
    sendJson(req, body);
  });

  // ── POST run ──
  auto* runHandler = new AsyncCallbackJsonWebHandler(
      "/api/v1/run", [this](AsyncWebServerRequest* req, JsonVariant& json) {
        JsonObject o = json.as<JsonObject>();
        const String action = o["action"] | "";
        if (action == "start") {
          const float rpm = o["rpm"] | 8.0f;
          const float targetC = o["targetC"] | 36.0f;
          if (rpm < 0.0f || rpm > 30.0f) {  // kMinRpm..kMaxRpm
            sendError(req, 400, "out_of_range", "rpm must be 0..30");
            return;
          }
          if (targetC < 0.0f || targetC > 55.0f) {  // processMaxC ceiling
            sendError(req, 400, "out_of_range", "targetC must be 0..55");
            return;
          }
          xSemaphoreTake(mutex_, portMAX_DELAY);
          pending_.runStart = true;
          pending_.runTargetC = targetC;
          pending_.runRpm = rpm;
          pending_.runDurMin = o["durationMin"] | 0;
          xSemaphoreGive(mutex_);
          sendOk(req);
        } else if (action == "stop") {
          xSemaphoreTake(mutex_, portMAX_DELAY);
          pending_.runStop = true;
          xSemaphoreGive(mutex_);
          sendOk(req);
        } else {
          sendError(req, 400, "invalid_request", "action must be start|stop");
        }
      });
  server_->addHandler(runHandler);

  // ── POST setpoint (live targetC / rpm) ──
  auto* setHandler = new AsyncCallbackJsonWebHandler(
      "/api/v1/setpoint", [this](AsyncWebServerRequest* req, JsonVariant& json) {
        JsonObject o = json.as<JsonObject>();
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (!o["targetC"].isNull()) {
          pending_.setTarget = true;
          pending_.setTargetC = o["targetC"].as<float>();
        }
        if (!o["rpm"].isNull()) {
          pending_.setRpm = true;
          pending_.setRpmVal = o["rpm"].as<float>();
        }
        xSemaphoreGive(mutex_);
        sendOk(req);
      });
  server_->addHandler(setHandler);

  // ── POST disc (drive params) ──
  auto* discHandler = new AsyncCallbackJsonWebHandler(
      "/api/v1/disc", [this](AsyncWebServerRequest* req, JsonVariant& json) {
        JsonObject o = json.as<JsonObject>();
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (!o["rpm"].isNull())       { pending_.discRpm = true; pending_.discRpmVal = o["rpm"].as<float>(); }
        if (!o["currentMa"].isNull()) { pending_.discCurrent = true; pending_.discCurrentMa = o["currentMa"].as<uint16_t>(); }
        if (!o["microsteps"].isNull()){ pending_.discMicro = true; pending_.discMicrosteps = o["microsteps"].as<uint16_t>(); }
        if (!o["direction"].isNull()) { pending_.discDir = true; pending_.discReverse = (o["direction"].as<String>() == "ccw"); }
        if (!o["enabled"].isNull())   { pending_.discEnable = true; pending_.discEnableVal = o["enabled"].as<bool>(); }
        xSemaphoreGive(mutex_);
        sendOk(req);
      });
  server_->addHandler(discHandler);

  // ── POST wifi connect ──
  auto* wifiHandler = new AsyncCallbackJsonWebHandler(
      "/api/v1/wifi/connect", [this](AsyncWebServerRequest* req, JsonVariant& json) {
        JsonObject o = json.as<JsonObject>();
        const String ssid = o["ssid"] | "";
        if (ssid.isEmpty()) {
          sendError(req, 400, "wifi_ssid_required", "ssid is required");
          return;
        }
        xSemaphoreTake(mutex_, portMAX_DELAY);
        pending_.wifiConnect = true;
        pending_.wifiSsid = ssid;
        pending_.wifiPass = o["password"] | "";
        xSemaphoreGive(mutex_);
        sendOk(req);
      });
  server_->addHandler(wifiHandler);

  server_->on("/api/v1/wifi/forget", HTTP_POST, [this](AsyncWebServerRequest* req) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    pending_.wifiForget = true;
    xSemaphoreGive(mutex_);
    sendOk(req);
  });

  // ── SD log download / clear ──
  server_->on("/api/v1/log", HTTP_GET, [this](AsyncWebServerRequest* req) {
    if (!sd_.mounted() || !SD.exists(sd_.logPath())) {
      sendError(req, 503, "no_log", "no log file on the SD card");
      return;
    }
    AsyncWebServerResponse* resp =
        req->beginResponse(SD, sd_.logPath(), "text/csv", true);
    req->send(resp);
  });

  server_->on("/api/v1/log/clear", HTTP_POST, [this](AsyncWebServerRequest* req) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    pending_.logClear = true;
    xSemaphoreGive(mutex_);
    sendOk(req);
  });

  // ── Static UI + SPA fallback ──
  server_->serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

  server_->onNotFound([](AsyncWebServerRequest* req) {
    if (req->url().startsWith("/api/")) {
      req->send(404, "application/json", "{\"ok\":false,\"error\":{\"code\":\"not_found\",\"message\":\"unknown endpoint\"}}");
      return;
    }
    if (SPIFFS.exists("/index.html")) {
      req->send(SPIFFS, "/index.html", "text/html");
    } else {
      req->send(200, "text/html",
                "<h1>Mini Reactor</h1><p>UI assets missing — run "
                "<code>pio run -t uploadfs</code>.</p>");
    }
  });
}

void WebInterface::applyPending() {
  Pending p;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  p = pending_;
  pending_ = Pending{};
  xSemaphoreGive(mutex_);

  if (p.runStart) reactor_.start(p.runTargetC, p.runRpm, p.runDurMin);
  if (p.runStop) reactor_.stop();
  if (p.setTarget) reactor_.setTargetC(p.setTargetC);
  if (p.setRpm) reactor_.setRpm(p.setRpmVal);
  if (p.discRpm) reactor_.setRpm(p.discRpmVal);
  if (p.discCurrent) reactor_.setDiscCurrentMa(p.discCurrentMa);
  if (p.discMicro) reactor_.setDiscMicrosteps(p.discMicrosteps);
  if (p.discDir) reactor_.setDiscReverse(p.discReverse);
  if (p.discEnable) reactor_.setDiscEnabled(p.discEnableVal);
  if (p.wifiConnect) wifi_.connect(p.wifiSsid, p.wifiPass);
  if (p.wifiForget) wifi_.forget();
  if (p.wifiScan) wifi_.requestScan();
  if (p.logClear) sd_.clearLog();
}
```

Also update the route-doc comment block at the top of `WebInterface.cpp` to read `/api/v1/...` (cosmetic; replace `/api/` with `/api/v1/` in the header comment).

- [ ] **Step 3: Rewrite `buildStatusJson()` to the §4 nested shape**

In `src/system/AppRuntime.cpp`, add `#include <WiFi.h>` to the include list, then replace the entire `buildStatusJson()` function with:

```cpp
// Build the nested /api/v1 telemetry object (spec §4). P1 fields only;
// pid terms, StallGuard load, driver flags, and the NTC raw/calibration block
// are added by later phases.
String buildStatusJson() {
  JsonDocument doc;
  const ReactorTelemetry t = g_reactor.telemetry();

  doc["apiVersion"] = "1.0";
  doc["uptimeSec"] = millis() / 1000UL;

  JsonObject sys = doc["system"].to<JsonObject>();
  sys["firmware"] = AppConfig::kFirmwareVersion;
  sys["freeHeap"] = ESP.getFreeHeap();
  Husb238::Status ps;
  sys["vbus"] = g_pd.refreshStatus(ps) ? voltageCodeStr(ps.voltage) : "?";
  sys["sdMounted"] = g_sd.mounted();

  JsonObject th = doc["thermal"].to<JsonObject>();
  if (t.sensorFault) {
    th["tempC"] = nullptr;
    th["errorC"] = nullptr;
  } else {
    th["tempC"] = roundf(t.liquidTempC * 100) / 100.0f;
    th["errorC"] = roundf((t.setpointC - t.liquidTempC) * 100) / 100.0f;
  }
  th["setpointC"] = t.setpointC;
  th["heaterPct"] = roundf(t.heaterDutyPct * 10) / 10.0f;
  th["fault"] = t.sensorFault;
  JsonObject safety = th["safety"].to<JsonObject>();
  safety["tripped"] = t.safetyTripped;
  if (isnan(t.heaterTempC)) safety["heaterTempC"] = nullptr;
  else safety["heaterTempC"] = roundf(t.heaterTempC * 10) / 10.0f;
  safety["heaterMaxC"] = AppConfig::Thermal::kHeaterSafetyMaxC;
  safety["processMaxC"] = AppConfig::Thermal::kProcessMaxC;

  JsonObject disc = doc["disc"].to<JsonObject>();
  disc["running"] = t.running;
  disc["rpm"] = t.rpm;
  disc["rpmSetpoint"] = g_reactor.rpmSetpoint();
  disc["direction"] = g_motor.reversed() ? "ccw" : "cw";
  disc["currentMa"] = g_motor.currentMilliamps();
  disc["microsteps"] = g_motor.microstepsValue();
  disc["enabled"] = g_motor.enabledState();
  JsonObject drv = disc["driver"].to<JsonObject>();
  char ver[8];
  snprintf(ver, sizeof(ver), "0x%02X", g_motor.version());
  drv["version"] = ver;
  drv["connected"] = g_motor.connected();

  JsonObject run = doc["run"].to<JsonObject>();
  run["active"] = t.running;
  run["elapsedSec"] = t.elapsedSec;
  if (t.running && t.durationMin > 0) run["remainingSec"] = t.remainingSec;
  else run["remainingSec"] = nullptr;
  run["durationMin"] = t.durationMin;

  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["mode"] = g_wifi.apActive() ? "ap" : "sta";
  wifi["connected"] = g_wifi.staConnected();
  wifi["ssid"] = WiFi.SSID();
  wifi["ip"] = g_wifi.ipAddress();
  if (g_wifi.staConnected()) wifi["rssi"] = WiFi.RSSI();
  else wifi["rssi"] = nullptr;

  JsonObject storage = doc["storage"].to<JsonObject>();
  storage["sdMounted"] = g_sd.mounted();
  storage["logBytes"] = nullptr;  // accurate size arrives with the SD-mgmt phase
  storage["logging"] = g_sd.mounted();

  JsonArray alarms = doc["alarms"].to<JsonArray>();
  if (t.sensorFault) {
    JsonObject a = alarms.add<JsonObject>();
    a["code"] = "sensor_fault"; a["severity"] = "warn";
  }
  if (isnan(t.heaterTempC)) {
    JsonObject a = alarms.add<JsonObject>();
    a["code"] = "heater_probe_fault"; a["severity"] = "warn";
  }
  if (t.safetyTripped) {
    JsonObject a = alarms.add<JsonObject>();
    a["code"] = "safety_tripped"; a["severity"] = "critical";
  }

  String out;
  serializeJson(doc, out);
  return out;
}
```

> Uses `Tmc2209Motor` getters from Task 2 (`reversed`, `currentMilliamps`, `microstepsValue`, `enabledState`, `version`, `connected`) and `Reactor::rpmSetpoint()` from Task 4. `g_motor.connected()` does a UART round-trip — acceptable at the ~10 Hz status cadence.

- [ ] **Step 4: Compile**

Run: `pio run -e esp32-s3-devkitc-1`
Expected: `SUCCESS`.

- [ ] **Step 5: Commit**

```bash
git add include/net/WebInterface.hpp src/net/WebInterface.cpp src/system/AppRuntime.cpp
git commit -m "feat(api): migrate control surface to /api/v1 + nested telemetry + /disc"
```

---

### Task 6: Rewrite the mock server to mirror `/api/v1`

**Files:**
- Modify: `tools/mock_server.py`

- [ ] **Step 1: Replace `tools/mock_server.py`**

Replace the full contents with:

```python
#!/usr/bin/env python3
"""Mock backend for the Mini-Reactor /api/v1 contract — develop the future UI in a
browser with no hardware. Serves data/ and fakes the firmware's v1 REST + WebSocket
API: nested telemetry, rpm-based disc drive, NTC safety probe, first-order liquid
thermal model.

Run:  python tools/mock_server.py   (needs aiohttp)
Open: http://localhost:8000
"""
import asyncio, json, time, random, pathlib
from aiohttp import web

DATA = pathlib.Path(__file__).resolve().parent.parent / "data"
AMBIENT = 22.0

state = {
    "running": False, "targetC": 36.0, "rpm": 8.0, "rpmSetpoint": 8.0,
    "durationMin": 0, "tempC": AMBIENT, "heaterPct": 0.0, "startMs": 0.0,
    "fault": False, "heaterTempC": 24.0, "safetyTripped": False,
    "currentMa": 600, "microsteps": 16, "reverse": False, "enabled": True,
    "ssid": "LAB-NET-5G", "connected": True, "ap": False, "ip": "192.168.1.42",
}
clients = set()


def status():
    now = time.monotonic()
    elapsed = int(now - state["startMs"]) if state["running"] else 0
    remaining = None
    if state["running"] and state["durationMin"] > 0:
        remaining = max(0, state["durationMin"] * 60 - elapsed)
    temp = None if state["fault"] else round(state["tempC"], 2)
    err = None if temp is None else round(state["targetC"] - state["tempC"], 2)
    rpm = state["rpm"] if state["running"] else 0.0
    alarms = []
    if state["fault"]:
        alarms.append({"code": "sensor_fault", "severity": "warn"})
    if state["safetyTripped"]:
        alarms.append({"code": "safety_tripped", "severity": "critical"})
    return {
        "apiVersion": "1.0", "uptimeSec": int(now),
        "system": {"firmware": "1.0.0-mock", "freeHeap": 142000,
                   "vbus": "12V", "sdMounted": True},
        "thermal": {
            "tempC": temp, "setpointC": state["targetC"], "errorC": err,
            "heaterPct": round(state["heaterPct"], 1), "fault": state["fault"],
            "safety": {"tripped": state["safetyTripped"],
                       "heaterTempC": round(state["heaterTempC"], 1),
                       "heaterMaxC": 80.0, "processMaxC": 55.0},
        },
        "disc": {
            "running": state["running"], "rpm": rpm,
            "rpmSetpoint": state["rpmSetpoint"],
            "direction": "ccw" if state["reverse"] else "cw",
            "currentMa": state["currentMa"], "microsteps": state["microsteps"],
            "enabled": state["enabled"],
            "driver": {"version": "0x21", "connected": True},
        },
        "run": {"active": state["running"], "elapsedSec": elapsed,
                "remainingSec": remaining, "durationMin": state["durationMin"]},
        "wifi": {"mode": "ap" if state["ap"] else "sta",
                 "connected": state["connected"], "ssid": state["ssid"],
                 "ip": state["ip"], "rssi": -54 if state["connected"] else None},
        "storage": {"sdMounted": True, "logBytes": None, "logging": True},
        "alarms": alarms,
    }


async def simulate():
    dt = 0.25
    while True:
        s = state
        if s["running"]:
            err = s["targetC"] - s["tempC"]
            s["heaterPct"] = max(0.0, min(100.0, 12.0 * err))
            duty = s["heaterPct"] / 100.0
            s["tempC"] += (0.6 * duty - 0.02 * (s["tempC"] - AMBIENT)) * dt
            s["heaterTempC"] = s["tempC"] + 18.0 * duty  # heater runs hotter than bath
            if s["durationMin"] > 0 and \
               time.monotonic() - s["startMs"] >= s["durationMin"] * 60:
                s["running"] = False
        else:
            s["heaterPct"] = 0.0
            s["tempC"] += (-0.02 * (s["tempC"] - AMBIENT)) * dt
            s["heaterTempC"] += (-0.05 * (s["heaterTempC"] - AMBIENT)) * dt
        s["tempC"] += random.uniform(-0.04, 0.04)
        payload = json.dumps(status())
        for ws in list(clients):
            try:
                await ws.send_str(payload)
            except Exception:
                clients.discard(ws)
        await asyncio.sleep(dt)


async def ws_handler(req):
    ws = web.WebSocketResponse()
    await ws.prepare(req)
    clients.add(ws)
    await ws.send_str(json.dumps(status()))
    try:
        async for _ in ws:
            pass
    finally:
        clients.discard(ws)
    return ws


async def api_status(req):
    return web.json_response(status())


async def api_run(req):
    b = await req.json()
    if b.get("action") == "start":
        rpm = float(b.get("rpm", 8))
        targetC = float(b.get("targetC", 36))
        if rpm < 0 or rpm > 30:
            return web.json_response(
                {"ok": False, "error": {"code": "out_of_range",
                                        "message": "rpm must be 0..30"}}, status=400)
        if targetC < 0 or targetC > 55:
            return web.json_response(
                {"ok": False, "error": {"code": "out_of_range",
                                        "message": "targetC must be 0..55"}}, status=400)
        state.update(running=True, targetC=targetC,
                     rpm=rpm, rpmSetpoint=rpm,
                     durationMin=int(b.get("durationMin", 0)),
                     startMs=time.monotonic())
        return web.json_response({"ok": True})
    if b.get("action") == "stop":
        state["running"] = False
        return web.json_response({"ok": True})
    return web.json_response(
        {"ok": False, "error": {"code": "invalid_request",
                                "message": "action must be start|stop"}}, status=400)


async def api_setpoint(req):
    b = await req.json()
    if "targetC" in b:
        state["targetC"] = float(b["targetC"])
    if "rpm" in b:
        state["rpm"] = state["rpmSetpoint"] = float(b["rpm"])
    return web.json_response({"ok": True})


async def api_disc(req):
    b = await req.json()
    if "rpm" in b:
        state["rpm"] = state["rpmSetpoint"] = float(b["rpm"])
    if "currentMa" in b:
        state["currentMa"] = int(b["currentMa"])
    if "microsteps" in b:
        state["microsteps"] = int(b["microsteps"])
    if "direction" in b:
        state["reverse"] = (b["direction"] == "ccw")
    if "enabled" in b:
        state["enabled"] = bool(b["enabled"])
    return web.json_response({"ok": True})


async def api_scan(req):
    nets = [{"ssid": n, "rssi": r, "secure": sec} for n, r, sec in [
        ("LAB-NET-5G", -42, True), ("fermentation_floor", -58, True),
        ("ESP-GUEST", -67, False), ("BUILDING-IOT", -74, True)]]
    return web.json_response({"scanning": False, "networks": nets})


async def api_connect(req):
    b = await req.json()
    if not b.get("ssid"):
        return web.json_response(
            {"ok": False, "error": {"code": "wifi_ssid_required",
                                    "message": "ssid is required"}}, status=400)
    state.update(ssid=b["ssid"], connected=True, ap=False)
    return web.json_response({"ok": True})


async def api_forget(req):
    state.update(connected=False, ap=True, ip="192.168.4.1")
    return web.json_response({"ok": True})


async def api_log(req):
    header = "t_ms,running,liquid_c,heater_c,setpoint_c,heater_pct,rpm,load,fault,safety"
    return web.Response(text=header + "\n", content_type="text/csv")


async def api_log_clear(req):
    return web.json_response({"ok": True})


def main():
    app = web.Application()
    app.router.add_get("/ws", ws_handler)
    app.router.add_get("/api/v1/status", api_status)
    app.router.add_post("/api/v1/run", api_run)
    app.router.add_post("/api/v1/setpoint", api_setpoint)
    app.router.add_post("/api/v1/disc", api_disc)
    app.router.add_get("/api/v1/wifi/scan", api_scan)
    app.router.add_post("/api/v1/wifi/connect", api_connect)
    app.router.add_post("/api/v1/wifi/forget", api_forget)
    app.router.add_get("/api/v1/log", api_log)
    app.router.add_post("/api/v1/log/clear", api_log_clear)
    app.router.add_get("/", lambda r: web.FileResponse(DATA / "index.html"))
    app.router.add_static("/", DATA)

    async def on_start(a):
        a["sim"] = asyncio.create_task(simulate())

    async def on_stop(a):
        a["sim"].cancel()

    app.on_startup.append(on_start)
    app.on_cleanup.append(on_stop)
    web.run_app(app, host="127.0.0.1", port=8000)


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Start the mock and verify the contract**

Run (in one terminal): `python tools/mock_server.py`

Then in another terminal verify the nested shape + rpm flow:

```bash
curl -s localhost:8000/api/v1/status | python -m json.tool | head -40
curl -s -XPOST localhost:8000/api/v1/run -H 'content-type: application/json' \
     -d '{"action":"start","targetC":36,"rpm":8,"durationMin":0}'
curl -s localhost:8000/api/v1/status | python -c \
     "import sys,json; d=json.load(sys.stdin); print('rpm=',d['disc']['rpm'],'setpoint=',d['disc']['rpmSetpoint'],'running=',d['run']['active'])"
curl -s -XPOST localhost:8000/api/v1/disc -H 'content-type: application/json' \
     -d '{"rpm":12,"direction":"ccw","currentMa":700}'
curl -s localhost:8000/api/v1/status | python -c \
     "import sys,json; d=json.load(sys.stdin); print(d['disc'])"
```

Expected:
- First `status` prints a nested object with `apiVersion`, `thermal.safety`, `disc.driver`, `run`, `alarms`.
- After `run`: `rpm= 8.0 setpoint= 8.0 running= True`.
- After `disc`: `disc` shows `'rpm': 12.0`, `'direction': 'ccw'`, `'currentMa': 700`.
- An invalid rpm returns HTTP 400 with `{"ok":false,"error":{"code":"out_of_range",...}}`:
  ```bash
  curl -s -o /dev/null -w "%{http_code}\n" -XPOST localhost:8000/api/v1/run \
       -H 'content-type: application/json' -d '{"action":"start","rpm":99}'
  ```
  Expected: `400`.

- [ ] **Step 3: Commit**

```bash
git add tools/mock_server.py
git commit -m "feat(mock): rewrite mock server for /api/v1 nested telemetry + rpm"
```

---

## Phase 1 done — definition of done

- `pio test -e native` passes (RpmKinematics).
- `pio run -e esp32-s3-devkitc-1` builds clean.
- Firmware emits the §4 nested telemetry under `/api/v1/status` + `/ws`; `/run`, `/setpoint`, `/disc` accept rpm; legacy `/api/*` routes are gone.
- `Reactor` persists and drives by rpm (default 8 rpm, range 0.5–30); CSV header bumped.
- Mock server mirrors the v1 contract (verified by curl).
- **On hardware (when available):** start a run and confirm the disc turns at ~8 rpm (time one revolution ≈ 7.5 s) and that `disc.rpm`/`direction`/`currentMa` in `/api/v1/status` reflect commands.

## Out of scope (later phases)
- P2: StallGuard `load`, `DRV_STATUS` flags, alarm `since` tracking, `disc.driver.flags`.
- P3: `thermal.pid` terms/mode, `/pid`, `/pid/autotune`.
- P4: NTC calibration (`thermal.safety.probe`, `/calibration*`).
- SD `/sd` + `/sd/format`, `/system`, and the `ConfigStore` consolidation.
- The redesigned light-theme UI (separate UI phase).
