#include <unity.h>
#include "motor/DrvStatus.hpp"

void setUp() {}
void tearDown() {}

void test_all_clear() {
  DrvStatusFlags f = decodeDrvStatus(0x00000000u);
  TEST_ASSERT_FALSE(f.otpw); TEST_ASSERT_FALSE(f.ot);
  TEST_ASSERT_FALSE(f.openLoadA); TEST_ASSERT_FALSE(f.openLoadB);
  TEST_ASSERT_FALSE(f.shortA); TEST_ASSERT_FALSE(f.shortB);
  TEST_ASSERT_FALSE(f.stall);  // decode never sets stall (DIAG-pin sourced)
}

void test_otpw_and_ot() {
  TEST_ASSERT_TRUE(decodeDrvStatus(1u << 0).otpw);
  TEST_ASSERT_TRUE(decodeDrvStatus(1u << 1).ot);
}

void test_open_load() {
  TEST_ASSERT_TRUE(decodeDrvStatus(1u << 6).openLoadA);  // ola
  TEST_ASSERT_TRUE(decodeDrvStatus(1u << 7).openLoadB);  // olb
}

void test_short_combines_s2g_and_s2vs() {
  TEST_ASSERT_TRUE(decodeDrvStatus(1u << 2).shortA);  // s2ga
  TEST_ASSERT_TRUE(decodeDrvStatus(1u << 4).shortA);  // s2vsa
  TEST_ASSERT_TRUE(decodeDrvStatus(1u << 3).shortB);  // s2gb
  TEST_ASSERT_TRUE(decodeDrvStatus(1u << 5).shortB);  // s2vsb
}

void test_combined() {
  DrvStatusFlags f = decodeDrvStatus((1u<<0) | (1u<<1) | (1u<<6) | (1u<<7));
  TEST_ASSERT_TRUE(f.otpw); TEST_ASSERT_TRUE(f.ot);
  TEST_ASSERT_TRUE(f.openLoadA); TEST_ASSERT_TRUE(f.openLoadB);
  TEST_ASSERT_FALSE(f.shortA); TEST_ASSERT_FALSE(f.shortB);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_all_clear);
  RUN_TEST(test_otpw_and_ot);
  RUN_TEST(test_open_load);
  RUN_TEST(test_short_combines_s2g_and_s2vs);
  RUN_TEST(test_combined);
  return UNITY_END();
}
