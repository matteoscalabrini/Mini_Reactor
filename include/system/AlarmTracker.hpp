/*
 * AlarmTracker.hpp — frame-based active-alarm set with stable `since` timestamps.
 *
 * Header-only, no Arduino deps (host-testable; time is injected via beginFrame).
 * Usage per telemetry tick:
 *   tracker.beginFrame(uptimeSec);
 *   if (cond) tracker.add("code", "severity");   // for each active alarm
 *   tracker.endFrame();                           // drops alarms not added this frame
 * Codes/severities must be string literals (static lifetime). `sinceSec` is set
 * when an alarm first appears and preserved while it stays active.
 */
#pragma once

#include <cstdint>
#include <cstring>

class AlarmTracker {
 public:
  static constexpr int kMax = 12;
  struct Alarm { const char* code; const char* severity; uint32_t sinceSec; };

  void beginFrame(uint32_t nowSec) {
    nowSec_ = nowSec;
    for (int i = 0; i < count_; ++i) seen_[i] = false;
  }

  void add(const char* code, const char* severity) {
    for (int i = 0; i < count_; ++i) {
      if (std::strcmp(slots_[i].code, code) == 0) {
        slots_[i].severity = severity;  // severity may change; since does not
        seen_[i] = true;
        return;
      }
    }
    if (count_ < kMax) {
      slots_[count_] = Alarm{code, severity, nowSec_};
      seen_[count_] = true;
      ++count_;
    }
  }

  void endFrame() {
    int w = 0;
    for (int i = 0; i < count_; ++i) {
      if (seen_[i]) { slots_[w] = slots_[i]; seen_[w] = true; ++w; }
    }
    count_ = w;
  }

  int size() const { return count_; }
  const Alarm& operator[](int i) const { return slots_[i]; }

 private:
  Alarm slots_[kMax] = {};
  bool seen_[kMax] = {false};
  int count_ = 0;
  uint32_t nowSec_ = 0;
};
