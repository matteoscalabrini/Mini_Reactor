#include "features/hub/sensor/Qmi8658.hpp"
#include "features/hub/sensor/Qmi8658Math.hpp"

#include <cmath>

namespace {

constexpr uint8_t kCtrl9CommandAck = 0x00;
constexpr uint8_t kCtrl9CommandWriteWomSetting = 0x08;
constexpr uint8_t kCtrl9CommandConfigureTap = 0x0C;

}  // namespace

Qmi8658::Qmi8658(TwoWire& wire, uint8_t address)
    : wire_(wire), configuredAddress_(address), activeAddress_(address) {}

bool Qmi8658::begin(const Config& config) {
  config_ = config;
  configuredAddress_ = config.address;
  activeAddress_ = configuredAddress_;
  lastProbeAddress_ = configuredAddress_;
  lastWhoAmIRead_ = 0;
  lastRevisionRead_ = 0;
  accelSensitivityGPerLsb_ = qmi::accelSensitivityGPerLsb(config_.accelRangeG);
  gyroSensitivityDpsPerLsb_ = qmi::gyroSensitivityDpsPerLsb(config_.gyroRangeDps);
  ready_ = false;

  if (!config_.featureEnabled) {
    lastError_ = Error::None;
    return false;
  }

  if (!probePart()) {
    return false;
  }
  if (!resetDevice()) {
    return false;
  }
  if (!applyConfig()) {
    return false;
  }

  Status initial = {};
  if (!readStatus(initial)) {
    return false;
  }

  ready_ = true;
  lastError_ = Error::None;
  return true;
}

bool Qmi8658::reconfigure() {
  if (!config_.featureEnabled) {
    lastError_ = Error::InvalidArgument;
    return false;
  }
  if (!probePart()) {
    ready_ = false;
    return false;
  }
  if (!resetDevice()) {
    ready_ = false;
    return false;
  }
  if (!applyConfig()) {
    ready_ = false;
    return false;
  }
  Status check = {};
  if (!readStatus(check)) {
    ready_ = false;
    return false;
  }
  ready_ = true;
  lastError_ = Error::None;
  return true;
}

bool Qmi8658::enterWakeOnMotionMode(const WakeOnMotionConfig& config) {
  if (!config_.featureEnabled) {
    lastError_ = Error::InvalidArgument;
    return false;
  }
  if (config.thresholdMg == 0U || config.blankingSamples > 0x3FU ||
      config.accelRangeG == 0U) {
    lastError_ = Error::InvalidArgument;
    return false;
  }
  if (!probePart()) {
    ready_ = false;
    return false;
  }

  const uint8_t ctrl2 = static_cast<uint8_t>(
      encodeAccelFsBits(config.accelRangeG) |
      encodeAccelOdrBits(config.accelOdrMilliHz));
  const uint8_t ctrl1 = encodeCtrl1(config.interruptOnInt1, !config.interruptOnInt1);
  const uint8_t ctrl8 = static_cast<uint8_t>(
      config_.useStatusIntForCtrl9Handshake ? (1U << 7) : 0U);
  uint8_t cal1H = static_cast<uint8_t>(config.blankingSamples & 0x3FU);
  if (!config.interruptOnInt1) {
    cal1H |= 0x40U;
  }
  if (config.interruptInitiallyHigh) {
    cal1H |= 0x80U;
  }

  if (!writeRegister8(kRegCtrl7, 0x00U) ||
      !writeRegister8(kRegCtrl8, ctrl8) ||
      !writeRegister8(kRegCtrl2, ctrl2) ||
      !writeRegister8(kRegCal1L, config.thresholdMg) ||
      !writeRegister8(kRegCal1H, cal1H) ||
      !runCtrl9Command(kCtrl9CommandWriteWomSetting, config.ctrl9TimeoutMs) ||
      !writeRegister8(kRegCtrl7, 0x01U) ||
      !writeRegister8(kRegCtrl1, ctrl1)) {
    ready_ = false;
    lastError_ = Error::WakeOnMotionFailed;
    return false;
  }

  // Let the programmed blanking window elapse and clear any early WoM event so
  // the interrupt line returns to its configured idle state before deep sleep.
  const uint32_t blankingMs =
      config.accelOdrMilliHz == 0U
          ? 0U
          : static_cast<uint32_t>(
                (static_cast<uint64_t>(config.blankingSamples) * 1000000ULL +
                 static_cast<uint64_t>(config.accelOdrMilliHz) - 1ULL) /
                static_cast<uint64_t>(config.accelOdrMilliHz));
  if (blankingMs > 0U) {
    delay(blankingMs + 8U);
  }
  uint8_t status1 = 0;
  if (!readRegister8(kRegStatus1, status1)) {
    ready_ = false;
    lastError_ = Error::WakeOnMotionFailed;
    return false;
  }

  ready_ = true;
  lastError_ = Error::None;
  return true;
}

