#include "features/hub/HubRuntime.hpp"
#include <Arduino.h>
#include "app_config.hpp"
#include "features/hub/power/Axp2101.hpp"
#include "features/hub/display/HubDisplay.hpp"
#include "features/hub/touch/Cst9217.hpp"
#include <lvgl.h>

namespace HubRuntime {

static Axp2101   g_axp{Wire, AppConfig::Hub::kAxp2101Address};
static HubDisplay g_display;
static Cst9217   g_touch{Wire, AppConfig::Hub::kCst9217Address};

static void hubTouchRead(lv_indev_drv_t*, lv_indev_data_t* data) {
  int16_t x, y; bool pressed;
  if (g_touch.readPoint(x, y, pressed)) {
    data->point.x = x; data->point.y = y;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  } else { data->state = LV_INDEV_STATE_RELEASED; }
}

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

  if (AppConfig::HubFeatures::kEnableTouch) {
    g_touch.begin();
    static lv_indev_drv_t d;
    lv_indev_drv_init(&d);
    d.type     = LV_INDEV_TYPE_POINTER;
    d.read_cb  = hubTouchRead;
    lv_indev_drv_register(&d);
    Serial.println("[HUB] touch: enabled");
  } else {
    Serial.println("[HUB] touch: disabled");
  }
}

void tick() {
  g_display.tick();

  const uint32_t now = millis();

  // Touch polling at kTouchPollMs cadence
  if (AppConfig::HubFeatures::kEnableTouch) {
    static uint32_t lastTouch = 0;
    if (now - lastTouch >= AppConfig::Hub::kTouchPollMs) {
      lastTouch = now;
      Cst9217::State ts;
      g_touch.refresh(ts);
    }
  }

  // PMIC telemetry at kPollMs cadence
  static uint32_t lastPoll = 0;
  if (now - lastPoll >= AppConfig::Hub::kPollMs) {
    lastPoll = now;
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
