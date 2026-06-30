/*
 * Axp2101.cpp — AXP2101 PMIC/charger driver implementation.
 * Register sequences ported verbatim from
 * ../Barebone/src/features/hub/HubFeature.cpp (lines 1619-1797).
 * Porting Convention transforms applied:
 *   - AppConfig::Pins::* / AppConfig::Hub* names re-homed to AppConfig::PinoutHub::* / AppConfig::HubPower::*
 *   - AppLog::*f() -> Serial.printf()
 *   - PRODUCT_MODE / BoardProfile gates removed
 *   - Pure math delegated to Axp2101Math.hpp
 */
#include "features/hub/power/Axp2101.hpp"
#include "app_config.hpp"

// ── Charge configuration lookup tables (verbatim from Barebone) ─────────────

static constexpr uint16_t kAxpChargeCurrentOptionsMa[] = {
    0, 100, 125, 150, 175, 200, 300, 400, 500, 600, 700, 800, 900, 1000,
};
static constexpr uint16_t kAxpChargeVoltageOptionsMv[] = {4000, 4100, 4200};
static constexpr uint16_t kAxpInputCurrentOptionsMa[] = {100, 500, 900, 1000, 1500, 2000};

// ── Private register constants (verbatim from Barebone, file lines 16-44) ───

static constexpr uint8_t kAxp2101IcTypeReg            = 0x03;
static constexpr uint8_t kAxp2101Status1Reg           = 0x00;
static constexpr uint8_t kAxp2101Status2Reg           = 0x01;
static constexpr uint8_t kAxp2101InputVoltageLimitReg = 0x15;
static constexpr uint8_t kAxp2101InputCurrentLimitReg = 0x16;
static constexpr uint8_t kAxp2101AdcChannelCtrlReg    = 0x30;
static constexpr uint8_t kAxp2101AdcResult0Reg        = 0x34;
static constexpr uint8_t kAxp2101AdcResult4Reg        = 0x38;
static constexpr uint8_t kAxp2101AdcResult6Reg        = 0x3A;
static constexpr uint8_t kAxp2101AdcResult8Reg        = 0x3C;
static constexpr uint8_t kAxp2101PrechargeReg         = 0x61;
static constexpr uint8_t kAxp2101ChargeCurrentReg     = 0x62;
static constexpr uint8_t kAxp2101TerminationReg       = 0x63;
static constexpr uint8_t kAxp2101ChargeVoltageReg     = 0x64;
static constexpr uint8_t kAxp2101BatteryDetectReg     = 0x68;
static constexpr uint8_t kAxp2101BatteryPercentReg    = 0xA4;
static constexpr uint8_t kAxp2101DcdcOnOffCtrlReg     = 0x80;
static constexpr uint8_t kAxp2101LdoOnOffCtrl0Reg     = 0x90;
static constexpr uint8_t kAxp2101LdoOnOffCtrl1Reg     = 0x91;
static constexpr uint8_t kAxp2101LdoCtrl0Value        = 0x01;  // ALDO1 only
static constexpr uint8_t kAxp2101LdoCtrl1Value        = 0x00;  // DLDO2 off
static constexpr uint8_t kAxp2101ExpectedChipId       = 0x4A;

// ── Constructor ──────────────────────────────────────────────────────────────

Axp2101::Axp2101(TwoWire& wire, uint8_t address)
    : wire_(wire), address_(address) {}

// ── Private I2C helpers (verbatim from Barebone lines 2340-2397) ─────────────

bool Axp2101::readReg(uint8_t reg, uint8_t& v) {
  return readRegs(reg, &v, 1);
}

bool Axp2101::readRegs(uint8_t reg, uint8_t* buf, size_t n) {
  wire_.beginTransmission(address_);
  wire_.write(reg);
  if (wire_.endTransmission(false) != 0) {
    return false;
  }
  const size_t count = wire_.requestFrom(static_cast<int>(address_),
                                         static_cast<int>(n));
  if (count != n) {
    return false;
  }
  for (size_t i = 0; i < n; ++i) {
    buf[i] = static_cast<uint8_t>(wire_.read());
  }
  return true;
}

bool Axp2101::writeReg(uint8_t reg, uint8_t v) {
  wire_.beginTransmission(address_);
  wire_.write(reg);
  wire_.write(v);
  return wire_.endTransmission() == 0;
}

// ── Public API ───────────────────────────────────────────────────────────────

