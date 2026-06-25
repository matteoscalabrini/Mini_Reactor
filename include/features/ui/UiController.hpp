#pragma once
#include "features/ui/UiTypes.hpp"

namespace ui {

class UiController {
 public:
  struct Config {
    float minRpm = 0.5f;
    float maxRpm = 30.0f;
    float minTargetC = 0.0f;
    float maxTargetC = 55.0f;
    float targetStepC = 0.5f;
    float rpmStep = 0.5f;
  };

  UiController(ReactorControl& control, const Config& cfg);

  void handle(UiEvent e, const ReactorSnapshot& s);

  Screen screen() const { return screen_; }
  int    menuIndex() const { return menuIndex_; }
  float  editTargetC() const { return editTargetC_; }
  float  editRpm() const { return editRpm_; }
  bool   faultOverlay(const ReactorSnapshot& s) const { return s.sensorFault || s.safetyTripped; }

 private:
  void goHome();
  void openMenu();
  void activateMenuItem(const ReactorSnapshot& s);
  static float clampf(float v, float lo, float hi);

  ReactorControl& control_;
  Config cfg_;
  Screen screen_ = Screen::Dashboard;
  int    menuIndex_ = 0;
  float  editTargetC_ = 0.0f;
  float  editRpm_ = 0.0f;
};

}  // namespace ui
