# Heater-NTC Disconnect / Fault Safeguard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make any disconnected or grossly-faulted heater NTC read as a *fault* (→ `NaN` → heater forced off / Start refused), never a plausible-but-wrong number, via a calibration-independent resistance plausibility band plus a pre-flight Start block.

**Architecture:** A pure, host-tested resistance band lives in `ThermistorCalibration` (derived from the factory Beta over a configurable 0…110 °C window ≈ 529 Ω…33.6 kΩ). `Thermistor::readResistanceOhms()` returns `NaN` for out-of-band readings at the single chokepoint, so both the safety read and calibration-point capture inherit it. `ThermalController::enable()` becomes `bool` and refuses to start when the probe is faulted/over-limit; `Reactor::start()` aborts on refusal. Telemetry/alarms (`resistanceOhms`/`heaterTempC` null, `heater_probe_fault` warn) already follow from `NaN`. The mock mirrors all of it.

**Tech Stack:** C++17 / Arduino-ESP32 / NVS Preferences, Unity (native tests), Python/aiohttp (mock).

**Spec:** `docs/superpowers/specs/2026-06-16-ntc-disconnect-safeguard-design.md`.

---

## Baseline assumptions

- Branch already created and checked out: `feature/ntc-disconnect-safeguard` (off `main`, which has P4 merged). The design doc is already committed here (`0ab0b0a`).
- PlatformIO CLI: `/tmp/pio-venv/bin/pio` (not on PATH). Mock Python: `/tmp/pio-venv/bin/python` (aiohttp).
- Baseline is green: `/tmp/pio-venv/bin/pio run -e esp32-s3-devkitc-1` → SUCCESS; `/tmp/pio-venv/bin/pio test -e native` → 29 pass (6 of them in `test_thermistor_calibration`).
- **SHARED-WORKTREE git rule for ALL subagents (implementers + reviewers):** never run `git checkout`/`switch`/`reset`/`stash` — only `git add` + `git commit` on the current branch and read-only `git show`/`diff`/`log`.

## Scope

In scope: resistance plausibility band in `ThermistorCalibration` (host-tested); band guard + config in `Thermistor`; pre-flight Start block in `ThermalController`/`Reactor`; config constants; mock simulation of a disconnected probe.

**Out of scope (documented in the spec):** cross-validation against the DS18B20 liquid probe to catch *subtle* in-band wrong readings (the band only catches gross faults/disconnects); the DS18B20 is trusted and not connected yet.

## File structure

| File | Responsibility | Action |
|---|---|---|
| `include/sensor/ThermistorCalibration.hpp` | Add calibration-independent plausible-resistance band (factory-Beta-based). | Modify |
| `test/test_thermistor_calibration/test_main.cpp` | Host tests for the band. | Modify |
| `include/sensor/Thermistor.hpp` | `Config.minPlausibleC/maxPlausibleC`. | Modify |
| `src/sensor/Thermistor.cpp` | Pass window into `cal_.begin(...)`; band guard in `readResistanceOhms()`. | Modify |
| `include/app_config.hpp` | `kNtcMinPlausibleC`/`kNtcMaxPlausibleC`. | Modify |
| `src/system/AppRuntime.cpp` | Wire the window into the Thermistor `Config`. | Modify |
| `include/control/ThermalController.hpp` / `.cpp` | `enable()` → `bool` with pre-flight check. | Modify |
| `src/control/Reactor.cpp` | `start()` aborts (stays idle) on refused enable. | Modify |
| `tools/mock_server.py` | Simulate a disconnected/faulted probe. | Modify |

---

### Task 1: Plausibility band in ThermistorCalibration (pure, TDD)

**Files:**
- Modify: `test/test_thermistor_calibration/test_main.cpp`
- Modify: `include/sensor/ThermistorCalibration.hpp`

- [ ] **Step 1: Write the failing tests**

In `test/test_thermistor_calibration/test_main.cpp`, add these two test functions immediately **before** `int main(int, char**) {`:

