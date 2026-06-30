#pragma once
#include <cstdint>

// Pure FSM for hub deep-sleep decisions. No ESP-IDF / Arduino / app_config deps —
// header-only so the native host test needs no extra source files.
// Phase-2 wake-service staging (ESP-NOW/WiFi) is omitted (scope: Phase 1 only).

namespace hubsleep {

enum class WakeSource : uint8_t { kUnknown = 0, kTouch, kImu, kButton, kTimer };

struct SleepInputs {
  bool externalPowerPresent = false;
  bool touchActive          = false;
  uint32_t idleMs           = 0;
  uint32_t idleThresholdMs  = 0;  // caller fills from AppConfig::HubSleep::kDeepSleepAfterMs
};

inline bool shouldEnterDeepSleep(const SleepInputs& in) {
  if (in.externalPowerPresent) return false;
  if (in.touchActive) return false;
  return in.idleMs >= in.idleThresholdMs;
}

// Storm guard. StormState must be placed in RTC-retained memory by the caller;
// evaluateStorm reads and writes it on each wake, but has no other side-effects.
// NOTE: AppConfig::HubSleep mirrors kStormThreshold / kShortWakeMs — keep in sync.
constexpr uint8_t  kStormThreshold = 3;
constexpr uint32_t kShortWakeMs    = 5000;  // wake with no interaction under this = storm-ish

struct StormState {
  uint8_t    consecutiveShortWakes = 0;
  WakeSource lastSource            = WakeSource::kUnknown;
};
struct StormVerdict {
  bool suppressSource = false;  // disable this wake source for the next sleep
};

inline StormVerdict evaluateStorm(StormState& state, WakeSource source,
                                  bool interactionSeen, uint32_t awakeMs) {
  StormVerdict v;
  if (interactionSeen || awakeMs >= kShortWakeMs) {
    state.consecutiveShortWakes = 0;
    state.lastSource = source;
    return v;
  }
  if (state.lastSource == source) {
    if (state.consecutiveShortWakes < 255) ++state.consecutiveShortWakes;
  } else {
    state.consecutiveShortWakes = 1;
    state.lastSource = source;
  }
  v.suppressSource = state.consecutiveShortWakes >= kStormThreshold;
  return v;
}

}  // namespace hubsleep
