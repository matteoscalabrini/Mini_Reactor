/*
 * WifiWatchdog.hpp — detect the WiFi-driver TX-path wedge and ask for a stack
 * restart. Pure, header-only, dependency-free (host-unit-tested).
 *
 * The failure (root-caused 2026-07-09): under sustained TX load the WiFi driver's
 * internal TX-buffer pool exhausts (esp_now_send returns ESP_ERR_ESPNOW_NO_MEM
 * 0x3067); both TCP (web UI) and ESP-NOW stop transmitting while the STA stays
 * associated, so WL_CONNECTED never drops and nothing self-heals.
 *
 * Detection is fed a monotonic TX "attempts" and "oks" counter each tick (from
 * EspNowLink::txDiag — the only always-present TX we can see the result of).
 * update() runs at loop rate (~3 ms) while sends happen every 250 ms, so the
 * detector tracks whether attempts are OUTSTANDING (advanced without any ok
 * since): the stall clock freezes at the first unanswered send and only progress
 * or disconnect clears it. Per-tick "am I sending right now" must NOT reset the
 * clock — that made the original detector provably unable to fire (the clock
 * was reset on the ~77 idle ticks between every send). It deliberately never
 * fires when idle (nothing outstanding) or disconnected — not a wedge.
 */
#pragma once

#include <cstdint>

class WifiWatchdog {
 public:
  struct Config {
    uint32_t stallMs = 30000;    // sustained "transmitting but 0 landing" before recover
    uint32_t backoffMs = 30000;  // min gap between recovery triggers
  };

  void setConfig(const Config& c) { cfg_ = c; }

  // Call once per tick. Returns true exactly on the tick a recovery should run.
  bool update(uint32_t now, bool connected, uint32_t txAttempts, uint32_t txOks) {
    if (!connected) {  // down: not our failure mode; hold counters, disarm
      lastAttempts_ = txAttempts;
      lastOks_ = txOks;
      outstanding_ = false;
      return false;
    }
    const bool sending = (txAttempts != lastAttempts_);
    const bool progressing = (txOks != lastOks_);
    lastAttempts_ = txAttempts;
    lastOks_ = txOks;

    if (progressing) outstanding_ = false;  // something landed — healthy
    else if (sending) outstanding_ = true;  // attempts advancing, none landing
    if (!outstanding_) {  // healthy or idle: no unanswered sends, clock follows now
      healthyMs_ = now;
      return false;
    }
    // Unanswered sends outstanding since healthyMs_.
    if (now - healthyMs_ < cfg_.stallMs) return false;
    if (recovered_ && now - lastRecoverMs_ < cfg_.backoffMs) return false;
    lastRecoverMs_ = now;
    healthyMs_ = now;      // restart the stall clock after triggering
    outstanding_ = false;  // re-arm only on the next unanswered send
    recovered_ = true;
    return true;
  }

 private:
  Config cfg_;
  uint32_t lastAttempts_ = 0, lastOks_ = 0;
  uint32_t healthyMs_ = 0, lastRecoverMs_ = 0;
  bool outstanding_ = false;  // attempts advanced with no ok since
  bool recovered_ = false;
};
