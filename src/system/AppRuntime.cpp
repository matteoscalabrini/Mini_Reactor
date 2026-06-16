/*
 * AppRuntime.cpp — device assembly + bring-up/scheduler for the bioreactor.
 *
 * Builds each device/controller's Config from app_config.hpp, defines the
 * long-lived global objects, runs the boot sequence (begin), and a non-blocking
 * cooperative scheduler (tick): WiFi FSM, thermal PID, run timer, web/WS update,
 * and periodic SD logging — no blocking delays, so the dashboard stays live.
 */

#include "system/AppRuntime.hpp"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <Wire.h>

#include "app_config.hpp"
#include "control/Reactor.hpp"
#include "control/ThermalController.hpp"
#include "heater/Heater.hpp"
#include "motor/DrvStatus.hpp"
#include "motor/Tmc2209Motor.hpp"
#include "system/AlarmTracker.hpp"
#include "net/WebInterface.hpp"
#include "net/WifiManager.hpp"
#include "power/Husb238.hpp"
#include "sensor/Ds18b20.hpp"
#include "sensor/Thermistor.hpp"
#include "storage/SdLogger.hpp"

namespace AppRuntime {
namespace {

// ─── CONFIG BUILDERS ──────────────────────────────────────────────────────────

Tmc2209Motor::Config makeMotorConfig() {
  Tmc2209Motor::Config c;
  c.serial = &Serial1;
  c.pinUartTx = AppConfig::Motor::kUartTxPin;
  c.pinUartRx = AppConfig::Motor::kUartRxPin;
  c.pinEnable = AppConfig::Motor::kEnablePin;
  c.pinStep = AppConfig::Motor::kStepPin;
  c.pinDir = AppConfig::Motor::kDirPin;
  c.pinDiag = AppConfig::Motor::kDiagPin;
  c.pinIndex = AppConfig::Motor::kIndexPin;
  c.rSenseOhms = AppConfig::Motor::kRSenseOhms;
  c.address = AppConfig::Motor::kAddress;
  c.uartBaud = AppConfig::Motor::kUartBaud;
  c.currentMilliamps = AppConfig::Motor::kCurrentMilliamps;
  c.microsteps = AppConfig::Motor::kMicrosteps;
  c.stepsPerRev = AppConfig::Motor::kStepsPerRev;
  c.topSpeedMicrostepHz = AppConfig::Motor::kSpinMicrostepHz;
  return c;
}

Heater::Config makeHeaterConfig() {
  Heater::Config c;
  c.gatePin = AppConfig::Heater::kGatePin;
  c.ledcChannel = AppConfig::Heater::kLedcChannel;
  c.freqHz = AppConfig::Heater::kFreqHz;
  c.resBits = AppConfig::Heater::kResBits;
  return c;
}

Ds18b20::Config makeDs18Config() {
  Ds18b20::Config c;
  c.pin = AppConfig::Sense::kLiquidOneWirePin;
  return c;
}

Thermistor::Config makeThermistorConfig() {  // heater NTC safety probe
  Thermistor::Config c;
  c.adcPin = AppConfig::Sense::kHeaterNtcAdcPin;
  c.seriesOhms = AppConfig::Thermal::kNtcSeriesOhms;
  c.r0Ohms = AppConfig::Thermal::kNtcR0Ohms;
  c.beta = AppConfig::Thermal::kNtcBeta;
  c.t0C = AppConfig::Thermal::kNtcT0C;
  c.vSupplyMv = AppConfig::Thermal::kVSupplyMv;
  c.minPlausibleC = AppConfig::Thermal::kNtcMinPlausibleC;
  c.maxPlausibleC = AppConfig::Thermal::kNtcMaxPlausibleC;
  return c;
}

ThermalController::Config makeThermalConfig() {
  ThermalController::Config c;
  c.kp = AppConfig::Thermal::kPidKp;
  c.ki = AppConfig::Thermal::kPidKi;
  c.kd = AppConfig::Thermal::kPidKd;
  c.dutyMin = AppConfig::Thermal::kDutyMin;
  c.dutyMax = AppConfig::Thermal::kDutyMax;
  c.defaultSetpointC = AppConfig::Thermal::kDefaultSetpointC;
  c.heaterSafetyMaxC = AppConfig::Thermal::kHeaterSafetyMaxC;
  c.processMaxC = AppConfig::Thermal::kProcessMaxC;
  c.safetyCheckMs = AppConfig::Thermal::kSafetyCheckMs;
  return c;
}

Reactor::Config makeReactorConfig() {
  Reactor::Config c;
  c.defaultRpm = AppConfig::Process::kDefaultRpm;
  c.minRpm = AppConfig::Process::kMinRpm;
  c.maxRpm = AppConfig::Process::kMaxRpm;
  c.defaultSetpointC = AppConfig::Thermal::kDefaultSetpointC;
  c.defaultDurationMin = AppConfig::Process::kDefaultDurationMin;
  c.defaultDiscCurrentMa = AppConfig::Motor::kCurrentMilliamps;
  c.defaultDiscMicrosteps = AppConfig::Motor::kMicrosteps;
  c.defaultDiscReverse = false;
  c.prefsNamespace = AppConfig::Process::kPrefsNamespace;
  return c;
}

SdLogger::Config makeSdLoggerConfig() {
  SdLogger::Config c;
  c.pinSck = AppConfig::Spi::kSckPin;
  c.pinMiso = AppConfig::Spi::kMisoPin;
  c.pinMosi = AppConfig::Spi::kMosiPin;
  c.pinCs = AppConfig::Spi::kCsPin;
  c.pinCardDetect = AppConfig::Sd::kCardDetectPin;
  c.freqHz = AppConfig::Sd::kFreqHz;
  c.logPath = AppConfig::Sd::kLogPath;
  c.logHeader = AppConfig::Sd::kLogHeader;
  return c;
}

WifiManager::Config makeWifiConfig() {
  WifiManager::Config c;
  c.hostname = AppConfig::Wifi::kHostname;
  c.apSsid = AppConfig::Wifi::kApSsid;
  c.apPassword = AppConfig::Wifi::kApPassword;
  c.connectTimeoutMs = AppConfig::Wifi::kConnectTimeoutMs;
  c.reconnectIntervalMs = AppConfig::Wifi::kReconnectIntervalMs;
  c.apFallbackDelayMs = AppConfig::Wifi::kApFallbackDelayMs;
  c.maxScanResults = AppConfig::Wifi::kMaxScanResults;
  c.prefsNamespace = AppConfig::Wifi::kPrefsNamespace;
  c.prefsSsidKey = AppConfig::Wifi::kPrefsSsidKey;
  c.prefsPassKey = AppConfig::Wifi::kPrefsPassKey;
  return c;
}

WebInterface::Config makeWebConfig() {
  WebInterface::Config c;
  c.port = AppConfig::Web::kPort;
  c.wsPushPeriodMs = AppConfig::Web::kWsPushPeriodMs;
  return c;
}

// ─── GLOBAL DEVICE / CONTROLLER OBJECTS ───────────────────────────────────────
// Construction order matters: dependencies first.

Husb238 g_pd(Wire, AppConfig::Pd::kAddress);
Tmc2209Motor g_motor(makeMotorConfig());
Heater g_heater(makeHeaterConfig());
Ds18b20 g_liquid(makeDs18Config());
Thermistor g_heaterNtc(makeThermistorConfig());
ThermalController g_thermal(g_liquid, g_heaterNtc, g_heater, makeThermalConfig());
Reactor g_reactor(g_thermal, g_motor, makeReactorConfig());
SdLogger g_sd(makeSdLoggerConfig());
WifiManager g_wifi(makeWifiConfig());
WebInterface g_web(g_reactor, g_wifi, g_sd, makeWebConfig());

// ─── HELPERS ──────────────────────────────────────────────────────────────────

const char* voltageCodeStr(Husb238::VoltageCode v) {
  switch (v) {
    case Husb238::VoltageCode::Unattached: return "unattached";
    case Husb238::VoltageCode::V5:  return "5V";
    case Husb238::VoltageCode::V9:  return "9V";
    case Husb238::VoltageCode::V12: return "12V";
    case Husb238::VoltageCode::V15: return "15V";
    case Husb238::VoltageCode::V18: return "18V";
    case Husb238::VoltageCode::V20: return "20V";
    default: return "?";
  }
}

void requestPd() {
  Serial.println(F("\n[HUSB238] Requesting 12V PD profile ..."));
  if (!g_pd.probe()) {
    Serial.printf("  not found (%s) — running on USB-default 5V\n",
                  g_pd.lastErrorString());
    return;
  }
  g_pd.requestProfile(AppConfig::Pd::kRequestProfile);
  delay(300);
  Husb238::Status s;
  if (g_pd.refreshStatus(s)) {
    Serial.printf("  negotiated VBUS = %s\n", voltageCodeStr(s.voltage));
  }
}

// Build the nested /api/v1 telemetry object (spec §4). P1+P2 fields;
// pid terms and the NTC raw/calibration block are added by later phases.
String buildStatusJson() {
  JsonDocument doc;
  const ReactorTelemetry t = g_reactor.telemetry();

  doc["apiVersion"] = "1.0";
  doc["uptimeSec"] = millis() / 1000UL;

  JsonObject sys = doc["system"].to<JsonObject>();
  sys["firmware"] = AppConfig::kFirmwareVersion;
  sys["freeHeap"] = ESP.getFreeHeap();
  Husb238::Status ps;
  sys["vbus"] = g_pd.refreshStatus(ps) ? voltageCodeStr(ps.voltage) : "?";
  sys["sdMounted"] = g_sd.mounted();

  JsonObject th = doc["thermal"].to<JsonObject>();
  if (t.sensorFault) {
    th["tempC"] = nullptr;
    th["errorC"] = nullptr;
  } else {
    th["tempC"] = roundf(t.liquidTempC * 100) / 100.0f;
    th["errorC"] = roundf((t.setpointC - t.liquidTempC) * 100) / 100.0f;
  }
  th["setpointC"] = t.setpointC;
  th["heaterPct"] = roundf(t.heaterDutyPct * 10) / 10.0f;
  th["fault"] = t.sensorFault;
  JsonObject safety = th["safety"].to<JsonObject>();
  safety["tripped"] = t.safetyTripped;
  if (isnan(t.heaterTempC)) safety["heaterTempC"] = nullptr;
  else safety["heaterTempC"] = roundf(t.heaterTempC * 10) / 10.0f;
  safety["heaterMaxC"] = AppConfig::Thermal::kHeaterSafetyMaxC;
  safety["processMaxC"] = AppConfig::Thermal::kProcessMaxC;
  JsonObject probe = safety["probe"].to<JsonObject>();
  const ThermistorCalibration& ncal = g_thermal.ntcCalibration();
  probe["adcRaw"] = g_thermal.ntcRawAdc();
  const float nres = g_thermal.ntcResistanceOhms();
  if (isnan(nres)) probe["resistanceOhms"] = nullptr;
  else probe["resistanceOhms"] = roundf(nres);
  probe["calibrated"] = ncal.calibrated();
  probe["method"] = ncal.methodStr();

  JsonObject pid = th["pid"].to<JsonObject>();
  pid["kp"] = g_thermal.kp();
  pid["ki"] = g_thermal.ki();
  pid["kd"] = g_thermal.kd();
  pid["p"] = g_thermal.pTerm();
  pid["i"] = g_thermal.iTerm();
  pid["d"] = g_thermal.dTerm();
  pid["out"] = roundf(g_thermal.outputDuty() * 1000) / 1000.0f;
  pid["mode"] = g_thermal.modeStr();
  JsonObject at = pid["autotune"].to<JsonObject>();
  at["active"] = g_thermal.autotuneActive();
  at["progress"] = g_thermal.autotuneProgress();
  const char* ares = g_thermal.autotuneResult();
  if (ares) at["result"] = ares;
  else at["result"] = nullptr;

  JsonObject disc = doc["disc"].to<JsonObject>();
  disc["running"] = t.running;
  disc["rpm"] = t.rpm;
  disc["rpmSetpoint"] = g_reactor.rpmSetpoint();
  disc["direction"] = g_motor.reversed() ? "ccw" : "cw";
  disc["currentMa"] = g_motor.currentMilliamps();
  disc["microsteps"] = g_motor.microstepsValue();
  disc["enabled"] = g_motor.enabledState();
  const bool drvConnected = g_motor.connected();
  const DrvStatusFlags dflags = drvConnected ? g_motor.driverFlags() : DrvStatusFlags{};
  // load: StallGuard, only meaningful while running + linked.
  if (t.running && drvConnected) disc["load"] = g_motor.stallGuardResult();
  else disc["load"] = nullptr;
  JsonObject drv = disc["driver"].to<JsonObject>();
  char ver[8];
  snprintf(ver, sizeof(ver), "0x%02X", g_motor.version());
  drv["version"] = ver;
  drv["connected"] = drvConnected;
  JsonObject dfl = drv["flags"].to<JsonObject>();
  dfl["otpw"] = dflags.otpw;
  dfl["ot"] = dflags.ot;
  dfl["stall"] = dflags.stall;
  dfl["openLoadA"] = dflags.openLoadA;
  dfl["openLoadB"] = dflags.openLoadB;
  dfl["shortA"] = dflags.shortA;
  dfl["shortB"] = dflags.shortB;

  JsonObject run = doc["run"].to<JsonObject>();
  run["active"] = t.running;
  run["elapsedSec"] = t.elapsedSec;
  if (t.running && t.durationMin > 0) run["remainingSec"] = t.remainingSec;
  else run["remainingSec"] = nullptr;
  run["durationMin"] = t.durationMin;

  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["mode"] = g_wifi.apActive() ? "ap" : "sta";
  wifi["connected"] = g_wifi.staConnected();
  wifi["ssid"] = WiFi.SSID();
  wifi["ip"] = g_wifi.ipAddress();
  if (g_wifi.staConnected()) wifi["rssi"] = WiFi.RSSI();
  else wifi["rssi"] = nullptr;

  JsonObject storage = doc["storage"].to<JsonObject>();
  storage["sdMounted"] = g_sd.mounted();
  storage["logBytes"] = nullptr;  // accurate size arrives with the SD-mgmt phase
  storage["logging"] = g_sd.mounted();

  static AlarmTracker s_alarms;
  s_alarms.beginFrame(millis() / 1000UL);
  if (t.sensorFault) s_alarms.add("sensor_fault", "warn");
  if (isnan(t.heaterTempC)) s_alarms.add("heater_probe_fault", "warn");
  if (t.safetyTripped) s_alarms.add("safety_tripped", "critical");
  if (drvConnected) {
    if (dflags.ot) s_alarms.add("driver_ot", "critical");
    else if (dflags.otpw) s_alarms.add("driver_otpw", "warn");
    if (dflags.stall) s_alarms.add("driver_stall", "warn");
    if (dflags.openLoadA || dflags.openLoadB) s_alarms.add("driver_open_load", "warn");
  }
  s_alarms.endFrame();
  JsonArray alarms = doc["alarms"].to<JsonArray>();
  for (int i = 0; i < s_alarms.size(); ++i) {
    JsonObject a = alarms.add<JsonObject>();
    a["code"] = s_alarms[i].code;
    a["severity"] = s_alarms[i].severity;
    a["since"] = s_alarms[i].sinceSec;
  }

  String out;
  serializeJson(doc, out);
  return out;
}

String buildCalJson() {
  JsonDocument doc;
  const ThermistorCalibration& c = g_thermal.ntcCalibration();
  doc["method"] = c.methodStr();
  doc["calibrated"] = c.calibrated();
  JsonArray pts = doc["points"].to<JsonArray>();
  for (int i = 0; i < c.pointCount(); i++) {
    JsonObject p = pts.add<JsonObject>();
    p["referenceC"] = c.pointRefC(i);
    p["resistanceOhms"] = roundf(c.pointR(i));
  }
  String out; serializeJson(doc, out); return out;
}

}  // namespace

// ─── ENTRY POINTS ─────────────────────────────────────────────────────────────

void begin() {
  Serial.begin(AppConfig::kSerialBaud);
  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < AppConfig::kSerialStartupDelayMs) {
  }
  Serial.println(F("\n=== Bioreactor Module — fermentation firmware ==="));

