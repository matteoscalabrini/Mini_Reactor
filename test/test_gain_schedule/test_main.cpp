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
