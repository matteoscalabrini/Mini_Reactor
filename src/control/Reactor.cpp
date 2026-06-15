/*
 * Reactor.cpp — fermentation run orchestration.
 * See include/control/Reactor.hpp and AppConfig::Process.
 */

#include "control/Reactor.hpp"

#include <math.h>

Reactor::Reactor(ThermalController& thermal, Tmc2209Motor& motor,
                 const Config& config)
    : thermal_(thermal), motor_(motor), cfg_(config) {
  targetC_ = config.defaultSetpointC;
  motorPercent_ = config.defaultMotorPercent;
  durationMin_ = config.defaultDurationMin;
}

void Reactor::begin() {
  prefs_.begin(cfg_.prefsNamespace, false);
  targetC_ = prefs_.getFloat("targetC", cfg_.defaultSetpointC);
  motorPercent_ = prefs_.getFloat("motorPct", cfg_.defaultMotorPercent);
  durationMin_ = prefs_.getUShort("durMin", cfg_.defaultDurationMin);
  thermal_.setSetpoint(targetC_);
}

void Reactor::persist() {
  prefs_.putFloat("targetC", targetC_);
  prefs_.putFloat("motorPct", motorPercent_);
  prefs_.putUShort("durMin", durationMin_);
}

void Reactor::start(float targetC, float motorPercent, uint16_t durationMin) {
  targetC_ = targetC;
  motorPercent_ = constrain(motorPercent, 0.0f, 100.0f);
  durationMin_ = durationMin;
  persist();

  running_ = true;
  startMs_ = millis();

  thermal_.setSetpoint(targetC_);
  thermal_.enable(true);
  motor_.enable(true);
  motor_.setSpeedPercent(motorPercent_);
}

void Reactor::stop() {
  running_ = false;
  thermal_.enable(false);
  motor_.stop();
}

void Reactor::update() {
  if (!running_) return;
  if (durationMin_ > 0) {
    const uint32_t elapsedMs = millis() - startMs_;
    if (elapsedMs >= static_cast<uint32_t>(durationMin_) * 60000UL) {
      stop();
    }
  }
}

void Reactor::setTargetC(float celsius) {
  targetC_ = celsius;
  thermal_.setSetpoint(celsius);
  persist();
}

void Reactor::setMotorPercent(float percent) {
  motorPercent_ = constrain(percent, 0.0f, 100.0f);
  if (running_) motor_.setSpeedPercent(motorPercent_);
  persist();
}

ReactorTelemetry Reactor::telemetry() const {
  ReactorTelemetry t;
  t.running = running_;
  t.liquidTempC = thermal_.temperatureC();
  t.heaterTempC = thermal_.heaterTempC();
  t.setpointC = thermal_.setpoint();
  t.heaterDutyPct = thermal_.dutyPercent();
  t.motorPercent = running_ ? motorPercent_ : 0.0f;
  t.sensorFault = thermal_.sensorFault();
  t.safetyTripped = thermal_.safetyTripped();
  t.durationMin = durationMin_;
  if (running_) {
    t.elapsedSec = (millis() - startMs_) / 1000UL;
    if (durationMin_ > 0) {
      const uint32_t totalSec = static_cast<uint32_t>(durationMin_) * 60UL;
      t.remainingSec = (t.elapsedSec < totalSec) ? (totalSec - t.elapsedSec) : 0;
    }
  }
  return t;
}

String Reactor::csvRow() const {
  const ReactorTelemetry t = telemetry();
  // t_ms,running,liquid_c,heater_c,setpoint_c,heater_pct,motor_pct,fault,safety
  char buf[128];
  snprintf(buf, sizeof(buf), "%lu,%d,%.2f,%.2f,%.2f,%.1f,%.1f,%d,%d",
           (unsigned long)millis(), t.running ? 1 : 0,
           isnan(t.liquidTempC) ? 0.0f : t.liquidTempC,
           isnan(t.heaterTempC) ? 0.0f : t.heaterTempC, t.setpointC,
           t.heaterDutyPct, t.motorPercent, t.sensorFault ? 1 : 0,
           t.safetyTripped ? 1 : 0);
  return String(buf);
}
