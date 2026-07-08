# Adaptive Gain-Scheduled Thermal Control — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop the heater overshooting setpoint (36 °C → 40 °C observed) by selecting PID gains and a max-duty ceiling by distance-to-setpoint, and derive both gain sets from an auto-run relay tune that oscillates safely below target.

**Architecture:** A new pure `GainSchedule` helper maps `(setpoint, pv)` to effective `{kp, ki, kd, dutyCeil}` across three regimes (heat / approach-blend / hold). `ThermalController` queries it each sample in Auto mode and passes the tapered `dutyCeil` as the PID's `outMax`, so the existing anti-windup bleeds the integrator down near setpoint for a soft landing. `PidController` gains a derivative-on-measurement mode. The relay autotune auto-runs once (when untuned) at a reduced setpoint and derives both gain sets. Everything is behind `kEnableAdaptiveThermal`.

**Tech Stack:** C++17, Arduino-ESP32 3.3.9 / ESP-IDF 5.5.4 (pioarduino), PlatformIO, Unity (native host tests). Pure logic headers are host-unit-tested; controller integration is build-verified on the `esp32-s3-devkitc-1` env.

## Global Constraints

- Tooling: use `~/.platformio/penv/bin/pio` (never bare `pio`) — copied verbatim from Claude.md.
- Pure logic headers must stay dependency-free (no `<Arduino.h>`) so they link into the `native` test env — `GainSchedule` and `PidController` follow `RelayAutotune`'s pattern.
- Feature toggle contract: `kEnableAdaptiveThermal = false` must restore today's exact fixed-PID behaviour; log enabled/disabled state at boot.
- YAGNI: no continuous online re-identification. No UI redesign in this plan (firmware + API only).
- No partition change; standard flash. Do NOT deploy to the in-progress "Mini FC 3" run.
- Frequent commits: one per task.

---

### Task 1: `GainSchedule` pure helper

**Files:**
- Create: `include/features/control/GainSchedule.hpp`
- Test: `test/test_gain_schedule/test_main.cpp`

**Interfaces:**
- Produces:
  - `struct GainSchedule::Gains { float kp, ki, kd; };`
  - `struct GainSchedule::Output { float kp, ki, kd, dutyCeil; };`
  - `struct GainSchedule::Config { Gains heat; Gains hold; float bandC; float holdDutyCap; float dutyMax; };`
  - `void setConfig(const Config&)`, `const Config& config() const`
  - `Output evaluate(float setpoint, float pv) const`
  - `const char* regime(float setpoint, float pv) const` → `"heat"|"approach"|"hold"`
  - `static Gains scaleForHeat(const Gains& hold, float kpScale)`

- [ ] **Step 1: Write the failing test**

Create `test/test_gain_schedule/test_main.cpp`:

