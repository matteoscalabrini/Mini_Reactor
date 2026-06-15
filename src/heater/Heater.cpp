/*
 * Heater.cpp — Heater MOSFET gate driver via LEDC PWM.
 * See include/heater/Heater.hpp and AppConfig::Heater.
 */

#include "heater/Heater.hpp"

Heater::Heater(const Config& config) : cfg_(config) {}

void Heater::begin() {
  maxCount_ = (1u << cfg_.resBits) - 1u;
  ledcSetup(cfg_.ledcChannel, cfg_.freqHz, cfg_.resBits);
  ledcAttachPin(cfg_.gatePin, cfg_.ledcChannel);
  off();
}

void Heater::setDuty(float duty) {
  if (duty < 0.0f) duty = 0.0f;
  if (duty > 1.0f) duty = 1.0f;
  duty_ = duty;
  ledcWrite(cfg_.ledcChannel, static_cast<uint32_t>(duty * maxCount_ + 0.5f));
}

void Heater::off() { setDuty(0.0f); }
