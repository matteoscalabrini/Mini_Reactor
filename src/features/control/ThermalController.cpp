/*
 * ThermalController.cpp — liquid-probe PID (auto/manual/autotune) + NTC safety.
 * See include/control/ThermalController.hpp.
 */

#include "features/control/ThermalController.hpp"

#include <math.h>
#include <string.h>

ThermalController::ThermalController(Ds18b20& liquid, Thermistor& heaterNtc,
                                     Heater& heater, const Config& config)
    : liquid_(liquid), ntc_(heaterNtc), heater_(heater), cfg_(config) {
  setpoint_ = config.defaultSetpointC;
  pid_.setGains(config.kp, config.ki, config.kd);
}

void ThermalController::begin() {
  liquid_.begin();
  ntc_.begin();
  heater_.begin();
  loadGains();
  lastSafetyMs_ = millis();
}

void ThermalController::loadGains() {
  prefs_.begin(cfg_.prefsNamespace, false);
  GainSchedule::Config sc;
  sc.hold = {prefs_.getFloat("holdKp", cfg_.holdKp),
             prefs_.getFloat("holdKi", cfg_.holdKi),
             prefs_.getFloat("holdKd", cfg_.holdKd)};
  sc.heat = {prefs_.getFloat("heatKp", cfg_.heatKp),
             prefs_.getFloat("heatKi", cfg_.heatKi),
             prefs_.getFloat("heatKd", cfg_.heatKd)};
  sc.bandC = cfg_.bandC;
  sc.holdDutyCap = cfg_.holdDutyCap;
  sc.dutyMax = cfg_.dutyMax;
  sched_.setConfig(sc);
  tuned_ = prefs_.getBool("tuned", false);
  // Fixed-mode PID keeps its own gains (unchanged legacy path).
  pid_.setGains(prefs_.getFloat("kp", cfg_.kp),
                prefs_.getFloat("ki", cfg_.ki),
                prefs_.getFloat("kd", cfg_.kd));
  pid_.setDerivativeOnMeasurement(cfg_.adaptiveEnabled);
}

void ThermalController::persistGains() {
  prefs_.putFloat("kp", pid_.kp());
  prefs_.putFloat("ki", pid_.ki());
  prefs_.putFloat("kd", pid_.kd());
}

void ThermalController::beginAutotuneAt(float atSetpointC) {
  RelayAutotune::Config ac;
  ac.relayHigh = cfg_.dutyMax;
  ac.relayLow = cfg_.dutyMin;
  autotune_.begin(atSetpointC, millis(), ac);
  autotuneResult_ = nullptr;
  mode_ = Mode::Autotune;
}

void ThermalController::persistSchedule() {
  const GainSchedule::Config& sc = sched_.config();
  prefs_.putFloat("holdKp", sc.hold.kp); prefs_.putFloat("holdKi", sc.hold.ki); prefs_.putFloat("holdKd", sc.hold.kd);
  prefs_.putFloat("heatKp", sc.heat.kp); prefs_.putFloat("heatKi", sc.heat.ki); prefs_.putFloat("heatKd", sc.heat.kd);
  prefs_.putBool("tuned", tuned_);
}

bool ThermalController::enable(bool on) {
  if (on == enabled_) return enabled_;
  if (on) {
    // Pre-flight: refuse to start if the heater NTC is faulted or already over-limit.
    heaterC_ = ntc_.readCelsius();
    if (isnan(heaterC_) || heaterC_ >= cfg_.heaterSafetyMaxC) {
      applyOff();  // leave enabled_ false; the run never begins
      return false;
    }
  }
  enabled_ = on;
  pid_.reset();
  lastPidMs_ = 0;
  if (on && cfg_.adaptiveEnabled && !tuned_) {
    // Commission once: relay-tune BELOW target so the culture never overshoots
    // during identification; derived gains apply to the real setpoint after.
    beginAutotuneAt(setpoint_ - cfg_.tuneMarginC);
  }
  if (!on) {
    cancelAutotune();  // clears autotuneResult_ + returns to Auto if mid-tune
    applyOff();
  }
  return enabled_;
}

void ThermalController::setSetpoint(float celsius) { setpoint_ = celsius; }

void ThermalController::setGains(float kp, float ki, float kd) {
  pid_.setGains(kp, ki, kd);
  persistGains();
}

