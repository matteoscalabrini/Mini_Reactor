#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "features/hub/touch/HubTouchTransform.hpp"
class Cst9217 {
 public:
  struct TouchPoint { bool valid=false; int16_t x=0; int16_t y=0; };
  struct State { bool present=false; bool ready=false; uint16_t chipId=0;
                 uint8_t pointCount=0; TouchPoint points[2]={}; };
  explicit Cst9217(TwoWire& wire = Wire, uint8_t address = 0x5A);
  bool begin();                         // reset + enter cmd mode + read id/resolution
  bool refresh(State& out);             // read+ACK+parse+transform, ~20 ms cadence
  bool readPoint(int16_t& x, int16_t& y, bool& pressed);  // convenience for LVGL indev
  bool enterSleep();
  const State& state() const { return state_; }
 private:
  bool writeReg16(uint16_t reg, const uint8_t* data, size_t n);
  bool readReg16(uint16_t reg, uint8_t* buf, size_t n);
  TwoWire& wire_; uint8_t address_; State state_;
};