bool Qmi8658::readStatus(Status& outStatus) {
  if (!config_.featureEnabled) {
    lastError_ = Error::InvalidArgument;
    return false;
  }

  uint8_t whoAmI = 0;
  uint8_t revision = 0;
  uint8_t ctrl1 = 0;
  uint8_t ctrl2 = 0;
  uint8_t ctrl3 = 0;
  uint8_t ctrl5 = 0;
  uint8_t ctrl7 = 0;
  uint8_t ctrl8 = 0;
  uint8_t statusTriplet[3] = {};
  uint8_t rawData[14] = {};

  if (!readRegister8(kRegWhoAmI, whoAmI) ||
      !readRegister8(kRegRevision, revision) ||
      !readRegister8(kRegCtrl1, ctrl1) ||
      !readRegister8(kRegCtrl2, ctrl2) ||
      !readRegister8(kRegCtrl3, ctrl3) ||
      !readRegister8(kRegCtrl5, ctrl5) ||
      !readRegister8(kRegCtrl7, ctrl7) ||
      !readRegister8(kRegCtrl8, ctrl8) ||
      !readRegisterBlock(kRegStatusInt, statusTriplet, sizeof(statusTriplet)) ||
      !readRegisterBlock(kRegTempL, rawData, sizeof(rawData))) {
    return false;
  }

  if (whoAmI != kWhoAmIValue) {
    lastError_ = Error::PartMismatch;
    return false;
  }

  const int16_t rawTemp   = qmi::decodeS16(&rawData[0]);
  const int16_t rawAccelX = qmi::decodeS16(&rawData[2]);
  const int16_t rawAccelY = qmi::decodeS16(&rawData[4]);
  const int16_t rawAccelZ = qmi::decodeS16(&rawData[6]);
  const int16_t rawGyroX  = qmi::decodeS16(&rawData[8]);
  const int16_t rawGyroY  = qmi::decodeS16(&rawData[10]);
  const int16_t rawGyroZ  = qmi::decodeS16(&rawData[12]);

  outStatus = {};
  outStatus.online = true;
  outStatus.accelDataReady = (statusTriplet[1] & 0x01U) != 0U;
  outStatus.gyroDataReady = (statusTriplet[1] & 0x02U) != 0U;
  outStatus.ctrl9CmdDone = (statusTriplet[0] & 0x80U) != 0U;
  outStatus.dataLocked = (statusTriplet[0] & 0x02U) != 0U;
  outStatus.dataAvailable = (statusTriplet[0] & 0x01U) != 0U;
  outStatus.significantMotionDetected = (statusTriplet[2] & 0x80U) != 0U;
  outStatus.noMotionDetected = (statusTriplet[2] & 0x40U) != 0U;
  outStatus.anyMotionDetected = (statusTriplet[2] & 0x20U) != 0U;
  outStatus.pedometerDetected = (statusTriplet[2] & 0x10U) != 0U;
  outStatus.wakeOnMotionDetected = (statusTriplet[2] & 0x04U) != 0U;
  outStatus.tapDetected = (statusTriplet[2] & 0x02U) != 0U;
  outStatus.address = activeAddress_;
  outStatus.whoAmI = whoAmI;
  outStatus.revision = revision;
  outStatus.rawCtrl1 = ctrl1;
  outStatus.rawCtrl2 = ctrl2;
  outStatus.rawCtrl3 = ctrl3;
  outStatus.rawCtrl5 = ctrl5;
  outStatus.rawCtrl7 = ctrl7;
  outStatus.rawCtrl8 = ctrl8;
  outStatus.rawStatusInt = statusTriplet[0];
  outStatus.rawStatus0 = statusTriplet[1];
  outStatus.rawStatus1 = statusTriplet[2];
  outStatus.rawTemp = rawTemp;
  outStatus.rawAccelX = rawAccelX;
  outStatus.rawAccelY = rawAccelY;
  outStatus.rawAccelZ = rawAccelZ;
  outStatus.rawGyroX = rawGyroX;
  outStatus.rawGyroY = rawGyroY;
  outStatus.rawGyroZ = rawGyroZ;
  outStatus.temperatureC = qmi::decodeTemperatureC(rawTemp);
  outStatus.accelXg = static_cast<float>(rawAccelX) * accelSensitivityGPerLsb_;
  outStatus.accelYg = static_cast<float>(rawAccelY) * accelSensitivityGPerLsb_;
  outStatus.accelZg = static_cast<float>(rawAccelZ) * accelSensitivityGPerLsb_;
  outStatus.gyroXdps = static_cast<float>(rawGyroX) * gyroSensitivityDpsPerLsb_;
  outStatus.gyroYdps = static_cast<float>(rawGyroY) * gyroSensitivityDpsPerLsb_;
  outStatus.gyroZdps = static_cast<float>(rawGyroZ) * gyroSensitivityDpsPerLsb_;

  lastError_ = Error::None;
  return true;
}

