# Phase 2 — Motor Telemetry & Diagnostics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Surface TMC2209 diagnostics — StallGuard `load` and decoded `DRV_STATUS` fault flags — through the `/api/v1` telemetry, drive driver-fault alarms with stable `since` timestamps, and populate the CSV `load` column.

**Architecture:** Two pure, host-tested units do the logic — `decodeDrvStatus()` (raw register → flags) and `AlarmTracker` (frame-based alarm `since` tracking). `Tmc2209Motor` gains thin diagnostic readers. `AppRuntime::buildStatusJson` adds `disc.load` + `disc.driver.flags` and routes all alarms through the tracker. The mock server gains a synthetic load/flags model so the future UI can exercise the new fields.

**Tech Stack:** C++17 / Arduino-ESP32 / PlatformIO, TMCStepper, ArduinoJson v7, Unity (native tests), Python/aiohttp (mock).

**Spec:** `docs/superpowers/specs/2026-06-15-reactor-firmware-api-design.md` — this is Phase 2 / §10 P2. Builds on Phase 1 (already on `main`).

---

## Baseline assumptions

- Starting point is `main` (P1 merged). Confirm green before Task 1:
  - `/tmp/pio-venv/bin/pio run -e esp32-s3-devkitc-1` → `SUCCESS`
  - `/tmp/pio-venv/bin/pio test -e native` → all pass (currently 4)
- PlatformIO CLI: `/tmp/pio-venv/bin/pio` (not on PATH). Mock Python: `/tmp/pio-venv/bin/python` (has aiohttp).
- Work on a branch (do not commit to `main` directly): `git checkout -b feature/p2-motor-telemetry`.

### TMC2209 facts (verified from the TMCStepper library)
- `TMC2209Stepper::DRV_STATUS()` → raw `uint32_t` (one UART read). Bit layout: `0 otpw, 1 ot, 2 s2ga, 3 s2gb, 4 s2vsa, 5 s2vsb, 6 ola, 7 olb` (8–11 = temp thresholds; 16–20 = cs_actual; 31 = stst).
- `TMC2209Stepper::SG_RESULT()` → `uint16_t` StallGuard result (relative load index; lower = higher load). Meaningful only while the motor is moving.
- TMC2209 has **no `stall` bit** in `DRV_STATUS`; stall is signalled on the **DIAG pin** (GPIO38, already configured `INPUT`). It only fires once StallGuard thresholds (`SGTHRS`) are tuned — out of P2 scope — so `stall` is wired through but documented as "requires tuning".

## File structure

| File | Responsibility | Action |
|---|---|---|
| `include/motor/DrvStatus.hpp` | Pure `DrvStatusFlags` + `decodeDrvStatus(uint32_t)`. No deps. | Create |
| `test/test_drv_status/test_main.cpp` | Unity tests for the decode. | Create |
| `include/system/AlarmTracker.hpp` | Pure frame-based alarm `since` tracker. No Arduino deps. | Create |
| `test/test_alarm_tracker/test_main.cpp` | Unity tests for the tracker. | Create |
| `include/motor/Tmc2209Motor.hpp` / `.cpp` | `stallGuardResult()` + `driverFlags()`. | Modify |
| `src/system/AppRuntime.cpp` | `disc.load`, `disc.driver.flags`, AlarmTracker-driven `alarms[]` with `since`. | Modify |
| `src/control/Reactor.cpp` | Populate the CSV `load` column. | Modify |
| `tools/mock_server.py` | Simulate load + driver flags + driver alarms + `since` on all alarms. | Modify |

> **Embedded testing note:** `decodeDrvStatus` and `AlarmTracker` are host-unit-tested (real TDD). The hardware readers (`SG_RESULT`/`DRV_STATUS`/DIAG) and JSON wiring are verified by compile + the mock's curl checks.

---

### Task 1: DrvStatus decode (pure, TDD)

