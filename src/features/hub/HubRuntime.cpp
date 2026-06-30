#include "features/hub/HubRuntime.hpp"
#include <Arduino.h>
#include <esp_sleep.h>
#include "app_config.hpp"
#include "features/hub/power/Axp2101.hpp"
#include "features/hub/display/HubDisplay.hpp"
#include "features/hub/touch/Cst9217.hpp"
#include "features/hub/sensor/Qmi8658.hpp"
#include "features/hub/rtc/Pcf85063.hpp"
#include "features/hub/io/Tca9554.hpp"
#include "features/hub/audio/Es8311.hpp"
#include "features/hub/audio/Es7210.hpp"
#include "features/hub/sleep/HubSleepLogic.hpp"
#include "features/hub/ui/BringupScreen.hpp"
#include <lvgl.h>

namespace HubRuntime {

static Qmi8658::Status g_lastImuStatus = {};

static Axp2101    g_axp{Wire, AppConfig::Hub::kAxp2101Address};
static HubDisplay g_display;
static Cst9217    g_touch{Wire, AppConfig::Hub::kCst9217Address};
static Qmi8658    g_imu{Wire, AppConfig::Hub::kQmi8658Address};
static Pcf85063   g_rtc{Wire, AppConfig::Hub::kPcf85063Address};
static Tca9554    g_io{Wire, AppConfig::Hub::kTca9554Address};
static Es8311     g_codec{Wire, AppConfig::Hub::kEs8311Address};
static Es7210     g_mic{Wire, AppConfig::Hub::kEs7210Address};

static uint32_t g_lastActivityMs = 0;  // updated on every touch event; used by sleep FSM

static void enterDeepSleep() {
  Serial.println("[HUB] entering deep sleep");
  Serial.flush();  // Task 9 M2: flush before I2C/sleep calls
  g_display.enterSleep();
  if (AppConfig::HubFeatures::kEnableImu) {
    Qmi8658::WakeOnMotionConfig wom;
    wom.thresholdMg     = AppConfig::HubMotionWake::kThresholdMg;
    wom.blankingSamples = AppConfig::HubMotionWake::kBlankingSamples;
    wom.accelOdrMilliHz = AppConfig::HubMotionWake::kAccelOdrMilliHz;
    wom.accelRangeG     = AppConfig::HubMotionWake::kAccelRangeG;
    g_imu.enterWakeOnMotionMode(wom);
  }
  g_axp.disableAdcForSleep();
  uint64_t mask = 0;
  if (AppConfig::HubSleep::kWakeOnTouchInt)
    mask |= (1ULL << AppConfig::PinoutHub::kHubTouchInt);
  if (AppConfig::HubFeatures::kEnableImu && AppConfig::HubSleep::kWakeOnImuInt2)
    mask |= (1ULL << AppConfig::PinoutHub::kImuInt2);
  // Task 9 M5: guard against zero wake mask — skip deep sleep if no source configured
  if (mask == 0) {
    Serial.println("[HUB] deep sleep skipped — no wake source configured");
    return;
  }
  esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);
  esp_deep_sleep_start();
}

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
  // Step 1: AXP2101 first — brings all power rails up; always initializes
  if (g_axp.begin(AppConfig::PinoutHub::kI2cSda, AppConfig::PinoutHub::kI2cScl,
                  AppConfig::Hub::kI2cClockHz)) {
    Serial.println("[HUB] axp2101: enabled");
  } else {
    Serial.printf("[HUB] axp2101: FAULT (%s)\n", g_axp.lastErrorString());
  }

  // Step 2: Display
  if (AppConfig::HubFeatures::kEnableDisplay) {
    if (g_display.begin()) {
      BringupScreen::create();
      Serial.println("[HUB] display: enabled");
    } else {
      Serial.println("[HUB] display: enabled (hardware absent)");
    }
  } else {
    if (g_display.isReady()) g_display.enterSleep();  // power down panel if it came up
    Serial.println("[HUB] display: disabled");
  }

  // Step 3: Touch
  if (AppConfig::HubFeatures::kEnableTouch) {
    const bool touchOk = g_touch.begin();
    const bool indevReady = AppConfig::HubFeatures::kEnableDisplay && g_display.isReady();
    if (indevReady) {
      static lv_indev_drv_t d;
      lv_indev_drv_init(&d);
      d.type    = LV_INDEV_TYPE_POINTER;
      d.read_cb = hubTouchRead;
      lv_indev_drv_register(&d);
      Serial.printf("[HUB] touch: %s\n", touchOk ? "enabled" : "enabled (hardware absent)");
    } else {
      Serial.printf("[HUB] touch: %s (no indev — display disabled)\n",
                    touchOk ? "enabled" : "enabled (hardware absent)");
    }
  } else {
    g_touch.begin();       // reset + enter cmd mode so enterSleep is well-defined
    g_touch.enterSleep();  // power down the controller
    Serial.println("[HUB] touch: disabled (powered down)");
  }

  // Step 4: IMU
  if (AppConfig::HubFeatures::kEnableImu) {
    Qmi8658::Config imuCfg;
    imuCfg.featureEnabled             = true;
    imuCfg.pinI2cSda                  = AppConfig::PinoutHub::kI2cSda;
    imuCfg.pinI2cScl                  = AppConfig::PinoutHub::kI2cScl;
    imuCfg.i2cClockHz                 = AppConfig::Hub::kI2cClockHz;
    imuCfg.address                    = AppConfig::Hub::kQmi8658Address;
    imuCfg.enableAddressFallbackProbe = true;
    imuCfg.accelRangeG                = AppConfig::HubImu::kAccelRangeG;
    imuCfg.accelOdrMilliHz            = AppConfig::HubImu::kAccelOdrMilliHz;
    imuCfg.gyroRangeDps               = AppConfig::HubImu::kGyroRangeDps;
    imuCfg.gyroOdrMilliHz             = AppConfig::HubImu::kGyroOdrMilliHz;
    imuCfg.enableAccelLpf             = AppConfig::HubImu::kEnableAccelLpf;
    imuCfg.enableGyroLpf              = AppConfig::HubImu::kEnableGyroLpf;
    imuCfg.enableTapDetection         = false;  // Task 5 M2: tap engine not used
    const bool imuOk = g_imu.begin(imuCfg);
    Serial.printf("[HUB] imu: %s\n", imuOk ? "enabled" : "enabled (hardware absent)");
  } else {
    Serial.println("[HUB] imu: disabled");  // left in default low-power; no high-ODR config
  }

  // Step 5: RTC (self-powered; only polling is skipped when disabled)
  if (AppConfig::HubFeatures::kEnableRtc) {
    const bool rtcOk = g_rtc.begin();
    Serial.printf("[HUB] rtc: %s\n", rtcOk ? "enabled" : "enabled (hardware absent)");
  } else {
    Serial.println("[HUB] rtc: disabled");
  }

  // Step 6: IO expander
  if (AppConfig::HubFeatures::kEnableIoExpander) {
    const bool ioOk = g_io.begin();
    Serial.printf("[HUB] ioexp: %s\n", ioOk ? "enabled" : "enabled (hardware absent)");
  } else {
    Serial.println("[HUB] ioexp: disabled");
  }

  // Step 7: Audio (always probed; powered down when disabled)
  if (AppConfig::HubFeatures::kEnableAudio) {
    g_codec.begin(); g_mic.begin();
    Serial.println("[HUB] audio: enabled");
  } else {
    bool c = g_codec.begin(), m = g_mic.begin();
    if (c) g_codec.powerDown();
    if (m) g_mic.powerDown();
    pinMode(AppConfig::PinoutHub::kHubAudioAmpEnable, OUTPUT);
    digitalWrite(AppConfig::PinoutHub::kHubAudioAmpEnable, LOW);
    Serial.printf("[HUB] es8311: %s, es7210: %s (powered down)\n",
                  c ? "present" : "absent", m ? "present" : "absent");
  }

  // Step 8: Sleep — armed last so idle timer starts after all hardware is up
  if (AppConfig::HubFeatures::kEnableSleep) {
    g_lastActivityMs = millis();
    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    Serial.printf("[HUB] sleep: enabled — wakeup cause %d\n", (int)cause);
  } else {
    Serial.println("[HUB] sleep: disabled");
  }
}

