#include <unity.h>
#include "system/AlarmTracker.hpp"

void setUp() {}
void tearDown() {}

void test_since_is_set_on_first_appearance() {
  AlarmTracker t;
  t.beginFrame(10);
  t.add("sensor_fault", "warn");
  t.endFrame();
  TEST_ASSERT_EQUAL_INT(1, t.size());
  TEST_ASSERT_EQUAL_UINT32(10, t[0].sinceSec);
}

void test_since_persists_across_frames() {
  AlarmTracker t;
  t.beginFrame(10); t.add("sensor_fault", "warn"); t.endFrame();
  t.beginFrame(25); t.add("sensor_fault", "warn"); t.endFrame();
  TEST_ASSERT_EQUAL_INT(1, t.size());
  TEST_ASSERT_EQUAL_UINT32(10, t[0].sinceSec);  // unchanged
}

void test_cleared_when_not_reported() {
  AlarmTracker t;
  t.beginFrame(10); t.add("sensor_fault", "warn"); t.endFrame();
  t.beginFrame(20); t.endFrame();  // not reported this frame
  TEST_ASSERT_EQUAL_INT(0, t.size());
}

void test_reappearance_gets_new_since() {
  AlarmTracker t;
  t.beginFrame(10); t.add("sensor_fault", "warn"); t.endFrame();
  t.beginFrame(20); t.endFrame();
  t.beginFrame(30); t.add("sensor_fault", "warn"); t.endFrame();
  TEST_ASSERT_EQUAL_UINT32(30, t[0].sinceSec);
}

void test_multiple_independent_since() {
  AlarmTracker t;
  t.beginFrame(10); t.add("a", "warn"); t.endFrame();
  t.beginFrame(20); t.add("a", "warn"); t.add("b", "critical"); t.endFrame();
  TEST_ASSERT_EQUAL_INT(2, t.size());
  // 'a' kept since 10, 'b' since 20 — find each regardless of order
  for (int i = 0; i < t.size(); ++i) {
    if (std::strcmp(t[i].code, "a") == 0) TEST_ASSERT_EQUAL_UINT32(10, t[i].sinceSec);
    if (std::strcmp(t[i].code, "b") == 0) TEST_ASSERT_EQUAL_UINT32(20, t[i].sinceSec);
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_since_is_set_on_first_appearance);
  RUN_TEST(test_since_persists_across_frames);
  RUN_TEST(test_cleared_when_not_reported);
  RUN_TEST(test_reappearance_gets_new_since);
  RUN_TEST(test_multiple_independent_since);
  return UNITY_END();
}
