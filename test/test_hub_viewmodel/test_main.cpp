#include <unity.h>
#include "features/hub/ui/HubViewModel.hpp"
using namespace hubui;

void setUp() {} void tearDown() {}

void test_hero_state_priority() {
  // fault (safety) beats heating
  TEST_ASSERT_EQUAL(HeroState::Fault,   heroState(true,  true,  false, true));
  // invalid temp is a fault
  TEST_ASSERT_EQUAL(HeroState::Fault,   heroState(false, false, false, false));
  // probe fault is a fault
  TEST_ASSERT_EQUAL(HeroState::Fault,   heroState(true,  false, true,  false));
  // heating when valid and no fault
  TEST_ASSERT_EQUAL(HeroState::Heating, heroState(true,  false, false, true));
  // steady/at-target otherwise
  TEST_ASSERT_EQUAL(HeroState::AtTarget,heroState(true,  false, false, false));
}

void test_hero_colors() {
  Rgb c = heroColor(HeroState::AtTarget);
  TEST_ASSERT_EQUAL_UINT8(0x00, c.r); TEST_ASSERT_EQUAL_UINT8(0xb4, c.g); TEST_ASSERT_EQUAL_UINT8(0xd8, c.b);
  Rgb a = heroColor(HeroState::Heating);
  TEST_ASSERT_EQUAL_UINT8(0xff, a.r); TEST_ASSERT_EQUAL_UINT8(0xb0, a.g); TEST_ASSERT_EQUAL_UINT8(0x20, a.b);
  Rgb f = heroColor(HeroState::Fault);
  TEST_ASSERT_EQUAL_UINT8(0xff, f.r); TEST_ASSERT_EQUAL_UINT8(0x4d, f.g); TEST_ASSERT_EQUAL_UINT8(0x4f, f.b);
}

void test_format_hms() {
  char b[16];
  TEST_ASSERT_EQUAL_STRING("6h 02",  formatHms(6*3600 + 2*60 + 9, b, sizeof(b)));
  TEST_ASSERT_EQUAL_STRING("0h 00",  formatHms(0, b, sizeof(b)));
  TEST_ASSERT_EQUAL_STRING("41h 58", formatHms(41*3600 + 58*60, b, sizeof(b)));
}

void test_format_remaining() {
  char b[16];
  TEST_ASSERT_EQUAL_STRING("until stop", formatRemaining(-1, b, sizeof(b)));
  TEST_ASSERT_EQUAL_STRING("0h 05",      formatRemaining(5*60, b, sizeof(b)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_hero_state_priority);
  RUN_TEST(test_hero_colors);
  RUN_TEST(test_format_hms);
  RUN_TEST(test_format_remaining);
  return UNITY_END();
}
