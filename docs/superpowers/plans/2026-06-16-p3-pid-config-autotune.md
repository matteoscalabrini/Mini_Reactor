# Phase 3 — PID Runtime Config + Autotune Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the bath PID gains runtime-configurable + persisted, add a control mode (auto/manual/autotune), implement a relay (Åström-Hägglund) autotune, and surface it all through `thermal.pid` telemetry + `/pid` commands.

**Architecture:** Two pure, host-tested units carry the logic — `PidController` (the PID step with anti-windup + term exposure) and `RelayAutotune` (relay oscillation → Tyreus-Luyben gains, time injected). `ThermalController` swaps its inline math for `PidController`, gains a `mode`, persists gains in its own NVS namespace, and runs `RelayAutotune` in autotune mode. Commands flow `WebInterface` → `Reactor` → `ThermalController`; telemetry is read directly from `g_thermal` in `buildStatusJson`.

**Tech Stack:** C++17 / Arduino-ESP32 / PlatformIO, ArduinoJson v7, NVS Preferences, Unity (native tests), Python/aiohttp (mock).

**Spec:** `docs/superpowers/specs/2026-06-15-reactor-firmware-api-design.md` — Phase 3 / §10 P3, telemetry §4 `thermal.pid`, endpoints §5, autotune §8. Builds on P1+P2 (on `main`).

---

## Baseline assumptions

- Start from `main` (P1+P2 merged). Confirm green before Task 1: `/tmp/pio-venv/bin/pio run -e esp32-s3-devkitc-1` (SUCCESS) and `/tmp/pio-venv/bin/pio test -e native` (14 pass).
- PlatformIO CLI: `/tmp/pio-venv/bin/pio` (not on PATH). Mock Python: `/tmp/pio-venv/bin/python` (aiohttp).
- Work on a branch: `git checkout -b feature/p3-pid-autotune`.
- **Persistence (deviation from spec §7 ConfigStore):** gains persist in a dedicated `ThermalController` NVS namespace `"pid"` (mirrors how `Reactor`/`WifiManager` each own a namespace). The single `ConfigStore` consolidation stays deferred — building it now is premature.

## Scope

In scope: runtime gains + persistence, mode (auto/manual/autotune), relay autotune, `thermal.pid` telemetry, `POST /api/v1/pid`, `POST /api/v1/pid/autotune`, mock simulation.

**Out of scope (deferred):**
- `GET /api/v1/pid` + `GET /api/v1/pid/autotune` convenience endpoints — their data is already in `/status` `thermal.pid`; dedicated GETs are a thin add-on for later.
- Manual-mode operator duty entry (manual just *freezes* the last duty for now).
- A `busy` rejection on re-`start` of autotune (a redundant start is idempotent at the controller).
- P4 (NTC calibration), SD/system endpoints, ConfigStore, UI.

## File structure

| File | Responsibility | Action |
|---|---|---|
| `include/control/PidController.hpp` | Pure PID step (anti-windup) + term/gain exposure. No deps. | Create |
| `test/test_pid_controller/test_main.cpp` | Unity tests for the PID. | Create |
| `include/control/RelayAutotune.hpp` | Pure relay autotune (oscillation → gains; time injected). No deps. | Create |
| `test/test_relay_autotune/test_main.cpp` | Unity tests for autotune. | Create |
| `include/control/ThermalController.hpp` / `.cpp` | Use PidController; add mode, gains persistence, autotune wiring, getters. | Modify |
| `include/control/Reactor.hpp` / `.cpp` | PID command delegation to ThermalController. | Modify |
| `include/net/WebInterface.hpp` / `src/net/WebInterface.cpp` | `/pid` + `/pid/autotune` POST routes + Pending. | Modify |
| `src/system/AppRuntime.cpp` | `thermal.pid` telemetry block. | Modify |
| `tools/mock_server.py` | `thermal.pid` + `/pid` + `/pid/autotune` simulation. | Modify |

> **Embedded testing note:** `PidController` and `RelayAutotune` are host-unit-tested (real TDD). The ThermalController wiring, endpoints, and telemetry are verified by compile + the mock's curl checks.

---

### Task 1: PidController (pure, TDD)

**Files:**
- Create: `include/control/PidController.hpp`
- Create: `test/test_pid_controller/test_main.cpp`

- [ ] **Step 1: Write the failing test**

Create `test/test_pid_controller/test_main.cpp`:

