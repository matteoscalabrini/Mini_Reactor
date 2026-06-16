# Phase 4 — Heater-NTC Calibration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Calibrate the heater-mounted NTC safety probe (so the ~80 °C cutoff is accurate) via captured reference points fitted to an offset / Beta-refit / Steinhart-Hart model, persisted in NVS, with `/calibration*` endpoints and `thermal.safety.probe` telemetry.

**Architecture:** A pure, host-tested `ThermistorCalibration` unit holds the model + fitting math (1 point → offset on Beta, 2 → Beta refit, ≥3 → Steinhart-Hart). `Thermistor` owns one, routes `readCelsius()` through it, captures points (reference °C + live NTC resistance), and persists coefficients in NVS `"cal"`. Commands flow `WebInterface` → `Reactor` → `ThermalController` → `Thermistor`; `GET /calibration` is served from a loop-built cache; telemetry reads probe state into `thermal.safety.probe`.

**Tech Stack:** C++17 / Arduino-ESP32 / NVS Preferences, ArduinoJson v7, Unity (native tests), Python/aiohttp (mock).

**Spec:** `docs/superpowers/specs/2026-06-15-reactor-firmware-api-design.md` — Phase 4 / §10 P4, §4 `thermal.safety.probe`, §5 `/calibration`, §9 calibration. Builds on P1–P3 (on `main`). **Note:** the DS18B20 liquid probe is trusted at factory accuracy and is NOT calibrated — this is the heater NTC only.

---

## Baseline assumptions

