# Firmware Run/History Persistence + Naming — Implementation Plan (Plan A)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the ESP32 firmware persist each run as its own SD file with an optional session name, and expose runs over HTTP so the existing History UI works on-device — with the name shown as the run's label.

**Architecture:** Pure, host-tested run-file helpers (id format/parse, next-id, name sanitize, label) live in a header so the ESP32 SD code and the Unity tests share one implementation. `SdLogger` is refactored to own a *current* per-run CSV plus a `.name` sidecar (`startRun`/`appendLine`-to-current/`endRun`/`listRuns`/`deleteRun`/`runCsvPath`/`latestRunId`). `AppRuntime` orchestrates the run lifecycle (open on start, log to current, finalize on stop / duration-timeout) and rebuilds a cached runs-list JSON. `WebInterface` adds the `/api/v1/runs*` routes and extends the run command with `name` + `stop{data}`. Async callbacks never touch the SD card — they queue commands and read cached JSON; all SD work runs in loop context.

**Tech Stack:** C++17, Arduino-ESP32 (PlatformIO `espressif32@6.9.0`), ArduinoJson v7, ESPAsyncWebServer 3.3.x. Pure helpers unit-tested under `env:native` with Unity.

## Global Constraints

- **No SD / hardware access in async web callbacks.** Handlers only validate input, queue into `pending_` under `mutex_`, or read cached JSON strings. All SD/reactor work happens in `applyPending()` / the main loop. (Copied from the existing `WebInterface` threading model.)
- **Run identity:** run id = (max existing id in `/runs`) + 1, starting at 1. Files: `/runs/NNNNN.csv` (5-digit zero-padded). Name sidecar: `/runs/NNNNN.name`.
- **Label rule:** `label = sanitizedName` if non-empty, else `"Run " + id`.
- **Name sanitization (authoritative, server-side):** trim leading/trailing spaces, strip control chars (byte `< 0x20`), truncate to **32 bytes**.
- **CSV header (unchanged):** `t_ms,running,liquid_c,heater_c,setpoint_c,heater_pct,rpm,load,fault,safety` (`AppConfig::Sd::kLogHeader`).
- **ArduinoJson v7 API** as used in `AppRuntime::buildStatusJson` (`doc["x"].to<JsonObject>()`, assign with `obj["k"] = v`, `nullptr` for null).
- **Back-compat:** `GET /api/v1/log` keeps working — it serves the latest run's CSV when runs exist, else the legacy log file.
- **Testing reality:** pure logic is unit-tested on host (`env:native`, Unity); SD/Web/lifecycle integration is verified by `pio run` (compile) + on-device HTTP validation (per the saved hardware-validation setup: flash, find IP in the serial boot log, drive the HTTP API on port 80).

---

## File Structure

| File | Responsibility |
|------|----------------|
| `include/storage/RunFiles.hpp` | **New.** Pure helpers (no Arduino): `csvPath`, `namePath`, `parseId`, `nextId`, `sanitizeName`, `label`. Shared by `SdLogger` + Unity tests. |
| `test/test_run_files/test_main.cpp` | **New.** Unity host tests for `RunFiles`. |
| `include/storage/SdLogger.hpp` | **Modify.** Add per-run API + `RunInfo`; keep existing single-log API. |
| `src/storage/SdLogger.cpp` | **Modify.** Implement per-run files + `.name` sidecar; route `appendLine` to the current run; `eraseAll` already clears `/runs` via `removeRecursive("/")`. |
| `include/net/WebInterface.hpp` | **Modify.** `Pending` gains run-name / stop-data / delete fields; add `cacheRunsJson` + `runsJson_`. |
| `src/net/WebInterface.cpp` | **Modify.** Extend run handler; add `GET /api/v1/runs`, `GET /api/v1/runs/<id>`, `POST /api/v1/runs/<id>/delete`; alias `/api/v1/log` to latest run; wire `applyPending`. |
| `src/system/AppRuntime.cpp` | **Modify.** Lifecycle orchestration (auto-stop finalize), cached runs JSON, `run.id`/`run.name` in status. |
| `platformio.ini` | **Modify.** Add `-DASYNCWEBSERVER_REGEX=1` so path-parameter routes work. |

Tasks are ordered so each ends with an independently verifiable deliverable. Task 1 is fully host-TDD. Tasks 2–5 are integration: each ends with a clean `pio run` compile and (where noted) a concrete on-device check.

