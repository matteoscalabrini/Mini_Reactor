# HUB ESP-NOW Link + Binding — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a bound, bidirectional ESP-NOW link so the WiFi-less HUB monitors and controls one reactor: the reactor sends compact binary telemetry, the HUB renders it and sends commands back, with a deliberate two-button pairing step.

**Architecture:** A reusable `src/sync/` transport (`EspNowLink`) + pure header-only frames/codec, compiled into both products. A reactor **responder** (`src/net/EspNowResponder`) encodes telemetry and routes commands through the reactor's existing `WebInterface` pending-command queue. A HUB **client** (`src/features/hub/link/HubLink`) decodes telemetry, drives a minimal 3-state LVGL screen, and sends commands. The HUB sets its own radio channel (sweep-to-discover, persist); the reactor stays on its WiFi channel.

**Tech Stack:** C++17, Arduino-ESP32 3.3.x / ESP-IDF 5.5.4 (pioarduino 55.03.39), ESP-NOW (built into IDF — no lib_dep), LVGL 8.3.11 (HUB only), Unity (native host tests), `Preferences` (NVS).

## Global Constraints

- Toolchain: pioarduino `55.03.39` → Arduino-ESP32 `3.3.x` → ESP-IDF `5.5.4`. Same for both products. Do not port.
- PlatformIO commands use the full path: `~/.platformio/penv/bin/pio`. Never bare `pio`.
- Envs: `hub`, `esp32-s3-devkitc-1` (reactor), `native` (host tests).
- Feature-toggle driven: gate ESP-NOW behind `AppConfig::Features::kEnableEspNow` (reactor) and `AppConfig::HubFeatures::kEnableEspNow` (HUB). Disabled ⇒ no `esp_now_init`, no radio use, explicit startup log, `503 feature_disabled` on the reactor pair endpoint.
- Reusable transport lives under `src/sync/` + `include/sync/` (NOT `features/`). Pure testable logic is **header-only** (no Arduino/ESP/app_config includes); device glue goes in `.cpp`.
- Scope: telemetry + control, **one** reactor. No multi-reactor/mesh, no payload encryption (v1), no HUB WiFi.
- ESP-NOW payload ≤ 250 bytes. All frames are `#pragma pack(1)` little-endian structs.
- YAGNI, one-liners, incremental changes. Keep `main.cpp`/`AppRuntime`/`HubRuntime` orchestration-focused.
- No partition-layout change (ESP-NOW adds code, not a flash region) ⇒ no forced erase.
- All git commits end with the trailer: `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`.
- Branch: `feature/hub-espnow-link` (already created; the design spec is committed there).

**Build separation (already correct — no `platformio.ini` filter edits needed):**
- `src/sync/**` is picked up by each device env's `+<*>` (shared by both).
- Reactor responder under `src/net/` → reactor builds it (`+<*>`), HUB excludes it (`-<net/>`).
- HUB client/screen under `src/features/hub/**` → HUB builds it (`+<*>`), reactor excludes it (`-<features/hub/>`).
- `native` does not build `src/*.cpp` (default `test_build_project_src=false`); only header-only logic links, so `EspNowLink.cpp` never reaches the host test.

**Reference:** `../Barebone` `src/network/SyncRuntime.inl` (ISR→queue→drain, 3× resend, NVS persist, peer add with `channel=0`). Design spec: `docs/superpowers/specs/2026-06-30-hub-espnow-link-design.md`.

---

## Task 1: Shared frames + codec (header-only) + native tests

Pure, host-testable foundation. No Arduino/ESP includes anywhere in this task.

**Files:**
- Create: `include/sync/SyncFrames.hpp`
- Create: `include/sync/SyncCodec.hpp`
- Test: `test/test_sync_codec/test_main.cpp`

**Interfaces:**
- Produces (used by Tasks 2–6):
  - `namespace sync` with `kProtocolVersion`, `kMacLen=6`, `kNameLen=33`, `kBroadcastMac[6]`, `kNullI16`.
  - `enum class MsgType : uint8_t { Telemetry=1, Command=2, Ack=3, PairRequest=4, PairAck=5 }`
  - `enum class DeviceRole : uint8_t { Reactor=1, Hub=2 }`
  - `enum class Opcode : uint8_t { RunStart=1, RunStop=2, Setpoint=3, Disc=4, DiscTest=5, Pause=6, Resume=7 }`
  - `enum class AckResult : uint8_t { Accepted=0, Rejected=1, FeatureDisabled=2 }`
  - `enum class AckError : uint8_t { None=0, InvalidRequest=1, OutOfRange=2, NotFound=3 }`
  - Flag bit consts `kFlag*` (telemetry), `kSetHasTargetC/kSetHasRpm`, `kDiscHas*`, `kCmdFlagDiscReverse/kCmdFlagDiscEnabled/kCmdFlagRunStopSave`, `kPauseTargetMotor=1/kPauseTargetAll=2`.
  - Packed structs `Header, Telemetry, Command, Ack, PairRequest, PairAck`.
  - Inline fns: `encFixed(float,float)->int16_t`, `decFixed(int16_t,float)->float`, `encFixedN(float,float,bool)->int16_t`, `encode(const T&, uint8_t*, size_t)->size_t` (overloaded per frame), `decodeTelemetry/decodeCommand/decodeAck/decodePairRequest/decodePairAck(const uint8_t*, size_t, T&)->bool`, `validateCommand(const Command&)->AckError`.

- [ ] **Step 1: Write `include/sync/SyncFrames.hpp`**

