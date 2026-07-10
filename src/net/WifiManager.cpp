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
  // Modem power-save must stay OFF. With PS on (Arduino default) a weak link
  // makes the STA miss DTIM delivery windows: the AP's buffered unicast to us
  // becomes undeliverable (ping/ARP/TCP all die) while beacons still arrive,
  // so WL_CONNECTED never drops and this FSM stays blind — the "network death
  // after 30min-2h" bug. Bench A/B 2026-07-08: PS on = dead in 5.5 min at
  // -82dBm; PS off = clean soak at the same RSSI. Espressif also requires PS
  // off for reliable ESP-NOW RX (the HUB link).
  WiFi.setSleep(false);
  WiFi.setHostname(cfg_.hostname);
  WiFi.setAutoReconnect(false);  // we manage reconnection ourselves

  WifiWatchdog::Config wc;
  wc.stallMs = cfg_.txStallMs;
  wc.backoffMs = cfg_.txRecoverBackoffMs;
  watchdog_.setConfig(wc);
  Serial.printf("[WIFI] TX-wedge watchdog %s (stall %lus, needs ESP-NOW TX as its probe)\n",
                cfg_.txWatchdogEnabled ? "enabled" : "disabled",
                (unsigned long)(cfg_.txStallMs / 1000));

  loadCredentials();
  if (ssid_.length() > 0) {
    beginConnect(ssid_, password_);
  } else {
    startAccessPoint();
  }
}

bool WifiManager::pollWatchdog(uint32_t now, uint32_t txAttempts, uint32_t txOks) {
  if (!cfg_.txWatchdogEnabled) return false;
  // Only meaningful while associated; watchdog_ ignores the down/idle cases.
  if (!watchdog_.update(now, staConnected(), txAttempts, txOks)) return false;
  recoverStack();
  return true;
}

void WifiManager::recoverStack() {
  // WiFi driver TX-buffer pool wedged (associated but nothing transmits). Cycle
  // the driver OFF->STA to reclaim it, then re-associate via the normal FSM.
  // NEVER ESP.restart() — a run may be active. ESP-NOW is rebuilt by the caller.
  ++recoveryCount_;
  Serial.printf("[WIFI] TX path wedged (associated, no TX landing) — restarting WiFi stack (#%u)\n",
                (unsigned)recoveryCount_);
  WiFi.disconnect(false, false);
  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setHostname(cfg_.hostname);
  wasConnected_ = false;
  staLostMs_ = 0;
  connecting_ = false;
  if (ssid_.length() > 0) beginConnect(ssid_, password_);
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
  // AP-ONLY (not AP_STA): the ESP32 shares one radio, and a co-running STA that
  // keeps scanning/associating starves the softAP data path — the portal becomes
  // unreachable (TCP SYNs time out even though the AP beacons + DHCP still work).
  // Auto-recovery happens by *switching* back to STA in poll(), never coexisting.
  WiFi.mode(WIFI_AP);
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
  credsDirty_ = true;
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
      // Persist only creds that came in via connect() — reconnects and self-heals
      // reuse the stored pair, and rewriting it each time just wears NVS.
      if (credsDirty_) { saveCredentials(ssid_, password_); credsDirty_ = false; }
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

  // ── Setup AP is up → hold a STABLE, AP-only portal ──
  // STA is OFF here (see startAccessPoint). We must NOT drive reconnects while the
  // AP serves clients, or the shared radio's STA activity makes the portal
  // unreachable. Auto-recover only by fully switching AP → STA and back — never
  // coexisting — and only occasionally (apRetryIntervalMs).
  if (apActive_) {
    if (ssid_.length() > 0 && now - lastReconnectMs_ > cfg_.apRetryIntervalMs) {
      lastReconnectMs_ = now;
      Serial.println("[WIFI] AP retry: dropping AP to try saved network");
      stopAccessPoint();               // full mode switch AP → STA (no coexistence)
      beginConnect(ssid_, password_);  // connecting_ FSM re-raises the AP on timeout
    }
    return;
  }

  // ── STA-only reconnect (safe: no AP up, so no radio contention) ──
  if (ssid_.length() > 0 && now - lastReconnectMs_ > cfg_.reconnectIntervalMs) {
    lastReconnectMs_ = now;
    beginConnect(ssid_, password_);
    return;
  }

  // Raise the setup AP after the grace period so onboarding stays available.
  if (staLostMs_ != 0 && now - staLostMs_ > cfg_.apFallbackDelayMs) {
    startAccessPoint();
  }
}

// Service an async WiFi scan. Called every poll() regardless of connection state.
void WifiManager::serviceScan() {
  static constexpr uint32_t kScanCooldownMs = 4000;
  // A connected, all-channel scan takes several seconds (~6 s observed on
  // hardware). Watchdog must comfortably exceed that.
  static constexpr uint32_t kScanTimeoutMs = 15000;
  const uint32_t now = millis();

  // Start a queued scan only when idle AND past the cooldown — a burst of polling GETs
  // (each re-arms scanRequested_) must not restart the scan back-to-back, which would hide
  // the freshly-completed results before the client can read them.
  if (scanRequested_ && !scanning_ && now - lastScanDoneMs_ > kScanCooldownMs) {
    scanRequested_ = false;
    Serial.println("[WIFI] scan started");
    if (WiFi.scanNetworks(true /*async*/, true /*show hidden*/) == WIFI_SCAN_FAILED) {
      lastScanDoneMs_ = now;  // couldn't even start (radio busy) — let the cooldown gate retries
      Serial.println("[WIFI] scan failed to start");
    } else {
      scanning_ = true;
      scanStartedMs_ = now;
    }
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
      Serial.printf("[WIFI] scan done: %d networks\n", n);
    } else if (now - scanStartedMs_ > kScanTimeoutMs) {
      // Watchdog only. Do NOT treat a bare WIFI_SCAN_FAILED (-2) as terminal:
      // scanComplete() returns -2 for a brief window right as the radio finishes,
      // before the async SCAN_DONE event publishes results. Bailing on that
      // transient discards a perfectly good scan — wait for results or this timeout.
      WiFi.scanDelete();
      scanning_ = false;
      lastScanDoneMs_ = now;
      Serial.println("[WIFI] scan failed (timeout)");
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
