/*
 * Pcf85063.hpp — PCF85063 real-time clock driver.
 * Wraps I2C communication with the NXP PCF85063A/TP RTC IC.
 * begin() probes, verifies RAM r/w, and configures 24-hour + running mode.
 * refresh() reads the 7-byte BCD time block (reg 0x04).
 * setDateTime() encodes and writes time registers.
 * BCD helpers live in Pcf85063Bcd.hpp (host-testable, no Arduino dependency).
 * Implementation: src/features/hub/rtc/Pcf85063.cpp
 */
#pragma once
#include <Arduino.h>
#include <Wire.h>

class Pcf85063 {
 public:
  struct DateTime {
    uint16_t year = 2000;
    uint8_t month = 1, day = 1, weekday = 0;
    uint8_t hours = 0, minutes = 0, seconds = 0;
    bool running = false;
  };

  explicit Pcf85063(TwoWire& wire = Wire, uint8_t address = 0x51);

  bool begin();                          // probe + RAM r/w check + 24h + start clock
  bool refresh(DateTime& out);           // read 7 BCD time bytes from reg 0x04
  bool setDateTime(const DateTime& dt);  // encode BCD and write time registers
  const DateTime& state() const { return state_; }

 private:
  static constexpr uint8_t kRegCtrl1 = 0x00;
  static constexpr uint8_t kRegRam   = 0x03;
  static constexpr uint8_t kRegTime  = 0x04;  // sec,min,hr,day,wday,mon,year

  bool readReg(uint8_t reg, uint8_t* buf, size_t n);
  bool writeReg(uint8_t reg, const uint8_t* buf, size_t n);

  TwoWire& wire_;
  uint8_t  address_;
  DateTime state_;
};
