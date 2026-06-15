/*
 * ThermalController.hpp — closed-loop bath temperature control with a safety limit.
 *
 * Two sensors, two jobs:
 *   - Ds18b20 (liquid probe): the PID process value — the loop drives heater duty
 *     to hold this at the setpoint.
 *   - Thermistor (heater-mounted NTC): a safety high-limit, read independently.
 *     If the heater probe exceeds the safety ceiling (or faults), the heater is
 *     forced off regardless of the PID — protecting against runaway/scorching.
 *
 * The heater is also cut if the liquid probe faults (NAN) or the liquid exceeds a
 * sanity ceiling. update() is non-blocking: the PID steps on each fresh DS18B20
 * sample (~0.75 s); the NTC safety check runs at a faster fixed cadence.
 *
 * Implementation: src/control/ThermalController.cpp
 */

#pragma once

#include <Arduino.h>

#include "heater/Heater.hpp"
#include "sensor/Ds18b20.hpp"
#include "sensor/Thermistor.hpp"

class ThermalController {
 public:
  // Config — built from AppConfig::Thermal.
  struct Config {
    float kp = 0.08f;
    float ki = 0.0015f;
    float kd = 0.4f;
    float dutyMin = 0.0f;
    float dutyMax = 1.0f;
    float defaultSetpointC = 30.0f;
    float heaterSafetyMaxC = 80.0f;  // NTC heater-probe over-temp cutoff
    float processMaxC = 55.0f;       // liquid sanity ceiling
    uint32_t safetyCheckMs = 200;    // NTC safety poll cadence
  };

  ThermalController(Ds18b20& liquid, Thermistor& heaterNtc, Heater& heater,
                    const Config& config);

  void begin();

  void enable(bool on);
  bool enabled() const { return enabled_; }

  void setSetpoint(float celsius);
  float setpoint() const { return setpoint_; }

  void update();

  float temperatureC() const { return liquidC_; }   // process value (liquid)
  float heaterTempC() const { return heaterC_; }     // safety probe
  bool sensorFault() const { return isnan(liquidC_); }  // liquid probe fault
  bool safetyTripped() const { return safetyTrip_; }
  float dutyPercent() const { return duty_ * 100.0f; }

 private:
  void applyOff();

  Ds18b20& liquid_;
  Thermistor& ntc_;
  Heater& heater_;
  Config cfg_;

  bool enabled_ = false;
  float setpoint_ = 30.0f;
  float liquidC_ = NAN;
  float heaterC_ = NAN;
  float duty_ = 0.0f;
  bool safetyTrip_ = false;

  float integral_ = 0.0f;
  float prevError_ = 0.0f;
  bool havePrev_ = false;
  uint32_t lastSafetyMs_ = 0;
  uint32_t lastPidMs_ = 0;
};
