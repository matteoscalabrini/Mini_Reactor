#pragma once
#include <U8g2lib.h>
#include "ui/UiController.hpp"
#include "ui/UiTypes.hpp"

namespace ui {

class Display {
 public:
  Display(uint8_t i2cAddr, int sclPin, int sdaPin);
  void begin();                       // call AFTER Wire.begin(sda, scl)
  bool present() const { return present_; }
  void render(const UiController& ui, const ReactorSnapshot& s);

 private:
  void drawDashboard(const ReactorSnapshot& s);
  void drawMenu(const UiController& ui, const ReactorSnapshot& s);
  void drawEdit(const UiController& ui, bool target);
  void drawInfo(const ReactorSnapshot& s);
  void drawFaultBanner(const ReactorSnapshot& s);

  U8G2_SH1107_PIMORONI_128X128_F_HW_I2C u8g2_;
  uint8_t addr_;
  bool present_ = false;
};

}  // namespace ui