bool Qmi8658::poll(Status& outStatus, uint32_t nowMs) {
  (void)nowMs;
  if (!config_.featureEnabled || !ready_) {
    return false;
  }
  if (!readStatus(outStatus)) {
    ready_ = false;
    return false;
  }
  return true;
}

bool Qmi8658::isFeatureEnabled() const { return config_.featureEnabled; }
bool Qmi8658::isReady() const { return ready_; }
uint8_t Qmi8658::activeAddress() const { return activeAddress_; }
uint8_t Qmi8658::lastProbeAddress() const { return lastProbeAddress_; }
uint8_t Qmi8658::lastWhoAmIRead() const { return lastWhoAmIRead_; }
uint8_t Qmi8658::lastRevisionRead() const { return lastRevisionRead_; }
Qmi8658::Error Qmi8658::lastError() const { return lastError_; }

const char* Qmi8658::lastErrorString() const {
  switch (lastError_) {
    case Error::None:
      return "none";
    case Error::DeviceNotFound:
      return "device_not_found";
    case Error::I2cWriteFailed:
      return "i2c_write_failed";
    case Error::I2cReadFailed:
      return "i2c_read_failed";
    case Error::InvalidArgument:
      return "invalid_argument";
    case Error::PartMismatch:
      return "part_mismatch";
    case Error::ResetFailed:
      return "reset_failed";
    case Error::ConfigFailed:
      return "config_failed";
    case Error::Ctrl9Failed:
      return "ctrl9_failed";
    case Error::TapConfigFailed:
      return "tap_config_failed";
    case Error::WakeOnMotionFailed:
      return "wake_on_motion_failed";
    default:
      return "unknown";
  }
}

bool Qmi8658::probePart() {
  if (probePartAtAddress(configuredAddress_)) {
    return true;
  }
  if (!config_.enableAddressFallbackProbe) {
    if (lastError_ != Error::PartMismatch) {
      lastError_ = Error::DeviceNotFound;
    }
    return false;
  }

  const uint8_t fallbackAddress = configuredAddress_ == 0x6A ? 0x6B : 0x6A;
  if (fallbackAddress != configuredAddress_ &&
      probePartAtAddress(fallbackAddress)) {
    return true;
  }
  if (lastError_ != Error::PartMismatch) {
    lastError_ = Error::DeviceNotFound;
  }
  return false;
}

