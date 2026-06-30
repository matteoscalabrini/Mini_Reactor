#pragma once
#include <stdint.h>
namespace rtc {
inline uint8_t bcdToUint(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
inline uint8_t uintToBcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
}  // namespace rtc
