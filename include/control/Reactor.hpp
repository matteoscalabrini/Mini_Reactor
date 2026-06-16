/*
 * Reactor.hpp — fermentation process orchestrator (the "brain").
 *
 * Ties the ThermalController and the motor into a single run. A run holds a
 * target temperature, a disc rotation speed (rpm), and an optional duration
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
  float rpm = 0.0f;          // disc rpm (0 when stopped)
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
    float defaultRpm = 8.0f;
    float minRpm = 0.5f;
    float maxRpm = 30.0f;
    float defaultSetpointC = 36.0f;
    uint16_t defaultDurationMin = 0;
    uint16_t defaultDiscCurrentMa = 600;
    uint16_t defaultDiscMicrosteps = 16;
    bool defaultDiscReverse = false;
    const char* prefsNamespace = "reactor";
  };

  Reactor(ThermalController& thermal, Tmc2209Motor& motor, const Config& config);

  /* begin() — Load persisted setpoints from NVS (does not auto-start a run). */
  void begin();

  /* start() — Begin a run with target temp, disc rpm, and optional duration. */
  void start(float targetC, float rpm, uint16_t durationMin);

  void stop();
  void update();
  bool running() const { return running_; }

  /* Live setpoint changes (apply immediately if running; always persisted). */
  void setTargetC(float celsius);
  void setRpm(float rpm);
  float rpmSetpoint() const { return rpm_; }

  /* Disc drive parameters (apply to the motor; current/microsteps/direction persist). */
  void setDiscCurrentMa(uint16_t milliamps);
  void setDiscMicrosteps(uint16_t microsteps);
  void setDiscReverse(bool reverse);
  void setDiscEnabled(bool on);

  /* PID control surface (delegates to the ThermalController). */
  void setPidGains(float kp, float ki, float kd) { thermal_.setGains(kp, ki, kd); }
  void setPidMode(const char* m) { thermal_.setModeStr(m); }
  void startAutotune() { thermal_.startAutotune(); }
  void cancelAutotune() { thermal_.cancelAutotune(); }

  /* Heater-NTC calibration delegates. */
  bool addCalibrationPoint(float referenceC) { return thermal_.addCalibrationPoint(referenceC); }
  bool computeCalibration() { return thermal_.computeCalibration(); }
  void resetCalibration() { thermal_.resetCalibration(); }

  ReactorTelemetry telemetry() const;
  String csvRow() const;

 private:
  void persist();

  ThermalController& thermal_;
  Tmc2209Motor& motor_;
  Config cfg_;
  Preferences prefs_;

  bool running_ = false;
  float targetC_ = 36.0f;
  float rpm_ = 8.0f;
  uint16_t discCurrentMa_ = 600;
  uint16_t discMicrosteps_ = 16;
  bool discReverse_ = false;
  uint16_t durationMin_ = 0;
  uint32_t startMs_ = 0;
};
