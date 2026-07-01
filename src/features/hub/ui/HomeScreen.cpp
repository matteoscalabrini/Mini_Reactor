#include "features/hub/ui/HomeScreen.hpp"
#include "features/hub/ui/HubAssets.hpp"
#include <cstdio>

namespace HomeScreen {
namespace {
lv_obj_t *s_dot, *s_batt, *s_name, *s_hero, *s_sub, *s_pauseBtn, *s_pauseLbl, *s_stopBtn, *s_startBtn;
volatile bool s_stop=false, s_start=false, s_pause=false;
void onStop (lv_event_t*){ s_stop=true; }
void onStart(lv_event_t*){ s_start=true; }
void onPause(lv_event_t*){ s_pause=true; }

lv_obj_t* mkBtn(lv_obj_t* p, const char* txt, lv_color_t bg, lv_color_t fg, bool ghost,
                lv_event_cb_t cb, lv_obj_t** lblOut=nullptr) {
  lv_obj_t* b = lv_obj_create(p);
  lv_obj_remove_style_all(b);
  lv_obj_set_size(b, 150, 56);
  lv_obj_set_style_radius(b, 28, 0);
  lv_obj_set_style_bg_opa(b, ghost ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(b, bg, 0);
  lv_obj_set_style_border_width(b, ghost ? 2 : 0, 0);
  lv_obj_set_style_border_color(b, bg, 0);
  lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* l = lv_label_create(b); lv_label_set_text(l, txt);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(l, fg, 0); lv_obj_center(l);
  if (lblOut) *lblOut = l;
  return b;
}
}  // namespace

void create(lv_obj_t* parent) {
  lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
  lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

  s_dot = lv_obj_create(parent); lv_obj_remove_style_all(s_dot);
  lv_obj_set_size(s_dot, 10, 10); lv_obj_set_style_radius(s_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(s_dot, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(s_dot, hubui::lv_cyan(), 0);
  lv_obj_align(s_dot, LV_ALIGN_TOP_LEFT, 108, 62);

  lv_obj_t* wm = lv_img_create(parent); lv_img_set_src(wm, &wordmark_white);
  lv_obj_align(wm, LV_ALIGN_TOP_MID, 0, 52);

  s_batt = lv_label_create(parent); lv_obj_set_style_text_color(s_batt, hubui::lv_muted(), 0);
  lv_obj_set_style_text_font(s_batt, &lv_font_montserrat_14, 0);
  lv_obj_align(s_batt, LV_ALIGN_TOP_RIGHT, -104, 56);

  s_name = lv_label_create(parent); lv_obj_set_style_text_color(s_name, hubui::lv_slate(), 0);
  lv_obj_set_style_text_font(s_name, &lv_font_montserrat_14, 0);
  lv_obj_align(s_name, LV_ALIGN_CENTER, 0, -80);

  s_hero = lv_label_create(parent); lv_obj_set_style_text_font(s_hero, &hub_doto_88, 0);
  lv_obj_set_style_text_color(s_hero, hubui::lv_cyan(), 0); lv_obj_align(s_hero, LV_ALIGN_CENTER, 0, -10);
  lv_label_set_text(s_hero, "--°");

  s_sub = lv_label_create(parent); lv_obj_set_style_text_color(s_sub, hubui::lv_muted(), 0);
  lv_obj_set_style_text_font(s_sub, &lv_font_montserrat_14, 0);
  lv_obj_align(s_sub, LV_ALIGN_CENTER, 0, 46);

  s_pauseBtn = mkBtn(parent, "PAUSE", hubui::lv_slate(), hubui::lv_slate(), true,  onPause, &s_pauseLbl);
  s_stopBtn  = mkBtn(parent, "STOP",  hubui::lv_red(),   lv_color_white(),  false, onStop);
  s_startBtn = mkBtn(parent, "START", hubui::lv_cyan(),  lv_color_black(),  false, onStart);
}

void update(const hubui::Model& m) {
  char b[64];
  snprintf(b, sizeof(b), "%u%%", (unsigned)m.batteryPct); lv_label_set_text(s_batt, b);

  const bool safety = m.safetyTripped;
  const bool idle   = !m.runActive && !safety;
  lv_obj_set_style_bg_color(s_dot, safety ? hubui::lv_redHero() : hubui::lv_cyan(), 0);

  const hubui::HeroState st = hubui::heroState(m.tempValid, m.safetyTripped, m.probeFault, m.heaterActive);
  lv_obj_set_style_text_color(s_hero, idle ? hubui::lv_slate() : hubui::toLv(hubui::heroColor(st)), 0);
  if (m.tempValid) snprintf(b, sizeof(b), "%.1f°", m.tempC);
  else             snprintf(b, sizeof(b), "--°");
  lv_label_set_text(s_hero, b);
  lv_obj_align(s_hero, LV_ALIGN_CENTER, 0, -10);

  if (safety) {
    lv_label_set_text(s_name, "OVER-TEMP CUTOFF");
    lv_obj_set_style_text_color(s_name, hubui::lv_redHero(), 0);
    lv_label_set_text(s_sub, "heater forced off • check reactor");
  } else if (idle) {
    lv_label_set_text(s_name, "NO RUN");
    lv_obj_set_style_text_color(s_name, hubui::lv_slate(), 0);
    lv_label_set_text(s_sub, "ambient • ready");
  } else {
    char e[16]; hubui::formatHms(m.elapsedSec, e, sizeof(e));
    snprintf(b, sizeof(b), "RUNNING • %s", e); lv_label_set_text(s_name, b);
    lv_obj_set_style_text_color(s_name, hubui::lv_slate(), 0);
    snprintf(b, sizeof(b), "target %.1f° • heat %.0f%% • %.0f rpm", m.setpointC, m.heaterPct, m.rpm);
    lv_label_set_text(s_sub, b);
  }
  lv_obj_align(s_name, LV_ALIGN_CENTER, 0, -80);
  lv_obj_align(s_sub,  LV_ALIGN_CENTER, 0, 46);

  lv_obj_add_flag(s_startBtn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_pauseBtn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_stopBtn,  LV_OBJ_FLAG_HIDDEN);
  if (idle) {
    lv_obj_align(s_startBtn, LV_ALIGN_BOTTOM_MID, 0, -60);
    lv_obj_clear_flag(s_startBtn, LV_OBJ_FLAG_HIDDEN);
  } else if (safety) {
    lv_obj_align(s_stopBtn, LV_ALIGN_BOTTOM_MID, 0, -60);
    lv_obj_clear_flag(s_stopBtn, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_align(s_pauseBtn, LV_ALIGN_BOTTOM_MID, -80, -60);
    lv_obj_align(s_stopBtn,  LV_ALIGN_BOTTOM_MID,  80, -60);
    lv_obj_clear_flag(s_pauseBtn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_stopBtn,  LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_pauseLbl, (m.motorPaused || m.fullHold) ? "RESUME" : "PAUSE");
  }
}

bool consumeStop()  { if(!s_stop)  return false; s_stop=false;  return true; }
bool consumeStart() { if(!s_start) return false; s_start=false; return true; }
bool consumePause() { if(!s_pause) return false; s_pause=false; return true; }
}  // namespace HomeScreen
