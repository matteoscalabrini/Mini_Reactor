/*
 * Es7210.hpp — ES7210 quad-mic ADC driver (Phase 1: probe + power-down only).
 * No I2S, no capture — Phase 2 will add configureEs7210ForCapture().
 * Register power-down sequence ported verbatim from
 * ../Barebone/src/features/hub/HubFeature.cpp:
 *   begin()     <- initializeEs7210() probe portion   lines 2278-2300
 *   powerDown() <- powerDownEs7210()                  lines 2302-2325
 * Implementation: src/features/hub/audio/Es7210.cpp
 */
#pragma once
#include <Arduino.h>
#include <Wire.h>

class Es7210 {
 public:
  explicit Es7210(TwoWire& wire = Wire, uint8_t address = 0x40);
  bool begin();        // probe only (no I2S — Phase 1)
  bool powerDown();    // verbatim low-power register sequence
  bool present() const { return present_; }

 private:
  bool writeReg(uint8_t reg, uint8_t v);

  TwoWire& wire_;
  uint8_t  address_;
  bool     present_ = false;
};
