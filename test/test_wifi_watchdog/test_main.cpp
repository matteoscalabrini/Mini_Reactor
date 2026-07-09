#include <unity.h>
#include "net/WifiWatchdog.hpp"

void setUp() {}
void tearDown() {}

static WifiWatchdog make() {
  WifiWatchdog w;
  WifiWatchdog::Config c;
  c.stallMs = 30000;
  c.backoffMs = 30000;
  w.setConfig(c);
  return w;
}

// Healthy: oks advance every tick -> never fires, no matter how long.
void test_healthy_never_fires() {
  WifiWatchdog w = make();
  uint32_t tx = 0, ok = 0;
  for (uint32_t t = 0; t <= 120000; t += 250) {
    tx += 1; ok += 1;  // every send lands
    TEST_ASSERT_FALSE(w.update(t, true, tx, ok));
  }
}

// Idle: attempts never advance (nothing to send) -> never fires even though oks
// are also flat (must not mistake "not sending" for "sending but wedged").
void test_idle_never_fires() {
  WifiWatchdog w = make();
  for (uint32_t t = 0; t <= 120000; t += 250)
    TEST_ASSERT_FALSE(w.update(t, true, 100, 100));  // constant counters
}

// Wedge: attempts advance, oks frozen. No fire before stallMs; fires once after,
// then holds off for backoffMs before it could fire again.
void test_wedge_fires_once_then_backoff() {
  WifiWatchdog w = make();
  uint32_t tx = 0, ok = 0;
  // 0..1000ms healthy (oks advancing) to anchor the clock
  for (uint32_t t = 0; t <= 1000; t += 250) { tx++; ok++; TEST_ASSERT_FALSE(w.update(t, true, tx, ok)); }
  // wedge onset at t=1000: attempts climb, ok frozen
  bool fired = false;
  uint32_t fireT = 0;
  for (uint32_t t = 1250; t <= 40000; t += 250) {
    tx++;  // still trying
    const bool r = w.update(t, true, tx, ok);  // ok stays frozen
    if (r) { fired = true; fireT = t; break; }
  }
  TEST_ASSERT_TRUE(fired);
  // last healthy tick was t=1000 -> fires at ~1000+stallMs = 31000
  TEST_ASSERT_UINT32_WITHIN(300, 31000, fireT);

  // Immediately after firing it must not re-fire within backoffMs even if still wedged.
  bool refired = false;
  for (uint32_t t = fireT + 250; t <= fireT + 25000; t += 250) {
    tx++;
    if (w.update(t, true, tx, ok)) { refired = true; break; }
  }
  TEST_ASSERT_FALSE(refired);
}

// Disconnect clears the stall: a wedge that resolves via reconnect doesn't fire.
void test_disconnect_resets() {
  WifiWatchdog w = make();
  uint32_t tx = 0, ok = 0;
  for (uint32_t t = 0; t <= 1000; t += 250) { tx++; ok++; w.update(t, true, tx, ok); }
  // wedge for 20s (< stall), then drop connection
  for (uint32_t t = 1250; t <= 21000; t += 250) { tx++; TEST_ASSERT_FALSE(w.update(t, true, tx, ok)); }
  w.update(21250, false, tx, ok);         // disconnected
  // reconnect, healthy again
  bool fired = false;
  for (uint32_t t = 21500; t <= 40000; t += 250) { tx++; ok++; if (w.update(t, true, tx, ok)) fired = true; }
  TEST_ASSERT_FALSE(fired);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_healthy_never_fires);
  RUN_TEST(test_idle_never_fires);
  RUN_TEST(test_wedge_fires_once_then_backoff);
  RUN_TEST(test_disconnect_resets);
  return UNITY_END();
}
