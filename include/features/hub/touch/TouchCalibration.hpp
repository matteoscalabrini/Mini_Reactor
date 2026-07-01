#pragma once
#include <cstdint>
#include "app_config.hpp"
#include "features/hub/touch/TouchCalibrationSolver.hpp"

class Cst9217;

// Boot touch-calibration wizard controller. Drives CalibrationScreen, accumulates
// averaged raw taps per target, solves the swap/mirror transform, and persists it
// via Cst9217. Touch-down is gated on pointCount>0 — this panel does not assert the
// CST9217 "valid"/event==0x06 flag on a normal press (see commit 6d77998), so the
// barebone's valid-gated collection would never advance here.
class TouchCalibration {
 public:
  void begin(Cst9217& touch);                           // start wizard: show step 0
  void tick(bool pressed, int16_t rawX, int16_t rawY);  // feed one raw touch sample
  bool active() const { return active_; }

 private:
  void showCurrentStep();

  Cst9217* touch_ = nullptr;
  bool active_ = false;
  bool touchActive_ = false;
  uint8_t stepIndex_ = 0;
  int32_t sumX_ = 0, sumY_ = 0;
  uint16_t samples_ = 0;
  TouchCalibrationSolver::Tap taps_[AppConfig::HubTouchCalibration::kStepCount] = {};
};
