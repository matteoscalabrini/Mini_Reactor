#pragma once
#include <stdint.h>
#ifndef UNIT_TEST
#include "app_config.hpp"
#else
// Minimal stubs for native unit tests (PlatformIO defines UNIT_TEST for test builds).
namespace AppConfig { namespace HubDisplay {
  static constexpr uint8_t kPanelColOffset1 = 6;
  static constexpr uint8_t kPanelRowOffset1 = 0;
} }
#endif
namespace geom {
inline uint8_t rotationMadctl(uint8_t rot) {
  switch (rot & 0x3) { case 1: return 0x02; case 2: return 0x07; case 3: return 0x05; default: return 0x00; }
}
inline uint16_t columnOffset(uint8_t rot) { return (rot & 0x1) ? AppConfig::HubDisplay::kPanelRowOffset1
                                                               : AppConfig::HubDisplay::kPanelColOffset1; }
inline uint16_t rowOffset(uint8_t rot)    { return (rot & 0x1) ? AppConfig::HubDisplay::kPanelColOffset1
                                                               : AppConfig::HubDisplay::kPanelRowOffset1; }
}  // namespace geom
