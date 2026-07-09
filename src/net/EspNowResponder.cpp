#include "net/EspNowResponder.hpp"

#include <Arduino.h>
#include <WiFi.h>
#include <cstring>
#include "app_config.hpp"
#include "features/control/Reactor.hpp"
#include "net/WebInterface.hpp"
#include "sync/SyncCodec.hpp"

using namespace synclink;

namespace { EspNowResponder* s_self = nullptr; }

void EspNowResponder::begin() {
  if (!AppConfig::Features::kEnableEspNow) {
    Serial.println("[ESPNOW] disabled");
    return;
  }
  s_self = this;
  if (!link_.begin(&EspNowResponder::onRecvStatic)) {
    Serial.println("[ESPNOW] init failed");
    return;
  }
  uint8_t ch = 0;
  if (link_.loadBinding(AppConfig::EspNow::kNvsNamespace, peerMac_, ch)) {
    link_.addPeer(peerMac_, 0);   // 0 = follow our WiFi channel
    bound_ = true;
    channel_ = ch;
    Serial.printf("[ESPNOW] enabled — bound to %02X:%02X:%02X:%02X:%02X:%02X\n",
                  peerMac_[0], peerMac_[1], peerMac_[2], peerMac_[3], peerMac_[4], peerMac_[5]);
  } else {
    Serial.println("[ESPNOW] enabled — unbound");
  }
}

void EspNowResponder::reinit() {
  if (!AppConfig::Features::kEnableEspNow) return;
  link_.reinit();                          // rebuild esp_now (broadcast peer re-added inside)
  if (bound_) link_.addPeer(peerMac_, 0);  // re-add the bound HUB peer (channel 0 = follow radio)
  Serial.println("[ESPNOW] re-initialized after WiFi stack recovery");
}

void EspNowResponder::onRecvStatic(const uint8_t* mac, const uint8_t* data, int len) {
  if (s_self) s_self->handleRecv(mac, data, len);
}

void EspNowResponder::handleRecv(const uint8_t* mac, const uint8_t* data, int len) {
  if (len < (int)sizeof(Header)) return;
  Header h; std::memcpy(&h, data, sizeof(h));
  if (h.version != kProtocolVersion) return;
  switch ((MsgType)h.msgType) {
    case MsgType::Command:     handleCommand(mac, data, len); break;
    case MsgType::PairRequest: handlePairRequest(mac, data, len); break;  // Task 5
    default: break;
  }
}

void EspNowResponder::handleCommand(const uint8_t* mac, const uint8_t* data, int len) {
  Command c;
  if (!decodeCommand(data, len, c)) return;
  // Only the bound peer may command us.
  if (!bound_ || std::memcmp(mac, peerMac_, 6) != 0) return;

  const AckError err = validateCommand(c);
  if (err != AckError::None) {
    sendAck(mac, c.hdr.seq, c.opcode, AckResult::Rejected, err);
    return;
  }
  switch ((Opcode)c.opcode) {
    case Opcode::RunStart:
      if (c.flags & kCmdFlagUseCurrent) {
        // HUB-initiated start: no params on the hub — use the reactor's live config.
        const ReactorTelemetry t = reactor_.telemetry();
        web_.cmdRunStart(t.setpointC, reactor_.rpmSetpoint(), 0, "");  // 0 = run until stopped
      } else {
        web_.cmdRunStart(decFixed(c.targetC_c, kScaleTempC),
                         decFixed((int16_t)c.rpm_c, kScaleRpm), c.durationMin, c.name);
      }
      break;
    case Opcode::RunStop:
      web_.cmdRunStop((c.flags & kCmdFlagRunStopSave) != 0);
      break;
    case Opcode::Setpoint:
      web_.cmdSetpoint((c.presentMask & kSetHasTargetC), decFixed(c.targetC_c, kScaleTempC),
                       (c.presentMask & kSetHasRpm),     decFixed((int16_t)c.rpm_c, kScaleRpm));
      break;
    case Opcode::Disc:
      web_.cmdDisc((c.presentMask & kDiscHasRpm),     decFixed((int16_t)c.rpm_c, kScaleRpm),
                   (c.presentMask & kDiscHasCurrent), c.currentMa,
                   (c.presentMask & kDiscHasMicro),   c.microsteps,
                   (c.presentMask & kDiscHasDir),     (c.flags & kCmdFlagDiscReverse) != 0,
                   (c.presentMask & kDiscHasEnabled), (c.flags & kCmdFlagDiscEnabled) != 0);
      break;
    case Opcode::DiscTest: web_.cmdDiscTest(); break;
    case Opcode::Pause:    web_.cmdPause(c.flags & 0x0F); break;
    case Opcode::Resume:   web_.cmdPause(0); break;
    default: break;
  }
  sendAck(mac, c.hdr.seq, c.opcode, AckResult::Accepted, AckError::None);
}

