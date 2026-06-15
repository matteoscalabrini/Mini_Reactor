/*
 * ThermalController.cpp — PID on the liquid probe + NTC safety high-limit.
 * See include/control/ThermalController.hpp and AppConfig::Thermal.
 */

#include "control/ThermalController.hpp"

#include <math.h>

ThermalController::ThermalController(Ds18b20& liquid, Thermistor& heaterNtc,
                                     Heater& heater, const Config& config)
    : liquid_(liquid), ntc_(heaterNtc), heater_(heater), cfg_(config) {
  setpoint_ = config.defaultSetpointC;
}

void ThermalController::begin() {
  liquid_.begin();
  ntc_.begin();
  heater_.begin();
  lastSafetyMs_ = millis();
}

void ThermalController::enable(bool on) {
  if (on == enabled_) return;
  enabled_ = on;
  integral_ = 0.0f;
  havePrev_ = false;
  lastPidMs_ = 0;
  if (!on) applyOff();
}

void ThermalController::setSetpoint(float celsius) { setpoint_ = celsius; }

void ThermalController::applyOff() {
  heater_.off();
  duty_ = 0.0f;
  integral_ = 0.0f;
  havePrev_ = false;
}

void ThermalController::update() {
  const uint32_t now = millis();

  // Always latch the liquid reading (even when idle) so telemetry stays live.
  const bool fresh = liquid_.update();
  if (fresh) liquidC_ = liquid_.celsius();

  // Fast, independent safety poll of the heater NTC.
  if (now - lastSafetyMs_ >= cfg_.safetyCheckMs) {
    lastSafetyMs_ = now;
    heaterC_ = ntc_.readCelsius();
    if (enabled_) {
      const bool over = isnan(heaterC_) || heaterC_ >= cfg_.heaterSafetyMaxC;
      if (over) {
        safetyTrip_ = true;
        applyOff();
      } else {
        safetyTrip_ = false;
      }
    }
  }

  if (!enabled_) {
    applyOff();
    return;
  }
  if (safetyTrip_) return;  // stay off until the heater probe cools

  // PID step on each fresh liquid sample.
  if (fresh) {
    if (isnan(liquidC_) || liquidC_ >= cfg_.processMaxC) {
      applyOff();
      return;
    }
    const float dt = lastPidMs_ ? (now - lastPidMs_) / 1000.0f : 0.75f;
    lastPidMs_ = now;

    const float error = setpoint_ - liquidC_;
    integral_ += error * dt;
    const float deriv = havePrev_ ? (error - prevError_) / dt : 0.0f;
    prevError_ = error;
    havePrev_ = true;

    float out = cfg_.kp * error + cfg_.ki * integral_ + cfg_.kd * deriv;
    if (out > cfg_.dutyMax) {
      out = cfg_.dutyMax;
      integral_ -= error * dt;
    } else if (out < cfg_.dutyMin) {
      out = cfg_.dutyMin;
      integral_ -= error * dt;
    }
    duty_ = out;
    heater_.setDuty(out);
  }
}
