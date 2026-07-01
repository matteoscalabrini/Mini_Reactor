#include "features/hub/link/HubLink.hpp"

#include <Arduino.h>
#include <WiFi.h>
#include <cstring>
#include "app_config.hpp"
#include "sync/SyncCodec.hpp"

using namespace synclink;

namespace { HubLink* s_self = nullptr; }

void HubLink::begin() {
  if (!AppConfig::HubFeatures::kEnableEspNow) {
    Serial.println("[HUB] espnow: disabled");
    return;
  }
  s_self = this;
  WiFi.mode(WIFI_STA);            // radio on, no association (HUB has no creds)
  WiFi.disconnect(false, false);
  if (!link_.begin(&HubLink::onRecvStatic)) {
    Serial.println("[HUB] espnow: init failed");
    return;
  }
  if (link_.loadBinding(AppConfig::HubEspNow::kNvsNamespace, peerMac_, channel_)) {
    link_.setChannel(channel_);
    link_.addPeer(peerMac_, 0);
    state_ = State::Paired;
    lastTelemetryMs_ = millis();
    Serial.printf("[HUB] espnow: enabled — bound, ch %u\n", channel_);
  } else {
    state_ = State::Unpaired;
    Serial.println("[HUB] espnow: enabled — unpaired");
  }
}

void HubLink::startPairing() {
  if (!link_.ready()) return;
  state_ = State::Searching;
  sweepCh_ = AppConfig::HubEspNow::kChannelMin;
  sweepStepMs_ = millis();
  searchStartMs_ = millis();
  lastBeaconMs_ = 0;
  Serial.println("[HUB] espnow: pairing — sweeping channels");
}

void HubLink::onRecvStatic(const uint8_t* mac, const uint8_t* data, int len) {
  if (s_self) s_self->handleRecv(mac, data, len);
}

void HubLink::handleRecv(const uint8_t* mac, const uint8_t* data, int len) {
  if (len < (int)sizeof(Header)) return;
  Header h; std::memcpy(&h, data, sizeof(h));
  if (h.version != kProtocolVersion) return;
  switch ((MsgType)h.msgType) {
    case MsgType::PairAck: {
      PairAck a;
      if (!decodePairAck(data, len, a)) return;
      std::memcpy(peerMac_, a.mac, 6);
      channel_ = a.channel;
      link_.setChannel(channel_);
      link_.addPeer(peerMac_, 0);
      link_.saveBinding(AppConfig::HubEspNow::kNvsNamespace, peerMac_, channel_);
      state_ = State::Paired;
      lastTelemetryMs_ = millis();
      Serial.printf("[HUB] espnow: paired on ch %u\n", channel_);
      break;
    }
    case MsgType::Telemetry: {
      Telemetry t;
      if (!decodeTelemetry(data, len, t)) return;
      latest_ = t;
      lastTelemetryMs_ = millis();
      linkAlive_ = true;
      // Reconnect: telemetry only comes from our bound reactor. If we were
      // searching after a silence/channel change, lock this channel + resume.
      if (state_ == State::Searching && std::memcmp(mac, peerMac_, 6) == 0) {
        channel_ = (uint8_t)WiFi.channel();
        link_.saveBinding(AppConfig::HubEspNow::kNvsNamespace, peerMac_, channel_);
        state_ = State::Paired;
        Serial.printf("[HUB] espnow: reconnected on ch %u\n", channel_);
      }
      break;
    }
    case MsgType::Ack: {
      Ack a;
      if (decodeAck(data, len, a))
        Serial.printf("[HUB] ack op=%u result=%u err=%u\n", a.opcode, a.result, a.errorCode);
      break;
    }
    case MsgType::Unpair: {
      if (std::memcmp(mac, peerMac_, 6) != 0) break;   // only our bound reactor
      link_.clearBinding(AppConfig::HubEspNow::kNvsNamespace);
      link_.removePeer(peerMac_);
      std::memset(peerMac_, 0, 6);
      channel_ = 0;
      linkAlive_ = false;
      state_ = State::Unpaired;
      Serial.println("[HUB] espnow: reactor unpaired us — back to PAIR");
      break;
    }
    case MsgType::RecalibrateTouch: {
      if (std::memcmp(mac, peerMac_, 6) != 0) break;   // only our bound reactor
      recalRequested_ = true;                          // HubRuntime re-runs the touch wizard
      Serial.println("[HUB] espnow: reactor requested touch recalibration");
      break;
    }
    default: break;
  }
}

void HubLink::tick() {
  if (!AppConfig::HubFeatures::kEnableEspNow || !link_.ready()) return;
  link_.poll();
  const uint32_t now = millis();

  if (state_ == State::Searching) {
    if (now - searchStartMs_ >= AppConfig::HubEspNow::kSearchGiveUpMs) {
      state_ = State::Unpaired;   // no reactor found -> let the user re-initiate from PAIR
      linkAlive_ = false;
      Serial.println("[HUB] espnow: search timed out — back to PAIR");
      return;
    }
    if (now - sweepStepMs_ >= AppConfig::HubEspNow::kSweepDwellMs) {
      sweepStepMs_ = now;
      sweepCh_++;
      if (sweepCh_ > AppConfig::HubEspNow::kChannelMax) sweepCh_ = AppConfig::HubEspNow::kChannelMin;
      link_.setChannel(sweepCh_);
    }
    if (now - lastBeaconMs_ >= AppConfig::HubEspNow::kPairBeaconMs) {
      lastBeaconMs_ = now;
      PairRequest req = {};
      req.hdr = {kProtocolVersion, (uint8_t)MsgType::PairRequest, link_.nextSeq()};
      req.role = (uint8_t)DeviceRole::Hub;
      WiFi.macAddress(req.mac);
      std::strncpy(req.name, AppConfig::HubEspNow::kDeviceName, kNameLen - 1);
      uint8_t buf[sizeof(PairRequest)];
      link_.sendBroadcast(buf, encode(req, buf, sizeof(buf)));
    }
    return;
  }

  if (state_ == State::Paired) {
    linkAlive_ = (now - lastTelemetryMs_) < AppConfig::HubEspNow::kLinkLostMs;
    if (!linkAlive_ && now - lastTelemetryMs_ > AppConfig::HubEspNow::kLinkLostMs * 2) {
      startPairing();   // reactor silent too long -> re-discover its channel
    }
  }
}

void HubLink::send(const Command& c) {
  if (state_ != State::Paired) return;
  uint8_t buf[sizeof(Command)];
  link_.send(peerMac_, buf, encode(c, buf, sizeof(buf)));
}

void HubLink::sendStop() {
  Command c = {};
  c.hdr = {kProtocolVersion, (uint8_t)MsgType::Command, link_.nextSeq()};
  c.opcode = (uint8_t)Opcode::RunStop;
  c.flags = kCmdFlagRunStopSave;
  send(c);
}

void HubLink::sendStart() {
  Command c = {};
  c.hdr = {kProtocolVersion, (uint8_t)MsgType::Command, link_.nextSeq()};
  c.opcode = (uint8_t)Opcode::RunStart;
  c.flags = kCmdFlagUseCurrent;   // reactor starts with its live setpoint/rpm, run-until-stopped
  send(c);
}

void HubLink::sendPause(uint8_t mode) {
  Command c = {};
  c.hdr = {kProtocolVersion, (uint8_t)MsgType::Command, link_.nextSeq()};
  c.opcode = mode == 0 ? (uint8_t)Opcode::Resume : (uint8_t)Opcode::Pause;
  c.flags = mode;
  send(c);
}
