/*
 * Tca9554.cpp — TCA9554 8-bit I/O expander driver implementation.
 * Register sequences ported verbatim from
 * ../Barebone/src/features/hub/HubFeature.cpp:
 *   begin()     <- initializeTca9554()    lines 2117-2131
 *   configure() <- configureTca9554()     lines 2133-2142
 *   refresh()   <- refreshTca9554Status() lines 2144-2158
 * Porting Convention transforms applied:
 *   - writeRegister8/readRegister8 -> local readReg/writeReg (Wire pattern)
 *   - AppLog::*f() -> Serial.printf() (logging moved to HubRuntime)
 *   - Feature-gate / NVS / AppRuntime references dropped (Phase 1)
 *   - Default masks: output=0x00, polarity=0x00, config=0xFF
 *     (matches AppConfig::HubIoExpander::kOutputMask/kPolarityMask/kConfigMask verbatim)
 */
#include "features/hub/io/Tca9554.hpp"

// ── Constructor ───────────────────────────────────────────────────────────────

Tca9554::Tca9554(TwoWire& wire, uint8_t address)
    : wire_(wire), address_(address) {}

// ── Private I2C helpers ───────────────────────────────────────────────────────

bool Tca9554::readReg(uint8_t reg, uint8_t& v) {
  wire_.beginTransmission(address_);
  wire_.write(reg);
  if (wire_.endTransmission(false) != 0) return false;
  if (wire_.requestFrom(static_cast<int>(address_), 1) != 1) return false;
  v = static_cast<uint8_t>(wire_.read());
  return true;
}

bool Tca9554::writeReg(uint8_t reg, uint8_t v) {
  wire_.beginTransmission(address_);
  wire_.write(reg);
  wire_.write(v);
  return wire_.endTransmission() == 0;
}

// ── Public API ────────────────────────────────────────────────────────────────

// Ported from HubFeature::initializeTca9554() (lines 2117-2131).
bool Tca9554::begin() {
  // Probe: zero-byte write, check ACK.
  wire_.beginTransmission(address_);
  state_.present = (wire_.endTransmission() == 0);
  state_.ready   = false;
  if (!state_.present) return false;

  // Apply default config: all pins as inputs, no polarity inversion.
  // Defaults match Barebone AppConfig::HubIoExpander:
  //   kOutputMask=0x00, kPolarityMask=0x00, kConfigMask=0xFF
  return configure(0x00, 0x00, 0xFF);
}

// Ported from HubFeature::configureTca9554() (lines 2133-2142).
bool Tca9554::configure(uint8_t output, uint8_t polarity, uint8_t config) {
  if (!writeReg(kRegOutput, output) ||
      !writeReg(kRegPolarity, polarity) ||
      !writeReg(kRegConfig, config)) {
    state_.ready = false;
    return false;
  }
  state_.output   = output;
  state_.polarity = polarity;
  state_.config   = config;
  state_.ready    = true;
  return true;
}

// Ported from HubFeature::refreshTca9554Status() (lines 2144-2158).
bool Tca9554::refresh(State& out) {
  uint8_t input = 0, output = 0, polarity = 0, config = 0;
  if (!readReg(kRegInput,    input)   ||
      !readReg(kRegOutput,   output)  ||
      !readReg(kRegPolarity, polarity)||
      !readReg(kRegConfig,   config)) {
    state_.ready = false;
    out = state_;
    return false;
  }
  state_.input    = input;
  state_.output   = output;
  state_.polarity = polarity;
  state_.config   = config;
  state_.ready    = true;
  out = state_;
  return true;
}
