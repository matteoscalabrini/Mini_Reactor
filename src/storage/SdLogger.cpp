/*
 * SdLogger.cpp — micro-SD storage device (SPI) + CSV logger.
 * See include/storage/SdLogger.hpp and AppConfig::Spi / AppConfig::Sd.
 */

#include "storage/SdLogger.hpp"

#include <FS.h>
#include <SD.h>
#include <SPI.h>

#include <vector>

namespace {
const char* cardTypeStr(uint8_t type) {
  switch (type) {
    case CARD_NONE: return "none";
    case CARD_MMC:  return "MMC";
    case CARD_SD:   return "SDSC";
    case CARD_SDHC: return "SDHC/SDXC";
    default:        return "unknown";
  }
}
}  // namespace

SdLogger::SdLogger(const Config& config) : cfg_(config) {}

bool SdLogger::begin() {
  pinMode(cfg_.pinCardDetect, INPUT_PULLUP);

  // SD.begin manages the CS pin; bind it to the SPI bus here.
  SPI.begin(cfg_.pinSck, cfg_.pinMiso, cfg_.pinMosi, cfg_.pinCs);

  if (!SD.begin(cfg_.pinCs, SPI, cfg_.freqHz)) {
    mounted_ = false;
    return false;
  }
  if (SD.cardType() == CARD_NONE) {
    SD.end();
    mounted_ = false;
    return false;
  }
  mounted_ = true;

  // Write the CSV header once for a fresh log (don't wipe an existing one).
  if (cfg_.logHeader[0] != '\0' && !SD.exists(cfg_.logPath)) {
    File h = SD.open(cfg_.logPath, FILE_WRITE);
    if (h) {
      h.println(cfg_.logHeader);
      h.close();
    }
  }
  return true;
}

void SdLogger::checkAndReport(Stream& out) {
  out.printf("\n[SD] card-detect (GPIO%d): %s\n", cfg_.pinCardDetect,
             digitalRead(cfg_.pinCardDetect) ? "HIGH" : "LOW");

  if (!mounted_) {
    out.println(F("  not mounted (card seated? FAT32? wiring?)"));
    return;
  }

  out.printf("  type=%s  size=%llu MB\n", cardTypeStr(SD.cardType()),
             SD.cardSize() / (1024ULL * 1024ULL));
  out.printf("  FS: used %llu / %llu MB\n",
             SD.usedBytes() / (1024ULL * 1024ULL),
             SD.totalBytes() / (1024ULL * 1024ULL));

  out.println(F("  root:"));
  File root = SD.open("/");
  if (root && root.isDirectory()) {
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
      out.printf("    %-20s %s%u\n", f.name(), f.isDirectory() ? "<dir>" : "",
                 f.isDirectory() ? 0 : (unsigned)f.size());
      f.close();
    }
  }
  if (root) root.close();

  // Non-destructive write/read/delete round-trip.
  const char* path = "/reactor_selftest.txt";
  const char* msg = "mini-reactor sd ok";
  bool rw = false;
  File w = SD.open(path, FILE_WRITE);
  if (w) {
    w.println(msg);
    w.close();
    File r = SD.open(path, FILE_READ);
    if (r) {
      rw = r.readStringUntil('\n').startsWith(msg);
      r.close();
    }
    SD.remove(path);
  }
  out.printf("  read/write test: %s\n", rw ? "PASS" : "FAIL");
}

bool SdLogger::appendLine(const String& line) {
  if (!mounted_) return false;
  File f = SD.open(cfg_.logPath, FILE_APPEND);
  if (!f) {
    mounted_ = false;
    return false;
  }
  f.println(line);
  f.close();
  return true;
}

bool SdLogger::clearLog() {
  if (!mounted_) return false;
  SD.remove(cfg_.logPath);
  File h = SD.open(cfg_.logPath, FILE_WRITE);
  if (!h) {
    mounted_ = false;
    return false;
  }
  if (cfg_.logHeader[0] != '\0') h.println(cfg_.logHeader);
  h.close();
  return true;
}

bool SdLogger::removeRecursive(const char* path) {
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return false; }

  // Snapshot all child paths first — deleting entries mid-iteration is
  // unreliable on FAT, and we must not re-scan after each delete (an entry we
  // fail to remove would be handed back forever).
  std::vector<String> files;
  std::vector<String> subdirs;
  for (File entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    (entry.isDirectory() ? subdirs : files).push_back(entry.path());
    entry.close();
  }
  dir.close();

  // Delete every child, skipping (not bailing on) any that won't go — e.g. an
  // OS-created hidden/system entry shouldn't strand the files after it.
  bool cleared = true;
  for (const String& f : files) {
    if (!SD.remove(f.c_str())) cleared = false;
  }
  for (const String& d : subdirs) {
    if (!removeRecursive(d.c_str())) cleared = false;  // empty it first
    if (!SD.rmdir(d.c_str())) cleared = false;          // then drop the dir
  }
  return cleared;
}

bool SdLogger::eraseAll() {
  if (!mounted_) return false;
  const bool cleared = removeRecursive("/");  // best-effort full wipe
  const bool logOk = clearLog();              // recreate the empty log with its header
  return cleared && logOk;                    // false if any entry resisted deletion
}