```cpp
#include <unity.h>
#include "features/control/GainSchedule.hpp"

void setUp() {}
void tearDown() {}

static GainSchedule makeSched() {
  GainSchedule s;
  GainSchedule::Config c;
  c.heat = {0.144f, 0.003f, 0.2f};
  c.hold = {0.08f, 0.0015f, 0.4f};
  c.bandC = 3.0f;
  c.holdDutyCap = 0.6f;
  c.dutyMax = 1.0f;
  s.setConfig(c);
  return s;
}

void test_heat_regime_far_below_setpoint() {
  GainSchedule s = makeSched();
  GainSchedule::Output o = s.evaluate(36.0f, 20.0f);  // error 16 >= band
  TEST_ASSERT_EQUAL_FLOAT(0.144f, o.kp);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, o.dutyCeil);
  TEST_ASSERT_EQUAL_STRING("heat", s.regime(36.0f, 20.0f));
}

void test_hold_regime_at_or_over_setpoint() {
  GainSchedule s = makeSched();
  GainSchedule::Output o = s.evaluate(36.0f, 36.5f);  // error < 0
  TEST_ASSERT_EQUAL_FLOAT(0.08f, o.kp);
  TEST_ASSERT_EQUAL_FLOAT(0.6f, o.dutyCeil);
  TEST_ASSERT_EQUAL_STRING("hold", s.regime(36.0f, 36.5f));
}

void test_approach_blends_at_band_midpoint() {
  GainSchedule s = makeSched();
  // error = 1.5 = band/2 → f = 0.5 → halfway hold..heat
  GainSchedule::Output o = s.evaluate(36.0f, 34.5f);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.112f, o.kp);        // (0.08+0.144)/2
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.8f, o.dutyCeil);    // (0.6+1.0)/2
  TEST_ASSERT_EQUAL_STRING("approach", s.regime(36.0f, 34.5f));
}

void test_scale_for_heat() {
  GainSchedule::Gains hold = {0.10f, 0.002f, 0.30f};
  GainSchedule::Gains heat = GainSchedule::scaleForHeat(hold, 1.8f);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.18f, heat.kp);      // 0.10 * 1.8
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0018f, heat.ki);    // 0.002 * 1.8 * 0.5
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.54f, heat.kd);      // 0.30 * 1.8
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_heat_regime_far_below_setpoint);
  RUN_TEST(test_hold_regime_at_or_over_setpoint);
  RUN_TEST(test_approach_blends_at_band_midpoint);
  RUN_TEST(test_scale_for_heat);
  return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `~/.platformio/penv/bin/pio test -e native -f test_gain_schedule`
Expected: FAIL — `GainSchedule.hpp` not found / no such file.

- [ ] **Step 3: Write minimal implementation**

Create `include/features/control/GainSchedule.hpp`:

```cpp
/*
 * GainSchedule.hpp — distance-to-setpoint gain scheduling for the thermal loop.
 *
 * Pure, header-only, dependency-free (host-unit-tested like PidController /
 * RelayAutotune). Maps (setpoint, pv) to effective PID gains plus a max-duty
 * ceiling across three regimes: heat (far below), approach (linear blend in the
 * band below setpoint), hold (at/over setpoint). The tapered ceiling, fed to the
 * PID as outMax, lets the existing anti-windup bleed the integrator down near
 * setpoint so the element sheds stored heat before the liquid arrives.
 */
#pragma once

class GainSchedule {
 public:
  struct Gains  { float kp = 0.0f, ki = 0.0f, kd = 0.0f; };
  struct Output { float kp, ki, kd, dutyCeil; };
  struct Config {
    Gains heat;                 // aggressive: error >= bandC
    Gains hold;                 // gentle: at/over setpoint
    float bandC = 3.0f;         // approach-band width below setpoint (degC)
    float holdDutyCap = 0.6f;   // max duty at setpoint (soft landing)
    float dutyMax = 1.0f;       // max duty far from setpoint
  };

  void setConfig(const Config& c) { cfg_ = c; }
  const Config& config() const { return cfg_; }

  Output evaluate(float setpoint, float pv) const {
    const float error = setpoint - pv;
    if (error >= cfg_.bandC)
      return {cfg_.heat.kp, cfg_.heat.ki, cfg_.heat.kd, cfg_.dutyMax};
    if (error <= 0.0f)
      return {cfg_.hold.kp, cfg_.hold.ki, cfg_.hold.kd, cfg_.holdDutyCap};
    const float f = error / cfg_.bandC;   // 0 at setpoint .. 1 at band edge
    return {lerp(cfg_.hold.kp, cfg_.heat.kp, f),
            lerp(cfg_.hold.ki, cfg_.heat.ki, f),
            lerp(cfg_.hold.kd, cfg_.heat.kd, f),
            lerp(cfg_.holdDutyCap, cfg_.dutyMax, f)};
  }

  const char* regime(float setpoint, float pv) const {
    const float error = setpoint - pv;
    if (error >= cfg_.bandC) return "heat";
    if (error <= 0.0f)       return "hold";
    return "approach";
  }

  static Gains scaleForHeat(const Gains& hold, float kpScale) {
    return {hold.kp * kpScale, hold.ki * kpScale * 0.5f, hold.kd * kpScale};
  }