```cpp
#include <unity.h>
#include "control/PidController.hpp"

void setUp() {}
void tearDown() {}

void test_proportional_only() {
  PidController pid;
  pid.setGains(2.0f, 0.0f, 0.0f);
  // error = 10-8 = 2; out = 2*2 = 4
  TEST_ASSERT_EQUAL_FLOAT(4.0f, pid.step(10.0f, 8.0f, 1.0f, 0.0f, 100.0f));
  TEST_ASSERT_EQUAL_FLOAT(4.0f, pid.pTerm());
}

void test_output_clamped_high() {
  PidController pid;
  pid.setGains(100.0f, 0.0f, 0.0f);
  // raw = 100*10 = 1000, clamped to 1.0
  TEST_ASSERT_EQUAL_FLOAT(1.0f, pid.step(10.0f, 0.0f, 1.0f, 0.0f, 1.0f));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, pid.output());
}

void test_integral_accumulates() {
  PidController pid;
  pid.setGains(0.0f, 1.0f, 0.0f);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, pid.step(10.0f, 9.0f, 1.0f, -100.0f, 100.0f)); // integral=1
  TEST_ASSERT_EQUAL_FLOAT(2.0f, pid.step(10.0f, 9.0f, 1.0f, -100.0f, 100.0f)); // integral=2
}

void test_anti_windup_holds_integral() {
  PidController pid;
  pid.setGains(0.0f, 1.0f, 0.0f);
  // error=10, integral would be 10 → i-term 10 → clamp to 1 → back-calc integral to 0
  pid.step(10.0f, 0.0f, 1.0f, 0.0f, 1.0f);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pid.iTerm());  // integral did not wind up
}

void test_derivative_on_change() {
  PidController pid;
  pid.setGains(0.0f, 0.0f, 1.0f);
  pid.step(10.0f, 8.0f, 1.0f, -100.0f, 100.0f);          // first: no prev, d=0
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pid.dTerm());
  pid.step(10.0f, 9.0f, 1.0f, -100.0f, 100.0f);          // error 2→1, deriv=-1
  TEST_ASSERT_EQUAL_FLOAT(-1.0f, pid.dTerm());
}

void test_reset_clears_state() {
  PidController pid;
  pid.setGains(0.0f, 1.0f, 0.0f);
  pid.step(10.0f, 9.0f, 1.0f, -100.0f, 100.0f);  // integral=1
  pid.reset();
  TEST_ASSERT_EQUAL_FLOAT(1.0f, pid.step(10.0f, 9.0f, 1.0f, -100.0f, 100.0f)); // integral back to 1
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_proportional_only);
  RUN_TEST(test_output_clamped_high);
  RUN_TEST(test_integral_accumulates);
  RUN_TEST(test_anti_windup_holds_integral);
  RUN_TEST(test_derivative_on_change);
  RUN_TEST(test_reset_clears_state);
  return UNITY_END();
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `/tmp/pio-venv/bin/pio test -e native -f test_pid_controller`
Expected: FAIL/ERROR — `control/PidController.hpp: No such file or directory`.

- [ ] **Step 3: Create the implementation**

Create `include/control/PidController.hpp`:

```cpp
/*
 * PidController.hpp — pure PID step with anti-windup + term exposure.
 *
 * Header-only, dependency-free so it is host-unit-testable. One `step()` per
 * control sample; output is clamped to [outMin,outMax] and the integral is
 * back-calculated on saturation (conditional integration). Term contributions
 * (p/i/d) and the last output are exposed for telemetry.
 */
#pragma once

class PidController {
 public:
  void setGains(float kp, float ki, float kd) { kp_ = kp; ki_ = ki; kd_ = kd; }
  float kp() const { return kp_; }
  float ki() const { return ki_; }
  float kd() const { return kd_; }

  void reset() {
    integral_ = 0.0f; prevError_ = 0.0f; havePrev_ = false;
    p_ = i_ = d_ = 0.0f; out_ = 0.0f;
  }

  float step(float setpoint, float pv, float dt, float outMin, float outMax) {
    const float error = setpoint - pv;
    integral_ += error * dt;
    const float deriv = havePrev_ ? (error - prevError_) / dt : 0.0f;
    prevError_ = error;
    havePrev_ = true;

    p_ = kp_ * error;
    i_ = ki_ * integral_;
    d_ = kd_ * deriv;
    float out = p_ + i_ + d_;

    if (out > outMax) {
      out = outMax;
      integral_ -= error * dt;   // anti-windup: undo this step's accumulation
      i_ = ki_ * integral_;
    } else if (out < outMin) {
      out = outMin;
      integral_ -= error * dt;
      i_ = ki_ * integral_;
    }
    out_ = out;
    return out;
  }

  float pTerm() const { return p_; }
  float iTerm() const { return i_; }
  float dTerm() const { return d_; }
  float output() const { return out_; }

