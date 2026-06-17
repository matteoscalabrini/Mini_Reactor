/*
 * WebInterface.cpp — async HTTP + WebSocket control surface.
 * See include/net/WebInterface.hpp.
 *
 * Routes:
 *   GET  /                      static UI from SPIFFS (index.html, app.css, app.js, logo.svg)
 *   GET  /api/v1/status         cached reactor + wifi + system status JSON
 *   POST /api/v1/run            {action:"start"|"stop", targetC, rpm, durationMin}
 *   POST /api/v1/setpoint       {targetC?, rpm?}  (live changes)
 *   POST /api/v1/disc           {rpm?, currentMa?, microsteps?, direction?, enabled?}
 *   GET  /api/v1/wifi/scan      cached scan results (also triggers a fresh scan)
 *   POST /api/v1/wifi/connect   {ssid, password}
 *   POST /api/v1/wifi/forget
 *   GET  /api/v1/log            download the SD CSV log
 *   POST /api/v1/log/clear      rotate (clear) the SD log
 *   GET  /api/v1/calibration          returns cached calibration state (method, calibrated, points)
 *   POST /api/v1/calibration/point    {referenceC}; queues capture of a calibration point (live NTC resistance)
 *   POST /api/v1/calibration/compute  queues fit (offset/Beta/Steinhart by point count); result via GET
 *   POST /api/v1/calibration/reset    queues revert to factory Beta
 *   POST /api/v1/disc/test            brief disc jog to confirm the drive turns (~8 rpm, 3 s)
 *   POST /api/v1/sd/erase             erase ALL files on the SD card, then recreate the log
 *   WS   /ws                    telemetry push
 */

#include "net/WebInterface.hpp"

#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>
#include <FS.h>
#include <SD.h>
#include <SPIFFS.h>

#include "control/Reactor.hpp"
#include "net/WifiManager.hpp"
#include "storage/SdLogger.hpp"

WebInterface::WebInterface(Reactor& reactor, WifiManager& wifi, SdLogger& sd,
                           const Config& config)
    : reactor_(reactor), wifi_(wifi), sd_(sd), cfg_(config) {
  mutex_ = xSemaphoreCreateMutex();
}

void WebInterface::begin() {
  if (!SPIFFS.begin(true)) {
    Serial.println("[WEB] SPIFFS mount failed");
  } else {
    Serial.printf("[WEB] SPIFFS mounted, index.html %s\n",
                  SPIFFS.exists("/index.html") ? "found" : "MISSING (run uploadfs)");
  }

  server_ = new AsyncWebServer(cfg_.port);
  ws_ = new AsyncWebSocket("/ws");
  registerRoutes();
  server_->begin();
  Serial.printf("[WEB] server up on port %u\n", cfg_.port);
}

static void sendJson(AsyncWebServerRequest* req, const String& body) {
  AsyncWebServerResponse* resp = req->beginResponse(200, "application/json", body);
  resp->addHeader("Access-Control-Allow-Origin", "*");
  req->send(resp);
}

static void sendOk(AsyncWebServerRequest* req) { sendJson(req, "{\"ok\":true}"); }

static void sendError(AsyncWebServerRequest* req, int status, const char* code,
                      const char* msg) {
  String body = String("{\"ok\":false,\"error\":{\"code\":\"") + code +
                "\",\"message\":\"" + msg + "\"}}";
  AsyncWebServerResponse* resp = req->beginResponse(status, "application/json", body);
  resp->addHeader("Access-Control-Allow-Origin", "*");
  req->send(resp);
}