 private:
  static float lerp(float a, float b, float f) { return a + (b - a) * f; }
  Config cfg_;
};
```

- [ ] **Step 4: Run test to verify it passes**

Run: `~/.platformio/penv/bin/pio test -e native -f test_gain_schedule`
Expected: PASS (4 tests).

- [ ] **Step 5: Commit**

```bash
git add include/features/control/GainSchedule.hpp test/test_gain_schedule/test_main.cpp
git commit -m "feat(control): GainSchedule — distance-to-setpoint gain + duty-ceiling scheduling"
```

---

### Task 2: `PidController` derivative-on-measurement

**Files:**
- Modify: `include/features/control/PidController.hpp`
- Test: `test/test_pid_controller/test_main.cpp` (extend)

**Interfaces:**
- Consumes: existing `PidController` from Task-0 codebase.
- Produces: `void setDerivativeOnMeasurement(bool on)`; `step()` computes the derivative from `-(pv - prevPv)/dt` when enabled, else the existing `(error - prevError)/dt`. Default `false` (legacy behaviour unchanged). `reset()` also clears `prevPv_`.

- [ ] **Step 1: Write the failing test**

Add these two tests to `test/test_pid_controller/test_main.cpp` (and their `RUN_TEST` lines in `main`):

```cpp
void test_derivative_on_measurement_no_setpoint_kick() {
  PidController pid;
  pid.setGains(0.0f, 0.0f, 1.0f);
  pid.setDerivativeOnMeasurement(true);
  pid.step(10.0f, 8.0f, 1.0f, -100.0f, 100.0f);   // prime prevPv=8
  // setpoint 10→20 with pv unchanged: derivative-on-error would spike +10;
  // on-measurement sees pv delta 0 → d=0 (no kick)
  pid.step(20.0f, 8.0f, 1.0f, -100.0f, 100.0f);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pid.dTerm());
}

void test_derivative_on_measurement_brakes_on_rise() {
  PidController pid;
  pid.setGains(0.0f, 0.0f, 1.0f);
  pid.setDerivativeOnMeasurement(true);
  pid.step(10.0f, 8.0f, 1.0f, -100.0f, 100.0f);   // prevPv=8
  pid.step(10.0f, 9.0f, 1.0f, -100.0f, 100.0f);   // pv 8→9, d = -(1)/1 = -1
  TEST_ASSERT_EQUAL_FLOAT(-1.0f, pid.dTerm());
}
```

Add to `main()`:
```cpp
  RUN_TEST(test_derivative_on_measurement_no_setpoint_kick);
  RUN_TEST(test_derivative_on_measurement_brakes_on_rise);
```

- [ ] **Step 2: Run test to verify it fails**

Run: `~/.platformio/penv/bin/pio test -e native -f test_pid_controller`
Expected: FAIL — `setDerivativeOnMeasurement` not a member.

- [ ] **Step 3: Write minimal implementation**

In `include/features/control/PidController.hpp`:

Add setter (near `setGains`):
```cpp
  void setDerivativeOnMeasurement(bool on) { derivOnMeas_ = on; }
```

Extend `reset()` to clear the new member:
```cpp
  void reset() {
    integral_ = 0.0f; prevError_ = 0.0f; prevPv_ = 0.0f; havePrev_ = false;
    p_ = i_ = d_ = 0.0f; out_ = 0.0f;
  }
```

Replace the derivative computation in `step()`:
```cpp
    const float deriv = havePrev_
        ? (derivOnMeas_ ? -(pv - prevPv_) / dt : (error - prevError_) / dt)
        : 0.0f;
    prevError_ = error;
    prevPv_ = pv;
    havePrev_ = true;
```

Add private members (next to `prevError_`):
```cpp
  float prevPv_ = 0.0f;
  bool derivOnMeas_ = false;
