/*
 * Es7210.cpp — ES7210 quad-mic ADC driver implementation.
 * Register sequences ported verbatim from
 * ../Barebone/src/features/hub/HubFeature.cpp:
 *   begin()     <- initializeEs7210() probe portion   lines 2278-2300
 *   powerDown() <- powerDownEs7210()                  lines 2302-2325
 * Porting Convention transforms applied:
 *   - writeRegister8(addr, reg, v) -> local writeReg(reg, v) (address from ctor)
 *   - micAdcState_.present -> present_ member
 *   - AppLog::*f() / micAdcError_ -> return value only (logging in HubRuntime)
 *   - configureEs7210ForCapture / I2S paths NOT ported (Phase 2)
 */
#include "features/hub/audio/Es7210.hpp"

// ── Register map (only those needed for probe + power-down) ──────────────────

static constexpr uint8_t kEs7210ClockOffReg  = 0x01;
static constexpr uint8_t kEs7210PowerDownReg = 0x06;
static constexpr uint8_t kEs7210AnalogReg    = 0x40;
static constexpr uint8_t kEs7210Mic1PowerReg = 0x47;
static constexpr uint8_t kEs7210Mic2PowerReg = 0x48;
static constexpr uint8_t kEs7210Mic3PowerReg = 0x49;
static constexpr uint8_t kEs7210Mic4PowerReg = 0x4A;
static constexpr uint8_t kEs7210Mic12PowerReg = 0x4B;
static constexpr uint8_t kEs7210Mic34PowerReg = 0x4C;

// ── Constructor ───────────────────────────────────────────────────────────────

Es7210::Es7210(TwoWire& wire, uint8_t address)
    : wire_(wire), address_(address) {}

// ── Private I2C helper ────────────────────────────────────────────────────────

bool Es7210::writeReg(uint8_t reg, uint8_t v) {
  wire_.beginTransmission(address_);
  wire_.write(reg);
  wire_.write(v);
  return wire_.endTransmission() == 0;
}

// ── Public API ────────────────────────────────────────────────────────────────

// Ported from HubFeature::initializeEs7210() probe portion (lines 2278-2300).
// Phase 1: probe only. configureEs7210ForCapture() is NOT called here (Phase 2).
bool Es7210::begin() {
  wire_.beginTransmission(address_);
  present_ = (wire_.endTransmission() == 0);
  return present_;
}

// Ported verbatim from HubFeature::powerDownEs7210() (lines 2302-2325).
// ES7210 power-down (mirrors ESP-ADF es7210_stop): power down each mic channel,
// the shared bias/ADC/PGA, the analog block, the ADC clock, then the chip itself.
bool Es7210::powerDown() {
  if (!writeReg(kEs7210Mic1PowerReg,  0xFF) ||  // mic 1 power down
      !writeReg(kEs7210Mic2PowerReg,  0xFF) ||  // mic 2 power down
      !writeReg(kEs7210Mic3PowerReg,  0xFF) ||  // mic 3 power down
      !writeReg(kEs7210Mic4PowerReg,  0xFF) ||  // mic 4 power down
      !writeReg(kEs7210Mic12PowerReg, 0xFF) ||  // mic 1+2 shared bias power down
      !writeReg(kEs7210Mic34PowerReg, 0xFF) ||  // mic 3+4 shared bias power down
      !writeReg(kEs7210AnalogReg,     0xC0) ||  // analog block power down
      !writeReg(kEs7210ClockOffReg,   0x7F) ||  // ADC clock off
      !writeReg(kEs7210PowerDownReg,  0x07)) {  // chip power down
    return false;
  }
  return true;
}
