/*
 * SdLogger.hpp — micro-SD card storage device (SPI mode) + CSV data logger.
 *
 * The card (J7) sits on the SPI bus: CS=GPIO10, MOSI/CMD=GPIO11, MISO/DAT0=
 * GPIO13, SCK=GPIO12, with 22R series resistors. begin() brings up the SPI bus
 * and mounts the card; checkAndReport() prints card info and runs a
 * write/read/delete self-test; appendLine() appends a line to the CSV log,
 * opening and closing the file per write so a yanked card can't corrupt a held
 * handle.
 *
 * Implementation: src/storage/SdLogger.cpp
 */

#pragma once

#include <Arduino.h>

class SdLogger {
 public:
  // Config — built from AppConfig::Spi / AppConfig::Sd.
  struct Config {
    int pinSck   = -1;
    int pinMiso  = -1;
    int pinMosi  = -1;
    int pinCs    = -1;
    int pinCardDetect = -1;
    uint32_t freqHz = 10000000;
    const char* logPath   = "/log.csv";
    const char* logHeader = "";  // written once when a fresh log is created
  };

  explicit SdLogger(const Config& config);

  /*
   * begin() — Start the SPI bus and mount the card. On success, ensures the log
   * file exists with its header. Returns true if mounted. Safe to call once.
   */
  bool begin();

  /* mounted() — True once begin() succeeded and the card is usable. */
  bool mounted() const { return mounted_; }

  /*
   * checkAndReport() — Print card-detect state, type, size, root listing, and a
   * non-destructive write/read/delete self-test result to out.
   */
  void checkAndReport(Stream& out);

  /*
   * appendLine() — Append one line (a newline is added) to the CSV log. Drops
   * the mounted state and returns false if the write fails.
   */
  bool appendLine(const String& line);

  /* logPath() — Path of the CSV log file (for download streaming). */
  const char* logPath() const { return cfg_.logPath; }

  /* clearLog() — Delete the log and rewrite just the header. Returns success. */
  bool clearLog();

 private:
  Config cfg_;
  bool mounted_ = false;
};
