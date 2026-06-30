/*
 * Es8311.cpp — ES8311 audio codec driver implementation.
 * Register sequences ported verbatim from
 * ../Barebone/src/features/hub/HubFeature.cpp:
 *   begin()     <- initializeEs8311() probe portion   lines 2160-2174
 *   powerDown() <- powerDownEs8311()                  lines 2233-2250
 * Porting Convention transforms applied:
 *   - writeRegister8(addr, reg, v) -> local writeReg(reg, v) (address from ctor)
 *   - codecState_.present -> present_ member
 *   - AppLog::*f() / codecError_ -> return value only (logging in HubRuntime)
 *   - configureEs8311 / refreshEs8311Status / I2S paths NOT ported (Phase 2)
 */
#include "features/hub/audio/Es8311.hpp"

// ── Register map (only those needed for probe + power-down) ──────────────────

static constexpr uint8_t kEs8311System0DReg = 0x0D;
static constexpr uint8_t kEs8311System0EReg = 0x0E;
static constexpr uint8_t kEs8311System12Reg = 0x12;
static constexpr uint8_t kEs8311System14Reg = 0x14;
static constexpr uint8_t kEs8311Adc15Reg    = 0x15;
static constexpr uint8_t kEs8311Adc17Reg    = 0x17;
static constexpr uint8_t kEs8311Dac32Reg    = 0x32;
static constexpr uint8_t kEs8311Gp45Reg     = 0x45;

// ── Constructor ───────────────────────────────────────────────────────────────

Es8311::Es8311(TwoWire& wire, uint8_t address)
    : wire_(wire), address_(address) {}

// ── Private I2C helper ────────────────────────────────────────────────────────

bool Es8311::writeReg(uint8_t reg, uint8_t v) {
  wire_.beginTransmission(address_);
  wire_.write(reg);
  wire_.write(v);
  return wire_.endTransmission() == 0;
}

// ── Public API ────────────────────────────────────────────────────────────────

// Ported from HubFeature::initializeEs8311() probe portion (lines 2160-2174).
// Phase 1: probe only. configureEs8311() is NOT called here (Phase 2).
bool Es8311::begin() {
  wire_.beginTransmission(address_);
  present_ = (wire_.endTransmission() == 0);
  return present_;
}

// Ported verbatim from HubFeature::powerDownEs8311() (lines 2233-2250).
// ES8311 suspend / power-down (mirrors ESP-ADF es8311_suspend): zero the DAC/ADC
// volumes, power down the ADC and DAC, then the analog bias/reference circuits so
// the codec stops drawing on the always-on A3V3 rail.
bool Es8311::powerDown() {
  if (!writeReg(kEs8311Dac32Reg,    0x00) ||  // DAC volume = 0
      !writeReg(kEs8311Adc17Reg,    0x00) ||  // ADC gain = 0
      !writeReg(kEs8311System0EReg, 0xFF) ||  // ADC/DAC power down
      !writeReg(kEs8311System12Reg, 0x02) ||  // clock power down
      !writeReg(kEs8311System14Reg, 0x00) ||  // DAC digital power down
      !writeReg(kEs8311System0DReg, 0xFA) ||  // analog bias/reference power down
      !writeReg(kEs8311Adc15Reg,    0x00) ||  // ADC PGA power down
      !writeReg(kEs8311Gp45Reg,     0x01)) {  // chip global power down
    return false;
  }
  return true;
}
