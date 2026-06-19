#include "ui/UiController.hpp"

namespace ui {

UiController::UiController(ReactorControl& control, const Config& cfg)
    : control_(control), cfg_(cfg) {}

float UiController::clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

void UiController::goHome() { screen_ = Screen::Dashboard; }
void UiController::openMenu() { screen_ = Screen::Menu; menuIndex_ = 0; }

void UiController::handle(UiEvent e, const ReactorSnapshot& s) {
  if (e == UiEvent::EncLongPush) { goHome(); return; }           // universal Home
  if (e == UiEvent::Btn1) { if (s.running) control_.setMotorPaused(!s.motorPaused); return; }
  if (e == UiEvent::Btn2) { if (s.running) control_.setFullHold(!s.fullHold); return; }
  if (e == UiEvent::Btn3) {
    screen_ = (screen_ == Screen::Info) ? Screen::Dashboard : Screen::Info;
    return;
  }

  switch (screen_) {
    case Screen::Dashboard:
      if (e == UiEvent::EncShortPush) openMenu();
      break;
    case Screen::Info:
      if (e == UiEvent::EncShortPush) goHome();
      break;
    case Screen::Menu:
      if (e == UiEvent::RotateCW)        menuIndex_ = (menuIndex_ + 1) % kMenuCount;
      else if (e == UiEvent::RotateCCW)  menuIndex_ = (menuIndex_ + kMenuCount - 1) % kMenuCount;
      else if (e == UiEvent::EncShortPush) activateMenuItem(s);
      break;
    case Screen::EditTarget:
      if (e == UiEvent::RotateCW)        editTargetC_ = clampf(editTargetC_ + cfg_.targetStepC, cfg_.minTargetC, cfg_.maxTargetC);
      else if (e == UiEvent::RotateCCW)  editTargetC_ = clampf(editTargetC_ - cfg_.targetStepC, cfg_.minTargetC, cfg_.maxTargetC);
      else if (e == UiEvent::EncShortPush) { control_.setTargetC(editTargetC_); screen_ = Screen::Menu; }
      break;
    case Screen::EditSpeed:
      if (e == UiEvent::RotateCW)        editRpm_ = clampf(editRpm_ + cfg_.rpmStep, cfg_.minRpm, cfg_.maxRpm);
      else if (e == UiEvent::RotateCCW)  editRpm_ = clampf(editRpm_ - cfg_.rpmStep, cfg_.minRpm, cfg_.maxRpm);
      else if (e == UiEvent::EncShortPush) { control_.setRpm(editRpm_); screen_ = Screen::Menu; }
      break;
  }
}

void UiController::activateMenuItem(const ReactorSnapshot& s) {
  switch (menuIndex_) {
    case kMenuStartStop:
      if (s.running) control_.stopRun();
      else           control_.startRun(s.setpointC, s.rpmSetpoint);
      break;
    case kMenuTarget:
      editTargetC_ = clampf(s.setpointC, cfg_.minTargetC, cfg_.maxTargetC);
      screen_ = Screen::EditTarget;
      break;
    case kMenuSpeed:
      editRpm_ = clampf(s.rpmSetpoint, cfg_.minRpm, cfg_.maxRpm);
      screen_ = Screen::EditSpeed;
      break;
    case kMenuInfo:
      screen_ = Screen::Info;
      break;
    case kMenuBack:
      goHome();
      break;
  }
}

}  // namespace ui
