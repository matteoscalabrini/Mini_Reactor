#include <unity.h>
#include "features/hub/touch/TouchCalibrationSolver.hpp"
#include "features/hub/touch/HubTouchTransform.hpp"

// Panel/orbit geometry matching AppConfig (kept literal so the solver stays a pure,
// host-testable unit with no app_config/Arduino dependency).
static constexpr uint16_t W = 466, H = 466, ORBIT = 174;
static constexpr uint8_t  N = 4;

void setUp() {}
void tearDown() {}

// For every physical panel orientation, synthetic taps (the raw a finger on each
// target would produce under that orientation) must be solved back to a transform
// that reprojects them exactly onto the shown targets.
void test_all_orientations_recovered() {
  const HubTouchTransform::Mapping all[] = {
      {false, false, false}, {false, true, false},
      {false, false, true},  {false, true, true},
      {true, false, false},  {true, true, false},
      {true, false, true},   {true, true, true},
  };
  for (const HubTouchTransform::Mapping& trueMap : all) {
    TouchCalibrationSolver::Tap taps[N];
    for (uint8_t i = 0; i < N; ++i) {
      const TouchCalibrationSolver::Point t =
          TouchCalibrationSolver::targetForStep(i, W, H, ORBIT);
      int16_t rx = t.x, ry = t.y;
      HubTouchTransform::apply(rx, ry, W, H, trueMap);  // raw that maps to target
      taps[i] = TouchCalibrationSolver::Tap{true, rx, ry};
    }
    HubTouchTransform::Mapping out;
    float score = -1.0f;
    TEST_ASSERT_TRUE(TouchCalibrationSolver::solve(taps, N, W, H, ORBIT, out, score));
    TEST_ASSERT_TRUE(score < 1.0f);  // exact fit (near zero)
    for (uint8_t i = 0; i < N; ++i) {
      const TouchCalibrationSolver::Point t =
          TouchCalibrationSolver::targetForStep(i, W, H, ORBIT);
      int16_t mx = taps[i].rawX, my = taps[i].rawY;
      HubTouchTransform::apply(mx, my, W, H, out);
      TEST_ASSERT_EQUAL_INT16(t.x, mx);
      TEST_ASSERT_EQUAL_INT16(t.y, my);
    }
  }
}

// A missing tap makes the solve fail rather than persist a garbage transform.
void test_missing_tap_fails() {
  TouchCalibrationSolver::Tap taps[N] = {};
  taps[0].valid = true;  // steps 1..3 never captured
  HubTouchTransform::Mapping out;
  float score = 0.0f;
  TEST_ASSERT_FALSE(TouchCalibrationSolver::solve(taps, N, W, H, ORBIT, out, score));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_all_orientations_recovered);
  RUN_TEST(test_missing_tap_fails);
  return UNITY_END();
}
