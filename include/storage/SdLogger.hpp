/*
 * SdLogger.hpp — micro-SD card storage device (SPI mode) + CSV data logger.
 *
 * The card (J7) sits on the SPI bus: CS=GPIO10, MOSI/CMD=GPIO11, MISO/DAT0=
 * GPIO13, SCK=GPIO12, with 22R series resistors. begin() brings up the SPI bus
 * and mounts the card; checkAndReport() prints card info and runs a
 * write/read/delete self-test. Logging is run-only: appendLine() writes rows to
 * the currently open per-run file (/runs/NNNNN.csv) and is a no-op when no run is
 * open — there is no always-on legacy log file.
 *
 * Implementation: src/storage/SdLogger.cpp
 */

#pragma once

#include <Arduino.h>
#include <FS.h>
#include <Preferences.h>

#include <vector>

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
    const char* logHeader = "";  // CSV header line, written at the top of each run file
    uint32_t logIntervalMs = 10000;  // default SD log row interval
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
   * appendLine() — Append one line (a newline is added) to the currently open
   * run file. No-op returning false when no run is open or the card isn't mounted.
   */
  bool appendLine(const String& line);

  /* eraseAll() — Delete every file/dir on the card (all runs). Destructive.
   * Returns success. */
  bool eraseAll();

  // ── Per-run files (/runs/NNNNN.csv + optional NNNNN.name sidecar) ──
  struct RunInfo {
    int id = 0;
    String label;       // sidecar name, or "Run <id>"
    uint32_t bytes = 0; // CSV size
  };

  /* startRun() — Create /runs/<next-id>.csv (with header) and open it as the
   * current run. Writes /runs/<id>.name when `name` is non-empty (caller has
   * already sanitized it). Returns the new id, or 0 on failure. */
  int startRun(const char* name);

  /* endRun() — Close the current run file. If !save, delete the CSV + sidecar.
   * No-op when no run is open. */
  void endRun(bool save);

  int currentRunId() const { return currentId_; }
  const char* currentRunName() const { return currentName_; }

  /* listRuns() — Enumerate saved runs (id, label, bytes). */
  std::vector<RunInfo> listRuns();

  /* latestRunId() — Highest existing run id, or 0 when none. */
  int latestRunId();

  /* runCsvPath() — SD path of a run's CSV (for download streaming). */
  String runCsvPath(int id);

  /* deleteRun() — Remove a run's CSV + sidecar. Returns success. */
  bool deleteRun(int id);

  /* Data-log row interval — runtime-settable (1..3600 s), persisted in NVS. */
  void setLogIntervalSec(uint32_t seconds);
  uint32_t logIntervalSec() const { return logIntervalMs_ / 1000; }
  uint32_t logIntervalMs() const { return logIntervalMs_; }

 private:
  Config cfg_;
  bool mounted_ = false;
  // Delete everything under `path` (not `path` itself). Skips entries it can't
  // remove and keeps going; returns true only if the directory was fully cleared.
  bool removeRecursive(const char* path);

  // Current open run, if any. currentId_ == 0 means "no run open".
  File current_;
  int currentId_ = 0;
  char currentName_[33] = {0};

  uint32_t logIntervalMs_ = 10000;  // SD log row interval (NVS-persisted)
  Preferences prefs_;
};
