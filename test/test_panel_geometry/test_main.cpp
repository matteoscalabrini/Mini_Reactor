#include <unity.h>
#include "features/hub/display/PanelGeometry.hpp"
void setUp() {} void tearDown() {}
void test_madctl_rotations() {
  TEST_ASSERT_EQUAL_HEX8(0x00, geom::rotationMadctl(0));
  TEST_ASSERT_EQUAL_HEX8(0x02, geom::rotationMadctl(1));
  TEST_ASSERT_EQUAL_HEX8(0x07, geom::rotationMadctl(2));
  TEST_ASSERT_EQUAL_HEX8(0x05, geom::rotationMadctl(3));
}
void test_col_offset_rot0() { TEST_ASSERT_EQUAL_UINT16(6, geom::columnOffset(0)); }
int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_madctl_rotations);
  RUN_TEST(test_col_offset_rot0);
  return UNITY_END();
}
