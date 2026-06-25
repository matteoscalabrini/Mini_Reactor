#pragma once
#include <U8g2lib.h>
#include "features/ui/UiController.hpp"
#include "features/ui/UiTypes.hpp"

namespace ui {

class Display {
 public:
  Display(uint8_t i2cAddr, int sclPin, int sdaPin, uint32_t busHz);
  void begin();                       // owns Wire1 on its own pins (call once)
  bool present() const { return present_; }
  void render(const UiController& ui, const ReactorSnapshot& s);

 private:
  void drawDashboard(const ReactorSnapshot& s);
  void drawMenu(const UiController& ui, const ReactorSnapshot& s);
  void drawEdit(const UiController& ui, bool target);
  void drawInfo(const ReactorSnapshot& s);
  void drawFaultBanner(const ReactorSnapshot& s);

  // Software (bit-banged) I2C on its own pins (GPIO43/44). No hardware I2C
  // peripheral, so no contention with UART0 on those pads or the HUSB238 on Wire.
  U8G2_SH1107_PIMORONI_128X128_F_SW_I2C u8g2_;
  uint8_t addr_;
  int sclPin_;
  int sdaPin_;
  uint32_t busHz_;
  bool present_ = false;
};

}  // namespace ui
