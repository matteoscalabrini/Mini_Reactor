#include <unity.h>
#include "features/control/PidController.hpp"

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
