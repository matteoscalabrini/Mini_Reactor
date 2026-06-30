/*
 * Qmi8658.hpp — Public interface for the QMI8658/QMI8658C IMU driver.
 *
 * This driver is intentionally based on the QMI8658C rev 0.9 datasheet that
 * lives in the local reference folder. It covers the documented UI register
 * interface: identity, reset, CTRL1..CTRL9, status registers, raw accel/gyro/
 * temperature output, and the documented motion-detection bits in CTRL8 and
 * STATUS1. It also implements the datasheet Wake-on-Motion path used by the
 * hub for deep-sleep wake, via CAL1_x + CTRL9(0x08).
 *
 * Tap delivery uses the documented CTRL8/STATUS1 path plus the CAL/CTRL9 tap
 * programming sequence observed in the vendor examples. The public rev 0.9
 * datasheet documents the tap event bit, but not the full host-side parameter
 * upload needed to make the tap engine emit events reliably.
 *
 * Ported from ../Barebone/include/sensors/Qmi8658.hpp.
 * Pure math functions extracted into Qmi8658Math.hpp (host-testable).
 * Implementation: src/features/hub/sensor/Qmi8658.cpp
 */

#pragma once

#include <Arduino.h>
#include <Wire.h>

class Qmi8658 {
 public:
  enum class Error : uint8_t {
    None = 0,
    DeviceNotFound,
    I2cWriteFailed,
    I2cReadFailed,
    InvalidArgument,
    PartMismatch,
    ResetFailed,
    ConfigFailed,
    Ctrl9Failed,
    TapConfigFailed,
    WakeOnMotionFailed,
  };

  enum class LpfMode : uint8_t {
    Odr2p66 = 0,
    Odr3p63 = 1,
    Odr5p39 = 2,
    Odr13p37 = 3,
  };

  struct Config {
    bool featureEnabled = false;
    uint8_t pinI2cSda = 10;
    uint8_t pinI2cScl = 8;
    uint32_t i2cClockHz = 400000;
    uint8_t address = 0x6B;
    bool enableAddressFallbackProbe = true;
    uint8_t accelRangeG = 4;
    uint32_t accelOdrMilliHz = 250000;
    uint16_t gyroRangeDps = 512;
    uint32_t gyroOdrMilliHz = 235000;
    bool enableAccelLpf = false;
    LpfMode accelLpfMode = LpfMode::Odr2p66;
    bool enableGyroLpf = true;
    LpfMode gyroLpfMode = LpfMode::Odr2p66;
    bool enableSyncSample = false;
    bool useHighSpeedClock = false;
    bool enableGyroSnooze = false;
    bool enableAttitudeEngine = false;
    bool enableTapDetection = true;
    bool enableAnyMotion = false;
    bool enableNoMotion = false;
    bool enableSignificantMotion = false;
    bool enablePedometer = false;
    bool useStatusIntForCtrl9Handshake = true;
    bool motionInterruptOnInt1 = true;
    uint8_t tapPriority = 0;
    uint8_t tapPeakWindowSamples = 20;
    uint16_t tapWindowSamples = 50;
    uint16_t tapDoubleWindowSamples = 250;
    float tapAlpha = 0.0625f;
    float tapGamma = 0.25f;
    float tapPeakMagnitudeThresholdGSquared = 0.8f;
    float tapQuietThresholdGSquared = 0.4f;
    uint32_t ctrl9TimeoutMs = 1000;
  };

  struct WakeOnMotionConfig {
    uint8_t thresholdMg = 96;
    bool interruptOnInt1 = false;
    bool interruptInitiallyHigh = true;
    uint8_t blankingSamples = 12;
    uint8_t accelRangeG = 4;
    uint32_t accelOdrMilliHz = 62500;
    uint32_t ctrl9TimeoutMs = 1000;
  };

  struct Status {
    bool online = false;
    bool accelDataReady = false;
    bool gyroDataReady = false;
    bool ctrl9CmdDone = false;
    bool dataLocked = false;
    bool dataAvailable = false;
    bool significantMotionDetected = false;
    bool noMotionDetected = false;
    bool anyMotionDetected = false;
    bool pedometerDetected = false;
    bool wakeOnMotionDetected = false;
    bool tapDetected = false;
    uint8_t address = 0;
    uint8_t whoAmI = 0;
    uint8_t revision = 0;
    uint8_t rawCtrl1 = 0;
    uint8_t rawCtrl2 = 0;
    uint8_t rawCtrl3 = 0;
    uint8_t rawCtrl5 = 0;
    uint8_t rawCtrl7 = 0;
    uint8_t rawCtrl8 = 0;
    uint8_t rawStatusInt = 0;
    uint8_t rawStatus0 = 0;
    uint8_t rawStatus1 = 0;
    int16_t rawTemp = 0;
    int16_t rawAccelX = 0;
    int16_t rawAccelY = 0;
    int16_t rawAccelZ = 0;
    int16_t rawGyroX = 0;
    int16_t rawGyroY = 0;
    int16_t rawGyroZ = 0;
    float temperatureC = 0.0f;
    float accelXg = 0.0f;
    float accelYg = 0.0f;
    float accelZg = 0.0f;
    float gyroXdps = 0.0f;
    float gyroYdps = 0.0f;
    float gyroZdps = 0.0f;
  };

