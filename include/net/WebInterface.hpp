/*
 * WebInterface.hpp — async HTTP + WebSocket control surface for the reactor.
 *
 * Serves the browser UI from SPIFFS and exposes a REST API + a WebSocket
 * telemetry feed (ESPAsyncWebServer). Threading model: the async callbacks run
 * in the AsyncTCP task, so handlers NEVER touch hardware — they only validate
 * input and queue commands (under a mutex) and read loop-updated JSON caches.
 * All hardware actions and WS broadcasts happen in update(), called from the
 * main loop. AppRuntime feeds it the current status/scan JSON each loop.
 *
 * Implementation: src/net/WebInterface.cpp
 */

#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class Reactor;
class WifiManager;
class SdLogger;
class AsyncWebServer;
class AsyncWebSocket;

class WebInterface {
 public:
  struct Config {
    uint16_t port = 80;
    uint32_t wsPushPeriodMs = 250;
  };

  WebInterface(Reactor& reactor, WifiManager& wifi, SdLogger& sd,
               const Config& config);

  /* begin() — Mount SPIFFS, register routes + WebSocket, start the server. */
  void begin();

  /* update() — Called every main-loop iteration:
   *   - caches statusJson + scanJson for handlers to read,
   *   - applies any queued commands to the reactor/wifi/sd (loop context),
   *   - broadcasts telemetry over the WebSocket at wsPushPeriodMs. */
  void update(const String& statusJson, const String& scanJson);

  /* Cache the calibration JSON (built in the loop) for GET /calibration. */
  void cacheCalJson(const String& calJson);

 private:
  void registerRoutes();
  void applyPending();

  Reactor& reactor_;
  WifiManager& wifi_;
  SdLogger& sd_;
  Config cfg_;

  AsyncWebServer* server_ = nullptr;
  AsyncWebSocket* ws_ = nullptr;

  SemaphoreHandle_t mutex_ = nullptr;
  String statusJson_ = "{}";
  String scanJson_ = "{\"scanning\":false,\"networks\":[]}";
  String calJson_ = "{}";
  uint32_t lastPushMs_ = 0;

  // Pending commands set by async handlers, drained in update() (loop context).
  struct Pending {
    bool runStart = false;
    float runTargetC = 0;
    float runRpm = 0;
    uint16_t runDurMin = 0;
    bool runStop = false;

    bool setTarget = false;
    float setTargetC = 0;
    bool setRpm = false;
    float setRpmVal = 0;

    bool discRpm = false;     float discRpmVal = 0;
    bool discCurrent = false; uint16_t discCurrentMa = 0;
    bool discMicro = false;   uint16_t discMicrosteps = 0;
    bool discDir = false;     bool discReverse = false;
    bool discEnable = false;  bool discEnableVal = false;

    bool pidGains = false;  float pidKp = 0, pidKi = 0, pidKd = 0;
    bool pidMode = false;   String pidModeStr;
    bool autotuneStart = false;
    bool autotuneCancel = false;

    bool calPoint = false;   float calRefC = 0;
    bool calCompute = false;
    bool calReset = false;
    bool sdErase = false;
    bool motorTest = false;

    bool wifiConnect = false;
    String wifiSsid;
    String wifiPass;
    bool wifiForget = false;
    bool wifiScan = false;
    bool logClear = false;
  } pending_;
};