bool Qmi8658::probePartAtAddress(uint8_t address) {
  activeAddress_ = address;
  lastProbeAddress_ = address;
  uint8_t whoAmI = 0;
  uint8_t revision = 0;
  if (!readRegister8(kRegWhoAmI, whoAmI) ||
      !readRegister8(kRegRevision, revision)) {
    return false;
  }
  lastWhoAmIRead_ = whoAmI;
  lastRevisionRead_ = revision;
  if (whoAmI != kWhoAmIValue) {
    lastError_ = Error::PartMismatch;
    return false;
  }
  lastError_ = Error::None;
  return true;
}

bool Qmi8658::applyConfig() {
  const bool motionInterruptEnabled =
      config_.enableTapDetection || config_.enableAnyMotion ||
      config_.enableNoMotion || config_.enableSignificantMotion ||
      config_.enablePedometer;
  const bool enableInt1 = motionInterruptEnabled && config_.motionInterruptOnInt1;
  const bool enableInt2 = motionInterruptEnabled && !config_.motionInterruptOnInt1;
  const uint8_t ctrl1 = encodeCtrl1(enableInt1, enableInt2);
  const uint8_t ctrl2 = static_cast<uint8_t>(
      encodeAccelFsBits(config_.accelRangeG) |
      encodeAccelOdrBits(config_.accelOdrMilliHz));
  const uint8_t ctrl3 = static_cast<uint8_t>(
      encodeGyroFsBits(config_.gyroRangeDps) |
      encodeGyroOdrBits(config_.gyroOdrMilliHz));

  uint8_t ctrl5 = 0;
  ctrl5 |= encodeLpfBits(config_.gyroLpfMode, config_.enableGyroLpf, true);
  ctrl5 |= encodeLpfBits(config_.accelLpfMode, config_.enableAccelLpf, false);

  uint8_t ctrl7 = 0;
  if (config_.enableSyncSample) {
    ctrl7 |= (1U << 7);
  }
  if (config_.useHighSpeedClock) {
    ctrl7 |= (1U << 6);
  }
  if (config_.enableGyroSnooze) {
    ctrl7 |= (1U << 4);
  }
  if (config_.enableAttitudeEngine) {
    ctrl7 |= (1U << 3);
  }
  ctrl7 |= (1U << 1);  // gEN
  ctrl7 |= (1U << 0);  // aEN

  uint8_t ctrl8Base = 0;
  if (config_.motionInterruptOnInt1) {
    ctrl8Base |= (1U << 6);
  }
  if (config_.enablePedometer) {
    ctrl8Base |= (1U << 4);
  }
  if (config_.enableSignificantMotion) {
    ctrl8Base |= (1U << 3);
  }
  if (config_.enableNoMotion) {
    ctrl8Base |= (1U << 2);
  }
  if (config_.enableAnyMotion) {
    ctrl8Base |= (1U << 1);
  }

  const uint8_t ctrl8Handshake = static_cast<uint8_t>(ctrl8Base | (1U << 7));
  const uint8_t ctrl8Final = static_cast<uint8_t>(
      ctrl8Base |
      (config_.useStatusIntForCtrl9Handshake ? (1U << 7) : 0U) |
      (config_.enableTapDetection ? (1U << 0) : 0U));

  if (!writeRegister8(kRegCtrl1, ctrl1) ||
      !writeRegister8(kRegCtrl2, ctrl2) ||
      !writeRegister8(kRegCtrl3, ctrl3) ||
      !writeRegister8(kRegCtrl5, ctrl5) ||
      !writeRegister8(kRegCtrl8, ctrl8Handshake)) {
    lastError_ = Error::ConfigFailed;
    return false;
  }

  if (config_.enableTapDetection && !configureTapEngine()) {
    lastError_ = Error::TapConfigFailed;
    return false;
  }

  if (!writeRegister8(kRegCtrl7, ctrl7) ||
      !writeRegister8(kRegCtrl8, ctrl8Final)) {
    lastError_ = Error::ConfigFailed;
    return false;
  }

  lastError_ = Error::None;
  return true;
}

