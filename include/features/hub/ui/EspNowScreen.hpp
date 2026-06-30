#pragma once
#include <cstdint>

// Minimal 3-state HUB link screen (label + lv_obj only, like BringupScreen).
// Unpaired: a "PAIR" tappable tile. Searching: sweep status. Paired: telemetry.

namespace EspNowScreen {
enum class Mode : uint8_t { Unpaired, Searching, Paired };
struct View {
  Mode    mode;
  uint8_t sweepChannel;
  bool    linkAlive;
  // telemetry (valid when Paired)
  float   tempC;        bool tempValid;
  float   setpointC;
  float   heaterPct;
  float   rpm;
  bool    runActive;
  bool    motorPaused;
  bool    fullHold;
  bool    safetyTripped;
  uint32_t elapsedSec;
};
void create();
void update(const View& v);
bool pairPressed();   // true once after the PAIR tile is tapped (consumes the latch)
}  // namespace EspNowScreen