 private:
  float kp_ = 0.0f, ki_ = 0.0f, kd_ = 0.0f;
  float integral_ = 0.0f, prevError_ = 0.0f;
  bool havePrev_ = false;
  float p_ = 0.0f, i_ = 0.0f, d_ = 0.0f, out_ = 0.0f;
};
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `/tmp/pio-venv/bin/pio test -e native -f test_pid_controller`
Expected: PASS — `6 Tests 0 Failures 0 Ignored ... OK`.

- [ ] **Step 5: Commit**

```bash
git add include/control/PidController.hpp test/test_pid_controller/test_main.cpp
git commit -m "feat(control): add host-tested PidController (anti-windup + terms)"
```

---

### Task 2: RelayAutotune (pure, TDD)

**Files:**
- Create: `include/control/RelayAutotune.hpp`
- Create: `test/test_relay_autotune/test_main.cpp`

- [ ] **Step 1: Write the failing test**

Create `test/test_relay_autotune/test_main.cpp`:

```cpp
#include <unity.h>
#include <cmath>
#include "control/RelayAutotune.hpp"

void setUp() {}
void tearDown() {}

// Relay drives high (heat) below setpoint-hyst, low above setpoint+hyst.
void test_relay_switching() {
  RelayAutotune at;
  RelayAutotune::Config c;  // relayHigh=1, relayLow=0, hyst=0.3 defaults
  at.begin(30.0f, 0, c);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, at.update(28.0f, 100));   // below → high
  TEST_ASSERT_EQUAL_FLOAT(0.0f, at.update(32.0f, 200));   // above → low
}

// Feed a clean triangle (setpoint 30, amplitude 2, period 8 s) for ~6 cycles;
// expect completion and Tyreus-Luyben gains from Ku=4d/(pi*a), Tu=period.
void test_converges_and_computes_gains() {
  RelayAutotune at;
  RelayAutotune::Config c;
  at.begin(30.0f, 0, c);
  const float amp = 2.0f, P = 8000.0f;   // ms
  for (uint32_t t = 0; t <= 60000; t += 250) {
    const float ph = (float)(t % (uint32_t)P) / P;          // 0..1
    const float tri = ph < 0.5f ? (ph * 2.0f) : (2.0f - ph * 2.0f);  // 0..1..0
    const float pv = 30.0f - amp + 2.0f * amp * tri;        // 28..32..28
    at.update(pv, t);
  }
  TEST_ASSERT_TRUE(at.done());
  TEST_ASSERT_FALSE(at.failed());
  TEST_ASSERT_EQUAL_INT(100, at.progressPct());
  float kp, ki, kd;
  TEST_ASSERT_TRUE(at.computeGains(kp, ki, kd));
  // Ku = 4*0.5/(pi*2) = 1/pi = 0.318; kp = 0.45*Ku = 0.143
  TEST_ASSERT_FLOAT_WITHIN(0.025f, 0.143f, kp);
  // ki = kp/(2.2*Tu), Tu≈8 → ~0.0081 ; kd = kp*Tu/6.3 → ~0.182
  TEST_ASSERT_FLOAT_WITHIN(0.003f, 0.0081f, ki);
  TEST_ASSERT_FLOAT_WITHIN(0.04f, 0.182f, kd);
}

void test_timeout_fails() {
  RelayAutotune at;
  RelayAutotune::Config c;
  c.timeoutMs = 1000;
  at.begin(30.0f, 0, c);
  at.update(25.0f, 500);     // constant below setpoint — no oscillation
  at.update(25.0f, 2000);    // past timeout
  TEST_ASSERT_TRUE(at.failed());
  float kp, ki, kd;
  TEST_ASSERT_FALSE(at.computeGains(kp, ki, kd));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_relay_switching);
  RUN_TEST(test_converges_and_computes_gains);
  RUN_TEST(test_timeout_fails);
  return UNITY_END();
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `/tmp/pio-venv/bin/pio test -e native -f test_relay_autotune`
Expected: FAIL/ERROR — `control/RelayAutotune.hpp: No such file or directory`.

- [ ] **Step 3: Create the implementation**

Create `include/control/RelayAutotune.hpp`:

```cpp
/*
 * RelayAutotune.hpp — pure relay (Åström-Hägglund) PID autotune.
 *
 * Header-only, dependency-free (time injected → host-testable). Drives a relay
 * output around the setpoint with hysteresis; measures the induced oscillation
 * period (Tu) and amplitude (a) over `targetCycles`, then derives Tyreus-Luyben
 * gains from the ultimate gain Ku = 4d/(pi*a), where d is the relay half-amplitude.
 *   Kp = 0.45 Ku, Ki = Kp/(2.2 Tu), Kd = Kp*Tu/6.3.
 * `update(pv, nowMs)` returns the relay duty to apply to the heater each sample.
 */