**Files:**
- Create: `include/motor/DrvStatus.hpp`
- Create: `test/test_drv_status/test_main.cpp`

- [ ] **Step 1: Write the failing test**

Create `test/test_drv_status/test_main.cpp`:

```cpp
#include <unity.h>
#include "motor/DrvStatus.hpp"

void setUp() {}
void tearDown() {}

void test_all_clear() {
  DrvStatusFlags f = decodeDrvStatus(0x00000000u);
  TEST_ASSERT_FALSE(f.otpw); TEST_ASSERT_FALSE(f.ot);
  TEST_ASSERT_FALSE(f.openLoadA); TEST_ASSERT_FALSE(f.openLoadB);
  TEST_ASSERT_FALSE(f.shortA); TEST_ASSERT_FALSE(f.shortB);
  TEST_ASSERT_FALSE(f.stall);  // decode never sets stall (DIAG-pin sourced)
}

void test_otpw_and_ot() {
  TEST_ASSERT_TRUE(decodeDrvStatus(1u << 0).otpw);
  TEST_ASSERT_TRUE(decodeDrvStatus(1u << 1).ot);
}

void test_open_load() {
  TEST_ASSERT_TRUE(decodeDrvStatus(1u << 6).openLoadA);  // ola
  TEST_ASSERT_TRUE(decodeDrvStatus(1u << 7).openLoadB);  // olb
}

void test_short_combines_s2g_and_s2vs() {
  TEST_ASSERT_TRUE(decodeDrvStatus(1u << 2).shortA);  // s2ga
  TEST_ASSERT_TRUE(decodeDrvStatus(1u << 4).shortA);  // s2vsa
  TEST_ASSERT_TRUE(decodeDrvStatus(1u << 3).shortB);  // s2gb
  TEST_ASSERT_TRUE(decodeDrvStatus(1u << 5).shortB);  // s2vsb
}

void test_combined() {
  DrvStatusFlags f = decodeDrvStatus((1u<<0) | (1u<<1) | (1u<<6) | (1u<<7));
  TEST_ASSERT_TRUE(f.otpw); TEST_ASSERT_TRUE(f.ot);
  TEST_ASSERT_TRUE(f.openLoadA); TEST_ASSERT_TRUE(f.openLoadB);
  TEST_ASSERT_FALSE(f.shortA); TEST_ASSERT_FALSE(f.shortB);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_all_clear);
  RUN_TEST(test_otpw_and_ot);
  RUN_TEST(test_open_load);
  RUN_TEST(test_short_combines_s2g_and_s2vs);
  RUN_TEST(test_combined);
  return UNITY_END();
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `/tmp/pio-venv/bin/pio test -e native -f test_drv_status`
Expected: FAIL/ERROR — `motor/DrvStatus.hpp: No such file or directory`.

- [ ] **Step 3: Create the implementation**

Create `include/motor/DrvStatus.hpp`:

```cpp
/*
 * DrvStatus.hpp — pure decode of the TMC2209 DRV_STATUS register (0x6F).
 *
 * Header-only, dependency-free so it is host-unit-testable. Bit layout:
 *   0 otpw, 1 ot, 2 s2ga, 3 s2gb, 4 s2vsa, 5 s2vsb, 6 ola, 7 olb.
 * `stall` is NOT in DRV_STATUS (it is a DIAG-pin signal); decode leaves it false
 * and Tmc2209Motor fills it from the pin.
 */
#pragma once

#include <cstdint>

struct DrvStatusFlags {
  bool otpw = false;       // over-temperature prewarning
  bool ot = false;         // over-temperature shutdown
  bool stall = false;      // StallGuard stall (DIAG pin; set by Tmc2209Motor)
  bool openLoadA = false;  // ola
  bool openLoadB = false;  // olb
  bool shortA = false;     // s2ga || s2vsa
  bool shortB = false;     // s2gb || s2vsb
};

