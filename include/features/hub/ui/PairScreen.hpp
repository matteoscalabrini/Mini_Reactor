#pragma once
#include <lvgl.h>

// Pairing flow (replaces the old EspNowScreen unpaired/searching states):
// unpaired -> "NOT PAIRED" + cyan PAIR button; searching -> radar pulse + live CH.
namespace PairScreen {
void create(lv_obj_t* parent);
void update(bool searching, uint8_t sweepChannel);
bool consumePair();   // PAIR tapped (consumes the latch)
}  // namespace PairScreen
