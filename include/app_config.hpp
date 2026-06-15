/*
 * app_config.hpp — Central compile-time configuration for the Bioreactor Module.
 *
 * Single authoritative source for pin assignments, bus parameters, and device
 * defaults. Organized as nested namespaces inside AppConfig so every constant is
 * unambiguously scoped (e.g. AppConfig::Motor::kStepPin). All values are pulled
 * from the board schematic (Resources/Board Schematic.pdf, ESP32-S3-WROOM-1).
 *
 * Only constexpr values live here — no mutable state, no function definitions.
 * The per-device Config structs in AppRuntime.cpp are built from these constants.
 */

#pragma once

#include <Arduino.h>

#include "power/Husb238.hpp"  // for AppConfig::Pd::kRequestProfile type

namespace AppConfig {

// Serial debug output.
static constexpr uint32_t kSerialBaud = 115200;
static constexpr uint16_t kSerialStartupDelayMs = 3000;  // USB-CDC enumerate

// ── I2C bus ─────────────────────────────────────────────────────────────────
// On-board HUSB238 PD sink (0x08) + external J3 header. 10k pull-ups on board.
namespace I2c {
static constexpr int      kSdaPin  = 1;       // GPIO1
static constexpr int      kSclPin  = 2;       // GPIO2
static constexpr uint32_t kClockHz = 100000;  // 100 kHz standard mode
}  // namespace I2c

// ── USB-PD sink (HUSB238) ────────────────────────────────────────────────────
namespace Pd {
static constexpr uint8_t kAddress = 0x08;  // fixed HUSB238 I2C address
// Voltage to negotiate at boot. The board's +12V rail (motor + heater) is
// sourced from VBUS via the PD-gated PMOS, so request 12V.
static constexpr Husb238::PdSelection kRequestProfile =
    Husb238::PdSelection::PD_SRC_12V;
}  // namespace Pd

// ── SPI bus + micro-SD card (J7) ─────────────────────────────────────────────
// 22R series resistors on SCK/MOSI/CS.
namespace Spi {
static constexpr int kSckPin  = 12;  // GPIO12 -> SD CLK
static constexpr int kMisoPin = 13;  // GPIO13 -> SD DAT0
static constexpr int kMosiPin = 11;  // GPIO11 -> SD CMD
static constexpr int kCsPin   = 10;  // GPIO10 -> SD CS (DAT3/CD)
}  // namespace Spi

namespace Sd {
static constexpr int      kCardDetectPin = 14;        // GPIO14 -> SD DET
static constexpr uint32_t kFreqHz        = 10000000;  // 10 MHz bring-up speed
static constexpr const char* kLogPath    = "/reactor_log.csv";
static constexpr const char* kLogHeader  =
    "t_ms,running,liquid_c,heater_c,setpoint_c,heater_pct,motor_pct,fault,safety";
}  // namespace Sd

// ── TMC2209 stepper driver (U4), single-wire UART ────────────────────────────
namespace Motor {
static constexpr int kUartTxPin = 17;  // GPIO17 U1TXD -> 1k -> PDN_UART
static constexpr int kUartRxPin = 18;  // GPIO18 U1RXD <- PDN_UART
static constexpr int kEnablePin = 21;  // GPIO21 TMC1_ENN (active low)
static constexpr int kStepPin   = 15;  // GPIO15 (unused; we spin via UART)
static constexpr int kDirPin    = 16;  // GPIO16
static constexpr int kDiagPin   = 38;  // GPIO38 (input)
static constexpr int kIndexPin  = 39;  // GPIO39 (input)

static constexpr float    kRSenseOhms       = 0.11f;   // R35/R36
static constexpr uint8_t  kAddress          = 0b00;    // MS1/MS2 -> GND
static constexpr uint32_t kUartBaud         = 115200;
static constexpr uint16_t kCurrentMilliamps = 600;     // RMS, generic NEMA-17
static constexpr uint16_t kMicrosteps       = 16;
// Demo spin speed in microsteps/second (~1 rev/s at 200 steps * 16 microsteps).
static constexpr float    kSpinMicrostepHz  = 3200.0f;
}  // namespace Motor

// ── Heater MOSFET (Q1, low-side NMOS), driven by LEDC PWM ─────────────────────
namespace Heater {
static constexpr int      kGatePin     = 40;    // GPIO40 HEATER_PWM -> 47R -> gate
static constexpr int      kLedcChannel = 0;     // dedicated LEDC channel
static constexpr uint32_t kFreqHz      = 1000;  // 1 kHz — quiet, easy on the FET
static constexpr int      kResBits     = 10;    // 10-bit duty (0..1023)
}  // namespace Heater

// ── Temperature sensors ──────────────────────────────────────────────────────
// Two sensors with different jobs:
//   - DS18B20 (1-Wire on GPIO42): the LIQUID/bath probe — the PID process value.
//   - 10k NTC (ADC on GPIO6): a HEATER-mounted probe used purely as a safety
//     high-limit (independent over-temp cutoff), not for control.
namespace Sense {
static constexpr int kLiquidOneWirePin = 42;  // GPIO42 (IO42, J6) -> DS18B20 data
static constexpr int kHeaterNtcAdcPin  = 6;   // GPIO6 -> heater NTC divider
static constexpr int kHeatIsnsPin      = 5;   // GPIO5 -> heater current sense (0.05R)
}  // namespace Sense

// ── Thermal: liquid-temp PID + heater NTC safety high-limit ──────────────────
// The PID controls the heater duty to the DS18B20 LIQUID temperature. The heater
// NTC is read independently as a safety cutoff. NTC is a high-side divider:
//   +3.3V — R39(10k) — THERM_ADC — NTC — GND
//   R_ntc = kNtcSeriesOhms * Vadc / (Vsupply - Vadc), then Beta equation -> degC.
namespace Thermal {
// Heater NTC (safety probe) conversion.
static constexpr float kNtcSeriesOhms = 10000.0f;  // R39 fixed resistor
static constexpr float kNtcR0Ohms     = 10000.0f;  // NTC nominal resistance @ T0
static constexpr float kNtcBeta       = 3950.0f;   // NTC Beta coefficient
static constexpr float kNtcT0C        = 25.0f;     // NTC nominal temperature
static constexpr float kVSupplyMv     = 3300.0f;   // divider top rail

// PID (output is heater duty 0..1). Conservative starting gains for a slow
// liquid bath; tune on hardware. The loop steps on each fresh DS18B20 sample.
static constexpr float kPidKp = 0.08f;
static constexpr float kPidKi = 0.0015f;
static constexpr float kPidKd = 0.4f;
static constexpr float kDutyMin = 0.0f;
static constexpr float kDutyMax = 1.0f;

static constexpr float kDefaultSetpointC = 30.0f;  // typical fermentation temp

// Safety limits (heater forced off when violated):
static constexpr float    kHeaterSafetyMaxC = 80.0f;  // NTC heater-probe over-temp
static constexpr float    kProcessMaxC      = 55.0f;  // liquid sanity ceiling
static constexpr uint32_t kSafetyCheckMs    = 200;    // fast NTC safety poll
}  // namespace Thermal

// ── Process / run defaults ───────────────────────────────────────────────────
namespace Process {
static constexpr uint8_t  kDefaultMotorPercent = 40;  // disk rotation speed
static constexpr uint16_t kDefaultDurationMin  = 0;   // 0 = run until stopped
static constexpr const char* kPrefsNamespace   = "reactor";
}  // namespace Process

// ── WiFi (STA + AP captive-portal onboarding) ────────────────────────────────
namespace Wifi {
static constexpr const char* kHostname    = "mini-reactor";
static constexpr const char* kApSsid       = "MiniReactor-Setup";
static constexpr const char* kApPassword   = "";  // open AP for onboarding
static constexpr uint32_t kConnectTimeoutMs    = 15000;
static constexpr uint32_t kReconnectIntervalMs = 20000;
static constexpr uint32_t kApFallbackDelayMs   = 20000;  // STA-loss grace period
static constexpr uint8_t  kMaxScanResults      = 16;
static constexpr const char* kPrefsNamespace = "wifi";
static constexpr const char* kPrefsSsidKey   = "ssid";
static constexpr const char* kPrefsPassKey   = "pass";
}  // namespace Wifi

// ── Web server ───────────────────────────────────────────────────────────────
namespace Web {
static constexpr uint16_t kPort           = 80;
static constexpr uint32_t kWsPushPeriodMs = 250;  // telemetry push (~4 Hz)
}  // namespace Web

// ── Main loop timing ─────────────────────────────────────────────────────────
namespace Timing {
static constexpr uint32_t kControlPeriodMs = 100;    // reactor/PID update
static constexpr uint32_t kLogPeriodMs     = 10000;  // SD log row interval
}  // namespace Timing

}  // namespace AppConfig
