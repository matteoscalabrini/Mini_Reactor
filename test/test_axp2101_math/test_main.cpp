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

void test_encode_input_voltage_reg() {
  // base 3880 mV, step 80 mV/LSB (AXP2101 reg 0x15)
  // 4360 mV: (4360-3880)/80 = 480/80 = 6  (was 2 with the cast-before-divide bug)
  TEST_ASSERT_EQUAL_UINT8(6, axp::encodeInputVoltageReg(4360));
  // boundary: minimum clamp → 0
  TEST_ASSERT_EQUAL_UINT8(0, axp::encodeInputVoltageReg(3880));
  // below minimum clamps to 0
  TEST_ASSERT_EQUAL_UINT8(0, axp::encodeInputVoltageReg(3000));
  // maximum: (5080-3880)/80 = 1200/80 = 15
  TEST_ASSERT_EQUAL_UINT8(15, axp::encodeInputVoltageReg(5080));
  // above maximum clamps to 15
  TEST_ASSERT_EQUAL_UINT8(15, axp::encodeInputVoltageReg(6000));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_temperature_decode);
  RUN_TEST(test_voltage_scale_passthrough_mv);
  RUN_TEST(test_encode_input_voltage_reg);
  return UNITY_END();
}
