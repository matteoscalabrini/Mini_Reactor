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
