/*
 * Tca9554.hpp — TCA9554 8-bit I/O expander driver.
 * Register-level sequences ported from
 * ../Barebone/src/features/hub/HubFeature.cpp:
 *   begin()     <- initializeTca9554()    lines 2117-2131
 *   configure() <- configureTca9554()     lines 2133-2142
 *   refresh()   <- refreshTca9554Status() lines 2144-2158
 * Implementation: src/features/hub/io/Tca9554.cpp
 */
#pragma once
#include <Arduino.h>
#include <Wire.h>

class Tca9554 {
 public:
  struct State {
    bool    present  = false;
    bool    ready    = false;
    uint8_t input    = 0;
    uint8_t output   = 0;
    uint8_t polarity = 0;
    uint8_t config   = 0xFF;
  };

  explicit Tca9554(TwoWire& wire = Wire, uint8_t address = 0x20);

  bool begin();                                                    // probe + default config
  bool configure(uint8_t output, uint8_t polarity, uint8_t config);
  bool refresh(State& out);                                        // read all 4 regs
  const State& state() const { return state_; }

 private:
  static constexpr uint8_t kRegInput    = 0x00;
  static constexpr uint8_t kRegOutput   = 0x01;
  static constexpr uint8_t kRegPolarity = 0x02;
  static constexpr uint8_t kRegConfig   = 0x03;

  bool readReg(uint8_t reg, uint8_t& v);
  bool writeReg(uint8_t reg, uint8_t v);

  TwoWire& wire_;
  uint8_t  address_;
  State    state_;
};