```cpp
#pragma once
#include <cstdint>

// Wire protocol for the reactor <-> HUB ESP-NOW link. Pure, header-only:
// no Arduino/ESP/app_config deps so the native host test links with no extra
// source. All frames are packed little-endian and fit one 250-byte ESP-NOW packet.

namespace sync {

constexpr uint8_t kProtocolVersion = 1;
constexpr uint8_t kMacLen  = 6;
constexpr uint8_t kNameLen = 33;                 // 32 chars + NUL
constexpr uint8_t kBroadcastMac[kMacLen] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
constexpr int16_t kNullI16 = INT16_MIN;          // "no value" sentinel for scaled fields

enum class MsgType   : uint8_t { Telemetry = 1, Command = 2, Ack = 3, PairRequest = 4, PairAck = 5 };
enum class DeviceRole: uint8_t { Reactor = 1, Hub = 2 };
enum class Opcode    : uint8_t { RunStart = 1, RunStop = 2, Setpoint = 3, Disc = 4, DiscTest = 5, Pause = 6, Resume = 7 };
enum class AckResult : uint8_t { Accepted = 0, Rejected = 1, FeatureDisabled = 2 };
enum class AckError  : uint8_t { None = 0, InvalidRequest = 1, OutOfRange = 2, NotFound = 3 };

// Telemetry.flags bits
constexpr uint16_t kFlagRunActive     = 1u << 0;
constexpr uint16_t kFlagMotorRunning  = 1u << 1;
constexpr uint16_t kFlagHeaterActive  = 1u << 2;
constexpr uint16_t kFlagThermalFault  = 1u << 3;
constexpr uint16_t kFlagSafetyTripped = 1u << 4;
constexpr uint16_t kFlagProbeFault    = 1u << 5;
constexpr uint16_t kFlagSdMounted     = 1u << 6;
constexpr uint16_t kFlagMotorPaused   = 1u << 7;
constexpr uint16_t kFlagFullHold      = 1u << 8;

// Command.presentMask bits (Setpoint)
constexpr uint8_t kSetHasTargetC = 1u << 0;
constexpr uint8_t kSetHasRpm     = 1u << 1;
// Command.presentMask bits (Disc)
constexpr uint8_t kDiscHasRpm    = 1u << 0;
constexpr uint8_t kDiscHasCurrent= 1u << 1;
constexpr uint8_t kDiscHasMicro  = 1u << 2;
constexpr uint8_t kDiscHasDir    = 1u << 3;
constexpr uint8_t kDiscHasEnabled= 1u << 4;
// Command.flags bits (boolean values)
constexpr uint8_t kCmdFlagDiscReverse = 1u << 0;
constexpr uint8_t kCmdFlagDiscEnabled = 1u << 1;
constexpr uint8_t kCmdFlagRunStopSave = 1u << 2;
// Pause targets (Command.flags low nibble when Opcode::Pause)
constexpr uint8_t kPauseTargetMotor = 1;
constexpr uint8_t kPauseTargetAll   = 2;

// Fixed-point scales
constexpr float kScaleTempC    = 100.0f;  // i16 centi-degrees
constexpr float kScaleHeaterC  = 10.0f;   // i16 deci-degrees
constexpr float kScaleRpm      = 100.0f;  // u16 centi-rpm
constexpr float kScaleHeaterPct= 2.0f;    // u8  half-percent (0..200)

#pragma pack(push, 1)
struct Header {
  uint8_t  version;   // kProtocolVersion
  uint8_t  msgType;   // MsgType
  uint16_t seq;
};

struct Telemetry {            // ~62 B
  Header   hdr;
  uint32_t uptimeSec;
  uint16_t flags;
  int16_t  tempC_c;           // x100; kNullI16 = fault/null
  int16_t  setpointC_c;       // x100
  uint8_t  heaterPct_h;       // x2  (0..200 => 0..100%)
  int16_t  heaterTempC_d;     // x10; kNullI16 = null
  int16_t  processMaxC_d;     // x10
  uint16_t rpm_c;             // x100
  uint16_t rpmSetpoint_c;     // x100
  int16_t  load;              // StallGuard; kNullI16 = null
  uint8_t  driverFlags;       // ot/otpw/stall/openload/connected
  uint16_t runId;             // 0 = idle
  uint32_t elapsedSec;
  int32_t  remainingSec;      // -1 = null (run-until-stopped)
  uint16_t durationMin;
  char     name[kNameLen];
  uint16_t alarmBitmask;
  uint8_t  worstSeverity;     // 0 none / 1 warn / 2 critical
};

struct Command {              // ~49 B
  Header   hdr;
  uint8_t  opcode;            // Opcode
  uint8_t  presentMask;       // Setpoint/Disc field presence
  int16_t  targetC_c;         // x100 (Setpoint/RunStart)
  uint16_t rpm_c;             // x100 (Setpoint/RunStart/Disc)
  uint16_t durationMin;       // RunStart
  uint16_t currentMa;         // Disc
  uint8_t  microsteps;        // Disc
  uint8_t  flags;             // boolean values + pause target
  char     name[kNameLen];    // RunStart
};

struct Ack {                  // 9 B
  Header   hdr;
  uint16_t ackSeq;            // echoes Command.hdr.seq
  uint8_t  opcode;
  uint8_t  result;            // AckResult
  uint8_t  errorCode;         // AckError
};

struct PairRequest {          // ~43 B
  Header  hdr;
  uint8_t role;               // DeviceRole (Hub)
  uint8_t mac[kMacLen];
  char    name[kNameLen];
};

struct PairAck {              // ~44 B
  Header  hdr;
  uint8_t role;               // DeviceRole (Reactor)
  uint8_t mac[kMacLen];
  uint8_t channel;            // reactor's current WiFi channel
  char    name[kNameLen];
};
#pragma pack(pop)

}  // namespace sync
```

- [ ] **Step 2: Write `include/sync/SyncCodec.hpp`**

```cpp
#pragma once
#include <cmath>
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
  if (cap < sizeof(f)) return 0; std::memcpy(out, &f, sizeof(f)); return sizeof(f); }
inline size_t encode(const Command& f, uint8_t* out, size_t cap) {
  if (cap < sizeof(f)) return 0; std::memcpy(out, &f, sizeof(f)); return sizeof(f); }
inline size_t encode(const Ack& f, uint8_t* out, size_t cap) {
  if (cap < sizeof(f)) return 0; std::memcpy(out, &f, sizeof(f)); return sizeof(f); }
inline size_t encode(const PairRequest& f, uint8_t* out, size_t cap) {
  if (cap < sizeof(f)) return 0; std::memcpy(out, &f, sizeof(f)); return sizeof(f); }
inline size_t encode(const PairAck& f, uint8_t* out, size_t cap) {
  if (cap < sizeof(f)) return 0; std::memcpy(out, &f, sizeof(f)); return sizeof(f); }

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
```

- [ ] **Step 3: Write the failing test `test/test_sync_codec/test_main.cpp`**