```cpp
// Plausible-resistance band over the real 0..110C window (factory 10k/3950/25):
// R(0C) ~= 33618 ohm, R(110C) ~= 529 ohm. Out-of-band -> implausible (probe fault).
void test_plausible_band_window() {
  ThermistorCalibration c; c.begin(kBeta, 0.0f, 110.0f);
  TEST_ASSERT_TRUE(c.plausibleResistance(10000.0f));   // 25C nominal, in band
  TEST_ASSERT_TRUE(c.plausibleResistance(33000.0f));   // just inside cold edge
  TEST_ASSERT_TRUE(c.plausibleResistance(600.0f));     // just inside hot edge
  TEST_ASSERT_FALSE(c.plausibleResistance(240.0f));    // ~140C disconnect: too low
  TEST_ASSERT_FALSE(c.plausibleResistance(50000.0f));  // ~-7C: too high
  TEST_ASSERT_FALSE(c.plausibleResistance(500.0f));    // below R(110C): too low
  TEST_ASSERT_FALSE(c.plausibleResistance(34000.0f));  // above R(0C): too high
}

// The band survives a reset (it is a fixed probe property, not a user calibration).
void test_plausible_band_survives_reset() {
  ThermistorCalibration c; c.begin(kBeta, 0.0f, 110.0f);
  c.addPoint(36.0f, 9000.0f); c.compute();  // a user calibration on top
  c.reset();
  TEST_ASSERT_FALSE(c.plausibleResistance(240.0f));    // still rejects out-of-band
  TEST_ASSERT_TRUE(c.plausibleResistance(10000.0f));
}
```

Register them in `main()` — add these two lines after the existing `RUN_TEST(test_reset_back_to_beta);` line:

```cpp
  RUN_TEST(test_plausible_band_window);
  RUN_TEST(test_plausible_band_survives_reset);
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `/tmp/pio-venv/bin/pio test -e native -f test_thermistor_calibration`
Expected: COMPILE ERROR — `'class ThermistorCalibration' has no member named 'plausibleResistance'` (and `begin` called with 3 args).

- [ ] **Step 3: Implement the band in `include/sensor/ThermistorCalibration.hpp`**

Replace the existing `begin(...)` method (lines 21–26):

```cpp
  void begin(const BetaParams& beta) {
    factory_ = beta;
    r0_ = beta.r0Ohms; beta_ = beta.beta; t0C_ = beta.t0C;
    method_ = Method::Beta; offsetC_ = 0.0f;
    a_ = b_ = c_ = 0.0f; nPoints_ = 0; calibrated_ = false;
  }
```

with (adds the plausible window with wide defaults so existing tests stay permissive):

```cpp
  void begin(const BetaParams& beta,
             float minPlausibleC = -50.0f, float maxPlausibleC = 200.0f) {
    factory_ = beta;
    r0_ = beta.r0Ohms; beta_ = beta.beta; t0C_ = beta.t0C;
    method_ = Method::Beta; offsetC_ = 0.0f;
    a_ = b_ = c_ = 0.0f; nPoints_ = 0; calibrated_ = false;
    // Plausible-resistance band, calibration-INDEPENDENT (always factory Beta).
    // Cold -> high R, hot -> low R: the min-temp edge gives the max resistance.
    minPlausibleC_ = minPlausibleC; maxPlausibleC_ = maxPlausibleC;
    rMaxPlausible_ = factoryResistanceAt(minPlausibleC_);
    rMinPlausible_ = factoryResistanceAt(maxPlausibleC_);
  }
```

Replace the existing `reset()` (line 64):

```cpp
  void reset() { begin(factory_); }
```

with (preserves the plausible window across reset):

```cpp
  void reset() { begin(factory_, minPlausibleC_, maxPlausibleC_); }
```

Add the public query method immediately **after** `reset()`:

```cpp
  // True when R is within the factory-Beta plausible band; false => probe fault.
  bool plausibleResistance(float r) const {
    return r >= rMinPlausible_ && r <= rMaxPlausible_;
  }
```

Add the private helper immediately **after** the `betaCelsius(...)` method (after line 96, before the `solveSteinhart` comment):

```cpp
  // Resistance at temperature tC on the FACTORY Beta curve (calibration-independent).
  float factoryResistanceAt(float tC) const {
    const float t0K = factory_.t0C + 273.15f;
    const float tK = tC + 273.15f;
    return factory_.r0Ohms * std::exp(factory_.beta * (1.0f / tK - 1.0f / t0K));
  }
