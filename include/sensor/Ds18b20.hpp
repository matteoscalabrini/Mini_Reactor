/*
 * Ds18b20.hpp — DS18B20 1-Wire liquid temperature probe (the PID process value).
 *
 * Wired on GPIO42 (IO42, broken out on J6) with the usual 4.7k pull-up on the
 * data line. A 12-bit conversion takes ~750 ms, so the read is asynchronous:
 * update() kicks a conversion and latches the result when ready (non-blocking),
 * returning true on the cycle a fresh sample arrives. celsius() returns the last
 * value, or NAN if the probe is disconnected / faulted.
 *
 * Implementation: src/sensor/Ds18b20.cpp
 */

#pragma once

#include <Arduino.h>

class OneWire;
class DallasTemperature;

class Ds18b20 {
 public:
  struct Config {
    int pin = -1;
    uint8_t resolutionBits = 12;   // 9..12 (12 = 0.0625°C, ~750 ms)
    uint32_t conversionMs = 750;   // worst-case 12-bit conversion time
  };

  explicit Ds18b20(const Config& config);

  void begin();

  /* update() — Service the conversion state machine. Returns true on the call
   * where a new reading was just latched (so the controller can step its PID). */
  bool update();

  /* celsius() — Last latched temperature, or NAN if disconnected/faulted. */
  float celsius() const { return lastC_; }

 private:
  Config cfg_;
  OneWire* wire_ = nullptr;
  DallasTemperature* dt_ = nullptr;
  float lastC_ = NAN;
  uint32_t requestedMs_ = 0;
  bool pending_ = false;
};