void tick() {
  const uint32_t now = millis();

  // Display pump every loop (LVGL; esp_timer drives lv_tick_inc)
  if (AppConfig::HubFeatures::kEnableDisplay) {
    g_display.tick();
  }

  // Touch polling at kTouchPollMs cadence
  if (AppConfig::HubFeatures::kEnableTouch) {
    static uint32_t lastTouch = 0;
    if (now - lastTouch >= AppConfig::Hub::kTouchPollMs) {
      lastTouch = now;
      Cst9217::State ts;
      g_touch.refresh(ts);
      if (ts.pointCount > 0) g_lastActivityMs = now;  // activity detected
    }
  }

  // IMU polling at kMotionPollMs cadence
  if (AppConfig::HubFeatures::kEnableImu) {
    static uint32_t lastMotion = 0;
    if (now - lastMotion >= AppConfig::Hub::kMotionPollMs) {
      lastMotion = now;
      Qmi8658::Status st;
      if (g_imu.poll(st)) {
        g_lastImuStatus = st;
        Serial.printf("[HUB] imu ax=%.3f ay=%.3f az=%.3f gx=%.1f gy=%.1f gz=%.1f\n",
                      st.accelXg, st.accelYg, st.accelZg,
                      st.gyroXdps, st.gyroYdps, st.gyroZdps);
      }
    }
  }

  // Task 9 M3: PMIC telemetry + RTC + IO at kPollMs cadence — BEFORE sleep FSM
  // so shouldEnterDeepSleep reads fresh vbusPresent from g_axp.state()
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
    if (AppConfig::HubFeatures::kEnableRtc) {
      Pcf85063::DateTime dt;
      if (g_rtc.refresh(dt)) {
        Serial.printf("[HUB] rtc %02u:%02u:%02u\n", dt.hours, dt.minutes, dt.seconds);
      }
    }
    if (AppConfig::HubFeatures::kEnableIoExpander) {
      Tca9554::State ioState;  // Task 7 Minor: named ioState to avoid shadowing Axp2101::State s
      if (g_io.refresh(ioState)) {
        Serial.printf("[HUB] ioexp input=0x%02X\n", ioState.input);
      }
    }
  }

  // Bringup screen refresh at 250 ms cadence (display must be enabled)
  if (AppConfig::HubFeatures::kEnableDisplay) {
    static uint32_t lastUi = 0;
    if (now - lastUi >= 250) {
      lastUi = now;
      BringupScreen::Snapshot snap = {};
      const auto& axp = g_axp.state();
      snap.ics[0] = {"AXP2101",  axp.present ? 1u : 2u};
      snap.ics[1] = {"QMI8658",  !AppConfig::HubFeatures::kEnableImu
                                  ? 0u : g_imu.isReady()           ? 1u : 2u};
      snap.ics[2] = {"PCF85063", !AppConfig::HubFeatures::kEnableRtc
                                  ? 0u : g_rtc.state().running      ? 1u : 2u};
      const auto& ts = g_touch.state();
      snap.ics[3] = {"CST9217",  !AppConfig::HubFeatures::kEnableTouch
                                  ? 0u : ts.present                 ? 1u : 2u};
      snap.ics[4] = {"TCA9554",  !AppConfig::HubFeatures::kEnableIoExpander
                                  ? 0u : g_io.state().present       ? 1u : 2u};
      snap.ics[5] = {"ES8311",   !AppConfig::HubFeatures::kEnableAudio
                                  ? 0u : g_codec.present()          ? 1u : 2u};
      snap.ics[6] = {"ES7210",   !AppConfig::HubFeatures::kEnableAudio
                                  ? 0u : g_mic.present()            ? 1u : 2u};
      snap.batteryPercent = axp.batteryPercent;
      snap.batteryMv      = axp.batteryVoltageMv;
      snap.charging       = axp.charging;
      snap.vbus           = axp.vbusPresent;
      snap.touchPressed   = ts.pointCount > 0;
      if (snap.touchPressed) { snap.touchX = ts.points[0].x; snap.touchY = ts.points[0].y; }
      snap.accelX         = g_lastImuStatus.accelXg;
      snap.accelY         = g_lastImuStatus.accelYg;
      snap.accelZ         = g_lastImuStatus.accelZg;
      const auto& dt      = g_rtc.state();
      snap.rtcHours       = dt.hours;
      snap.rtcMinutes     = dt.minutes;
      snap.rtcSeconds     = dt.seconds;
      snap.freeHeap       = ESP.getFreeHeap();
      snap.freePsram      = ESP.getFreePsram();
      snap.firmware       = AppConfig::kFirmwareVersion;
      BringupScreen::update(snap);
    }
  }

  // Deep-sleep FSM (gated on kEnableSleep) — runs after PMIC telemetry for fresh vbusPresent
  if (AppConfig::HubFeatures::kEnableSleep) {
    hubsleep::SleepInputs si;
    si.externalPowerPresent = g_axp.state().vbusPresent;
    si.touchActive          = g_touch.state().pointCount > 0;
    si.idleMs               = now - g_lastActivityMs;
    si.idleThresholdMs      = AppConfig::HubSleep::kDeepSleepAfterMs;
    if (hubsleep::shouldEnterDeepSleep(si)) enterDeepSleep();
  }
}

}  // namespace HubRuntime
