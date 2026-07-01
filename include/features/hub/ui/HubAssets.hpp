#pragma once
#include <lvgl.h>
#include "features/hub/ui/HubViewModel.hpp"

// Generated brand assets (see src/features/hub/ui/fonts,img) + brand colour helpers.
LV_FONT_DECLARE(hub_doto_88);
LV_FONT_DECLARE(hub_doto_16);
LV_IMG_DECLARE(wordmark_white);

namespace hubui {
inline lv_color_t toLv(Rgb c)  { return lv_color_make(c.r, c.g, c.b); }
inline lv_color_t lv_cyan()    { return lv_color_make(0x00, 0xb4, 0xd8); }
inline lv_color_t lv_amber()   { return lv_color_make(0xff, 0xb0, 0x20); }
inline lv_color_t lv_red()     { return lv_color_make(0xe2, 0x26, 0x26); }
inline lv_color_t lv_redHero() { return lv_color_make(0xff, 0x4d, 0x4f); }
inline lv_color_t lv_slate()   { return lv_color_make(0x6e, 0x85, 0x95); }
inline lv_color_t lv_green()   { return lv_color_make(0x37, 0xd6, 0x7a); }
inline lv_color_t lv_muted()   { return lv_color_make(0x94, 0xa4, 0xad); }
}  // namespace hubui
