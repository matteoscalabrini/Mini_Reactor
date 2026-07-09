/*
 * WifiManager.hpp — WiFi connection core (STA + AP captive-portal onboarding).
 *
 * Distilled from the barebone firmware's WiFi runtime (the connection FSM only;
 * no BLE, MQTT, ESP-NOW, or Matter). Behaviour:
 *   - begin(): load stored credentials; if present, attempt STA connect, else
 *     start the setup AP with a captive-portal DNS responder.
 *   - poll(): non-blocking FSM — handles connect timeout, periodic reconnect,
 *     AP fallback on STA loss, captive-portal DNS, and async scan servicing.
 *   - connect(): initiate a new STA connection (used by the web onboarding form);
 *     credentials are persisted (password AES-encrypted via NvsAes) on success.
 *
 * Implementation: src/net/WifiManager.cpp
 */

#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <Preferences.h>

#include "net/WifiWatchdog.hpp"

class WifiManager {
 public:
  // Config — built from AppConfig::Wifi.
  struct Config {
    const char* hostname = "device";
    const char* apSsid = "Setup";
    const char* apPassword = "";
    uint32_t connectTimeoutMs = 15000;
    uint32_t reconnectIntervalMs = 20000;
    uint32_t apFallbackDelayMs = 20000;
    uint32_t apRetryIntervalMs = 300000;  // while on setup AP, how often to briefly retry the saved network
    bool     txWatchdogEnabled = true;    // self-heal the WiFi TX-buffer wedge
    uint32_t txStallMs = 30000;           // associated + sending but 0 landing this long => restart stack
    uint32_t txRecoverBackoffMs = 30000;
    uint8_t maxScanResults = 16;
    const char* prefsNamespace = "wifi";
    const char* prefsSsidKey = "ssid";
    const char* prefsPassKey = "pass";
  };

  explicit WifiManager(const Config& config);

  void begin();
  void poll();

  /* connect() — Begin a STA connection to the given network. On success the
   * credentials are saved (password encrypted). Returns true if the attempt
   * was started. */
  bool connect(const String& ssid, const String& password);

  /* forget() — Clear stored credentials and drop to AP mode. */
  void forget();

  /* requestScan() — Ask for an async scan; results land in scanJson() shortly. */
  void requestScan();

  /* pollWatchdog() — Drive the TX-wedge detector with the ESP-NOW send counters
   * (the TX whose result we can observe). Returns true on the tick it just
   * restarted the WiFi stack, so the caller can re-init ESP-NOW. No-op unless
   * txWatchdogEnabled. */
  bool pollWatchdog(uint32_t now, uint32_t txAttempts, uint32_t txOks);

  /* recoveryCount() — number of WiFi-stack self-heals since boot (exposed in
   * telemetry so the wedge/recovery is visible over WiFi without serial). */
  uint32_t recoveryCount() const { return recoveryCount_; }

  bool staConnected() const;
  bool apActive() const { return apActive_; }
  String ipAddress() const;

  /* JSON for the web API (built with ArduinoJson in the .cpp). */
  String statusJson() const;
  String scanJson();

 private:
  void startAccessPoint();
  void stopAccessPoint();
  void recoverStack();   // cycle the WiFi driver to reclaim the wedged TX-buffer pool
  void beginConnect(const String& ssid, const String& password);
  void loadCredentials();
  void saveCredentials(const String& ssid, const String& password);
  void serviceScan();  // run every poll, regardless of connection state

  Config cfg_;
  Preferences prefs_;
  DNSServer dns_;
  WifiWatchdog watchdog_;
  uint32_t recoveryCount_ = 0;   // WiFi-stack self-heals since boot

  String ssid_;
  String password_;
  bool apActive_ = false;
  bool connecting_ = false;
  bool wasConnected_ = false;

  uint32_t connectStartedMs_ = 0;
  uint32_t lastReconnectMs_ = 0;
  uint32_t staLostMs_ = 0;

  bool scanRequested_ = false;
  bool scanning_ = false;
  uint32_t scanStartedMs_ = 0;   // for the scan watchdog timeout
  uint32_t lastScanDoneMs_ = 0;  // cooldown anchor so completed results stay readable
  String scanJson_ = "{\"scanning\":false,\"networks\":[]}";
};