```cpp
#include <unity.h>
#include "sync/SyncCodec.hpp"

using namespace sync;

void setUp() {}
void tearDown() {}

void test_fixed_point_round_trip() {
  TEST_ASSERT_EQUAL_INT16(3581, encFixed(35.81f, kScaleTempC));
  TEST_ASSERT_FLOAT_WITHIN(0.005f, 35.81f, decFixed(3581, kScaleTempC));
  TEST_ASSERT_EQUAL_INT16(kNullI16, encFixedN(0.0f, kScaleTempC, false));
  TEST_ASSERT_EQUAL_INT16(800, encFixedN(8.0f, kScaleRpm, true));
}

void test_telemetry_round_trip() {
  Telemetry t = {};
  t.hdr = {kProtocolVersion, (uint8_t)MsgType::Telemetry, 42};
  t.flags = kFlagRunActive | kFlagMotorRunning;
  t.tempC_c = encFixed(35.81f, kScaleTempC);
  t.setpointC_c = encFixed(36.0f, kScaleTempC);
  t.rpm_c = encFixed(8.0f, kScaleRpm);
  t.runId = 7;
  t.remainingSec = -1;
  std::strncpy(t.name, "Ethanol", kNameLen);

  uint8_t buf[250];
  const size_t n = encode(t, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_UINT32(sizeof(Telemetry), n);

  Telemetry got = {};
  TEST_ASSERT_TRUE(decodeTelemetry(buf, n, got));
  TEST_ASSERT_EQUAL_UINT16(42, got.hdr.seq);
  TEST_ASSERT_EQUAL_INT16(3581, got.tempC_c);
  TEST_ASSERT_EQUAL_UINT16(7, got.runId);
  TEST_ASSERT_EQUAL_INT(-1, got.remainingSec);
  TEST_ASSERT_EQUAL_STRING("Ethanol", got.name);
}

void test_decode_rejects_bad_version() {
  Telemetry t = {};
  t.hdr = {(uint8_t)(kProtocolVersion + 1), (uint8_t)MsgType::Telemetry, 1};
  uint8_t buf[250];
  const size_t n = encode(t, buf, sizeof(buf));
  Telemetry got = {};
  TEST_ASSERT_FALSE(decodeTelemetry(buf, n, got));
}

void test_decode_rejects_wrong_type_and_short() {
  Command c = {};
  c.hdr = {kProtocolVersion, (uint8_t)MsgType::Command, 1};
  uint8_t buf[250];
  const size_t n = encode(c, buf, sizeof(buf));
  Telemetry got = {};
  TEST_ASSERT_FALSE(decodeTelemetry(buf, n, got));   // msgType mismatch
  Command cg = {};
  TEST_ASSERT_FALSE(decodeCommand(buf, 4, cg));      // too short
}

void test_validate_command_bounds() {
  Command c = {};
  c.hdr = {kProtocolVersion, (uint8_t)MsgType::Command, 1};
  c.opcode = (uint8_t)Opcode::RunStart;
  c.targetC_c = encFixed(36.0f, kScaleTempC);
  c.rpm_c = (uint16_t)encFixed(8.0f, kScaleRpm);
  TEST_ASSERT_EQUAL_INT((int)AckError::None, (int)validateCommand(c));

  c.targetC_c = encFixed(70.0f, kScaleTempC);        // out of range
  TEST_ASSERT_EQUAL_INT((int)AckError::OutOfRange, (int)validateCommand(c));

  Command p = {};
  p.opcode = (uint8_t)Opcode::Pause;
  p.flags = 9;                                        // invalid target
  TEST_ASSERT_EQUAL_INT((int)AckError::InvalidRequest, (int)validateCommand(p));
  p.flags = kPauseTargetAll;
  TEST_ASSERT_EQUAL_INT((int)AckError::None, (int)validateCommand(p));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_fixed_point_round_trip);
  RUN_TEST(test_telemetry_round_trip);
  RUN_TEST(test_decode_rejects_bad_version);
  RUN_TEST(test_decode_rejects_wrong_type_and_short);
  RUN_TEST(test_validate_command_bounds);
  return UNITY_END();
}
```

- [ ] **Step 4: Run the test — expect FAIL (headers not created yet, or compile error if a name is wrong)**

Run: `~/.platformio/penv/bin/pio test -e native -f test_sync_codec`
Expected: FAIL/compile-error until Steps 1–2 headers compile. Once they do, the test must PASS. If it fails, fix the headers — do not edit the test to pass.

- [ ] **Step 5: Run the full native suite to confirm no regressions**

Run: `~/.platformio/penv/bin/pio test -e native`
Expected: all existing suites + `test_sync_codec` PASS.

- [ ] **Step 6: Commit**

```bash
git add include/sync/SyncFrames.hpp include/sync/SyncCodec.hpp test/test_sync_codec/
git commit -m "feat(sync): packed ESP-NOW frames + header-only codec with native tests

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: `EspNowLink` transport (device glue)

ESP-NOW init, peer/channel mgmt, 3× best-effort send, ISR→queue→drain receive, seq dedup, NVS binding. Compiled into both products. Not host-tested (ESP deps) — verified by build + on-device smoke, per repo convention.

**Files:**
- Create: `include/sync/EspNowLink.hpp`
- Create: `src/sync/EspNowLink.cpp`

**Interfaces:**
- Consumes: `sync::*` from Task 1; ESP-IDF `esp_now.h`, `esp_wifi.h`; Arduino `Preferences`.
- Produces (used by Tasks 4–6):
  - `class EspNowLink` with:
    - `using RecvFn = void (*)(const uint8_t* mac, const uint8_t* data, int len);`
    - `bool begin(RecvFn onRecv);` — `esp_now_init`, register cbs, add broadcast peer. Idempotent.
    - `void poll();` — drain RX queue, invoke `onRecv` in loop context (with seq dedup).
    - `bool send(const uint8_t* mac, const uint8_t* data, size_t len);` — 3× resend.
    - `bool sendBroadcast(const uint8_t* data, size_t len);`
    - `bool addPeer(const uint8_t* mac, uint8_t channel);` — `channel=0` follows radio.
    - `bool removePeer(const uint8_t* mac);`
    - `void setChannel(uint8_t ch);` — `esp_wifi_set_channel` (HUB only; never call on reactor).
    - `uint16_t nextSeq();` — monotonic per-device seq for outgoing headers.
    - `bool loadBinding(const char* ns, uint8_t macOut[6], uint8_t& chOut);`
    - `void saveBinding(const char* ns, const uint8_t mac[6], uint8_t ch);`
    - `void clearBinding(const char* ns);`

- [ ] **Step 1: Write `include/sync/EspNowLink.hpp`**

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <Preferences.h>
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
```

- [ ] **Step 2: Write `src/sync/EspNowLink.cpp`**

```cpp
#include "sync/EspNowLink.hpp"

#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <cstring>

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
```

- [ ] **Step 3: Build both device envs to verify the transport compiles**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Then: `~/.platformio/penv/bin/pio run -e hub`
Expected: both PASS. (First clean build may hit the benign pioarduino `command not found` once — re-run.)

- [ ] **Step 4: Confirm native suite still green (EspNowLink.cpp must NOT leak into host build)**

Run: `~/.platformio/penv/bin/pio test -e native`
Expected: PASS. If it tries to compile `EspNowLink.cpp` (ESP headers not found), something includes `EspNowLink.hpp` from a test — it must not. Keep the host test on `SyncCodec.hpp` only.

- [ ] **Step 5: Commit**

```bash
git add include/sync/EspNowLink.hpp src/sync/EspNowLink.cpp
git commit -m "feat(sync): EspNowLink transport — peer/channel, 3x send, ISR->queue recv, NVS binding

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: Reactor command-intake — extend `WebInterface` (incl. pause/resume)

Give the responder a public, mutex-guarded way to enqueue commands into the **existing** `Pending` queue, and teach `applyPending()` to apply pause/resume. One apply path for REST and ESP-NOW.

**Files:**
- Modify: `include/net/WebInterface.hpp` (add `Pending` fields + public `cmd*` methods)
- Modify: `src/net/WebInterface.cpp` (`applyPending()` pause handling + `cmd*` bodies)

**Interfaces:**
- Consumes: `Reactor` control methods `setMotorPaused(bool)`, `setFullHold(bool)` (exist).
- Produces (used by Task 4):
  - `void WebInterface::cmdRunStart(float targetC, float rpm, uint16_t durMin, const char* name);`
  - `void WebInterface::cmdRunStop(bool save);`
  - `void WebInterface::cmdSetpoint(bool hasT, float targetC, bool hasR, float rpm);`
  - `void WebInterface::cmdDisc(bool hasRpm, float rpm, bool hasCur, uint16_t mA, bool hasMicro, uint16_t micro, bool hasDir, bool reverse, bool hasEn, bool en);`
  - `void WebInterface::cmdDiscTest();`
  - `void WebInterface::cmdPause(uint8_t mode);  // 0=resume, 1=motor(B1), 2=all(B2)`

