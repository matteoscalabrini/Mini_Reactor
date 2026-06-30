#include <unity.h>
#include "features/hub/rtc/Pcf85063Bcd.hpp"
void setUp() {} void tearDown() {}
void test_bcd_to_uint() { TEST_ASSERT_EQUAL_UINT8(42, rtc::bcdToUint(0x42)); }
void test_uint_to_bcd() { TEST_ASSERT_EQUAL_HEX8(0x42, rtc::uintToBcd(42)); }
void test_roundtrip() { for (uint8_t v=0; v<60; ++v) TEST_ASSERT_EQUAL_UINT8(v, rtc::bcdToUint(rtc::uintToBcd(v))); }
int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_bcd_to_uint);
  RUN_TEST(test_uint_to_bcd);
  RUN_TEST(test_roundtrip);
  return UNITY_END();
}