void EspNowResponder::sendAck(const uint8_t* mac, uint16_t ackSeq, uint8_t opcode,
                              AckResult result, AckError err) {
  Ack a = {};
  a.hdr = {kProtocolVersion, (uint8_t)MsgType::Ack, link_.nextSeq()};
  a.ackSeq = ackSeq;
  a.opcode = opcode;
  a.result = (uint8_t)result;
  a.errorCode = (uint8_t)err;
  uint8_t buf[sizeof(Ack)];
  link_.send(mac, buf, encode(a, buf, sizeof(buf)));
}

void EspNowResponder::sendTelemetry() {
  const ReactorTelemetry t = reactor_.telemetry();
  Telemetry f = {};
  f.hdr = {kProtocolVersion, (uint8_t)MsgType::Telemetry, link_.nextSeq()};
  f.uptimeSec = millis() / 1000UL;
  f.flags = (t.running ? kFlagRunActive : 0)
          | ((t.rpm > 0.0f) ? kFlagMotorRunning : 0)
          | ((t.heaterDutyPct > 0.0f) ? kFlagHeaterActive : 0)
          | (t.sensorFault ? kFlagThermalFault : 0)
          | (t.safetyTripped ? kFlagSafetyTripped : 0)
          | (t.motorPaused ? kFlagMotorPaused : 0)
          | (t.fullHold ? kFlagFullHold : 0);
  f.tempC_c       = encFixedN(t.liquidTempC, kScaleTempC, !t.sensorFault);
  f.setpointC_c   = encFixed(t.setpointC, kScaleTempC);
  f.heaterPct_h   = (uint8_t)lroundf(t.heaterDutyPct * kScaleHeaterPct);
  f.heaterTempC_d = encFixedN(t.heaterTempC, kScaleHeaterC, !isnan(t.heaterTempC));
  f.processMaxC_d = encFixed(55.0f, kScaleHeaterC);   // process ceiling (API §status.safety.processMaxC)
  f.rpm_c         = (uint16_t)encFixed(t.rpm, kScaleRpm);
  f.rpmSetpoint_c = (uint16_t)encFixed(reactor_.rpmSetpoint(), kScaleRpm);
  f.load          = kNullI16;                          // StallGuard not in ReactorTelemetry; null for v1
  f.driverFlags   = 0;
  f.runId         = 0;                                 // run id is web-cache-side; 0 = unknown for v1
  f.elapsedSec    = t.elapsedSec;
  f.remainingSec  = (t.durationMin == 0) ? -1 : (int32_t)t.remainingSec;
  f.durationMin   = t.durationMin;
  f.name[0]       = '\0';                              // session name lives in SD sidecar; v1 leaves blank
  f.alarmBitmask  = 0;
  f.worstSeverity = t.safetyTripped ? 2 : (t.sensorFault ? 1 : 0);
  uint8_t buf[sizeof(Telemetry)];
  link_.send(peerMac_, buf, encode(f, buf, sizeof(buf)));
}

