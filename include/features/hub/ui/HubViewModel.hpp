#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>

// Pure, header-only view-model for the HUB UI: no LVGL/Arduino deps so it links
// into the native host tests. Screens consume `Model`; helpers turn telemetry
// into brand colours and display strings.
namespace hubui {

struct Rgb { uint8_t r, g, b; };

enum class HeroState : uint8_t { AtTarget, Heating, Fault };

inline HeroState heroState(bool tempValid, bool safety, bool probeFault, bool heaterActive) {
  if (!tempValid || safety || probeFault) return HeroState::Fault;
  if (heaterActive)                        return HeroState::Heating;
  return HeroState::AtTarget;
}

inline Rgb heroColor(HeroState s) {
  switch (s) {
    case HeroState::Fault:   return {0xff, 0x4d, 0x4f};
    case HeroState::Heating: return {0xff, 0xb0, 0x20};
    default:                 return {0x00, 0xb4, 0xd8};
  }
}

inline const char* formatHms(uint32_t totalSec, char* buf, size_t n) {
  const unsigned h = (unsigned)(totalSec / 3600UL);
  const unsigned m = (unsigned)((totalSec % 3600UL) / 60UL);
  std::snprintf(buf, n, "%uh %02u", h, m);
  return buf;
}

inline const char* formatRemaining(int32_t sec, char* buf, size_t n) {
  if (sec < 0) { std::snprintf(buf, n, "until stop"); return buf; }
  return formatHms((uint32_t)sec, buf, n);
}

// The single view-struct HubRuntime fills and every screen renders.
struct Model {
  // link / power
  bool     linked        = false;
  uint8_t  sweepChannel  = 0;
  uint8_t  batteryPct    = 0;
  uint16_t batteryMv     = 0;
  bool     charging      = false;
  // run + process
  bool     runActive     = false;
  bool     motorPaused   = false;
  bool     fullHold      = false;
  bool     safetyTripped = false;
  bool     probeFault    = false;
  bool     heaterActive  = false;
  bool     tempValid     = false;
  float    tempC         = 0.0f;
  float    setpointC     = 0.0f;
  float    heaterPct     = 0.0f;
  float    rpm           = 0.0f;
  float    heaterTempC   = 0.0f;  bool heaterTempValid = false;
  float    processMaxC   = 0.0f;
  int16_t  load          = 0;     bool loadValid = false;
  uint32_t elapsedSec    = 0;
  int32_t  remainingSec  = -1;
};

}  // namespace hubui