inline DrvStatusFlags decodeDrvStatus(uint32_t s) {
  DrvStatusFlags f;
  f.otpw = (s & (1u << 0)) != 0;
  f.ot = (s & (1u << 1)) != 0;
  f.shortA = (s & (1u << 2)) != 0 || (s & (1u << 4)) != 0;  // s2ga || s2vsa
  f.shortB = (s & (1u << 3)) != 0 || (s & (1u << 5)) != 0;  // s2gb || s2vsb
  f.openLoadA = (s & (1u << 6)) != 0;
  f.openLoadB = (s & (1u << 7)) != 0;
  return f;
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `/tmp/pio-venv/bin/pio test -e native -f test_drv_status`
Expected: PASS — `5 Tests 0 Failures 0 Ignored ... OK`.

- [ ] **Step 5: Commit**

```bash
git add include/motor/DrvStatus.hpp test/test_drv_status/test_main.cpp
git commit -m "feat(motor): add host-tested DRV_STATUS decode"
```

---

### Task 2: AlarmTracker (pure, TDD)

**Files:**
- Create: `include/system/AlarmTracker.hpp`
- Create: `test/test_alarm_tracker/test_main.cpp`

- [ ] **Step 1: Write the failing test**

Create `test/test_alarm_tracker/test_main.cpp`:

```cpp
#include <unity.h>
#include "system/AlarmTracker.hpp"

void setUp() {}
void tearDown() {}

void test_since_is_set_on_first_appearance() {
  AlarmTracker t;
  t.beginFrame(10);
  t.add("sensor_fault", "warn");
  t.endFrame();
  TEST_ASSERT_EQUAL_INT(1, t.size());
  TEST_ASSERT_EQUAL_UINT32(10, t[0].sinceSec);
}

void test_since_persists_across_frames() {
  AlarmTracker t;
  t.beginFrame(10); t.add("sensor_fault", "warn"); t.endFrame();
  t.beginFrame(25); t.add("sensor_fault", "warn"); t.endFrame();
  TEST_ASSERT_EQUAL_INT(1, t.size());
  TEST_ASSERT_EQUAL_UINT32(10, t[0].sinceSec);  // unchanged
}

void test_cleared_when_not_reported() {
  AlarmTracker t;
  t.beginFrame(10); t.add("sensor_fault", "warn"); t.endFrame();
  t.beginFrame(20); t.endFrame();  // not reported this frame
  TEST_ASSERT_EQUAL_INT(0, t.size());
}

void test_reappearance_gets_new_since() {
  AlarmTracker t;
  t.beginFrame(10); t.add("sensor_fault", "warn"); t.endFrame();
  t.beginFrame(20); t.endFrame();
  t.beginFrame(30); t.add("sensor_fault", "warn"); t.endFrame();
  TEST_ASSERT_EQUAL_UINT32(30, t[0].sinceSec);
}

void test_multiple_independent_since() {
  AlarmTracker t;
  t.beginFrame(10); t.add("a", "warn"); t.endFrame();
  t.beginFrame(20); t.add("a", "warn"); t.add("b", "critical"); t.endFrame();
  TEST_ASSERT_EQUAL_INT(2, t.size());
  // 'a' kept since 10, 'b' since 20 — find each regardless of order
  for (int i = 0; i < t.size(); ++i) {
    if (std::strcmp(t[i].code, "a") == 0) TEST_ASSERT_EQUAL_UINT32(10, t[i].sinceSec);
    if (std::strcmp(t[i].code, "b") == 0) TEST_ASSERT_EQUAL_UINT32(20, t[i].sinceSec);
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_since_is_set_on_first_appearance);
  RUN_TEST(test_since_persists_across_frames);
  RUN_TEST(test_cleared_when_not_reported);
  RUN_TEST(test_reappearance_gets_new_since);
  RUN_TEST(test_multiple_independent_since);
  return UNITY_END();
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `/tmp/pio-venv/bin/pio test -e native -f test_alarm_tracker`
Expected: FAIL/ERROR — `system/AlarmTracker.hpp: No such file or directory`.

- [ ] **Step 3: Create the implementation**

Create `include/system/AlarmTracker.hpp`:

```cpp
/*
 * AlarmTracker.hpp — frame-based active-alarm set with stable `since` timestamps.
 *
 * Header-only, no Arduino deps (host-testable; time is injected via beginFrame).
 * Usage per telemetry tick:
 *   tracker.beginFrame(uptimeSec);
 *   if (cond) tracker.add("code", "severity");   // for each active alarm
 *   tracker.endFrame();                           // drops alarms not added this frame
 * Codes/severities must be string literals (static lifetime). `sinceSec` is set
 * when an alarm first appears and preserved while it stays active.
 */
#pragma once

#include <cstdint>
#include <cstring>

class AlarmTracker {
 public:
  static constexpr int kMax = 12;
  struct Alarm { const char* code; const char* severity; uint32_t sinceSec; };

  void beginFrame(uint32_t nowSec) {
    nowSec_ = nowSec;
    for (int i = 0; i < count_; ++i) seen_[i] = false;
  }

  void add(const char* code, const char* severity) {
    for (int i = 0; i < count_; ++i) {
      if (std::strcmp(slots_[i].code, code) == 0) {
        slots_[i].severity = severity;  // severity may change; since does not
        seen_[i] = true;
        return;
      }
    }
    if (count_ < kMax) {
      slots_[count_] = Alarm{code, severity, nowSec_};
      seen_[count_] = true;
      ++count_;
    }
  }

  void endFrame() {
    int w = 0;
    for (int i = 0; i < count_; ++i) {
      if (seen_[i]) { slots_[w] = slots_[i]; seen_[w] = true; ++w; }
    }
    count_ = w;
  }

  int size() const { return count_; }
  const Alarm& operator[](int i) const { return slots_[i]; }

 private:
  Alarm slots_[kMax] = {};
  bool seen_[kMax] = {false};
  int count_ = 0;
  uint32_t nowSec_ = 0;
};
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `/tmp/pio-venv/bin/pio test -e native -f test_alarm_tracker`
Expected: PASS — `5 Tests 0 Failures 0 Ignored ... OK`.

- [ ] **Step 5: Commit**

```bash
git add include/system/AlarmTracker.hpp test/test_alarm_tracker/test_main.cpp
git commit -m "feat(system): add host-tested AlarmTracker (since tracking)"
```

---

### Task 3: Tmc2209Motor diagnostic readers

**Files:**
- Modify: `include/motor/Tmc2209Motor.hpp`
- Modify: `src/motor/Tmc2209Motor.cpp`

- [ ] **Step 1: Declare the readers in the header**

In `include/motor/Tmc2209Motor.hpp`, add the include near the existing includes (after `#include <TMCStepper.h>`):

```cpp
#include "motor/DrvStatus.hpp"
```

In the `public:` section, after the existing `drvStatus()` declaration, add:

```cpp
  /* stallGuardResult() — StallGuard SG_RESULT (relative load index; lower = more
   * load). Only meaningful while the motor is moving. One UART read. */
  uint16_t stallGuardResult();

  /* driverFlags() — Decoded DRV_STATUS (one UART read) with `stall` filled from
   * the DIAG pin. Note: DIAG/stall requires StallGuard (SGTHRS) tuning to be
   * meaningful — out of scope here; it reads the raw pin level. */
  DrvStatusFlags driverFlags();
```

- [ ] **Step 2: Implement them**

In `src/motor/Tmc2209Motor.cpp`, after the existing `drvStatus()` definition, add:

```cpp
uint16_t Tmc2209Motor::stallGuardResult() { return driver_.SG_RESULT(); }

DrvStatusFlags Tmc2209Motor::driverFlags() {
  DrvStatusFlags f = decodeDrvStatus(driver_.DRV_STATUS());
  f.stall = digitalRead(cfg_.pinDiag) != 0;
  return f;
}
```

- [ ] **Step 3: Compile**

Run: `/tmp/pio-venv/bin/pio run -e esp32-s3-devkitc-1`
Expected: `SUCCESS`.

- [ ] **Step 4: Commit**

```bash
git add include/motor/Tmc2209Motor.hpp src/motor/Tmc2209Motor.cpp
git commit -m "feat(motor): add stallGuardResult + driverFlags readers"
```

---

### Task 4: Telemetry + alarms + CSV load wiring

**Files:**
- Modify: `src/system/AppRuntime.cpp`
- Modify: `src/control/Reactor.cpp`

- [ ] **Step 1: Add includes to AppRuntime.cpp**

In `src/system/AppRuntime.cpp`, add to the include list (near the other project includes):

```cpp
#include "motor/DrvStatus.hpp"
#include "system/AlarmTracker.hpp"
```

- [ ] **Step 2: Extend the `disc` block in `buildStatusJson()`**

In `buildStatusJson()`, the `disc` block currently ends after the `driver` sub-object (with `version` + `connected`). Replace the driver sub-object construction so the connection state is captured once and `load` + `flags` are added. Find:

```cpp
  JsonObject drv = disc["driver"].to<JsonObject>();
  char ver[8];
  snprintf(ver, sizeof(ver), "0x%02X", g_motor.version());
  drv["version"] = ver;
  drv["connected"] = g_motor.connected();
```

Replace with:

```cpp
  const bool drvConnected = g_motor.connected();
  const DrvStatusFlags dflags = drvConnected ? g_motor.driverFlags() : DrvStatusFlags{};
  // load: StallGuard, only meaningful while running + linked.
  if (t.running && drvConnected) disc["load"] = g_motor.stallGuardResult();
  else disc["load"] = nullptr;
  JsonObject drv = disc["driver"].to<JsonObject>();
  char ver[8];
  snprintf(ver, sizeof(ver), "0x%02X", g_motor.version());
  drv["version"] = ver;
  drv["connected"] = drvConnected;
  JsonObject dfl = drv["flags"].to<JsonObject>();
  dfl["otpw"] = dflags.otpw;
  dfl["ot"] = dflags.ot;
  dfl["stall"] = dflags.stall;
  dfl["openLoadA"] = dflags.openLoadA;
  dfl["openLoadB"] = dflags.openLoadB;
  dfl["shortA"] = dflags.shortA;
  dfl["shortB"] = dflags.shortB;
```

- [ ] **Step 3: Replace the inline alarms with the AlarmTracker**

Still in `buildStatusJson()`, find the existing alarms block:

```cpp
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
```

Replace with (a function-static tracker keeps `since` stable across calls; reuses `dflags`/`drvConnected` from Step 2):

```cpp
  static AlarmTracker s_alarms;
  s_alarms.beginFrame(millis() / 1000UL);
  if (t.sensorFault) s_alarms.add("sensor_fault", "warn");
  if (isnan(t.heaterTempC)) s_alarms.add("heater_probe_fault", "warn");
  if (t.safetyTripped) s_alarms.add("safety_tripped", "critical");
  if (drvConnected) {
    if (dflags.ot) s_alarms.add("driver_ot", "critical");
    else if (dflags.otpw) s_alarms.add("driver_otpw", "warn");
    if (dflags.stall) s_alarms.add("driver_stall", "warn");
    if (dflags.openLoadA || dflags.openLoadB) s_alarms.add("driver_open_load", "warn");
  }
  s_alarms.endFrame();
  JsonArray alarms = doc["alarms"].to<JsonArray>();
  for (int i = 0; i < s_alarms.size(); ++i) {
    JsonObject a = alarms.add<JsonObject>();
    a["code"] = s_alarms[i].code;
    a["severity"] = s_alarms[i].severity;
    a["since"] = s_alarms[i].sinceSec;
  }
```

- [ ] **Step 4: Populate the CSV `load` column in Reactor.cpp**

In `src/control/Reactor.cpp`, `csvRow()` currently passes a literal `0` for load. Change it to read StallGuard while running. Find:

```cpp
           t.heaterDutyPct, t.rpm, /*load*/ 0, t.sensorFault ? 1 : 0,
```

Replace with:

```cpp
           t.heaterDutyPct, t.rpm, running_ ? (int)motor_.stallGuardResult() : 0,
           t.sensorFault ? 1 : 0,
```

(`running_` and the `motor_` reference are both members; reading SG_RESULT from the const `csvRow()` is fine — the referent is non-const.)

- [ ] **Step 5: Compile + native tests**

Run: `/tmp/pio-venv/bin/pio run -e esp32-s3-devkitc-1` → Expected: `SUCCESS`.
Run: `/tmp/pio-venv/bin/pio test -e native` → Expected: all pass (now 14 across rpm + drv_status + alarm_tracker).

- [ ] **Step 6: Commit**

```bash
git add src/system/AppRuntime.cpp src/control/Reactor.cpp
git commit -m "feat(api): add disc.load + driver.flags telemetry, alarm since, CSV load"
```

---

### Task 5: Mock server — load, driver flags, alarm `since`

**Files:**
- Modify: `tools/mock_server.py`

- [ ] **Step 1: Add load + flags + driver-fault state**

In `tools/mock_server.py`, add fields to the `state` dict (next to the other disc fields like `"enabled": True,`):

```python
    "load": 280, "loadBias": 0.0,
    "drvOt": False, "drvOtpw": False, "drvStall": False,
    "drvOpenLoadA": False, "drvOpenLoadB": False, "drvShortA": False, "drvShortB": False,
```

- [ ] **Step 2: Simulate a drifting load in `simulate()`**

In `simulate()`, inside the `if s["running"]:` branch (after the thermal lines), add a slow "biofilm" load drift + noise:

```python
            s["loadBias"] = min(120.0, s["loadBias"] + 0.05)   # biofilm slowly loads the disc
            s["load"] = int(max(0, 380 - s["loadBias"] + random.uniform(-8, 8)))
```

And in the `else:` (stopped) branch, set load to its idle value:

```python
            s["load"] = 0
            s["loadBias"] = 0.0
```

- [ ] **Step 3: Emit `load`, `driver.flags`, and alarm `since` in `status()`**

In `status()`, replace the `disc` block's `driver` sub-dict and the whole `alarms` assembly.

First, the `disc` dict — change the `"driver"` line and add `"load"`. Find:

```python
        "disc": {
            "running": state["running"], "rpm": rpm,
            "rpmSetpoint": state["rpmSetpoint"],
            "direction": "ccw" if state["reverse"] else "cw",
            "currentMa": state["currentMa"], "microsteps": state["microsteps"],
            "enabled": state["enabled"],
            "driver": {"version": "0x21", "connected": True},
        },
```

Replace with:

```python
        "disc": {
            "running": state["running"], "rpm": rpm,
            "rpmSetpoint": state["rpmSetpoint"],
            "direction": "ccw" if state["reverse"] else "cw",
            "currentMa": state["currentMa"], "microsteps": state["microsteps"],
            "enabled": state["enabled"],
            "load": state["load"] if state["running"] else None,
            "driver": {"version": "0x21", "connected": True, "flags": {
                "otpw": state["drvOtpw"], "ot": state["drvOt"], "stall": state["drvStall"],
                "openLoadA": state["drvOpenLoadA"], "openLoadB": state["drvOpenLoadB"],
                "shortA": state["drvShortA"], "shortB": state["drvShortB"]}},
        },
```

- [ ] **Step 4: Build alarms with `since` (frame tracking)**

Replace the existing alarm assembly in `status()`. Find:

```python
    alarms = []
    if state["fault"]:
        alarms.append({"code": "sensor_fault", "severity": "warn"})
    if state["safetyTripped"]:
        alarms.append({"code": "safety_tripped", "severity": "critical"})
```

Replace with (module-level `_alarm_since` dict keeps `since` stable; add `heater_probe_fault` + driver alarms):

```python
    active = []
    if state["fault"]:
        active.append(("sensor_fault", "warn"))
    if state["heaterProbeFault"]:
        active.append(("heater_probe_fault", "warn"))
    if state["safetyTripped"]:
        active.append(("safety_tripped", "critical"))
    if state["drvOt"]:
        active.append(("driver_ot", "critical"))
    elif state["drvOtpw"]:
        active.append(("driver_otpw", "warn"))
    if state["drvStall"]:
        active.append(("driver_stall", "warn"))
    if state["drvOpenLoadA"] or state["drvOpenLoadB"]:
        active.append(("driver_open_load", "warn"))
    now = int(time.monotonic())
    codes = {c for c, _ in active}
    for c in list(_alarm_since):
        if c not in codes:
            del _alarm_since[c]
    alarms = []
    for code, sev in active:
        _alarm_since.setdefault(code, now)
        alarms.append({"code": code, "severity": sev, "since": _alarm_since[code]})
```

Then add the module-level dict near the top (after `clients = set()` or `AMBIENT = 22.0`):

```python
_alarm_since = {}
```

- [ ] **Step 5: Run the mock and verify the new fields**

```bash
/tmp/pio-venv/bin/python tools/mock_server.py & MOCK=$!; sleep 2
curl -s -XPOST localhost:8000/api/v1/run -H 'content-type: application/json' -d '{"action":"start","rpm":8}' >/dev/null
sleep 1
curl -s localhost:8000/api/v1/status | /tmp/pio-venv/bin/python -c \
  "import sys,json; d=json.load(sys.stdin); print('load=',d['disc']['load'],'flags=',d['disc']['driver']['flags'])"
# fault-injection: flip a driver flag and check the alarm appears with a 'since'
/tmp/pio-venv/bin/python -c "
import tools.mock_server as m, time
m.state['drvOt'] = True
a = m.status()['alarms']
print('alarms=', a)
assert any(x['code']=='driver_ot' and 'since' in x for x in a), 'driver_ot alarm missing'
print('OK')
"
kill $MOCK 2>/dev/null
```

Expected: `load=` a number (~360–380) and `flags=` a dict of 7 booleans; the inline check prints `alarms=` including `{'code': 'driver_ot', 'severity': 'critical', 'since': ...}` then `OK`.

- [ ] **Step 6: Commit**

```bash
git add tools/mock_server.py
git commit -m "feat(mock): simulate StallGuard load, driver flags, alarm since"
```

---

## Phase 2 done — definition of done

- `pio test -e native` passes (rpm + drv_status + alarm_tracker).
- `pio run -e esp32-s3-devkitc-1` builds clean.
- `GET /api/v1/status` includes `disc.load` (null when stopped/offline), `disc.driver.flags` (7 booleans), and `alarms[]` entries carry `since`; driver faults raise `driver_ot`/`driver_otpw`/`driver_stall`/`driver_open_load`.
- CSV `load` column carries SG_RESULT while running.
- Mock mirrors all of the above (verified by curl + fault injection).
- **On hardware (when available):** with the disc running, confirm `disc.load` changes with applied resistance to the shaft, and that disconnecting a coil raises `driver_open_load`.

## Out of scope (later)
- StallGuard threshold (`SGTHRS`/`TCOOLTHRS`) tuning so DIAG `stall` is meaningful — needs hardware calibration (revisit with the motor-config UI).
- Driver-read rate-limiting (P2 reads DRV_STATUS/SG_RESULT each ~100 ms status build; fine for now).
- P3 (PID config + autotune), P4 (NTC calibration), SD/system endpoints, the UI phase.
