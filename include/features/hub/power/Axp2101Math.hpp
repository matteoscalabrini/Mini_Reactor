#pragma once
#include <stdint.h>
namespace axp {
inline float decodeTemperatureC(int16_t raw) { return 22.0f + (7274.0f - (float)raw) / 20.0f; }
inline uint16_t scaleVoltageMv(uint16_t raw) { return raw; }  // 1 mV/LSB nominal
// Input-voltage-limit register encoding: 3880–5080 mV, 80 mV/LSB (AXP2101 reg 0x15).
inline uint8_t encodeInputVoltageReg(uint16_t mv) {
  const uint16_t clamped = mv < 3880u ? 3880u : (mv > 5080u ? 5080u : mv);
  return static_cast<uint8_t>((clamped - 3880u) / 80u);
}
}  // namespace axp
