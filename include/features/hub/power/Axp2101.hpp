/*
 * Axp2101.hpp — AXP2101 PMIC/charger driver (power authority).
 * Owns charge configuration, battery/VBUS telemetry, and the low-power
 * paths used before deep sleep. Register sequence ported verbatim from
 * Barebone HubFeature. Implementation: src/features/hub/power/Axp2101.cpp
 */
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "features/hub/power/Axp2101Math.hpp"

class Axp2101 {
 public:
  struct State {
    bool present = false;
    bool ready = false;
    uint8_t chipId = 0;
    bool batteryPresent = false;
    bool vbusPresent = false;
    bool charging = false;
    uint16_t batteryVoltageMv = 0;
    uint16_t vbusVoltageMv = 0;
    uint16_t systemVoltageMv = 0;
    int batteryPercent = -1;
    float temperatureC = NAN;
  };

  explicit Axp2101(TwoWire& wire = Wire, uint8_t address = 0x34);
  bool begin(int sdaPin, int sclPin, uint32_t frequencyHz);  // also starts the bus
  bool probe();
  bool configureCharging();   // applies AppConfig::HubPower::*
  bool refreshStatus(State& out);
  bool disableAdcForSleep();  // stop batt/vbus/sys/temp measurement
  bool powerDown();           // lowest-power state for unused-by-telemetry mode
  const State& state() const { return state_; }
  const char* lastErrorString() const { return lastError_; }

 private:
  // Register map lives in Axp2101.cpp (file-scope constexpr block).
  bool readReg(uint8_t reg, uint8_t& v);
  bool writeReg(uint8_t reg, uint8_t v);
  bool readRegs(uint8_t reg, uint8_t* buf, size_t n);
  TwoWire& wire_;
  uint8_t address_;
  State state_;
  const char* lastError_ = "";
};
