#include "features/hub/touch/TouchCalibration.hpp"
#include <Arduino.h>
#include <cmath>
#include "features/hub/touch/Cst9217.hpp"
#include "features/hub/ui/CalibrationScreen.hpp"

// State machine ported from barebone AppFeatureRuntime::updateHubTouchBootCalibration,
// re-pointed to Cst9217 + CalibrationScreen and gated on pointCount>0 (see header).

void TouchCalibration::begin(Cst9217& touch) {
  touch_ = &touch;
  active_ = true;
  touchActive_ = false;
  stepIndex_ = 0;
  sumX_ = sumY_ = 0;
  samples_ = 0;
  for (TouchCalibrationSolver::Tap& t : taps_) t = TouchCalibrationSolver::Tap{};
  CalibrationScreen::create();
  showCurrentStep();
  Serial.println("[HUB] touch: calibration wizard started");
}

void TouchCalibration::showCurrentStep() {
  const TouchCalibrationSolver::Point t = TouchCalibrationSolver::targetForStep(
      stepIndex_, AppConfig::Hub::kTouchWidth, AppConfig::Hub::kTouchHeight,
      AppConfig::HubTouchCalibration::kTargetOrbitRadiusPx);
  CalibrationScreen::showStep(stepIndex_, AppConfig::HubTouchCalibration::kStepCount,
                              t.x, t.y);
}

void TouchCalibration::tick(bool pressed, int16_t rawX, int16_t rawY) {
  if (!active_) return;

  if (pressed) {  // accumulate raw samples while the finger is down
    sumX_ += rawX;
    sumY_ += rawY;
    samples_ += 1;
    touchActive_ = true;
    return;
  }

  if (!touchActive_) return;  // released with nothing collected — waiting for a tap
  if (samples_ < AppConfig::HubTouchCalibration::kMinSamplesPerStep) {
    touchActive_ = false; sumX_ = sumY_ = 0; samples_ = 0;  // ignore ultra-brief noise
    return;
  }

  TouchCalibrationSolver::Tap& tap = taps_[stepIndex_];
  tap.valid = true;
  tap.rawX = static_cast<int16_t>(sumX_ / static_cast<int32_t>(samples_));
  tap.rawY = static_cast<int16_t>(sumY_ / static_cast<int32_t>(samples_));
  Serial.printf("[HUB] touch: calibration step %u raw=(%d,%d)\n",
                (unsigned)(stepIndex_ + 1), (int)tap.rawX, (int)tap.rawY);

  stepIndex_ += 1;
  touchActive_ = false; sumX_ = sumY_ = 0; samples_ = 0;

  if (stepIndex_ < AppConfig::HubTouchCalibration::kStepCount) {
    showCurrentStep();
    return;
  }

  // All targets tapped — solve for the best swap/mirror and persist it.
  HubTouchTransform::Mapping m;
  float score = 0.0f;
  if (TouchCalibrationSolver::solve(
          taps_, AppConfig::HubTouchCalibration::kStepCount,
          AppConfig::Hub::kTouchWidth, AppConfig::Hub::kTouchHeight,
          AppConfig::HubTouchCalibration::kTargetOrbitRadiusPx, m, score)) {
    const bool ok = touch_->applyMapping(m.swapXY, m.mirrorX, m.mirrorY, true);
    Serial.printf("[HUB] touch: calibration %s swap=%u mirrorX=%u mirrorY=%u score=%ld\n",
                  ok ? "saved" : "runtime-only", (unsigned)m.swapXY,
                  (unsigned)m.mirrorX, (unsigned)m.mirrorY, (long)lroundf(score));
  } else {
    Serial.println("[HUB] touch: calibration solve failed");
  }

  CalibrationScreen::close();
  active_ = false;
}