- [ ] **Step 1: Add pause fields to `Pending` in `include/net/WebInterface.hpp`**

Inside `struct Pending { ... }` (after the `motorTest` field), add:

```cpp
  bool pauseCmd = false;   // a pause/resume command is queued
  uint8_t pauseMode = 0;   // 0 = resume (clear both), 1 = motor B1, 2 = all B2
```

- [ ] **Step 2: Declare the public `cmd*` intake methods in `include/net/WebInterface.hpp`**

In the `public:` section of `class WebInterface` (near `begin()`), add:

```cpp
  // Command intake for non-HTTP transports (e.g. ESP-NOW responder). Each
  // takes the same mutex and sets the same Pending queue drained by
  // applyPending(), so ESP-NOW and REST share one validate+apply path.
  void cmdRunStart(float targetC, float rpm, uint16_t durMin, const char* name);
  void cmdRunStop(bool save);
  void cmdSetpoint(bool hasT, float targetC, bool hasR, float rpm);
  void cmdDisc(bool hasRpm, float rpm, bool hasCur, uint16_t mA,
               bool hasMicro, uint16_t micro, bool hasDir, bool reverse,
               bool hasEn, bool en);
  void cmdDiscTest();
  void cmdPause(uint8_t mode);   // 0=resume, 1=motor B1, 2=all B2
```

- [ ] **Step 3: Apply pause in `applyPending()` — `src/net/WebInterface.cpp`**

In `WebInterface::applyPending()`, after the `if (p.motorTest) { ... }` line, add:

```cpp
  if (p.pauseCmd) {
    if (p.pauseMode == 1) { Serial.println("[CMD] pause motor (B1)"); reactor_.setMotorPaused(true); }
    else if (p.pauseMode == 2) { Serial.println("[CMD] pause all (B2)"); reactor_.setFullHold(true); }
    else { Serial.println("[CMD] resume"); reactor_.setMotorPaused(false); reactor_.setFullHold(false); }
  }
```

- [ ] **Step 4: Implement the `cmd*` bodies in `src/net/WebInterface.cpp`**

Add at the end of the file (before any closing namespace, matching existing method style):

```cpp
void WebInterface::cmdRunStart(float targetC, float rpm, uint16_t durMin, const char* name) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  pending_.runStart = true;
  pending_.runTargetC = targetC;
  pending_.runRpm = rpm;
  pending_.runDurMin = durMin;
  copySanitizedName(pending_.runName, sizeof(pending_.runName), String(name ? name : ""));
  xSemaphoreGive(mutex_);
}

void WebInterface::cmdRunStop(bool save) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  pending_.runStop = true;
  pending_.runStopSave = save;
  xSemaphoreGive(mutex_);
}

void WebInterface::cmdSetpoint(bool hasT, float targetC, bool hasR, float rpm) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (hasT) { pending_.setTarget = true; pending_.setTargetC = targetC; }
  if (hasR) { pending_.setRpm = true; pending_.setRpmVal = rpm; }
  xSemaphoreGive(mutex_);
}

void WebInterface::cmdDisc(bool hasRpm, float rpm, bool hasCur, uint16_t mA,
                           bool hasMicro, uint16_t micro, bool hasDir, bool reverse,
                           bool hasEn, bool en) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (hasRpm)   { pending_.discRpm = true;     pending_.discRpmVal = rpm; }
  if (hasCur)   { pending_.discCurrent = true; pending_.discCurrentMa = mA; }
  if (hasMicro) { pending_.discMicro = true;   pending_.discMicrosteps = micro; }
  if (hasDir)   { pending_.discDir = true;     pending_.discReverse = reverse; }
  if (hasEn)    { pending_.discEnable = true;  pending_.discEnableVal = en; }
  xSemaphoreGive(mutex_);
}

void WebInterface::cmdDiscTest() {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  pending_.motorTest = true;
  xSemaphoreGive(mutex_);
}

void WebInterface::cmdPause(uint8_t mode) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  pending_.pauseCmd = true;
  pending_.pauseMode = mode;
  xSemaphoreGive(mutex_);
}
```

> Note: `copySanitizedName(...)` is the existing private helper used by the `/api/v1/run` handler (see `WebInterface.cpp`). Reuse it verbatim.

- [ ] **Step 5: Build the reactor to verify it compiles**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add include/net/WebInterface.hpp src/net/WebInterface.cpp
git commit -m "feat(net): public command-intake API + pause/resume in applyPending

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: Reactor responder — telemetry + command apply + Ack

`EspNowResponder` owns an `EspNowLink`, builds telemetry from `reactor_.telemetry()`, routes commands through `web_.cmd*()`, and replies with an `Ack`. Toggle-gated. Pairing is added in Task 5.

**Files:**
- Create: `include/net/EspNowResponder.hpp`
- Create: `src/net/EspNowResponder.cpp`
- Modify: `include/app_config.hpp` (add `Features::kEnableEspNow` + `EspNow` namespace)
- Modify: `src/system/AppRuntime.cpp` (construct + `begin()` + `tick()` hooks; advertise feature)

**Interfaces:**
- Consumes: `EspNowLink` (Task 2), `sync::*` + codec (Task 1), `WebInterface::cmd*` (Task 3), `Reactor::telemetry()`.
- Produces (used by Task 5): `class EspNowResponder` with `void begin();`, `void poll();`, `void openPairWindow();`, `void forget();`, `bool bound() const;`.

- [ ] **Step 1: Add reactor config to `include/app_config.hpp`**

In `namespace Features` (after `kEnableAutotune`), add:

```cpp
static constexpr bool kEnableEspNow    = true;   // ESP-NOW responder (HUB link)
```

After the `Features` namespace block, add:

```cpp
namespace EspNow {
static constexpr const char* kNvsNamespace = "espnow";
static constexpr uint32_t kTelemetryPeriodMs = 250;  // ~4 Hz, mirrors WS push
static constexpr uint32_t kPairWindowMs = 60000;     // 60 s allow-pairing window
static constexpr const char* kDeviceName = "reactor";
}  // namespace EspNow
```

- [ ] **Step 2: Write `include/net/EspNowResponder.hpp`**

```cpp
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
  void forget();                // POST /espnow/forget
  bool bound() const { return bound_; }

 private:
  static void onRecvStatic(const uint8_t* mac, const uint8_t* data, int len);
  void handleRecv(const uint8_t* mac, const uint8_t* data, int len);
  void handleCommand(const uint8_t* mac, const uint8_t* data, int len);
  void handlePairRequest(const uint8_t* mac, const uint8_t* data, int len);  // Task 5
  void sendTelemetry();
  void sendAck(const uint8_t* mac, uint16_t ackSeq, uint8_t opcode,
               sync::AckResult result, sync::AckError err);

  Reactor& reactor_;
  WebInterface& web_;
  EspNowLink link_;
  bool      bound_ = false;
  uint8_t   peerMac_[6] = {0};
  uint32_t  lastTelemetryMs_ = 0;
  uint32_t  pairUntilMs_ = 0;   // millis() deadline; 0 = closed
};
```

