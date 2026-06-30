#pragma once
#include <stdint.h>
namespace qmi {
inline int16_t decodeS16(const uint8_t* b) { return (int16_t)(((uint16_t)b[1] << 8) | b[0]); }
inline float decodeTemperatureC(int16_t raw) { return raw / 256.0f; }
inline float accelSensitivityGPerLsb(uint8_t rangeG) { return (float)rangeG / 32768.0f; }
inline float gyroSensitivityDpsPerLsb(uint16_t rangeDps) { return (float)rangeDps / 32768.0f; }
}  // namespace qmi