```

Add the new private members at the end of the private member block — replace:

```cpp
  Point points_[kMaxPoints];
  int nPoints_ = 0;
};
```

with:

```cpp
  Point points_[kMaxPoints];
  int nPoints_ = 0;
  // Plausible-resistance band (permissive until begin() sets it).
  float minPlausibleC_ = -50.0f, maxPlausibleC_ = 200.0f;
  float rMinPlausible_ = 0.0f, rMaxPlausible_ = 1e12f;
};
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `/tmp/pio-venv/bin/pio test -e native -f test_thermistor_calibration`
Expected: PASS — `8 Tests 0 Failures 0 Ignored ... OK` (the prior 6 + 2 new).

- [ ] **Step 5: Commit**

```bash
git add include/sensor/ThermistorCalibration.hpp test/test_thermistor_calibration/test_main.cpp
git commit -m "feat(sensor): add calibration-independent plausible-resistance band"
```

---

### Task 2: Thermistor band guard + config wiring

**Files:**
- Modify: `include/sensor/Thermistor.hpp`
- Modify: `src/sensor/Thermistor.cpp`
- Modify: `include/app_config.hpp`
- Modify: `src/system/AppRuntime.cpp`

- [ ] **Step 1: Add config knobs in `include/app_config.hpp`**

In `namespace Thermal`, immediately after the line `static constexpr float kVSupplyMv     = 3300.0f;   // divider top rail`, add:

```cpp
// Plausible heater-NTC reading window; outside -> probe fault (heater off / start refused).
static constexpr float kNtcMinPlausibleC = 0.0f;
static constexpr float kNtcMaxPlausibleC = 110.0f;
```

- [ ] **Step 2: Add the window to `Thermistor::Config` in `include/sensor/Thermistor.hpp`**

In `struct Config`, immediately after `int samples = 8;              // averaged ADC reads`, add:

```cpp
    float minPlausibleC = 0.0f;   // reading below this -> probe fault
    float maxPlausibleC = 110.0f; // reading above this -> probe fault
```

- [ ] **Step 3: Wire the window in `src/sensor/Thermistor.cpp`**

In `begin()`, replace:

```cpp
  cal_.begin({cfg_.r0Ohms, cfg_.beta, cfg_.t0C});
```

with:

```cpp
  cal_.begin({cfg_.r0Ohms, cfg_.beta, cfg_.t0C}, cfg_.minPlausibleC, cfg_.maxPlausibleC);
```

Then replace `readResistanceOhms()`:

```cpp
float Thermistor::readResistanceOhms() {
  const float vAdc = static_cast<float>(readMilliVolts());
  if (vAdc <= 1.0f || vAdc >= cfg_.vSupplyMv - 1.0f) return NAN;  // open/short
  return cfg_.seriesOhms * vAdc / (cfg_.vSupplyMv - vAdc);
}
```

with:

```cpp
float Thermistor::readResistanceOhms() {
  const float vAdc = static_cast<float>(readMilliVolts());
  if (vAdc <= 1.0f || vAdc >= cfg_.vSupplyMv - 1.0f) return NAN;  // rail: short / open
  const float r = cfg_.seriesOhms * vAdc / (cfg_.vSupplyMv - vAdc);
  if (!cal_.plausibleResistance(r)) return NAN;                  // out-of-band: probe fault
  return r;
}
```

- [ ] **Step 4: Wire the config in `src/system/AppRuntime.cpp`**

In `makeThermistorConfig()`, immediately after `c.vSupplyMv = AppConfig::Thermal::kVSupplyMv;`, add:

```cpp
  c.minPlausibleC = AppConfig::Thermal::kNtcMinPlausibleC;
  c.maxPlausibleC = AppConfig::Thermal::kNtcMaxPlausibleC;
```

- [ ] **Step 5: Compile + native tests**

Run: `/tmp/pio-venv/bin/pio run -e esp32-s3-devkitc-1` → Expected: `SUCCESS`.
Run: `/tmp/pio-venv/bin/pio test -e native` → Expected: all pass (29 + the 2 new band tests = 31).

- [ ] **Step 6: Commit**

```bash
git add include/sensor/Thermistor.hpp src/sensor/Thermistor.cpp include/app_config.hpp src/system/AppRuntime.cpp
git commit -m "feat(sensor): reject out-of-band NTC readings as probe fault"
```

---

### Task 3: Pre-flight Start block

