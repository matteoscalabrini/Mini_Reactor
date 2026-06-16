#include <unity.h>
#include <cmath>
#include "control/RelayAutotune.hpp"

void setUp() {}
void tearDown() {}

// Relay drives high (heat) below setpoint-hyst, low above setpoint+hyst.
void test_relay_switching() {
  RelayAutotune at;
  RelayAutotune::Config c;  // relayHigh=1, relayLow=0, hyst=0.3 defaults
  at.begin(30.0f, 0, c);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, at.update(28.0f, 100));   // below → high
  TEST_ASSERT_EQUAL_FLOAT(0.0f, at.update(32.0f, 200));   // above → low
}

// Feed a clean triangle (setpoint 30, amplitude 2, period 8 s) for ~6 cycles;
// expect completion and Tyreus-Luyben gains from Ku=4d/(pi*a), Tu=period.
void test_converges_and_computes_gains() {
  RelayAutotune at;
  RelayAutotune::Config c;
  at.begin(30.0f, 0, c);
  const float amp = 2.0f, P = 8000.0f;   // ms
  for (uint32_t t = 0; t <= 60000; t += 250) {
    const float ph = (float)(t % (uint32_t)P) / P;          // 0..1
    const float tri = ph < 0.5f ? (ph * 2.0f) : (2.0f - ph * 2.0f);  // 0..1..0
    const float pv = 30.0f - amp + 2.0f * amp * tri;        // 28..32..28
    at.update(pv, t);
  }
  TEST_ASSERT_TRUE(at.done());
  TEST_ASSERT_FALSE(at.failed());
  TEST_ASSERT_EQUAL_INT(100, at.progressPct());
  float kp, ki, kd;
  TEST_ASSERT_TRUE(at.computeGains(kp, ki, kd));
  // Ku = 4*0.5/(pi*2) = 1/pi = 0.318; kp = 0.45*Ku = 0.143
  TEST_ASSERT_FLOAT_WITHIN(0.025f, 0.143f, kp);
  // ki = kp/(2.2*Tu), Tu≈8 → ~0.0081 ; kd = kp*Tu/6.3 → ~0.182
  TEST_ASSERT_FLOAT_WITHIN(0.003f, 0.0081f, ki);
  TEST_ASSERT_FLOAT_WITHIN(0.04f, 0.182f, kd);
}

void test_timeout_fails() {
  RelayAutotune at;
  RelayAutotune::Config c;
  c.timeoutMs = 1000;
  at.begin(30.0f, 0, c);
  at.update(25.0f, 500);     // constant below setpoint — no oscillation
  at.update(25.0f, 2000);    // past timeout
  TEST_ASSERT_TRUE(at.failed());
  float kp, ki, kd;
  TEST_ASSERT_FALSE(at.computeGains(kp, ki, kd));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_relay_switching);
  RUN_TEST(test_converges_and_computes_gains);
  RUN_TEST(test_timeout_fails);
  return UNITY_END();
}
