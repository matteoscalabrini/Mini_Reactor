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

void test_menu_rotate_cw_wraps_forward() {
  FakeControl fc; UiController ui(fc, cfg()); ReactorSnapshot s;
  ui.handle(UiEvent::EncShortPush, s);          // -> Menu, index = kMenuStartStop (0)
  ui.handle(UiEvent::RotateCCW, s);             // wrap to kMenuBack (last item)
  TEST_ASSERT_EQUAL_INT(kMenuBack, ui.menuIndex());
  ui.handle(UiEvent::RotateCW, s);              // CW from last -> wraps to first
  TEST_ASSERT_EQUAL_INT(kMenuStartStop, ui.menuIndex());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_dashboard_push_opens_menu);
  RUN_TEST(test_menu_rotate_wraps);
  RUN_TEST(test_long_press_returns_home_from_menu);
  RUN_TEST(test_btn3_toggles_info);
  RUN_TEST(test_info_push_returns_home);
  RUN_TEST(test_menu_start_when_stopped_calls_startRun);
  RUN_TEST(test_menu_stop_when_running_calls_stopRun);
  RUN_TEST(test_edit_target_adjust_and_apply);
  RUN_TEST(test_edit_target_clamped_high);
  RUN_TEST(test_edit_speed_clamped_low);
  RUN_TEST(test_btn1_toggles_motor_pause_only_when_running);
  RUN_TEST(test_btn2_toggles_full_hold_off_when_already_held);
  RUN_TEST(test_menu_rotate_cw_wraps_forward);
  return UNITY_END();
}
