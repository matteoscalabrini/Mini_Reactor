/*
 * Tmc2209Motor.cpp — TMC2209 single-wire UART driver device.
 *
 * See include/motor/Tmc2209Motor.hpp and AppConfig::Motor for wiring. Motion is
 * produced by the chip's internal step generator (VACTUAL register) over UART;
 * the STEP/DIR pins are held idle.
 */

#include "motor/Tmc2209Motor.hpp"
#include "motor/RpmKinematics.hpp"

namespace {
constexpr float kTmcClockHz = 12000000.0f;  // internal oscillator
}  // namespace

Tmc2209Motor::Tmc2209Motor(const Config& config)
    : cfg_(config),
      driver_(config.serial, config.rSenseOhms, config.address) {}

bool Tmc2209Motor::begin() {
  // EN high = disabled while we configure (active-low enable).
  pinMode(cfg_.pinEnable, OUTPUT);
  digitalWrite(cfg_.pinEnable, HIGH);

  // STEP/DIR idle — motion comes from VACTUAL over UART.
  pinMode(cfg_.pinStep, OUTPUT);
  digitalWrite(cfg_.pinStep, LOW);
  pinMode(cfg_.pinDir, OUTPUT);
  digitalWrite(cfg_.pinDir, LOW);
  pinMode(cfg_.pinDiag, INPUT);
  pinMode(cfg_.pinIndex, INPUT);

  // Start UART on the PDN_UART pins. ESP32: begin(baud, cfg, rx, tx).
  cfg_.serial->begin(cfg_.uartBaud, SERIAL_8N1, cfg_.pinUartRx, cfg_.pinUartTx);

  driver_.begin();
  driver_.toff(5);                       // enable the chopper
  driver_.rms_current(cfg_.currentMilliamps);
  driver_.microsteps(cfg_.microsteps);
  driver_.pwm_autoscale(true);           // stealthChop current control
  driver_.en_spreadCycle(false);         // quiet stealthChop
  driver_.VACTUAL(0);                    // start stationary

  enable(true);                          // EN low = outputs on
  return connected();
}

bool Tmc2209Motor::connected() { return driver_.test_connection() == 0; }

uint8_t Tmc2209Motor::version() { return driver_.version(); }

void Tmc2209Motor::enable(bool on) {
  enabled_ = on;
  digitalWrite(cfg_.pinEnable, on ? LOW : HIGH);  // active-low
}

void Tmc2209Motor::setCurrentMilliamps(uint16_t milliamps) {
  currentMa_ = milliamps;
  driver_.rms_current(milliamps);
}

void Tmc2209Motor::setMicrosteps(uint16_t microsteps) {
  cfg_.microsteps = microsteps;
  driver_.microsteps(microsteps);
  if (lastRpm_ > 0.0f) setRpm(lastRpm_);  // keep rpm constant across resolution change
}

void Tmc2209Motor::spin(int32_t velocity) {
  driver_.shaft(velocity < 0);  // GCONF shaft bit sets direction
  const uint32_t magnitude =
      static_cast<uint32_t>(velocity < 0 ? -velocity : velocity);
  driver_.VACTUAL(magnitude);
}

void Tmc2209Motor::stop() { spin(0); }

void Tmc2209Motor::setSpeedPercent(float percent) {
  if (percent > 100.0f) percent = 100.0f;
  if (percent < -100.0f) percent = -100.0f;
  spin(vactualForMicrostepHz(cfg_.topSpeedMicrostepHz * percent / 100.0f));
}

void Tmc2209Motor::setRpm(float rpm) {
  lastRpm_ = (rpm < 0.0f) ? 0.0f : rpm;
  const int32_t v =
      RpmKinematics::rpmToVactual(lastRpm_, cfg_.stepsPerRev, cfg_.microsteps);
  spin(reverse_ ? -v : v);  // spin() sets the shaft (direction) bit from the sign
}

void Tmc2209Motor::setDirection(bool reverse) {
  reverse_ = reverse;
  if (lastRpm_ > 0.0f) setRpm(lastRpm_);
}

uint32_t Tmc2209Motor::drvStatus() { return driver_.DRV_STATUS(); }

int32_t Tmc2209Motor::vactualForMicrostepHz(float microstepsPerSecond) {
  // speed[µsteps/s] = VACTUAL * fCLK / 2^24  →  VACTUAL = speed * 2^24 / fCLK
  const float vactual = microstepsPerSecond * 16777216.0f / kTmcClockHz;
  return static_cast<int32_t>(lroundf(vactual));
}
