/*
 * RpmKinematics.hpp — pure disc-rpm ↔ TMC2209 VACTUAL conversions.
 *
 * Header-only and dependency-free (no Arduino/TMCStepper) so the math is
 * host-unit-testable. Direct 1:1 drivetrain: disc rpm == motor rpm.
 *   µsteps/s = (rpm / 60) * stepsPerRev * microsteps
 *   VACTUAL  = round(µsteps/s * 2^24 / fCLK)        (fCLK = 12 MHz internal)
 */
#pragma once

#include <cmath>
#include <cstdint>

namespace RpmKinematics {

inline float rpmToMicrostepHz(float rpm, uint16_t stepsPerRev, uint16_t microsteps) {
  return (rpm / 60.0f) * static_cast<float>(stepsPerRev) * static_cast<float>(microsteps);
}

inline int32_t microstepHzToVactual(float microstepHz, float fclkHz = 12000000.0f) {
  return static_cast<int32_t>(std::lround(microstepHz * 16777216.0f / fclkHz));
}

inline int32_t rpmToVactual(float rpm, uint16_t stepsPerRev, uint16_t microsteps,
                            float fclkHz = 12000000.0f) {
  return microstepHzToVactual(rpmToMicrostepHz(rpm, stepsPerRev, microsteps), fclkHz);
}

// 0 (or negative) means "stopped"; any positive rpm is clamped into [minRpm,maxRpm].
inline float clampRpm(float rpm, float minRpm, float maxRpm) {
  if (rpm <= 0.0f) return 0.0f;
  if (rpm < minRpm) return minRpm;
  if (rpm > maxRpm) return maxRpm;
  return rpm;
}

}  // namespace RpmKinematics