```

- [ ] **Step 4: Run test to verify it passes**

Run: `~/.platformio/penv/bin/pio test -e native -f test_pid_controller`
Expected: PASS (8 tests — 6 legacy + 2 new; legacy derivative-on-error test still passes).

- [ ] **Step 5: Commit**

```bash
git add include/features/control/PidController.hpp test/test_pid_controller/test_main.cpp
git commit -m "feat(control): PID derivative-on-measurement option (no setpoint kick)"
```

---

### Task 3: Config constants + feature toggle

**Files:**
- Modify: `include/app_config.hpp` (`AppConfig::Thermal` namespace ~line 157; `AppConfig::Features` ~line 203)

**Interfaces:**
- Produces (all `static constexpr`, `AppConfig::Thermal`): `kHeatKp/kHeatKi/kHeatKd`, `kHoldKp/kHoldKi/kHoldKd`, `kApproachBandC`, `kHoldDutyCap`, `kTuneMarginC`, `kHeatKpScale`. And `AppConfig::Features::kEnableAdaptiveThermal`.

- [ ] **Step 1: Add the constants**

In `AppConfig::Thermal`, after the existing `kPidKp/kPidKi/kPidKd` block:
```cpp
// Adaptive gain scheduling. hold = today's conservative set (unchanged); heat =
// aggressive set for the ramp far below setpoint. Duty is capped on a taper from
// kHoldDutyCap (at setpoint) up to kDutyMax (>= kApproachBandC below setpoint).
static constexpr float kHoldKp = 0.08f;
static constexpr float kHoldKi = 0.0015f;
static constexpr float kHoldKd = 0.4f;
static constexpr float kHeatKp = 0.144f;   // ~1.8x hold
static constexpr float kHeatKi = 0.003f;
static constexpr float kHeatKd = 0.2f;
static constexpr float kApproachBandC = 3.0f;   // blend/taper band below setpoint
static constexpr float kHoldDutyCap   = 0.6f;   // max duty at setpoint (soft landing)
static constexpr float kTuneMarginC   = 3.0f;   // auto-tune this far BELOW target
static constexpr float kHeatKpScale   = 1.8f;   // heat = scaleForHeat(hold, this)
```

In `AppConfig::Features`, after `kEnableAutotune`:
```cpp
static constexpr bool kEnableAdaptiveThermal = true;  // gain-scheduled thermal + auto-tune-on-first-run
```

- [ ] **Step 2: Verify it compiles (used in later tasks; smoke-build now)**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: SUCCESS (constants unused yet is fine — `constexpr` in a header is not a warning).

- [ ] **Step 3: Commit**

```bash
git add include/app_config.hpp
git commit -m "feat(control): adaptive thermal config constants + kEnableAdaptiveThermal toggle"
```

---

### Task 4: Wire `GainSchedule` + dual-gain persistence into `ThermalController`

**Files:**
- Modify: `include/features/control/ThermalController.hpp`
- Modify: `src/features/control/ThermalController.cpp`

**Interfaces:**
- Consumes: `GainSchedule` (Task 1), `PidController::setDerivativeOnMeasurement` (Task 2), `AppConfig::Thermal::*` + `Features::kEnableAdaptiveThermal` (Task 3).
- Produces: new `Config` fields `adaptiveEnabled`, `bandC`, `holdDutyCap`, `heatKp/Ki/Kd`, `holdKp/Ki/Kd`, `tuneMarginC`, `heatKpScale`; new accessors `const char* regimeStr() const`, `float dutyCeil() const`, `bool tuned() const`, and `GainSchedule::Gains heatGains() const` / `holdGains() const` for telemetry. Auto branch of `update()` uses the schedule when `adaptiveEnabled`.

- [ ] **Step 1: Extend the Config struct + members + accessors (header)**

In `ThermalController.hpp`, add to `struct Config` (after `prefsNamespace`):
```cpp
    bool  adaptiveEnabled = false;   // gain scheduling + auto-tune-on-first-run
    float bandC = 3.0f;
    float holdDutyCap = 0.6f;
    float heatKp = 0.144f, heatKi = 0.003f, heatKd = 0.2f;
    float holdKp = 0.08f,  holdKi = 0.0015f, holdKd = 0.4f;
    float tuneMarginC = 3.0f;
    float heatKpScale = 1.8f;
```

Add `#include "features/control/GainSchedule.hpp"` near the other control includes.

Add public accessors (after `outputDuty()`):
```cpp
  const char* regimeStr() const;                // "heat"|"approach"|"hold" (adaptive) or "fixed"
  float dutyCeil() const { return dutyCeil_; }
  bool tuned() const { return tuned_; }
  GainSchedule::Gains heatGains() const { return sched_.config().heat; }
  GainSchedule::Gains holdGains() const { return sched_.config().hold; }
```

Add private members (after `RelayAutotune autotune_;`):
```cpp
  GainSchedule sched_;
  bool tuned_ = false;
  float dutyCeil_ = 1.0f;
```

- [ ] **Step 2: Load schedule config + persisted gains in begin()/loadGains() (cpp)**

