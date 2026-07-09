#include <Arduino.h>
#include <Preferences.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <cstring>

#include "sync/EspNowLink.hpp"

namespace {
struct RxItem {
  uint8_t mac[6];
  uint8_t data[250];
  uint8_t len;
};
QueueHandle_t s_rxQueue = nullptr;
EspNowLink::RecvFn s_onRecv = nullptr;

// DIAG (network-wedge hunt): L2 send-health counters (see EspNowLink::txDiag).
uint32_t s_txSends = 0;    // esp_now_send calls issued
uint32_t s_txOks = 0;      // ... accepted by the driver TX path (ESP_OK)
int      s_txLastErr = 0;  // last non-OK esp_now_send return code

// ISR/Wi-Fi-task context: copy bytes into the queue, nothing else.
void onDataRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (!s_rxQueue || len <= 0 || len > 250) return;
  RxItem item;
  std::memcpy(item.mac, info->src_addr, 6);
  std::memcpy(item.data, data, len);
  item.len = (uint8_t)len;
  BaseType_t hpw = pdFALSE;
  xQueueSendFromISR(s_rxQueue, &item, &hpw);
  if (hpw) portYIELD_FROM_ISR();
}
}  // namespace

bool EspNowLink::begin(RecvFn onRecv) {
  if (ready_) return true;
  s_onRecv = onRecv;
  if (!s_rxQueue) s_rxQueue = xQueueCreate(8, sizeof(RxItem));
  if (!s_rxQueue) { Serial.println("[ESPNOW] queue alloc failed"); return false; }
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESPNOW] esp_now_init failed");
    return false;
  }
  esp_now_register_recv_cb(onDataRecv);
  if (!addPeer(synclink::kBroadcastMac, 0)) Serial.println("[ESPNOW] broadcast peer add failed");
  ready_ = true;
  return true;
}

void EspNowLink::reinit() {
  // A WiFi-stack restart (WIFI_OFF) tears down esp_now. Rebuild it: clear stale
  // state, re-init, re-register the recv cb, re-add the broadcast peer. The RX
  // queue is static and persists, so it is not recreated here.
  ready_ = false;
  esp_now_deinit();
  if (esp_now_init() != ESP_OK) { Serial.println("[ESPNOW] reinit: esp_now_init failed"); return; }
  esp_now_register_recv_cb(onDataRecv);
  if (!addPeer(synclink::kBroadcastMac, 0)) Serial.println("[ESPNOW] reinit: broadcast peer add failed");
  ready_ = true;
}

bool EspNowLink::seenBefore(uint16_t seq) {
  for (int i = 0; i < kRingSize; ++i)
    if (recentValid_[i] && recentSeq_[i] == seq) return true;
  recentSeq_[ringHead_] = seq;
  recentValid_[ringHead_] = true;
  ringHead_ = (ringHead_ + 1) % kRingSize;
  return false;
}

void EspNowLink::poll() {
  if (!s_rxQueue) return;
  RxItem item;
  while (xQueueReceive(s_rxQueue, &item, 0) == pdTRUE) {
    if (item.len >= sizeof(synclink::Header)) {
      synclink::Header h;
      std::memcpy(&h, item.data, sizeof(h));
      if (seenBefore(h.seq)) continue;  // drop 3x resend duplicates
    }
    if (s_onRecv) s_onRecv(item.mac, item.data, item.len);
  }
}

bool EspNowLink::send(const uint8_t* mac, const uint8_t* data, size_t len) {
  if (!ready_) return false;
  if (len > 250) { Serial.println("[ESPNOW] send: oversized frame"); return false; }
  bool ok = false;
  for (int i = 0; i < 3; ++i) {                        // 3x best-effort resend
    const esp_err_t e = esp_now_send(mac, data, len);
    ++s_txSends;
    if (e == ESP_OK) { ++s_txOks; ok = true; }
    else s_txLastErr = (int)e;
  }
  return ok;
}

void EspNowLink::txDiag(uint32_t& sends, uint32_t& oks, int& lastErr) {
  sends = s_txSends;
  oks = s_txOks;
  lastErr = s_txLastErr;
}

bool EspNowLink::sendBroadcast(const uint8_t* data, size_t len) {
  return send(synclink::kBroadcastMac, data, len);
}

bool EspNowLink::addPeer(const uint8_t* mac, uint8_t channel) {
  if (esp_now_is_peer_exist(mac)) return true;
  esp_now_peer_info_t p = {};
  std::memcpy(p.peer_addr, mac, 6);
  p.channel = channel;            // 0 = follow current radio channel
  p.ifidx   = WIFI_IF_STA;
  p.encrypt = false;
  return esp_now_add_peer(&p) == ESP_OK;
}

bool EspNowLink::removePeer(const uint8_t* mac) {
  return esp_now_del_peer(mac) == ESP_OK;
}

void EspNowLink::setChannel(uint8_t ch) {
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
}

bool EspNowLink::loadBinding(const char* ns, uint8_t macOut[6], uint8_t& chOut) {
  Preferences prefs;
  prefs.begin(ns, true);
  const bool bound = prefs.getBool("bound", false);
  size_t n = bound ? prefs.getBytes("peerMac", macOut, 6) : 0;
  chOut = bound ? prefs.getUChar("channel", 0) : 0;
  prefs.end();
  return bound && n == 6;
}

void EspNowLink::saveBinding(const char* ns, const uint8_t mac[6], uint8_t ch) {
  Preferences prefs;
  prefs.begin(ns, false);
  prefs.putBytes("peerMac", mac, 6);
  prefs.putUChar("channel", ch);
  prefs.putBool("bound", true);
  prefs.end();
}

void EspNowLink::clearBinding(const char* ns) {
  Preferences prefs;
  prefs.begin(ns, false);
  prefs.clear();
  prefs.end();
}