---

## Task 1: RunFiles pure helpers (host-TDD)

**Files:**
- Create: `include/storage/RunFiles.hpp`
- Test: `test/test_run_files/test_main.cpp`

**Interfaces:**
- Produces (all in `namespace RunFiles`, `#include <string>`, `#include <vector>`):
  - `std::string csvPath(int id);` → `"/runs/00007.csv"`
  - `std::string namePath(int id);` → `"/runs/00007.name"`
  - `int parseId(const std::string& nameOrPath);` → run id if the basename matches `NNNNN.csv`, else `-1`
  - `int nextId(const std::vector<int>& ids);` → `max(ids)+1`, or `1` if empty
  - `std::string sanitizeName(const std::string& raw, std::size_t maxLen = 32);`
  - `std::string label(int id, const std::string& name);`

- [ ] **Step 1: Write the failing test**

Create `test/test_run_files/test_main.cpp`:

```cpp
#include <unity.h>
#include <string>
#include <vector>
#include "storage/RunFiles.hpp"

void setUp() {}
void tearDown() {}

void test_csv_and_name_path_zero_pad() {
  TEST_ASSERT_EQUAL_STRING("/runs/00007.csv", RunFiles::csvPath(7).c_str());
  TEST_ASSERT_EQUAL_STRING("/runs/00007.name", RunFiles::namePath(7).c_str());
  TEST_ASSERT_EQUAL_STRING("/runs/12345.csv", RunFiles::csvPath(12345).c_str());
}

void test_parse_id_basename_and_path() {
  TEST_ASSERT_EQUAL_INT(7, RunFiles::parseId("00007.csv"));
  TEST_ASSERT_EQUAL_INT(7, RunFiles::parseId("/runs/00007.csv"));
  TEST_ASSERT_EQUAL_INT(42, RunFiles::parseId("00042.csv"));
}

void test_parse_id_rejects_non_run_files() {
  TEST_ASSERT_EQUAL_INT(-1, RunFiles::parseId("00007.name"));
  TEST_ASSERT_EQUAL_INT(-1, RunFiles::parseId("reactor_log.csv"));
  TEST_ASSERT_EQUAL_INT(-1, RunFiles::parseId("/runs/notes.txt"));
  TEST_ASSERT_EQUAL_INT(-1, RunFiles::parseId(""));
}

void test_next_id() {
  TEST_ASSERT_EQUAL_INT(1, RunFiles::nextId({}));
  TEST_ASSERT_EQUAL_INT(8, RunFiles::nextId({1, 7, 3}));
  TEST_ASSERT_EQUAL_INT(2, RunFiles::nextId({1}));
}

void test_sanitize_trims_strips_ctrl_truncates() {
  TEST_ASSERT_EQUAL_STRING("Ethanol distillation",
      RunFiles::sanitizeName("  Ethanol distillation  ").c_str());
  TEST_ASSERT_EQUAL_STRING("ab", RunFiles::sanitizeName(std::string("a\nb\t")).c_str());
  // 40 'x' truncates to 32
  TEST_ASSERT_EQUAL_UINT32(32u, (uint32_t)RunFiles::sanitizeName(std::string(40, 'x')).size());
  TEST_ASSERT_EQUAL_STRING("", RunFiles::sanitizeName("   ").c_str());
}

void test_label_fallback() {
  TEST_ASSERT_EQUAL_STRING("Run 7", RunFiles::label(7, "").c_str());
  TEST_ASSERT_EQUAL_STRING("Buffer prep", RunFiles::label(7, "Buffer prep").c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_csv_and_name_path_zero_pad);
  RUN_TEST(test_parse_id_basename_and_path);
  RUN_TEST(test_parse_id_rejects_non_run_files);
  RUN_TEST(test_next_id);
  RUN_TEST(test_sanitize_trims_strips_ctrl_truncates);
  RUN_TEST(test_label_fallback);
  return UNITY_END();
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `pio test -e native -f test_run_files`
Expected: FAIL — `fatal error: storage/RunFiles.hpp: No such file or directory`.

- [ ] **Step 3: Write the minimal implementation**

Create `include/storage/RunFiles.hpp`:

```cpp
/*
 * RunFiles.hpp — pure helpers for per-run SD file naming and labels.
 * No Arduino dependencies, so the firmware and host (Unity) tests share one
 * implementation. Paths use the /runs/NNNNN.csv layout.
 */
#pragma once

#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