- [ ] **Step 3: Write `src/net/EspNowResponder.cpp` (telemetry + command apply + Ack; pairing stubbed for Task 5)**

```cpp
#include "net/EspNowResponder.hpp"

#include <Arduino.h>
#include <WiFi.h>
#include <cstring>
#include "app_config.hpp"
#include "features/control/Reactor.hpp"
#include "net/WebInterface.hpp"
#include "sync/SyncCodec.hpp"

using namespace sync;

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
    Serial.printf("[ESPNOW] enabled — bound to %02X:%02X:%02X:%02X:%02X:%02X\n",
                  peerMac_[0], peerMac_[1], peerMac_[2], peerMac_[3], peerMac_[4], peerMac_[5]);
  } else {
    Serial.println("[ESPNOW] enabled — unbound");
  }
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
      web_.cmdRunStart(decFixed(c.targetC_c, kScaleTempC),
                       decFixed((int16_t)c.rpm_c, kScaleRpm), c.durationMin, c.name);
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

// Pairing — implemented in Task 5.
void EspNowResponder::handlePairRequest(const uint8_t*, const uint8_t*, int) {}
void EspNowResponder::openPairWindow() {}
void EspNowResponder::forget() {}
```

> `load`, `runId`, and the session `name` are intentionally `null`/blank in v1 — they aren't in `ReactorTelemetry` (they live in the web/SD caches). The frame reserves them; wiring them is a trivial later add. Note this in the spec's "v1 leaves blank" notes during the docs task.

- [ ] **Step 4: Construct + advertise + hook in `src/system/AppRuntime.cpp`**

(a) After the `WebInterface g_web(...)` global (line ~161), add:

```cpp
#include "net/EspNowResponder.hpp"
EspNowResponder g_espnow(g_reactor, g_web);
```

(b) In `buildStatusJson()`, in the `features` block (after `feats["autotune"] = ...`), add:

```cpp
  feats["espnow"] = AppConfig::Features::kEnableEspNow;
```

(c) In `begin()`, after `g_web.begin();`, add:

```cpp
  g_espnow.begin();
```

(d) In `tick()`, after `g_web.update(statusJson, scanJson);`, add:

```cpp
  g_espnow.poll();
```

- [ ] **Step 5: Build the reactor (default + ESP-NOW off)**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
# flip AppConfig::Features::kEnableEspNow = false, rebuild, then restore to true
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
```
Expected: both PASS; with the flag off the boot log prints `[ESPNOW] disabled`.

- [ ] **Step 6: Commit**

```bash
git add include/net/EspNowResponder.hpp src/net/EspNowResponder.cpp include/app_config.hpp src/system/AppRuntime.cpp
git commit -m "feat(net): reactor ESP-NOW responder — telemetry push + command apply + Ack

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: Pairing & binding — reactor window + endpoints + HUB sweep glue

Mutual two-button pairing. Reactor opens a 60 s window (`POST /api/v1/espnow/pair`), answers `PairRequest` on its WiFi channel with a `PairAck`, persists the HUB MAC. `POST /api/v1/espnow/forget` clears it. (HUB sweep + UI in Task 6.)

**Files:**
- Modify: `src/net/EspNowResponder.cpp` (`handlePairRequest`, `openPairWindow`, `forget`)
- Modify: `src/net/WebInterface.cpp` (register `/api/v1/espnow/pair` + `/api/v1/espnow/forget`)
- Modify: `include/net/WebInterface.hpp` (hold an `EspNowResponder*` + setter)
- Modify: `src/system/AppRuntime.cpp` (wire the responder into WebInterface)

**Interfaces:**
- Consumes: `EspNowResponder::openPairWindow()/forget()/bound()` (Task 4), `AppConfig::Features::kEnableEspNow`.
- Produces: `void WebInterface::setEspNow(EspNowResponder* r);`

- [ ] **Step 1: Implement pairing in `src/net/EspNowResponder.cpp` (replace the Task-4 stubs)**

```cpp
void EspNowResponder::openPairWindow() {
  if (!AppConfig::Features::kEnableEspNow || !link_.ready()) return;
  pairUntilMs_ = millis() + AppConfig::EspNow::kPairWindowMs;
  Serial.printf("[ESPNOW] pairing window open for %lus\n",
                (unsigned long)(AppConfig::EspNow::kPairWindowMs / 1000));
}

void EspNowResponder::forget() {
  link_.clearBinding(AppConfig::EspNow::kNvsNamespace);
  if (bound_) link_.removePeer(peerMac_);
  bound_ = false;
  std::memset(peerMac_, 0, 6);
  Serial.println("[ESPNOW] binding cleared");
}

void EspNowResponder::handlePairRequest(const uint8_t* mac, const uint8_t* data, int len) {
  if (millis() > pairUntilMs_) return;                // window closed
  PairRequest req;
  if (!decodePairRequest(data, len, req)) return;
  if ((DeviceRole)req.role != DeviceRole::Hub) return;  // only bind a HUB

  std::memcpy(peerMac_, req.mac, 6);
  link_.addPeer(peerMac_, 0);
  const uint8_t ch = (uint8_t)WiFi.channel();
  link_.saveBinding(AppConfig::EspNow::kNvsNamespace, peerMac_, ch);
  bound_ = true;
  pairUntilMs_ = 0;

  PairAck ack = {};
  ack.hdr = {kProtocolVersion, (uint8_t)MsgType::PairAck, link_.nextSeq()};
  ack.role = (uint8_t)DeviceRole::Reactor;
  std::memcpy(ack.mac, peerMac_, 6);                  // echo so HUB confirms target
  ack.channel = ch;
  std::strncpy(ack.name, AppConfig::EspNow::kDeviceName, kNameLen - 1);
  uint8_t buf[sizeof(PairAck)];
  link_.send(mac, buf, encode(ack, buf, sizeof(buf)));
  Serial.printf("[ESPNOW] paired with HUB %02X:%02X:%02X:%02X:%02X:%02X on ch %u\n",
                peerMac_[0], peerMac_[1], peerMac_[2], peerMac_[3], peerMac_[4], peerMac_[5], ch);
}
```

- [ ] **Step 2: Hold an `EspNowResponder*` in `include/net/WebInterface.hpp`**

Add a forward declaration near the top: `class EspNowResponder;`
In `public:`, add: `void setEspNow(EspNowResponder* r) { espnow_ = r; }`
In `private:`, add: `EspNowResponder* espnow_ = nullptr;`

- [ ] **Step 3: Register the two endpoints in `src/net/WebInterface.cpp`**

Add `#include "net/EspNowResponder.hpp"` at the top. In `WebInterface::begin()` (where routes are registered), add:

