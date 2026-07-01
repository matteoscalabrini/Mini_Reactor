#include "features/hub/ui/HubUi.hpp"
#include "features/hub/ui/HomeScreen.hpp"
#include "features/hub/ui/DetailsScreen.hpp"
#include "features/hub/ui/PairScreen.hpp"
#include "features/hub/ui/HubAssets.hpp"

namespace HubUi {
namespace {
lv_obj_t *s_home, *s_details, *s_pair, *s_dotL, *s_dotR, *s_overlay, *s_ovTitle;
int  s_page = 0;                 // 0 home, 1 details
bool s_confirmStop=false, s_confirmStart=false;
volatile bool s_outStop=false, s_outStart=false, s_outPause=false, s_outPair=false;

void showPage(int p) {
  s_page = p;
  lv_obj_clear_flag(p==0 ? s_home : s_details, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag  (p==0 ? s_details : s_home, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_bg_color(s_dotL, p==0 ? hubui::lv_cyan() : hubui::lv_slate(), 0);
  lv_obj_set_style_bg_color(s_dotR, p==1 ? hubui::lv_cyan() : hubui::lv_slate(), 0);
}
void onGesture(lv_event_t*) {
  if (!lv_obj_has_flag(s_overlay, LV_OBJ_FLAG_HIDDEN)) return;   // ignore swipes under an overlay
  if (!lv_obj_has_flag(s_pair, LV_OBJ_FLAG_HIDDEN)) return;      // ...or while pairing
  const lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_get_act());
  if (d==LV_DIR_LEFT  && s_page==0) showPage(1);
  if (d==LV_DIR_RIGHT && s_page==1) showPage(0);
}
void openConfirm(const char* title, bool stop) {
  s_confirmStop = stop; s_confirmStart = !stop;
  lv_label_set_text(s_ovTitle, title);
  lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}
void onCancel(lv_event_t*){ s_confirmStop=s_confirmStart=false; lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN); }
void onYes(lv_event_t*){
  if (s_confirmStop)  s_outStop=true;
  if (s_confirmStart) s_outStart=true;
  s_confirmStop=s_confirmStart=false; lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}
lv_obj_t* mkDot(lv_obj_t* p, lv_coord_t x) {
  lv_obj_t* d=lv_obj_create(p); lv_obj_remove_style_all(d);
  lv_obj_set_size(d,8,8); lv_obj_set_style_radius(d,LV_RADIUS_CIRCLE,0); lv_obj_set_style_bg_opa(d,LV_OPA_COVER,0);
  lv_obj_align(d, LV_ALIGN_BOTTOM_MID, x, -132); return d;
}
lv_obj_t* mkOvBtn(lv_obj_t* p, lv_coord_t x, const char* txt, bool danger, lv_event_cb_t cb) {
  lv_obj_t* b=lv_obj_create(p); lv_obj_remove_style_all(b); lv_obj_set_size(b,150,56);
  lv_obj_align(b,LV_ALIGN_BOTTOM_MID,x,-60); lv_obj_set_style_radius(b,28,0);
  lv_obj_set_style_bg_opa(b, danger?LV_OPA_COVER:LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_color(b, hubui::lv_red(), 0);
  lv_obj_set_style_border_width(b, danger?0:2, 0); lv_obj_set_style_border_color(b, hubui::lv_slate(), 0);
  lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE); lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* l=lv_label_create(b); lv_label_set_text(l,txt);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(l, danger?lv_color_white():hubui::lv_slate(), 0); lv_obj_center(l);
  return b;
}
}  // namespace

void begin() {
  lv_obj_t* scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(scr, onGesture, LV_EVENT_GESTURE, nullptr);

  s_home = lv_obj_create(scr); lv_obj_remove_style_all(s_home); lv_obj_set_size(s_home, 466, 466);
  HomeScreen::create(s_home);
  s_details = lv_obj_create(scr); lv_obj_remove_style_all(s_details); lv_obj_set_size(s_details, 466, 466);
  DetailsScreen::create(s_details);
  s_pair = lv_obj_create(scr); lv_obj_remove_style_all(s_pair); lv_obj_set_size(s_pair, 466, 466);
  PairScreen::create(s_pair);

  s_dotL = mkDot(scr, -8); s_dotR = mkDot(scr, 8);

  s_overlay = lv_obj_create(scr); lv_obj_remove_style_all(s_overlay); lv_obj_set_size(s_overlay, 466, 466);
  lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0); lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
  lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
  s_ovTitle = lv_label_create(s_overlay); lv_obj_set_style_text_font(s_ovTitle, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(s_ovTitle, lv_color_white(), 0); lv_obj_align(s_ovTitle, LV_ALIGN_CENTER, 0, -40);
  mkOvBtn(s_overlay, -80, "CANCEL", false, onCancel);
  mkOvBtn(s_overlay,  80, "YES",    true,  onYes);
  lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);

  showPage(0);
}

void update(const hubui::Model& m, Mode mode, bool searching, uint8_t sweepChannel) {
  if (mode == Mode::Pairing) {
    lv_obj_clear_flag(s_pair, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_home, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(s_details, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_dotL, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(s_dotR, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    PairScreen::update(searching, sweepChannel);
    if (PairScreen::consumePair()) s_outPair = true;
    return;
  }
  lv_obj_add_flag(s_pair, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(s_dotL, LV_OBJ_FLAG_HIDDEN); lv_obj_clear_flag(s_dotR, LV_OBJ_FLAG_HIDDEN);
  showPage(s_page);
  HomeScreen::update(m); DetailsScreen::update(m);

  if (HomeScreen::consumeStop())  openConfirm("STOP RUN?",  true);
  if (HomeScreen::consumeStart()) openConfirm("START RUN?", false);
  if (HomeScreen::consumePause()) s_outPause = true;
}

bool consumeStop()  { if(!s_outStop)  return false; s_outStop=false;  return true; }
bool consumeStart() { if(!s_outStart) return false; s_outStart=false; return true; }
bool consumePause() { if(!s_outPause) return false; s_outPause=false; return true; }
bool consumePair()  { if(!s_outPair)  return false; s_outPair=false;  return true; }
}  // namespace HubUi
