/*
 * Thermistor.cpp — NTC sensor (ADC + calibration model).
 * See include/sensor/Thermistor.hpp and AppConfig::Thermal.
 */

#include "features/sensor/Thermistor.hpp"

#include <math.h>

Thermistor::Thermistor(const Config& config) : cfg_(config) {}

void Thermistor::begin() {
  analogReadResolution(12);
  analogSetPinAttenuation(cfg_.adcPin, ADC_11db);  // full ~0..3.3V range
  cal_.begin({cfg_.r0Ohms, cfg_.beta, cfg_.t0C}, cfg_.minPlausibleC, cfg_.maxPlausibleC);
  loadCalibration();
}

uint32_t Thermistor::readMilliVolts() {
  uint32_t sum = 0;
  for (int i = 0; i < cfg_.samples; i++) sum += analogReadMilliVolts(cfg_.adcPin);
  return sum / cfg_.samples;
}

uint16_t Thermistor::readRawAdc() {
  uint32_t sum = 0;
  for (int i = 0; i < cfg_.samples; i++) sum += analogRead(cfg_.adcPin);
  return static_cast<uint16_t>(sum / cfg_.samples);
}

float Thermistor::readResistanceOhms() {
  const float vAdc = static_cast<float>(readMilliVolts());
  if (vAdc <= 1.0f || vAdc >= cfg_.vSupplyMv - 1.0f) return NAN;  // rail: short / open
  const float r = cfg_.seriesOhms * vAdc / (cfg_.vSupplyMv - vAdc);
  if (!cal_.plausibleResistance(r)) return NAN;                  // out-of-band: probe fault
  return r;
}

float Thermistor::readCelsius() {
  const float r = readResistanceOhms();
  if (isnan(r)) return NAN;
  return cal_.resistanceToCelsius(r);
}

bool Thermistor::addCalibrationPoint(float referenceC) {
  const float r = readResistanceOhms();
  if (isnan(r)) return false;
  return cal_.addPoint(referenceC, r);
}

bool Thermistor::computeCalibration() {
  if (!cal_.compute()) return false;
  persistCalibration();
  return true;
}

void Thermistor::resetCalibration() {
  cal_.reset();
  persistCalibration();
}

void Thermistor::loadCalibration() {
  prefs_.begin(cfg_.prefsNamespace, false);
  if (!prefs_.getBool("cal", false)) return;  // no stored user calibration
  // Restore by re-loading the captured points and re-fitting — reproduces the exact
  // offset/Beta/Steinhart model without serializing each coefficient set.
  const int n = prefs_.getInt("npts", 0);
  for (int i = 0; i < n; i++) {
    char kr[8], kt[8];
    snprintf(kr, sizeof(kr), "r%d", i);
    snprintf(kt, sizeof(kt), "t%d", i);
    cal_.addPoint(prefs_.getFloat(kt, 0.0f), prefs_.getFloat(kr, 0.0f));
  }
  if (n > 0) cal_.compute();
}

void Thermistor::persistCalibration() {
  prefs_.clear();  // drop stale point keys; the "cal" namespace is calibration-only
  const ThermistorCalibration& c = cal_;
  prefs_.putBool("cal", c.calibrated());
  prefs_.putInt("npts", c.pointCount());
  for (int i = 0; i < c.pointCount(); i++) {
    char kr[8], kt[8];
    snprintf(kr, sizeof(kr), "r%d", i);
    snprintf(kt, sizeof(kt), "t%d", i);
    prefs_.putFloat(kr, c.pointR(i));
    prefs_.putFloat(kt, c.pointRefC(i));
  }
}