In `ThermalController.cpp`, replace `loadGains()` body with:
```cpp
void ThermalController::loadGains() {
  prefs_.begin(cfg_.prefsNamespace, false);
  GainSchedule::Config sc;
  sc.hold = {prefs_.getFloat("holdKp", cfg_.holdKp),
             prefs_.getFloat("holdKi", cfg_.holdKi),
             prefs_.getFloat("holdKd", cfg_.holdKd)};
  sc.heat = {prefs_.getFloat("heatKp", cfg_.heatKp),
             prefs_.getFloat("heatKi", cfg_.heatKi),
             prefs_.getFloat("heatKd", cfg_.heatKd)};
  sc.bandC = cfg_.bandC;
  sc.holdDutyCap = cfg_.holdDutyCap;
  sc.dutyMax = cfg_.dutyMax;
  sched_.setConfig(sc);
  tuned_ = prefs_.getBool("tuned", false);
  // Fixed-mode PID keeps its own gains (unchanged legacy path).
  pid_.setGains(prefs_.getFloat("kp", cfg_.kp),
                prefs_.getFloat("ki", cfg_.ki),
                prefs_.getFloat("kd", cfg_.kd));
  pid_.setDerivativeOnMeasurement(cfg_.adaptiveEnabled);
}
```

- [ ] **Step 3: Use the schedule in update()'s Auto branch (cpp)**

In `ThermalController::update()`, replace the final `else` (the `Mode::Auto` branch, currently `duty_ = pid_.step(setpoint_, liquidC_, dt, cfg_.dutyMin, cfg_.dutyMax); heater_.setDuty(duty_);`) with:
```cpp
  } else {  // Mode::Auto
    if (cfg_.adaptiveEnabled) {
      const GainSchedule::Output g = sched_.evaluate(setpoint_, liquidC_);
      dutyCeil_ = g.dutyCeil;
      pid_.setGains(g.kp, g.ki, g.kd);
      duty_ = pid_.step(setpoint_, liquidC_, dt, cfg_.dutyMin, g.dutyCeil);
    } else {
      dutyCeil_ = cfg_.dutyMax;
      duty_ = pid_.step(setpoint_, liquidC_, dt, cfg_.dutyMin, cfg_.dutyMax);
    }
    heater_.setDuty(duty_);
  }
```

- [ ] **Step 4: Add regimeStr() (cpp)**

Add near `modeStr()`:
```cpp
const char* ThermalController::regimeStr() const {
  if (!cfg_.adaptiveEnabled) return "fixed";
  return sched_.regime(setpoint_, liquidC_);
}
```

- [ ] **Step 5: Build to verify integration compiles**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: SUCCESS. (ThermalController isn't in the native test set — it pulls Arduino/Preferences — so integration is build-verified; the scheduling logic itself is covered by Task 1's unit tests.)

- [ ] **Step 6: Commit**

```bash
git add include/features/control/ThermalController.hpp src/features/control/ThermalController.cpp
git commit -m "feat(control): ThermalController uses GainSchedule in Auto mode + persists dual gains"
```

---

### Task 5: Auto-run tune at reduced setpoint; derive both gain sets on completion

**Files:**
- Modify: `src/features/control/ThermalController.cpp`
- Modify: `include/features/control/ThermalController.hpp` (one private helper decl)

**Interfaces:**
- Consumes: Task 4 members (`sched_`, `tuned_`), `GainSchedule::scaleForHeat` (Task 1), `cfg_.tuneMarginC`, `cfg_.heatKpScale`.
- Produces: private `void beginAutotuneAt(float atSetpointC);` and `void persistSchedule();`. `enable(true)` auto-starts the tune when `adaptiveEnabled && !tuned_`; autotune completion writes both gain sets + `tuned=true`.

- [ ] **Step 1: Declare the two private helpers (header)**

In `ThermalController.hpp` private section (after `persistGains();`):
```cpp
  void beginAutotuneAt(float atSetpointC);
  void persistSchedule();
```

- [ ] **Step 2: Auto-start the tune on run enable (cpp)**

In `ThermalController::enable(bool on)`, after `enabled_ = on; pid_.reset(); lastPidMs_ = 0;` and before the `if (!on)` block, add:
```cpp
  if (on && cfg_.adaptiveEnabled && !tuned_) {
    // Commission once: relay-tune BELOW target so the culture never overshoots
    // during identification; derived gains apply to the real setpoint after.
    beginAutotuneAt(setpoint_ - cfg_.tuneMarginC);
  }
```

