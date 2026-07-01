#include "features/hub/ui/EspNowScreen.hpp"
#include <lvgl.h>
#include <cstdio>

// Only lv_label + lv_obj (colored rectangles) — matches BringupScreen's allowed
// widget set. The PAIR tile is an lv_obj with LV_OBJ_FLAG_CLICKABLE + an event cb
// (LV_USE_BTN is 0 in lv_conf.h, but object click events are available).

namespace EspNowScreen {
namespace {
lv_obj_t* s_title = nullptr;
lv_obj_t* s_pairTile = nullptr;
lv_obj_t* s_pairLbl = nullptr;
lv_obj_t* s_line1 = nullptr;
lv_obj_t* s_line2 = nullptr;
lv_obj_t* s_line3 = nullptr;
volatile bool s_pairLatch = false;

void onPairClicked(lv_event_t*) { s_pairLatch = true; }

// Round 466x466 panel: fixed-width, centre-aligned labels aligned to the screen
// centre keep text off the clipped corners and stay centred as the text width
// changes.
lv_obj_t* makeLine(lv_obj_t* parent, lv_coord_t dy, const char* txt) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_width(l, 320);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(l, LV_ALIGN_CENTER, 0, dy);
  lv_label_set_text(l, txt);
  return l;
}
}  // namespace

void create() {
  lv_obj_t* scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, LV_COLOR_MAKE(0x10, 0x10, 0x10), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // Everything centred — the circular panel clips the corners.
  s_title = makeLine(scr, -150, "REACTOR LINK");
  s_line1 = makeLine(scr, -40, "");
  s_line2 = makeLine(scr, -8, "");
  s_line3 = makeLine(scr, 24, "");

  s_pairTile = lv_obj_create(scr);
  lv_obj_set_size(s_pairTile, 170, 58);
  lv_obj_align(s_pairTile, LV_ALIGN_CENTER, 0, 120);
  lv_obj_set_style_radius(s_pairTile, 10, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_pairTile, LV_COLOR_MAKE(0x0a, 0x84, 0xff), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_pairTile, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(s_pairTile, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_pairTile, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s_pairTile, onPairClicked, LV_EVENT_CLICKED, nullptr);
  s_pairLbl = lv_label_create(s_pairTile);
  lv_label_set_text(s_pairLbl, "PAIR");
  lv_obj_center(s_pairLbl);
}

void update(const View& v) {
  if (!s_title) return;
  char b[96];

  const bool showPair = (v.mode == Mode::Unpaired);
  if (showPair) lv_obj_clear_flag(s_pairTile, LV_OBJ_FLAG_HIDDEN);
  else          lv_obj_add_flag(s_pairTile, LV_OBJ_FLAG_HIDDEN);

  switch (v.mode) {
    case Mode::Unpaired:
      lv_label_set_text(s_line1, "Not paired");
      lv_label_set_text(s_line2, "Tap PAIR, then press");
      lv_label_set_text(s_line3, "'Pair HUB' on the reactor");
      break;
    case Mode::Searching:
      snprintf(b, sizeof(b), "Searching... ch %u", (unsigned)v.sweepChannel);
      lv_label_set_text(s_line1, b);
      lv_label_set_text(s_line2, "Open the reactor's");
      lv_label_set_text(s_line3, "pairing window");
      break;
    case Mode::Paired:
      if (v.tempValid) snprintf(b, sizeof(b), "T %.1f / %.1f C  H %.0f%%", v.tempC, v.setpointC, v.heaterPct);
      else             snprintf(b, sizeof(b), "T --- / %.1f C  H %.0f%%", v.setpointC, v.heaterPct);
      lv_label_set_text(s_line1, b);
      snprintf(b, sizeof(b), "rpm %.1f   %s%s", v.rpm,
               v.runActive ? "RUN" : "idle",
               v.fullHold ? " HOLD" : (v.motorPaused ? " PAUSE" : ""));
      lv_label_set_text(s_line2, b);
      snprintf(b, sizeof(b), "%s  elapsed %lus%s",
               v.linkAlive ? "linked" : "DISCONNECTED",
               (unsigned long)v.elapsedSec, v.safetyTripped ? "  !SAFETY" : "");
      lv_label_set_text(s_line3, b);
      break;
  }
}

bool pairPressed() {
  if (!s_pairLatch) return false;
  s_pairLatch = false;
  return true;
}
}  // namespace EspNowScreen
