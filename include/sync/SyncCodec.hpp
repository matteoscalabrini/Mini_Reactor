#pragma once
#include <cmath>
#include <cstddef>
#include <cstring>
#include "sync/SyncFrames.hpp"

// Pure encode/decode + fixed-point scaling + command validation. Header-only,
// no Arduino deps. encode() copies a packed struct to a buffer; decode*()
// validates length + version + msgType before copying back.

namespace sync {

inline int16_t encFixed(float v, float scale) { return (int16_t)lroundf(v * scale); }
inline float   decFixed(int16_t v, float scale) { return (float)v / scale; }
inline int16_t encFixedN(float v, float scale, bool valid) { return valid ? encFixed(v, scale) : kNullI16; }

inline bool headerOk(const uint8_t* buf, size_t len, MsgType type, size_t need) {
  if (!buf || len < need) return false;
  Header h; std::memcpy(&h, buf, sizeof(h));
  return h.version == kProtocolVersion && h.msgType == (uint8_t)type;
}

inline size_t encode(const Telemetry& f, uint8_t* out, size_t cap) {
  if (!out || cap < sizeof(f)) return 0; std::memcpy(out, &f, sizeof(f)); return sizeof(f); }
inline size_t encode(const Command& f, uint8_t* out, size_t cap) {
  if (!out || cap < sizeof(f)) return 0; std::memcpy(out, &f, sizeof(f)); return sizeof(f); }
inline size_t encode(const Ack& f, uint8_t* out, size_t cap) {
  if (!out || cap < sizeof(f)) return 0; std::memcpy(out, &f, sizeof(f)); return sizeof(f); }
inline size_t encode(const PairRequest& f, uint8_t* out, size_t cap) {
  if (!out || cap < sizeof(f)) return 0; std::memcpy(out, &f, sizeof(f)); return sizeof(f); }
inline size_t encode(const PairAck& f, uint8_t* out, size_t cap) {
  if (!out || cap < sizeof(f)) return 0; std::memcpy(out, &f, sizeof(f)); return sizeof(f); }

inline bool decodeTelemetry(const uint8_t* in, size_t len, Telemetry& out) {
  if (!headerOk(in, len, MsgType::Telemetry, sizeof(out))) return false; std::memcpy(&out, in, sizeof(out)); return true; }
inline bool decodeCommand(const uint8_t* in, size_t len, Command& out) {
  if (!headerOk(in, len, MsgType::Command, sizeof(out))) return false; std::memcpy(&out, in, sizeof(out)); return true; }
inline bool decodeAck(const uint8_t* in, size_t len, Ack& out) {
  if (!headerOk(in, len, MsgType::Ack, sizeof(out))) return false; std::memcpy(&out, in, sizeof(out)); return true; }
inline bool decodePairRequest(const uint8_t* in, size_t len, PairRequest& out) {
  if (!headerOk(in, len, MsgType::PairRequest, sizeof(out))) return false; std::memcpy(&out, in, sizeof(out)); return true; }
inline bool decodePairAck(const uint8_t* in, size_t len, PairAck& out) {
  if (!headerOk(in, len, MsgType::PairAck, sizeof(out))) return false; std::memcpy(&out, in, sizeof(out)); return true; }

// Range-validate a decoded command. Mirrors the REST bounds: targetC 0..55,
// rpm 0..30. currentMa/microsteps are clamped by the reactor, so not checked.
inline AckError validateCommand(const Command& c) {
  switch ((Opcode)c.opcode) {
    case Opcode::RunStart: {
      const float t = decFixed(c.targetC_c, kScaleTempC);
      const float r = decFixed((int16_t)c.rpm_c, kScaleRpm);
      if (t < 0.0f || t > 55.0f) return AckError::OutOfRange;
      if (r < 0.0f || r > 30.0f) return AckError::OutOfRange;
      return AckError::None;
    }
    case Opcode::Setpoint: {
      if (c.presentMask & kSetHasTargetC) {
        const float t = decFixed(c.targetC_c, kScaleTempC);
        if (t < 0.0f || t > 55.0f) return AckError::OutOfRange;
      }
      if (c.presentMask & kSetHasRpm) {
        const float r = decFixed((int16_t)c.rpm_c, kScaleRpm);
        if (r < 0.0f || r > 30.0f) return AckError::OutOfRange;
      }
      return AckError::None;
    }
    case Opcode::Disc: {
      if (c.presentMask & kDiscHasRpm) {
        const float r = decFixed((int16_t)c.rpm_c, kScaleRpm);
        if (r < 0.0f || r > 30.0f) return AckError::OutOfRange;
      }
      return AckError::None;
    }
    case Opcode::Pause: {
      const uint8_t tgt = c.flags & 0x0F;
      if (tgt != kPauseTargetMotor && tgt != kPauseTargetAll) return AckError::InvalidRequest;
      return AckError::None;
    }
    case Opcode::RunStop:
    case Opcode::DiscTest:
    case Opcode::Resume:
      return AckError::None;
    default:
      return AckError::InvalidRequest;
  }
}

}  // namespace sync
