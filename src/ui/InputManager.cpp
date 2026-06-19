#include "ui/InputManager.hpp"
#include <Arduino.h>

namespace ui {

namespace {
volatile int32_t g_encCount = 0;
int g_encAPin = -1, g_encBPin = -1;

void IRAM_ATTR onEncEdge() {
  // Quadrature: on A edge, B level gives direction.
  int b = digitalRead(g_encBPin);
  int a = digitalRead(g_encAPin);
  g_encCount += (a == b) ? 1 : -1;
}
}  // namespace

InputManager::InputManager(const Pins& pins, const Timing& timing)
    : pins_(pins), timing_(timing) {}

void InputManager::begin() {
  pinMode(pins_.encA, INPUT_PULLUP);
  pinMode(pins_.encB, INPUT_PULLUP);
  pinMode(pins_.encSw, INPUT_PULLUP);
  pinMode(pins_.btn1, INPUT_PULLUP);
  pinMode(pins_.btn2, INPUT_PULLUP);
  pinMode(pins_.btn3, INPUT_PULLUP);
  g_encAPin = pins_.encA;
  g_encBPin = pins_.encB;
  attachInterrupt(digitalPinToInterrupt(pins_.encA), onEncEdge, CHANGE);
  lastEncCount_ = g_encCount;
}

UiEvent InputManager::pollButton(int pin, UiEvent ev, bool& downState, uint32_t& edgeMs) {
  bool pressed = (digitalRead(pin) == LOW);   // active-low w/ pullup
  uint32_t now = millis();
  if (pressed && !downState && (now - edgeMs) > timing_.debounceMs) {
    downState = true; edgeMs = now; return ev;
  }
  if (!pressed && downState && (now - edgeMs) > timing_.debounceMs) {
    downState = false; edgeMs = now;
  }
  return UiEvent::None;
}

UiEvent InputManager::poll() {
  // 1) Encoder rotation (4 counts per detent typical -> coalesce in steps of 4).
  int32_t cur = g_encCount;
  if (cur - lastEncCount_ >= 4) { lastEncCount_ += 4; return UiEvent::RotateCW; }
  if (cur - lastEncCount_ <= -4) { lastEncCount_ -= 4; return UiEvent::RotateCCW; }

  // 2) Encoder switch: short vs long press.
  uint32_t now = millis();
  bool sw = (digitalRead(pins_.encSw) == LOW);
  if (sw && !swDown_) { swDown_ = true; swDownMs_ = now; swLongFired_ = false; }
  if (sw && swDown_ && !swLongFired_ && (now - swDownMs_) >= timing_.longPressMs) {
    swLongFired_ = true; return UiEvent::EncLongPush;
  }
  if (!sw && swDown_) {
    swDown_ = false;
    if (!swLongFired_ && (now - swDownMs_) > timing_.debounceMs) return UiEvent::EncShortPush;
  }

  // 3) Buttons.
  UiEvent e;
  if ((e = pollButton(pins_.btn1, UiEvent::Btn1, b1Down_, b1Ms_)) != UiEvent::None) return e;
  if ((e = pollButton(pins_.btn2, UiEvent::Btn2, b2Down_, b2Ms_)) != UiEvent::None) return e;
  if ((e = pollButton(pins_.btn3, UiEvent::Btn3, b3Down_, b3Ms_)) != UiEvent::None) return e;

  return UiEvent::None;
}

}  // namespace ui
