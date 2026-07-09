#pragma once
#include <cstddef>
#include <cstdint>
#include "sync/SyncFrames.hpp"

// Reusable ESP-NOW transport for both products. Protocol-agnostic beyond seq
// dedup: callers encode/decode sync frames themselves. The recv callback runs
// in loop context (poll()), never in the ESP-NOW ISR.

class EspNowLink {
 public:
  using RecvFn = void (*)(const uint8_t* mac, const uint8_t* data, int len);

  bool begin(RecvFn onRecv);          // esp_now_init + register cbs + broadcast peer
  void poll();                        // drain RX queue -> onRecv (with dedup)

  bool send(const uint8_t* mac, const uint8_t* data, size_t len);  // 3x resend
  bool sendBroadcast(const uint8_t* data, size_t len);

  bool addPeer(const uint8_t* mac, uint8_t channel);  // channel 0 = follow radio
  bool removePeer(const uint8_t* mac);
  void setChannel(uint8_t ch);        // HUB-only; reactor stays on its WiFi channel

  uint16_t nextSeq() { return ++seq_; }

  bool loadBinding(const char* ns, uint8_t macOut[6], uint8_t& chOut);
  void saveBinding(const char* ns, const uint8_t mac[6], uint8_t ch);
  void clearBinding(const char* ns);

  bool ready() const { return ready_; }

  // DIAG (network-wedge hunt): L2 TX health. ESP-NOW bypasses lwIP/TCP entirely,
  // so comparing this against the (dead) web path localizes a wedge: if enOk
  // keeps climbing while HTTP is dead the fault is above L2 (TCP/lwIP); if enOk
  // freezes too, the WiFi driver itself has wedged (lastErr carries the code,
  // e.g. ESP_ERR_ESPNOW_NO_MEM = TX-buffer exhaustion). Static so a heartbeat can
  // read it without the instance. Counts each esp_now_send call (3 per send()).
  static void txDiag(uint32_t& sends, uint32_t& oks, int& lastErr);

 private:
  bool     ready_ = false;
  uint16_t seq_   = 0;

  // seq dedup ring (single peer; drops the 3x resend duplicates)
  static constexpr int kRingSize = 16;
  uint16_t recentSeq_[kRingSize] = {0};
  bool     recentValid_[kRingSize] = {false};
  int      ringHead_ = 0;
  bool seenBefore(uint16_t seq);
};
