/*
 * Heater.hpp — Heater MOSFET device (Q1, low-side N-channel switch), PWM-driven.
 *
 * Board topology:
 *   +12V ──[ heater element ]── HEATER- ──► Q1 drain
 *   HEATER_PWM (GPIO40) ──[ 47R ]── Q1 gate   (R25 100k pulldown: off at boot)
 *   Q1 source ──[ R26 0.05R ]── GND
 *
 * The gate is driven by an LEDC PWM channel so the thermal PID can modulate
 * average power (0..1 duty). Implementation: src/heater/Heater.cpp
 */

#pragma once

#include <Arduino.h>

class Heater {
 public:
  // Config — built from AppConfig::Heater.
  struct Config {
    int gatePin = -1;
    uint32_t freqHz = 1000;
    int resBits = 10;
  };

  explicit Heater(const Config& config);

  /* begin() — Set up the LEDC PWM channel on the gate pin; start fully off. */
  void begin();

  /* setDuty() — Average heater power, 0.0 (off) .. 1.0 (full on). Clamped. */
  void setDuty(float duty);

  /* off() — setDuty(0). */
  void off();

  /* duty() — Last commanded duty, 0.0 .. 1.0. */
  float duty() const { return duty_; }

 private:
  Config cfg_;
  float duty_ = 0.0f;
  uint32_t maxCount_ = 1023;
};