bool Axp2101::begin(int sdaPin, int sclPin, uint32_t frequencyHz) {
  // Start and configure the shared I2C bus (verbatim from Barebone beginBus,
  // lines 1377-1386, with pin constants replaced per porting convention).
  if (!wire_.begin(sdaPin, sclPin, frequencyHz)) {
    lastError_ = "hub_i2c_begin_failed";
    return false;
  }
  wire_.setClock(frequencyHz);

  // initializeAxp2101() verbatim from Barebone lines 1619-1642.
  state_.present = probe();
  state_.ready = false;
  if (!state_.present) {
    lastError_ = "axp2101_not_found";
    return false;
  }

  if (!readReg(kAxp2101IcTypeReg, state_.chipId)) {
    lastError_ = "axp2101_chip_id_read_failed";
    return false;
  }
  if (state_.chipId != kAxp2101ExpectedChipId) {
    lastError_ = "axp2101_chip_id_mismatch";
    return false;
  }
  State tmp;
  if (!configureCharging() || !refreshStatus(tmp)) {
    return false;
  }
  state_.ready = true;
  lastError_ = "";
  return true;
}

bool Axp2101::probe() {
  wire_.beginTransmission(address_);
  return wire_.endTransmission() == 0;
}

bool Axp2101::configureCharging() {
  // Verbatim from Barebone lines 1644-1727.
  // axpSettings_* -> AppConfig::HubPower::* (porting convention rename).

  uint8_t adcCtrl = 0;
  if (!readReg(kAxp2101AdcChannelCtrlReg, adcCtrl)) {
    lastError_ = "axp2101_adc_ctrl_read_failed";
    state_.ready = false;
    return false;
  }
  adcCtrl |= 0x1D;   // batt, vbus, system, temp
  adcCtrl &= ~0x02;  // disable TS pin measure
  if (!writeReg(kAxp2101AdcChannelCtrlReg, adcCtrl) ||
      !writeReg(kAxp2101BatteryDetectReg, 0x01)) {
    lastError_ = "axp2101_measure_setup_failed";
    state_.ready = false;
    return false;
  }

  const uint8_t inputVoltageReg =
      axp::encodeInputVoltageReg(AppConfig::HubPower::kInputVoltageLimitMv);

  uint8_t inputCurrentReg = 0;
  for (size_t i = 0;
       i < sizeof(kAxpInputCurrentOptionsMa) / sizeof(kAxpInputCurrentOptionsMa[0]); ++i) {
    if (kAxpInputCurrentOptionsMa[i] == AppConfig::HubPower::kInputCurrentLimitMa) {
      inputCurrentReg = static_cast<uint8_t>(i);
      break;
    }
  }

  uint8_t chargeCurrentReg = 0;
  if (AppConfig::HubPower::kChargingEnabled) {
    for (size_t i = 0;
         i < sizeof(kAxpChargeCurrentOptionsMa) / sizeof(kAxpChargeCurrentOptionsMa[0]); ++i) {
      if (kAxpChargeCurrentOptionsMa[i] == AppConfig::HubPower::kChargeCurrentMa) {
        chargeCurrentReg = static_cast<uint8_t>((i == 0) ? 0 : (i + 3));
        break;
      }
    }
  }

  uint8_t chargeVoltageReg = 1;
  for (size_t i = 0;
       i < sizeof(kAxpChargeVoltageOptionsMv) / sizeof(kAxpChargeVoltageOptionsMv[0]); ++i) {
    if (kAxpChargeVoltageOptionsMv[i] == AppConfig::HubPower::kChargeVoltageMv) {
      chargeVoltageReg = static_cast<uint8_t>(i + 1);
      break;
    }
  }

  const uint8_t prechargeReg    = static_cast<uint8_t>(AppConfig::HubPower::kPrechargeCurrentMa / 25);
  const uint8_t terminationReg  = static_cast<uint8_t>(AppConfig::HubPower::kTerminationCurrentMa / 25);

  if (!writeReg(kAxp2101InputVoltageLimitReg, inputVoltageReg) ||
      !writeReg(kAxp2101InputCurrentLimitReg, inputCurrentReg) ||
      !writeReg(kAxp2101PrechargeReg, prechargeReg) ||
      !writeReg(kAxp2101ChargeCurrentReg, chargeCurrentReg) ||
      !writeReg(kAxp2101TerminationReg, terminationReg | 0x10) ||
      !writeReg(kAxp2101ChargeVoltageReg, chargeVoltageReg)) {
    lastError_ = "axp2101_config_write_failed";
    state_.ready = false;
    return false;
  }

  if (!writeReg(kAxp2101LdoOnOffCtrl0Reg, kAxp2101LdoCtrl0Value) ||
      !writeReg(kAxp2101LdoOnOffCtrl1Reg, kAxp2101LdoCtrl1Value)) {
    lastError_ = "axp2101_ldo_config_write_failed";
    state_.ready = false;
    return false;
  }

  // DCDC2 (0.9V) / DCDC3 (1.2V) / DCDC4 (1.8V) are enabled from OTP default but have no
  // external inductor/output on this board (schematic NC; bench-confirmed the hub runs
  // fine with them off), so the unused buck converters just burn quiescent. Disable them;
  // keep DCDC1 (bit0 = VCC3V3 system rail).
  uint8_t dcdcCtrl = 0;
  if (!readReg(kAxp2101DcdcOnOffCtrlReg, dcdcCtrl)) {
    lastError_ = "axp2101_dcdc_ctrl_read_failed";
    state_.ready = false;
    return false;
  }
  dcdcCtrl &= static_cast<uint8_t>(~0x0EU);
  if (!writeReg(kAxp2101DcdcOnOffCtrlReg, dcdcCtrl)) {
    lastError_ = "axp2101_dcdc_config_write_failed";
    state_.ready = false;
    return false;
  }

  lastError_ = "";
  return true;
}