#pragma once

#include <cstdint>

class RelayAutotune {
 public:
  struct Config {
    float relayHigh = 1.0f;        // heater duty below setpoint
    float relayLow = 0.0f;         // duty above setpoint
    float hysteresisC = 0.3f;      // switching deadband around setpoint
    int targetCycles = 4;          // oscillation cycles to average
    uint32_t timeoutMs = 1800000;  // 30 min cap
  };

  void begin(float setpointC, uint32_t nowMs, const Config& cfg) {
    cfg_ = cfg; setpoint_ = setpointC; startMs_ = nowMs;
    high_ = true; output_ = cfg_.relayHigh;
    cycles_ = 0; haveLastCross_ = false; lastCrossMs_ = 0;
    periodSum_ = 0.0f; ampSum_ = 0.0f;
    curMin_ = 1e30f; curMax_ = -1e30f;
    done_ = false; failed_ = false;
  }

  float update(float pv, uint32_t nowMs) {
    if (done_ || failed_) return cfg_.relayLow;
    if (nowMs - startMs_ > cfg_.timeoutMs) { failed_ = true; return cfg_.relayLow; }

    if (pv < curMin_) curMin_ = pv;
    if (pv > curMax_) curMax_ = pv;

    if (high_ && pv > setpoint_ + cfg_.hysteresisC) {
      high_ = false; output_ = cfg_.relayLow;
    } else if (!high_ && pv < setpoint_ - cfg_.hysteresisC) {
      high_ = true; output_ = cfg_.relayHigh;
      if (haveLastCross_) {
        periodSum_ += (float)(nowMs - lastCrossMs_);
        ampSum_ += (curMax_ - curMin_) / 2.0f;
        if (++cycles_ >= cfg_.targetCycles) done_ = true;
      }
      lastCrossMs_ = nowMs; haveLastCross_ = true;
      curMin_ = 1e30f; curMax_ = -1e30f;
    }
    return output_;
  }

  bool done() const { return done_; }
  bool failed() const { return failed_; }
  int progressPct() const {
    if (failed_) return 100;
    if (cfg_.targetCycles <= 0) return 0;
    int p = (int)(100.0f * cycles_ / cfg_.targetCycles);
    return p > 100 ? 100 : p;
  }

  bool computeGains(float& kp, float& ki, float& kd) const {
    if (!done_ || cycles_ == 0) return false;
    const float Tu = (periodSum_ / cycles_) / 1000.0f;   // seconds
    const float a = ampSum_ / cycles_;                   // PV amplitude
    if (a <= 1e-4f || Tu <= 1e-3f) return false;
    const float d = (cfg_.relayHigh - cfg_.relayLow) / 2.0f;
    const float Ku = (4.0f * d) / (3.14159265f * a);
    kp = 0.45f * Ku;
    ki = kp / (2.2f * Tu);
    kd = kp * Tu / 6.3f;
    return true;
  }

 private:
  Config cfg_;
  float setpoint_ = 0.0f, output_ = 0.0f;
  bool high_ = true;
  uint32_t startMs_ = 0, lastCrossMs_ = 0;
  bool haveLastCross_ = false, done_ = false, failed_ = false;
  int cycles_ = 0;
  float periodSum_ = 0.0f, ampSum_ = 0.0f, curMin_ = 1e30f, curMax_ = -1e30f;
};
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `/tmp/pio-venv/bin/pio test -e native -f test_relay_autotune`
Expected: PASS — `3 Tests 0 Failures 0 Ignored ... OK`.

- [ ] **Step 5: Commit**

```bash
git add include/control/RelayAutotune.hpp test/test_relay_autotune/test_main.cpp
git commit -m "feat(control): add host-tested RelayAutotune (Tyreus-Luyben)"
```

---

### Task 3: ThermalController — PidController, mode, persistence, autotune wiring

**Files:**
- Modify: `include/control/ThermalController.hpp`
- Modify: `src/control/ThermalController.cpp`
- Modify: `include/control/Reactor.hpp`
- Modify: `src/control/Reactor.cpp`

- [ ] **Step 1: Update `ThermalController.hpp`**

Add includes after the existing ones (after `#include "sensor/Thermistor.hpp"`):

```cpp
#include <Preferences.h>

#include "control/PidController.hpp"
#include "control/RelayAutotune.hpp"
```

Add a `Mode` enum and a prefs namespace to the `public:` section (just under `class ThermalController {` `public:`):

```cpp
  enum class Mode { Auto, Manual, Autotune };
```

In `struct Config`, add (after `safetyCheckMs`):

