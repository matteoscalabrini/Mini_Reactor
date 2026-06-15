/*
 * Tmc2209Motor.hpp — TMC2209 stepper driver device, single-wire UART control.
 *
 * The TMC2209 (U4) is wired for single-wire UART:
 *   ESP TX (GPIO17 / U1TXD) ── 1k (R14) ──┐
 *                                          ├──► TMC PDN_UART (single wire)
 *   ESP RX (GPIO18 / U1RXD) ───────────────┘
 * The 1k series resistor on TX lets both directions share the one PDN line.
 * Address pins MS1/MS2 are tied to GND (UART address 0); sense resistors 0.11Ω.
 *
 * Construct with a Config (built from AppConfig in AppRuntime). The driver spins
 * the motor over UART via the internal step generator (VACTUAL) — no STEP/DIR
 * pulses required.
 *
 * Implementation: src/motor/Tmc2209Motor.cpp
 */

#pragma once

#include <Arduino.h>
#include <TMCStepper.h>

class Tmc2209Motor {
 public:
  // ─── CONFIG STRUCT ─────────────────────────────────────────────────────────
  /*
   * Config — all driver parameters, built from AppConfig::Motor.
   *   serial            : HardwareSerial wired to PDN_UART (e.g. &Serial1).
   *   pinUartTx/Rx      : UART TX/RX GPIOs (TX through the 1k bridge resistor).
   *   pinEnable         : EN/ENN pin, active-low.
   *   pinStep/Dir       : STEP/DIR GPIOs (idle here; motion is via VACTUAL).
   *   pinDiag/Index     : DIAG/INDEX status inputs.
   *   rSenseOhms        : current-sense resistor value (0.11Ω on this board).
   *   address           : UART slave address (0 — MS1/MS2 to GND).
   *   uartBaud          : PDN_UART baud rate.
   *   currentMilliamps  : RMS coil current target.
   *   microsteps        : 0/2/4/.../256 (0 = full step).
   */
  struct Config {
    HardwareSerial* serial = nullptr;
    int pinUartTx = -1;
    int pinUartRx = -1;
    int pinEnable = -1;
    int pinStep   = -1;
    int pinDir    = -1;
    int pinDiag   = -1;
    int pinIndex  = -1;
    float rSenseOhms = 0.11f;
    uint8_t address = 0;
    uint32_t uartBaud = 115200;
    uint16_t currentMilliamps = 600;
    uint16_t microsteps = 16;
    float topSpeedMicrostepHz = 3200.0f;  // 100% setSpeedPercent maps here
    uint16_t stepsPerRev = 200;           // full steps/rev (direct 1:1 drivetrain)
  };

  explicit Tmc2209Motor(const Config& config);

  /*
   * begin() — Start the UART, configure quiet stealthChop motion, and verify
   * the link. Leaves the motor enabled but stationary (VACTUAL = 0).
   * Returns true if the driver answered with the expected version.
   */
  bool begin();

  /* connected() — Re-check the UART link (test_connection() == 0). */
  bool connected();

  /* version() — Chip version byte (0x21 for TMC2209). */
  uint8_t version();

  /* enable() — Drive the EN pin (active-low). */
  void enable(bool on);

  /* setCurrentMilliamps() — RMS coil current target. */
  void setCurrentMilliamps(uint16_t milliamps);

  /* setMicrosteps() — 0/2/4/.../256. */
  void setMicrosteps(uint16_t microsteps);

  /*
   * spin() — Continuous rotation over UART via VACTUAL.
   *   velocity > 0 forward, < 0 reverse, 0 stop. Magnitude in VACTUAL units
   *   (1 unit ≈ 0.715 microsteps/s); use vactualForMicrostepHz() to convert.
   */
  void spin(int32_t velocity);

  /* stop() — spin(0). */
  void stop();

  /*
   * setSpeedPercent() — Spin at a fraction of the configured top speed.
   *   percent 0..100 maps to 0..topSpeedMicrostepHz (forward). Negative
   *   reverses. Convenience wrapper over spin() for UI-driven control.
   */
  void setSpeedPercent(float percent);

  /* setRpm() — Spin the disc at an absolute rpm (0 = stop). Uses the current
   * microsteps + stepsPerRev; honours the direction set by setDirection(). */
  void setRpm(float rpm);

  /* setDirection() — false = cw (forward), true = ccw (reverse). Re-applies the
   * current speed in the new direction if already spinning. */
  void setDirection(bool reverse);

  // Drive-state getters (for telemetry).
  float    rpm() const { return lastRpm_; }
  bool     reversed() const { return reverse_; }
  bool     enabledState() const { return enabled_; }
  uint16_t currentMilliamps() const { return currentMa_; }
  uint16_t microstepsValue() const { return cfg_.microsteps; }

  /* drvStatus() — Raw DRV_STATUS (over-temp, open-load, stall, etc.). */
  uint32_t drvStatus();

  /* Underlying TMCStepper object for advanced use. */
  TMC2209Stepper& driver() { return driver_; }

  /* Convert microsteps/second to a VACTUAL value (fCLK = 12 MHz). */
  static int32_t vactualForMicrostepHz(float microstepsPerSecond);

 private:
  Config cfg_;
  TMC2209Stepper driver_;
  float lastRpm_ = 0.0f;
  bool reverse_ = false;
  bool enabled_ = false;
  uint16_t currentMa_ = 600;
};
