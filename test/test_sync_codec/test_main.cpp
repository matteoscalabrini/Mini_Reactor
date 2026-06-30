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
