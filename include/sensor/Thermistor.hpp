/*
 * Thermistor.hpp — NTC thermistor temperature sensor (ADC + Beta equation).
 *
 * High-side divider on this board:
 *   +3.3V ── seriesOhms (R39 10k) ── THERM_ADC node ── NTC ── GND
 * So the NTC resistance is:
 *   R_ntc = seriesOhms * Vadc / (Vsupply - Vadc)
 * and temperature comes from the Beta equation:
 *   1/T = 1/T0 + (1/Beta) * ln(R_ntc / R0)
 *
 * Implementation: src/sensor/Thermistor.cpp
 */

#pragma once

#include <Arduino.h>

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
  };

  explicit Thermistor(const Config& config);

  void begin();

  /* readCelsius() — Sampled temperature in °C. Returns NAN if the divider
   * reading is out of range (open/short — e.g. no probe connected). */
  float readCelsius();

  /* readMilliVolts() — Raw averaged ADC voltage at the divider node. */
  uint32_t readMilliVolts();

 private:
  Config cfg_;
};