void WebInterface::registerRoutes() {
  // ── WebSocket ──
  ws_->onEvent([this](AsyncWebSocket*, AsyncWebSocketClient* client,
                      AwsEventType type, void*, uint8_t*, size_t) {
    if (type == WS_EVT_CONNECT) {
      xSemaphoreTake(mutex_, portMAX_DELAY);
      const String snapshot = statusJson_;
      xSemaphoreGive(mutex_);
      client->text(snapshot);
    }
  });
  server_->addHandler(ws_);

  // ── GET status ──
  server_->on("/api/v1/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const String body = statusJson_;
    xSemaphoreGive(mutex_);
    sendJson(req, body);
  });

  // ── GET wifi scan ──
  server_->on("/api/v1/wifi/scan", HTTP_GET, [this](AsyncWebServerRequest* req) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    pending_.wifiScan = true;
    const String body = scanJson_;
    xSemaphoreGive(mutex_);
    sendJson(req, body);
  });

  // ── POST run ──
  auto* runHandler = new AsyncCallbackJsonWebHandler(
      "/api/v1/run", [this](AsyncWebServerRequest* req, JsonVariant& json) {
        JsonObject o = json.as<JsonObject>();
        const String action = o["action"] | "";
        if (action == "start") {
          const float rpm = o["rpm"] | 8.0f;
          const float targetC = o["targetC"] | 36.0f;
          if (rpm < 0.0f || rpm > 30.0f) {  // kMinRpm..kMaxRpm
            sendError(req, 400, "out_of_range", "rpm must be 0..30");
            return;
          }
          if (targetC < 0.0f || targetC > 55.0f) {  // processMaxC ceiling
            sendError(req, 400, "out_of_range", "targetC must be 0..55");
            return;
          }
          xSemaphoreTake(mutex_, portMAX_DELAY);
          pending_.runStart = true;
          pending_.runTargetC = targetC;
          pending_.runRpm = rpm;
          pending_.runDurMin = o["durationMin"] | 0;
          xSemaphoreGive(mutex_);
          sendOk(req);
        } else if (action == "stop") {
          xSemaphoreTake(mutex_, portMAX_DELAY);
          pending_.runStop = true;
          xSemaphoreGive(mutex_);
          sendOk(req);
        } else {
          sendError(req, 400, "invalid_request", "action must be start|stop");
        }
      });
  server_->addHandler(runHandler);

  // ── POST setpoint (live targetC / rpm) ──
  auto* setHandler = new AsyncCallbackJsonWebHandler(
      "/api/v1/setpoint", [this](AsyncWebServerRequest* req, JsonVariant& json) {
        JsonObject o = json.as<JsonObject>();
        if (!o["targetC"].isNull()) {
          const float targetC = o["targetC"].as<float>();
          if (targetC < 0.0f || targetC > 55.0f) {  // processMaxC ceiling
            sendError(req, 400, "out_of_range", "targetC must be 0..55");
            return;
          }
        }
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (!o["targetC"].isNull()) {
          pending_.setTarget = true;
          pending_.setTargetC = o["targetC"].as<float>();
        }
        if (!o["rpm"].isNull()) {
          pending_.setRpm = true;
          pending_.setRpmVal = o["rpm"].as<float>();
        }
        xSemaphoreGive(mutex_);
        sendOk(req);
      });
  server_->addHandler(setHandler);

  // ── POST disc (drive params) ──
  auto* discHandler = new AsyncCallbackJsonWebHandler(
      "/api/v1/disc", [this](AsyncWebServerRequest* req, JsonVariant& json) {
        JsonObject o = json.as<JsonObject>();
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (!o["rpm"].isNull())       { pending_.discRpm = true; pending_.discRpmVal = o["rpm"].as<float>(); }
        if (!o["currentMa"].isNull()) { pending_.discCurrent = true; pending_.discCurrentMa = o["currentMa"].as<uint16_t>(); }
        if (!o["microsteps"].isNull()){ pending_.discMicro = true; pending_.discMicrosteps = o["microsteps"].as<uint16_t>(); }
        if (!o["direction"].isNull()) { pending_.discDir = true; pending_.discReverse = (o["direction"].as<String>() == "ccw"); }
        if (!o["enabled"].isNull())   { pending_.discEnable = true; pending_.discEnableVal = o["enabled"].as<bool>(); }
        xSemaphoreGive(mutex_);
        sendOk(req);
      });
  server_->addHandler(discHandler);

  // ── POST pid (gains + mode) ──
  auto* pidHandler = new AsyncCallbackJsonWebHandler(
      "/api/v1/pid", [this](AsyncWebServerRequest* req, JsonVariant& json) {
        JsonObject o = json.as<JsonObject>();
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (!o["kp"].isNull() && !o["ki"].isNull() && !o["kd"].isNull()) {
          pending_.pidGains = true;
          pending_.pidKp = o["kp"].as<float>();
          pending_.pidKi = o["ki"].as<float>();
          pending_.pidKd = o["kd"].as<float>();
        }
        if (!o["mode"].isNull()) {
          pending_.pidMode = true;
          pending_.pidModeStr = o["mode"].as<String>();
        }
        xSemaphoreGive(mutex_);
        sendOk(req);
      });
  server_->addHandler(pidHandler);

  // ── POST pid/autotune (start|cancel) ──
  auto* autotuneHandler = new AsyncCallbackJsonWebHandler(
      "/api/v1/pid/autotune", [this](AsyncWebServerRequest* req, JsonVariant& json) {
        JsonObject o = json.as<JsonObject>();
        const String action = o["action"] | "";
        if (action != "start" && action != "cancel") {
          sendError(req, 400, "invalid_request", "action must be start|cancel");
          return;
        }
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (action == "start") pending_.autotuneStart = true;
        else pending_.autotuneCancel = true;
        xSemaphoreGive(mutex_);
        sendOk(req);
      });
  server_->addHandler(autotuneHandler);

  // ── GET calibration ──
  server_->on("/api/v1/calibration", HTTP_GET, [this](AsyncWebServerRequest* req) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const String body = calJson_;
    xSemaphoreGive(mutex_);
    sendJson(req, body);
  });

  // ── POST calibration/point ──
  auto* calPointHandler = new AsyncCallbackJsonWebHandler(
      "/api/v1/calibration/point", [this](AsyncWebServerRequest* req, JsonVariant& json) {
        JsonObject o = json.as<JsonObject>();
        if (o["referenceC"].isNull()) {
          sendError(req, 400, "invalid_request", "referenceC required");
          return;
        }
        xSemaphoreTake(mutex_, portMAX_DELAY);
        pending_.calPoint = true;
        pending_.calRefC = o["referenceC"].as<float>();
        xSemaphoreGive(mutex_);
        sendOk(req);
      });
  server_->addHandler(calPointHandler);

  // ── POST calibration/compute ──
  server_->on("/api/v1/calibration/compute", HTTP_POST, [this](AsyncWebServerRequest* req) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    pending_.calCompute = true;
    xSemaphoreGive(mutex_);
    sendOk(req);
  });

  // ── POST calibration/reset ──
  server_->on("/api/v1/calibration/reset", HTTP_POST, [this](AsyncWebServerRequest* req) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    pending_.calReset = true;
    xSemaphoreGive(mutex_);
    sendOk(req);
  });

  // ── POST sd/erase ──
  server_->on("/api/v1/sd/erase", HTTP_POST, [this](AsyncWebServerRequest* req) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    pending_.sdErase = true;
    xSemaphoreGive(mutex_);
    sendOk(req);
  });

  // ── POST disc/test (timed motor jog) ──
  server_->on("/api/v1/disc/test", HTTP_POST, [this](AsyncWebServerRequest* req) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    pending_.motorTest = true;
    xSemaphoreGive(mutex_);
    sendOk(req);
  });

  // ── POST wifi connect ──
  auto* wifiHandler = new AsyncCallbackJsonWebHandler(
      "/api/v1/wifi/connect", [this](AsyncWebServerRequest* req, JsonVariant& json) {
        JsonObject o = json.as<JsonObject>();
        const String ssid = o["ssid"] | "";
        if (ssid.isEmpty()) {
          sendError(req, 400, "wifi_ssid_required", "ssid is required");
          return;
        }
        xSemaphoreTake(mutex_, portMAX_DELAY);
        pending_.wifiConnect = true;
        pending_.wifiSsid = ssid;
        pending_.wifiPass = o["password"] | "";
        xSemaphoreGive(mutex_);
        sendOk(req);
      });
  server_->addHandler(wifiHandler);

  server_->on("/api/v1/wifi/forget", HTTP_POST, [this](AsyncWebServerRequest* req) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    pending_.wifiForget = true;
    xSemaphoreGive(mutex_);
    sendOk(req);
  });

  // ── SD log download / clear ──
  server_->on("/api/v1/log", HTTP_GET, [this](AsyncWebServerRequest* req) {
    if (!sd_.mounted() || !SD.exists(sd_.logPath())) {
      sendError(req, 503, "no_log", "no log file on the SD card");
      return;
    }
    AsyncWebServerResponse* resp =
        req->beginResponse(SD, sd_.logPath(), "text/csv", true);
    req->send(resp);
  });

  server_->on("/api/v1/log/clear", HTTP_POST, [this](AsyncWebServerRequest* req) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    pending_.logClear = true;
    xSemaphoreGive(mutex_);
    sendOk(req);
  });

  // ── Static UI + SPA fallback ──
  server_->serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

  server_->onNotFound([](AsyncWebServerRequest* req) {
    if (req->url().startsWith("/api/")) {
      sendError(req, 404, "not_found", "unknown endpoint");
      return;
    }
    if (SPIFFS.exists("/index.html")) {
      req->send(SPIFFS, "/index.html", "text/html");
    } else {
      req->send(200, "text/html",
                "<h1>Mini Reactor</h1><p>UI assets missing — run "
                "<code>pio run -t uploadfs</code>.</p>");
    }
  });
}

