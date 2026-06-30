#pragma once
#include <cstdint>

// Wire protocol for the reactor <-> HUB ESP-NOW link. Pure, header-only:
// no Arduino/ESP/app_config deps so the native host test links with no extra
// source. All frames are packed little-endian and fit one 250-byte ESP-NOW packet.

namespace synclink {

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

struct Telemetry {            // ~74 B
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

struct PairRequest {          // ~44 B
  Header  hdr;
  uint8_t role;               // DeviceRole (Hub)
  uint8_t mac[kMacLen];
  char    name[kNameLen];
};

struct PairAck {              // ~45 B
  Header  hdr;
  uint8_t role;               // DeviceRole (Reactor)
  uint8_t mac[kMacLen];
  uint8_t channel;            // reactor's current WiFi channel
  char    name[kNameLen];
};
#pragma pack(pop)

}  // namespace synclink