```cpp
    const char* prefsNamespace = "pid";
```

Replace the existing PID-related public methods block. After `float setpoint() const ...`, add the new control surface:

```cpp
  /* Gains — runtime-settable and persisted in NVS. */
  void setGains(float kp, float ki, float kd);
  float kp() const { return pid_.kp(); }
  float ki() const { return pid_.ki(); }
  float kd() const { return pid_.kd(); }

  /* Mode. setModeStr accepts "auto"|"manual"; startAutotune/cancelAutotune
   * drive the autotune lifecycle. */
  void setMode(Mode m);
  void setModeStr(const char* m);          // "auto"|"manual"
  void startAutotune();
  void cancelAutotune();
  Mode mode() const { return mode_; }
  const char* modeStr() const;

  /* PID telemetry. */
  float pTerm() const { return pid_.pTerm(); }
  float iTerm() const { return pid_.iTerm(); }
  float dTerm() const { return pid_.dTerm(); }
  float outputDuty() const { return duty_; }  // 0..1 controller output

  /* Autotune telemetry. */
  bool autotuneActive() const { return mode_ == Mode::Autotune; }
  int autotuneProgress() const { return autotune_.progressPct(); }
  const char* autotuneResult() const { return autotuneResult_; }  // null|"ok"|"failed"
```

Remove the old `kp/ki/kd` from `Config`? **No** — keep `kp/ki/kd` in `Config` (they remain the NVS defaults). In `private:`, REMOVE the now-unused inline PID state (`integral_`, `prevError_`, `havePrev_`) and ADD the new members:

Replace the private block:

```cpp
 private:
  void applyOff();
  void loadGains();
  void persistGains();

  Ds18b20& liquid_;
  Thermistor& ntc_;
  Heater& heater_;
  Config cfg_;

  PidController pid_;
  RelayAutotune autotune_;
  Mode mode_ = Mode::Auto;
  const char* autotuneResult_ = nullptr;
  Preferences prefs_;

  bool enabled_ = false;
  float setpoint_ = 30.0f;
  float liquidC_ = NAN;
  float heaterC_ = NAN;
  float duty_ = 0.0f;
  bool safetyTrip_ = false;

  uint32_t lastSafetyMs_ = 0;
  uint32_t lastPidMs_ = 0;
};
```

- [ ] **Step 2: Rewrite `src/control/ThermalController.cpp`**

Replace the ENTIRE file with:

