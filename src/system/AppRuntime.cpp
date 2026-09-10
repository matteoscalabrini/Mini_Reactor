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
#include <esp_heap_caps.h>  // DIAG: heap-leak instrumentation (system.* fields)

#include "app_config.hpp"
#include "features/control/GainSchedule.hpp"
#include "features/control/Reactor.hpp"
#include "features/control/ThermalController.hpp"
#include "features/heater/Heater.hpp"
#include "features/motor/DrvStatus.hpp"
#include "features/motor/Tmc2209Motor.hpp"
#include "system/AlarmTracker.hpp"
#include "net/EspNowResponder.hpp"
#include "net/WebInterface.hpp"
#include "net/WifiManager.hpp"
#include "power/Husb238.hpp"
#include "features/sensor/Ds18b20.hpp"
#include "features/sensor/Thermistor.hpp"
#include "storage/SdLogger.hpp"
#include "features/ui/Display.hpp"
#include "features/ui/InputManager.hpp"
#include "features/ui/UiController.hpp"

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
  c.adaptiveEnabled = AppConfig::Features::kEnableAdaptiveThermal;
  c.bandC = AppConfig::Thermal::kApproachBandC;
  c.holdDutyCap = AppConfig::Thermal::kHoldDutyCap;
  c.heatKp = AppConfig::Thermal::kHeatKp; c.heatKi = AppConfig::Thermal::kHeatKi; c.heatKd = AppConfig::Thermal::kHeatKd;
  c.holdKp = AppConfig::Thermal::kHoldKp; c.holdKi = AppConfig::Thermal::kHoldKi; c.holdKd = AppConfig::Thermal::kHoldKd;
  c.tuneMarginC = AppConfig::Thermal::kTuneMarginC;
  c.heatKpScale = AppConfig::Thermal::kHeatKpScale;
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
  c.logHeader = AppConfig::Sd::kLogHeader;
  c.logIntervalMs = AppConfig::Timing::kLogPeriodMs;  // default; runtime-settable via API
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
  c.apRetryIntervalMs = AppConfig::Wifi::kApRetryIntervalMs;
  c.txWatchdogEnabled = AppConfig::Wifi::kEnableTxWatchdog;
  c.txStallMs = AppConfig::Wifi::kTxStallMs;
  c.txRecoverBackoffMs = AppConfig::Wifi::kTxRecoverBackoffMs;
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
EspNowResponder g_espnow(g_reactor, g_web);

struct ReactorControlAdapter : ui::ReactorControl {
  Reactor& r;
  explicit ReactorControlAdapter(Reactor& reactor) : r(reactor) {}
  void startRun(float t, float rpm) override { r.start(t, rpm, 0); }
  void stopRun() override { r.stop(); }
  void setTargetC(float c) override { r.setTargetC(c); }
  void setRpm(float rpm) override { r.setRpm(rpm); }
  void setMotorPaused(bool on) override { r.setMotorPaused(on); }
  void setFullHold(bool on) override { r.setFullHold(on); }
};

ui::UiController::Config makeUiConfig() {
  ui::UiController::Config c;
  c.minRpm = AppConfig::Process::kMinRpm;
  c.maxRpm = AppConfig::Process::kMaxRpm;
  c.minTargetC = AppConfig::Ui::kTargetMinC;
  c.maxTargetC = AppConfig::Ui::kTargetMaxC;
  c.targetStepC = AppConfig::Ui::kTargetStepC;
  c.rpmStep = AppConfig::Ui::kRpmStep;
  return c;
}

ReactorControlAdapter g_uiControl(g_reactor);
ui::UiController g_ui(g_uiControl, makeUiConfig());
ui::Display g_display(AppConfig::Ui::kDisplayI2cAddr, AppConfig::Ui::kDisplaySclPin,
                      AppConfig::Ui::kDisplaySdaPin, AppConfig::Ui::kDisplayBusClockHz);
ui::InputManager g_input(
    {AppConfig::Ui::kEncAPin, AppConfig::Ui::kEncBPin, AppConfig::Ui::kEncSwPin,
     AppConfig::Ui::kBtn1Pin, AppConfig::Ui::kBtn2Pin, AppConfig::Ui::kBtn3Pin},
    {});

// ─── HELPERS ──────────────────────────────────────────────────────────────────

