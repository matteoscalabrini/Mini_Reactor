#pragma once
#include <cstdint>
#include "sync/EspNowLink.hpp"

class Reactor;
class WebInterface;

// Reactor-side ESP-NOW endpoint: pushes telemetry to the bound HUB, applies
// incoming commands via WebInterface's command-intake, replies with Acks, and
// owns the allow-pairing window. Gated by AppConfig::Features::kEnableEspNow.

class EspNowResponder {
 public:
  EspNowResponder(Reactor& reactor, WebInterface& web) : reactor_(reactor), web_(web) {}

  void begin();                 // gated init: esp_now + load binding
  void poll();                  // drain link + push telemetry at cadence
  void openPairWindow();        // POST /espnow/pair
  void forget();                // POST /espnow/forget — notifies the HUB (Unpair) then drops it
  void recalibrateHub();        // POST /espnow/recalibrate — asks the bound HUB to re-run its touch wizard
  bool bound() const { return bound_; }
  const uint8_t* peerMac() const { return peerMac_; }   // 6 bytes; valid when bound()
  const char* peerName() const { return peerName_; }    // HUB name from pairing ("" if unknown)
  uint8_t channel() const { return channel_; }

 private:
  static void onRecvStatic(const uint8_t* mac, const uint8_t* data, int len);
  void handleRecv(const uint8_t* mac, const uint8_t* data, int len);
  void handleCommand(const uint8_t* mac, const uint8_t* data, int len);
  void handlePairRequest(const uint8_t* mac, const uint8_t* data, int len);  // Task 5
  void sendTelemetry();
  void sendAck(const uint8_t* mac, uint16_t ackSeq, uint8_t opcode,
               synclink::AckResult result, synclink::AckError err);

  Reactor& reactor_;
  WebInterface& web_;
  EspNowLink link_;
  bool      bound_ = false;
  uint8_t   peerMac_[6] = {0};
  uint8_t   channel_ = 0;                      // WiFi channel at bind time
  char      peerName_[synclink::kNameLen] = {0};
  uint32_t  lastTelemetryMs_ = 0;
  uint32_t  pairUntilMs_ = 0;   // millis() deadline; 0 = closed
};