```cpp
/*
 * ThermalController.cpp — liquid-probe PID (auto/manual/autotune) + NTC safety.
 * See include/control/ThermalController.hpp.
 */

#include "control/ThermalController.hpp"

#include <math.h>
#include <string.h>

ThermalController::ThermalController(Ds18b20& liquid, Thermistor& heaterNtc,
                                     Heater& heater, const Config& config)
    : liquid_(liquid), ntc_(heaterNtc), heater_(heater), cfg_(config) {
  setpoint_ = config.defaultSetpointC;
  pid_.setGains(config.kp, config.ki, config.kd);
}

void ThermalController::begin() {
  liquid_.begin();
  ntc_.begin();
  heater_.begin();
  loadGains();
  lastSafetyMs_ = millis();
}

void ThermalController::loadGains() {
  prefs_.begin(cfg_.prefsNamespace, false);
  pid_.setGains(prefs_.getFloat("kp", cfg_.kp),
                prefs_.getFloat("ki", cfg_.ki),
                prefs_.getFloat("kd", cfg_.kd));
}

void ThermalController::persistGains() {
  prefs_.putFloat("kp", pid_.kp());
  prefs_.putFloat("ki", pid_.ki());
  prefs_.putFloat("kd", pid_.kd());
}

void ThermalController::enable(bool on) {
  if (on == enabled_) return;
  enabled_ = on;
  pid_.reset();
  lastPidMs_ = 0;
  if (!on) {
    if (mode_ == Mode::Autotune) mode_ = Mode::Auto;
    applyOff();
  }
}

void ThermalController::setSetpoint(float celsius) { setpoint_ = celsius; }

void ThermalController::setGains(float kp, float ki, float kd) {
  pid_.setGains(kp, ki, kd);
  persistGains();
}

void ThermalController::setMode(Mode m) {
  if (m == mode_) return;
  if (m == Mode::Autotune) {
    RelayAutotune::Config ac;
    ac.relayHigh = cfg_.dutyMax;
    ac.relayLow = cfg_.dutyMin;
    autotune_.begin(setpoint_, millis(), ac);
    autotuneResult_ = nullptr;  // in progress
  }
  if (m == Mode::Auto) {
    pid_.reset();
    lastPidMs_ = 0;
  }
  mode_ = m;
}

void ThermalController::setModeStr(const char* m) {
  if (m && strcmp(m, "manual") == 0) setMode(Mode::Manual);
  else setMode(Mode::Auto);  // any non-"manual" returns to auto
}

void ThermalController::startAutotune() { setMode(Mode::Autotune); }

void ThermalController::cancelAutotune() {
  if (mode_ == Mode::Autotune) {
    autotuneResult_ = nullptr;  // cancelled, no result
    setMode(Mode::Auto);
  }
}

const char* ThermalController::modeStr() const {
  switch (mode_) {
    case Mode::Manual: return "manual";
    case Mode::Autotune: return "autotune";
    default: return "auto";
  }
}

void ThermalController::applyOff() {
  heater_.off();
  duty_ = 0.0f;
  pid_.reset();
}

void ThermalController::update() {
  const uint32_t now = millis();

  const bool fresh = liquid_.update();
  if (fresh) liquidC_ = liquid_.celsius();

  // Fast, independent safety poll of the heater NTC.
  if (now - lastSafetyMs_ >= cfg_.safetyCheckMs) {
    lastSafetyMs_ = now;
    heaterC_ = ntc_.readCelsius();
    if (enabled_) {
      const bool over = isnan(heaterC_) || heaterC_ >= cfg_.heaterSafetyMaxC;
      if (over) { safetyTrip_ = true; applyOff(); }
      else safetyTrip_ = false;
    }
  }

  if (!enabled_) { applyOff(); return; }
  if (safetyTrip_) return;
  if (!fresh) return;
  if (isnan(liquidC_) || liquidC_ >= cfg_.processMaxC) { applyOff(); return; }

  const float dt = lastPidMs_ ? (now - lastPidMs_) / 1000.0f : 0.75f;
  lastPidMs_ = now;

  if (mode_ == Mode::Autotune) {
    duty_ = autotune_.update(liquidC_, now);
    heater_.setDuty(duty_);
    if (autotune_.done() || autotune_.failed()) {
      if (autotune_.done()) {
        float kp, ki, kd;
        if (autotune_.computeGains(kp, ki, kd)) { setGains(kp, ki, kd); }
        autotuneResult_ = "ok";
      } else {
        autotuneResult_ = "failed";
      }
      mode_ = Mode::Auto;
      pid_.reset();
    }
  } else if (mode_ == Mode::Manual) {
    heater_.setDuty(duty_);  // freeze at last duty
  } else {
    duty_ = pid_.step(setpoint_, liquidC_, dt, cfg_.dutyMin, cfg_.dutyMax);
    heater_.setDuty(duty_);
  }
}
```

- [ ] **Step 3: Add PID delegation to `Reactor.hpp`**

In `include/control/Reactor.hpp`, in the `public:` section (after `void setDiscEnabled(bool on);`), add:

```cpp
  /* PID control surface (delegates to the ThermalController). */
  void setPidGains(float kp, float ki, float kd) { thermal_.setGains(kp, ki, kd); }
  void setPidMode(const char* m) { thermal_.setModeStr(m); }
  void startAutotune() { thermal_.startAutotune(); }
  void cancelAutotune() { thermal_.cancelAutotune(); }
```

