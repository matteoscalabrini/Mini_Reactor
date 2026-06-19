# On-Device OLED Control UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a front-panel UI (SH1107 128×128 OLED + rotary encoder + 3 buttons) that monitors the reactor (temperature-first dashboard) and gives core control — Start/Stop, adjust target/speed, and motor/heater pause holds.

**Architecture:** A dedicated `ui/` module — `Display` (U8g2 wrapper) and `InputManager` (encoder + buttons → discrete events) are thin hardware shells; `UiController` is a pure, host-tested screen state machine that reads a `ReactorSnapshot` POD and drives a narrow `ReactorControl` interface. `AppRuntime` wires them together and adapts `ReactorControl` to the real `Reactor`. Pause/hold is added to `Reactor`/`ThermalController`.

**Tech Stack:** C++17, Arduino-ESP32 (PlatformIO `espressif32@6.9.0`), U8g2, Unity (native unit tests).

## Global Constraints

- Target board only: `esp32-s3-devkitc-1`. Pure-logic code (`ui/UiController`, `ui/UiTypes`) must contain **no `Arduino.h`/U8g2 includes** so it compiles under `env:native`.
- Display: SH1107 128×128, I2C address **0x3C**, on the existing bus **SDA GPIO1 / SCL GPIO2** (shared with HUSB238 @ 0x08).
- **The U8g2 constructor MUST be passed the I2C pins explicitly** (`clock=2, data=1`). The ESP32-S3 Arduino default `Wire` pins are GPIO8/9, which are the encoder pins — letting U8g2 default would break the encoder and the bus.
- Input pins (per `docs/PINOUT.md`): encoder A/B/SW = GPIO8/9/41; buttons B1/B2/B3 = GPIO47/48/4.
- Pause is a toggle; the run stays active and `elapsedSec` keeps counting in all pause states. Only menu **Stop** ends a run. Safety trips override everything.
- Library pin: `olikraus/U8g2 @ ^2.36.18`.
- Commit after each task. Branch: `feature/oled-control-ui`.

---

## File structure

| File | Responsibility |
|---|---|
| `include/ui/UiTypes.hpp` (new) | `UiEvent`, `Screen`, `MenuItem` enums; `ReactorSnapshot` POD; `ReactorControl` interface. Pure. |
| `include/ui/UiController.hpp` / `src/ui/UiController.cpp` (new) | Pure screen state machine. Host-tested. |
| `include/ui/Display.hpp` / `src/ui/Display.cpp` (new) | U8g2 SH1107 wrapper; renders controller+snapshot. Hardware. |
| `include/ui/InputManager.hpp` / `src/ui/InputManager.cpp` (new) | Encoder ISR + debounced buttons → `UiEvent`. Hardware. |
| `include/control/ThermalController.hpp` / `src/control/ThermalController.cpp` (modify) | Add `setInhibited(bool)` (heater off + PID reset on resume). |
| `include/control/Reactor.hpp` / `src/control/Reactor.cpp` (modify) | Add pause/hold flags, `setMotorPaused`/`setFullHold`, telemetry fields. |
| `include/app_config.hpp` (modify) | New `AppConfig::Ui` namespace (pins, address, steps, timing). |
| `src/system/AppRuntime.cpp` (modify) | Construct UI objects + `ReactorControl` adapter; pump input + throttled render in loop. |
| `platformio.ini` (modify) | Add U8g2 dependency. |
| `test/test_ui_controller/test_main.cpp` (new) | Unity tests for `UiController`. |

---

## Task 1: Reactor & ThermalController pause / hold

Hardware-coupled (drives `motor_`/`thermal_`), so verified by compile + on-device check, consistent with the existing untested `Reactor`/`ThermalController` boundary. No host test.

**Files:**
- Modify: `include/control/ThermalController.hpp`, `src/control/ThermalController.cpp`
- Modify: `include/control/Reactor.hpp`, `src/control/Reactor.cpp`

**Interfaces:**
- Produces: `Reactor::setMotorPaused(bool)`, `Reactor::setFullHold(bool)`, `Reactor::motorPaused() const`, `Reactor::fullHold() const`; `ReactorTelemetry.motorPaused`, `ReactorTelemetry.fullHold`; `ThermalController::setInhibited(bool)`.

- [ ] **Step 1: Add `setInhibited` to ThermalController header.** In `include/control/ThermalController.hpp`, add a public method after `void update();` and a private flag after `bool safetyTrip_` (in the private section):

```cpp
  // public:
  void setInhibited(bool on);   // full-hold: force heater off, keep enabled_/mode
```
```cpp
  // private:
  bool inhibited_ = false;
```

- [ ] **Step 2: Implement `setInhibited` in ThermalController.cpp.** Add after `applyOff()`'s definition:

```cpp
void ThermalController::setInhibited(bool on) {
  if (inhibited_ && !on) pid_.reset();  // resume: clear integrator -> no wind-up bump
  inhibited_ = on;
  if (on) applyOff();
}
```

- [ ] **Step 3: Honor `inhibited_` in `update()`.** In `src/control/ThermalController.cpp::update()`, immediately after the line `if (safetyTrip_) return;` insert:

```cpp
  if (inhibited_) { applyOff(); return; }  // full-hold: heater off, run still active
```

- [ ] **Step 4: Add pause fields/methods to Reactor header.** In `include/control/Reactor.hpp`:
  - In `struct ReactorTelemetry`, after `bool safetyTripped = false;` add:
```cpp
  bool motorPaused = false;   // B1 hold: disc off, heater on
  bool fullHold = false;      // B2 hold: disc off, heater off
```
  - In the public methods (near `setDiscEnabled`) add:
```cpp
  /* Pause holds toggled from the front panel (run stays active). */
  void setMotorPaused(bool on);   // B1: disc off
  void setFullHold(bool on);      // B2: disc off + heater off
  bool motorPaused() const { return motorPaused_; }
  bool fullHold() const { return fullHold_; }
```
  - In the private section add:
```cpp
  bool motorPaused_ = false;
  bool fullHold_ = false;
  void applyMotorState();   // disc runs only if running && !motorPaused_ && !fullHold_
```