```cpp
  // ── POST espnow/pair (open the 60s allow-pairing window) ──
  server_->on("/api/v1/espnow/pair", HTTP_POST, [this](AsyncWebServerRequest* req) {
    if (featureGate(req, AppConfig::Features::kEnableEspNow)) return;
    if (espnow_) espnow_->openPairWindow();
    sendOk(req);
  });

  // ── POST espnow/forget (clear binding) ──
  server_->on("/api/v1/espnow/forget", HTTP_POST, [this](AsyncWebServerRequest* req) {
    if (featureGate(req, AppConfig::Features::kEnableEspNow)) return;
    if (espnow_) espnow_->forget();
    sendOk(req);
  });
```

> `featureGate(...)` and `sendOk(...)` are existing static helpers in `WebInterface.cpp`. `AppConfig` is already included there.

- [ ] **Step 4: Wire the responder into WebInterface in `src/system/AppRuntime.cpp`**

In `begin()`, immediately after `g_espnow.begin();`, add:

```cpp
  g_web.setEspNow(&g_espnow);
```

- [ ] **Step 5: Build the reactor (default + ESP-NOW off)**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
# with kEnableEspNow=false, the pair/forget endpoints must 503; rebuild to confirm compile
```
Expected: PASS both ways.

- [ ] **Step 6: Commit**

```bash
git add include/net/EspNowResponder.hpp src/net/EspNowResponder.cpp include/net/WebInterface.hpp src/net/WebInterface.cpp src/system/AppRuntime.cpp
git commit -m "feat(net): ESP-NOW pairing window + /espnow/pair + /espnow/forget

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: HUB client + minimal 3-state UI

`HubLink` owns an `EspNowLink`, runs the channel sweep when unpaired, decodes telemetry into a snapshot, and sends commands. `EspNowScreen` shows Unpaired (Pair button) / Searching / Paired. `HubRuntime` switches from the bring-up screen to this one and pumps the link.

**Files:**
- Create: `include/features/hub/link/HubLink.hpp`
- Create: `src/features/hub/link/HubLink.cpp`
- Create: `include/features/hub/ui/EspNowScreen.hpp`
- Create: `src/features/hub/ui/EspNowScreen.cpp`
- Modify: `include/app_config.hpp` (HUB ESP-NOW config + toggle)
- Modify: `src/features/hub/HubRuntime.cpp` (own + pump `HubLink`; show `EspNowScreen`)

**Interfaces:**
- Consumes: `EspNowLink` (Task 2), `sync::*`/codec (Task 1), LVGL (label + lv_obj only), `WiFi`.
- Produces: `class HubLink { void begin(); void tick(); void startPairing(); void sendStop(); void sendPause(uint8_t mode); enum class State{Unpaired,Searching,Paired}; State state(); const sync::Telemetry& latest(); bool linkAlive(); };`
  `namespace EspNowScreen { struct View{...}; void create(); void update(const View&); bool pairPressed(); }`

- [ ] **Step 1: Add HUB config to `include/app_config.hpp`**

In `namespace HubFeatures` (after `kEnablePmicTelemetry`), add:

```cpp
static constexpr bool kEnableEspNow = true;       // ESP-NOW client (reactor link)
```

After the `HubFeatures` block, add:

```cpp
namespace HubEspNow {
static constexpr const char* kNvsNamespace = "espnow";
static constexpr const char* kDeviceName = "hub";
static constexpr uint8_t  kChannelMin = 1;
static constexpr uint8_t  kChannelMax = 13;        // sweep range (region max)
static constexpr uint32_t kSweepDwellMs = 120;     // listen per channel before hopping
static constexpr uint32_t kPairBeaconMs = 60;      // re-broadcast PairRequest cadence within a channel
static constexpr uint32_t kLinkLostMs = 3000;      // no telemetry => disconnected + re-sweep
}  // namespace HubEspNow
```

- [ ] **Step 2: Write `include/features/hub/link/HubLink.hpp`**

```cpp
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
  const sync::Telemetry& latest() const { return latest_; }
  bool linkAlive() const { return linkAlive_; }
  uint8_t sweepChannel() const { return sweepCh_; }

 private:
  static void onRecvStatic(const uint8_t* mac, const uint8_t* data, int len);
  void handleRecv(const uint8_t* mac, const uint8_t* data, int len);
  void send(const sync::Command& c);

  EspNowLink link_;
  State    state_ = State::Unpaired;
  uint8_t  peerMac_[6] = {0};
  uint8_t  channel_ = 0;
  // sweep
  uint8_t  sweepCh_ = 1;
  uint32_t sweepStepMs_ = 0;
  // telemetry / link-loss
  sync::Telemetry latest_ = {};
  uint32_t lastTelemetryMs_ = 0;
  bool     linkAlive_ = false;
};
```

- [ ] **Step 3: Write `src/features/hub/link/HubLink.cpp`**

```cpp
#include "features/hub/link/HubLink.hpp"

#include <Arduino.h>
#include <WiFi.h>
#include <cstring>
#include "app_config.hpp"
#include "sync/SyncCodec.hpp"

using namespace sync;

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
  sweepStepMs_ = 0;
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
      break;
    }
    case MsgType::Ack: {
      Ack a;
      if (decodeAck(data, len, a))
        Serial.printf("[HUB] ack op=%u result=%u err=%u\n", a.opcode, a.result, a.errorCode);
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
    if (now - sweepStepMs_ >= AppConfig::HubEspNow::kSweepDwellMs) {
      sweepStepMs_ = now;
      sweepCh_++;
      if (sweepCh_ > AppConfig::HubEspNow::kChannelMax) sweepCh_ = AppConfig::HubEspNow::kChannelMin;
      link_.setChannel(sweepCh_);
    }
    PairRequest req = {};
    req.hdr = {kProtocolVersion, (uint8_t)MsgType::PairRequest, link_.nextSeq()};
    req.role = (uint8_t)DeviceRole::Hub;
    WiFi.macAddress(req.mac);
    std::strncpy(req.name, AppConfig::HubEspNow::kDeviceName, kNameLen - 1);
    uint8_t buf[sizeof(PairRequest)];
    link_.sendBroadcast(buf, encode(req, buf, sizeof(buf)));
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

void HubLink::sendPause(uint8_t mode) {
  Command c = {};
  c.hdr = {kProtocolVersion, (uint8_t)MsgType::Command, link_.nextSeq()};
  c.opcode = mode == 0 ? (uint8_t)Opcode::Resume : (uint8_t)Opcode::Pause;
  c.flags = mode;
  send(c);
}
```

- [ ] **Step 4: Write `include/features/hub/ui/EspNowScreen.hpp`**

```cpp
#pragma once
#include <cstdint>

// Minimal 3-state HUB link screen (label + lv_obj only, like BringupScreen).
// Unpaired: a "PAIR" tappable tile. Searching: sweep status. Paired: telemetry.

namespace EspNowScreen {
enum class Mode : uint8_t { Unpaired, Searching, Paired };
struct View {
  Mode    mode;
  uint8_t sweepChannel;
  bool    linkAlive;
  // telemetry (valid when Paired)
  float   tempC;        bool tempValid;
  float   setpointC;
  float   heaterPct;
  float   rpm;
  bool    runActive;
  bool    motorPaused;
  bool    fullHold;
  bool    safetyTripped;
  uint32_t elapsedSec;
};
void create();
void update(const View& v);
bool pairPressed();   // true once after the PAIR tile is tapped (consumes the latch)
}  // namespace EspNowScreen
```

