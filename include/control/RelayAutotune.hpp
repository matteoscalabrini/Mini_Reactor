/*
 * RelayAutotune.hpp — pure relay (Åström-Hägglund) PID autotune.
 *
 * Header-only, dependency-free (time injected → host-testable). Drives a relay
 * output around the setpoint with hysteresis; measures the induced oscillation
 * period (Tu) and amplitude (a) over `targetCycles`, then derives Tyreus-Luyben
 * gains from the ultimate gain Ku = 4d/(pi*a), where d is the relay half-amplitude.
 *   Kp = 0.45 Ku, Ki = Kp/(2.2 Tu), Kd = Kp*Tu/6.3.
 * `update(pv, nowMs)` returns the relay duty to apply to the heater each sample.
 */
#pragma once

#include <cstdint>

class RelayAutotune {
 public:
  struct Config {
    float relayHigh = 1.0f;        // heater duty below setpoint
    float relayLow = 0.0f;         // duty above setpoint
    float hysteresisC = 0.3f;      // switching deadband around setpoint
    int targetCycles = 4;          // oscillation cycles to average
    uint32_t timeoutMs = 1800000;  // 30 min cap
  };

  void begin(float setpointC, uint32_t nowMs, const Config& cfg) {
    cfg_ = cfg; setpoint_ = setpointC; startMs_ = nowMs;
    high_ = true; output_ = cfg_.relayHigh;
    cycles_ = 0; haveLastCross_ = false; lastCrossMs_ = 0;
    periodSum_ = 0.0f; ampSum_ = 0.0f;
    curMin_ = 1e30f; curMax_ = -1e30f;
    done_ = false; failed_ = false;
  }

  float update(float pv, uint32_t nowMs) {
    if (done_ || failed_) return cfg_.relayLow;
    if (nowMs - startMs_ > cfg_.timeoutMs) { failed_ = true; return cfg_.relayLow; }

    if (pv < curMin_) curMin_ = pv;
    if (pv > curMax_) curMax_ = pv;

    if (high_ && pv > setpoint_ + cfg_.hysteresisC) {
      high_ = false; output_ = cfg_.relayLow;
    } else if (!high_ && pv < setpoint_ - cfg_.hysteresisC) {
      high_ = true; output_ = cfg_.relayHigh;
      if (haveLastCross_) {
        periodSum_ += (float)(nowMs - lastCrossMs_);
        ampSum_ += (curMax_ - curMin_) / 2.0f;
        if (++cycles_ >= cfg_.targetCycles) done_ = true;
      }
      lastCrossMs_ = nowMs; haveLastCross_ = true;
      curMin_ = 1e30f; curMax_ = -1e30f;
    }
    return output_;
  }

  bool done() const { return done_; }
  bool failed() const { return failed_; }
  int progressPct() const {
    if (failed_) return 100;
    if (cfg_.targetCycles <= 0) return 0;
    int p = (int)(100.0f * cycles_ / cfg_.targetCycles);
    return p > 100 ? 100 : p;
  }

  bool computeGains(float& kp, float& ki, float& kd) const {
    if (!done_ || cycles_ == 0) return false;
    const float Tu = (periodSum_ / cycles_) / 1000.0f;   // seconds
    const float a = ampSum_ / cycles_;                   // PV amplitude
    if (a <= 1e-4f || Tu <= 1e-3f) return false;
    const float d = (cfg_.relayHigh - cfg_.relayLow) / 2.0f;
    const float Ku = (4.0f * d) / (3.14159265f * a);
    kp = 0.45f * Ku;
    ki = kp / (2.2f * Tu);
    kd = kp * Tu / 6.3f;
    return true;
  }

 private:
  Config cfg_;
  float setpoint_ = 0.0f, output_ = 0.0f;
  bool high_ = true;
  uint32_t startMs_ = 0, lastCrossMs_ = 0;
  bool haveLastCross_ = false, done_ = false, failed_ = false;
  int cycles_ = 0;
  float periodSum_ = 0.0f, ampSum_ = 0.0f, curMin_ = 1e30f, curMax_ = -1e30f;
};
