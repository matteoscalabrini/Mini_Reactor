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
 *   POST /api/v1/log/interval   {seconds} set the SD log row interval (1..3600)
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

#include <string.h>
#include "control/Reactor.hpp"
#include "net/WifiManager.hpp"
#include "storage/SdLogger.hpp"
#include "storage/RunFiles.hpp"

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

// Copy a sanitized session name into a fixed buffer (firmware is authoritative).
static void copySanitizedName(char* dst, size_t dstSize, const String& raw) {
  const std::string s = RunFiles::sanitizeName(std::string(raw.c_str()), dstSize - 1);
  strncpy(dst, s.c_str(), dstSize - 1);
  dst[dstSize - 1] = '\0';
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
          const String name = o["name"] | "";
          xSemaphoreTake(mutex_, portMAX_DELAY);
          pending_.runStart = true;
          pending_.runTargetC = targetC;
          pending_.runRpm = rpm;
          pending_.runDurMin = o["durationMin"] | 0;
          copySanitizedName(pending_.runName, sizeof(pending_.runName), name);
          xSemaphoreGive(mutex_);
          sendOk(req);
        } else if (action == "stop") {
          const String data = o["data"] | "save";
          if (data != "save" && data != "discard") {
            sendError(req, 400, "invalid_request", "data must be save|discard");
            return;
          }
          xSemaphoreTake(mutex_, portMAX_DELAY);
          pending_.runStop = true;
          pending_.runStopSave = (data == "save");
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
    if (!sd_.mounted()) {
      sendError(req, 503, "no_log", "no log file on the SD card");
      return;
    }
    const int latest = sd_.latestRunId();
    const String path = latest > 0 ? sd_.runCsvPath(latest) : String(sd_.logPath());
    if (!SD.exists(path)) {
      sendError(req, 503, "no_log", "no log file on the SD card");
      return;
    }
    req->send(SD, path, "text/csv", true);
  });

  server_->on("/api/v1/log/clear", HTTP_POST, [this](AsyncWebServerRequest* req) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    pending_.logClear = true;
    xSemaphoreGive(mutex_);
    sendOk(req);
  });

  // ── POST log interval (seconds between SD log rows) ──
  auto* logIntervalHandler = new AsyncCallbackJsonWebHandler(
      "/api/v1/log/interval", [this](AsyncWebServerRequest* req, JsonVariant& json) {
        JsonObject o = json.as<JsonObject>();
        if (o["seconds"].isNull()) {
          sendError(req, 400, "invalid_request", "seconds required");
          return;
        }
        const long s = o["seconds"].as<long>();
        if (s < 1 || s > 3600) {
          sendError(req, 400, "out_of_range", "seconds must be 1..3600");
          return;
        }
        xSemaphoreTake(mutex_, portMAX_DELAY);
        pending_.logInterval = true;
        pending_.logIntervalSec = (uint32_t)s;
        xSemaphoreGive(mutex_);
        sendOk(req);
      });
  server_->addHandler(logIntervalHandler);

  // ── GET runs list (served from the loop-built cache) ──
  server_->on("/api/v1/runs", HTTP_GET, [this](AsyncWebServerRequest* req) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const String body = runsJson_;
    xSemaphoreGive(mutex_);
    sendJson(req, body);
  });

  // ── GET one run's CSV (download) ──
  server_->on("^\\/api\\/v1\\/runs\\/([0-9]+)$", HTTP_GET,
              [this](AsyncWebServerRequest* req) {
    const int id = req->pathArg(0).toInt();
    const String path = sd_.runCsvPath(id);
    if (!sd_.mounted() || !SD.exists(path)) {
      sendError(req, 404, "not_found", "no such run");
      return;
    }
    req->send(SD, path, "text/csv", true);
  });

  // ── POST delete a run ──
  server_->on("^\\/api\\/v1\\/runs\\/([0-9]+)\\/delete$", HTTP_POST,
              [this](AsyncWebServerRequest* req) {
    const int id = req->pathArg(0).toInt();
    xSemaphoreTake(mutex_, portMAX_DELAY);
    pending_.runDelete = true;
    pending_.runDeleteId = id;
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

  // Log each triggered command to serial ([CMD]) for on-device debugging.
  if (p.runStart) {
    Serial.printf("[CMD] run start: target=%.1fC rpm=%.1f dur=%umin name='%s'\n",
                  p.runTargetC, p.runRpm, (unsigned)p.runDurMin, p.runName);
    reactor_.start(p.runTargetC, p.runRpm, p.runDurMin);
    if (reactor_.running()) sd_.startRun(p.runName);  // open the per-run file
  }
  if (p.runStop) {
    Serial.printf("[CMD] run stop (%s)\n", p.runStopSave ? "save" : "discard");
    reactor_.stop();
    sd_.endRun(p.runStopSave);
  }
  if (p.runDelete) {
    Serial.printf("[CMD] run delete id=%d\n", p.runDeleteId);
    sd_.deleteRun(p.runDeleteId);
  }
  if (p.setTarget) { Serial.printf("[CMD] setpoint target=%.1fC\n", p.setTargetC); reactor_.setTargetC(p.setTargetC); }
  if (p.setRpm) { Serial.printf("[CMD] setpoint rpm=%.1f\n", p.setRpmVal); reactor_.setRpm(p.setRpmVal); }
  if (p.discRpm) { Serial.printf("[CMD] disc rpm=%.1f\n", p.discRpmVal); reactor_.setRpm(p.discRpmVal); }
  if (p.discCurrent) { Serial.printf("[CMD] disc current=%umA\n", (unsigned)p.discCurrentMa); reactor_.setDiscCurrentMa(p.discCurrentMa); }
  if (p.discMicro) { Serial.printf("[CMD] disc microsteps=%u\n", (unsigned)p.discMicrosteps); reactor_.setDiscMicrosteps(p.discMicrosteps); }
  if (p.discDir) { Serial.printf("[CMD] disc direction=%s\n", p.discReverse ? "ccw" : "cw"); reactor_.setDiscReverse(p.discReverse); }
  if (p.discEnable) { Serial.printf("[CMD] disc enable=%d\n", p.discEnableVal); reactor_.setDiscEnabled(p.discEnableVal); }
  if (p.pidGains) { Serial.printf("[CMD] pid gains kp=%.4f ki=%.4f kd=%.4f\n", p.pidKp, p.pidKi, p.pidKd); reactor_.setPidGains(p.pidKp, p.pidKi, p.pidKd); }
  if (p.pidMode) { Serial.printf("[CMD] pid mode=%s\n", p.pidModeStr.c_str()); reactor_.setPidMode(p.pidModeStr.c_str()); }
  if (p.autotuneStart) { Serial.println("[CMD] autotune start"); reactor_.startAutotune(); }
  if (p.autotuneCancel) { Serial.println("[CMD] autotune cancel"); reactor_.cancelAutotune(); }
  if (p.calPoint) { Serial.printf("[CMD] calibration point ref=%.1fC\n", p.calRefC); reactor_.addCalibrationPoint(p.calRefC); }
  if (p.calCompute) { Serial.println("[CMD] calibration compute"); reactor_.computeCalibration(); }
  if (p.calReset) { Serial.println("[CMD] calibration reset"); reactor_.resetCalibration(); }
  if (p.wifiConnect) { Serial.printf("[CMD] wifi connect ssid='%s'\n", p.wifiSsid.c_str()); wifi_.connect(p.wifiSsid, p.wifiPass); }
  if (p.wifiForget) { Serial.println("[CMD] wifi forget"); wifi_.forget(); }
  if (p.wifiScan) { Serial.println("[CMD] wifi scan requested"); wifi_.requestScan(); }
  if (p.logClear) { Serial.println("[CMD] sd log clear"); sd_.clearLog(); }
  if (p.logInterval) { Serial.printf("[CMD] log interval=%us\n", (unsigned)p.logIntervalSec); sd_.setLogIntervalSec(p.logIntervalSec); }
  if (p.sdErase) { Serial.println("[CMD] sd ERASE all files"); sd_.eraseAll(); }
  if (p.motorTest) { Serial.println("[CMD] motor test jog"); reactor_.startMotorTest(); }
}

void WebInterface::cacheCalJson(const String& calJson) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  calJson_ = calJson;
  xSemaphoreGive(mutex_);
}

void WebInterface::cacheRunsJson(const String& runsJson) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  runsJson_ = runsJson;
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