- [ ] **Step 5: Implement pause logic in Reactor.cpp.** Add these definitions (e.g. after `setDiscEnabled`):

```cpp
void Reactor::applyMotorState() {
  const bool motorOn = running_ && !motorPaused_ && !fullHold_;
  if (motorOn) {
    motor_.enable(true);
    motor_.setRpm(rpm_);
  } else {
    motor_.stop();
    motor_.enable(false);
  }
}

void Reactor::setMotorPaused(bool on) {
  motorPaused_ = on;
  applyMotorState();
}

void Reactor::setFullHold(bool on) {
  fullHold_ = on;
  thermal_.setInhibited(on);
  applyMotorState();
}
```

- [ ] **Step 6: Clear holds on start/stop and publish telemetry.** In `Reactor::start()`, at the very top of the body add `motorPaused_ = false; fullHold_ = false;`. In `Reactor::stop()`, after `running_ = false;` add `motorPaused_ = false; fullHold_ = false; thermal_.setInhibited(false);`. In `Reactor::telemetry()`, change the rpm line to `t.rpm = (running_ && !motorPaused_ && !fullHold_) ? rpm_ : 0.0f;` and add `t.motorPaused = motorPaused_; t.fullHold = fullHold_;`.

- [ ] **Step 7: Build to verify it compiles.**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: `[SUCCESS]`

- [ ] **Step 8: Commit.**

```bash
git add include/control/Reactor.hpp src/control/Reactor.cpp include/control/ThermalController.hpp src/control/ThermalController.cpp
git commit -m "feat(control): add motor/heater pause holds to Reactor + heater inhibit"
```

---

## Task 2: UiController — types + navigation state machine (TDD)

Pure logic, host-tested under `env:native`.

**Files:**
- Create: `include/ui/UiTypes.hpp`, `include/ui/UiController.hpp`, `src/ui/UiController.cpp`
- Test: `test/test_ui_controller/test_main.cpp`

**Interfaces:**
- Produces: `ui::UiEvent`, `ui::Screen`, `ui::MenuItem`, `ui::ReactorSnapshot`, `ui::ReactorControl`, `ui::UiController` (ctor `(ReactorControl&, const Config&)`, `handle(UiEvent, const ReactorSnapshot&)`, accessors `screen()`, `menuIndex()`, `editTargetC()`, `editRpm()`).

- [ ] **Step 1: Create the pure types.** `include/ui/UiTypes.hpp`:

```cpp
#pragma once
#include <stdint.h>

namespace ui {

enum class UiEvent : uint8_t {
  None, RotateCW, RotateCCW, EncShortPush, EncLongPush, Btn1, Btn2, Btn3
};

enum class Screen : uint8_t { Dashboard, Menu, EditTarget, EditSpeed, Info };

enum MenuItem : int { kMenuStartStop = 0, kMenuTarget, kMenuSpeed, kMenuInfo, kMenuBack, kMenuCount };

struct ReactorSnapshot {
  bool  running = false;
  bool  motorPaused = false;
  bool  fullHold = false;
  bool  sensorFault = false;
  bool  safetyTripped = false;
  float liquidTempC = 0.0f;    // may be NaN on fault
  float setpointC = 0.0f;
  float rpm = 0.0f;            // actual disc rpm (0 when stopped/paused)
  float rpmSetpoint = 0.0f;    // configured speed
  float heaterDutyPct = 0.0f;
  uint32_t elapsedSec = 0;
  // info screen
  bool  wifiConnected = false;
  const char* wifiSsid = "";
  const char* ip = "";
  int   rssi = 0;
  bool  sdMounted = false;
  const char* firmware = "";
};

struct ReactorControl {
  virtual ~ReactorControl() {}
  virtual void startRun(float targetC, float rpm) = 0;
  virtual void stopRun() = 0;
  virtual void setTargetC(float c) = 0;
  virtual void setRpm(float rpm) = 0;
  virtual void setMotorPaused(bool on) = 0;
  virtual void setFullHold(bool on) = 0;
};

}  // namespace ui
```

- [ ] **Step 2: Create the controller header.** `include/ui/UiController.hpp`:

```cpp
#pragma once
#include "ui/UiTypes.hpp"

namespace ui {

class UiController {
 public:
  struct Config {
    float minRpm = 0.5f;
    float maxRpm = 30.0f;
    float minTargetC = 0.0f;
    float maxTargetC = 55.0f;
    float targetStepC = 0.5f;
    float rpmStep = 0.5f;
  };

  UiController(ReactorControl& control, const Config& cfg);

  void handle(UiEvent e, const ReactorSnapshot& s);

  Screen screen() const { return screen_; }
  int    menuIndex() const { return menuIndex_; }
  float  editTargetC() const { return editTargetC_; }
  float  editRpm() const { return editRpm_; }
  bool   faultOverlay(const ReactorSnapshot& s) const { return s.sensorFault || s.safetyTripped; }

 private:
  void goHome();
  void openMenu();
  void activateMenuItem(const ReactorSnapshot& s);
  static float clampf(float v, float lo, float hi);

  ReactorControl& control_;
  Config cfg_;
  Screen screen_ = Screen::Dashboard;
  int    menuIndex_ = 0;
  float  editTargetC_ = 0.0f;
  float  editRpm_ = 0.0f;
};

}  // namespace ui
```

- [ ] **Step 3: Write the failing navigation tests.** `test/test_ui_controller/test_main.cpp`:

