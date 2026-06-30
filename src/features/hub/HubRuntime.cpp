#include "features/hub/HubRuntime.hpp"
#include <Arduino.h>
#include "app_config.hpp"
#include "features/hub/power/Axp2101.hpp"
#include "features/hub/display/HubDisplay.hpp"
#include "features/hub/touch/Cst9217.hpp"
#include "features/hub/sensor/Qmi8658.hpp"
#include <lvgl.h>

namespace HubRuntime {

static Axp2101    g_axp{Wire, AppConfig::Hub::kAxp2101Address};
static HubDisplay g_display;
static Cst9217    g_touch{Wire, AppConfig::Hub::kCst9217Address};
static Qmi8658    g_imu{Wire, AppConfig::Hub::kQmi8658Address};

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
    const bool touchOk = g_touch.begin();
    static lv_indev_drv_t d;
    lv_indev_drv_init(&d);
    d.type     = LV_INDEV_TYPE_POINTER;
    d.read_cb  = hubTouchRead;
    lv_indev_drv_register(&d);
    Serial.printf("[HUB] touch: %s\n", touchOk ? "enabled" : "enabled (hardware absent)");
  } else {
    Serial.println("[HUB] touch: disabled");
  }

  if (AppConfig::HubFeatures::kEnableImu) {
    Qmi8658::Config imuCfg;
    imuCfg.featureEnabled          = true;
    imuCfg.pinI2cSda               = AppConfig::PinoutHub::kI2cSda;
    imuCfg.pinI2cScl               = AppConfig::PinoutHub::kI2cScl;
    imuCfg.i2cClockHz              = AppConfig::Hub::kI2cClockHz;
    imuCfg.address                 = AppConfig::Hub::kQmi8658Address;
    imuCfg.enableAddressFallbackProbe = true;
    imuCfg.accelRangeG             = AppConfig::HubImu::kAccelRangeG;
    imuCfg.accelOdrMilliHz         = AppConfig::HubImu::kAccelOdrMilliHz;
    imuCfg.gyroRangeDps            = AppConfig::HubImu::kGyroRangeDps;
    imuCfg.gyroOdrMilliHz          = AppConfig::HubImu::kGyroOdrMilliHz;
    imuCfg.enableAccelLpf          = AppConfig::HubImu::kEnableAccelLpf;
    imuCfg.enableGyroLpf           = AppConfig::HubImu::kEnableGyroLpf;
    const bool imuOk = g_imu.begin(imuCfg);
    Serial.printf("[HUB] imu: %s\n", imuOk ? "enabled" : "enabled (hardware absent)");
  } else {
    Serial.println("[HUB] imu: disabled");
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

  // IMU polling at kMotionPollMs cadence
  if (AppConfig::HubFeatures::kEnableImu) {
    static uint32_t lastMotion = 0;
    if (now - lastMotion >= AppConfig::Hub::kMotionPollMs) {
      lastMotion = now;
      Qmi8658::Status st;
      if (g_imu.poll(st)) {
        Serial.printf("[HUB] imu ax=%.3f ay=%.3f az=%.3f gx=%.1f gy=%.1f gz=%.1f\n",
                      st.accelXg, st.accelYg, st.accelZg,
                      st.gyroXdps, st.gyroYdps, st.gyroZdps);
      }
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
