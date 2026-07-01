#pragma once
#include <cstdint>

// Boot touch-calibration wizard screen: a moving orange dot the user taps 4 times.
// Uses its own lv screen object so it swaps cleanly with EspNowScreen/BringupScreen.
namespace CalibrationScreen {
void create();  // build + load a dedicated calibration screen
void showStep(uint8_t stepIndex, uint8_t stepCount, int16_t targetX, int16_t targetY);
void close();   // restore the previous screen + delete the calibration screen
}  // namespace CalibrationScreen