ui::ReactorSnapshot buildUiSnapshot() {
  const ReactorTelemetry t = g_reactor.telemetry();
  ui::ReactorSnapshot s;
  s.running = t.running;
  s.motorPaused = t.motorPaused;
  s.fullHold = t.fullHold;
  s.sensorFault = t.sensorFault;
  s.safetyTripped = t.safetyTripped;
  s.liquidTempC = t.liquidTempC;
  s.setpointC = t.setpointC;
  s.rpm = t.rpm;
  s.rpmSetpoint = g_reactor.rpmSetpoint();
  s.heaterDutyPct = t.heaterDutyPct;
  s.elapsedSec = t.elapsedSec;
  s.wifiConnected = g_wifi.staConnected();
  // WiFi strings + RSSI cached at the OLED redraw rate: this runs every tick
  // (~2-3 ms) and WiFi.SSID() / ipAddress() each allocate a String while
  // RSSI() is a driver call — pure heap churn at ~400 Hz for a 4 Hz panel.
  static String ssid, ip;       // keep backing storage alive for the const char*
  static int8_t rssi = 0;
  static uint32_t lastNetMs = 0;
  if (millis() - lastNetMs >= AppConfig::Ui::kRedrawIntervalMs) {
    lastNetMs = millis();
    ssid = WiFi.SSID(); ip = g_wifi.ipAddress();
    rssi = s.wifiConnected ? WiFi.RSSI() : 0;
  }
  s.wifiSsid = ssid.c_str();
  s.ip = ip.c_str();
  s.rssi = rssi;
  s.sdMounted = g_sd.mounted();
  s.firmware = AppConfig::kFirmwareVersion;
  return s;
}

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
  Serial.println(F("\n[HUSB238] USB-PD sink probe ..."));
  if (!g_pd.probe()) {
    Serial.printf("  HUSB238 not found (%s) — running on USB-default 5V\n",
                  g_pd.lastErrorString());
    return;
  }
  // No PD command here: at cold plug the Type-C attach + source-cap exchange is
  // still in flight when boot reaches this point, and a command fired into that
  // handshake collides with it — the source answers with a PD Hard Reset that
  // drops VBUS to 0V (the "PSU reboots on plug" failure). pdReconcile() in the
  // loop negotiates 12V once ATTACH is actually reported.
  Serial.println(F("  found — 12V negotiated from the loop once attach completes"));
}

// Negotiate/hold the 12V contract from the loop, self-gated to kReconcilePeriodMs.
// At most ONE PD action per period, and only while ATTACH is set:
//   - table shows the 12V PDO   → single REQUEST_PD (no caps command paired with it)
//   - table empty (no caps yet) → single GET_SRC_CAP; the request goes next period
//   - caps refreshed but still no 12V PDO → source can't do 12V; stop until re-plug
// GET_SRC_CAP restarts the PD negotiation, so pairing it back-to-back with
// REQUEST_PD makes the two exchanges collide and the source Hard Reset (VBUS→0V).
// Last PD status read by pdReconcile (1 Hz) — the status build reads this cache
// instead of hitting the I2C bus on every rebuild.
Husb238::Status g_pdStatus;
bool g_pdStatusOk = false;

void pdReconcile() {
  static uint32_t lastMs = 0;
  static bool at12 = false;
  static bool capsAsked = false;   // one caps refresh per attach
  static bool no12V = false;       // source advertises caps without a 12V PDO
  const uint32_t now = millis();
  if (now - lastMs < AppConfig::Pd::kReconcilePeriodMs) return;
  lastMs = now;
  g_pdStatusOk = g_pd.refreshStatus(g_pdStatus);
  if (!g_pdStatusOk) return;                             // I2C hiccup; retry next period
  const Husb238::Status& s = g_pdStatus;
  if (!s.attached) {                                     // unplugged: re-arm for next attach
    at12 = false; capsAsked = false; no12V = false;
    return;
  }
  if (s.voltage == Husb238::VoltageCode::V12) {
    if (!at12) { Serial.println(F("[HUSB238] VBUS = 12V negotiated")); at12 = true; }
    return;                                              // at 12V — nothing to do
  }
  at12 = false;                                          // below 12V — reconcile
  if (no12V) return;
  Husb238::SourceCapability cap12;
  if (!g_pd.readSourceCapability(AppConfig::Pd::kRequestProfile, cap12)) return;
  if (cap12.present) {
    g_pd.requestProfile(AppConfig::Pd::kRequestProfile); // select 12V + REQUEST_PD
  } else if (!capsAsked) {
    g_pd.requestSourceCapabilities();                    // populate SRC_PDO table
    capsAsked = true;
  } else {
    Serial.println(F("[HUSB238] source offers no 12V PDO — +12V rail stays off"));
    no12V = true;
  }
}

