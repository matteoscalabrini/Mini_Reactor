#pragma once
#include <lvgl.h>
#include "features/hub/ui/HubViewModel.hpp"

// HOME carousel page: top strip (link dot · wordmark · battery), run name +
// elapsed, state-coloured hero temperature, and the Start/Pause/Stop controls.
// Renders IDLE (no run) and SAFETY (limit tripped) presentations. Buttons expose
// consume-latches; the coordinator raises confirm overlays and routes to the link.
namespace HomeScreen {
void create(lv_obj_t* parent);            // builds the home page inside `parent`
void update(const hubui::Model& m);
bool consumeStop();                        // STOP tapped
bool consumeStart();                       // START tapped (idle state)
bool consumePause();                       // PAUSE/RESUME tapped
}  // namespace HomeScreen
