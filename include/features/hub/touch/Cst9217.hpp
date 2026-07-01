#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "features/hub/touch/HubTouchTransform.hpp"
class Cst9217 {
 public:
  struct TouchPoint { bool valid=false; int16_t x=0; int16_t y=0;
                      int16_t rawX=0; int16_t rawY=0; };  // raw = pre-transform (for calibration)
  struct State { bool present=false; bool ready=false; uint16_t chipId=0;
                 uint8_t pointCount=0; TouchPoint points[2]={}; };
  explicit Cst9217(TwoWire& wire = Wire, uint8_t address = 0x5A);
  bool begin();                         // reset + enter cmd mode + read id/resolution
  bool refresh(State& out);             // read+ACK+parse+transform, ~20 ms cadence
  bool readPoint(int16_t& x, int16_t& y, bool& pressed);  // convenience for LVGL indev
  bool enterSleep();
  const State& state() const { return state_; }

  // Runtime touch transform. Loaded from NVS at boot (defaults = AppConfig::HubTouch
  // factory flags); overwritten + persisted by the boot calibration wizard.
  void loadCalibration();               // NVS -> runtime flags (+ calibrated_)
  bool applyMapping(bool swapXY, bool mirrorX, bool mirrorY, bool persist);
  bool forgetCalibration();             // clear calibrated_ (+ persist) so the wizard re-runs
  bool calibrated() const { return calibrated_; }
  HubTouchTransform::Mapping mapping() const;
 private:
  bool writeReg16(uint16_t reg, const uint8_t* data, size_t n);
  bool readReg16(uint16_t reg, uint8_t* buf, size_t n);
  bool saveCalibration();
  TwoWire& wire_; uint8_t address_; State state_;
  bool swapXY_; bool mirrorX_; bool mirrorY_; bool calibrated_ = false;
};
