/*
 * Heater.cpp — Heater MOSFET gate driver via LEDC PWM.
 * See include/heater/Heater.hpp and AppConfig::Heater.
 */

#include "features/heater/Heater.hpp"

Heater::Heater(const Config& config) : cfg_(config) {}

void Heater::begin() {
  maxCount_ = (1u << cfg_.resBits) - 1u;
  // Arduino-ESP32 3.x: pin-based LEDC (channel auto-assigned). Returns false if
  // no LEDC timer/channel could be bound — log it, the toggle-contract way.
  const bool ok = ledcAttach(cfg_.gatePin, cfg_.freqHz, cfg_.resBits);
  Serial.printf("[Heater] LEDC %s on GPIO%d @ %uHz/%ubit\n",
                ok ? "attached" : "ATTACH FAILED", cfg_.gatePin, cfg_.freqHz, cfg_.resBits);
  off();
}

void Heater::setDuty(float duty) {
  if (duty < 0.0f) duty = 0.0f;
  if (duty > 1.0f) duty = 1.0f;
  duty_ = duty;
  ledcWrite(cfg_.gatePin, static_cast<uint32_t>(duty * maxCount_ + 0.5f));
}

void Heater::off() { setDuty(0.0f); }
