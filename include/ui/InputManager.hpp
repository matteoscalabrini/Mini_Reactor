#pragma once
#include <stdint.h>
#include "ui/UiTypes.hpp"

namespace ui {

class InputManager {
 public:
  struct Pins { int encA; int encB; int encSw; int btn1; int btn2; int btn3; };
  struct Timing { uint16_t debounceMs = 25; uint16_t longPressMs = 600; };

  InputManager(const Pins& pins, const Timing& timing);
  void begin();
  UiEvent poll();   // returns one pending event per call, or UiEvent::None

 private:
  UiEvent pollButton(int pin, UiEvent ev, bool& downState, uint32_t& edgeMs);
  Pins pins_;
  Timing timing_;
  // encoder
  int32_t lastEncCount_ = 0;
  // buttons
  bool b1Down_ = false, b2Down_ = false, b3Down_ = false;
  uint32_t b1Ms_ = 0, b2Ms_ = 0, b3Ms_ = 0;
  // encoder switch (short/long press)
  bool swDown_ = false; uint32_t swDownMs_ = 0; bool swLongFired_ = false;
};

}  // namespace ui
