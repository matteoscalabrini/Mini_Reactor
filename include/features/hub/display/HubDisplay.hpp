#pragma once
#include <Arduino.h>

class HubDisplay {
 public:
  bool begin();
  void tick();
  bool enterSleep();
  bool exitSleep();
  bool isReady() const { return ready_; }

 private:
  bool ready_ = false;
};
