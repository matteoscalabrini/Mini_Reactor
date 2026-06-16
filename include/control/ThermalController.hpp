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

#include <Preferences.h>

#include "control/PidController.hpp"
#include "control/RelayAutotune.hpp"

class ThermalController {
 public:
  enum class Mode { Auto, Manual, Autotune };

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
    const char* prefsNamespace = "pid";
  };

  ThermalController(Ds18b20& liquid, Thermistor& heaterNtc, Heater& heater,
                    const Config& config);

  void begin();

  bool enable(bool on);  // returns the resulting enabled state; false => start refused
  bool enabled() const { return enabled_; }

  void setSetpoint(float celsius);
  float setpoint() const { return setpoint_; }

  /* Gains — runtime-settable and persisted in NVS. */
  void setGains(float kp, float ki, float kd);
  float kp() const { return pid_.kp(); }
  float ki() const { return pid_.ki(); }
  float kd() const { return pid_.kd(); }

  /* Mode. setModeStr accepts "auto"|"manual"; startAutotune/cancelAutotune
   * drive the autotune lifecycle. */
  void setMode(Mode m);
  void setModeStr(const char* m);          // "auto"|"manual"
  void startAutotune();
  void cancelAutotune();
  Mode mode() const { return mode_; }
  const char* modeStr() const;

  /* PID telemetry. */
  float pTerm() const { return pid_.pTerm(); }
  float iTerm() const { return pid_.iTerm(); }
  float dTerm() const { return pid_.dTerm(); }
  float outputDuty() const { return duty_; }  // 0..1 controller output

  /* Autotune telemetry. */
  bool autotuneActive() const { return mode_ == Mode::Autotune; }
  int autotuneProgress() const { return autotune_.progressPct(); }
  const char* autotuneResult() const { return autotuneResult_; }  // null|"ok"|"failed"

  /* Heater-NTC calibration (delegates to the safety thermistor). */
  bool addCalibrationPoint(float referenceC) { return ntc_.addCalibrationPoint(referenceC); }
  bool computeCalibration() { return ntc_.computeCalibration(); }
  void resetCalibration() { ntc_.resetCalibration(); }
  uint16_t ntcRawAdc() { return ntc_.readRawAdc(); }
  float ntcResistanceOhms() { return ntc_.readResistanceOhms(); }
  const ThermistorCalibration& ntcCalibration() const { return ntc_.calibration(); }

  void update();

  float temperatureC() const { return liquidC_; }   // process value (liquid)
  float heaterTempC() const { return heaterC_; }     // safety probe
  bool sensorFault() const { return isnan(liquidC_); }  // liquid probe fault
  bool safetyTripped() const { return safetyTrip_; }
  float dutyPercent() const { return duty_ * 100.0f; }

 private:
  void applyOff();
  void loadGains();
  void persistGains();

  Ds18b20& liquid_;
  Thermistor& ntc_;
  Heater& heater_;
  Config cfg_;

  PidController pid_;
  RelayAutotune autotune_;
  Mode mode_ = Mode::Auto;
  const char* autotuneResult_ = nullptr;
  Preferences prefs_;

  bool enabled_ = false;
  float setpoint_ = 30.0f;
  float liquidC_ = NAN;
  float heaterC_ = NAN;
  float duty_ = 0.0f;
  bool safetyTrip_ = false;

  uint32_t lastSafetyMs_ = 0;
  uint32_t lastPidMs_ = 0;
};
