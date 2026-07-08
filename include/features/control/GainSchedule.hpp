/*
 * GainSchedule.hpp — distance-to-setpoint gain scheduling for the thermal loop.
 *
 * Pure, header-only, dependency-free (host-unit-tested like PidController /
 * RelayAutotune). Maps (setpoint, pv) to effective PID gains plus a max-duty
 * ceiling across three regimes: heat (far below), approach (linear blend in the
 * band below setpoint), hold (at/over setpoint). The tapered ceiling, fed to the
 * PID as outMax, lets the existing anti-windup bleed the integrator down near
 * setpoint so the element sheds stored heat before the liquid arrives.
 */
#pragma once

class GainSchedule {
 public:
  struct Gains  { float kp = 0.0f, ki = 0.0f, kd = 0.0f; };
  struct Output { float kp, ki, kd, dutyCeil; };
  struct Config {
    Gains heat;                 // aggressive: error >= bandC
    Gains hold;                 // gentle: at/over setpoint
    float bandC = 3.0f;         // approach-band width below setpoint (degC)
    float holdDutyCap = 0.6f;   // max duty at setpoint (soft landing)
    float dutyMax = 1.0f;       // max duty far from setpoint
  };

  void setConfig(const Config& c) { cfg_ = c; }
  const Config& config() const { return cfg_; }

  Output evaluate(float setpoint, float pv) const {
    const float error = setpoint - pv;
    if (error >= cfg_.bandC)
      return {cfg_.heat.kp, cfg_.heat.ki, cfg_.heat.kd, cfg_.dutyMax};
    if (error <= 0.0f)
      return {cfg_.hold.kp, cfg_.hold.ki, cfg_.hold.kd, cfg_.holdDutyCap};
    const float f = error / cfg_.bandC;   // 0 at setpoint .. 1 at band edge
    return {lerp(cfg_.hold.kp, cfg_.heat.kp, f),
            lerp(cfg_.hold.ki, cfg_.heat.ki, f),
            lerp(cfg_.hold.kd, cfg_.heat.kd, f),
            lerp(cfg_.holdDutyCap, cfg_.dutyMax, f)};
  }

  const char* regime(float setpoint, float pv) const {
    const float error = setpoint - pv;
    if (error >= cfg_.bandC) return "heat";
    if (error <= 0.0f)       return "hold";
    return "approach";
  }

  static Gains scaleForHeat(const Gains& hold, float kpScale) {
    return {hold.kp * kpScale, hold.ki * kpScale * 0.5f, hold.kd * kpScale};
  }

 private:
  static float lerp(float a, float b, float f) { return a + (b - a) * f; }
  Config cfg_;
};
