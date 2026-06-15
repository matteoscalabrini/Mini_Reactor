/*
 * Reactor.cpp — fermentation run orchestration (rpm-based disc drive).
 * See include/control/Reactor.hpp and AppConfig::Process.
 */

#include "control/Reactor.hpp"

#include <math.h>

#include "motor/RpmKinematics.hpp"

Reactor::Reactor(ThermalController& thermal, Tmc2209Motor& motor,
                 const Config& config)
    : thermal_(thermal), motor_(motor), cfg_(config) {
  targetC_ = config.defaultSetpointC;
  rpm_ = config.defaultRpm;
  durationMin_ = config.defaultDurationMin;
  discCurrentMa_ = config.defaultDiscCurrentMa;
  discMicrosteps_ = config.defaultDiscMicrosteps;
  discReverse_ = config.defaultDiscReverse;
}

void Reactor::begin() {
  prefs_.begin(cfg_.prefsNamespace, false);
  targetC_ = prefs_.getFloat("targetC", cfg_.defaultSetpointC);
  rpm_ = prefs_.getFloat("rpm", cfg_.defaultRpm);
  durationMin_ = prefs_.getUShort("durMin", cfg_.defaultDurationMin);
  discCurrentMa_ = prefs_.getUShort("discMa", cfg_.defaultDiscCurrentMa);
  discMicrosteps_ = prefs_.getUShort("discUs", cfg_.defaultDiscMicrosteps);
  discReverse_ = prefs_.getBool("discRev", cfg_.defaultDiscReverse);
  thermal_.setSetpoint(targetC_);
  // Apply persisted drive params to the (already-begun) motor.
  motor_.setCurrentMilliamps(discCurrentMa_);
  motor_.setMicrosteps(discMicrosteps_);
  motor_.setDirection(discReverse_);
}

void Reactor::persist() {
  prefs_.putFloat("targetC", targetC_);
  prefs_.putFloat("rpm", rpm_);
  prefs_.putUShort("durMin", durationMin_);
  prefs_.putUShort("discMa", discCurrentMa_);
  prefs_.putUShort("discUs", discMicrosteps_);
  prefs_.putBool("discRev", discReverse_);
}

void Reactor::start(float targetC, float rpm, uint16_t durationMin) {
  targetC_ = targetC;
  rpm_ = RpmKinematics::clampRpm(rpm, cfg_.minRpm, cfg_.maxRpm);
  durationMin_ = durationMin;
  persist();

  running_ = true;
  startMs_ = millis();

  thermal_.setSetpoint(targetC_);
  thermal_.enable(true);
  motor_.enable(true);
  motor_.setCurrentMilliamps(discCurrentMa_);
  motor_.setMicrosteps(discMicrosteps_);
  motor_.setDirection(discReverse_);
  motor_.setRpm(rpm_);
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

void Reactor::setRpm(float rpm) {
  rpm_ = RpmKinematics::clampRpm(rpm, cfg_.minRpm, cfg_.maxRpm);
  if (running_) motor_.setRpm(rpm_);
  persist();
}

void Reactor::setDiscCurrentMa(uint16_t milliamps) {
  if (milliamps < 100) milliamps = 100;
  if (milliamps > 1500) milliamps = 1500;
  discCurrentMa_ = milliamps;
  motor_.setCurrentMilliamps(discCurrentMa_);
  persist();
}

void Reactor::setDiscMicrosteps(uint16_t microsteps) {
  discMicrosteps_ = microsteps;
  motor_.setMicrosteps(microsteps);
  persist();
}

void Reactor::setDiscReverse(bool reverse) {
  discReverse_ = reverse;
  motor_.setDirection(reverse);
  persist();
}

void Reactor::setDiscEnabled(bool on) {
  if (!on) {
    motor_.stop();
    motor_.enable(false);
  } else {
    motor_.enable(true);
    if (running_) motor_.setRpm(rpm_);
  }
}

ReactorTelemetry Reactor::telemetry() const {
  ReactorTelemetry t;
  t.running = running_;
  t.liquidTempC = thermal_.temperatureC();
  t.heaterTempC = thermal_.heaterTempC();
  t.setpointC = thermal_.setpoint();
  t.heaterDutyPct = thermal_.dutyPercent();
  t.rpm = running_ ? rpm_ : 0.0f;
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
  // t_ms,running,liquid_c,heater_c,setpoint_c,heater_pct,rpm,load,fault,safety
  char buf[160];
  snprintf(buf, sizeof(buf), "%lu,%d,%.2f,%.2f,%.2f,%.1f,%.2f,%d,%d,%d",
           (unsigned long)millis(), t.running ? 1 : 0,
           isnan(t.liquidTempC) ? 0.0f : t.liquidTempC,
           isnan(t.heaterTempC) ? 0.0f : t.heaterTempC, t.setpointC,
           t.heaterDutyPct, t.rpm, /*load*/ 0, t.sensorFault ? 1 : 0,
           t.safetyTripped ? 1 : 0);
  return String(buf);
}