  // Power: I2C up, negotiate 12V for the motor/heater rail.
  Wire.begin(AppConfig::I2c::kSdaPin, AppConfig::I2c::kSclPin,
             AppConfig::I2c::kClockHz);
  requestPd();

  // Storage.
  g_sd.begin();
  g_sd.checkAndReport(Serial);

  // Control hardware: thermistor + heater (PID), then the motor.
  g_thermal.begin();
  Serial.println(F("\n[TMC2209] Bringing up UART driver ..."));
  if (g_motor.begin()) {
    Serial.printf("  connected, version 0x%02X\n", g_motor.version());
  } else {
    Serial.printf("  NOT responding (version 0x%02X) — check +12V and UART\n",
                  g_motor.version());
  }

  // Process state (loads persisted setpoints) — does not auto-start a run.
  g_reactor.begin();

  // Networking + web UI.
  g_wifi.begin();
  g_web.begin();

  Serial.println(F("\n[RUN] reactor idle — control via browser."));
}

void tick() {
  g_wifi.poll();
  g_thermal.update();   // PID, internally gated to its sample period
  g_reactor.update();   // run timer

  // Rebuild status/scan JSON at ~10 Hz; apply commands + push WS every loop.
  static uint32_t lastStatusMs = 0;
  static String statusJson = "{}";
  static String scanJson = "{\"scanning\":false,\"networks\":[]}";
  const uint32_t now = millis();
  if (now - lastStatusMs >= 100) {
    lastStatusMs = now;
    statusJson = buildStatusJson();
    scanJson = g_wifi.scanJson();
    g_web.cacheCalJson(buildCalJson());
  }
  g_web.update(statusJson, scanJson);

  // Periodic SD logging.
  static uint32_t lastLogMs = 0;
  if (g_sd.mounted() && now - lastLogMs >= AppConfig::Timing::kLogPeriodMs) {
    lastLogMs = now;
    g_sd.appendLine(g_reactor.csvRow());
  }

  delay(2);  // yield to WiFi/AsyncTCP tasks
}

}  // namespace AppRuntime