- [ ] **Step 5: Write `src/features/hub/ui/EspNowScreen.cpp`**

```cpp
#include "features/hub/ui/EspNowScreen.hpp"
#include <lvgl.h>
#include <cstdio>

// Only lv_label + lv_obj (colored rectangles) — matches BringupScreen's allowed
// widget set. The PAIR tile is an lv_obj with LV_OBJ_FLAG_CLICKABLE + an event cb
// (LV_USE_BTN is 0 in lv_conf.h, but object click events are available).

namespace EspNowScreen {
namespace {
lv_obj_t* s_title = nullptr;
lv_obj_t* s_pairTile = nullptr;
lv_obj_t* s_pairLbl = nullptr;
lv_obj_t* s_line1 = nullptr;
lv_obj_t* s_line2 = nullptr;
lv_obj_t* s_line3 = nullptr;
volatile bool s_pairLatch = false;

void onPairClicked(lv_event_t*) { s_pairLatch = true; }

lv_obj_t* makeLbl(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, const char* txt) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_pos(l, x, y);
  lv_label_set_text(l, txt);
  return l;
}
}  // namespace

void create() {
  lv_obj_t* scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, LV_COLOR_MAKE(0x10, 0x10, 0x10), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  s_title = makeLbl(scr, 12, 8, "REACTOR LINK");

  s_pairTile = lv_obj_create(scr);
  lv_obj_set_size(s_pairTile, 180, 60);
  lv_obj_set_pos(s_pairTile, 140, 200);
  lv_obj_set_style_radius(s_pairTile, 10, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_pairTile, LV_COLOR_MAKE(0x0a, 0x84, 0xff), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_pairTile, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(s_pairTile, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_pairTile, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s_pairTile, onPairClicked, LV_EVENT_CLICKED, nullptr);
  s_pairLbl = lv_label_create(s_pairTile);
  lv_label_set_text(s_pairLbl, "PAIR");
  lv_obj_center(s_pairLbl);

  s_line1 = makeLbl(scr, 12, 60,  "");
  s_line2 = makeLbl(scr, 12, 92,  "");
  s_line3 = makeLbl(scr, 12, 124, "");
}

void update(const View& v) {
  if (!s_title) return;
  char b[96];

  const bool showPair = (v.mode == Mode::Unpaired);
  if (showPair) lv_obj_clear_flag(s_pairTile, LV_OBJ_FLAG_HIDDEN);
  else          lv_obj_add_flag(s_pairTile, LV_OBJ_FLAG_HIDDEN);

  switch (v.mode) {
    case Mode::Unpaired:
      lv_label_set_text(s_line1, "Not paired");
      lv_label_set_text(s_line2, "Tap PAIR, then press");
      lv_label_set_text(s_line3, "'Pair HUB' on the reactor");
      break;
    case Mode::Searching:
      snprintf(b, sizeof(b), "Searching... ch %u", (unsigned)v.sweepChannel);
      lv_label_set_text(s_line1, b);
      lv_label_set_text(s_line2, "Open the reactor's");
      lv_label_set_text(s_line3, "pairing window");
      break;
    case Mode::Paired:
      if (v.tempValid) snprintf(b, sizeof(b), "T %.1f / %.1f C  H %.0f%%", v.tempC, v.setpointC, v.heaterPct);
      else             snprintf(b, sizeof(b), "T --- / %.1f C  H %.0f%%", v.setpointC, v.heaterPct);
      lv_label_set_text(s_line1, b);
      snprintf(b, sizeof(b), "rpm %.1f   %s%s", v.rpm,
               v.runActive ? "RUN" : "idle",
               v.fullHold ? " HOLD" : (v.motorPaused ? " PAUSE" : ""));
      lv_label_set_text(s_line2, b);
      snprintf(b, sizeof(b), "%s  elapsed %lus%s",
               v.linkAlive ? "linked" : "DISCONNECTED",
               (unsigned long)v.elapsedSec, v.safetyTripped ? "  !SAFETY" : "");
      lv_label_set_text(s_line3, b);
      break;
  }
}

bool pairPressed() {
  if (!s_pairLatch) return false;
  s_pairLatch = false;
  return true;
}
}  // namespace EspNowScreen
```

- [ ] **Step 6: Integrate into `src/features/hub/HubRuntime.cpp`**

(a) Add includes near the top:

```cpp
#include "features/hub/link/HubLink.hpp"
#include "features/hub/ui/EspNowScreen.hpp"
```

(b) Add a namespace-scope static (near the other `g_*` statics):

```cpp
static HubLink g_link;
```

(c) In `begin()`, replace the `BringupScreen::create();` call with the link screen when ESP-NOW is on, and start the link. Where Step 2 of the bring-up created `BringupScreen`:

```cpp
    if (g_display.begin()) {
      if (AppConfig::HubFeatures::kEnableEspNow) EspNowScreen::create();
      else                                       BringupScreen::create();
      Serial.println("[HUB] display: enabled");
    } else { ... }   // unchanged
```

Then at the end of `begin()` (after the sleep section), add:

```cpp
  g_link.begin();
```

(d) In `tick()`, pump the link and the screen. After the display pump (`g_display.tick();`), add:

```cpp
  if (AppConfig::HubFeatures::kEnableEspNow) {
    g_link.tick();
    if (EspNowScreen::pairPressed()) g_link.startPairing();
  }
```

Then replace the bring-up screen refresh block (the `if (AppConfig::HubFeatures::kEnableDisplay) { ... BringupScreen::update(snap); }` at ~250 ms) with a guard that updates whichever screen is active:

```cpp
  if (AppConfig::HubFeatures::kEnableDisplay) {
    static uint32_t lastUi = 0;
    if (now - lastUi >= 250) {
      lastUi = now;
      if (AppConfig::HubFeatures::kEnableEspNow) {
        const sync::Telemetry& t = g_link.latest();
        EspNowScreen::View v = {};
        v.mode = g_link.state() == HubLink::State::Paired   ? EspNowScreen::Mode::Paired
               : g_link.state() == HubLink::State::Searching ? EspNowScreen::Mode::Searching
                                                             : EspNowScreen::Mode::Unpaired;
        v.sweepChannel  = g_link.sweepChannel();
        v.linkAlive     = g_link.linkAlive();
        v.tempValid     = (t.tempC_c != sync::kNullI16);
        v.tempC         = sync::decFixed(t.tempC_c, sync::kScaleTempC);
        v.setpointC     = sync::decFixed(t.setpointC_c, sync::kScaleTempC);
        v.heaterPct     = t.heaterPct_h / sync::kScaleHeaterPct;
        v.rpm           = sync::decFixed((int16_t)t.rpm_c, sync::kScaleRpm);
        v.runActive     = (t.flags & sync::kFlagRunActive);
        v.motorPaused   = (t.flags & sync::kFlagMotorPaused);
        v.fullHold      = (t.flags & sync::kFlagFullHold);
        v.safetyTripped = (t.flags & sync::kFlagSafetyTripped);
        v.elapsedSec    = t.elapsedSec;
        EspNowScreen::update(v);
      } else {
        BringupScreen::Snapshot snap = {};
        /* ...existing bring-up snapshot population (unchanged)... */
        BringupScreen::update(snap);
      }
    }
  }
```

