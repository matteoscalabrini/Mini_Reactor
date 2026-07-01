#pragma once
#include <lvgl.h>
#include "features/hub/ui/HubViewModel.hpp"

// Coordinator: owns the HOME<->DETAILS swipe carousel, page dots, the pairing
// screen, and the Stop/Start confirm overlays. Screens stay link-free; HubRuntime
// polls these consume-latches and routes them to HubLink.
namespace HubUi {
enum class Mode : uint8_t { Pairing, Normal };
void begin();
void update(const hubui::Model& m, Mode mode, bool searching, uint8_t sweepChannel);
bool consumeStop();     // confirmed stop (after overlay YES)
bool consumeStart();    // confirmed start (after overlay YES)
bool consumePause();
bool consumePair();     // pairing: PAIR tapped
}  // namespace HubUi
