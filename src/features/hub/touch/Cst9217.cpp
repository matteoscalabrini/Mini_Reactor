#include "features/hub/touch/Cst9217.hpp"
#include <Arduino.h>
#include <algorithm>
#include "app_config.hpp"

// ── CST9217/CST9220 register constants (verbatim from Barebone HubFeature.cpp) ──
static constexpr uint16_t kCst92xxReadCommand   = 0xD000;
static constexpr uint8_t  kCst92xxAck           = 0xAB;
static constexpr uint16_t kCst92xxRegDebugMode  = 0xD101;
static constexpr uint16_t kCst92xxRegSleepMode  = 0xD105;
static constexpr uint16_t kCst9217ChipId        = 0x9217;
static constexpr uint16_t kCst9220ChipId        = 0x9220;
static constexpr uint8_t  kMaxTouchPoints       = 2;

Cst9217::Cst9217(TwoWire& wire, uint8_t address)
    : wire_(wire), address_(address) {}

// ── I2C helpers (ported verbatim from HubFeature::readRegister16/writeRegister16) ──
bool Cst9217::readReg16(uint16_t reg, uint8_t* buf, size_t n) {
  wire_.beginTransmission(address_);
  wire_.write(static_cast<uint8_t>(reg >> 8));
  wire_.write(static_cast<uint8_t>(reg & 0xFF));
  if (wire_.endTransmission(false) != 0) return false;
  const size_t count = wire_.requestFrom(static_cast<int>(address_), static_cast<int>(n));
  if (count != n) return false;
  for (size_t i = 0; i < n; ++i) buf[i] = static_cast<uint8_t>(wire_.read());
  return true;
}

bool Cst9217::writeReg16(uint16_t reg, const uint8_t* data, size_t n) {
  wire_.beginTransmission(address_);
  wire_.write(static_cast<uint8_t>(reg >> 8));
  wire_.write(static_cast<uint8_t>(reg & 0xFF));
  for (size_t i = 0; i < n; ++i) wire_.write(data[i]);
  return wire_.endTransmission() == 0;
}

// ── begin() — ported verbatim from HubFeature::initializeCst9217() ──
bool Cst9217::begin() {
  // Probe
  wire_.beginTransmission(address_);
  state_.present = (wire_.endTransmission() == 0);
  state_.ready = false;
  if (!state_.present) {
    Serial.printf("[CST9217] not found at 0x%02X\n", address_);
    return false;
  }

  // GPIO reset
  pinMode(AppConfig::PinoutHub::kHubTouchReset, OUTPUT);
  pinMode(AppConfig::PinoutHub::kHubTouchInt, INPUT_PULLUP);
  digitalWrite(AppConfig::PinoutHub::kHubTouchReset, LOW);
  delay(10);
  digitalWrite(AppConfig::PinoutHub::kHubTouchReset, HIGH);
  delay(30);

  // Enter debug/command mode
  if (!writeReg16(kCst92xxRegDebugMode, nullptr, 0)) {
    Serial.println("[CST9217] command mode failed");
    return false;
  }

  // Read resolution
  uint8_t buffer[8] = {};
  if (!readReg16(0xD1F8, buffer, 4)) {
    Serial.println("[CST9217] resolution read failed");
    return false;
  }
  // resolutionX/Y not stored in State — used only for logging
  const uint16_t resX = static_cast<uint16_t>((buffer[1] << 8) | buffer[0]);
  const uint16_t resY = static_cast<uint16_t>((buffer[3] << 8) | buffer[2]);

  // Read chip ID
  if (!readReg16(0xD204, buffer, 4)) {
    Serial.println("[CST9217] chip id read failed");
    return false;
  }
  state_.chipId = static_cast<uint16_t>((buffer[3] << 8) | buffer[2]);

  // Validate chip ID
  if (state_.chipId != kCst9217ChipId && state_.chipId != kCst9220ChipId) {
    Serial.printf("[CST9217] chip id mismatch: 0x%04X\n", state_.chipId);
    return false;
  }

  // Initial status read
  State tmp{};
  if (!refresh(tmp)) return false;

  state_.ready = true;
  Serial.printf("[CST9217] ok chipId=0x%04X res=%ux%u\n", state_.chipId, resX, resY);
  return true;
}

// ── refresh() — ported verbatim from HubFeature::refreshCst9217Status() ──
bool Cst9217::refresh(State& out) {
  for (TouchPoint& pt : state_.points) pt = TouchPoint{};
  state_.pointCount = 0;

  uint8_t readBuffer[(kMaxTouchPoints * 5) + 5] = {};
  if (!readReg16(kCst92xxReadCommand, readBuffer, sizeof(readBuffer))) {
    state_.ready = false;
    out = state_;
    return false;
  }

  const uint8_t ackPayload = kCst92xxAck;
  writeReg16(kCst92xxReadCommand, &ackPayload, 1);  // best-effort ACK

  if (readBuffer[6] == kCst92xxAck) {
    const uint8_t rawCount = readBuffer[5] & 0x7F;
    state_.pointCount = std::min<uint8_t>(rawCount, kMaxTouchPoints);
    for (uint8_t i = 0; i < state_.pointCount; ++i) {
      uint8_t* data = readBuffer + (i * 5) + (i == 0 ? 0 : 2);
      const uint8_t event  = static_cast<uint8_t>(data[0] & 0x0F);
      const uint16_t rawX  = static_cast<uint16_t>((data[1] << 4) | (data[3] >> 4));
      const uint16_t rawY  = static_cast<uint16_t>((data[2] << 4) | (data[3] & 0x0F));
      TouchPoint& pt = state_.points[i];
      pt.valid = (event == 0x06);
      int16_t mx = static_cast<int16_t>(rawX);
      int16_t my = static_cast<int16_t>(rawY);
      HubTouchTransform::Mapping mapping;
      mapping.swapXY  = AppConfig::HubTouch::kSwapXY;
      mapping.mirrorX = AppConfig::HubTouch::kMirrorX;
      mapping.mirrorY = AppConfig::HubTouch::kMirrorY;
      HubTouchTransform::apply(mx, my,
                               AppConfig::Hub::kTouchWidth,
                               AppConfig::Hub::kTouchHeight,
                               mapping);
      pt.x = mx;
      pt.y = my;
    }
  }

  out = state_;
  return true;
}

// ── readPoint() — convenience for LVGL indev callback ──
bool Cst9217::readPoint(int16_t& x, int16_t& y, bool& pressed) {
  if (state_.pointCount > 0 && state_.points[0].valid) {
    x = state_.points[0].x;
    y = state_.points[0].y;
    pressed = true;
    return true;
  }
  pressed = false;
  return false;
}

// ── enterSleep() — ported from HubFeature sleepCst9217 path ──
bool Cst9217::enterSleep() {
  if (!writeReg16(kCst92xxRegDebugMode, nullptr, 0)) return false;
  if (!writeReg16(kCst92xxRegSleepMode, nullptr, 0)) return false;
  state_.ready = false;
  state_.pointCount = 0;
  for (TouchPoint& pt : state_.points) pt = TouchPoint{};
  return true;
}
