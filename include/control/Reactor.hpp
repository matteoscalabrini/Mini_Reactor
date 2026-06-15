/*
 * Reactor.hpp — fermentation process orchestrator (the "brain").
 *
 * Ties the ThermalController and the motor into a single run. A run holds a
 * target temperature, a disk rotation speed (motor %), and an optional duration
 * timer. start()/stop() begin and end a run; update() advances the run timer and
 * applies live setpoint changes. telemetry() returns a snapshot for the web UI;
 * csvRow() formats a line for the SD logger. Setpoints persist in NVS.
 *
 * Implementation: src/control/Reactor.cpp
 */

#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include "control/ThermalController.hpp"
#include "motor/Tmc2209Motor.hpp"

struct ReactorTelemetry {
  bool running = false;
  float liquidTempC = NAN;   // DS18B20 process value
  float heaterTempC = NAN;   // NTC safety probe
  float setpointC = 0.0f;
  float heaterDutyPct = 0.0f;
  float motorPercent = 0.0f;
  bool sensorFault = false;  // liquid probe fault
  bool safetyTripped = false;
  uint32_t elapsedSec = 0;
  uint32_t remainingSec = 0;  // 0 when no duration timer is set
  uint16_t durationMin = 0;
};

class Reactor {
 public:
  // Config — built from AppConfig::Process.
  struct Config {
    uint8_t defaultMotorPercent = 40;
    float defaultSetpointC = 30.0f;
    uint16_t defaultDurationMin = 0;
    const char* prefsNamespace = "reactor";
  };

  Reactor(ThermalController& thermal, Tmc2209Motor& motor, const Config& config);

  /* begin() — Load persisted setpoints from NVS (does not auto-start a run). */
  void begin();

  /* start() — Begin a run with the given target temp, motor %, and optional
   * duration (minutes; 0 = until stopped). Persists the setpoints. */
  void start(float targetC, float motorPercent, uint16_t durationMin);

  /* stop() — End the run: heater off, motor stopped. */
  void stop();

  /* update() — Advance the run timer; auto-stops when a duration elapses. */
  void update();

  bool running() const { return running_; }

  /* Live setpoint changes (apply immediately if running; always persisted). */
  void setTargetC(float celsius);
  void setMotorPercent(float percent);

  ReactorTelemetry telemetry() const;
  String csvRow() const;

 private:
  void persist();

  ThermalController& thermal_;
  Tmc2209Motor& motor_;
  Config cfg_;
  Preferences prefs_;

  bool running_ = false;
  float targetC_ = 30.0f;
  float motorPercent_ = 40.0f;
  uint16_t durationMin_ = 0;
  uint32_t startMs_ = 0;
};
