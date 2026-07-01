#include "features/hub/ui/CalibrationScreen.hpp"
#include <lvgl.h>
#include <cstdio>
#include <algorithm>
#include "app_config.hpp"

// Same widget set as EspNowScreen (lv_label + lv_obj rectangles/circle). Dark
// #101010 background to match the rest of the hub UI. Ported visually from the
// barebone createCalibrationPage.
namespace CalibrationScreen {
namespace {
lv_obj_t* s_screen = nullptr;
lv_obj_t* s_prev   = nullptr;  // screen to restore on close()
lv_obj_t* s_step   = nullptr;
lv_obj_t* s_dot    = nullptr;
}  // namespace

void create() {
  s_prev = lv_scr_act();
  s_screen = lv_obj_create(nullptr);
  lv_obj_remove_style_all(s_screen);
  lv_obj_set_size(s_screen, AppConfig::HubDisplay::kWidth, AppConfig::HubDisplay::kHeight);
  lv_obj_set_style_bg_color(s_screen, LV_COLOR_MAKE(0x10, 0x10, 0x10), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title = lv_label_create(s_screen);
  lv_obj_set_width(title, 260);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
  lv_label_set_text(title, "Touch the dot");
  lv_obj_align(title, LV_ALIGN_CENTER, 0, -110);

  s_step = lv_label_create(s_screen);
  lv_obj_set_width(s_step, 260);
  lv_obj_set_style_text_align(s_step, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_color(s_step, LV_COLOR_MAKE(0x8c, 0x8c, 0x8c), LV_PART_MAIN);
  lv_label_set_text(s_step, "Step 1 / 4");
  lv_obj_align(s_step, LV_ALIGN_CENTER, 0, 112);

  s_dot = lv_obj_create(s_screen);
  lv_obj_remove_style_all(s_dot);
  lv_obj_set_size(s_dot, AppConfig::HubTouchCalibration::kTargetDiameterPx,
                  AppConfig::HubTouchCalibration::kTargetDiameterPx);
  lv_obj_set_style_radius(s_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_dot, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(s_dot, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_dot, LV_COLOR_MAKE(0xff, 0xc8, 0x6e), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_dot, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(s_dot, 22, LV_PART_MAIN);
  lv_obj_set_style_shadow_color(s_dot, LV_COLOR_MAKE(0xff, 0xc8, 0x6e), LV_PART_MAIN);
  lv_obj_clear_flag(s_dot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

  lv_scr_load(s_screen);
}

void showStep(uint8_t stepIndex, uint8_t stepCount, int16_t targetX, int16_t targetY) {
  if (!s_screen) return;
  char b[24];
  const uint8_t total = std::max<uint8_t>(stepCount, 1);
  const uint8_t cur = std::min<uint8_t>(static_cast<uint8_t>(stepIndex + 1), total);
  snprintf(b, sizeof(b), "Step %u / %u", (unsigned)cur, (unsigned)total);
  lv_label_set_text(s_step, b);
  const int16_t half = AppConfig::HubTouchCalibration::kTargetDiameterPx / 2;
  lv_obj_set_pos(s_dot, targetX - half, targetY - half);
}

void close() {
  if (s_prev) lv_scr_load(s_prev);
  if (s_screen) { lv_obj_del(s_screen); s_screen = nullptr; }  // deletes children too
  s_prev = nullptr; s_step = nullptr; s_dot = nullptr;
}
}  // namespace CalibrationScreen