// TMC2209 diagnostics cached off the UART at Motor::kDiagPeriodMs. The status
// build used to do 4 blocking single-wire UART round-trips per rebuild
// (connected + DRV_STATUS + SG_RESULT + IOIN version); now ONE DRV_STATUS read
// (+ SG_RESULT while running) per period, version read once at boot.
struct MotorDiag {
  bool connected = false;
  uint8_t version = 0;
  DrvStatusFlags flags;
  uint16_t load = 0;   // StallGuard; valid only while running && connected
};
MotorDiag g_motorDiag;

void motorDiagRefresh(uint32_t now) {
  static uint32_t lastMs = 0;
  if (now - lastMs < AppConfig::Motor::kDiagPeriodMs) return;
  lastMs = now;
  g_motorDiag.connected = g_motor.readDiag(g_motorDiag.flags);
  g_motorDiag.load = (g_motorDiag.connected && g_reactor.running()) ? g_motor.stallGuardResult() : 0;
}

// Build the nested /api/v1 telemetry object (spec §4). P1+P2 fields;
// pid terms and the NTC raw/calibration block are added by later phases.
String buildStatusJson() {
  JsonDocument doc;
  const ReactorTelemetry t = g_reactor.telemetry();

  doc["apiVersion"] = "1.0";
  doc["uptimeSec"] = millis() / 1000UL;

  JsonObject feats = doc["features"].to<JsonObject>();
  feats["sdLogging"] = AppConfig::Features::kEnableSdLogging;
  feats["oledUi"] = AppConfig::Features::kEnableOledUi;
  feats["autotune"] = AppConfig::Features::kEnableAutotune;
  feats["espnow"] = AppConfig::Features::kEnableEspNow;

  // ESP-NOW binding state so the web UI can list the paired HUB(s) + Forget.
  JsonObject en = doc["espnow"].to<JsonObject>();
  en["enabled"] = AppConfig::Features::kEnableEspNow;
  en["bound"] = g_espnow.bound();
  JsonArray peers = en["peers"].to<JsonArray>();
  if (g_espnow.bound()) {
    const uint8_t* m = g_espnow.peerMac();
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             m[0], m[1], m[2], m[3], m[4], m[5]);
    JsonObject p = peers.add<JsonObject>();
    p["mac"] = macStr;
    p["channel"] = g_espnow.channel();
    if (g_espnow.peerName()[0]) p["name"] = g_espnow.peerName();
  }

  JsonObject sys = doc["system"].to<JsonObject>();
  sys["firmware"] = AppConfig::kFirmwareVersion;
  sys["freeHeap"] = ESP.getFreeHeap();
  // DIAG (heap-leak hunt): minFreeHeap = lowest-ever free (true-leak floor);
  // largestBlock = biggest contiguous free block (fragmentation signal — if free
  // stays flat but this shrinks, it's fragmentation not a leak); dma pool is the
  // one lwIP/WiFi pbufs draw from, so its floor is what actually kills the radio.
  sys["minFreeHeap"]  = ESP.getMinFreeHeap();
  sys["largestBlock"] = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  sys["freeDma"]      = heap_caps_get_free_size(MALLOC_CAP_DMA);
  sys["minFreeDma"]   = heap_caps_get_minimum_free_size(MALLOC_CAP_DMA);
  sys["vbus"] = g_pdStatusOk ? voltageCodeStr(g_pdStatus.voltage) : "?";  // 1 Hz cache (pdReconcile)
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
  pid["regime"] = g_thermal.regimeStr();
  pid["dutyCeil"] = roundf(g_thermal.dutyCeil() * 1000) / 1000.0f;
  pid["tuned"] = g_thermal.tuned();
  JsonObject sch = pid["schedule"].to<JsonObject>();
  const GainSchedule::Gains hg = g_thermal.heatGains();
  const GainSchedule::Gains dg = g_thermal.holdGains();
  JsonObject shHeat = sch["heat"].to<JsonObject>();
  shHeat["kp"] = hg.kp; shHeat["ki"] = hg.ki; shHeat["kd"] = hg.kd;
  JsonObject shHold = sch["hold"].to<JsonObject>();
  shHold["kp"] = dg.kp; shHold["ki"] = dg.ki; shHold["kd"] = dg.kd;
  JsonObject at = pid["autotune"].to<JsonObject>();
  at["active"] = g_thermal.autotuneActive();
  at["progress"] = g_thermal.autotuneProgress();
  if (g_thermal.autotuneActive()) at["phase"] = g_thermal.autotunePhase();  // "ramp"|"cycling"
  else at["phase"] = nullptr;
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
  const bool drvConnected = g_motorDiag.connected;          // 1 Hz cache (motorDiagRefresh)
  const DrvStatusFlags dflags = g_motorDiag.flags;
  // load: StallGuard, only meaningful while running + linked.
  if (t.running && drvConnected) disc["load"] = g_motorDiag.load;
  else disc["load"] = nullptr;
  JsonObject drv = disc["driver"].to<JsonObject>();
  char ver[8];
  snprintf(ver, sizeof(ver), "0x%02X", g_motorDiag.version);
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
  const int runId = g_sd.currentRunId();
  if (runId) run["id"] = runId;
  else run["id"] = nullptr;
  const char* runName = g_sd.currentRunName();
  if (runId && runName[0]) run["name"] = runName;
  else run["name"] = nullptr;

  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["mode"] = g_wifi.apActive() ? "ap" : "sta";
  wifi["connected"] = g_wifi.staConnected();
  wifi["recoveries"] = g_wifi.recoveryCount();  // WiFi-stack self-heals since boot
  wifi["ssid"] = WiFi.SSID();
  wifi["ip"] = g_wifi.ipAddress();
  if (g_wifi.staConnected()) wifi["rssi"] = WiFi.RSSI();
  else wifi["rssi"] = nullptr;

  JsonObject storage = doc["storage"].to<JsonObject>();
  storage["sdMounted"] = g_sd.mounted();
  storage["logBytes"] = nullptr;  // accurate size arrives with the SD-mgmt phase
  // logging = rows are actually being written right now (run open, writes OK) —
  // not merely "card mounted". logDegraded latches when a row write fails
  // mid-run (yanked/dying card): rows are being lost.
  storage["logging"] = g_sd.mounted() && g_sd.currentRunId() != 0 && !g_sd.writeDegraded();
  storage["logDegraded"] = g_sd.writeDegraded();
  storage["logIntervalSec"] = g_sd.logIntervalSec();

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