bool Qmi8658::configureTapEngine() {
  if (config_.enableSyncSample) {
    lastError_ = Error::InvalidArgument;
    return false;
  }

  if (!writeRegister8(kRegCal1L, config_.tapPeakWindowSamples) ||
      !writeRegister8(kRegCal1H, config_.tapPriority) ||
      !writeRegister8(
          kRegCal2L, static_cast<uint8_t>(config_.tapWindowSamples & 0xFFU)) ||
      !writeRegister8(
          kRegCal2H,
          static_cast<uint8_t>((config_.tapWindowSamples >> 8) & 0xFFU)) ||
      !writeRegister8(kRegCal3L,
                      static_cast<uint8_t>(config_.tapDoubleWindowSamples &
                                           0xFFU)) ||
      !writeRegister8(kRegCal3H,
                      static_cast<uint8_t>((config_.tapDoubleWindowSamples >>
                                           8) &
                                          0xFFU)) ||
      !writeRegister8(kRegCal4H, 0x01U) ||
      !runCtrl9Command(kCtrl9CommandConfigureTap, config_.ctrl9TimeoutMs)) {
    lastError_ = Error::TapConfigFailed;
    return false;
  }

  const uint16_t peakThreshold =
      encodeTapMagnitudeThreshold(config_.tapPeakMagnitudeThresholdGSquared);
  const uint16_t quietThreshold =
      encodeTapMagnitudeThreshold(config_.tapQuietThresholdGSquared);
  if (!writeRegister8(kRegCal1L, encodeTapFraction(config_.tapAlpha)) ||
      !writeRegister8(kRegCal1H, encodeTapFraction(config_.tapGamma)) ||
      !writeRegister8(kRegCal2L,
                      static_cast<uint8_t>(peakThreshold & 0xFFU)) ||
      !writeRegister8(kRegCal2H,
                      static_cast<uint8_t>((peakThreshold >> 8) & 0xFFU)) ||
      !writeRegister8(kRegCal3L,
                      static_cast<uint8_t>(quietThreshold & 0xFFU)) ||
      !writeRegister8(kRegCal3H,
                      static_cast<uint8_t>((quietThreshold >> 8) & 0xFFU)) ||
      !writeRegister8(kRegCal4H, 0x02U) ||
      !runCtrl9Command(kCtrl9CommandConfigureTap, config_.ctrl9TimeoutMs)) {
    lastError_ = Error::TapConfigFailed;
    return false;
  }

  lastError_ = Error::None;
  return true;
}

bool Qmi8658::runCtrl9Command(uint8_t command, uint32_t timeoutMs) {
  if (!writeRegister8(kRegCtrl9, command) ||
      !waitForCtrl9Flag(true, timeoutMs) ||
      !writeRegister8(kRegCtrl9, kCtrl9CommandAck) ||
      !waitForCtrl9Flag(false, timeoutMs)) {
    lastError_ = Error::Ctrl9Failed;
    return false;
  }
  lastError_ = Error::None;
  return true;
}

bool Qmi8658::waitForCtrl9Flag(bool expectedSet, uint32_t timeoutMs) {
  const uint32_t startMs = millis();
  while ((millis() - startMs) <= timeoutMs) {
    uint8_t statusInt = 0;
    if (!readRegister8(kRegStatusInt, statusInt)) {
      delay(1);
      continue;
    }
    if (((statusInt & 0x80U) != 0U) == expectedSet) {
      return true;
    }
    delay(1);
  }
  lastError_ = Error::Ctrl9Failed;
  return false;
}

