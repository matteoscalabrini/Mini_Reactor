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

// API + firmware version reported in telemetry.
static constexpr const char* kFirmwareVersion = "1.0.0";

// Serial debug output.
static constexpr uint32_t kSerialBaud = 115200;
static constexpr uint16_t kSerialStartupDelayMs = 3000;  // USB-CDC enumerate

// ── I2C bus ─────────────────────────────────────────────────────────────────
// On-board HUSB238 PD sink (0x08) + external J3 header. 10k pull-ups on board.
namespace I2c {
static constexpr int      kSdaPin  = 1;       // GPIO1
static constexpr int      kSclPin  = 2;       // GPIO2
static constexpr uint32_t kClockHz = 100000;  // 100 kHz standard mode (HUSB238 etc.)
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
// CSV header written at the top of each per-run file (/runs/NNNNN.csv).
static constexpr const char* kLogHeader  =
    "t_ms,running,liquid_c,heater_c,setpoint_c,heater_pct,rpm,load,fault,safety";
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
static constexpr float    kMotorTestRpm = 8.0f;    // disc speed for the one-click test
static constexpr uint32_t kMotorTestMs  = 3000;    // test jog duration
static constexpr uint16_t kMicrosteps       = 16;
static constexpr uint16_t kStepsPerRev = 200;  // full steps/rev, direct 1:1 drivetrain
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

// ── Front-panel UI (OLED + encoder + 3 buttons) ──────────────────────────────
namespace Ui {
static constexpr uint8_t kDisplayI2cAddr = 0x3C;   // SH1107 address
// The OLED gets its OWN hardware I2C bus (Wire1) on GPIO43/44 (UART0 pins, free
// because the console runs over USB-CDC). This isolates it from the HUSB238 on
// the primary bus (GPIO1/2) — no shared-bus contention or pull-up fighting.
static constexpr int kDisplaySdaPin = 43;   // GPIO43 (U0TXD) -> OLED SDA, Wire1
static constexpr int kDisplaySclPin = 44;   // GPIO44 (U0RXD) -> OLED SCL, Wire1
static constexpr uint32_t kDisplayBusClockHz = 400000;  // SW-I2C bit-bang speed (240 MHz S3)
static constexpr int kEncAPin  = 8;    // GPIO8
static constexpr int kEncBPin  = 9;    // GPIO9
static constexpr int kEncSwPin = 41;   // GPIO41 (push)
static constexpr int kBtn1Pin  = 47;   // GPIO47 — motor pause
static constexpr int kBtn2Pin  = 48;   // GPIO48 — motor + heater hold
static constexpr int kBtn3Pin  = 4;    // GPIO4  — info
static constexpr float kTargetStepC = 0.5f;
static constexpr float kRpmStep     = 0.5f;
static constexpr float kTargetMinC  = 0.0f;
static constexpr float kTargetMaxC  = 55.0f;   // matches Thermal::kProcessMaxC
static constexpr uint32_t kRedrawIntervalMs = 250;  // idle live-data refresh (~4 Hz)
static constexpr uint32_t kMinRedrawMs = 40;        // floor between input-driven redraws (keeps the
                                                    // blocking SW-I2C blit from starving input polling)
}  // namespace Ui

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

// Plausible heater-NTC reading window; outside -> probe fault (heater off / start refused).
static constexpr float kNtcMinPlausibleC = 0.0f;
static constexpr float kNtcMaxPlausibleC = 110.0f;

// PID (output is heater duty 0..1). Conservative starting gains for a slow
// liquid bath; tune on hardware. The loop steps on each fresh DS18B20 sample.
static constexpr float kPidKp = 0.08f;
static constexpr float kPidKi = 0.0015f;
static constexpr float kPidKd = 0.4f;
static constexpr float kDutyMin = 0.0f;
static constexpr float kDutyMax = 1.0f;

static constexpr float kDefaultSetpointC = 36.0f;  // bacterial-cellulose operating point

// Safety limits (heater forced off when violated):
static constexpr float    kHeaterSafetyMaxC = 80.0f;  // NTC heater-probe over-temp
static constexpr float    kProcessMaxC      = 55.0f;  // liquid sanity ceiling
static constexpr uint32_t kSafetyCheckMs    = 200;    // fast NTC safety poll
}  // namespace Thermal

// ── Process / run defaults ───────────────────────────────────────────────────
namespace Process {
static constexpr float    kDefaultRpm      = 8.0f;   // disc operating speed
static constexpr float    kMinRpm          = 0.5f;   // allowed run range
static constexpr float    kMaxRpm          = 30.0f;
static constexpr uint16_t kDefaultDurationMin = 0;   // 0 = run until stopped
static constexpr const char* kPrefsNamespace  = "reactor";
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
