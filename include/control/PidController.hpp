/*
 * PidController.hpp — pure PID step with anti-windup + term exposure.
 *
 * Header-only, dependency-free so it is host-unit-testable. One `step()` per
 * control sample; output is clamped to [outMin,outMax] and the integral is
 * back-calculated on saturation (conditional integration). Term contributions
 * (p/i/d) and the last output are exposed for telemetry.
 */
#pragma once

class PidController {
 public:
  void setGains(float kp, float ki, float kd) { kp_ = kp; ki_ = ki; kd_ = kd; }
  float kp() const { return kp_; }
  float ki() const { return ki_; }
  float kd() const { return kd_; }

  void reset() {
    integral_ = 0.0f; prevError_ = 0.0f; havePrev_ = false;
    p_ = i_ = d_ = 0.0f; out_ = 0.0f;
  }

  float step(float setpoint, float pv, float dt, float outMin, float outMax) {
    const float error = setpoint - pv;
    integral_ += error * dt;
    const float deriv = havePrev_ ? (error - prevError_) / dt : 0.0f;
    prevError_ = error;
    havePrev_ = true;

    p_ = kp_ * error;
    i_ = ki_ * integral_;
    d_ = kd_ * deriv;
    float out = p_ + i_ + d_;

    if (out > outMax) {
      out = outMax;
      integral_ -= error * dt;   // anti-windup: undo this step's accumulation
      i_ = ki_ * integral_;
    } else if (out < outMin) {
      out = outMin;
      integral_ -= error * dt;
      i_ = ki_ * integral_;
    }
    out_ = out;
    return out;
  }

  float pTerm() const { return p_; }
  float iTerm() const { return i_; }
  float dTerm() const { return d_; }
  float output() const { return out_; }

 private:
  float kp_ = 0.0f, ki_ = 0.0f, kd_ = 0.0f;
  float integral_ = 0.0f, prevError_ = 0.0f;
  bool havePrev_ = false;
  float p_ = 0.0f, i_ = 0.0f, d_ = 0.0f, out_ = 0.0f;
};
