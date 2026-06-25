/*
 * Ds18b20.cpp — async DS18B20 1-Wire temperature read.
 * See include/sensor/Ds18b20.hpp and AppConfig::Sense / AppConfig::Thermal.
 */

#include "features/sensor/Ds18b20.hpp"

#include <DallasTemperature.h>
#include <OneWire.h>

Ds18b20::Ds18b20(const Config& config) : cfg_(config) {}

void Ds18b20::begin() {
  wire_ = new OneWire(cfg_.pin);
  dt_ = new DallasTemperature(wire_);
  dt_->begin();
  dt_->setResolution(cfg_.resolutionBits);
  dt_->setWaitForConversion(false);  // non-blocking: we poll for completion
  dt_->requestTemperatures();
  requestedMs_ = millis();
  pending_ = true;
}

bool Ds18b20::update() {
  if (!dt_ || !pending_) return false;
  if (millis() - requestedMs_ < cfg_.conversionMs) return false;

  const float c = dt_->getTempCByIndex(0);
  lastC_ = (c <= DEVICE_DISCONNECTED_C) ? NAN : c;

  // Kick the next conversion immediately (continuous sampling).
  dt_->requestTemperatures();
  requestedMs_ = millis();
  return true;
}