bool Qmi8658::ensureI2cBus(bool forceRestart) {
  if (!config_.featureEnabled) {
    lastError_ = Error::InvalidArgument;
    return false;
  }

  if (forceRestart && wireStarted_) {
    wire_.end();
    wireStarted_ = false;
    delay(1);
  }

  if (wireStarted_) {
    return true;
  }

  if (!wire_.begin(config_.pinI2cSda, config_.pinI2cScl, config_.i2cClockHz)) {
    lastError_ = Error::I2cWriteFailed;
    return false;
  }

  wireStarted_ = true;
  return true;
}

bool Qmi8658::resetDevice() {
  if (!writeRegister8(kRegReset, kResetValue)) {
    lastError_ = Error::ResetFailed;
    return false;
  }
  delay(10);
  uint8_t whoAmI = 0;
  for (uint8_t attempt = 0; attempt < 20; ++attempt) {
    if (readRegister8(kRegWhoAmI, whoAmI) && whoAmI == kWhoAmIValue) {
      lastError_ = Error::None;
      return true;
    }
    delay(2);
  }
  lastError_ = Error::ResetFailed;
  return false;
}

bool Qmi8658::readRegister8(uint8_t reg, uint8_t& outValue) {
  for (uint8_t attempt = 0; attempt < kI2cMaxAttempts; ++attempt) {
    if (attempt > 0U) {
      if (!ensureI2cBus(attempt > 1U)) {
        continue;
      }
    }

    wire_.beginTransmission(activeAddress_);
    wire_.write(reg);
    if (wire_.endTransmission(false) != 0) {
      lastError_ = Error::I2cWriteFailed;
      continue;
    }

    const uint8_t bytesRequested = 1;
    const uint8_t bytesReceived = wire_.requestFrom(activeAddress_, bytesRequested);
    if (bytesReceived != bytesRequested || wire_.available() < bytesRequested) {
      lastError_ = Error::I2cReadFailed;
      continue;
    }

    outValue = static_cast<uint8_t>(wire_.read());
    wireStarted_ = true;
    lastError_ = Error::None;
    return true;
  }
  return false;
}

bool Qmi8658::writeRegister8(uint8_t reg, uint8_t value) {
  for (uint8_t attempt = 0; attempt < kI2cMaxAttempts; ++attempt) {
    if (attempt > 0U) {
      if (!ensureI2cBus(attempt > 1U)) {
        continue;
      }
    }

    wire_.beginTransmission(activeAddress_);
    wire_.write(reg);
    wire_.write(value);
    if (wire_.endTransmission() == 0) {
      wireStarted_ = true;
      lastError_ = Error::None;
      return true;
    }
    lastError_ = Error::I2cWriteFailed;
  }
  return false;
}

bool Qmi8658::readRegisterBlock(uint8_t startReg, uint8_t* outBuffer, uint8_t length) {
  if (outBuffer == nullptr || length == 0U) {
    lastError_ = Error::InvalidArgument;
    return false;
  }
  for (uint8_t attempt = 0; attempt < kI2cMaxAttempts; ++attempt) {
    if (attempt > 0U) {
      if (!ensureI2cBus(attempt > 1U)) {
        continue;
      }
    }

    wire_.beginTransmission(activeAddress_);
    wire_.write(startReg);
    if (wire_.endTransmission(false) != 0) {
      lastError_ = Error::I2cWriteFailed;
      continue;
    }

    const uint8_t bytesReceived = wire_.requestFrom(activeAddress_, length);
    if (bytesReceived != length || wire_.available() < length) {
      lastError_ = Error::I2cReadFailed;
      continue;
    }

    for (uint8_t i = 0; i < length; ++i) {
      outBuffer[i] = static_cast<uint8_t>(wire_.read());
    }
    wireStarted_ = true;
    lastError_ = Error::None;
    return true;
  }
  return false;
}

