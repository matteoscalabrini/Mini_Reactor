#include "features/hub/HubRuntime.hpp"
#include <Arduino.h>
#include "app_config.hpp"
#include "features/hub/power/Axp2101.hpp"
#include "features/hub/display/HubDisplay.hpp"
#include <lvgl.h>

namespace HubRuntime {

static Axp2101 g_axp{Wire, AppConfig::Hub::kAxp2101Address};
static HubDisplay g_display;

void begin() {
  Serial.begin(AppConfig::kSerialBaud);
  delay(AppConfig::kSerialStartupDelayMs);
  Serial.printf("[HUB] boot — firmware %s\n", AppConfig::kFirmwareVersion);
  Serial.printf("[HUB] free heap %u, PSRAM %u\n",
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());

  if (g_axp.begin(AppConfig::PinoutHub::kI2cSda, AppConfig::PinoutHub::kI2cScl,
                  AppConfig::Hub::kI2cClockHz)) {
    Serial.println("[HUB] axp2101: enabled");
  } else {
    Serial.printf("[HUB] axp2101: FAULT (%s)\n", g_axp.lastErrorString());
  }

  if (AppConfig::HubFeatures::kEnableDisplay && g_display.begin()) {
    lv_obj_t* label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "MINI-REACTOR HUB");
    lv_obj_center(label);
    Serial.println("[HUB] display: enabled");
  } else {
    Serial.println("[HUB] display: disabled");
  }
}

void tick() {
  g_display.tick();
  static uint32_t last = 0;
  const uint32_t now = millis();
  if (now - last >= AppConfig::Hub::kPollMs) {
    last = now;
    if (AppConfig::HubFeatures::kEnablePmicTelemetry) {
      Axp2101::State s;
      if (g_axp.refreshStatus(s)) {
        Serial.printf("[HUB] batt %d%% %umV vbus=%d\n",
                      s.batteryPercent, s.batteryVoltageMv, (int)s.vbusPresent);
      }
    }
  }
}

}  // namespace HubRuntime