bool Axp2101::refreshStatus(State& out) {
  // Verbatim from Barebone lines 1749-1797 with axpState_.*-> out.* mapping.
  uint8_t status1 = 0;
  uint8_t status2 = 0;
  uint8_t percent = 0;
  uint8_t batteryRaw[2] = {};
  uint8_t vbusRaw[2]    = {};
  uint8_t systemRaw[2]  = {};
  uint8_t tempRaw[2]    = {};

  if (!readReg(kAxp2101Status1Reg, status1) ||
      !readReg(kAxp2101Status2Reg, status2) ||
      !readReg(kAxp2101BatteryPercentReg, percent) ||
      !readRegs(kAxp2101AdcResult0Reg, batteryRaw, sizeof(batteryRaw)) ||
      !readRegs(kAxp2101AdcResult4Reg, vbusRaw, sizeof(vbusRaw)) ||
      !readRegs(kAxp2101AdcResult6Reg, systemRaw, sizeof(systemRaw)) ||
      !readRegs(kAxp2101AdcResult8Reg, tempRaw, sizeof(tempRaw))) {
    state_.ready = false;
    lastError_ = "axp2101_status_read_failed";
    return false;
  }

  const uint16_t batteryVoltage =
      static_cast<uint16_t>(((batteryRaw[0] & 0x1F) << 8) | batteryRaw[1]);
  const uint16_t vbusVoltage =
      static_cast<uint16_t>(((vbusRaw[0] & 0x3F) << 8) | vbusRaw[1]);
  const uint16_t systemVoltage =
      static_cast<uint16_t>(((systemRaw[0] & 0x3F) << 8) | systemRaw[1]);
  const uint16_t temperatureRaw =
      static_cast<uint16_t>(((tempRaw[0] & 0x3F) << 8) | tempRaw[1]);

  out.batteryPresent   = (status1 & 0x08) != 0;
  const bool vbusGood  = (status1 & 0x20) != 0;
  out.vbusPresent      = ((status2 & 0x08) == 0) && vbusGood;
  out.charging         = ((status2 >> 5) == 0x01);
  out.batteryVoltageMv = out.batteryPresent ? axp::scaleVoltageMv(batteryVoltage) : 0;
  out.vbusVoltageMv    = out.vbusPresent    ? axp::scaleVoltageMv(vbusVoltage)    : 0;
  out.systemVoltageMv  = axp::scaleVoltageMv(systemVoltage);
  out.batteryPercent   = out.batteryPresent ? static_cast<int>(percent) : -1;
  out.temperatureC     = axp::decodeTemperatureC(static_cast<int16_t>(temperatureRaw));
  out.present          = state_.present;
  out.ready            = true;
  out.chipId           = state_.chipId;

  state_ = out;
  lastError_ = "";
  return true;
}

bool Axp2101::disableAdcForSleep() {
  // Verbatim from Barebone lines 1729-1747 (feature-gate checks removed per
  // porting convention; gating lives in HubRuntime).
  if (!state_.present || !state_.ready) {
    return true;
  }
  // Stop the AXP2101 batt/vbus/sys/temp ADC continuous conversion for the sleep window.
  // configureCharging() re-enables it (reg 0x30 |= 0x1D) on the wake reboot.
  uint8_t adcCtrl = 0;
  if (!readReg(kAxp2101AdcChannelCtrlReg, adcCtrl)) {
    return false;
  }
  adcCtrl &= static_cast<uint8_t>(~0x1DU);
  return writeReg(kAxp2101AdcChannelCtrlReg, adcCtrl);
}

bool Axp2101::powerDown() {
  // Disable ADC measurements then put IC in lowest quiescent state.
  if (!disableAdcForSleep()) {
    return false;
  }
  // Turn off LDOs (ALDO1 off, DLDO2 already off).
  if (!writeReg(kAxp2101LdoOnOffCtrl0Reg, 0x00)) {
    lastError_ = "axp2101_ldo_powerdown_failed";
    return false;
  }
  lastError_ = "";
  return true;
}