uint8_t Qmi8658::encodeCtrl1(bool enableInt1, bool enableInt2) {
  uint8_t value = static_cast<uint8_t>(1U << 6);
  if (enableInt1) {
    value |= static_cast<uint8_t>(1U << 3);
  }
  if (enableInt2) {
    value |= static_cast<uint8_t>(1U << 4);
  }
  return value;
}

uint8_t Qmi8658::encodeAccelOdrBits(uint32_t odrMilliHz) {
  if (odrMilliHz >= 1000000UL) {
    return 0x03;
  }
  if (odrMilliHz >= 500000UL) {
    return 0x04;
  }
  if (odrMilliHz >= 250000UL) {
    return 0x05;
  }
  if (odrMilliHz >= 128000UL) {
    return 0x0C;
  }
  if (odrMilliHz >= 125000UL) {
    return 0x06;
  }
  if (odrMilliHz >= 62500UL) {
    return 0x07;
  }
  if (odrMilliHz >= 31250UL) {
    return 0x08;
  }
  if (odrMilliHz >= 21000UL) {
    return 0x0D;
  }
  if (odrMilliHz >= 11000UL) {
    return 0x0E;
  }
  return 0x0F;
}

uint8_t Qmi8658::encodeGyroOdrBits(uint32_t odrMilliHz) {
  if (odrMilliHz >= 7520000UL) {
    return 0x00;
  }
  if (odrMilliHz >= 3760000UL) {
    return 0x01;
  }
  if (odrMilliHz >= 1880000UL) {
    return 0x02;
  }
  if (odrMilliHz >= 940000UL) {
    return 0x03;
  }
  if (odrMilliHz >= 470000UL) {
    return 0x04;
  }
  if (odrMilliHz >= 235000UL) {
    return 0x05;
  }
  if (odrMilliHz >= 117500UL) {
    return 0x06;
  }
  if (odrMilliHz >= 58750UL) {
    return 0x07;
  }
  return 0x08;
}

uint8_t Qmi8658::encodeAccelFsBits(uint8_t rangeG) {
  if (rangeG >= 16U) {
    return 0x30;
  }
  if (rangeG >= 8U) {
    return 0x20;
  }
  if (rangeG >= 4U) {
    return 0x10;
  }
  return 0x00;
}

uint8_t Qmi8658::encodeGyroFsBits(uint16_t rangeDps) {
  if (rangeDps <= 16U) {
    return 0x00;
  }
  if (rangeDps <= 32U) {
    return 0x10;
  }
  if (rangeDps <= 64U) {
    return 0x20;
  }
  if (rangeDps <= 128U) {
    return 0x30;
  }
  if (rangeDps <= 256U) {
    return 0x40;
  }
  if (rangeDps <= 512U) {
    return 0x50;
  }
  if (rangeDps <= 1024U) {
    return 0x60;
  }
  return 0x70;
}

uint8_t Qmi8658::encodeLpfBits(LpfMode mode, bool enabled, bool gyro) {
  const uint8_t modeBits = static_cast<uint8_t>(mode) & 0x03U;
  if (gyro) {
    return static_cast<uint8_t>((modeBits << 5) | (enabled ? (1U << 4) : 0U));
  }
  return static_cast<uint8_t>((modeBits << 1) | (enabled ? 1U : 0U));
}

uint8_t Qmi8658::encodeTapFraction(float value) {
  if (!std::isfinite(value) || value <= 0.0f) {
    return 0U;
  }
  const float scaled = value * 128.0f;
  if (scaled >= 255.0f) {
    return 0xFFU;
  }
  return static_cast<uint8_t>(std::lround(scaled));
}

uint16_t Qmi8658::encodeTapMagnitudeThreshold(float thresholdGSquared) {
  if (!std::isfinite(thresholdGSquared) || thresholdGSquared <= 0.0f) {
    return 0U;
  }
  const float scaled = thresholdGSquared * 1000.0f;
  if (scaled >= 65535.0f) {
    return 0xFFFFU;
  }
  return static_cast<uint16_t>(std::lround(scaled));
}
