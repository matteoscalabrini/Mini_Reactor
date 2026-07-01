#include "features/hub/ui/CalibrationScreen.hpp"
#include "features/hub/ui/HubAssets.hpp"
#include "features/hub/touch/TouchCalibrationSolver.hpp"
#include <lvgl.h>
#include <cstdio>
#include <algorithm>
#include "app_config.hpp"

// Cross-layout touch calibration: MINI REACTOR wordmark centred, four round
// targets on the cardinal points (the solver's real tap points via targetForStep).
// active target glows cyan with a pulsing ring, done -> green check, pending -> dim.
namespace CalibrationScreen {
namespace {
constexpr int kMarkSize = 46;

lv_obj_t* s_screen = nullptr;
lv_obj_t* s_prev   = nullptr;  // screen to restore on close()
lv_obj_t* s_step   = nullptr;
lv_obj_t* s_ring   = nullptr;  // shared pulsing ring, moved over the active target
lv_obj_t* s_marks[AppConfig::HubTouchCalibration::kStepCount]   = {};
lv_obj_t* s_markLbl[AppConfig::HubTouchCalibration::kStepCount] = {};
int s_activeCx = AppConfig::HubDisplay::kWidth / 2;
int s_activeCy = AppConfig::HubDisplay::kHeight / 2;

void ringExec(void*, int32_t v) {
  lv_obj_set_size(s_ring, v, v);
  lv_obj_set_pos(s_ring, s_activeCx - v / 2, s_activeCy - v / 2);
  int op = 255 - ((v - kMarkSize) * 255) / 40;   // 46px -> 255, 86px -> 0
  if (op < 0) op = 0;
  lv_obj_set_style_opa(s_ring, (lv_opa_t)op, LV_PART_MAIN);
}
}  // namespace

void create() {
  s_prev = lv_scr_act();
  s_screen = lv_obj_create(nullptr);
  lv_obj_remove_style_all(s_screen);
  lv_obj_set_size(s_screen, AppConfig::HubDisplay::kWidth, AppConfig::HubDisplay::kHeight);
  lv_obj_set_style_bg_color(s_screen, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* wm = lv_img_create(s_screen); lv_img_set_src(wm, &wordmark_white);
  lv_obj_align(wm, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t* title = lv_label_create(s_screen);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_set_style_text_color(title, hubui::lv_muted(), LV_PART_MAIN);
  lv_label_set_text(title, "Touch the dots");
  lv_obj_align(title, LV_ALIGN_CENTER, 0, -40);

  s_step = lv_label_create(s_screen);
  lv_obj_set_style_text_font(s_step, &hub_doto_16, LV_PART_MAIN);
  lv_obj_set_style_text_color(s_step, hubui::lv_slate(), LV_PART_MAIN);
  lv_label_set_text(s_step, "STEP 1 / 4");
  lv_obj_align(s_step, LV_ALIGN_CENTER, 0, 40);

  // Four target markers on the cardinal cross — the solver's real tap points.
  for (uint8_t i = 0; i < AppConfig::HubTouchCalibration::kStepCount; i++) {
    const TouchCalibrationSolver::Point t = TouchCalibrationSolver::targetForStep(
        i, AppConfig::Hub::kTouchWidth, AppConfig::Hub::kTouchHeight,
        AppConfig::HubTouchCalibration::kTargetOrbitRadiusPx);
    lv_obj_t* mk = lv_obj_create(s_screen); lv_obj_remove_style_all(mk);
    lv_obj_set_size(mk, kMarkSize, kMarkSize);
    lv_obj_set_style_radius(mk, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_pos(mk, t.x - kMarkSize / 2, t.y - kMarkSize / 2);
    lv_obj_clear_flag(mk, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* l = lv_label_create(mk);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(l);
    s_marks[i] = mk; s_markLbl[i] = l;
  }

  s_ring = lv_obj_create(s_screen); lv_obj_remove_style_all(s_ring);
  lv_obj_set_style_radius(s_ring, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_ring, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(s_ring, hubui::lv_cyan(), LV_PART_MAIN);
  lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, s_ring); lv_anim_set_exec_cb(&a, ringExec);
  lv_anim_set_values(&a, kMarkSize, kMarkSize + 40); lv_anim_set_time(&a, 1200);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE); lv_anim_start(&a);

  lv_scr_load(s_screen);
}

void showStep(uint8_t stepIndex, uint8_t stepCount, int16_t targetX, int16_t targetY) {
  if (!s_screen) return;
  char b[24];
  const uint8_t total = std::max<uint8_t>(stepCount, 1);
  const uint8_t cur = std::min<uint8_t>((uint8_t)(stepIndex + 1), total);
  snprintf(b, sizeof(b), "STEP %u / %u", (unsigned)cur, (unsigned)total);
  lv_label_set_text(s_step, b);

  for (uint8_t i = 0; i < AppConfig::HubTouchCalibration::kStepCount; i++) {
    lv_obj_t* mk = s_marks[i]; lv_obj_t* l = s_markLbl[i];
    if (!mk) continue;
    if (i < stepIndex) {                 // done
      lv_obj_set_style_bg_opa(mk, LV_OPA_TRANSP, LV_PART_MAIN);
      lv_obj_set_style_border_width(mk, 2, LV_PART_MAIN);
      lv_obj_set_style_border_color(mk, hubui::lv_green(), LV_PART_MAIN);
      lv_label_set_text(l, LV_SYMBOL_OK);
      lv_obj_set_style_text_color(l, hubui::lv_green(), LV_PART_MAIN);
    } else if (i == stepIndex) {         // active
      lv_obj_set_style_bg_opa(mk, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_bg_color(mk, hubui::lv_cyan(), LV_PART_MAIN);
      lv_obj_set_style_border_width(mk, 0, LV_PART_MAIN);
      snprintf(b, sizeof(b), "%u", (unsigned)(i + 1));
      lv_label_set_text(l, b);
      lv_obj_set_style_text_color(l, lv_color_black(), LV_PART_MAIN);
    } else {                             // pending
      lv_obj_set_style_bg_opa(mk, LV_OPA_TRANSP, LV_PART_MAIN);
      lv_obj_set_style_border_width(mk, 2, LV_PART_MAIN);
      lv_obj_set_style_border_color(mk, hubui::lv_slate(), LV_PART_MAIN);
      snprintf(b, sizeof(b), "%u", (unsigned)(i + 1));
      lv_label_set_text(l, b);
      lv_obj_set_style_text_color(l, hubui::lv_slate(), LV_PART_MAIN);
    }
  }
  s_activeCx = targetX; s_activeCy = targetY;   // pulsing ring follows the active target
}

void close() {
  if (s_prev) lv_scr_load(s_prev);
  if (s_screen) { lv_obj_del(s_screen); s_screen = nullptr; }  // deletes children (+ anim var) too
  s_prev = nullptr; s_step = nullptr; s_ring = nullptr;
  for (auto& m : s_marks)   m = nullptr;
  for (auto& l : s_markLbl) l = nullptr;
}
}  // namespace CalibrationScreen
