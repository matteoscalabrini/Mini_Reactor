#pragma once
#include <cstdint>
#include <cmath>
#include "features/hub/touch/HubTouchTransform.hpp"

// Pure-math touch calibration solver — no Arduino/LVGL deps, host-testable.
// Ported from the barebone hub (AppFeatureRuntime::solveHubTouchBootCalibration):
// tap 4 targets on an orbit, then brute-force the 8 swap/mirror permutations and
// keep the one whose mapped taps best fit the shown targets (least squared error).
namespace TouchCalibrationSolver {

struct Point { int16_t x = 0; int16_t y = 0; };
struct Tap   { bool valid = false; int16_t rawX = 0; int16_t rawY = 0; };

// The i-th target (0=top, 1=right, 2=bottom, 3=left) on the orbit of a
// width x height panel. Matches barebone hubTouchCalibrationTarget().
inline Point targetForStep(uint8_t step, uint16_t width, uint16_t height,
                           uint16_t orbitRadius) {
  const int16_t cx = static_cast<int16_t>(width / 2);
  const int16_t cy = static_cast<int16_t>(height / 2);
  const int16_t r  = static_cast<int16_t>(orbitRadius);
  switch (step & 0x3U) {
    case 0:  return { cx, static_cast<int16_t>(cy - r) };  // top
    case 1:  return { static_cast<int16_t>(cx + r), cy };  // right
    case 2:  return { cx, static_cast<int16_t>(cy + r) };  // bottom
    default: return { static_cast<int16_t>(cx - r), cy };  // left
  }
}

// Returns false if any tap is invalid. On success outMapping is the best-fit
// swap/mirror and outScore is its summed squared reprojection error (lower=better).
inline bool solve(const Tap* taps, uint8_t stepCount, uint16_t width,
                  uint16_t height, uint16_t orbitRadius,
                  HubTouchTransform::Mapping& outMapping, float& outScore) {
  // All 8 permutations of {swapXY, mirrorX, mirrorY} — same order as barebone.
  const HubTouchTransform::Mapping candidates[] = {
      {false, false, false}, {false, true, false},
      {false, false, true},  {false, true, true},
      {true, false, false},  {true, true, false},
      {true, false, true},   {true, true, true},
  };
  float bestScore = INFINITY;
  HubTouchTransform::Mapping best = {false, false, false};
  for (const HubTouchTransform::Mapping& cand : candidates) {
    float score = 0.0f;
    for (uint8_t i = 0; i < stepCount; ++i) {
      if (!taps[i].valid) return false;
      int16_t mx = taps[i].rawX;
      int16_t my = taps[i].rawY;
      HubTouchTransform::apply(mx, my, width, height, cand);
      const Point t = targetForStep(i, width, height, orbitRadius);
      const float dx = static_cast<float>(mx - t.x);
      const float dy = static_cast<float>(my - t.y);
      score += (dx * dx) + (dy * dy);
    }
    if (score < bestScore) { bestScore = score; best = cand; }
  }
  outMapping = best;
  outScore = bestScore;
  return std::isfinite(bestScore);
}

}  // namespace TouchCalibrationSolver
