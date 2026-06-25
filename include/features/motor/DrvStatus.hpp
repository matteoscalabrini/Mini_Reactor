/*
 * DrvStatus.hpp — pure decode of the TMC2209 DRV_STATUS register (0x6F).
 *
 * Header-only, dependency-free so it is host-unit-testable. Bit layout:
 *   0 otpw, 1 ot, 2 s2ga, 3 s2gb, 4 s2vsa, 5 s2vsb, 6 ola, 7 olb.
 * `stall` is NOT in DRV_STATUS (it is a DIAG-pin signal); decode leaves it false
 * and Tmc2209Motor fills it from the pin.
 */
#pragma once

#include <cstdint>

struct DrvStatusFlags {
  bool otpw = false;       // over-temperature prewarning
  bool ot = false;         // over-temperature shutdown
  bool stall = false;      // StallGuard stall (DIAG pin; set by Tmc2209Motor)
  bool openLoadA = false;  // ola
  bool openLoadB = false;  // olb
  bool shortA = false;     // s2ga || s2vsa
  bool shortB = false;     // s2gb || s2vsb
};

inline DrvStatusFlags decodeDrvStatus(uint32_t s) {
  DrvStatusFlags f;
  f.otpw = (s & (1u << 0)) != 0;
  f.ot = (s & (1u << 1)) != 0;
  f.shortA = (s & (1u << 2)) != 0 || (s & (1u << 4)) != 0;  // s2ga || s2vsa
  f.shortB = (s & (1u << 3)) != 0 || (s & (1u << 5)) != 0;  // s2gb || s2vsb
  f.openLoadA = (s & (1u << 6)) != 0;
  f.openLoadB = (s & (1u << 7)) != 0;
  return f;
}