- Start from `main` (P1–P3 merged). Confirm green before Task 1: `/tmp/pio-venv/bin/pio run -e esp32-s3-devkitc-1` (SUCCESS) and `/tmp/pio-venv/bin/pio test -e native` (23 pass).
- PlatformIO CLI: `/tmp/pio-venv/bin/pio` (not on PATH). Mock Python: `/tmp/pio-venv/bin/python` (aiohttp).
- Work on a branch: `git checkout -b feature/p4-ntc-calibration`.
- **SHARED-WORKTREE git rule for ALL subagents (implementers + reviewers):** never run `git checkout`/`switch`/`reset`/`stash` — only `git add`+`git commit` on the current branch and read-only `git show`/`diff`/`log`. (A prior phase's reviewer `git checkout` bounced the shared HEAD.)
- **Persistence (deviation from spec §7 ConfigStore):** calibration persists in a dedicated NVS namespace `"cal"` (consistent with `"pid"`/`"reactor"`/`"wifi"`) — the captured points are stored and re-fitted on boot, which faithfully restores any of the three models. ConfigStore consolidation stays deferred.
- **Async-queue limitation:** `POST /calibration/compute` acks `{ "ok": true }` immediately (handlers may not touch hardware/NVS — the actual fit runs next loop). The result (`calibrated`, `method`, or unchanged on 0 points) is observed via `GET /calibration` / `/status` `thermal.safety.probe`, NOT a synchronous body. So spec §5's synchronous `{method, coefficients}` return and `calibration_incomplete` 400 are surfaced as state, not an HTTP error. The mock matches this (acks, updates state).

## Scope

In scope: `ThermistorCalibration` model + fitting, `Thermistor` integration + NVS persistence, `/calibration` (GET) + `/calibration/{point,compute,reset}` (POST), `thermal.safety.probe` telemetry (adcRaw, resistanceOhms, calibrated, method), mock simulation.

**Out of scope (deferred):** persisting captured points across reboot (coefficients persist; points are a calibration-session artifact); auto-suggesting points; DS18B20 calibration (trusted); SD/system endpoints; ConfigStore; UI.

## File structure

| File | Responsibility | Action |
|---|---|---|
| `include/sensor/ThermistorCalibration.hpp` | Pure model + fit (offset/Beta/Steinhart) + `resistanceToCelsius`. No Arduino deps. | Create |
| `test/test_thermistor_calibration/test_main.cpp` | Unity tests for the fitting math. | Create |
| `include/sensor/Thermistor.hpp` / `.cpp` | Own a calibration; capture points; route conversion; NVS persist; raw/resistance getters. | Modify |
| `include/control/ThermalController.hpp` / `.cpp` | Delegate calibration ops + expose probe/cal state. | Modify |
| `include/control/Reactor.hpp` | Delegate calibration commands to ThermalController. | Modify |
| `include/net/WebInterface.hpp` / `src/net/WebInterface.cpp` | `/calibration` GET + POST routes + Pending + cal-JSON cache. | Modify |
| `src/system/AppRuntime.cpp` | `thermal.safety.probe` telemetry + build the cal-JSON cache each loop. | Modify |
| `tools/mock_server.py` | Simulate probe + `/calibration*`. | Modify |

> **Embedded testing note:** `ThermistorCalibration` is host-unit-tested (real TDD). The `Thermistor`/controller/endpoint wiring is verified by compile + the mock's curl checks.

---

### Task 1: ThermistorCalibration (pure, TDD)

**Files:**
- Create: `include/sensor/ThermistorCalibration.hpp`
- Create: `test/test_thermistor_calibration/test_main.cpp`

- [ ] **Step 1: Write the failing test**

Create `test/test_thermistor_calibration/test_main.cpp`:

```cpp
#include <unity.h>
#include <cmath>
#include "sensor/ThermistorCalibration.hpp"

// Factory 10k NTC, Beta 3950, T0 25C.
static ThermistorCalibration::BetaParams kBeta{10000.0f, 3950.0f, 25.0f};

void setUp() {}
void tearDown() {}

// Beta model: R == R0 at T0 → 25 C.
void test_beta_default() {
  ThermistorCalibration c; c.begin(kBeta);
  TEST_ASSERT_FALSE(c.calibrated());
  TEST_ASSERT_EQUAL_FLOAT(25.0f, c.resistanceToCelsius(10000.0f));
}

// 1 point → constant offset on the Beta curve.
void test_one_point_offset() {
  ThermistorCalibration c; c.begin(kBeta);
  const float R = 12000.0f;
  const float betaC = c.resistanceToCelsius(R);   // uncalibrated reading at R
  c.addPoint(betaC + 2.0f, R);                     // operator says it's really +2C
  TEST_ASSERT_TRUE(c.compute());
  TEST_ASSERT_TRUE(c.calibrated());
  TEST_ASSERT_EQUAL_INT((int)ThermistorCalibration::Method::Offset, (int)c.method());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, betaC + 2.0f, c.resistanceToCelsius(R));
}

// 2 points → Beta refit that reproduces both points.
void test_two_point_beta_refit() {
  ThermistorCalibration c; c.begin(kBeta);
  // Generate two synthetic points from a KNOWN beta=4000,R0=10000,T0=25.
  auto rAt = [](float tC){ float t0=298.15f,T=tC+273.15f; return 10000.0f*expf(4000.0f*(1.0f/T-1.0f/t0)); };
  c.addPoint(10.0f, rAt(10.0f));
  c.addPoint(60.0f, rAt(60.0f));
  TEST_ASSERT_TRUE(c.compute());
  TEST_ASSERT_EQUAL_INT((int)ThermistorCalibration::Method::Beta, (int)c.method());
  TEST_ASSERT_TRUE(c.calibrated());
  TEST_ASSERT_FLOAT_WITHIN(0.2f, 10.0f, c.resistanceToCelsius(rAt(10.0f)));
  TEST_ASSERT_FLOAT_WITHIN(0.2f, 60.0f, c.resistanceToCelsius(rAt(60.0f)));
}

// 3 points → Steinhart-Hart that reproduces all three.
void test_three_point_steinhart() {
  ThermistorCalibration c; c.begin(kBeta);
  // Known coefficients (typical 10k NTC).
  const float A=1.129241e-3f, B=2.341077e-4f, Cc=8.775468e-8f;
  auto tAt = [&](float R){ float L=logf(R); float invT=A+B*L+Cc*L*L*L; return 1.0f/invT-273.15f; };
  float Rs[3] = {20000.0f, 10000.0f, 4000.0f};
  for (int i=0;i<3;i++) c.addPoint(tAt(Rs[i]), Rs[i]);
  TEST_ASSERT_TRUE(c.compute());
  TEST_ASSERT_EQUAL_INT((int)ThermistorCalibration::Method::Steinhart, (int)c.method());
  for (int i=0;i<3;i++) TEST_ASSERT_FLOAT_WITHIN(0.1f, tAt(Rs[i]), c.resistanceToCelsius(Rs[i]));
}

void test_compute_zero_points_fails() {
  ThermistorCalibration c; c.begin(kBeta);
  TEST_ASSERT_FALSE(c.compute());
}

void test_reset_back_to_beta() {
  ThermistorCalibration c; c.begin(kBeta);
  c.addPoint(c.resistanceToCelsius(12000.0f) + 5.0f, 12000.0f);
  c.compute();
  c.reset();
  TEST_ASSERT_FALSE(c.calibrated());
  TEST_ASSERT_EQUAL_INT((int)ThermistorCalibration::Method::Beta, (int)c.method());
  TEST_ASSERT_EQUAL_INT(0, c.pointCount());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_beta_default);
  RUN_TEST(test_one_point_offset);
  RUN_TEST(test_two_point_beta_refit);
  RUN_TEST(test_three_point_steinhart);
  RUN_TEST(test_compute_zero_points_fails);
  RUN_TEST(test_reset_back_to_beta);
  return UNITY_END();
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `/tmp/pio-venv/bin/pio test -e native -f test_thermistor_calibration`
Expected: FAIL/ERROR — `sensor/ThermistorCalibration.hpp: No such file or directory`.

- [ ] **Step 3: Create the implementation**

Create `include/sensor/ThermistorCalibration.hpp`:

```cpp
/*
 * ThermistorCalibration.hpp — pure NTC calibration model + fitting.
 *
 * Header-only, dependency-free (host-testable). Converts a measured NTC
 * resistance to °C via one of three models, fitted from captured points:
 *   1 point  → constant offset on the factory Beta curve
 *   2 points → Beta refit (recompute R0, Beta)
 *   >=3      → Steinhart-Hart  1/T = A + B*ln(R) + C*ln(R)^3   (uses first/mid/last)
 * Resistances in ohms, temperatures in °C (converted to Kelvin internally).
 */
#pragma once

#include <cmath>

class ThermistorCalibration {
 public:
  enum class Method { Beta, Offset, Steinhart };
  struct BetaParams { float r0Ohms; float beta; float t0C; };
  static constexpr int kMaxPoints = 6;

  void begin(const BetaParams& beta) {
    factory_ = beta;
    r0_ = beta.r0Ohms; beta_ = beta.beta; t0C_ = beta.t0C;
    method_ = Method::Beta; offsetC_ = 0.0f;
    a_ = b_ = c_ = 0.0f; nPoints_ = 0; calibrated_ = false;
  }

  bool addPoint(float referenceC, float resistanceOhms) {
    if (nPoints_ >= kMaxPoints) return false;
    points_[nPoints_].refC = referenceC;
    points_[nPoints_].rOhms = resistanceOhms;
    ++nPoints_;
    return true;
  }
  int pointCount() const { return nPoints_; }
  void clearPoints() { nPoints_ = 0; }
  float pointRefC(int i) const { return points_[i].refC; }
  float pointR(int i) const { return points_[i].rOhms; }

  bool compute() {
    if (nPoints_ == 1) {
      offsetC_ = points_[0].refC - betaCelsius(points_[0].rOhms);
      method_ = Method::Offset; calibrated_ = true; return true;
    }
    if (nPoints_ == 2) {
      const float t1 = points_[0].refC + 273.15f, t2 = points_[1].refC + 273.15f;
      const float denom = (1.0f / t1 - 1.0f / t2);
      if (std::fabs(denom) < 1e-9f) return false;
      beta_ = std::log(points_[0].rOhms / points_[1].rOhms) / denom;
      const float t0K = t0C_ + 273.15f;
      r0_ = points_[0].rOhms / std::exp(beta_ * (1.0f / t1 - 1.0f / t0K));
      method_ = Method::Beta; calibrated_ = true; return true;
    }
    if (nPoints_ >= 3) {
      const Point& p0 = points_[0];
      const Point& p1 = points_[nPoints_ / 2];
      const Point& p2 = points_[nPoints_ - 1];
      if (!solveSteinhart(p0, p1, p2)) return false;
      method_ = Method::Steinhart; calibrated_ = true; return true;
    }
    return false;  // 0 points
  }

  void reset() { begin(factory_); }

  float resistanceToCelsius(float R) const {
    switch (method_) {
      case Method::Offset: return betaCelsius(R) + offsetC_;
      case Method::Steinhart: {
        const float L = std::log(R);
        const float invT = a_ + b_ * L + c_ * L * L * L;
        return 1.0f / invT - 273.15f;
      }
      default: return betaCelsius(R);
    }
  }

  Method method() const { return method_; }
  bool calibrated() const { return calibrated_; }
  const char* methodStr() const {
    switch (method_) {
      case Method::Offset: return "offset";
      case Method::Steinhart: return "steinhart";
      default: return "beta";
    }
  }

 private:
  struct Point { float refC; float rOhms; };

  float betaCelsius(float R) const {
    const float t0K = t0C_ + 273.15f;
    const float invT = 1.0f / t0K + (1.0f / beta_) * std::log(R / r0_);
    return 1.0f / invT - 273.15f;
  }

  // Solve [[1,L1,L1^3],[1,L2,L2^3],[1,L3,L3^3]] [a,b,c]^T = [1/T1,1/T2,1/T3]^T (Cramer).
  bool solveSteinhart(const Point& p0, const Point& p1, const Point& p2) {
    const float L1 = std::log(p0.rOhms), L2 = std::log(p1.rOhms), L3 = std::log(p2.rOhms);
    const float Y1 = 1.0f / (p0.refC + 273.15f);
    const float Y2 = 1.0f / (p1.refC + 273.15f);
    const float Y3 = 1.0f / (p2.refC + 273.15f);
    const float c1 = L1 * L1 * L1, c2 = L2 * L2 * L2, c3 = L3 * L3 * L3;
    const float det = det3(1, L1, c1, 1, L2, c2, 1, L3, c3);
    if (std::fabs(det) < 1e-30f) return false;
    a_ = det3(Y1, L1, c1, Y2, L2, c2, Y3, L3, c3) / det;
    b_ = det3(1, Y1, c1, 1, Y2, c2, 1, Y3, c3) / det;
    c_ = det3(1, L1, Y1, 1, L2, Y2, 1, L3, Y3) / det;
    return true;
  }
  static float det3(float a, float b, float c, float d, float e, float f,
                    float g, float h, float i) {
    return a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
  }

  BetaParams factory_{10000.0f, 3950.0f, 25.0f};
  Method method_ = Method::Beta;
  float r0_ = 10000.0f, beta_ = 3950.0f, t0C_ = 25.0f;
  float offsetC_ = 0.0f, a_ = 0.0f, b_ = 0.0f, c_ = 0.0f;
  bool calibrated_ = false;
  Point points_[kMaxPoints];
  int nPoints_ = 0;
};
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `/tmp/pio-venv/bin/pio test -e native -f test_thermistor_calibration`
Expected: PASS — `6 Tests 0 Failures 0 Ignored ... OK`.

- [ ] **Step 5: Commit**

```bash
git add include/sensor/ThermistorCalibration.hpp test/test_thermistor_calibration/test_main.cpp
git commit -m "feat(sensor): add host-tested ThermistorCalibration (offset/Beta/Steinhart)"
```

---

### Task 2: Thermistor integration + NVS persistence

**Files:**
- Modify: `include/sensor/Thermistor.hpp`
- Modify: `src/sensor/Thermistor.cpp`

- [ ] **Step 1: Update `include/sensor/Thermistor.hpp`**

Add includes after `#include <Arduino.h>`:

```cpp
#include <Preferences.h>

#include "sensor/ThermistorCalibration.hpp"
```

In `struct Config`, add (after `int samples = 8;`):

```cpp
    const char* prefsNamespace = "cal";
```

Replace the public method block (the existing `readCelsius()` / `readMilliVolts()` declarations) with:

```cpp
  /* readCelsius() — Sampled temperature in °C via the active calibration. NAN on
   * open/short (no probe). */
  float readCelsius();

  /* readMilliVolts() — Raw averaged ADC voltage at the divider node. */
  uint32_t readMilliVolts();

  /* readRawAdc() — Averaged raw 12-bit ADC count (0..4095). */
  uint16_t readRawAdc();

  /* readResistanceOhms() — Computed NTC resistance, or NAN on open/short. */
  float readResistanceOhms();

  /* Calibration ops (return false if the live resistance is faulty / incomplete). */
  bool addCalibrationPoint(float referenceC);  // captures live resistance
  bool computeCalibration();                    // fits + persists on success
  void resetCalibration();                      // factory Beta + persists

  /* Calibration state (for telemetry / GET). */
  const ThermistorCalibration& calibration() const { return cal_; }
```

In `private:`, add (after `Config cfg_;`):

```cpp
  ThermistorCalibration cal_;
  Preferences prefs_;
  void loadCalibration();
  void persistCalibration();
```

- [ ] **Step 2: Update `src/sensor/Thermistor.cpp`**

Replace the file contents with:

```cpp
/*
 * Thermistor.cpp — NTC sensor (ADC + calibration model).
 * See include/sensor/Thermistor.hpp and AppConfig::Thermal.
 */

#include "sensor/Thermistor.hpp"

#include <math.h>

Thermistor::Thermistor(const Config& config) : cfg_(config) {}

void Thermistor::begin() {
  analogReadResolution(12);
  analogSetPinAttenuation(cfg_.adcPin, ADC_11db);  // full ~0..3.3V range
  cal_.begin({cfg_.r0Ohms, cfg_.beta, cfg_.t0C});
  loadCalibration();
}

uint32_t Thermistor::readMilliVolts() {
  uint32_t sum = 0;
  for (int i = 0; i < cfg_.samples; i++) sum += analogReadMilliVolts(cfg_.adcPin);
  return sum / cfg_.samples;
}

uint16_t Thermistor::readRawAdc() {
  uint32_t sum = 0;
  for (int i = 0; i < cfg_.samples; i++) sum += analogRead(cfg_.adcPin);
  return static_cast<uint16_t>(sum / cfg_.samples);
}

float Thermistor::readResistanceOhms() {
  const float vAdc = static_cast<float>(readMilliVolts());
  if (vAdc <= 1.0f || vAdc >= cfg_.vSupplyMv - 1.0f) return NAN;  // open/short
  return cfg_.seriesOhms * vAdc / (cfg_.vSupplyMv - vAdc);
}

float Thermistor::readCelsius() {
  const float r = readResistanceOhms();
  if (isnan(r)) return NAN;
  return cal_.resistanceToCelsius(r);
}

bool Thermistor::addCalibrationPoint(float referenceC) {
  const float r = readResistanceOhms();
  if (isnan(r)) return false;
  return cal_.addPoint(referenceC, r);
}

bool Thermistor::computeCalibration() {
  if (!cal_.compute()) return false;
  persistCalibration();
  return true;
}

void Thermistor::resetCalibration() {
  cal_.reset();
  persistCalibration();
}

void Thermistor::loadCalibration() {
  prefs_.begin(cfg_.prefsNamespace, false);
  if (!prefs_.getBool("cal", false)) return;  // no stored user calibration
  // Restore by re-loading the captured points and re-fitting — reproduces the exact
  // offset/Beta/Steinhart model without serializing each coefficient set.
  const int n = prefs_.getInt("npts", 0);
  for (int i = 0; i < n; i++) {
    char kr[8], kt[8];
    snprintf(kr, sizeof(kr), "r%d", i);
    snprintf(kt, sizeof(kt), "t%d", i);
    cal_.addPoint(prefs_.getFloat(kt, 0.0f), prefs_.getFloat(kr, 0.0f));
  }
  if (n > 0) cal_.compute();
}

void Thermistor::persistCalibration() {
  const ThermistorCalibration& c = cal_;
  prefs_.putBool("cal", c.calibrated());
  prefs_.putInt("npts", c.pointCount());
  for (int i = 0; i < c.pointCount(); i++) {
    char kr[8], kt[8];
    snprintf(kr, sizeof(kr), "r%d", i);
    snprintf(kt, sizeof(kt), "t%d", i);
    prefs_.putFloat(kr, c.pointR(i));
    prefs_.putFloat(kt, c.pointRefC(i));
  }
}
```

> Persistence approach: store the captured points + recompute on boot — this faithfully restores any of the three models without serializing each coefficient set. (Points thus survive reboot too, a small bonus over the plan's "session-only" note.)

- [ ] **Step 3: Compile**

Run: `/tmp/pio-venv/bin/pio run -e esp32-s3-devkitc-1`
Expected: `SUCCESS`.

- [ ] **Step 4: Commit**

```bash
git add include/sensor/Thermistor.hpp src/sensor/Thermistor.cpp
git commit -m "feat(sensor): route Thermistor through calibration + NVS persist"
```

---

### Task 3: ThermalController + Reactor delegation

**Files:**
- Modify: `include/control/ThermalController.hpp`
- Modify: `src/control/ThermalController.cpp`
- Modify: `include/control/Reactor.hpp`

- [ ] **Step 1: `ThermalController.hpp` — expose NTC calibration**

In the `public:` section (after the autotune telemetry getters), add:

```cpp
  /* Heater-NTC calibration (delegates to the safety thermistor). */
  bool addCalibrationPoint(float referenceC) { return ntc_.addCalibrationPoint(referenceC); }
  bool computeCalibration() { return ntc_.computeCalibration(); }
  void resetCalibration() { ntc_.resetCalibration(); }
  uint16_t ntcRawAdc() { return ntc_.readRawAdc(); }
  float ntcResistanceOhms() { return ntc_.readResistanceOhms(); }
  const ThermistorCalibration& ntcCalibration() const { return ntc_.calibration(); }
```

(`ntc_` is the existing `Thermistor&` member. `Thermistor.hpp` is already included by `ThermalController.hpp`.)

- [ ] **Step 2: `ThermalController.cpp` — no change needed**

The methods above are inline in the header. No `.cpp` edit. (This step is a no-op checkpoint.)

- [ ] **Step 3: `Reactor.hpp` — delegate calibration commands**

In the `public:` section (after the PID delegates), add:

```cpp
  /* Heater-NTC calibration delegates. */
  bool addCalibrationPoint(float referenceC) { return thermal_.addCalibrationPoint(referenceC); }
  bool computeCalibration() { return thermal_.computeCalibration(); }
  void resetCalibration() { thermal_.resetCalibration(); }
```

- [ ] **Step 4: Compile**

Run: `/tmp/pio-venv/bin/pio run -e esp32-s3-devkitc-1`
Expected: `SUCCESS`.

- [ ] **Step 5: Commit**

```bash
git add include/control/ThermalController.hpp include/control/Reactor.hpp
git commit -m "feat(control): delegate heater-NTC calibration through Reactor/ThermalController"
```

---

### Task 4: Endpoints + telemetry + cal-JSON cache

**Files:**
- Modify: `include/net/WebInterface.hpp`
- Modify: `src/net/WebInterface.cpp`
- Modify: `src/system/AppRuntime.cpp`

- [ ] **Step 1: `WebInterface.hpp` — Pending fields, cal cache, GET accessor**

In `struct Pending`, add (before `bool wifiConnect = false;`):

```cpp
    bool calPoint = false;   float calRefC = 0;
    bool calCompute = false;
    bool calReset = false;
```

In the `public:` section (after `void update(...)`), add:

```cpp
  /* Cache the calibration JSON (built in the loop) for GET /calibration. */
  void cacheCalJson(const String& calJson);
```

In `private:`, add a cache field (next to `String scanJson_`):

```cpp
  String calJson_ = "{}";
```

- [ ] **Step 2: `WebInterface.cpp` — routes + applyPending + cache setter**

Add the `cacheCalJson` definition near `update()`:

```cpp
void WebInterface::cacheCalJson(const String& calJson) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  calJson_ = calJson;
  xSemaphoreGive(mutex_);
}
```

In `registerRoutes()`, add after the `/api/v1/pid/autotune` handler:

```cpp
  // ── GET calibration ──
  server_->on("/api/v1/calibration", HTTP_GET, [this](AsyncWebServerRequest* req) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const String body = calJson_;
    xSemaphoreGive(mutex_);
    sendJson(req, body);
  });

  // ── POST calibration/point ──
  auto* calPointHandler = new AsyncCallbackJsonWebHandler(
      "/api/v1/calibration/point", [this](AsyncWebServerRequest* req, JsonVariant& json) {
        JsonObject o = json.as<JsonObject>();
        if (o["referenceC"].isNull()) {
          sendError(req, 400, "invalid_request", "referenceC required");
          return;
        }
        xSemaphoreTake(mutex_, portMAX_DELAY);
        pending_.calPoint = true;
        pending_.calRefC = o["referenceC"].as<float>();
        xSemaphoreGive(mutex_);
        sendOk(req);
      });
  server_->addHandler(calPointHandler);

  // ── POST calibration/compute ──
  server_->on("/api/v1/calibration/compute", HTTP_POST, [this](AsyncWebServerRequest* req) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    pending_.calCompute = true;
    xSemaphoreGive(mutex_);
    sendOk(req);
  });

  // ── POST calibration/reset ──
  server_->on("/api/v1/calibration/reset", HTTP_POST, [this](AsyncWebServerRequest* req) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    pending_.calReset = true;
    xSemaphoreGive(mutex_);
    sendOk(req);
  });
```

In `applyPending()`, add (after the autotune lines):

```cpp
  if (p.calPoint) reactor_.addCalibrationPoint(p.calRefC);
  if (p.calCompute) reactor_.computeCalibration();
  if (p.calReset) reactor_.resetCalibration();
```

- [ ] **Step 3: `AppRuntime.cpp` — `thermal.safety.probe` telemetry + cal cache**

First add `#include "sensor/ThermistorCalibration.hpp"` if not already pulled in transitively (it is, via ThermalController → Thermistor; this include is belt-and-suspenders and harmless).

In `buildStatusJson()`, in the `safety` sub-object (after `safety["processMaxC"] = ...;`), add the probe block:

```cpp
  JsonObject probe = safety["probe"].to<JsonObject>();
  const ThermistorCalibration& ncal = g_thermal.ntcCalibration();
  probe["adcRaw"] = g_thermal.ntcRawAdc();
  const float nres = g_thermal.ntcResistanceOhms();
  if (isnan(nres)) probe["resistanceOhms"] = nullptr;
  else probe["resistanceOhms"] = roundf(nres);
  probe["calibrated"] = ncal.calibrated();
  probe["method"] = ncal.methodStr();
```

Add a helper to build the calibration JSON (place it next to `buildStatusJson()` in the anonymous namespace):

```cpp
String buildCalJson() {
  JsonDocument doc;
  const ThermistorCalibration& c = g_thermal.ntcCalibration();
  doc["method"] = c.methodStr();
  doc["calibrated"] = c.calibrated();
  JsonArray pts = doc["points"].to<JsonArray>();
  for (int i = 0; i < c.pointCount(); i++) {
    JsonObject p = pts.add<JsonObject>();
    p["referenceC"] = c.pointRefC(i);
    p["resistanceOhms"] = roundf(c.pointR(i));
  }
  String out; serializeJson(doc, out); return out;
}
```

In `tick()`, where `statusJson`/`scanJson` are rebuilt (~10 Hz), also refresh + push the cal cache. Find the block that sets `statusJson = buildStatusJson();` and add right after it:

```cpp
    g_web.cacheCalJson(buildCalJson());
```

- [ ] **Step 4: Compile + native tests**

Run: `/tmp/pio-venv/bin/pio run -e esp32-s3-devkitc-1` → Expected: `SUCCESS`.
Run: `/tmp/pio-venv/bin/pio test -e native` → Expected: all pass (now 29: prior 23 + 6 calibration).

- [ ] **Step 5: Commit**

```bash
git add include/net/WebInterface.hpp src/net/WebInterface.cpp src/system/AppRuntime.cpp
git commit -m "feat(api): add /calibration endpoints + thermal.safety.probe telemetry"
```

---

### Task 5: Mock server — probe + `/calibration*`

**Files:**
- Modify: `tools/mock_server.py`

- [ ] **Step 1: Add calibration state**

In the `state` dict, add (near the thermal fields):

```python
    "calMethod": "beta", "calibrated": False, "calPoints": [], "ntcAdc": 1820, "ntcR": 9120.0,
```

- [ ] **Step 2: Emit `thermal.safety.probe` in `status()`**

In `status()`, inside the `"safety"` sub-dict (after `"processMaxC": 55.0`), add a `probe` key:

```python
                       "processMaxC": 55.0,
                       "probe": {"adcRaw": state["ntcAdc"],
                                 "resistanceOhms": round(state["ntcR"]),
                                 "calibrated": state["calibrated"],
                                 "method": state["calMethod"]}},
```

(Adjust the existing `"processMaxC": 55.0}` line so the dict stays valid — `processMaxC` now has a trailing comma and `probe` follows before the closing brace.)

- [ ] **Step 3: Add `/calibration` handlers**

Add these near `api_pid`:

```python
async def api_calibration(req):
    return web.json_response({"method": state["calMethod"], "calibrated": state["calibrated"],
                              "points": state["calPoints"]})


async def api_cal_point(req):
    b = await req.json()
    if "referenceC" not in b:
        return web.json_response(
            {"ok": False, "error": {"code": "invalid_request",
                                    "message": "referenceC required"}}, status=400)
    state["calPoints"].append({"referenceC": float(b["referenceC"]),
                               "resistanceOhms": round(state["ntcR"])})
    return web.json_response({"ok": True})


async def api_cal_compute(req):
    # Mirrors the firmware's async ack: always {"ok":true}; 0 points → no change
    # (calibrated stays False), observed via GET /calibration.
    n = len(state["calPoints"])
    if n >= 1:
        state["calMethod"] = "offset" if n == 1 else ("beta" if n == 2 else "steinhart")
        state["calibrated"] = True
    return web.json_response({"ok": True})


async def api_cal_reset(req):
    state.update(calMethod="beta", calibrated=False, calPoints=[])
    return web.json_response({"ok": True})
```

Register in `main()`:

```python
    app.router.add_get("/api/v1/calibration", api_calibration)
    app.router.add_post("/api/v1/calibration/point", api_cal_point)
    app.router.add_post("/api/v1/calibration/compute", api_cal_compute)
    app.router.add_post("/api/v1/calibration/reset", api_cal_reset)
```

- [ ] **Step 4: Verify**

```bash
/tmp/pio-venv/bin/python -c "import ast; ast.parse(open('tools/mock_server.py').read()); print('syntax ok')"
/tmp/pio-venv/bin/python tools/mock_server.py & MOCK=$!; sleep 2
curl -s localhost:8000/api/v1/status | /tmp/pio-venv/bin/python -c \
  "import sys,json; print('probe=', json.load(sys.stdin)['thermal']['safety']['probe'])"
curl -s -XPOST localhost:8000/api/v1/calibration/point -H 'content-type: application/json' -d '{"referenceC":36.0}'
echo
curl -s -XPOST localhost:8000/api/v1/calibration/compute -H 'content-type: application/json' -d '{}'
echo
curl -s localhost:8000/api/v1/calibration | /tmp/pio-venv/bin/python -c \
  "import sys,json; d=json.load(sys.stdin); print('cal=',d); assert d['calibrated'] and d['method']=='offset'"
echo "after reset + compute (0 points), calibrated stays false:"
curl -s -XPOST localhost:8000/api/v1/calibration/reset -d '{}' >/dev/null
curl -s -XPOST localhost:8000/api/v1/calibration/compute -H 'content-type: application/json' -d '{}' >/dev/null
curl -s localhost:8000/api/v1/calibration | /tmp/pio-venv/bin/python -c \
  "import sys,json; d=json.load(sys.stdin); print('after reset:',d); assert not d['calibrated'] and d['method']=='beta'"
kill $MOCK 2>/dev/null
```

Expected: probe shows adcRaw/resistanceOhms/calibrated/method; after one point + compute, `cal` shows `calibrated:true, method:"offset"`; after reset+compute (0 points) `calibrated:false, method:"beta"`. A missing `referenceC` on `/point` returns `400 invalid_request` (synchronous request-shape check).

- [ ] **Step 5: Commit**

```bash
git add tools/mock_server.py
git commit -m "feat(mock): simulate thermal.safety.probe + /calibration"
```

---

## Phase 4 done — definition of done

- `pio test -e native` passes (29: prior 23 + 6 calibration).
- `pio run -e esp32-s3-devkitc-1` builds clean.
- `POST /calibration/point` captures (referenceC + live NTC R); `compute` fits offset/Beta/Steinhart by point count + persists; `reset` reverts to factory Beta. `GET /calibration` lists method + calibrated + points. `/status` `thermal.safety.probe` carries adcRaw, resistanceOhms, calibrated, method.
- Calibration survives reboot (NVS `"cal"`).
- Mock mirrors all of the above (verified by curl).
- **On hardware (when available):** capture 2–3 points against a reference thermometer at the heater, compute, and confirm the NTC reading tracks the reference and the ~80 °C safety cutoff trips at the corrected temperature.

## Out of scope (later)
- Raw fitted coefficients in `GET /calibration` (spec §5 shows `coefficients`): the resolved model is applied internally; `method` + `calibrated` + `points` convey the calibration. Exposing A/B/C / Beta is a thin add-on.
- DS18B20 liquid calibration (trusted at factory accuracy).
- SD/system endpoints; ConfigStore consolidation; the UI phase (the firmware `/api/v1` contract is COMPLETE after this phase).
