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
#include <Wire.h>

#include "app_config.hpp"
#include "control/Reactor.hpp"
#include "control/ThermalController.hpp"
#include "heater/Heater.hpp"
#include "motor/Tmc2209Motor.hpp"
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
  c.defaultMotorPercent = AppConfig::Process::kDefaultMotorPercent;
  c.defaultSetpointC = AppConfig::Thermal::kDefaultSetpointC;
  c.defaultDurationMin = AppConfig::Process::kDefaultDurationMin;
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

// Build the full status JSON consumed by GET /api/status and the WS feed.
String buildStatusJson() {
  JsonDocument doc;

  const ReactorTelemetry t = g_reactor.telemetry();
  JsonObject r = doc["reactor"].to<JsonObject>();
  r["running"] = t.running;
  if (t.sensorFault) {
    r["tempC"] = nullptr;  // liquid (process) probe
  } else {
    r["tempC"] = roundf(t.liquidTempC * 100) / 100.0f;
  }
  if (isnan(t.heaterTempC)) {
    r["heaterC"] = nullptr;  // heater NTC safety probe
  } else {
    r["heaterC"] = roundf(t.heaterTempC * 10) / 10.0f;
  }
  r["setpointC"] = t.setpointC;
  r["heaterPct"] = roundf(t.heaterDutyPct * 10) / 10.0f;
  r["motorPct"] = t.motorPercent;
  r["fault"] = t.sensorFault;
  r["safety"] = t.safetyTripped;
  r["elapsedSec"] = t.elapsedSec;
  r["remainingSec"] = t.remainingSec;
  r["durationMin"] = t.durationMin;

  JsonObject w = doc["wifi"].to<JsonObject>();
  w["connected"] = g_wifi.staConnected();
  w["ap"] = g_wifi.apActive();
  w["ip"] = g_wifi.ipAddress();

  Husb238::Status s;
  doc["vbus"] = g_pd.refreshStatus(s) ? voltageCodeStr(s.voltage) : "?";
  doc["sdMounted"] = g_sd.mounted();
  doc["uptimeSec"] = millis() / 1000UL;

  String out;
  serializeJson(doc, out);
  return out;
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