```cpp
#include <unity.h>
#include "ui/UiController.hpp"

using namespace ui;

struct FakeControl : ReactorControl {
  int startRunCalls = 0, stopRunCalls = 0, setTargetCalls = 0, setRpmCalls = 0;
  int motorPausedCalls = 0, fullHoldCalls = 0;
  float lastTargetC = 0, lastRpm = 0, setTargetVal = 0, setRpmVal = 0;
  bool lastMotorPaused = false, lastFullHold = false;
  void startRun(float t, float r) override { startRunCalls++; lastTargetC = t; lastRpm = r; }
  void stopRun() override { stopRunCalls++; }
  void setTargetC(float c) override { setTargetCalls++; setTargetVal = c; }
  void setRpm(float r) override { setRpmCalls++; setRpmVal = r; }
  void setMotorPaused(bool on) override { motorPausedCalls++; lastMotorPaused = on; }
  void setFullHold(bool on) override { fullHoldCalls++; lastFullHold = on; }
};

static UiController::Config cfg() { return UiController::Config{}; }

void setUp() {}
void tearDown() {}

void test_dashboard_push_opens_menu() {
  FakeControl fc; UiController ui(fc, cfg()); ReactorSnapshot s;
  ui.handle(UiEvent::EncShortPush, s);
  TEST_ASSERT_EQUAL(static_cast<int>(Screen::Menu), static_cast<int>(ui.screen()));
  TEST_ASSERT_EQUAL_INT(kMenuStartStop, ui.menuIndex());
}

void test_menu_rotate_wraps() {
  FakeControl fc; UiController ui(fc, cfg()); ReactorSnapshot s;
  ui.handle(UiEvent::EncShortPush, s);          // -> Menu, index 0
  ui.handle(UiEvent::RotateCCW, s);             // wrap to last item
  TEST_ASSERT_EQUAL_INT(kMenuBack, ui.menuIndex());
  ui.handle(UiEvent::RotateCW, s);              // wrap back to 0
  TEST_ASSERT_EQUAL_INT(kMenuStartStop, ui.menuIndex());
}

void test_long_press_returns_home_from_menu() {
  FakeControl fc; UiController ui(fc, cfg()); ReactorSnapshot s;
  ui.handle(UiEvent::EncShortPush, s);          // Menu
  ui.handle(UiEvent::EncLongPush, s);           // Home
  TEST_ASSERT_EQUAL(static_cast<int>(Screen::Dashboard), static_cast<int>(ui.screen()));
}

void test_btn3_toggles_info() {
  FakeControl fc; UiController ui(fc, cfg()); ReactorSnapshot s;
  ui.handle(UiEvent::Btn3, s);
  TEST_ASSERT_EQUAL(static_cast<int>(Screen::Info), static_cast<int>(ui.screen()));
  ui.handle(UiEvent::Btn3, s);
  TEST_ASSERT_EQUAL(static_cast<int>(Screen::Dashboard), static_cast<int>(ui.screen()));
}

void test_info_push_returns_home() {
  FakeControl fc; UiController ui(fc, cfg()); ReactorSnapshot s;
  ui.handle(UiEvent::Btn3, s);                  // Info
  ui.handle(UiEvent::EncShortPush, s);          // Home
  TEST_ASSERT_EQUAL(static_cast<int>(Screen::Dashboard), static_cast<int>(ui.screen()));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_dashboard_push_opens_menu);
  RUN_TEST(test_menu_rotate_wraps);
  RUN_TEST(test_long_press_returns_home_from_menu);
  RUN_TEST(test_btn3_toggles_info);
  RUN_TEST(test_info_push_returns_home);
  return UNITY_END();
}
```

- [ ] **Step 4: Run the test, verify it fails to link/compile.**

Run: `~/.platformio/penv/bin/pio test -e native -f test_ui_controller`
Expected: FAIL — `UiController` symbols undefined (no `.cpp` yet).

- [ ] **Step 5: Implement navigation in `src/ui/UiController.cpp`.**

```cpp
#include "ui/UiController.hpp"

namespace ui {

UiController::UiController(ReactorControl& control, const Config& cfg)
    : control_(control), cfg_(cfg) {}

float UiController::clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

void UiController::goHome() { screen_ = Screen::Dashboard; }
void UiController::openMenu() { screen_ = Screen::Menu; menuIndex_ = 0; }

void UiController::handle(UiEvent e, const ReactorSnapshot& s) {
  if (e == UiEvent::EncLongPush) { goHome(); return; }           // universal Home
  if (e == UiEvent::Btn1) { if (s.running) control_.setMotorPaused(!s.motorPaused); return; }
  if (e == UiEvent::Btn2) { if (s.running) control_.setFullHold(!s.fullHold); return; }
  if (e == UiEvent::Btn3) {
    screen_ = (screen_ == Screen::Info) ? Screen::Dashboard : Screen::Info;
    return;
  }

  switch (screen_) {
    case Screen::Dashboard:
      if (e == UiEvent::EncShortPush) openMenu();
      break;
    case Screen::Info:
      if (e == UiEvent::EncShortPush) goHome();
      break;
    case Screen::Menu:
      if (e == UiEvent::RotateCW)        menuIndex_ = (menuIndex_ + 1) % kMenuCount;
      else if (e == UiEvent::RotateCCW)  menuIndex_ = (menuIndex_ + kMenuCount - 1) % kMenuCount;
      else if (e == UiEvent::EncShortPush) activateMenuItem(s);
      break;
    case Screen::EditTarget:
      if (e == UiEvent::RotateCW)        editTargetC_ = clampf(editTargetC_ + cfg_.targetStepC, cfg_.minTargetC, cfg_.maxTargetC);
      else if (e == UiEvent::RotateCCW)  editTargetC_ = clampf(editTargetC_ - cfg_.targetStepC, cfg_.minTargetC, cfg_.maxTargetC);
      else if (e == UiEvent::EncShortPush) { control_.setTargetC(editTargetC_); screen_ = Screen::Menu; }
      break;
    case Screen::EditSpeed:
      if (e == UiEvent::RotateCW)        editRpm_ = clampf(editRpm_ + cfg_.rpmStep, cfg_.minRpm, cfg_.maxRpm);
      else if (e == UiEvent::RotateCCW)  editRpm_ = clampf(editRpm_ - cfg_.rpmStep, cfg_.minRpm, cfg_.maxRpm);
      else if (e == UiEvent::EncShortPush) { control_.setRpm(editRpm_); screen_ = Screen::Menu; }
      break;
  }
}

void UiController::activateMenuItem(const ReactorSnapshot& s) {
  switch (menuIndex_) {
    case kMenuStartStop:
      if (s.running) control_.stopRun();
      else           control_.startRun(s.setpointC, s.rpmSetpoint);
      break;
    case kMenuTarget:
      editTargetC_ = clampf(s.setpointC, cfg_.minTargetC, cfg_.maxTargetC);
      screen_ = Screen::EditTarget;
      break;
    case kMenuSpeed:
      editRpm_ = clampf(s.rpmSetpoint, cfg_.minRpm, cfg_.maxRpm);
      screen_ = Screen::EditSpeed;
      break;
    case kMenuInfo:
      screen_ = Screen::Info;
      break;
    case kMenuBack:
      goHome();
      break;
  }
}

}  // namespace ui
```