namespace RunFiles {

inline std::string pad5(int id) {
  std::string s = std::to_string(id < 0 ? 0 : id);
  if (s.size() < 5) s.insert(0, 5 - s.size(), '0');
  return s;
}

inline std::string csvPath(int id)  { return "/runs/" + pad5(id) + ".csv"; }
inline std::string namePath(int id) { return "/runs/" + pad5(id) + ".name"; }

// Return the run id if `nameOrPath`'s basename is exactly NNNNN.csv (N = digit),
// else -1. Accepts both "00007.csv" and "/runs/00007.csv".
inline int parseId(const std::string& nameOrPath) {
  const std::size_t slash = nameOrPath.find_last_of('/');
  const std::string base =
      (slash == std::string::npos) ? nameOrPath : nameOrPath.substr(slash + 1);
  const std::string suffix = ".csv";
  if (base.size() <= suffix.size()) return -1;
  if (base.compare(base.size() - suffix.size(), suffix.size(), suffix) != 0) return -1;
  const std::string stem = base.substr(0, base.size() - suffix.size());
  if (stem.empty()) return -1;
  for (char c : stem) if (!std::isdigit(static_cast<unsigned char>(c))) return -1;
  return std::stoi(stem);
}

inline int nextId(const std::vector<int>& ids) {
  int mx = 0;
  for (int id : ids) if (id > mx) mx = id;
  return mx + 1;
}

inline std::string sanitizeName(const std::string& raw, std::size_t maxLen = 32) {
  std::string out;
  out.reserve(raw.size());
  for (char c : raw) {
    if (static_cast<unsigned char>(c) >= 0x20) out.push_back(c);  // drop control chars
  }
  const std::size_t b = out.find_first_not_of(' ');
  const std::size_t e = out.find_last_not_of(' ');
  out = (b == std::string::npos) ? "" : out.substr(b, e - b + 1);
  if (out.size() > maxLen) out.resize(maxLen);
  return out;
}

inline std::string label(int id, const std::string& name) {
  return name.empty() ? ("Run " + std::to_string(id)) : name;
}

}  // namespace RunFiles
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `pio test -e native -f test_run_files`
Expected: PASS — `6 Tests 0 Failures 0 Ignored OK`.

- [ ] **Step 5: Commit**

```bash
git add include/storage/RunFiles.hpp test/test_run_files/test_main.cpp
git commit -m "feat(fw): pure run-file helpers (id/name/label) with host tests"
```

---

## Task 2: SdLogger per-run files + name sidecar

**Files:**
- Modify: `include/storage/SdLogger.hpp`
- Modify: `src/storage/SdLogger.cpp`

**Interfaces:**
- Consumes: `RunFiles::*` (Task 1).
- Produces (new public `SdLogger` members):
  - `int startRun(const char* name);` — pick next id, create `/runs/<id>.csv` with the header, open it as the current file, write `/runs/<id>.name` when `name` is non-empty (already sanitized by the caller). Returns the id (≥1), or `0` on failure.
  - `void endRun(bool save);` — close the current file; if `!save`, delete the CSV and its sidecar; clear current state.
  - `int currentRunId() const;` — current run id, or `0` when idle.
  - `const char* currentRunName() const;` — current run's name (`""` when unnamed/idle).
  - `struct RunInfo { int id; String label; uint32_t bytes; };`
  - `std::vector<RunInfo> listRuns();` — enumerate `/runs/*.csv`, newest selection left to callers; label from the sidecar (fallback `Run <id>`).
  - `int latestRunId();` — highest existing run id, or `0`.
  - `String runCsvPath(int id);` — SD path for streaming (`"/runs/NNNNN.csv"`).
  - `bool deleteRun(int id);` — remove the CSV + sidecar.
  - `appendLine(line)` now writes to the **current run file** when one is open, else the legacy `cfg_.logPath` (unchanged signature).

- [ ] **Step 1: Add the per-run API to the header**

In `include/storage/SdLogger.hpp`, add `#include <vector>` near the top includes, then inside `class SdLogger` (after the existing `eraseAll()` declaration, before `private:`) add:

```cpp
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
```

Then in the `private:` section (after `bool removeRecursive(const char* path);`) add:

```cpp
  // Current open run, if any. currentId_ == 0 means "no run open".
  File current_;
  int currentId_ = 0;
  char currentName_[33] = {0};
```