> Keep the existing bring-up snapshot population verbatim inside the `else` branch. The ESP-NOW build path replaces only the *default screen*, leaving the bring-up screen reachable when `kEnableEspNow=false`.

(e) **Sleep interaction:** ESP-NOW listening needs the radio on. In this phase, leave the Phase-1 sleep FSM as-is but ensure the link never sweeps while idle-armed: sweeping only runs in `State::Searching`, entered by an explicit Pair tap, so an idle Paired/Unpaired HUB still sleeps normally. No FSM change required for v1.

- [ ] **Step 7: Build the HUB (default + ESP-NOW off)**

```bash
~/.platformio/penv/bin/pio run -e hub
# flip AppConfig::HubFeatures::kEnableEspNow = false, rebuild (bring-up screen path), restore to true
~/.platformio/penv/bin/pio run -e hub
```
Expected: both PASS. With the flag off, boot logs `[HUB] espnow: disabled` and the bring-up screen shows.

- [ ] **Step 8: Confirm reactor + native still green**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
~/.platformio/penv/bin/pio test -e native
```
Expected: PASS.

- [ ] **Step 9: Commit**

```bash
git add include/features/hub/link/ src/features/hub/link/ include/features/hub/ui/EspNowScreen.hpp src/features/hub/ui/EspNowScreen.cpp include/app_config.hpp src/features/hub/HubRuntime.cpp
git commit -m "feat(hub): ESP-NOW client + minimal pair/search/telemetry screen

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 7: Docs, toggle-off builds & on-device validation

**Files:**
- Create: `docs/PROTOCOL_ESPNOW.md`
- Modify: `API.md`
- Modify: `Roadmap.md`
- Modify: `README.md`

- [ ] **Step 1: Write `docs/PROTOCOL_ESPNOW.md`**

Document, from `include/sync/SyncFrames.hpp`: the `Header`, the five `MsgType`s, the `Telemetry` field table with scales (`tempC ×100`, `heaterTempC ×10`, `rpm ×100`, `heaterPct ×2`, `kNullI16`/`-1` nulls), the `Command` opcodes + present-mask/flag bits, the `Ack` result/error codes, the `PairRequest`/`PairAck` fields, and the pairing + channel-sweep sequence. State the v1 simplifications: single peer, no encryption, `load`/`runId`/`name` reserved-but-blank in telemetry.

- [ ] **Step 2: Update `API.md`**

Add a "## ESP-NOW link" section: `POST /api/v1/espnow/pair` (opens a 60 s window; `{ok:true}`; 503 `feature_disabled` when off), `POST /api/v1/espnow/forget` (clears binding; 503 when off), `"features": { ..., "espnow": true }` in status, and a note that ESP-NOW telemetry is a compact binary projection of the status doc (link to `docs/PROTOCOL_ESPNOW.md`). Add both endpoints to the feature-toggle 503 table and the connection-audit table.

- [ ] **Step 3: Update `Roadmap.md`**

Check the HUB Phase 2 item and the ESP-NOW Platform-Services / Backlog items:

```markdown
- [x] Phase 2 — ESP-NOW peer link + reactor binding (both firmwares). Design: docs/superpowers/specs/2026-06-30-hub-espnow-link-design.md (telemetry+control, 1 reactor; WiFi-less HUB; shared `src/sync` transport)
```
and under Platform Services change `- [ ] ESP-NOW peer sync / binding` to `- [x]`, and the Backlog `- [ ] ESP-NOW multi-reactor sync + binding` stays unchecked (multi-reactor deferred) but annotate "single-reactor link shipped Phase 2".

- [ ] **Step 4: Update `README.md`**

Add a line noting the shared `src/sync/` ESP-NOW transport compiled into both products, and that the HUB is WiFi-less (ESP-NOW only).

- [ ] **Step 5: Full toggle-matrix build gate**

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1     # kEnableEspNow = true
~/.platformio/penv/bin/pio run -e hub                    # kEnableEspNow = true
# set both kEnableEspNow flags = false:
~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1
~/.platformio/penv/bin/pio run -e hub
# restore both flags = true
~/.platformio/penv/bin/pio test -e native
```
Expected: all PASS. Restore flags to `true` before committing.

- [ ] **Step 6: On-device validation checklist (record results in the commit body)**

1. Flash reactor + HUB. HUB boots to "Not paired".
2. Tap PAIR on HUB → "Searching... ch N". Press "Pair HUB" in reactor web Settings (or `curl -X POST http://<reactor-ip>/api/v1/espnow/pair`).
3. HUB transitions to Paired; telemetry updates ~4 Hz; temp/setpoint/rpm match the web UI.
4. Reboot HUB → reconnects on the saved channel without re-sweeping.
5. From the HUB, send Stop and Pause → reactor reacts (serial `[CMD] ...`), telemetry reflects it, HUB logs the Ack.
6. `curl -X POST .../api/v1/espnow/forget` → HUB shows DISCONNECTED then re-sweeps.
7. Build both products with `kEnableEspNow=false` → boot logs show `disabled`, pair endpoint returns 503.

- [ ] **Step 7: Commit**

```bash
git add docs/PROTOCOL_ESPNOW.md API.md Roadmap.md README.md
git commit -m "docs(espnow): protocol doc, API endpoints, roadmap + readme

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Self-Review (completed during planning)

- **Spec coverage:** shared transport (T1–T2), reactor responder (T4), HUB client (T6), pairing/binding + channel sweep + NVS (T2 binding, T5 reactor, T6 HUB), telemetry frame + 4 Hz cadence (T1, T4), command set incl. pause/resume (T1 opcodes, T3 apply, T4 decode, T6 send), Ack model (T1, T4, T6), toggle gating both products (T4, T6), `/espnow/pair`+`/espnow/forget` (T5), link-loss + re-sweep (T6), docs (T7), native tests (T1), toggle-off builds (T4/T6/T7). All spec sections map to a task.
- **Placeholders:** none — every code step has complete code; device/UI tasks state explicitly that verification is build + on-device (repo convention: only pure logic is host-tested).
- **Type consistency:** `sync::` enums/structs/scales and `decFixed/encFixed/encFixedN` names match across T1→T6; `WebInterface::cmd*` signatures defined in T3 are consumed verbatim in T4; `EspNowLink` API defined in T2 is used verbatim in T4/T5/T6; `HubLink`/`EspNowScreen` interfaces match their T6 callers.
- **Known v1 simplifications (documented in T7):** telemetry `load`, `runId`, and session `name` are reserved-but-blank (not in `ReactorTelemetry`); REST pause/resume gap (`action:"pause"`) is left open — the underlying apply path now exists (T3), so closing it later is ~10 lines.
