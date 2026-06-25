#include "features/ui/InputManager.hpp"
#include <Arduino.h>

namespace ui {

namespace {
volatile int32_t g_encCount = 0;       // +1 / -1 per detent (clean, bounce-rejected)
volatile int g_encAPin = -1, g_encBPin = -1;
volatile uint8_t g_encState = 0;

// Ben Buxton full-step quadrature state table: only valid Gray-code transitions
// advance the machine, so contact bounce can't produce spurious or wrong-direction
// counts. A detent emits exactly one DIR_CW/DIR_CCW. Index by (A<<1)|B.
constexpr uint8_t R_START = 0x0, R_CW_FINAL = 0x1, R_CW_BEGIN = 0x2, R_CW_NEXT = 0x3,
                  R_CCW_BEGIN = 0x4, R_CCW_FINAL = 0x5, R_CCW_NEXT = 0x6;
constexpr uint8_t DIR_CW = 0x10, DIR_CCW = 0x20;
const uint8_t kEncTable[7][4] = {
    {R_START, R_CW_BEGIN, R_CCW_BEGIN, R_START},               // R_START
    {R_CW_NEXT, R_START, R_CW_FINAL, R_START | DIR_CW},        // R_CW_FINAL
    {R_CW_NEXT, R_CW_BEGIN, R_START, R_START},                 // R_CW_BEGIN
    {R_CW_NEXT, R_CW_BEGIN, R_CW_FINAL, R_START},              // R_CW_NEXT
    {R_CCW_NEXT, R_START, R_CCW_BEGIN, R_START},               // R_CCW_BEGIN
    {R_CCW_NEXT, R_CCW_FINAL, R_START, R_START | DIR_CCW},     // R_CCW_FINAL
    {R_CCW_NEXT, R_CCW_FINAL, R_CCW_BEGIN, R_START},           // R_CCW_NEXT
};

void IRAM_ATTR onEncEdge() {
  uint8_t pinstate = (digitalRead(g_encAPin) << 1) | digitalRead(g_encBPin);
  g_encState = kEncTable[g_encState & 0x07][pinstate];
  if (g_encState & DIR_CW) g_encCount++;
  else if (g_encState & DIR_CCW) g_encCount--;
}
}  // namespace

InputManager::InputManager(const Pins& pins, const Timing& timing)
    : pins_(pins), timing_(timing) {}

int32_t InputManager::debugEncoderCount() const { return g_encCount; }  // TEMP

void InputManager::begin() {
  pinMode(pins_.encA, INPUT_PULLUP);
  pinMode(pins_.encB, INPUT_PULLUP);
  pinMode(pins_.encSw, INPUT_PULLUP);
  pinMode(pins_.btn1, INPUT_PULLUP);
  pinMode(pins_.btn2, INPUT_PULLUP);
  pinMode(pins_.btn3, INPUT_PULLUP);
  g_encAPin = pins_.encA;
  g_encBPin = pins_.encB;
  g_encState = (digitalRead(pins_.encA) << 1) | digitalRead(pins_.encB);  // seed from rest state
  attachInterrupt(digitalPinToInterrupt(pins_.encA), onEncEdge, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pins_.encB), onEncEdge, CHANGE);  // decode both edges
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
  // 1) Encoder rotation: the state machine already emits one count per detent.
  int32_t cur = g_encCount;
  if (cur != lastEncCount_) {
    if (cur > lastEncCount_) { lastEncCount_++; return UiEvent::RotateCW; }
    lastEncCount_--; return UiEvent::RotateCCW;
  }

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