Add `#include <FS.h>` to the header includes (for the `File current_` member) — place it after `#include <Arduino.h>`.

- [ ] **Step 2: Implement the per-run methods**

In `src/storage/SdLogger.cpp`, add the include near the top (after the existing includes):

```cpp
#include "storage/RunFiles.hpp"
```

Append these method definitions at the end of the file:

```cpp
int SdLogger::startRun(const char* name) {
  if (!mounted_) return 0;
  if (!SD.exists("/runs")) SD.mkdir("/runs");

  // Next id = max existing + 1.
  std::vector<int> ids;
  File dir = SD.open("/runs");
  if (dir && dir.isDirectory()) {
    for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
      const int id = RunFiles::parseId(e.name());
      if (id > 0) ids.push_back(id);
      e.close();
    }
  }
  if (dir) dir.close();
  const int id = RunFiles::nextId(ids);

  File f = SD.open(RunFiles::csvPath(id).c_str(), FILE_WRITE);
  if (!f) { mounted_ = false; return 0; }
  if (cfg_.logHeader[0] != '\0') f.println(cfg_.logHeader);
  f.close();

  currentId_ = id;
  currentName_[0] = '\0';
  if (name && name[0] != '\0') {
    strncpy(currentName_, name, sizeof(currentName_) - 1);
    File n = SD.open(RunFiles::namePath(id).c_str(), FILE_WRITE);
    if (n) { n.print(currentName_); n.close(); }
  }
  current_ = SD.open(RunFiles::csvPath(id).c_str(), FILE_APPEND);
  if (!current_) { mounted_ = false; currentId_ = 0; return 0; }
  return id;
}

void SdLogger::endRun(bool save) {
  if (currentId_ == 0) return;
  if (current_) current_.close();
  if (!save) {
    SD.remove(RunFiles::csvPath(currentId_).c_str());
    SD.remove(RunFiles::namePath(currentId_).c_str());
  }
  currentId_ = 0;
  currentName_[0] = '\0';
}

std::vector<SdLogger::RunInfo> SdLogger::listRuns() {
  std::vector<RunInfo> out;
  if (!mounted_) return out;
  File dir = SD.open("/runs");
  if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return out; }
  for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
    const int id = RunFiles::parseId(e.name());
    if (id > 0) {
      RunInfo info;
      info.id = id;
      info.bytes = (uint32_t)e.size();
      String nm;
      File n = SD.open(RunFiles::namePath(id).c_str(), FILE_READ);
      if (n) { nm = n.readStringUntil('\n'); nm.trim(); n.close(); }
      info.label = nm.length() ? nm : (String("Run ") + id);
      out.push_back(info);
    }
    e.close();
  }
  dir.close();
  return out;
}

int SdLogger::latestRunId() {
  int mx = 0;
  for (const RunInfo& r : listRuns()) if (r.id > mx) mx = r.id;
  return mx;
}

String SdLogger::runCsvPath(int id) { return String(RunFiles::csvPath(id).c_str()); }

bool SdLogger::deleteRun(int id) {
  if (!mounted_) return false;
  if (id == currentId_) endRun(false);     // can't delete the open run; discard it
  SD.remove(RunFiles::namePath(id).c_str());
  return SD.remove(RunFiles::csvPath(id).c_str());
}
```

- [ ] **Step 3: Route `appendLine` to the current run file**

In `src/storage/SdLogger.cpp`, replace the body of `appendLine`:

```cpp
bool SdLogger::appendLine(const String& line) {
  if (!mounted_) return false;
  if (currentId_ != 0 && current_) {       // an open run takes the data
    current_.println(line);
    current_.flush();                       // survive a yanked card mid-run
    return true;
  }
  File f = SD.open(cfg_.logPath, FILE_APPEND);  // idle fallback: legacy log
  if (!f) {
    mounted_ = false;
    return false;
  }
  f.println(line);
  f.close();
  return true;
}
```

- [ ] **Step 4: Compile**

Run: `pio run -e esp32-s3-devkitc-1`
Expected: `[SUCCESS]`. (Fix any signature mismatches against the header before moving on.)

- [ ] **Step 5: Commit**

```bash
git add include/storage/SdLogger.hpp src/storage/SdLogger.cpp
git commit -m "feat(fw): SdLogger per-run files + name sidecar"
```

---

## Task 3: WebInterface — run command extension + runs routes

**Files:**
- Modify: `platformio.ini`
- Modify: `include/net/WebInterface.hpp`
- Modify: `src/net/WebInterface.cpp`