**Files:**
- Modify: `include/control/ThermalController.hpp`
- Modify: `src/control/ThermalController.cpp`
- Modify: `src/control/Reactor.cpp`

- [ ] **Step 1: Change the `enable` signature in `include/control/ThermalController.hpp`**

Replace:

```cpp
  void enable(bool on);
```

with:

```cpp
  bool enable(bool on);  // returns the resulting enabled state; false => start refused
```

- [ ] **Step 2: Add the pre-flight check in `src/control/ThermalController.cpp`**

Replace the whole `enable` function:

```cpp
void ThermalController::enable(bool on) {
  if (on == enabled_) return;
  enabled_ = on;
  pid_.reset();
  lastPidMs_ = 0;
  if (!on) {
    cancelAutotune();  // clears autotuneResult_ + returns to Auto if mid-tune
    applyOff();
  }
}
```

with:

```cpp
bool ThermalController::enable(bool on) {
  if (on == enabled_) return enabled_;
  if (on) {
    // Pre-flight: refuse to start if the heater NTC is faulted or already over-limit.
    heaterC_ = ntc_.readCelsius();
    if (isnan(heaterC_) || heaterC_ >= cfg_.heaterSafetyMaxC) {
      applyOff();  // leave enabled_ false; the run never begins
      return false;
    }
  }
  enabled_ = on;
  pid_.reset();
  lastPidMs_ = 0;
  if (!on) {
    cancelAutotune();  // clears autotuneResult_ + returns to Auto if mid-tune
    applyOff();
  }
  return enabled_;
}
```

- [ ] **Step 3: Abort the run on refusal in `src/control/Reactor.cpp`**

Replace the `start` function body:

```cpp
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
```