- [ ] **Step 3: Implement beginAutotuneAt + persistSchedule (cpp)**

Add:
```cpp
void ThermalController::beginAutotuneAt(float atSetpointC) {
  RelayAutotune::Config ac;
  ac.relayHigh = cfg_.dutyMax;
  ac.relayLow = cfg_.dutyMin;
  autotune_.begin(atSetpointC, millis(), ac);
  autotuneResult_ = nullptr;
  mode_ = Mode::Autotune;
}

void ThermalController::persistSchedule() {
  const GainSchedule::Config& sc = sched_.config();
  prefs_.putFloat("holdKp", sc.hold.kp); prefs_.putFloat("holdKi", sc.hold.ki); prefs_.putFloat("holdKd", sc.hold.kd);
  prefs_.putFloat("heatKp", sc.heat.kp); prefs_.putFloat("heatKi", sc.heat.ki); prefs_.putFloat("heatKd", sc.heat.kd);
  prefs_.putBool("tuned", tuned_);
}
```

- [ ] **Step 4: Derive both gain sets when the tune completes (cpp)**

In `ThermalController::update()`, in the `Mode::Autotune` branch, replace the `if (autotune_.done()) { ... setGains(kp,ki,kd) ... }` success path with:
```cpp
      if (autotune_.done()) {
        float kp, ki, kd;
        if (autotune_.computeGains(kp, ki, kd)) {
          GainSchedule::Config sc = sched_.config();
          sc.hold = {kp, ki, kd};                                   // conservative = hold
          sc.heat = GainSchedule::scaleForHeat(sc.hold, cfg_.heatKpScale);
          sched_.setConfig(sc);
          tuned_ = true;
          persistSchedule();
        }
        autotuneResult_ = "ok";
      } else {
        autotuneResult_ = "failed";
      }
```

(Leave the `mode_ = Mode::Auto; pid_.reset();` that follows the if/else intact — after tuning, Auto mode resumes at the real `setpoint_`.)

- [ ] **Step 5: Build to verify**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: SUCCESS.

- [ ] **Step 6: Commit**

```bash
git add include/features/control/ThermalController.hpp src/features/control/ThermalController.cpp
git commit -m "feat(control): auto-run relay tune below setpoint, derive heat+hold gain sets"
```

---

### Task 6: Build the controller config from app_config + expose telemetry

**Files:**
- Modify: `src/system/AppRuntime.cpp` (`makeThermalConfig()` ~line 88; `buildStatusJson()` `thermal.pid` block ~line 325; boot `[FEAT]` logs ~line 466)
- Modify: `API.md` (thermal.pid section)

**Interfaces:**
- Consumes: Task 3 config constants + toggle; Task 4/5 accessors (`regimeStr()`, `dutyCeil()`, `tuned()`, `heatGains()`, `holdGains()`).
- Produces: populated `ThermalController::Config` adaptive fields; `thermal.pid.regime`, `thermal.pid.dutyCeil`, `thermal.pid.tuned`, `thermal.pid.schedule.{heat,hold}` in the status JSON.

- [ ] **Step 1: Populate the adaptive Config in makeThermalConfig() (cpp)**

In `AppRuntime.cpp` `makeThermalConfig()`, before `return c;`:
```cpp
  c.adaptiveEnabled = AppConfig::Features::kEnableAdaptiveThermal;
  c.bandC = AppConfig::Thermal::kApproachBandC;
  c.holdDutyCap = AppConfig::Thermal::kHoldDutyCap;
  c.heatKp = AppConfig::Thermal::kHeatKp; c.heatKi = AppConfig::Thermal::kHeatKi; c.heatKd = AppConfig::Thermal::kHeatKd;
  c.holdKp = AppConfig::Thermal::kHoldKp; c.holdKi = AppConfig::Thermal::kHoldKi; c.holdKd = AppConfig::Thermal::kHoldKd;
  c.tuneMarginC = AppConfig::Thermal::kTuneMarginC;
  c.heatKpScale = AppConfig::Thermal::kHeatKpScale;
```

- [ ] **Step 2: Add the boot log line (cpp)**

After the existing `[FEAT] autotune:` printf in `begin()`:
```cpp
  Serial.printf("[FEAT] adaptive PID: %s\n", AppConfig::Features::kEnableAdaptiveThermal ? "enabled" : "disabled");
```

