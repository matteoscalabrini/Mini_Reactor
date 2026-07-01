#pragma once
#include <lvgl.h>
#include "features/hub/ui/HubViewModel.hpp"

// DETAILS carousel page: the deeper telemetry as a key -> value list.
namespace DetailsScreen {
void create(lv_obj_t* parent);
void update(const hubui::Model& m);
}  // namespace DetailsScreen