with (enable first; if refused, leave everything idle and don't touch the motor):

```cpp
void Reactor::start(float targetC, float rpm, uint16_t durationMin) {
  targetC_ = targetC;
  rpm_ = RpmKinematics::clampRpm(rpm, cfg_.minRpm, cfg_.maxRpm);
  durationMin_ = durationMin;
  persist();

  thermal_.setSetpoint(targetC_);
  if (!thermal_.enable(true)) {  // heater NTC faulted / over-limit -> refuse start
    running_ = false;
    return;
  }
  running_ = true;
  startMs_ = millis();

  motor_.enable(true);
  motor_.setCurrentMilliamps(discCurrentMa_);
  motor_.setMicrosteps(discMicrosteps_);
  motor_.setDirection(discReverse_);
  motor_.setRpm(rpm_);
}
```

- [ ] **Step 4: Compile + native tests**

Run: `/tmp/pio-venv/bin/pio run -e esp32-s3-devkitc-1` → Expected: `SUCCESS`.
Run: `/tmp/pio-venv/bin/pio test -e native` → Expected: all pass (31).

- [ ] **Step 5: Commit**

```bash
git add include/control/ThermalController.hpp src/control/ThermalController.cpp src/control/Reactor.cpp
git commit -m "feat(control): refuse Start when heater NTC is faulted or over-limit"
```

---

### Task 4: Mock server — simulate a disconnected/faulted probe

**Files:**
- Modify: `tools/mock_server.py`

- [ ] **Step 1: Null resistance on fault in `status()`**

In the `probe` sub-dict, replace:

```python
                       "probe": {"adcRaw": state["ntcAdc"],
                                 "resistanceOhms": round(state["ntcR"]),
                                 "calibrated": state["calibrated"],
                                 "method": state["calMethod"]}},
```

with (a faulted probe reports `resistanceOhms: null`, mirroring the firmware):

```python
                       "probe": {"adcRaw": state["ntcAdc"],
                                 "resistanceOhms": None if state["heaterProbeFault"]
                                                   else round(state["ntcR"]),
                                 "calibrated": state["calibrated"],
                                 "method": state["calMethod"]}},
```

(`heaterTempC` is already emitted as `null` and the `heater_probe_fault` alarm is already
raised when `state["heaterProbeFault"]` is set — no change needed there.)

- [ ] **Step 2: Refuse Start when the probe is faulted in `api_run`**

In `api_run`, in the `action == "start"` branch, immediately **before** the
`state.update(running=True, ...)` call, add:

```python
        if state["heaterProbeFault"]:
            # Mirror firmware pre-flight: heater NTC faulted -> start refused, run stays
            # idle. /run still acks (async queue); refusal observed via GET /status.
            return web.json_response({"ok": True})
```

- [ ] **Step 3: Add a debug toggle to set/clear the simulated fault**

Add this handler immediately **before** `async def api_setpoint(req):`:

```python
async def api_debug_probe_fault(req):
    # Mock-only: simulate a disconnected/faulted heater NTC for UI/integration testing.
    b = await req.json()
    state["heaterProbeFault"] = bool(b.get("fault", False))
    return web.json_response({"ok": True, "heaterProbeFault": state["heaterProbeFault"]})
```

Register it in `main()` immediately after the `app.router.add_post("/api/v1/run", api_run)` line:

```python
    app.router.add_post("/api/v1/debug/probe-fault", api_debug_probe_fault)
```

- [ ] **Step 4: Verify**

```bash
/tmp/pio-venv/bin/python -c "import ast; ast.parse(open('tools/mock_server.py').read()); print('syntax ok')"
/tmp/pio-venv/bin/python tools/mock_server.py & MOCK=$!; sleep 2
echo "set probe fault:"; curl -s -XPOST localhost:8000/api/v1/debug/probe-fault -H 'content-type: application/json' -d '{"fault":true}'; echo
curl -s localhost:8000/api/v1/status | /tmp/pio-venv/bin/python -c \
  "import sys,json; d=json.load(sys.stdin); p=d['thermal']['safety']['probe']; al=[a['code'] for a in d['alarms']]; print('probe=',p,'alarms=',al); assert p['resistanceOhms'] is None and d['thermal']['safety']['heaterTempC'] is None and 'heater_probe_fault' in al"
echo "start with fault -> acked but run stays idle:"; curl -s -XPOST localhost:8000/api/v1/run -H 'content-type: application/json' -d '{"action":"start","targetC":36,"rpm":8}'; echo
curl -s localhost:8000/api/v1/status | /tmp/pio-venv/bin/python -c \
  "import sys,json; d=json.load(sys.stdin); print('running=',d['running']); assert d['running'] is False"
echo "clear fault -> start works:"; curl -s -XPOST localhost:8000/api/v1/debug/probe-fault -H 'content-type: application/json' -d '{"fault":false}' >/dev/null
curl -s -XPOST localhost:8000/api/v1/run -H 'content-type: application/json' -d '{"action":"start","targetC":36,"rpm":8}' >/dev/null
curl -s localhost:8000/api/v1/status | /tmp/pio-venv/bin/python -c \
  "import sys,json; d=json.load(sys.stdin); print('running=',d['running']); assert d['running'] is True"
kill $MOCK 2>/dev/null
```

Expected: with the fault set, `probe.resistanceOhms` and `heaterTempC` are `null` and `heater_probe_fault` is in `alarms`; Start acks but `running` stays `false`; after clearing the fault, Start sets `running:true`. Kill the mock at the end (don't leave it running).

- [ ] **Step 5: Commit**

```bash
git add tools/mock_server.py
git commit -m "feat(mock): simulate disconnected NTC (null telemetry + refused start)"
```

---

## Definition of done

- `pio test -e native` passes (31: prior 29 + 2 band tests).
- `pio run -e esp32-s3-devkitc-1` builds clean.
- A reading outside 0…110 °C (≈ <529 Ω or >33.6 kΩ) makes `readResistanceOhms()`/`readCelsius()` return `NaN`; `thermal.safety.probe.resistanceOhms` and `thermal.heaterTempC` become `null`; `heater_probe_fault` (warn) is raised even when idle.
- Pressing Start with a faulted/over-limit heater NTC leaves the reactor idle (`running:false`); a healthy in-band probe starts normally.
- Mock mirrors all of the above (verified by curl).
- **On hardware:** with the NTC unplugged, `/status` shows `probe.resistanceOhms: null`, `heaterTempC: null`, a `heater_probe_fault` alarm, and `POST /run` start leaves `running:false`. (Re-check with the real probe + reference thermometer once available: a genuine reading is in-band and Start is allowed.)

## Out of scope (later)
- DS18B20 cross-validation to catch *subtle* in-band wrong readings (a fault that reads plausibly-but-wrong). The band only catches gross faults/disconnects.
