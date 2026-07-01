#pragma once
#include <cstdint>
#include "sync/EspNowLink.hpp"
#include "sync/SyncFrames.hpp"

// HUB-side ESP-NOW client. Discovers the reactor's channel by sweeping while
// unpaired, persists the binding, decodes telemetry, and sends commands.
// Gated by AppConfig::HubFeatures::kEnableEspNow.

class HubLink {
 public:
  enum class State : uint8_t { Unpaired, Searching, Paired };

  void begin();                 // gated init; load binding -> Paired, else Unpaired
  void tick();                  // drain link, run sweep, watch link-loss

  void startPairing();          // Pair button -> begin channel sweep
  void sendStop();              // RUN_STOP (save)
  void sendPause(uint8_t mode); // 1=motor B1, 2=all B2, 0=resume

  State state() const { return state_; }
  const synclink::Telemetry& latest() const { return latest_; }
  bool linkAlive() const { return linkAlive_; }
  uint8_t sweepChannel() const { return sweepCh_; }
  bool consumeRecalibrate() {           // reactor asked us to re-run the touch wizard (one-shot)
    if (!recalRequested_) return false;
    recalRequested_ = false;
    return true;
  }

 private:
  static void onRecvStatic(const uint8_t* mac, const uint8_t* data, int len);
  void handleRecv(const uint8_t* mac, const uint8_t* data, int len);
  void send(const synclink::Command& c);

  EspNowLink link_;
  State    state_ = State::Unpaired;
  uint8_t  peerMac_[6] = {0};
  uint8_t  channel_ = 0;
  // sweep
  uint8_t  sweepCh_ = 1;
  uint32_t sweepStepMs_ = 0;
  uint32_t lastBeaconMs_ = 0;
  uint32_t searchStartMs_ = 0;   // when the current Searching phase began (give-up timer)
  // telemetry / link-loss
  synclink::Telemetry latest_ = {};
  uint32_t lastTelemetryMs_ = 0;
  bool     linkAlive_ = false;
  bool     recalRequested_ = false;   // set by RecalibrateTouch from the bound reactor
};