- [ ] **Step 3: Emit the new telemetry fields (cpp)**

In `buildStatusJson()`, in the `JsonObject pid = th["pid"]...` block, after `pid["mode"] = g_thermal.modeStr();`:
```cpp
  pid["regime"] = g_thermal.regimeStr();
  pid["dutyCeil"] = roundf(g_thermal.dutyCeil() * 1000) / 1000.0f;
  pid["tuned"] = g_thermal.tuned();
  JsonObject sch = pid["schedule"].to<JsonObject>();
  const GainSchedule::Gains hg = g_thermal.heatGains();
  const GainSchedule::Gains dg = g_thermal.holdGains();
  JsonObject shHeat = sch["heat"].to<JsonObject>();
  shHeat["kp"] = hg.kp; shHeat["ki"] = hg.ki; shHeat["kd"] = hg.kd;
  JsonObject shHold = sch["hold"].to<JsonObject>();
  shHold["kp"] = dg.kp; shHold["ki"] = dg.ki; shHold["kd"] = dg.kd;
```

Add near the other control includes at the top of `AppRuntime.cpp` (if not already transitively available):
```cpp
#include "features/control/GainSchedule.hpp"
```

- [ ] **Step 4: Document in API.md**

In `API.md`, under the `thermal.pid` object description, add:
```
"regime": "heat" | "approach" | "hold" | "fixed"   // active gain-scheduling regime; "fixed" when kEnableAdaptiveThermal is false
"dutyCeil": 0.6            // current max-duty ceiling (soft-landing taper near setpoint)
"tuned": true             // false until the first auto-run relay tune completes
"schedule": {             // present when adaptive; both derived gain sets
  "heat": { "kp": 0.144, "ki": 0.003, "kd": 0.2 },
  "hold": { "kp": 0.08, "ki": 0.0015, "kd": 0.4 }
}
```

- [ ] **Step 5: Build to verify**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: SUCCESS.

- [ ] **Step 6: Full native test sweep (guard against regressions)**

Run: `~/.platformio/penv/bin/pio test -e native`
Expected: all suites PASS (incl. `test_gain_schedule`, `test_pid_controller`).

- [ ] **Step 7: Commit**

```bash
git add src/system/AppRuntime.cpp API.md
git commit -m "feat(control): build adaptive thermal config + expose regime/schedule telemetry"
```

---

### Task 7: Toggle-off regression check + roadmap

**Files:**
- Modify: `include/app_config.hpp` (temporarily), `Roadmap.md`

- [ ] **Step 1: Verify the disabled path builds and reverts behaviour**

Temporarily set `AppConfig::Features::kEnableAdaptiveThermal = false`, then:
Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: SUCCESS. Confirm `regimeStr()` returns `"fixed"` and `update()` takes the legacy `cfg_.dutyMax` path (no scheduling). Then restore to `true`.

- [ ] **Step 2: Tick the roadmap**

In `Roadmap.md`, under the thermal/control area, add:
```
- [x] Adaptive gain-scheduled thermal control — heat/approach/hold regimes with tapered duty
      ceiling (kills 36→40 overshoot); auto-run relay tune below setpoint derives heat+hold
      gains; toggle kEnableAdaptiveThermal. Spec/plan under docs/superpowers/. On-hardware
      overshoot verification pending next run.
```

- [ ] **Step 3: Commit**

```bash
git add include/app_config.hpp Roadmap.md
git commit -m "docs(control): roadmap — adaptive thermal control shipped (HW overshoot check pending)"
```

---

## On-hardware verification (after implementation, not a code task)

The native tests prove the scheduling/PID/tune math. The overshoot fix itself must be
confirmed on the bench reactor on the next run (NOT on the live "Mini FC 3"):
1. Flash, start a run at a test setpoint from cold.
2. Watch `/api/v1/status` `thermal`: `regime` should read `heat` → `approach` → `hold`; the
   auto-tune runs first (`pid.mode` = `autotune`, `tuned` flips true) a few °C below target.
3. Confirm peak `tempC` does not exceed `setpointC` (goal: zero overshoot, slower settle OK).
`tools/heapmon.py` can log `thermal` alongside heap if a longer capture is wanted.
```
```