- [ ] **Step 6: Run the tests, verify they pass.**

Run: `~/.platformio/penv/bin/pio test -e native -f test_ui_controller`
Expected: `5 Tests 0 Failures`.

- [ ] **Step 7: Commit.**

```bash
git add include/ui/UiTypes.hpp include/ui/UiController.hpp src/ui/UiController.cpp test/test_ui_controller/test_main.cpp
git commit -m "feat(ui): pure UiController navigation state machine + tests"
```

---

## Task 3: UiController — control actions (TDD)

Extends the same files/tests with the action behaviors (Start/Stop, edit apply + clamp, pause toggles).

**Files:**
- Modify: `test/test_ui_controller/test_main.cpp`
- (No source change expected — Task 2's `UiController.cpp` already implements these; this task proves them.)

**Interfaces:**
- Consumes: everything from Task 2.

- [ ] **Step 1: Add the failing action tests.** Insert these functions before `main()` in `test/test_ui_controller/test_main.cpp`:

```cpp
void test_menu_start_when_stopped_calls_startRun() {
  FakeControl fc; UiController ui(fc, cfg());
  ReactorSnapshot s; s.running = false; s.setpointC = 36.0f; s.rpmSetpoint = 8.0f;
  ui.handle(UiEvent::EncShortPush, s);          // Menu, index = Start
  ui.handle(UiEvent::EncShortPush, s);          // activate Start
  TEST_ASSERT_EQUAL_INT(1, fc.startRunCalls);
  TEST_ASSERT_EQUAL_FLOAT(36.0f, fc.lastTargetC);
  TEST_ASSERT_EQUAL_FLOAT(8.0f, fc.lastRpm);
}

void test_menu_stop_when_running_calls_stopRun() {
  FakeControl fc; UiController ui(fc, cfg());
  ReactorSnapshot s; s.running = true;
  ui.handle(UiEvent::EncShortPush, s);          // Menu
  ui.handle(UiEvent::EncShortPush, s);          // activate Stop
  TEST_ASSERT_EQUAL_INT(1, fc.stopRunCalls);
}

void test_edit_target_adjust_and_apply() {
  FakeControl fc; UiController ui(fc, cfg());
  ReactorSnapshot s; s.setpointC = 36.0f;
  ui.handle(UiEvent::EncShortPush, s);          // Menu
  ui.handle(UiEvent::RotateCW, s);              // -> Target
  ui.handle(UiEvent::EncShortPush, s);          // enter EditTarget (init 36.0)
  ui.handle(UiEvent::RotateCW, s);              // 36.5
  ui.handle(UiEvent::EncShortPush, s);          // apply
  TEST_ASSERT_EQUAL_INT(1, fc.setTargetCalls);
  TEST_ASSERT_EQUAL_FLOAT(36.5f, fc.setTargetVal);
  TEST_ASSERT_EQUAL(static_cast<int>(Screen::Menu), static_cast<int>(ui.screen()));
}

void test_edit_target_clamped_high() {
  FakeControl fc; UiController ui(fc, cfg());
  ReactorSnapshot s; s.setpointC = 54.5f;       // maxTargetC = 55.0
  ui.handle(UiEvent::EncShortPush, s);
  ui.handle(UiEvent::RotateCW, s);              // -> Target
  ui.handle(UiEvent::EncShortPush, s);          // EditTarget init 54.5
  ui.handle(UiEvent::RotateCW, s);              // 55.0
  ui.handle(UiEvent::RotateCW, s);              // clamp at 55.0
  TEST_ASSERT_EQUAL_FLOAT(55.0f, ui.editTargetC());
}

void test_edit_speed_clamped_low() {
  FakeControl fc; UiController ui(fc, cfg());
  ReactorSnapshot s; s.rpmSetpoint = 1.0f;      // minRpm = 0.5
  ui.handle(UiEvent::EncShortPush, s);
  ui.handle(UiEvent::RotateCW, s); ui.handle(UiEvent::RotateCW, s);  // -> Speed
  ui.handle(UiEvent::EncShortPush, s);          // EditSpeed init 1.0
  ui.handle(UiEvent::RotateCCW, s);             // 0.5
  ui.handle(UiEvent::RotateCCW, s);             // clamp at 0.5
  TEST_ASSERT_EQUAL_FLOAT(0.5f, ui.editRpm());
}

void test_btn1_toggles_motor_pause_only_when_running() {
  FakeControl fc; UiController ui(fc, cfg());
  ReactorSnapshot stopped; stopped.running = false;
  ui.handle(UiEvent::Btn1, stopped);
  TEST_ASSERT_EQUAL_INT(0, fc.motorPausedCalls);          // no-op when stopped
  ReactorSnapshot running; running.running = true; running.motorPaused = false;
  ui.handle(UiEvent::Btn1, running);
  TEST_ASSERT_EQUAL_INT(1, fc.motorPausedCalls);
  TEST_ASSERT_TRUE(fc.lastMotorPaused);                    // pressed -> pause on
}

void test_btn2_toggles_full_hold_off_when_already_held() {
  FakeControl fc; UiController ui(fc, cfg());
  ReactorSnapshot s; s.running = true; s.fullHold = true;
  ui.handle(UiEvent::Btn2, s);
  TEST_ASSERT_EQUAL_INT(1, fc.fullHoldCalls);
  TEST_ASSERT_FALSE(fc.lastFullHold);                      // pressed while held -> resume
}
```

- [ ] **Step 2: Register the new tests in `main()`.** Add inside `UNITY_BEGIN()`/`UNITY_END()`:

```cpp
  RUN_TEST(test_menu_start_when_stopped_calls_startRun);
  RUN_TEST(test_menu_stop_when_running_calls_stopRun);
  RUN_TEST(test_edit_target_adjust_and_apply);
  RUN_TEST(test_edit_target_clamped_high);
  RUN_TEST(test_edit_speed_clamped_low);
  RUN_TEST(test_btn1_toggles_motor_pause_only_when_running);
  RUN_TEST(test_btn2_toggles_full_hold_off_when_already_held);
```

- [ ] **Step 3: Run all UiController tests.**

Run: `~/.platformio/penv/bin/pio test -e native -f test_ui_controller`
Expected: `12 Tests 0 Failures`. (If any fail, fix `UiController.cpp` from Task 2 — do not weaken the tests.)

- [ ] **Step 4: Commit.**

```bash
git add test/test_ui_controller/test_main.cpp
git commit -m "test(ui): cover UiController start/stop, edit clamping, pause toggles"
```

---

## Task 4: Display — U8g2 SH1107 wrapper

Hardware; verified on-device. Adds the U8g2 dependency.

**Files:**
- Modify: `platformio.ini`
- Create: `include/ui/Display.hpp`, `src/ui/Display.cpp`

**Interfaces:**
- Consumes: `ui::UiController`, `ui::ReactorSnapshot`.
- Produces: `ui::Display(uint8_t addr, int sclPin, int sdaPin)`, `begin()`, `bool present()`, `render(const UiController&, const ReactorSnapshot&)`.

- [ ] **Step 1: Add U8g2 to `platformio.ini`** under `[env:esp32-s3-devkitc-1]` `lib_deps`:

```ini
    olikraus/U8g2 @ ^2.36.18                ; SH1107 128x128 front-panel OLED
```

- [ ] **Step 2: Create `include/ui/Display.hpp`.**

```cpp
#pragma once
#include <U8g2lib.h>
#include "ui/UiController.hpp"
#include "ui/UiTypes.hpp"

namespace ui {

class Display {
 public:
  Display(uint8_t i2cAddr, int sclPin, int sdaPin);
  void begin();                       // call AFTER Wire.begin(sda, scl)
  bool present() const { return present_; }
  void render(const UiController& ui, const ReactorSnapshot& s);

 private:
  void drawDashboard(const ReactorSnapshot& s);
  void drawMenu(const UiController& ui, const ReactorSnapshot& s);
  void drawEdit(const UiController& ui, bool target);
  void drawInfo(const ReactorSnapshot& s);
  void drawFaultBanner(const ReactorSnapshot& s);

  U8G2_SH1107_PIMORONI_128X128_F_HW_I2C u8g2_;
  uint8_t addr_;
  bool present_ = false;
};

}  // namespace ui
```

- [ ] **Step 3: Create `src/ui/Display.cpp`.** (Pins passed to the constructor — see Global Constraints. Layout is functional; pixel polish happens during on-device verification.)

```cpp
#include "ui/Display.hpp"

#include <Wire.h>
#include <math.h>
#include <stdio.h>

namespace ui {

Display::Display(uint8_t i2cAddr, int sclPin, int sdaPin)
    : u8g2_(U8G2_R0, U8X8_PIN_NONE, sclPin, sdaPin), addr_(i2cAddr) {}

void Display::begin() {
  Wire.beginTransmission(addr_);
  present_ = (Wire.endTransmission() == 0);
  if (!present_) return;             // run headless if no panel on the bus
  u8g2_.setI2CAddress(addr_ << 1);
  u8g2_.begin();
  u8g2_.setContrast(255);
}

static void fmtElapsed(uint32_t sec, char* out, size_t n) {
  uint32_t h = sec / 3600, m = (sec % 3600) / 60, s = sec % 60;
  if (h) snprintf(out, n, "%lu:%02lu:%02lu", (unsigned long)h, (unsigned long)m, (unsigned long)s);
  else   snprintf(out, n, "%02lu:%02lu", (unsigned long)m, (unsigned long)s);
}

void Display::render(const UiController& ui, const ReactorSnapshot& s) {
  if (!present_) return;
  u8g2_.clearBuffer();
  if (ui.faultOverlay(s)) {
    drawFaultBanner(s);
  } else {
    switch (ui.screen()) {
      case Screen::Dashboard:  drawDashboard(s); break;
      case Screen::Menu:       drawMenu(ui, s); break;
      case Screen::EditTarget: drawEdit(ui, true); break;
      case Screen::EditSpeed:  drawEdit(ui, false); break;
      case Screen::Info:       drawInfo(s); break;
    }
  }
  u8g2_.sendBuffer();
}

void Display::drawDashboard(const ReactorSnapshot& s) {
  char buf[24];
  u8g2_.setFont(u8g2_font_6x12_tr);
  const char* state = "STOPPED";
  if (s.fullHold) state = "HOLD: M+H";
  else if (s.motorPaused) state = "MOTOR PAUSE";
  else if (s.running) state = "RUNNING";
  u8g2_.drawStr(2, 11, state);
  u8g2_.drawStr(95, 11, s.wifiConnected ? "wifi" : "----");

  // Hero temperature.
  u8g2_.setFont(u8g2_font_logisoso28_tn);
  if (isnan(s.liquidTempC)) snprintf(buf, sizeof(buf), "--.-");
  else snprintf(buf, sizeof(buf), "%.1f", s.liquidTempC);
  u8g2_.drawStr(8, 58, buf);
  u8g2_.setFont(u8g2_font_6x12_tr);
  u8g2_.drawStr(100, 40, "C");

  snprintf(buf, sizeof(buf), "target %.1f", s.setpointC);
  u8g2_.drawStr(8, 74, buf);

  snprintf(buf, sizeof(buf), "RPM %.1f", s.rpmSetpoint);
  u8g2_.drawStr(2, 98, buf);
  char t[12]; fmtElapsed(s.elapsedSec, t, sizeof(t));
  snprintf(buf, sizeof(buf), "time %s", t);
  u8g2_.drawStr(64, 98, buf);
  snprintf(buf, sizeof(buf), "heater %.0f%%", s.heaterDutyPct);
  u8g2_.drawStr(2, 116, buf);
}

void Display::drawMenu(const UiController& ui, const ReactorSnapshot& s) {
  char buf[24];
  u8g2_.setFont(u8g2_font_6x12_tr);
  u8g2_.drawStr(2, 11, "CONTROL");
  const char* labels[kMenuCount];
  labels[kMenuStartStop] = s.running ? "Stop run" : "Start run";
  labels[kMenuTarget]    = "Target temp";
  labels[kMenuSpeed]     = "Speed";
  labels[kMenuInfo]      = "Info";
  labels[kMenuBack]      = "Back";
  for (int i = 0; i < kMenuCount; ++i) {
    int y = 30 + i * 16;
    if (i == ui.menuIndex()) u8g2_.drawStr(2, y, ">");
    u8g2_.drawStr(14, y, labels[i]);
    if (i == kMenuTarget) { snprintf(buf, sizeof(buf), "%.1fC", s.setpointC); u8g2_.drawStr(86, y, buf); }
    if (i == kMenuSpeed)  { snprintf(buf, sizeof(buf), "%.1f", s.rpmSetpoint); u8g2_.drawStr(92, y, buf); }
  }
}

void Display::drawEdit(const UiController& ui, bool target) {
  char buf[24];
  u8g2_.setFont(u8g2_font_6x12_tr);
  u8g2_.drawStr(2, 11, target ? "SET TARGET TEMP" : "SET SPEED");
  u8g2_.setFont(u8g2_font_logisoso24_tn);
  if (target) snprintf(buf, sizeof(buf), "%.1f", ui.editTargetC());
  else        snprintf(buf, sizeof(buf), "%.1f", ui.editRpm());
  u8g2_.drawStr(20, 70, buf);
  u8g2_.setFont(u8g2_font_6x12_tr);
  u8g2_.drawStr(2, 110, "push: apply  long: home");
}

void Display::drawInfo(const ReactorSnapshot& s) {
  char buf[28];
  u8g2_.setFont(u8g2_font_6x12_tr);
  u8g2_.drawStr(2, 11, "INFO");
  snprintf(buf, sizeof(buf), "WiFi %s", s.wifiConnected ? s.wifiSsid : "down"); u8g2_.drawStr(2, 30, buf);
  snprintf(buf, sizeof(buf), "IP   %s", s.ip);                                  u8g2_.drawStr(2, 46, buf);
  snprintf(buf, sizeof(buf), "RSSI %d dBm", s.rssi);                            u8g2_.drawStr(2, 62, buf);
  snprintf(buf, sizeof(buf), "SD   %s", s.sdMounted ? "mounted" : "no card");   u8g2_.drawStr(2, 78, buf);
  snprintf(buf, sizeof(buf), "fw   %s", s.firmware);                            u8g2_.drawStr(2, 94, buf);
}

void Display::drawFaultBanner(const ReactorSnapshot& s) {
  u8g2_.setFont(u8g2_font_6x12_tr);
  u8g2_.drawBox(0, 40, 128, 40);
  u8g2_.setDrawColor(0);
  if (s.safetyTripped) {
    u8g2_.drawStr(8, 58, "! SAFETY TRIP");
    u8g2_.drawStr(8, 72, "  HEATER OFF");
  } else {
    u8g2_.drawStr(8, 58, "! LIQUID PROBE");
    u8g2_.drawStr(8, 72, "  FAULT");
  }
  u8g2_.setDrawColor(1);
}

}  // namespace ui
```

- [ ] **Step 4: Build to verify it compiles.**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: `[SUCCESS]`. (If the U8g2 constructor name errors, confirm the exact SH1107 128×128 variant in `.pio/libdeps/.../U8g2/src/U8g2lib.h` and use it — see spec open item 1.)

- [ ] **Step 5: Commit.**

```bash
git add platformio.ini include/ui/Display.hpp src/ui/Display.cpp
git commit -m "feat(ui): U8g2 SH1107 128x128 Display renderer"
```

---

## Task 5: InputManager — encoder + buttons

Hardware; verified on-device.

**Files:**
- Create: `include/ui/InputManager.hpp`, `src/ui/InputManager.cpp`

**Interfaces:**
- Produces: `ui::InputManager(const Pins&, const Timing&)`, `begin()`, `UiEvent poll()`.

- [ ] **Step 1: Create `include/ui/InputManager.hpp`.**

```cpp
#pragma once
#include <stdint.h>
#include "ui/UiTypes.hpp"

namespace ui {

class InputManager {
 public:
  struct Pins { int encA; int encB; int encSw; int btn1; int btn2; int btn3; };
  struct Timing { uint16_t debounceMs = 25; uint16_t longPressMs = 600; };

  InputManager(const Pins& pins, const Timing& timing);
  void begin();
  UiEvent poll();   // returns one pending event per call, or UiEvent::None

 private:
  UiEvent pollButton(int pin, UiEvent ev, bool& downState, uint32_t& edgeMs);
  Pins pins_;
  Timing timing_;
  // encoder
  int32_t lastEncCount_ = 0;
  // buttons
  bool b1Down_ = false, b2Down_ = false, b3Down_ = false;
  uint32_t b1Ms_ = 0, b2Ms_ = 0, b3Ms_ = 0;
  // encoder switch (short/long press)
  bool swDown_ = false; uint32_t swDownMs_ = 0; bool swLongFired_ = false;
};

}  // namespace ui
```

- [ ] **Step 2: Create `src/ui/InputManager.cpp`.** Encoder uses an ISR updating a file-scope counter (one encoder in the system).

```cpp
#include "ui/InputManager.hpp"
#include <Arduino.h>

namespace ui {

namespace {
volatile int32_t g_encCount = 0;
int g_encAPin = -1, g_encBPin = -1;

void IRAM_ATTR onEncEdge() {
  // Quadrature: on A edge, B level gives direction.
  int b = digitalRead(g_encBPin);
  int a = digitalRead(g_encAPin);
  g_encCount += (a == b) ? 1 : -1;
}
}  // namespace

InputManager::InputManager(const Pins& pins, const Timing& timing)
    : pins_(pins), timing_(timing) {}

void InputManager::begin() {
  pinMode(pins_.encA, INPUT_PULLUP);
  pinMode(pins_.encB, INPUT_PULLUP);
  pinMode(pins_.encSw, INPUT_PULLUP);
  pinMode(pins_.btn1, INPUT_PULLUP);
  pinMode(pins_.btn2, INPUT_PULLUP);
  pinMode(pins_.btn3, INPUT_PULLUP);
  g_encAPin = pins_.encA;
  g_encBPin = pins_.encB;
  attachInterrupt(digitalPinToInterrupt(pins_.encA), onEncEdge, CHANGE);
  lastEncCount_ = g_encCount;
}

UiEvent InputManager::pollButton(int pin, UiEvent ev, bool& downState, uint32_t& edgeMs) {
  bool pressed = (digitalRead(pin) == LOW);   // active-low w/ pullup
  uint32_t now = millis();
  if (pressed && !downState && (now - edgeMs) > timing_.debounceMs) {
    downState = true; edgeMs = now; return ev;
  }
  if (!pressed && downState && (now - edgeMs) > timing_.debounceMs) {
    downState = false; edgeMs = now;
  }
  return UiEvent::None;
}

UiEvent InputManager::poll() {
  // 1) Encoder rotation (4 counts per detent typical -> coalesce in steps of 4).
  int32_t cur = g_encCount;
  if (cur - lastEncCount_ >= 4) { lastEncCount_ += 4; return UiEvent::RotateCW; }
  if (cur - lastEncCount_ <= -4) { lastEncCount_ -= 4; return UiEvent::RotateCCW; }

  // 2) Encoder switch: short vs long press.
  uint32_t now = millis();
  bool sw = (digitalRead(pins_.encSw) == LOW);
  if (sw && !swDown_) { swDown_ = true; swDownMs_ = now; swLongFired_ = false; }
  if (sw && swDown_ && !swLongFired_ && (now - swDownMs_) >= timing_.longPressMs) {
    swLongFired_ = true; return UiEvent::EncLongPush;
  }
  if (!sw && swDown_) {
    swDown_ = false;
    if (!swLongFired_ && (now - swDownMs_) > timing_.debounceMs) return UiEvent::EncShortPush;
  }

  // 3) Buttons.
  UiEvent e;
  if ((e = pollButton(pins_.btn1, UiEvent::Btn1, b1Down_, b1Ms_)) != UiEvent::None) return e;
  if ((e = pollButton(pins_.btn2, UiEvent::Btn2, b2Down_, b2Ms_)) != UiEvent::None) return e;
  if ((e = pollButton(pins_.btn3, UiEvent::Btn3, b3Down_, b3Ms_)) != UiEvent::None) return e;

  return UiEvent::None;
}

}  // namespace ui
```

- [ ] **Step 3: Build to verify it compiles.**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: `[SUCCESS]`.

- [ ] **Step 4: Commit.**

```bash
git add include/ui/InputManager.hpp src/ui/InputManager.cpp
git commit -m "feat(ui): InputManager (encoder ISR + debounced buttons -> events)"
```

---

## Task 6: AppRuntime integration + config

Hardware; verified on-device. Wires the module into the running system.

**Files:**
- Modify: `include/app_config.hpp`
- Modify: `src/system/AppRuntime.cpp`

**Interfaces:**
- Consumes: all of Tasks 1–5.

- [ ] **Step 1: Add the `Ui` config namespace** to `include/app_config.hpp` (after the `Sense` namespace):

```cpp
// ── Front-panel UI (OLED + encoder + 3 buttons) ──────────────────────────────
namespace Ui {
static constexpr uint8_t kDisplayI2cAddr = 0x3C;   // SH1107 on the shared I2C bus
static constexpr int kEncAPin  = 8;    // GPIO8
static constexpr int kEncBPin  = 9;    // GPIO9
static constexpr int kEncSwPin = 41;   // GPIO41 (push)
static constexpr int kBtn1Pin  = 47;   // GPIO47 — motor pause
static constexpr int kBtn2Pin  = 48;   // GPIO48 — motor + heater hold
static constexpr int kBtn3Pin  = 4;    // GPIO4  — info
static constexpr float kTargetStepC = 0.5f;
static constexpr float kRpmStep     = 0.5f;
static constexpr float kTargetMinC  = 0.0f;
static constexpr float kTargetMaxC  = 55.0f;   // matches Thermal::kProcessMaxC
static constexpr uint32_t kRedrawIntervalMs = 66;  // ~15 Hz
}  // namespace Ui
```

- [ ] **Step 2: Add includes + the ReactorControl adapter and UI globals** in `src/system/AppRuntime.cpp`. Near the other `#include`s:

```cpp
#include "ui/Display.hpp"
#include "ui/InputManager.hpp"
#include "ui/UiController.hpp"
```
After `ThermalController g_thermal(...)` / `Reactor g_reactor(...)` are declared, add:

```cpp
struct ReactorControlAdapter : ui::ReactorControl {
  Reactor& r;
  explicit ReactorControlAdapter(Reactor& reactor) : r(reactor) {}
  void startRun(float t, float rpm) override { r.start(t, rpm, 0); }
  void stopRun() override { r.stop(); }
  void setTargetC(float c) override { r.setTargetC(c); }
  void setRpm(float rpm) override { r.setRpm(rpm); }
  void setMotorPaused(bool on) override { r.setMotorPaused(on); }
  void setFullHold(bool on) override { r.setFullHold(on); }
};

ui::UiController::Config makeUiConfig() {
  ui::UiController::Config c;
  c.minRpm = AppConfig::Process::kMinRpm;
  c.maxRpm = AppConfig::Process::kMaxRpm;
  c.minTargetC = AppConfig::Ui::kTargetMinC;
  c.maxTargetC = AppConfig::Ui::kTargetMaxC;
  c.targetStepC = AppConfig::Ui::kTargetStepC;
  c.rpmStep = AppConfig::Ui::kRpmStep;
  return c;
}

ReactorControlAdapter g_uiControl(g_reactor);
ui::UiController g_ui(g_uiControl, makeUiConfig());
ui::Display g_display(AppConfig::Ui::kDisplayI2cAddr, AppConfig::I2c::kSclPin, AppConfig::I2c::kSdaPin);
ui::InputManager g_input(
    {AppConfig::Ui::kEncAPin, AppConfig::Ui::kEncBPin, AppConfig::Ui::kEncSwPin,
     AppConfig::Ui::kBtn1Pin, AppConfig::Ui::kBtn2Pin, AppConfig::Ui::kBtn3Pin},
    {});
```

- [ ] **Step 3: Add a snapshot builder** in `src/system/AppRuntime.cpp` (near `buildStatusJson`):

```cpp
ui::ReactorSnapshot buildUiSnapshot() {
  const ReactorTelemetry t = g_reactor.telemetry();
  ui::ReactorSnapshot s;
  s.running = t.running;
  s.motorPaused = t.motorPaused;
  s.fullHold = t.fullHold;
  s.sensorFault = t.sensorFault;
  s.safetyTripped = t.safetyTripped;
  s.liquidTempC = t.liquidTempC;
  s.setpointC = t.setpointC;
  s.rpm = t.rpm;
  s.rpmSetpoint = g_reactor.rpmSetpoint();
  s.heaterDutyPct = t.heaterDutyPct;
  s.elapsedSec = t.elapsedSec;
  s.wifiConnected = g_wifi.staConnected();
  static String ssid, ip;       // keep backing storage alive for the const char*
  ssid = WiFi.SSID(); ip = g_wifi.ipAddress();
  s.wifiSsid = ssid.c_str();
  s.ip = ip.c_str();
  s.rssi = g_wifi.staConnected() ? WiFi.RSSI() : 0;
  s.sdMounted = g_sd.mounted();
  s.firmware = AppConfig::kFirmwareVersion;
  return s;
}
```

- [ ] **Step 4: Initialize the panel in setup, after `Wire.begin(...)`.** Find the `Wire.begin(AppConfig::I2c::kSdaPin, AppConfig::I2c::kSclPin, ...)` call in the setup path and add after it:

```cpp
  g_input.begin();
  g_display.begin();
  Serial.printf("[UI] OLED %s\n", g_display.present() ? "detected" : "absent (headless)");
```

- [ ] **Step 5: Pump input + throttled render in the loop.** In the main loop function (where `g_thermal.update(); g_reactor.update();` are called) add:

```cpp
  {
    ui::ReactorSnapshot snap = buildUiSnapshot();
    for (ui::UiEvent e = g_input.poll(); e != ui::UiEvent::None; e = g_input.poll())
      g_ui.handle(e, snap);
    static uint32_t lastDrawMs = 0;
    if (millis() - lastDrawMs >= AppConfig::Ui::kRedrawIntervalMs) {
      lastDrawMs = millis();
      g_display.render(g_ui, snap);
    }
  }
```

- [ ] **Step 6: Build to verify it compiles.**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: `[SUCCESS]`.

- [ ] **Step 7: Run the full native test suite (no regressions).**

Run: `~/.platformio/penv/bin/pio test -e native`
Expected: all suites pass, including `test_ui_controller`.

- [ ] **Step 8: Commit.**

```bash
git add include/app_config.hpp src/system/AppRuntime.cpp
git commit -m "feat(ui): wire OLED UI + input into AppRuntime loop"
```

---

## Task 7: On-device verification

**Files:** none (hardware validation).

- [ ] **Step 1: Flash.**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 -t upload --upload-port /dev/cu.usbmodem8401`
Expected: `[SUCCESS]`; serial prints `[UI] OLED detected` (if absent, run an I2C scan and confirm address/wiring — spec open items 1–2).

- [ ] **Step 2: Verify the dashboard** shows live liquid temperature (cross-check against `curl -s http://10.77.77.250/api/v1/status`), RPM setpoint, and elapsed time once running.

- [ ] **Step 3: Verify navigation** — encoder rotate moves the menu cursor; short-push opens the menu / selects; long-push returns Home from any screen; B3 toggles Info.

- [ ] **Step 4: Verify control** — edit Target and Speed, confirm the applied values appear in `/api/v1/status` (`thermal.setpointC`, `disc.rpmSetpoint`); Start/Stop from the menu toggles `run.active`.

- [ ] **Step 5: Verify holds** — with a run active: **B1** stops the disc while heater duty continues; **B2** stops the disc AND drives heater duty to 0 (`thermal.heaterPct → 0`); pressing each again resumes; elapsed time keeps counting throughout. Confirm against `/api/v1/status`.

- [ ] **Step 6: Verify fault overlay** — unplug the DS18B20 (or trip safety) and confirm the banner takes over the screen, then clears when restored.

---

## Self-review notes

- **Spec coverage:** dashboard/menu/edit/info/fault-overlay (Tasks 2–4), encoder+buttons incl. long-press Home (Tasks 2,5), B1/B2/B3 mapping (Tasks 2,3,5), pause model + timer-always-counts + heater inhibit (Task 1), shared-I2C/pin-collision constraint (Task 4/6), host tests for the pure controller (Tasks 2–3), on-device checks for hardware (Task 7). Advanced config correctly omitted (out of scope).
- **Type consistency:** `ReactorControl`/`ReactorSnapshot`/`UiController` signatures are identical across Tasks 2, 3, 6; `setMotorPaused`/`setFullHold`/`setInhibited` names match between Task 1 and the adapter in Task 6.
- **Open items carried from spec** (resolve during Task 4/7): exact U8g2 SH1107 constructor variant; I2C address 0x3C vs 0x3D; hero font sizing; encoder pull-ups.
