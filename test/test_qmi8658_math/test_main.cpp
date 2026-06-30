#include <unity.h>
#include "features/hub/sensor/Qmi8658Math.hpp"
void setUp() {} void tearDown() {}
void test_s16_little_endian() {
  uint8_t b[2] = {0x34, 0x12};
  TEST_ASSERT_EQUAL_INT16(0x1234, qmi::decodeS16(b));
}
void test_temp_decode() { TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, qmi::decodeTemperatureC(256)); }
void test_accel_sensitivity() { TEST_ASSERT_FLOAT_WITHIN(1e-7f, 4.0f/32768.0f, qmi::accelSensitivityGPerLsb(4)); }
int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_s16_little_endian);
  RUN_TEST(test_temp_decode);
  RUN_TEST(test_accel_sensitivity);
  return UNITY_END();
}
