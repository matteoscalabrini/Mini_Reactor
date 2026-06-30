#include <unity.h>
#include "features/hub/touch/HubTouchTransform.hpp"
void setUp() {} void tearDown() {}
void test_identity() {
  int16_t x = 10, y = 20; HubTouchTransform::Mapping m{false,false,false};
  HubTouchTransform::apply(x, y, 466, 466, m);
  TEST_ASSERT_EQUAL_INT16(10, x); TEST_ASSERT_EQUAL_INT16(20, y);
}
void test_swap_and_mirror() {
  int16_t x = 0, y = 0; HubTouchTransform::Mapping m{true,true,true};  // swapXY, mirrorX, mirrorY
  HubTouchTransform::apply(x, y, 466, 466, m);   // swap->(0,0); mirrorX->465; mirrorY->465
  TEST_ASSERT_EQUAL_INT16(465, x); TEST_ASSERT_EQUAL_INT16(465, y);
}
int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_identity);
  RUN_TEST(test_swap_and_mirror);
  return UNITY_END();
}