**Interfaces:**
- Consumes: `SdLogger::startRun/endRun/listRuns/latestRunId/runCsvPath/deleteRun` (Task 2).
- Produces:
  - `void WebInterface::cacheRunsJson(const String& runsJson);` (mirrors `cacheCalJson`) — AppRuntime (Task 4) calls this each loop.
  - Routes: `GET /api/v1/runs` (serves the cached runs JSON), `GET /api/v1/runs/<id>` (streams the CSV), `POST /api/v1/runs/<id>/delete` (queues a delete).
  - Extended `POST /api/v1/run`: `start` accepts `name`; `stop` accepts `data: "save"|"discard"`.
  - `Pending` fields consumed by `applyPending` and (Task 4) by AppRuntime for lifecycle.

- [ ] **Step 1: Enable regex routes**

In `platformio.ini`, under `[env:esp32-s3-devkitc-1]`, extend `build_flags` so path-parameter routes compile:

```ini
build_flags =
    -DARDUINO_USB_CDC_ON_BOOT=1   ; route Serial over the native USB CDC port
    -DASYNCWEBSERVER_REGEX=1      ; enable regex routes (/api/v1/runs/<id>)
```

- [ ] **Step 2: Extend the Pending struct + add the cache API**

In `include/net/WebInterface.hpp`:

Add the public method (after `void cacheCalJson(const String& calJson);`):

```cpp
  /* Cache the runs-list JSON (built in the loop) for GET /runs. */
  void cacheRunsJson(const String& runsJson);
```

Add the cache member (after `String calJson_ = "{}";`):

```cpp
  String runsJson_ = "{\"runs\":[]}";
```

In the `Pending` struct, replace:

```cpp
    bool runStart = false;
    float runTargetC = 0;
    float runRpm = 0;
    uint16_t runDurMin = 0;
    bool runStop = false;
```

with:

```cpp
    bool runStart = false;
    float runTargetC = 0;
    float runRpm = 0;
    uint16_t runDurMin = 0;
    char runName[33] = {0};      // sanitized session name ("" = unnamed)
    bool runStop = false;
    bool runStopSave = true;     // stop&save vs stop&discard
    bool runDelete = false;
    int  runDeleteId = 0;
```

- [ ] **Step 3: Add `#include` and a sanitize helper in the .cpp**

In `src/net/WebInterface.cpp`, add after the existing includes:

```cpp
#include <string.h>
#include "storage/RunFiles.hpp"
```

Add a file-local helper (after the `sendError` definition, before `registerRoutes`):

```cpp
// Copy a sanitized session name into a fixed buffer (firmware is authoritative).
static void copySanitizedName(char* dst, size_t dstSize, const String& raw) {
  const std::string s = RunFiles::sanitizeName(std::string(raw.c_str()), dstSize - 1);
  strncpy(dst, s.c_str(), dstSize - 1);
  dst[dstSize - 1] = '\0';
}
```

- [ ] **Step 4: Extend the run handler (name + stop data)**

In `registerRoutes()`, replace the whole `start`/`stop` body of the `/api/v1/run` handler (the `if (action == "start") { … } else if (action == "stop") { … } else { … }` block) with:

```cpp
        if (action == "start") {
          const float rpm = o["rpm"] | 8.0f;
          const float targetC = o["targetC"] | 36.0f;
          if (rpm < 0.0f || rpm > 30.0f) {  // kMinRpm..kMaxRpm
            sendError(req, 400, "out_of_range", "rpm must be 0..30");
            return;
          }
          if (targetC < 0.0f || targetC > 55.0f) {  // processMaxC ceiling
            sendError(req, 400, "out_of_range", "targetC must be 0..55");
            return;
          }
          const String name = o["name"] | "";
          xSemaphoreTake(mutex_, portMAX_DELAY);
          pending_.runStart = true;
          pending_.runTargetC = targetC;
          pending_.runRpm = rpm;
          pending_.runDurMin = o["durationMin"] | 0;
          copySanitizedName(pending_.runName, sizeof(pending_.runName), name);
          xSemaphoreGive(mutex_);
          sendOk(req);
        } else if (action == "stop") {
          const String data = o["data"] | "save";
          if (data != "save" && data != "discard") {
            sendError(req, 400, "invalid_request", "data must be save|discard");
            return;
          }
          xSemaphoreTake(mutex_, portMAX_DELAY);
          pending_.runStop = true;
          pending_.runStopSave = (data == "save");
          xSemaphoreGive(mutex_);
          sendOk(req);
        } else {
          sendError(req, 400, "invalid_request", "action must be start|stop");
        }
```

