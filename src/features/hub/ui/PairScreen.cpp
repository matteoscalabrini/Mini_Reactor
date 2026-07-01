#include "features/hub/ui/PairScreen.hpp"
#include "features/hub/ui/HubAssets.hpp"
#include <cstdio>

namespace PairScreen {
namespace {
constexpr int kRingMin = 40;
constexpr int kRingMax = 280;   // spread wide across the round panel

lv_obj_t *s_state, *s_instr, *s_pairBtn, *s_rings[2], *s_chan;
volatile bool s_pair=false;
void onPair(lv_event_t*){ s_pair=true; }

// Expanding-ring animation, centred on the screen; fades as it grows.
void ringExec(void* var, int32_t v) {
  lv_obj_t* r = (lv_obj_t*)var;
  lv_obj_set_size(r, v, v);
  lv_obj_align(r, LV_ALIGN_CENTER, 0, 0);
  int op = 220 - (v - kRingMin) * 220 / (kRingMax - kRingMin);   // kRingMin -> 220, kRingMax -> 0
  if (op < 0) op = 0;
  lv_obj_set_style_opa(r, (lv_opa_t)op, LV_PART_MAIN);
}
}  // namespace

void create(lv_obj_t* parent) {
  lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
  lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* wm = lv_img_create(parent); lv_img_set_src(wm, &wordmark_white);
  lv_obj_align(wm, LV_ALIGN_TOP_MID, 0, 48);

  // Two staggered radar rings, centred on the screen.
  for (int i = 0; i < 2; i++) {
    s_rings[i] = lv_obj_create(parent); lv_obj_remove_style_all(s_rings[i]);
    lv_obj_set_style_radius(s_rings[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_rings[i], 2, 0);
    lv_obj_set_style_border_color(s_rings[i], hubui::lv_cyan(), 0);
    lv_obj_align(s_rings[i], LV_ALIGN_CENTER, 0, 0);
    lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, s_rings[i]); lv_anim_set_exec_cb(&a, ringExec);
    lv_anim_set_values(&a, kRingMin, kRingMax); lv_anim_set_time(&a, 1800);
    lv_anim_set_delay(&a, i * 900); lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
  }

  s_state = lv_label_create(parent); lv_obj_set_style_text_color(s_state, hubui::lv_slate(), 0);
  lv_obj_set_style_text_font(s_state, &lv_font_montserrat_14, 0);

  s_chan = lv_label_create(parent); lv_obj_set_style_text_color(s_chan, hubui::lv_cyan(), 0);
  lv_obj_set_style_text_font(s_chan, &lv_font_montserrat_14, 0);

  s_instr = lv_label_create(parent); lv_obj_set_style_text_color(s_instr, hubui::lv_muted(), 0);
  lv_obj_set_style_text_font(s_instr, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(s_instr, LV_TEXT_ALIGN_CENTER, 0);

  s_pairBtn = lv_obj_create(parent); lv_obj_remove_style_all(s_pairBtn);
  lv_obj_set_size(s_pairBtn, 170, 58); lv_obj_align(s_pairBtn, LV_ALIGN_BOTTOM_MID, 0, -64);
  lv_obj_set_style_radius(s_pairBtn, 28, 0);
  lv_obj_set_style_bg_opa(s_pairBtn, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(s_pairBtn, hubui::lv_cyan(), 0);
  lv_obj_clear_flag(s_pairBtn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_pairBtn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s_pairBtn, onPair, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* pl = lv_label_create(s_pairBtn); lv_label_set_text(pl, "PAIR");
  lv_obj_set_style_text_font(pl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(pl, lv_color_black(), 0); lv_obj_center(pl);
}

void update(bool searching, uint8_t sweepChannel) {
  char b[24];
  if (searching) {
    for (auto* r : s_rings) lv_obj_clear_flag(r, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_chan, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_pairBtn, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_state, "SEARCHING");
    snprintf(b, sizeof(b), "CH %u", (unsigned)sweepChannel); lv_label_set_text(s_chan, b);
    lv_label_set_text(s_instr, "Open the reactor's\npairing window.");
    lv_obj_align(s_state, LV_ALIGN_CENTER, 0, -10);
    lv_obj_align(s_chan,  LV_ALIGN_CENTER, 0, 14);
    lv_obj_align(s_instr, LV_ALIGN_BOTTOM_MID, 0, -44);
  } else {
    for (auto* r : s_rings) lv_obj_add_flag(r, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_chan, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_pairBtn, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_state, "NOT PAIRED");
    lv_label_set_text(s_instr, "Tap PAIR, then press\n'Pair HUB' on the reactor.");
    lv_obj_align(s_state, LV_ALIGN_CENTER, 0, -24);
    lv_obj_align(s_instr, LV_ALIGN_CENTER, 0, 24);
  }
}

bool consumePair() { if(!s_pair) return false; s_pair=false; return true; }
}  // namespace PairScreen
