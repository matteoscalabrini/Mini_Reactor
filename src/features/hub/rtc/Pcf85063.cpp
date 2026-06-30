/*
 * Pcf85063.cpp — PCF85063 RTC driver implementation.
 * Register sequences ported verbatim from
 * ../Barebone/src/features/hub/HubFeature.cpp:
 *   begin()       <- initializePcf85063()   lines 1908-1942
 *   refresh()     <- refreshPcf85063Status() lines 1944-1968
 *   setDateTime() <- setRtcDateTime()        lines 887-948 (I2C write portion)
 * Porting Convention transforms applied:
 *   - AppLog::*f() -> Serial.printf()
 *   - Feature-gate / NVS / time() / network-sync logic dropped (Phase 1)
 *   - BCD helpers pulled from Pcf85063Bcd.hpp
 */
#include "features/hub/rtc/Pcf85063.hpp"
#include "features/hub/rtc/Pcf85063Bcd.hpp"

// ── Constructor ───────────────────────────────────────────────────────────────

Pcf85063::Pcf85063(TwoWire& wire, uint8_t address)
    : wire_(wire), address_(address) {}

// ── Private I2C helpers ───────────────────────────────────────────────────────

bool Pcf85063::readReg(uint8_t reg, uint8_t* buf, size_t n) {
  wire_.beginTransmission(address_);
  wire_.write(reg);
  if (wire_.endTransmission(false) != 0) return false;
  const size_t count = wire_.requestFrom(static_cast<int>(address_),
                                         static_cast<int>(n));
  if (count != n) return false;
  for (size_t i = 0; i < n; ++i) buf[i] = static_cast<uint8_t>(wire_.read());
  return true;
}

bool Pcf85063::writeReg(uint8_t reg, const uint8_t* buf, size_t n) {
  wire_.beginTransmission(address_);
  wire_.write(reg);
  for (size_t i = 0; i < n; ++i) wire_.write(buf[i]);
  return wire_.endTransmission() == 0;
}

// ── Public API ────────────────────────────────────────────────────────────────

// Ported from HubFeature::initializePcf85063() (lines 1908-1942).
bool Pcf85063::begin() {
  // Probe: attempt a zero-byte write and check for ACK.
  wire_.beginTransmission(address_);
  if (wire_.endTransmission() != 0) return false;

  // RAM r/w sanity check at reg 0x03 (verbatim).
  uint8_t ramBefore = 0;
  if (!readReg(kRegRam, &ramBefore, 1)) return false;
  uint8_t ramFlip = static_cast<uint8_t>(ramBefore ^ 0x5A);
  if (!writeReg(kRegRam, &ramFlip, 1)) return false;
  writeReg(kRegRam, &ramBefore, 1);  // restore (best-effort)

  // Clear CTRL1 bit 0x02 (24-hour mode) + 0x20 (start clock) (verbatim).
  uint8_t ctrl1 = 0;
  if (!readReg(kRegCtrl1, &ctrl1, 1)) return false;
  ctrl1 &= static_cast<uint8_t>(~0x02U);  // 24-hour mode
  ctrl1 &= static_cast<uint8_t>(~0x20U);  // start RTC
  if (!writeReg(kRegCtrl1, &ctrl1, 1)) return false;

  return refresh(state_);
}

// Ported from HubFeature::refreshPcf85063Status() (lines 1944-1968).
bool Pcf85063::refresh(DateTime& out) {
  uint8_t ctrl1 = 0;
  uint8_t timeRaw[7] = {};
  if (!readReg(kRegCtrl1, &ctrl1, 1) ||
      !readReg(kRegTime, timeRaw, sizeof(timeRaw))) {
    return false;
  }

  out.running  = (ctrl1 & 0x20U) == 0;
  out.seconds  = rtc::bcdToUint(timeRaw[0] & 0x7FU);
  out.minutes  = rtc::bcdToUint(timeRaw[1] & 0x7FU);
  out.hours    = rtc::bcdToUint(timeRaw[2] & 0x3FU);
  out.day      = rtc::bcdToUint(timeRaw[3] & 0x3FU);
  out.weekday  = rtc::bcdToUint(timeRaw[4] & 0x07U);
  out.month    = rtc::bcdToUint(timeRaw[5] & 0x1FU);
  out.year     = static_cast<uint16_t>(2000 + rtc::bcdToUint(timeRaw[6]));
  state_ = out;
  return true;
}

// Ported from HubFeature::setRtcDateTime() (lines 887-948, I2C write portion).
bool Pcf85063::setDateTime(const DateTime& dt) {
  if (dt.year < 2000 || dt.year > 2099 || dt.month < 1 || dt.month > 12 ||
      dt.day < 1 || dt.day > 31 || dt.weekday > 6 ||
      dt.hours > 23 || dt.minutes > 59 || dt.seconds > 59) {
    return false;
  }

  uint8_t ctrl1 = 0;
  if (!readReg(kRegCtrl1, &ctrl1, 1)) return false;
  ctrl1 &= static_cast<uint8_t>(~0x02U);
  ctrl1 &= static_cast<uint8_t>(~0x20U);

  uint8_t timeRaw[7] = {
      static_cast<uint8_t>(rtc::uintToBcd(dt.seconds) & 0x7FU),
      static_cast<uint8_t>(rtc::uintToBcd(dt.minutes) & 0x7FU),
      static_cast<uint8_t>(rtc::uintToBcd(dt.hours)   & 0x3FU),
      static_cast<uint8_t>(rtc::uintToBcd(dt.day)      & 0x3FU),
      static_cast<uint8_t>(rtc::uintToBcd(dt.weekday)  & 0x07U),
      static_cast<uint8_t>(rtc::uintToBcd(dt.month)    & 0x1FU),
      rtc::uintToBcd(static_cast<uint8_t>(dt.year - 2000U)),
  };

  if (!writeReg(kRegCtrl1, &ctrl1, 1) ||
      !writeReg(kRegTime, timeRaw, sizeof(timeRaw))) {
    return false;
  }

  return refresh(state_);
}