- [ ] **Step 5: Add the runs routes**

In `registerRoutes()`, immediately after the `/api/v1/log/clear` handler block (before the `// ── Static UI + SPA fallback ──` comment), add:

```cpp
  // ── GET runs list (served from the loop-built cache) ──
  server_->on("/api/v1/runs", HTTP_GET, [this](AsyncWebServerRequest* req) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const String body = runsJson_;
    xSemaphoreGive(mutex_);
    sendJson(req, body);
  });

  // ── GET one run's CSV (download) ──
  server_->on("^\\/api\\/v1\\/runs\\/([0-9]+)$", HTTP_GET,
              [this](AsyncWebServerRequest* req) {
    const int id = req->pathArg(0).toInt();
    const String path = sd_.runCsvPath(id);
    if (!sd_.mounted() || !SD.exists(path)) {
      sendError(req, 404, "not_found", "no such run");
      return;
    }
    req->send(SD, path, "text/csv", true);
  });

  // ── POST delete a run ──
  server_->on("^\\/api\\/v1\\/runs\\/([0-9]+)\\/delete$", HTTP_POST,
              [this](AsyncWebServerRequest* req) {
    const int id = req->pathArg(0).toInt();
    xSemaphoreTake(mutex_, portMAX_DELAY);
    pending_.runDelete = true;
    pending_.runDeleteId = id;
    xSemaphoreGive(mutex_);
    sendOk(req);
  });
```

- [ ] **Step 6: Alias `/api/v1/log` to the latest run**

Replace the existing `/api/v1/log` GET handler body with:

```cpp
  server_->on("/api/v1/log", HTTP_GET, [this](AsyncWebServerRequest* req) {
    if (!sd_.mounted()) {
      sendError(req, 503, "no_log", "no log file on the SD card");
      return;
    }
    const int latest = sd_.latestRunId();
    const String path = latest > 0 ? sd_.runCsvPath(latest) : String(sd_.logPath());
    if (!SD.exists(path)) {
      sendError(req, 503, "no_log", "no log file on the SD card");
      return;
    }
    req->send(SD, path, "text/csv", true);
  });
```

- [ ] **Step 7: Wire the new commands into applyPending + add cacheRunsJson**

In `applyPending()`, replace:

```cpp
  if (p.runStart) {
    Serial.printf("[CMD] run start: target=%.1fC rpm=%.1f dur=%umin\n",
                  p.runTargetC, p.runRpm, (unsigned)p.runDurMin);
    reactor_.start(p.runTargetC, p.runRpm, p.runDurMin);
  }
  if (p.runStop) { Serial.println("[CMD] run stop"); reactor_.stop(); }
```

with:

```cpp
  if (p.runStart) {
    Serial.printf("[CMD] run start: target=%.1fC rpm=%.1f dur=%umin name='%s'\n",
                  p.runTargetC, p.runRpm, (unsigned)p.runDurMin, p.runName);
    reactor_.start(p.runTargetC, p.runRpm, p.runDurMin);
    if (reactor_.running()) sd_.startRun(p.runName);  // open the per-run file
  }
  if (p.runStop) {
    Serial.printf("[CMD] run stop (%s)\n", p.runStopSave ? "save" : "discard");
    reactor_.stop();
    sd_.endRun(p.runStopSave);
  }
  if (p.runDelete) {
    Serial.printf("[CMD] run delete id=%d\n", p.runDeleteId);
    sd_.deleteRun(p.runDeleteId);
  }
```

Then add the cache setter (after the existing `cacheCalJson` definition):

```cpp
void WebInterface::cacheRunsJson(const String& runsJson) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  runsJson_ = runsJson;
  xSemaphoreGive(mutex_);
}
```

- [ ] **Step 8: Compile**

Run: `pio run -e esp32-s3-devkitc-1`
Expected: `[SUCCESS]`.

- [ ] **Step 9: Commit**

```bash
git add platformio.ini include/net/WebInterface.hpp src/net/WebInterface.cpp
git commit -m "feat(fw): runs routes + name/stop-data on /api/v1/run"
```

---

## Task 4: AppRuntime — lifecycle finalize, runs JSON, run.id/name status