// Build the runs-list JSON for GET /api/v1/runs (spec §Runs). current=true marks
// the in-progress run. startedSec/durationSec are omitted (no per-run RTC); the
// UI tolerates their absence.
String buildRunsJson(int& latestOut) {
  JsonDocument doc;
  JsonArray arr = doc["runs"].to<JsonArray>();
  const int curId = g_sd.currentRunId();
  latestOut = 0;
  for (const SdLogger::RunInfo& r : g_sd.listRuns()) {  // one enumeration, reused below
    JsonObject o = arr.add<JsonObject>();
    o["id"] = r.id;
    o["label"] = r.label;
    o["bytes"] = r.bytes;
    o["current"] = (r.id == curId);
    if (r.id > latestOut) latestOut = r.id;
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
  Serial.printf("[boot] ESP-IDF %s / Arduino-ESP32 %s\n", ESP.getSdkVersion(), ESP_ARDUINO_VERSION_STR);
  Serial.printf("[FEAT] SD logging: %s\n", AppConfig::Features::kEnableSdLogging ? "enabled" : "disabled");
  Serial.printf("[FEAT] OLED UI:    %s\n", AppConfig::Features::kEnableOledUi ? "enabled" : "disabled");
  Serial.printf("[FEAT] autotune:   %s\n", AppConfig::Features::kEnableAutotune ? "enabled" : "disabled");
  Serial.printf("[FEAT] adaptive PID: %s\n", AppConfig::Features::kEnableAdaptiveThermal ? "enabled" : "disabled");

  // Primary I2C bus (Wire, GPIO1/2): HUSB238 and anything else on the board header.
  Wire.begin(AppConfig::I2c::kSdaPin, AppConfig::I2c::kSclPin, AppConfig::I2c::kClockHz);
  // The OLED lives on its OWN bus (Wire1, GPIO43/44), brought up inside g_display.begin()
  // — fully isolated from the HUSB238, so neither can disturb the other.
  g_input.begin();
  if (AppConfig::Features::kEnableOledUi) {
    g_display.begin();
    Serial.printf("[UI] OLED %s\n", g_display.present() ? "detected" : "absent (headless)");
  }
  requestPd();

  // Storage.
  if (AppConfig::Features::kEnableSdLogging) {
    g_sd.begin();
    g_sd.checkAndReport(Serial);
  }

  // Control hardware: thermistor + heater (PID), then the motor.
  g_thermal.begin();
  Serial.println(F("\n[TMC2209] Bringing up UART driver ..."));
  g_motorDiag.connected = g_motor.begin();
  g_motorDiag.version = g_motor.version();   // read once; cached for telemetry
  if (g_motorDiag.connected) {
    Serial.printf("  connected, version 0x%02X\n", g_motorDiag.version);
  } else {
    Serial.printf("  NOT responding (version 0x%02X) — check +12V and UART\n",
                  g_motorDiag.version);
  }

  // Process state (loads persisted setpoints) — does not auto-start a run.
  g_reactor.begin();

  // Networking + web UI.
  g_wifi.begin();
  g_web.begin();
  g_espnow.begin();
  g_web.setEspNow(&g_espnow);

  Serial.println(F("\n[RUN] reactor idle — control via browser."));
}

void tick() {
  g_wifi.poll();
  pdReconcile();        // keep re-asserting the 12V PD request until VBUS=12V
  g_thermal.update();   // PID, internally gated to its sample period
  g_reactor.update();   // run timer

  // Run-file lifecycle on the running edge — the one place that sees EVERY
  // start/stop path (web, HUB, OLED panel, duration timeout).
  //   rising edge: open a run file if none is open yet. Web starts already
  //     opened one (with the session name) in applyPending; panel starts used
  //     to bypass SD entirely and log nothing — this closes that hole.
  //   falling edge: close+save the file. Web stops already called endRun().
  static bool prevRunning = false;
  const bool nowRunning = g_reactor.running();
  if (AppConfig::Features::kEnableSdLogging && !prevRunning && nowRunning &&
      g_sd.mounted() && g_sd.currentRunId() == 0) {
    const int id = g_sd.startRun("");  // unnamed: panel has no name entry
    Serial.printf("[SD] run file opened (id=%d) for non-web start\n", id);
  }
  if (AppConfig::Features::kEnableSdLogging && prevRunning && !nowRunning && g_sd.currentRunId() != 0) {
    g_sd.endRun(true);  // auto-stop saves
  }
  prevRunning = nowRunning;

  {
    ui::ReactorSnapshot snap = buildUiSnapshot();
    for (ui::UiEvent e = g_input.poll(); e != ui::UiEvent::None; e = g_input.poll())
      g_ui.handle(e, snap);
    static uint32_t lastDrawMs = 0;
    if (AppConfig::Features::kEnableOledUi && millis() - lastDrawMs >= AppConfig::Ui::kRedrawIntervalMs) {
      lastDrawMs = millis();
      g_display.render(g_ui, snap);
    }
  }

  // Rebuild status/scan JSON at the WS push rate (~4 Hz — building faster than
  // it can be pushed was wasted work); apply commands + push WS every loop.
  static uint32_t lastStatusMs = 0;
  static String statusJson = "{}";
  static String scanJson = "{\"scanning\":false,\"networks\":[]}";
  const uint32_t now = millis();
  motorDiagRefresh(now);   // TMC UART poll, self-gated to kDiagPeriodMs
  if (now - lastStatusMs >= AppConfig::Web::kWsPushPeriodMs) {
    lastStatusMs = now;
    statusJson = buildStatusJson();
    scanJson = g_wifi.scanJson();
    g_web.cacheCalJson(buildCalJson());
    // Rebuild the runs list only when the run-file set actually changed
    // (start/stop/delete/erase). The old ~1 Hz rebuild re-enumerated the card —
    // opening every .name sidecar — every second, forever.
    static uint32_t lastRunsMut = ~0UL;
    if (AppConfig::Features::kEnableSdLogging && g_sd.mutations() != lastRunsMut) {
      lastRunsMut = g_sd.mutations();
      int latestRun = 0;
      g_web.cacheRunsJson(buildRunsJson(latestRun));
      g_web.cacheLatestRunId(latestRun);  // lets GET /log resolve the newest run off-bus
    }
  }
  g_web.update(statusJson, scanJson);
  g_espnow.poll();

  // WiFi TX-wedge self-heal. Driven by the ESP-NOW L2 send counters (the TX whose
  // result we can see): if the STA is associated but nothing lands for kTxStallMs,
  // the driver TX-buffer pool has wedged — restart the WiFi stack (never
  // ESP.restart(), a run may be active) and rebuild ESP-NOW.
  {
    uint32_t enTx = 0, enOk = 0; int enErr = 0;
    EspNowLink::txDiag(enTx, enOk, enErr);
    if (g_wifi.pollWatchdog(now, enTx, enOk)) g_espnow.reinit();
  }

  // Periodic SD logging — run-only: rows are written only while a run is open.
  static uint32_t lastLogMs = 0;
  if (AppConfig::Features::kEnableSdLogging && g_sd.mounted() && g_sd.currentRunId() != 0 && now - lastLogMs >= g_sd.logIntervalMs()) {
    lastLogMs = now;
    g_sd.appendLine(g_reactor.csvRow());
  }

  delay(2);  // yield to WiFi/AsyncTCP tasks
}

}  // namespace AppRuntime
