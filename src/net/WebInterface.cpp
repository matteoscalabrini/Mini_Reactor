/*
 * WebInterface.cpp — async HTTP + WebSocket control surface.
 * See include/net/WebInterface.hpp.
 *
 * Routes:
 *   GET  /                  static UI from SPIFFS (index.html, app.css, app.js, logo.svg)
 *   GET  /api/status        cached reactor + wifi + system status JSON
 *   POST /api/run           {action:"start"|"stop", targetC, motorPercent, durationMin}
 *   POST /api/setpoint      {targetC?, motorPercent?}  (live changes)
 *   GET  /api/wifi/scan     cached scan results (also triggers a fresh scan)
 *   POST /api/wifi/connect  {ssid, password}
 *   POST /api/wifi/forget
 *   GET  /api/log           download the SD CSV log
 *   POST /api/log/clear     rotate (clear) the SD log
 *   WS   /ws                telemetry push
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

  // ── GET status / scan ──
  server_->on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const String body = statusJson_;
    xSemaphoreGive(mutex_);
    sendJson(req, body);
  });

  server_->on("/api/wifi/scan", HTTP_GET, [this](AsyncWebServerRequest* req) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    pending_.wifiScan = true;  // trigger a fresh scan for next time
    const String body = scanJson_;
    xSemaphoreGive(mutex_);
    sendJson(req, body);
  });

  // ── POST run ──
  auto* runHandler = new AsyncCallbackJsonWebHandler(
      "/api/run", [this](AsyncWebServerRequest* req, JsonVariant& json) {
        JsonObject o = json.as<JsonObject>();
        const String action = o["action"] | "";
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (action == "start") {
          pending_.runStart = true;
          pending_.runTargetC = o["targetC"] | 30.0f;
          pending_.runMotorPct = o["motorPercent"] | 40.0f;
          pending_.runDurMin = o["durationMin"] | 0;
        } else if (action == "stop") {
          pending_.runStop = true;
        }
        xSemaphoreGive(mutex_);
        sendJson(req, "{\"ok\":true}");
      });
  server_->addHandler(runHandler);

  // ── POST setpoint (live changes) ──
  auto* setHandler = new AsyncCallbackJsonWebHandler(
      "/api/setpoint", [this](AsyncWebServerRequest* req, JsonVariant& json) {
        JsonObject o = json.as<JsonObject>();
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (!o["targetC"].isNull()) {
          pending_.setTarget = true;
          pending_.setTargetC = o["targetC"].as<float>();
        }
        if (!o["motorPercent"].isNull()) {
          pending_.setMotor = true;
          pending_.setMotorPct = o["motorPercent"].as<float>();
        }
        xSemaphoreGive(mutex_);
        sendJson(req, "{\"ok\":true}");
      });
  server_->addHandler(setHandler);

  // ── POST wifi connect ──
  auto* wifiHandler = new AsyncCallbackJsonWebHandler(
      "/api/wifi/connect", [this](AsyncWebServerRequest* req, JsonVariant& json) {
        JsonObject o = json.as<JsonObject>();
        const String ssid = o["ssid"] | "";
        if (ssid.isEmpty()) {
          sendJson(req, "{\"ok\":false,\"error\":\"ssid_required\"}");
          return;
        }
        xSemaphoreTake(mutex_, portMAX_DELAY);
        pending_.wifiConnect = true;
        pending_.wifiSsid = ssid;
        pending_.wifiPass = o["password"] | "";
        xSemaphoreGive(mutex_);
        sendJson(req, "{\"ok\":true}");
      });
  server_->addHandler(wifiHandler);

  server_->on("/api/wifi/forget", HTTP_POST, [this](AsyncWebServerRequest* req) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    pending_.wifiForget = true;
    xSemaphoreGive(mutex_);
    sendJson(req, "{\"ok\":true}");
  });

  // ── SD log download / clear ──
  server_->on("/api/log", HTTP_GET, [this](AsyncWebServerRequest* req) {
    if (!sd_.mounted() || !SD.exists(sd_.logPath())) {
      req->send(503, "application/json", "{\"error\":\"no_log\"}");
      return;
    }
    AsyncWebServerResponse* resp =
        req->beginResponse(SD, sd_.logPath(), "text/csv", true);
    req->send(resp);
  });

  server_->on("/api/log/clear", HTTP_POST, [this](AsyncWebServerRequest* req) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    pending_.logClear = true;
    xSemaphoreGive(mutex_);
    sendJson(req, "{\"ok\":true}");
  });

  // ── Static UI + captive-portal / SPA fallback ──
  server_->serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

  server_->onNotFound([](AsyncWebServerRequest* req) {
    if (req->url().startsWith("/api/")) {
      req->send(404, "application/json", "{\"error\":\"not_found\"}");
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
  // Snapshot + clear under the mutex, then act in this (loop) context.
  Pending p;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  p = pending_;
  pending_ = Pending{};
  xSemaphoreGive(mutex_);

  if (p.runStart) reactor_.start(p.runTargetC, p.runMotorPct, p.runDurMin);
  if (p.runStop) reactor_.stop();
  if (p.setTarget) reactor_.setTargetC(p.setTargetC);
  if (p.setMotor) reactor_.setMotorPercent(p.setMotorPct);
  if (p.wifiConnect) wifi_.connect(p.wifiSsid, p.wifiPass);
  if (p.wifiForget) wifi_.forget();
  if (p.wifiScan) wifi_.requestScan();
  if (p.logClear) sd_.clearLog();
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