void EspNowResponder::poll() {
  if (!AppConfig::Features::kEnableEspNow || !link_.ready()) return;
  link_.poll();
  const uint32_t now = millis();
  if (bound_ && now - lastTelemetryMs_ >= AppConfig::EspNow::kTelemetryPeriodMs) {
    lastTelemetryMs_ = now;
    sendTelemetry();
  }
}

void EspNowResponder::openPairWindow() {
  if (!AppConfig::Features::kEnableEspNow || !link_.ready()) return;
  pairUntilMs_ = millis() + AppConfig::EspNow::kPairWindowMs;
  Serial.printf("[ESPNOW] pairing window open for %lus\n",
                (unsigned long)(AppConfig::EspNow::kPairWindowMs / 1000));
}

void EspNowResponder::forget() {
  if (bound_) {
    // Tell the HUB to drop us so it returns to its PAIR screen (Unpair), then
    // evict the peer. Best-effort: if the HUB is out of range it falls back to
    // its own disconnect handling.
    Header h = {kProtocolVersion, (uint8_t)MsgType::Unpair, link_.nextSeq()};
    uint8_t buf[sizeof(Header)];
    std::memcpy(buf, &h, sizeof(h));
    link_.send(peerMac_, buf, sizeof(h));
    link_.removePeer(peerMac_);
  }
  link_.clearBinding(AppConfig::EspNow::kNvsNamespace);
  bound_ = false;
  channel_ = 0;
  peerName_[0] = '\0';
  std::memset(peerMac_, 0, 6);
  Serial.println("[ESPNOW] binding cleared (HUB notified)");
}

void EspNowResponder::recalibrateHub() {
  if (!bound_) return;   // nothing paired to command; binding is preserved
  // Ask the bound HUB to re-run its first-boot touch calibration wizard. Bare
  // Header, best-effort (same pattern as forget's Unpair) — the binding stays.
  Header h = {kProtocolVersion, (uint8_t)MsgType::RecalibrateTouch, link_.nextSeq()};
  uint8_t buf[sizeof(Header)];
  std::memcpy(buf, &h, sizeof(h));
  link_.send(peerMac_, buf, sizeof(h));
  Serial.println("[ESPNOW] recalibrate sent to HUB");
}

void EspNowResponder::handlePairRequest(const uint8_t* mac, const uint8_t* data, int len) {
  if (millis() > pairUntilMs_) return;                // window closed
  PairRequest req;
  if (!decodePairRequest(data, len, req)) return;
  if ((DeviceRole)req.role != DeviceRole::Hub) return;  // only bind a HUB

  if (bound_) link_.removePeer(peerMac_);   // evict previously-bound HUB before rebinding
  std::memcpy(peerMac_, req.mac, 6);
  link_.addPeer(peerMac_, 0);
  const uint8_t ch = (uint8_t)WiFi.channel();
  link_.saveBinding(AppConfig::EspNow::kNvsNamespace, peerMac_, ch);
  bound_ = true;
  channel_ = ch;
  std::strncpy(peerName_, req.name, kNameLen - 1);
  peerName_[kNameLen - 1] = '\0';
  pairUntilMs_ = 0;

  PairAck ack = {};
  ack.hdr = {kProtocolVersion, (uint8_t)MsgType::PairAck, link_.nextSeq()};
  ack.role = (uint8_t)DeviceRole::Reactor;
  WiFi.macAddress(ack.mac);            // reactor's own MAC, per PROTOCOL_ESPNOW.md
  ack.channel = ch;
  std::strncpy(ack.name, AppConfig::EspNow::kDeviceName, kNameLen - 1);
  uint8_t buf[sizeof(PairAck)];
  link_.send(mac, buf, encode(ack, buf, sizeof(buf)));
  Serial.printf("[ESPNOW] paired with HUB %02X:%02X:%02X:%02X:%02X:%02X on ch %u\n",
                peerMac_[0], peerMac_[1], peerMac_[2], peerMac_[3], peerMac_[4], peerMac_[5], ch);
}
