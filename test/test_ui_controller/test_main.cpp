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