**Files:**
- Modify: `src/system/AppRuntime.cpp`

**Interfaces:**
- Consumes: `SdLogger::listRuns/endRun/currentRunId/currentRunName` (Task 2), `WebInterface::cacheRunsJson` (Task 3), `g_reactor.running()`.
- Produces: cached runs JSON (`{"runs":[{id,label,bytes,current}]}`) and `run.id`/`run.name` in the status JSON.

- [ ] **Step 1: Add `run.id` and `run.name` to the status JSON**

In `src/system/AppRuntime.cpp`, in `buildStatusJson()`, find:

```cpp
  run["durationMin"] = t.durationMin;
```

and add immediately after it (using the same if/else null pattern as the existing `remainingSec` lines, which is known to compile under this ArduinoJson v7 setup):

```cpp
  const int runId = g_sd.currentRunId();
  if (runId) run["id"] = runId;
  else run["id"] = nullptr;
  const char* runName = g_sd.currentRunName();
  if (runId && runName[0]) run["name"] = runName;
  else run["name"] = nullptr;
```

- [ ] **Step 2: Add a runs-JSON builder**

In `src/system/AppRuntime.cpp`, add this function right after `buildStatusJson()` (it needs `#include <vector>` — already pulled in transitively by SdLogger.hpp, which AppRuntime includes; if compilation complains, add `#include <vector>` near the top):

```cpp
// Build the runs-list JSON for GET /api/v1/runs (spec §Runs). current=true marks
// the in-progress run. startedSec/durationSec are omitted (no per-run RTC); the
// UI tolerates their absence.
String buildRunsJson() {
  JsonDocument doc;
  JsonArray arr = doc["runs"].to<JsonArray>();
  const int curId = g_sd.currentRunId();
  for (const SdLogger::RunInfo& r : g_sd.listRuns()) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = r.id;
    o["label"] = r.label;
    o["bytes"] = r.bytes;
    o["current"] = (r.id == curId);
  }
  String out;
  serializeJson(doc, out);
  return out;
}
```

- [ ] **Step 3: Finalize auto-stopped runs + cache the runs JSON in the loop**

In the loop function, find the run-update line:

```cpp
  g_reactor.update();   // run timer
```

and add immediately after it:

```cpp
  // Finalize a run that ended on its own (duration timeout): the reactor stops
  // itself, so close+save the open file here. Web-initiated stops already call
  // endRun() in applyPending.
  static bool prevRunning = false;
  const bool nowRunning = g_reactor.running();
  if (prevRunning && !nowRunning && g_sd.currentRunId() != 0) {
    g_sd.endRun(true);  // auto-stop saves
  }
  prevRunning = nowRunning;
```

Then, in the ~10 Hz status-rebuild block, find:

```cpp
    statusJson = buildStatusJson();
    scanJson = g_wifi.scanJson();
    g_web.cacheCalJson(buildCalJson());
```

