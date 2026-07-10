#include <unity.h>
#include "net/WifiWatchdog.hpp"

void setUp() {}
void tearDown() {}

// Cadence contract (the 2026-07-10 regression): update() runs at LOOP rate
// (~3-5 ms) while ESP-NOW sends happen every 250 ms (+3 attempts each — the 3x
// resend). The original detector reset its stall clock on every non-sending
// tick, so at this cadence it could NEVER fire. Every test here drives the
// detector the way AppRuntime::tick() actually does.
static constexpr uint32_t kPollMs = 5;
static constexpr uint32_t kSendMs = 250;

static WifiWatchdog make() {
  WifiWatchdog w;
  WifiWatchdog::Config c;
  c.stallMs = 30000;
  c.backoffMs = 30000;
  w.setConfig(c);
  return w;
}

// Drives [from..to) at poll cadence; sends fire on the kSendMs grid, landing
// (ok) or not. Returns the time of the first fire, or 0 if none.
static uint32_t drive(WifiWatchdog& w, uint32_t& tx, uint32_t& ok, uint32_t from,
                      uint32_t to, bool connected, bool landing) {
  for (uint32_t t = from; t < to; t += kPollMs) {
    if (t % kSendMs == 0) { tx += 3; if (landing) ok += 3; }
    if (w.update(t, connected, tx, ok)) return t;
  }
  return 0;
}

// Healthy: every send lands -> never fires, no matter how long.
void test_healthy_never_fires() {
  WifiWatchdog w = make();
  uint32_t tx = 0, ok = 0;
  TEST_ASSERT_EQUAL_UINT32(0, drive(w, tx, ok, 0, 300000, true, true));
}

// Idle: attempts never advance (nothing to send) -> never fires even though oks
// are also flat (must not mistake "not sending" for "sending but wedged").
void test_idle_never_fires() {
  WifiWatchdog w = make();
  for (uint32_t t = 0; t <= 120000; t += kPollMs)
    TEST_ASSERT_FALSE(w.update(t, true, 100, 100));  // constant counters
}

// THE regression: wedge at real cadence (poll rate >> send rate) must fire
// ~stallMs after the first unanswered send. The original detector never fired
// here because the ~49 idle polls between sends each reset the stall clock.
void test_wedge_fires_at_loop_cadence() {
  WifiWatchdog w = make();
  uint32_t tx = 0, ok = 0;
  TEST_ASSERT_EQUAL_UINT32(0, drive(w, tx, ok, 0, 10000, true, true));  // healthy 10s
  const uint32_t fireT = drive(w, tx, ok, 10000, 120000, true, false);  // wedge
  TEST_ASSERT_NOT_EQUAL(0, fireT);
  // first unanswered send at t=10000 -> fires at ~10000+stallMs
  TEST_ASSERT_UINT32_WITHIN(kSendMs + kPollMs, 40000, fireT);
}

// After firing it must hold off for backoffMs, then fire again if still wedged.
void test_backoff_then_refire() {
  WifiWatchdog w = make();
  uint32_t tx = 0, ok = 0;
  drive(w, tx, ok, 0, 10000, true, true);
  const uint32_t fire1 = drive(w, tx, ok, 10000, 120000, true, false);
  TEST_ASSERT_NOT_EQUAL(0, fire1);
  const uint32_t fire2 = drive(w, tx, ok, fire1 + kPollMs, fire1 + 120000, true, false);
  TEST_ASSERT_NOT_EQUAL(0, fire2);
  // still wedged: re-arms on the next unanswered send, stalls stallMs, gated by
  // backoffMs — with stall == backoff the refire lands ~stallMs after fire1.
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(30000, fire2 - fire1);
  TEST_ASSERT_UINT32_WITHIN(2 * kSendMs, fire1 + 30000, fire2);
}

// Disconnect clears the stall: a wedge that resolves via reconnect doesn't fire.
void test_disconnect_resets() {
  WifiWatchdog w = make();
  uint32_t tx = 0, ok = 0;
  drive(w, tx, ok, 0, 10000, true, true);
  TEST_ASSERT_EQUAL_UINT32(0, drive(w, tx, ok, 10000, 30000, true, false));  // 20s wedge < stall
  w.update(30000, false, tx, ok);  // disconnected
  // reconnect, healthy again — the partial stall must not carry over
  TEST_ASSERT_EQUAL_UINT32(0, drive(w, tx, ok, 30005, 120000, true, true));
}

// Recovery mid-wedge: oks resume before stallMs -> clock resets, no fire.
void test_recovered_wedge_never_fires() {
  WifiWatchdog w = make();
  uint32_t tx = 0, ok = 0;
  drive(w, tx, ok, 0, 10000, true, true);
  TEST_ASSERT_EQUAL_UINT32(0, drive(w, tx, ok, 10000, 35000, true, false));  // 25s wedge
  TEST_ASSERT_EQUAL_UINT32(0, drive(w, tx, ok, 35000, 120000, true, true));  // drains
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_healthy_never_fires);
  RUN_TEST(test_idle_never_fires);
  RUN_TEST(test_wedge_fires_at_loop_cadence);
  RUN_TEST(test_backoff_then_refire);
  RUN_TEST(test_disconnect_resets);
  RUN_TEST(test_recovered_wedge_never_fires);
  return UNITY_END();
}
