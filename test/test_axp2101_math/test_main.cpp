#include <unity.h>
#include "features/hub/power/Axp2101Math.hpp"

void setUp() {}
void tearDown() {}

void test_temperature_decode() {
  // Barebone formula: 22.0 + (7274 - raw)/20.0 ; raw=7274 => 22.0C
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 22.0f, axp::decodeTemperatureC(7274));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 32.0f, axp::decodeTemperatureC(7074)); // (7274-7074)/20=10
}

void test_voltage_scale_passthrough_mv() {
  // 11-bit ADC, 1 mV/LSB nominal
  TEST_ASSERT_EQUAL_UINT16(4200, axp::scaleVoltageMv(4200));
  TEST_ASSERT_EQUAL_UINT16(0, axp::scaleVoltageMv(0));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_temperature_decode);
  RUN_TEST(test_voltage_scale_passthrough_mv);
  return UNITY_END();
}
