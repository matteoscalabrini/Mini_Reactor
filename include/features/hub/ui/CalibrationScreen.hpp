#pragma once
#include <cstdint>

// Boot touch-calibration wizard screen: MINI REACTOR wordmark centred with four
// cross-layout targets the user taps in turn (active/done/pending states).
// Uses its own lv screen object so it swaps cleanly with the HubUi/BringupScreen.
namespace CalibrationScreen {
void create();  // build + load a dedicated calibration screen
void showStep(uint8_t stepIndex, uint8_t stepCount, int16_t targetX, int16_t targetY);
void close();   // restore the previous screen + delete the calibration screen
}  // namespace CalibrationScreen
