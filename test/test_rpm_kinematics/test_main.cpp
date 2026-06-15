#include <unity.h>
#include "motor/RpmKinematics.hpp"

void setUp() {}
void tearDown() {}

// 8 rpm, 200 full steps/rev, 16 microsteps → (8/60)*200*16 = 426.667 µsteps/s
void test_rpm_to_microstep_hz() {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 426.6667f,
      RpmKinematics::rpmToMicrostepHz(8.0f, 200, 16));
}

// 426.667 µsteps/s → VACTUAL = round(426.667 * 2^24 / 12e6) = 597
void test_rpm_to_vactual_8rpm() {
  TEST_ASSERT_EQUAL_INT32(597, RpmKinematics::rpmToVactual(8.0f, 200, 16));
}

void test_rpm_to_vactual_zero_is_zero() {
  TEST_ASSERT_EQUAL_INT32(0, RpmKinematics::rpmToVactual(0.0f, 200, 16));
}

void test_clamp_rpm() {
  TEST_ASSERT_EQUAL_FLOAT(0.0f, RpmKinematics::clampRpm(0.0f, 0.5f, 30.0f));   // stop allowed
  TEST_ASSERT_EQUAL_FLOAT(0.5f, RpmKinematics::clampRpm(0.2f, 0.5f, 30.0f));   // up to min
  TEST_ASSERT_EQUAL_FLOAT(30.0f, RpmKinematics::clampRpm(50.0f, 0.5f, 30.0f)); // down to max
  TEST_ASSERT_EQUAL_FLOAT(8.0f, RpmKinematics::clampRpm(8.0f, 0.5f, 30.0f));   // in range
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_rpm_to_microstep_hz);
  RUN_TEST(test_rpm_to_vactual_8rpm);
  RUN_TEST(test_rpm_to_vactual_zero_is_zero);
  RUN_TEST(test_clamp_rpm);
  return UNITY_END();
}