and add after it (throttled so the directory scan isn't run at 10 Hz):

```cpp
    static uint32_t lastRunsMs = 0;
    if (now - lastRunsMs >= 1000) {       // refresh the runs list ~1 Hz
      lastRunsMs = now;
      g_web.cacheRunsJson(buildRunsJson());
    }
```

- [ ] **Step 4: Compile**

Run: `pio run -e esp32-s3-devkitc-1`
Expected: `[SUCCESS]`.

- [ ] **Step 5: Commit**

```bash
git add src/system/AppRuntime.cpp
git commit -m "feat(fw): run lifecycle finalize + runs JSON + run.id/name status"
```

---

## Task 5: On-device validation

**Files:** none (verification only).

This task confirms the integration on real hardware (the host build can't exercise SD/Wi-Fi). Use the saved hardware-validation setup: flash, read the device IP from the serial boot log (no mDNS, port 80), then drive the HTTP API. Replace `$IP` below.

- [ ] **Step 1: Flash firmware + filesystem**

```bash
pio run -e esp32-s3-devkitc-1 -t upload
pio run -e esp32-s3-devkitc-1 -t uploadfs
pio device monitor -b 115200   # note the printed IP, then Ctrl-]
```
Expected: boot log shows `[WEB] server up on port 80` and an IP.

- [ ] **Step 2: Start a named run, confirm it appears as the label**

```bash
curl -s -X POST http://$IP/api/v1/run \
  -H 'Content-Type: application/json' \
  -d '{"action":"start","targetC":36,"rpm":8,"durationMin":0,"name":"Ethanol distillation"}'
sleep 12   # let at least one log row write
curl -s http://$IP/api/v1/runs
curl -s http://$IP/api/v1/status | grep -o '"run":{[^}]*}'
```
Expected: `/api/v1/runs` lists one run with `"label":"Ethanol distillation"` and `"current":true`; status `run` shows `"id":1,"name":"Ethanol distillation"`.

- [ ] **Step 3: Stop & save, then download the CSV**

```bash
curl -s -X POST http://$IP/api/v1/run -H 'Content-Type: application/json' -d '{"action":"stop","data":"save"}'
curl -s http://$IP/api/v1/runs                       # current:false now
curl -s http://$IP/api/v1/runs/1 | head -3           # header + data rows
```
Expected: run 1 still listed (`current:false`); the CSV starts with the header row and has data lines.

- [ ] **Step 4: Unnamed run falls back to `Run N`; discard deletes the file**

```bash
curl -s -X POST http://$IP/api/v1/run -H 'Content-Type: application/json' -d '{"action":"start","targetC":36,"rpm":8,"durationMin":0}'
curl -s http://$IP/api/v1/runs                       # new run labelled "Run 2"
curl -s -X POST http://$IP/api/v1/run -H 'Content-Type: application/json' -d '{"action":"stop","data":"discard"}'
curl -s http://$IP/api/v1/runs                       # run 2 gone
curl -s -X POST http://$IP/api/v1/runs/1/delete      # delete run 1
curl -s http://$IP/api/v1/runs                       # empty list
```
Expected: run 2 shows `"label":"Run 2"`; after discard it's absent; after delete, the list is empty (`{"runs":[]}`).

- [ ] **Step 5: Record results**

No code change. Note pass/fail for each check in the PR/branch notes. If any check fails, debug with the serial `[CMD]` logs before proceeding to Plan B.

---

## Self-Review

**Spec coverage** (against `2026-06-24-run-session-naming-design.md` + the firmware half of `2026-06-24-web-ui-run-control-history-design.md`):
- Name in `POST /api/v1/run` start, sanitized server-side → Task 3 Step 4 + `copySanitizedName` + `RunFiles::sanitizeName` (Task 1).
- Per-run `.name` sidecar (chosen storage approach) → Task 2 `startRun`/`endRun`/`deleteRun`.
- `label = name || "Run <id>"` → `RunFiles::label` (Task 1) + `SdLogger::listRuns` (Task 2).
- `run.name` (+ `run.id`) in status → Task 4 Step 1.
- `GET /api/v1/runs`, `GET /api/v1/runs/<id>`, `POST /api/v1/runs/<id>/delete` → Task 3 Step 5.
- Per-run CSV files, stop save/discard, duration auto-save → Task 2 + Task 3 Step 7 + Task 4 Step 3.
- `/api/v1/log` latest-run alias (back-compat) → Task 3 Step 6.

**Out of scope here (deferred to Plan B / Plan C, intentionally):** web pause/resume + `run.pause` (Plan B); the `SESSION NAME` UI field + mock-server name (Plan C). Until Plan C lands, the firmware accepts `name` but the browser doesn't send one yet — runs simply show `Run N`, which is correct behavior.

**Placeholder scan:** none — every step has exact code/commands and expected output.

**Type consistency:** `startRun(const char*)`, `endRun(bool)`, `currentRunId()→int`, `currentRunName()→const char*`, `listRuns()→std::vector<RunInfo>`, `RunInfo{int id; String label; uint32_t bytes;}`, `runCsvPath(int)→String`, `latestRunId()→int`, `deleteRun(int)→bool`, `cacheRunsJson(const String&)` — used identically in Tasks 2/3/4. `RunFiles` signatures match Task 1 ↔ callers. `Pending.runName[33]` matches `copySanitizedName`'s `dstSize-1` and `currentName_[33]`.

**Open considerations:** `run.id`/`run.name` use the existing `if/else … = nullptr` pattern (Task 4 Step 1), matching `remainingSec` — no new ArduinoJson idiom. `req->pathArg(0)` requires `-DASYNCWEBSERVER_REGEX=1` (Task 3 Step 1) — if the build can't resolve regex routes, confirm the flag took effect (`pio run -t clean` then rebuild). `current_.flush()` on every row (Task 2 Step 3) trades a little write overhead for crash-safety mid-run; acceptable at the 10 s log cadence.