  explicit Qmi8658(TwoWire& wire = Wire, uint8_t address = 0x6B);

  bool begin(const Config& config);
  bool readStatus(Status& outStatus);
  bool poll(Status& outStatus, uint32_t nowMs = millis());
  bool reconfigure();
  bool enterWakeOnMotionMode(const WakeOnMotionConfig& config);

  bool isFeatureEnabled() const;
  bool isReady() const;
  uint8_t activeAddress() const;
  uint8_t lastProbeAddress() const;
  uint8_t lastWhoAmIRead() const;
  uint8_t lastRevisionRead() const;
  Error lastError() const;
  const char* lastErrorString() const;

 private:
  static constexpr uint8_t kRegWhoAmI = 0x00;
  static constexpr uint8_t kRegRevision = 0x01;
  static constexpr uint8_t kRegCtrl1 = 0x02;
  static constexpr uint8_t kRegCtrl2 = 0x03;
  static constexpr uint8_t kRegCtrl3 = 0x04;
  static constexpr uint8_t kRegCtrl5 = 0x06;
  static constexpr uint8_t kRegCtrl7 = 0x08;
  static constexpr uint8_t kRegCtrl8 = 0x09;
  static constexpr uint8_t kRegCtrl9 = 0x0A;
  static constexpr uint8_t kRegCal1L = 0x0B;
  static constexpr uint8_t kRegCal1H = 0x0C;
  static constexpr uint8_t kRegCal2L = 0x0D;
  static constexpr uint8_t kRegCal2H = 0x0E;
  static constexpr uint8_t kRegCal3L = 0x0F;
  static constexpr uint8_t kRegCal3H = 0x10;
  static constexpr uint8_t kRegCal4L = 0x11;
  static constexpr uint8_t kRegCal4H = 0x12;
  static constexpr uint8_t kRegStatusInt = 0x2D;
  static constexpr uint8_t kRegStatus0 = 0x2E;
  static constexpr uint8_t kRegStatus1 = 0x2F;
  static constexpr uint8_t kRegTempL = 0x33;
  static constexpr uint8_t kRegReset = 0x60;

  static constexpr uint8_t kWhoAmIValue = 0x05;
  static constexpr uint8_t kResetValue = 0xB0;
  static constexpr uint8_t kI2cMaxAttempts = 3;

  bool probePart();
  bool probePartAtAddress(uint8_t address);
  bool applyConfig();
  bool configureTapEngine();
  bool runCtrl9Command(uint8_t command, uint32_t timeoutMs);
  bool waitForCtrl9Flag(bool expectedSet, uint32_t timeoutMs);
  bool ensureI2cBus(bool forceRestart);
  bool resetDevice();

  bool readRegister8(uint8_t reg, uint8_t& outValue);
  bool writeRegister8(uint8_t reg, uint8_t value);
  bool readRegisterBlock(uint8_t startReg, uint8_t* outBuffer, uint8_t length);

  static uint8_t encodeCtrl1(bool enableInt1, bool enableInt2);
  static uint8_t encodeAccelOdrBits(uint32_t odrMilliHz);
  static uint8_t encodeGyroOdrBits(uint32_t odrMilliHz);
  static uint8_t encodeAccelFsBits(uint8_t rangeG);
  static uint8_t encodeGyroFsBits(uint16_t rangeDps);
  static uint8_t encodeLpfBits(LpfMode mode, bool enabled, bool gyro);
  static uint8_t encodeTapFraction(float value);
  static uint16_t encodeTapMagnitudeThreshold(float thresholdGSquared);

  TwoWire& wire_;
  uint8_t configuredAddress_ = 0x6B;
  uint8_t activeAddress_ = 0x6B;
  Config config_ = {};
  Error lastError_ = Error::None;
  bool ready_ = false;
  bool wireStarted_ = false;
  uint8_t lastProbeAddress_ = 0;
  uint8_t lastWhoAmIRead_ = 0;
  uint8_t lastRevisionRead_ = 0;
  float accelSensitivityGPerLsb_ = 4.0f / 32768.0f;
  float gyroSensitivityDpsPerLsb_ = 512.0f / 32768.0f;
};
