#include "features/hub/ui/DetailsScreen.hpp"
#include "features/hub/ui/HubAssets.hpp"
#include <cstdio>

namespace DetailsScreen {
namespace {
lv_obj_t* s_val[6];
const char* kKeys[6] = {"Heater", "Process max", "Load (SG)", "Remaining", "Battery", "Link"};
}  // namespace

void create(lv_obj_t* parent) {
  lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
  lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* wm = lv_img_create(parent); lv_img_set_src(wm, &wordmark_white);
  lv_obj_align(wm, LV_ALIGN_TOP_MID, 0, 52);

  for (int i = 0; i < 6; i++) {
    lv_obj_t* k = lv_label_create(parent);
    lv_obj_set_style_text_font(k, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(k, hubui::lv_muted(), 0);
    lv_label_set_text(k, kKeys[i]);
    lv_obj_align(k, LV_ALIGN_TOP_LEFT, 108, 118 + i * 34);

    s_val[i] = lv_label_create(parent);
    lv_obj_set_style_text_font(s_val[i], &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_val[i], lv_color_white(), 0);
    lv_obj_align(s_val[i], LV_ALIGN_TOP_RIGHT, -108, 118 + i * 34);
  }
}

void update(const hubui::Model& m) {
  char b[24];
  if (m.heaterTempValid) snprintf(b, sizeof(b), "%.1f°", m.heaterTempC); else snprintf(b, sizeof(b), "--");
  lv_label_set_text(s_val[0], b);
  snprintf(b, sizeof(b), "%.1f°", m.processMaxC); lv_label_set_text(s_val[1], b);
  if (m.loadValid) snprintf(b, sizeof(b), "%d", (int)m.load); else snprintf(b, sizeof(b), "--");
  lv_label_set_text(s_val[2], b);
  { char r[16]; hubui::formatRemaining(m.remainingSec, r, sizeof(r)); lv_label_set_text(s_val[3], r); }
  snprintf(b, sizeof(b), "%.2fV %u%%", m.batteryMv / 1000.0f, (unsigned)m.batteryPct); lv_label_set_text(s_val[4], b);
  snprintf(b, sizeof(b), "ch%u • %s", (unsigned)m.sweepChannel, m.linked ? "ok" : "lost"); lv_label_set_text(s_val[5], b);
}
}  // namespace DetailsScreen
