// Arduino/ESP-IDF headers include lwipopts.h→unistd.h which declares
// void sync(void) — a POSIX stub that conflicts with namespace sync
// (SyncFrames.hpp).  Rename the POSIX identifier during those includes,
// then undef so namespace sync can be declared cleanly afterwards.
#define sync _posix_sync_DO_NOT_CALL  // suppress POSIX void sync() clash

#include <Arduino.h>
#include <nvs.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <cstring>

#undef sync  // restore identifier; namespace sync (SyncFrames.hpp) is now safe

#include "sync/EspNowLink.hpp"

namespace {
struct RxItem {
  uint8_t mac[6];
  uint8_t data[250];
  uint8_t len;
};
QueueHandle_t s_rxQueue = nullptr;
EspNowLink::RecvFn s_onRecv = nullptr;

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
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESPNOW] esp_now_init failed");
    return false;
  }
  esp_now_register_recv_cb(onDataRecv);
  addPeer(sync::kBroadcastMac, 0);
  ready_ = true;
  return true;
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
    if (item.len >= sizeof(sync::Header)) {
      sync::Header h;
      std::memcpy(&h, item.data, sizeof(h));
      if (seenBefore(h.seq)) continue;  // drop 3x resend duplicates
    }
    if (s_onRecv) s_onRecv(item.mac, item.data, item.len);
  }
}

bool EspNowLink::send(const uint8_t* mac, const uint8_t* data, size_t len) {
  if (!ready_) return false;
  bool ok = false;
  for (int i = 0; i < 3; ++i)                          // 3x best-effort resend
    ok = (esp_now_send(mac, data, len) == ESP_OK) || ok;
  return ok;
}

bool EspNowLink::sendBroadcast(const uint8_t* data, size_t len) {
  return send(sync::kBroadcastMac, data, len);
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

// NVS used directly (not via Arduino Preferences) because Preferences.h pulls
// in the same lwipopts.h→unistd.h chain even outside the #define guard above.
bool EspNowLink::loadBinding(const char* ns, uint8_t macOut[6], uint8_t& chOut) {
  nvs_handle_t h;
  if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return false;
  uint8_t bound = 0;
  nvs_get_u8(h, "bound", &bound);
  size_t n = 6;
  const bool macOk = bound && nvs_get_blob(h, "peerMac", macOut, &n) == ESP_OK && n == 6;
  chOut = 0;
  if (bound) nvs_get_u8(h, "channel", &chOut);
  nvs_close(h);
  return macOk;
}

void EspNowLink::saveBinding(const char* ns, const uint8_t mac[6], uint8_t ch) {
  nvs_handle_t h;
  if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_blob(h, "peerMac", mac, 6);
  nvs_set_u8(h, "channel", ch);
  nvs_set_u8(h, "bound", 1);
  nvs_commit(h);
  nvs_close(h);
}

void EspNowLink::clearBinding(const char* ns) {
  nvs_handle_t h;
  if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return;
  nvs_erase_all(h);
  nvs_commit(h);
  nvs_close(h);
}
