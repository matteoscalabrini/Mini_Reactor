/*
 * Es8311.hpp — ES8311 audio codec driver (Phase 1: probe + power-down only).
 * No I2S, no playback — Phase 2 will add configureEs8311() and the audio stack.
 * Register power-down sequence ported verbatim from
 * ../Barebone/src/features/hub/HubFeature.cpp:
 *   begin()     <- initializeEs8311() probe portion   lines 2160-2174
 *   powerDown() <- powerDownEs8311()                  lines 2233-2250
 * Implementation: src/features/hub/audio/Es8311.cpp
 */
#pragma once
#include <Arduino.h>
#include <Wire.h>

class Es8311 {
 public:
  explicit Es8311(TwoWire& wire = Wire, uint8_t address = 0x18);
  bool begin();        // probe only (no I2S — Phase 1)
  bool powerDown();    // verbatim low-power register sequence
  bool present() const { return present_; }

 private:
  bool writeReg(uint8_t reg, uint8_t v);

  TwoWire& wire_;
  uint8_t  address_;
  bool     present_ = false;
};
