/*
 * Thermistor.cpp — NTC temperature sensor via ADC + Beta equation.
 * See include/sensor/Thermistor.hpp and AppConfig::Thermal.
 */

#include "sensor/Thermistor.hpp"

#include <math.h>

Thermistor::Thermistor(const Config& config) : cfg_(config) {}

void Thermistor::begin() {
  analogReadResolution(12);
  analogSetPinAttenuation(cfg_.adcPin, ADC_11db);  // full ~0..3.3V range
}

uint32_t Thermistor::readMilliVolts() {
  uint32_t sum = 0;
  for (int i = 0; i < cfg_.samples; i++) {
    sum += analogReadMilliVolts(cfg_.adcPin);
  }
  return sum / cfg_.samples;
}

float Thermistor::readCelsius() {
  const float vAdc = static_cast<float>(readMilliVolts());

  // Guard against open/short probe: node pinned near rail or ground.
  if (vAdc <= 1.0f || vAdc >= cfg_.vSupplyMv - 1.0f) {
    return NAN;
  }

  // High-side divider: R_ntc = Rseries * Vadc / (Vsupply - Vadc).
  const float rNtc = cfg_.seriesOhms * vAdc / (cfg_.vSupplyMv - vAdc);

  // Beta equation. T0 in kelvin.
  const float t0K = cfg_.t0C + 273.15f;
  const float invT = 1.0f / t0K + (1.0f / cfg_.beta) * logf(rNtc / cfg_.r0Ohms);
  return (1.0f / invT) - 273.15f;
}
