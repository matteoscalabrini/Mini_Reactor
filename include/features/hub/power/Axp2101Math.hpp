#pragma once
#include <stdint.h>
namespace axp {
inline float decodeTemperatureC(int16_t raw) { return 22.0f + (7274.0f - (float)raw) / 20.0f; }
inline uint16_t scaleVoltageMv(uint16_t raw) { return raw; }  // 1 mV/LSB nominal
}  // namespace axp
