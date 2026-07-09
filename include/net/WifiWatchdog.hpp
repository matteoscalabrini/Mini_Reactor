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
 * Fires when we are connected AND actively transmitting (attempts advancing) but
 * nothing lands (oks frozen) for stallMs. It deliberately never fires when idle
 * (not transmitting) or disconnected — an idle or down link is not a wedge.
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
      armed_ = false;
      return false;
    }
    const bool sending = (txAttempts != lastAttempts_);
    const bool progressing = (txOks != lastOks_);
    lastAttempts_ = txAttempts;
    lastOks_ = txOks;

    if (progressing || !sending || !armed_) {
      healthyMs_ = now;  // healthy, idle, or first connected observation
      armed_ = true;
      return false;
    }
    // Transmitting but nothing landing.
    if (now - healthyMs_ < cfg_.stallMs) return false;
    if (recovered_ && now - lastRecoverMs_ < cfg_.backoffMs) return false;
    lastRecoverMs_ = now;
    healthyMs_ = now;  // restart the stall clock after triggering
    recovered_ = true;
    return true;
  }

 private:
  Config cfg_;
  uint32_t lastAttempts_ = 0, lastOks_ = 0;
  uint32_t healthyMs_ = 0, lastRecoverMs_ = 0;
  bool armed_ = false;
  bool recovered_ = false;
};
