/*
 * WifiManager.cpp — WiFi connection FSM (STA + AP captive portal).
 * See include/net/WifiManager.hpp and AppConfig::Wifi.
 */

#include "net/WifiManager.hpp"

#include <ArduinoJson.h>
#include <WiFi.h>

#include "security/NvsAes.hpp"

namespace {
constexpr uint8_t kDnsPort = 53;
}  // namespace

WifiManager::WifiManager(const Config& config) : cfg_(config) {}

void WifiManager::begin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(cfg_.hostname);
  WiFi.setAutoReconnect(false);  // we manage reconnection ourselves

  loadCredentials();
  if (ssid_.length() > 0) {
    beginConnect(ssid_, password_);
  } else {
    startAccessPoint();
  }
}

void WifiManager::loadCredentials() {
  prefs_.begin(cfg_.prefsNamespace, false);
  ssid_ = prefs_.getString(cfg_.prefsSsidKey, "");
  password_ = NvsAes::readCredential(prefs_, cfg_.prefsPassKey, "");
}

void WifiManager::saveCredentials(const String& ssid, const String& password) {
  prefs_.putString(cfg_.prefsSsidKey, ssid);
  NvsAes::writeCredential(prefs_, cfg_.prefsPassKey, password);
}

void WifiManager::startAccessPoint() {
  if (apActive_) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(cfg_.apSsid, strlen(cfg_.apPassword) ? cfg_.apPassword : nullptr);
  dns_.setErrorReplyCode(DNSReplyCode::NoError);
  dns_.start(kDnsPort, "*", WiFi.softAPIP());
  apActive_ = true;
  Serial.printf("[WIFI] AP '%s' up at %s (captive portal)\n", cfg_.apSsid,
                WiFi.softAPIP().toString().c_str());
}

void WifiManager::stopAccessPoint() {
  if (!apActive_) return;
  dns_.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apActive_ = false;
  Serial.println("[WIFI] AP stopped");
}

void WifiManager::beginConnect(const String& ssid, const String& password) {
  Serial.printf("[WIFI] connecting to '%s' ...\n", ssid.c_str());
  WiFi.begin(ssid.c_str(), password.c_str());
  connecting_ = true;
  connectStartedMs_ = millis();
}

bool WifiManager::connect(const String& ssid, const String& password) {
  if (ssid.isEmpty()) return false;
  ssid_ = ssid;
  password_ = password;
  beginConnect(ssid_, password_);
  return true;
}

void WifiManager::forget() {
  prefs_.remove(cfg_.prefsSsidKey);
  prefs_.remove(cfg_.prefsPassKey);
  ssid_ = "";
  password_ = "";
  WiFi.disconnect(false, true);
  wasConnected_ = false;
  connecting_ = false;
  startAccessPoint();
}

void WifiManager::requestScan() { scanRequested_ = true; }

bool WifiManager::staConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

String WifiManager::ipAddress() const {
  if (staConnected()) return WiFi.localIP().toString();
  if (apActive_) return WiFi.softAPIP().toString();
  return "0.0.0.0";
}

void WifiManager::poll() {
  if (apActive_) dns_.processNextRequest();

  const uint32_t now = millis();
  const bool connected = (WiFi.status() == WL_CONNECTED);

  serviceScan();  // scans must work regardless of connection state (was unreachable when connected)

  // ── Connecting → resolve success or timeout ──
  if (connecting_) {
    if (connected) {
      connecting_ = false;
      wasConnected_ = true;
      saveCredentials(ssid_, password_);
      Serial.printf("[WIFI] connected, IP %s\n", WiFi.localIP().toString().c_str());
      stopAccessPoint();  // onboarding done — drop the setup AP
    } else if (now - connectStartedMs_ > cfg_.connectTimeoutMs) {
      connecting_ = false;
      lastReconnectMs_ = now;
      Serial.println("[WIFI] connect timed out");
      startAccessPoint();  // keep onboarding reachable
    }
    return;
  }

  // ── Connected → watch for loss ──
  if (connected) {
    if (!wasConnected_) wasConnected_ = true;
    staLostMs_ = 0;
    return;
  }

  // ── Disconnected ──
  if (wasConnected_) {
    wasConnected_ = false;
    staLostMs_ = now;
    Serial.println("[WIFI] connection lost");
  }

  // Periodic reconnect if we have credentials.
  if (ssid_.length() > 0 && now - lastReconnectMs_ > cfg_.reconnectIntervalMs) {
    lastReconnectMs_ = now;
    beginConnect(ssid_, password_);
    return;
  }

  // AP fallback after the grace period so onboarding stays available.
  if (!apActive_ && staLostMs_ != 0 && now - staLostMs_ > cfg_.apFallbackDelayMs) {
    startAccessPoint();
  }

}

// Service an async WiFi scan. Called every poll() regardless of connection state.
void WifiManager::serviceScan() {
  static constexpr uint32_t kScanCooldownMs = 4000;
  const uint32_t now = millis();

  // Start a queued scan only when idle AND past the cooldown — a burst of polling GETs
  // (each re-arms scanRequested_) must not restart the scan back-to-back, which would hide
  // the freshly-completed results before the client can read them.
  if (scanRequested_ && !scanning_ && now - lastScanDoneMs_ > kScanCooldownMs) {
    scanRequested_ = false;
    scanning_ = true;
    WiFi.scanNetworks(true /*async*/, true /*show hidden*/);
  }
  if (scanning_) {
    const int n = WiFi.scanComplete();
    if (n >= 0) {
      JsonDocument doc;
      doc["scanning"] = false;
      JsonArray nets = doc["networks"].to<JsonArray>();
      const int limit = min(n, (int)cfg_.maxScanResults);
      for (int i = 0; i < limit; i++) {
        JsonObject o = nets.add<JsonObject>();
        o["ssid"] = WiFi.SSID(i);
        o["rssi"] = WiFi.RSSI(i);
        o["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
      }
      scanJson_ = "";
      serializeJson(doc, scanJson_);
      WiFi.scanDelete();
      scanning_ = false;
      lastScanDoneMs_ = now;
    } else if (n == WIFI_SCAN_FAILED) {  // -2: don't get stuck "scanning" forever
      scanning_ = false;
      lastScanDoneMs_ = now;
    }
  }
}

String WifiManager::statusJson() const {
  JsonDocument doc;
  const bool connected = staConnected();
  doc["connected"] = connected;
  doc["ap"] = apActive_;
  doc["hostname"] = cfg_.hostname;
  doc["ssid"] = connected ? WiFi.SSID() : ssid_;
  doc["ip"] = ipAddress();
  doc["rssi"] = connected ? WiFi.RSSI() : 0;
  String out;
  serializeJson(doc, out);
  return out;
}

String WifiManager::scanJson() {
  if (scanning_) return "{\"scanning\":true,\"networks\":[]}";
  return scanJson_;
}