void ThermalController::setMode(Mode m) {
  if (m == mode_) return;
  if (m == Mode::Autotune) {
    RelayAutotune::Config ac;
    ac.relayHigh = cfg_.dutyMax;
    ac.relayLow = cfg_.dutyMin;
    autotune_.begin(setpoint_, millis(), ac);
    autotuneResult_ = nullptr;  // in progress
  }
  if (m == Mode::Auto) {
    pid_.reset();
    lastPidMs_ = 0;
  }
  mode_ = m;
}

void ThermalController::setModeStr(const char* m) {
  if (m && strcmp(m, "manual") == 0) setMode(Mode::Manual);
  else setMode(Mode::Auto);  // any non-"manual" returns to auto
}

void ThermalController::startAutotune() { setMode(Mode::Autotune); }

void ThermalController::cancelAutotune() {
  if (mode_ == Mode::Autotune) {
    autotuneResult_ = nullptr;  // cancelled, no result
    setMode(Mode::Auto);
  }
}

const char* ThermalController::modeStr() const {
  switch (mode_) {
    case Mode::Manual: return "manual";
    case Mode::Autotune: return "autotune";
    default: return "auto";
  }
}

const char* ThermalController::regimeStr() const {
  if (!cfg_.adaptiveEnabled) return "fixed";
  return sched_.regime(setpoint_, liquidC_);
}

void ThermalController::applyOff() {
  heater_.off();
  duty_ = 0.0f;
  pid_.reset();
}

void ThermalController::setInhibited(bool on) {
  if (inhibited_ && !on) pid_.reset();  // resume: clear integrator -> no wind-up bump
  inhibited_ = on;
  if (on) applyOff();
}

void ThermalController::update() {
  const uint32_t now = millis();

  const bool fresh = liquid_.update();
  if (fresh) liquidC_ = liquid_.celsius();

  // Fast, independent safety poll of the heater NTC.
  if (now - lastSafetyMs_ >= cfg_.safetyCheckMs) {
    lastSafetyMs_ = now;
    heaterC_ = ntc_.readCelsius();
    if (enabled_) {
      const bool over = isnan(heaterC_) || heaterC_ >= cfg_.heaterSafetyMaxC;
      if (over) { safetyTrip_ = true; applyOff(); }
      else safetyTrip_ = false;
    }
  }

  if (!enabled_) { applyOff(); return; }
  if (safetyTrip_) return;
  if (inhibited_) { applyOff(); return; }  // full-hold: heater off, run still active
  if (!fresh) return;
  if (isnan(liquidC_) || liquidC_ >= cfg_.processMaxC) { applyOff(); return; }

  const float dt = lastPidMs_ ? (now - lastPidMs_) / 1000.0f : 0.75f;
  lastPidMs_ = now;

  if (mode_ == Mode::Autotune) {
    duty_ = autotune_.update(liquidC_, now);
    heater_.setDuty(duty_);
    if (autotune_.done() || autotune_.failed()) {
      if (autotune_.done()) {
        float kp, ki, kd;
        if (autotune_.computeGains(kp, ki, kd)) {
          GainSchedule::Config sc = sched_.config();
          sc.hold = {kp, ki, kd};                                   // conservative = hold
          sc.heat = GainSchedule::scaleForHeat(sc.hold, cfg_.heatKpScale);
          sched_.setConfig(sc);
          tuned_ = true;
          persistSchedule();
          autotuneResult_ = "ok";
        } else {
          autotuneResult_ = "failed";
        }
      } else {
        autotuneResult_ = "failed";
      }
      mode_ = Mode::Auto;
      pid_.reset();
    }
  } else if (mode_ == Mode::Manual) {
    heater_.setDuty(duty_);  // freeze at last duty
  } else {  // Mode::Auto
    if (cfg_.adaptiveEnabled) {
      const GainSchedule::Output g = sched_.evaluate(setpoint_, liquidC_);
      dutyCeil_ = g.dutyCeil;
      pid_.setGains(g.kp, g.ki, g.kd);
      duty_ = pid_.step(setpoint_, liquidC_, dt, cfg_.dutyMin, g.dutyCeil);
    } else {
      dutyCeil_ = cfg_.dutyMax;
      duty_ = pid_.step(setpoint_, liquidC_, dt, cfg_.dutyMin, cfg_.dutyMax);
    }
    heater_.setDuty(duty_);
  }
}