(`thermal_` is already a member reference; these are inline one-liners, so `Reactor.cpp` needs no change. The `ThermalController` full definition is already included via `Reactor.hpp`'s `#include "control/ThermalController.hpp"`.)

- [ ] **Step 4: Compile + native tests**

Run: `/tmp/pio-venv/bin/pio run -e esp32-s3-devkitc-1` → Expected: `SUCCESS`.
Run: `/tmp/pio-venv/bin/pio test -e native` → Expected: all pass (now 23: rpm 4 + drv_status 5 + alarm_tracker 5 + pid_controller 6 + relay_autotune 3).

- [ ] **Step 5: Commit**

```bash
git add include/control/ThermalController.hpp src/control/ThermalController.cpp \
        include/control/Reactor.hpp
git commit -m "feat(control): runtime PID gains + mode + relay autotune in ThermalController"
```

---

### Task 4: Endpoints + `thermal.pid` telemetry

**Files:**
- Modify: `include/net/WebInterface.hpp`
- Modify: `src/net/WebInterface.cpp`
- Modify: `src/system/AppRuntime.cpp`

- [ ] **Step 1: Add Pending fields in `WebInterface.hpp`**

In the `struct Pending { ... }`, add (before `bool wifiConnect = false;`):

```cpp
    bool pidGains = false;  float pidKp = 0, pidKi = 0, pidKd = 0;
    bool pidMode = false;   String pidModeStr;
    bool autotuneStart = false;
    bool autotuneCancel = false;
```

- [ ] **Step 2: Add the routes in `WebInterface.cpp` `registerRoutes()`**

Add these two handlers (place them after the `/api/v1/disc` handler block, before the wifi handlers):

```cpp
  // ── POST pid (gains + mode) ──
  auto* pidHandler = new AsyncCallbackJsonWebHandler(
      "/api/v1/pid", [this](AsyncWebServerRequest* req, JsonVariant& json) {
        JsonObject o = json.as<JsonObject>();
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (!o["kp"].isNull() && !o["ki"].isNull() && !o["kd"].isNull()) {
          pending_.pidGains = true;
          pending_.pidKp = o["kp"].as<float>();
          pending_.pidKi = o["ki"].as<float>();
          pending_.pidKd = o["kd"].as<float>();
        }
        if (!o["mode"].isNull()) {
          pending_.pidMode = true;
          pending_.pidModeStr = o["mode"].as<String>();
        }
        xSemaphoreGive(mutex_);
        sendOk(req);
      });
  server_->addHandler(pidHandler);

  // ── POST pid/autotune (start|cancel) ──
  auto* autotuneHandler = new AsyncCallbackJsonWebHandler(
      "/api/v1/pid/autotune", [this](AsyncWebServerRequest* req, JsonVariant& json) {
        JsonObject o = json.as<JsonObject>();
        const String action = o["action"] | "";
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (action == "start") pending_.autotuneStart = true;
        else if (action == "cancel") pending_.autotuneCancel = true;
        xSemaphoreGive(mutex_);
        if (action != "start" && action != "cancel") {
          sendError(req, 400, "invalid_request", "action must be start|cancel");
          return;
        }
        sendOk(req);
      });
  server_->addHandler(autotuneHandler);
```

- [ ] **Step 3: Consume them in `applyPending()`**

In `applyPending()`, add (after the `if (p.discEnable) ...` line):

```cpp
  if (p.pidGains) reactor_.setPidGains(p.pidKp, p.pidKi, p.pidKd);
  if (p.pidMode) reactor_.setPidMode(p.pidModeStr.c_str());
  if (p.autotuneStart) reactor_.startAutotune();
  if (p.autotuneCancel) reactor_.cancelAutotune();
```

- [ ] **Step 4: Add the `thermal.pid` block in `AppRuntime.cpp` `buildStatusJson()`**

In the `thermal` block, after the `safety` sub-object is fully built (after the `safety["processMaxC"] = ...;` line), add:

```cpp
  JsonObject pid = th["pid"].to<JsonObject>();
  pid["kp"] = g_thermal.kp();
  pid["ki"] = g_thermal.ki();
  pid["kd"] = g_thermal.kd();
  pid["p"] = g_thermal.pTerm();
  pid["i"] = g_thermal.iTerm();
  pid["d"] = g_thermal.dTerm();
  pid["out"] = roundf(g_thermal.outputDuty() * 1000) / 1000.0f;
  pid["mode"] = g_thermal.modeStr();
  JsonObject at = pid["autotune"].to<JsonObject>();
  at["active"] = g_thermal.autotuneActive();
  at["progress"] = g_thermal.autotuneProgress();
  const char* ares = g_thermal.autotuneResult();
  if (ares) at["result"] = ares;
  else at["result"] = nullptr;
```

- [ ] **Step 5: Compile + native tests**

Run: `/tmp/pio-venv/bin/pio run -e esp32-s3-devkitc-1` → Expected: `SUCCESS`.
Run: `/tmp/pio-venv/bin/pio test -e native` → Expected: 23 pass.

- [ ] **Step 6: Commit**

```bash
git add include/net/WebInterface.hpp src/net/WebInterface.cpp src/system/AppRuntime.cpp
git commit -m "feat(api): add /pid + /pid/autotune commands and thermal.pid telemetry"
```

---

### Task 5: Mock server — `thermal.pid` + `/pid` + autotune

**Files:**
- Modify: `tools/mock_server.py`

- [ ] **Step 1: Add PID state**

In the `state` dict, add (near the thermal fields):

```python
    "kp": 0.08, "ki": 0.0015, "kd": 0.4, "pidMode": "auto",
    "atActive": False, "atProgress": 0, "atResult": None, "atStartMs": 0.0,
```

- [ ] **Step 2: Emit `thermal.pid` in `status()`**

In `status()`, inside the returned `"thermal"` dict, add a `pid` key right after the `safety` sub-dict:

```python
            "pid": {
                "kp": state["kp"], "ki": state["ki"], "kd": state["kd"],
                "p": 0.0, "i": 0.0, "d": 0.0,
                "out": round(state["heaterPct"] / 100.0, 3),
                "mode": "autotune" if state["atActive"] else state["pidMode"],
                "autotune": {"active": state["atActive"],
                             "progress": state["atProgress"],
                             "result": state["atResult"]},
            },
```

- [ ] **Step 3: Advance the autotune progress in `simulate()`**

In `simulate()`, after the thermal update lines (inside the loop, applies whether running or not), add:

```python
        if state["atActive"]:
            state["atProgress"] = min(100, state["atProgress"] + 5)
            if state["atProgress"] >= 100:
                state["atActive"] = False
                state["atResult"] = "ok"
                state["kp"], state["ki"], state["kd"] = 0.12, 0.0021, 0.55  # "tuned" gains
                state["pidMode"] = "auto"
```

- [ ] **Step 4: Add `/api/v1/pid` and `/api/v1/pid/autotune` handlers**

Add these two handler functions (near `api_disc`):

```python
async def api_pid(req):
    b = await req.json()
    if "kp" in b and "ki" in b and "kd" in b:
        state["kp"], state["ki"], state["kd"] = float(b["kp"]), float(b["ki"]), float(b["kd"])
    if "mode" in b:
        state["pidMode"] = "manual" if b["mode"] == "manual" else "auto"
    return web.json_response({"ok": True})


async def api_autotune(req):
    b = await req.json()
    action = b.get("action")
    if action == "start":
        state.update(atActive=True, atProgress=0, atResult=None, atStartMs=time.monotonic())
        return web.json_response({"ok": True})
    if action == "cancel":
        state.update(atActive=False, atResult=None)
        return web.json_response({"ok": True})
    return web.json_response(
        {"ok": False, "error": {"code": "invalid_request",
                                "message": "action must be start|cancel"}}, status=400)
```

Register them in `main()` (next to the other routes):

```python
    app.router.add_post("/api/v1/pid", api_pid)
    app.router.add_post("/api/v1/pid/autotune", api_autotune)
```

- [ ] **Step 5: Verify**

```bash
/tmp/pio-venv/bin/python -c "import ast; ast.parse(open('tools/mock_server.py').read()); print('syntax ok')"
/tmp/pio-venv/bin/python tools/mock_server.py & MOCK=$!; sleep 2
curl -s -XPOST localhost:8000/api/v1/pid -H 'content-type: application/json' -d '{"kp":0.1,"ki":0.002,"kd":0.5,"mode":"manual"}'
echo
curl -s localhost:8000/api/v1/status | /tmp/pio-venv/bin/python -c \
  "import sys,json; d=json.load(sys.stdin); print('pid=',d['thermal']['pid'])"
curl -s -XPOST localhost:8000/api/v1/pid/autotune -H 'content-type: application/json' -d '{"action":"start"}'
echo
sleep 1
curl -s localhost:8000/api/v1/status | /tmp/pio-venv/bin/python -c \
  "import sys,json; d=json.load(sys.stdin); a=d['thermal']['pid']['autotune']; print('autotune=',a); assert a['active'] and a['progress']>0"
echo "bad action:"; curl -s -o /dev/null -w "%{http_code}\n" -XPOST localhost:8000/api/v1/pid/autotune -H 'content-type: application/json' -d '{"action":"x"}'
kill $MOCK 2>/dev/null
```

Expected: pid block shows `mode: 'manual'`, `kp: 0.1`; after autotune start, `autotune.active` is true with `progress > 0`; bad action → `400`.

- [ ] **Step 6: Commit**

```bash
git add tools/mock_server.py
git commit -m "feat(mock): simulate thermal.pid + /pid + autotune"
```

---

## Phase 3 done — definition of done

- `pio test -e native` passes (23: rpm + drv_status + alarm_tracker + pid_controller + relay_autotune).
- `pio run -e esp32-s3-devkitc-1` builds clean.
- `POST /api/v1/pid` sets gains (persisted) + mode; `POST /api/v1/pid/autotune` starts/cancels; `/status` `thermal.pid` carries gains, p/i/d terms, out, mode, and autotune {active, progress, result}.
- Gains survive reboot (NVS namespace `"pid"`).
- Mock mirrors all of the above (verified by curl).
- **On hardware (when available):** run a tune (autotune mode), confirm the heater relays around setpoint and that on completion `thermal.pid` shows new gains with `autotune.result: "ok"`, and the bath then holds setpoint in auto.

## Out of scope (later)
- `GET /pid` + `GET /pid/autotune` convenience endpoints (data is in `/status`).
- `suggested` gains preview during autotune; manual-mode operator duty entry; `busy` rejection.
- P4 (NTC calibration), SD/system endpoints, ConfigStore consolidation, UI phase.
