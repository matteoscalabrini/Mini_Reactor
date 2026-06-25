#include "features/ui/Display.hpp"

#include <driver/gpio.h>
#include <math.h>
#include <stdio.h>

namespace ui {

Display::Display(uint8_t i2cAddr, int sclPin, int sdaPin, uint32_t busHz)
    : u8g2_(U8G2_R0, /*clock=*/sclPin, /*data=*/sdaPin, U8X8_PIN_NONE),
      addr_(i2cAddr),
      sclPin_(sclPin),
      sdaPin_(sdaPin),
      busHz_(busHz) {}

void Display::begin() {
  // GPIO43/44 are UART0 (U0TXD/U0RXD); the core can leave UART0 bound to those
  // pads. Detach them before bit-banging so UART0 doesn't fight the SW-I2C lines.
  gpio_reset_pin(static_cast<gpio_num_t>(sdaPin_));
  gpio_reset_pin(static_cast<gpio_num_t>(sclPin_));
  // Software (bit-banged) I2C — u8g2 drives the pins directly via digitalWrite, so
  // there is no hardware I2C peripheral to contend with UART0 or the HUSB238's Wire.
  u8g2_.setI2CAddress(addr_ << 1);
  u8g2_.setBusClock(busHz_);
  u8g2_.begin();
  present_ = true;                   // SW I2C can't probe; the wired panel is assumed present
  u8g2_.setContrast(255);
}

static void fmtElapsed(uint32_t sec, char* out, size_t n) {
  uint32_t h = sec / 3600, m = (sec % 3600) / 60, s = sec % 60;
  if (h) snprintf(out, n, "%lu:%02lu:%02lu", (unsigned long)h, (unsigned long)m, (unsigned long)s);
  else   snprintf(out, n, "%02lu:%02lu", (unsigned long)m, (unsigned long)s);
}

void Display::render(const UiController& ui, const ReactorSnapshot& s) {
  if (!present_) return;
  u8g2_.clearBuffer();
  if (ui.faultOverlay(s)) {
    drawFaultBanner(s);
  } else {
    switch (ui.screen()) {
      case Screen::Dashboard:  drawDashboard(s); break;
      case Screen::Menu:       drawMenu(ui, s); break;
      case Screen::EditTarget: drawEdit(ui, true); break;
      case Screen::EditSpeed:  drawEdit(ui, false); break;
      case Screen::Info:       drawInfo(s); break;
    }
  }
  u8g2_.sendBuffer();
}

void Display::drawDashboard(const ReactorSnapshot& s) {
  char buf[24];
  u8g2_.setFont(u8g2_font_6x12_tr);
  const char* state = "STOPPED";
  if (s.fullHold) state = "HOLD: M+H";
  else if (s.motorPaused) state = "MOTOR PAUSE";
  else if (s.running) state = "RUNNING";
  u8g2_.drawStr(2, 11, state);
  u8g2_.drawStr(95, 11, s.wifiConnected ? "wifi" : "----");

  // Hero temperature.
  u8g2_.setFont(u8g2_font_logisoso28_tn);
  if (isnan(s.liquidTempC)) snprintf(buf, sizeof(buf), "--.-");
  else snprintf(buf, sizeof(buf), "%.1f", s.liquidTempC);
  u8g2_.drawStr(8, 58, buf);
  u8g2_.setFont(u8g2_font_6x12_tr);
  u8g2_.drawStr(100, 40, "C");

  snprintf(buf, sizeof(buf), "target %.1f", s.setpointC);
  u8g2_.drawStr(8, 74, buf);

  snprintf(buf, sizeof(buf), "RPM %.1f", s.rpmSetpoint);
  u8g2_.drawStr(2, 98, buf);
  char t[12]; fmtElapsed(s.elapsedSec, t, sizeof(t));
  snprintf(buf, sizeof(buf), "time %s", t);
  u8g2_.drawStr(64, 98, buf);
  snprintf(buf, sizeof(buf), "heater %.0f%%", s.heaterDutyPct);
  u8g2_.drawStr(2, 116, buf);
}

void Display::drawMenu(const UiController& ui, const ReactorSnapshot& s) {
  char buf[24];
  u8g2_.setFont(u8g2_font_6x12_tr);
  u8g2_.drawStr(2, 11, "CONTROL");
  const char* labels[kMenuCount];
  labels[kMenuStartStop] = s.running ? "Stop run" : "Start run";
  labels[kMenuTarget]    = "Target temp";
  labels[kMenuSpeed]     = "Speed";
  labels[kMenuInfo]      = "Info";
  labels[kMenuBack]      = "Back";
  for (int i = 0; i < kMenuCount; ++i) {
    int y = 30 + i * 16;
    if (i == ui.menuIndex()) u8g2_.drawStr(2, y, ">");
    u8g2_.drawStr(14, y, labels[i]);
    if (i == kMenuTarget) { snprintf(buf, sizeof(buf), "%.1fC", s.setpointC); u8g2_.drawStr(86, y, buf); }
    if (i == kMenuSpeed)  { snprintf(buf, sizeof(buf), "%.1f", s.rpmSetpoint); u8g2_.drawStr(92, y, buf); }
  }
}

void Display::drawEdit(const UiController& ui, bool target) {
  char buf[24];
  u8g2_.setFont(u8g2_font_6x12_tr);
  u8g2_.drawStr(2, 11, target ? "SET TARGET TEMP" : "SET SPEED");
  u8g2_.setFont(u8g2_font_logisoso24_tn);
  if (target) snprintf(buf, sizeof(buf), "%.1f", ui.editTargetC());
  else        snprintf(buf, sizeof(buf), "%.1f", ui.editRpm());
  u8g2_.drawStr(20, 70, buf);
  u8g2_.setFont(u8g2_font_6x12_tr);
  u8g2_.drawStr(2, 110, "push: apply  long: home");
}

void Display::drawInfo(const ReactorSnapshot& s) {
  char buf[28];
  u8g2_.setFont(u8g2_font_6x12_tr);
  u8g2_.drawStr(2, 11, "INFO");
  snprintf(buf, sizeof(buf), "WiFi %.20s", s.wifiConnected ? s.wifiSsid : "down"); u8g2_.drawStr(2, 30, buf);
  snprintf(buf, sizeof(buf), "IP   %s", s.ip);                                  u8g2_.drawStr(2, 46, buf);
  snprintf(buf, sizeof(buf), "RSSI %d dBm", s.rssi);                            u8g2_.drawStr(2, 62, buf);
  snprintf(buf, sizeof(buf), "SD   %s", s.sdMounted ? "mounted" : "no card");   u8g2_.drawStr(2, 78, buf);
  snprintf(buf, sizeof(buf), "fw   %s", s.firmware);                            u8g2_.drawStr(2, 94, buf);
}

void Display::drawFaultBanner(const ReactorSnapshot& s) {
  u8g2_.setFont(u8g2_font_6x12_tr);
  u8g2_.drawBox(0, 40, 128, 40);
  u8g2_.setDrawColor(0);
  if (s.safetyTripped) {
    u8g2_.drawStr(8, 58, "! SAFETY TRIP");
    u8g2_.drawStr(8, 72, "  HEATER OFF");
  } else {
    u8g2_.drawStr(8, 58, "! LIQUID PROBE");
    u8g2_.drawStr(8, 72, "  FAULT");
  }
  u8g2_.setDrawColor(1);
}

}  // namespace ui
