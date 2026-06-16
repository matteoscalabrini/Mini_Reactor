/*
 * Thermistor.hpp — NTC thermistor temperature sensor (ADC + calibrated model).
 *
 * High-side divider on this board:
 *   +3.3V ── seriesOhms (R39 10k) ── THERM_ADC node ── NTC ── GND
 * So the NTC resistance is:
 *   R_ntc = seriesOhms * Vadc / (Vsupply - Vadc)
 * Temperature conversion is routed through ThermistorCalibration (offset /
 * Beta refit / Steinhart-Hart), seeded from the factory Beta parameters below.
 *
 * Implementation: src/sensor/Thermistor.cpp
 */

#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include "sensor/ThermistorCalibration.hpp"

class Thermistor {
 public:
  // Config — built from AppConfig::Thermal.
  struct Config {
    int adcPin = -1;
    float seriesOhms = 10000.0f;  // fixed divider resistor
    float r0Ohms = 10000.0f;      // NTC resistance at t0C
    float beta = 3950.0f;
    float t0C = 25.0f;
    float vSupplyMv = 3300.0f;    // divider top rail
    int samples = 8;              // averaged ADC reads
    const char* prefsNamespace = "cal";
  };

  explicit Thermistor(const Config& config);

  void begin();

  /* readCelsius() — Sampled temperature in °C via the active calibration. NAN on
   * open/short (no probe). */
  float readCelsius();

  /* readMilliVolts() — Raw averaged ADC voltage at the divider node. */
  uint32_t readMilliVolts();

  /* readRawAdc() — Averaged raw 12-bit ADC count (0..4095). */
  uint16_t readRawAdc();

  /* readResistanceOhms() — Computed NTC resistance, or NAN on open/short. */
  float readResistanceOhms();

  /* Calibration ops (return false if the live resistance is faulty / incomplete). */
  bool addCalibrationPoint(float referenceC);  // captures live resistance
  bool computeCalibration();                    // fits + persists on success
  void resetCalibration();                      // factory Beta + persists

  /* Calibration state (for telemetry / GET). */
  const ThermistorCalibration& calibration() const { return cal_; }

 private:
  Config cfg_;
  ThermistorCalibration cal_;
  Preferences prefs_;
  void loadCalibration();
  void persistCalibration();
};
