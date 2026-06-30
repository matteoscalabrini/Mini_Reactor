#pragma once
#include <stdint.h>
namespace BringupScreen {
struct IcStatus { const char* name; uint8_t state; };  // 0=off/grey, 1=ok/green, 2=error/red
struct Snapshot {
  IcStatus ics[7];                 // axp,qmi,pcf,cst,tca,es8311,es7210
  int batteryPercent; uint16_t batteryMv; bool charging; bool vbus;
  int16_t touchX, touchY; bool touchPressed;
  float accelX, accelY, accelZ;
  uint8_t rtcHours, rtcMinutes, rtcSeconds;
  uint32_t freeHeap, freePsram;
  const char* firmware;
};
void create();                     // build the LVGL widget tree once
void update(const Snapshot& s);    // refresh labels/colors
}  // namespace BringupScreen