void WebInterface::applyPending() {
  Pending p;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  p = pending_;
  pending_ = Pending{};
  xSemaphoreGive(mutex_);

  if (p.runStart) reactor_.start(p.runTargetC, p.runRpm, p.runDurMin);
  if (p.runStop) reactor_.stop();
  if (p.setTarget) reactor_.setTargetC(p.setTargetC);
  if (p.setRpm) reactor_.setRpm(p.setRpmVal);
  if (p.discRpm) reactor_.setRpm(p.discRpmVal);
  if (p.discCurrent) reactor_.setDiscCurrentMa(p.discCurrentMa);
  if (p.discMicro) reactor_.setDiscMicrosteps(p.discMicrosteps);
  if (p.discDir) reactor_.setDiscReverse(p.discReverse);
  if (p.discEnable) reactor_.setDiscEnabled(p.discEnableVal);
  if (p.pidGains) reactor_.setPidGains(p.pidKp, p.pidKi, p.pidKd);
  if (p.pidMode) reactor_.setPidMode(p.pidModeStr.c_str());
  if (p.autotuneStart) reactor_.startAutotune();
  if (p.autotuneCancel) reactor_.cancelAutotune();
  if (p.calPoint) reactor_.addCalibrationPoint(p.calRefC);
  if (p.calCompute) reactor_.computeCalibration();
  if (p.calReset) reactor_.resetCalibration();
  if (p.wifiConnect) wifi_.connect(p.wifiSsid, p.wifiPass);
  if (p.wifiForget) wifi_.forget();
  if (p.wifiScan) wifi_.requestScan();
  if (p.logClear) sd_.clearLog();
  if (p.sdErase) sd_.eraseAll();
  if (p.motorTest) reactor_.startMotorTest();
}

void WebInterface::cacheCalJson(const String& calJson) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  calJson_ = calJson;
  xSemaphoreGive(mutex_);
}

void WebInterface::update(const String& statusJson, const String& scanJson) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  statusJson_ = statusJson;
  scanJson_ = scanJson;
  xSemaphoreGive(mutex_);

  applyPending();

  const uint32_t now = millis();
  if (ws_ && ws_->count() > 0 && now - lastPushMs_ >= cfg_.wsPushPeriodMs) {
    lastPushMs_ = now;
    ws_->textAll(statusJson);
  }
  if (ws_) ws_->cleanupClients();
}
