# Feature-Toggle Scaffold Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add compile-time toggles for SD logging, OLED UI, and PID autotune that, when off, skip init/runtime, return HTTP 503 `feature_disabled` on control endpoints, log their state at boot, and hide their web-UI sections.

**Architecture:** Three `constexpr bool` flags in `AppConfig::Features` (default `true`). `AppRuntime` gates init in `begin()` and runtime in `tick()`; `WebInterface` reads the flags directly and 503s gated endpoints; `buildStatusJson()` advertises a top-level `features` map the SPA reads via a new pure helper to hide sections. All disabled branches are dead-code-eliminated, so every flag combination compiles.

**Tech Stack:** C++ (ESP32-S3 / Arduino-ESP32, ArduinoJson), vanilla ES-module SPA, Node.js `node:assert` host tests, Python aiohttp mock server, PlatformIO.

## Global Constraints

- PlatformIO full path only: `~/.platformio/penv/bin/pio` — build env `esp32-s3-devkitc-1`.
- YAGNI, one-liner solutions, incremental changes — no broad rewrites.
- Toggle defaults stay `true` (shipped behavior unchanged).
- Error envelope is the project standard: `{"ok":false,"error":{"code":"feature_disabled","message":"..."}}` (the `code` carries `feature_disabled`).
- `POST /api/v1/run` is NOT gated — running the reactor is core control; SD only records it.
- Build must pass with every flag `true` AND with each flag `false`.
- No changes outside the `Mini_Reactor` project.
- Branch: `feature/feature-toggles` (already created).

---

### Task 1: SPA feature helper (pure, TDD)

**Files:**
- Create: `data/core/features.js`
- Test: `test/ui/features.test.mjs`

**Interfaces:**
- Consumes: nothing.
- Produces: `featureEnabled(status, key) -> boolean` — returns `true` when `status`, `status.features`, or `status.features[key]` is missing/null (back-compat); otherwise the boolean value.

- [ ] **Step 1: Write the failing test**

Create `test/ui/features.test.mjs`:

```js
import assert from "node:assert/strict";
import { featureEnabled } from "../../data/core/features.js";

assert.equal(featureEnabled({}, "sdLogging"), true);                 // no features map -> enabled
assert.equal(featureEnabled(null, "autotune"), true);               // no status -> enabled
assert.equal(featureEnabled({ features: {} }, "sdLogging"), true);  // key absent -> enabled
assert.equal(featureEnabled({ features: { sdLogging: false } }, "sdLogging"), false);
assert.equal(featureEnabled({ features: { sdLogging: true } }, "sdLogging"), true);
assert.equal(featureEnabled({ features: { oledUi: false } }, "oledUi"), false);

console.log("features.test OK");
```

- [ ] **Step 2: Run test to verify it fails**

Run: `node test/ui/features.test.mjs`
Expected: FAIL — `Cannot find module '.../data/core/features.js'`.

- [ ] **Step 3: Write minimal implementation**

Create `data/core/features.js`:

```js
// Feature availability from /api/v1/status. A missing features map or key means
// enabled — back-compat with firmware built before the toggle scaffold. No DOM.
export function featureEnabled(status, key) {
  const f = status && status.features;
  if (!f || f[key] == null) return true;
  return !!f[key];
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `node test/ui/features.test.mjs`
Expected: PASS — prints `features.test OK`.

- [ ] **Step 5: Commit**

```bash
git add data/core/features.js test/ui/features.test.mjs
git commit -m "feat(ui): featureEnabled helper for toggle-aware SPA"
```

---

### Task 2: `AppConfig::Features` + boot logs

**Files:**
- Modify: `include/app_config.hpp` (add `Features` namespace before the closing `}  // namespace AppConfig`)
- Modify: `src/system/AppRuntime.cpp` (`begin()` — add `[FEAT]` log block)

**Interfaces:**
- Consumes: nothing.
- Produces: `AppConfig::Features::kEnableSdLogging`, `AppConfig::Features::kEnableOledUi`, `AppConfig::Features::kEnableAutotune` — all `constexpr bool`, default `true`.

- [ ] **Step 1: Add the Features namespace**

In `include/app_config.hpp`, immediately before the final line `}  // namespace AppConfig`, insert:

```cpp
// ── Feature toggles (compile-time) ───────────────────────────────────────────
// false => skip init + runtime, control endpoints return 503 feature_disabled,
// and the SPA hides the section. Defaults true (shipped behavior unchanged).
namespace Features {
static constexpr bool kEnableSdLogging = true;
static constexpr bool kEnableOledUi    = true;  // OLED blit only; encoder/buttons stay live
static constexpr bool kEnableAutotune  = true;
}  // namespace Features

```

- [ ] **Step 2: Add the boot-log block**

In `src/system/AppRuntime.cpp`, in `begin()`, find:

```cpp
  Serial.println(F("\n=== Bioreactor Module — fermentation firmware ==="));
```

and insert directly after it:

```cpp
  Serial.printf("[FEAT] SD logging: %s\n", AppConfig::Features::kEnableSdLogging ? "enabled" : "disabled");
  Serial.printf("[FEAT] OLED UI:    %s\n", AppConfig::Features::kEnableOledUi ? "enabled" : "disabled");
  Serial.printf("[FEAT] autotune:   %s\n", AppConfig::Features::kEnableAutotune ? "enabled" : "disabled");
```

- [ ] **Step 3: Build to verify it compiles (all flags true)**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: `SUCCESS`.

- [ ] **Step 4: Commit**

```bash
git add include/app_config.hpp src/system/AppRuntime.cpp
git commit -m "feat(fw): AppConfig::Features toggles + boot-state logs"
```

---

### Task 3: Advertise `features` in status JSON (firmware + mock)

**Files:**
- Modify: `src/system/AppRuntime.cpp` (`buildStatusJson()`)
- Modify: `tools/mock_server.py` (`status()` + module constant)

**Interfaces:**
- Consumes: `AppConfig::Features::kEnable*` (Task 2).
- Produces: `/api/v1/status` gains top-level `"features": {"sdLogging":bool,"oledUi":bool,"autotune":bool}`. The mock server emits the same shape from `MOCK_FEATURES`.

- [ ] **Step 1: Add the features map to buildStatusJson**

In `src/system/AppRuntime.cpp`, in `buildStatusJson()`, find:

```cpp
  doc["apiVersion"] = "1.0";
  doc["uptimeSec"] = millis() / 1000UL;
```

and insert directly after it:

```cpp
  JsonObject feats = doc["features"].to<JsonObject>();
  feats["sdLogging"] = AppConfig::Features::kEnableSdLogging;
  feats["oledUi"] = AppConfig::Features::kEnableOledUi;
  feats["autotune"] = AppConfig::Features::kEnableAutotune;
```

- [ ] **Step 2: Build to verify it compiles**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: `SUCCESS`.

- [ ] **Step 3: Mirror it in the mock server**

In `tools/mock_server.py`, directly above `def status():`, add:

```python
# Compile-time feature toggles mirrored from AppConfig::Features. Flip any to
# False to preview the SPA hiding that feature's controls (no hardware needed).
MOCK_FEATURES = {"sdLogging": True, "oledUi": True, "autotune": True}
```

Then in the `return {` dict inside `status()`, find:

```python
        "apiVersion": "1.0", "uptimeSec": int(now),
```

and change it to:

```python
        "apiVersion": "1.0", "uptimeSec": int(now),
        "features": MOCK_FEATURES,
```

- [ ] **Step 4: Verify the mock serves the features map**

Run (starts mock, curls status, greps, stops it):

```bash
python3 tools/mock_server.py & MOCK_PID=$!; sleep 1; \
curl -s http://localhost:8000/api/v1/status | grep -o '"features":{[^}]*}'; \
kill $MOCK_PID
```

Expected: prints `"features":{"sdLogging": true, "oledUi": true, "autotune": true}` (key order/spacing may vary).

- [ ] **Step 5: Commit**

```bash
git add src/system/AppRuntime.cpp tools/mock_server.py
git commit -m "feat(api): advertise features map in /status (fw + mock)"
```

---

### Task 4: Gate SD-logging + OLED init/runtime in AppRuntime

**Files:**
- Modify: `src/system/AppRuntime.cpp` (`begin()` and `tick()`)

**Interfaces:**
- Consumes: `AppConfig::Features::kEnableSdLogging`, `AppConfig::Features::kEnableOledUi` (Task 2).
- Produces: no new symbols. When the flags are false, `g_sd.begin()/checkAndReport()` and `g_display.begin()/render()` plus the SD runtime blocks are skipped; `g_input.poll()`/`g_ui.handle()` keep running.

- [ ] **Step 1: Gate init in begin()**

In `src/system/AppRuntime.cpp`, in `begin()`, find:

```cpp
  g_input.begin();
  g_display.begin();
  Serial.printf("[UI] OLED %s\n", g_display.present() ? "detected" : "absent (headless)");
  requestPd();

  // Storage.
  g_sd.begin();
  g_sd.checkAndReport(Serial);
```

and replace it with:

```cpp
  g_input.begin();
  if (AppConfig::Features::kEnableOledUi) {
    g_display.begin();
    Serial.printf("[UI] OLED %s\n", g_display.present() ? "detected" : "absent (headless)");
  }
  requestPd();

  // Storage.
  if (AppConfig::Features::kEnableSdLogging) {
    g_sd.begin();
    g_sd.checkAndReport(Serial);
  }
```

- [ ] **Step 2: Gate the OLED blit in tick()**

In `tick()`, find:

```cpp
    static uint32_t lastDrawMs = 0;
    if (millis() - lastDrawMs >= AppConfig::Ui::kRedrawIntervalMs) {
      lastDrawMs = millis();
      g_display.render(g_ui, snap);
    }
```

and replace it with:

```cpp
    static uint32_t lastDrawMs = 0;
    if (AppConfig::Features::kEnableOledUi && millis() - lastDrawMs >= AppConfig::Ui::kRedrawIntervalMs) {
      lastDrawMs = millis();
      g_display.render(g_ui, snap);
    }
```

- [ ] **Step 3: Gate the SD auto-stop finalize in tick()**

In `tick()`, find:

```cpp
  static bool prevRunning = false;
  const bool nowRunning = g_reactor.running();
  if (prevRunning && !nowRunning && g_sd.currentRunId() != 0) {
    g_sd.endRun(true);  // auto-stop saves
  }
  prevRunning = nowRunning;
```

and replace it with:

```cpp
  static bool prevRunning = false;
  const bool nowRunning = g_reactor.running();
  if (AppConfig::Features::kEnableSdLogging && prevRunning && !nowRunning && g_sd.currentRunId() != 0) {
    g_sd.endRun(true);  // auto-stop saves
  }
  prevRunning = nowRunning;
```

- [ ] **Step 4: Gate the SD runs-cache block in tick()**

In `tick()`, find:

```cpp
    static uint32_t lastRunsMs = 0;
    if (now - lastRunsMs >= 1000) {       // refresh the runs list ~1 Hz
```

and change the condition line to:

```cpp
    static uint32_t lastRunsMs = 0;
    if (AppConfig::Features::kEnableSdLogging && now - lastRunsMs >= 1000) {       // refresh the runs list ~1 Hz
```

- [ ] **Step 5: Gate the periodic SD log-append in tick()**

In `tick()`, find:

```cpp
  static uint32_t lastLogMs = 0;
  if (g_sd.mounted() && g_sd.currentRunId() != 0 && now - lastLogMs >= g_sd.logIntervalMs()) {
```

and change the condition line to:

```cpp
  static uint32_t lastLogMs = 0;
  if (AppConfig::Features::kEnableSdLogging && g_sd.mounted() && g_sd.currentRunId() != 0 && now - lastLogMs >= g_sd.logIntervalMs()) {
```

- [ ] **Step 6: Build with all flags true**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: `SUCCESS`.

- [ ] **Step 7: Build with SD + OLED disabled (verify the false path compiles)**

Temporarily set both flags false in `include/app_config.hpp`:
`kEnableSdLogging = false;` and `kEnableOledUi = false;`

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: `SUCCESS` (no unused-variable/dead-code errors).

Then revert both flags back to `true` in `include/app_config.hpp`.

- [ ] **Step 8: Commit**

```bash
git add include/app_config.hpp src/system/AppRuntime.cpp
git commit -m "feat(fw): gate SD-logging + OLED init/runtime on toggles"
```

---

### Task 5: 503 gated endpoints in WebInterface

**Files:**
- Modify: `src/net/WebInterface.cpp` (add include, `featureGate` helper, endpoint guards, `startRun` guard)

**Interfaces:**
- Consumes: `AppConfig::Features::kEnableSdLogging`, `AppConfig::Features::kEnableAutotune` (Task 2).
- Produces: `featureGate(req, enabled) -> bool` (file-local static) — sends 503 `feature_disabled` and returns `true` when `!enabled`, else returns `false`.

- [ ] **Step 1: Include app_config.hpp**

In `src/net/WebInterface.cpp`, find:

```cpp
#include "control/Reactor.hpp"
```

and insert directly before it:

```cpp
#include "app_config.hpp"
```

- [ ] **Step 2: Add the featureGate helper**

In `src/net/WebInterface.cpp`, find the end of the existing `sendError` function:

```cpp
  AsyncWebServerResponse* resp = req->beginResponse(status, "application/json", body);
  resp->addHeader("Access-Control-Allow-Origin", "*");
  req->send(resp);
}
```

and insert directly after it:

```cpp

// Returns true (and sends 503 feature_disabled) when the feature is off; the
// caller then returns. Keeps each gated handler to a single guard line.
static bool featureGate(AsyncWebServerRequest* req, bool enabled) {
  if (enabled) return false;
  sendError(req, 503, "feature_disabled", "feature disabled at build time");
  return true;
}
```

- [ ] **Step 3: Guard the autotune endpoint**

In `registerRoutes()`, in the `/api/v1/pid/autotune` handler, find:

```cpp
      "/api/v1/pid/autotune", [this](AsyncWebServerRequest* req, JsonVariant& json) {
        JsonObject o = json.as<JsonObject>();
        const String action = o["action"] | "";
```

and insert the guard as the first statement in the lambda body:

```cpp
      "/api/v1/pid/autotune", [this](AsyncWebServerRequest* req, JsonVariant& json) {
        if (featureGate(req, AppConfig::Features::kEnableAutotune)) return;
        JsonObject o = json.as<JsonObject>();
        const String action = o["action"] | "";
```

- [ ] **Step 4: Guard the SD download endpoint (`GET /api/v1/log`)**

Find:

```cpp
  server_->on("/api/v1/log", HTTP_GET, [this](AsyncWebServerRequest* req) {
    if (!sd_.mounted()) {
```

and change to:

```cpp
  server_->on("/api/v1/log", HTTP_GET, [this](AsyncWebServerRequest* req) {
    if (featureGate(req, AppConfig::Features::kEnableSdLogging)) return;
    if (!sd_.mounted()) {
```

- [ ] **Step 5: Guard `POST /api/v1/log/interval`**

Find:

```cpp
      "/api/v1/log/interval", [this](AsyncWebServerRequest* req, JsonVariant& json) {
        JsonObject o = json.as<JsonObject>();
        if (o["seconds"].isNull()) {
```

and change to:

```cpp
      "/api/v1/log/interval", [this](AsyncWebServerRequest* req, JsonVariant& json) {
        if (featureGate(req, AppConfig::Features::kEnableSdLogging)) return;
        JsonObject o = json.as<JsonObject>();
        if (o["seconds"].isNull()) {
```

- [ ] **Step 6: Guard `POST /api/v1/sd/erase`**

Find:

```cpp
  server_->on("/api/v1/sd/erase", HTTP_POST, [this](AsyncWebServerRequest* req) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    pending_.sdErase = true;
```

and change to:

```cpp
  server_->on("/api/v1/sd/erase", HTTP_POST, [this](AsyncWebServerRequest* req) {
    if (featureGate(req, AppConfig::Features::kEnableSdLogging)) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    pending_.sdErase = true;
```

- [ ] **Step 7: Guard `GET /api/v1/runs/{id}` (download)**

Find:

```cpp
  server_->on("^\\/api\\/v1\\/runs\\/([0-9]+)$", HTTP_GET,
              [this](AsyncWebServerRequest* req) {
    const int id = req->pathArg(0).toInt();
```

and change to:

```cpp
  server_->on("^\\/api\\/v1\\/runs\\/([0-9]+)$", HTTP_GET,
              [this](AsyncWebServerRequest* req) {
    if (featureGate(req, AppConfig::Features::kEnableSdLogging)) return;
    const int id = req->pathArg(0).toInt();
```

- [ ] **Step 8: Guard `POST /api/v1/runs/{id}/delete`**

Find:

```cpp
  server_->on("^\\/api\\/v1\\/runs\\/([0-9]+)\\/delete$", HTTP_POST,
              [this](AsyncWebServerRequest* req) {
    const int id = req->pathArg(0).toInt();
```

and change to:

```cpp
  server_->on("^\\/api\\/v1\\/runs\\/([0-9]+)\\/delete$", HTTP_POST,
              [this](AsyncWebServerRequest* req) {
    if (featureGate(req, AppConfig::Features::kEnableSdLogging)) return;
    const int id = req->pathArg(0).toInt();
```

- [ ] **Step 9: Guard `GET /api/v1/runs` (list)**

Find:

```cpp
  server_->on("/api/v1/runs", HTTP_GET, [this](AsyncWebServerRequest* req) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const String body = runsJson_;
```

and change to:

```cpp
  server_->on("/api/v1/runs", HTTP_GET, [this](AsyncWebServerRequest* req) {
    if (featureGate(req, AppConfig::Features::kEnableSdLogging)) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const String body = runsJson_;
```

- [ ] **Step 10: Guard the core-path startRun in applyPending**

In `applyPending()`, find:

```cpp
    reactor_.start(p.runTargetC, p.runRpm, p.runDurMin);
    if (reactor_.running()) sd_.startRun(p.runName);  // open the per-run file
```

and change the second line to:

```cpp
    reactor_.start(p.runTargetC, p.runRpm, p.runDurMin);
    if (AppConfig::Features::kEnableSdLogging && reactor_.running()) sd_.startRun(p.runName);  // open the per-run file
```

- [ ] **Step 11: Build with all flags true**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: `SUCCESS`.

- [ ] **Step 12: Build with autotune disabled (verify the false path compiles)**

Temporarily set `kEnableAutotune = false;` in `include/app_config.hpp`.

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: `SUCCESS`.

Then revert `kEnableAutotune` back to `true`.

- [ ] **Step 13: Commit**

```bash
git add src/net/WebInterface.cpp
git commit -m "feat(api): 503 feature_disabled on gated SD + autotune endpoints"
```

---

### Task 6: Hide disabled sections in the settings SPA

**Files:**
- Modify: `data/screens/settings.js` (import helper, extract autotune button refs, toggle `.hidden` in the subscribe handler)

**Interfaces:**
- Consumes: `featureEnabled` from `data/core/features.js` (Task 1); `status.features` (Task 3).
- Produces: no new exports. The **DATA LOG** card and the two autotune buttons get `.hidden` set reactively from the latest status.

- [ ] **Step 1: Import the helper**

In `data/screens/settings.js`, find:

```js
import { pollScan } from "../core/wifiscan.js";
```

and insert directly after it:

```js
import { featureEnabled } from "../core/features.js";
```

- [ ] **Step 2: Extract the autotune buttons to named refs**

Find:

```js
    el("div", { class: "btns" },
      el("button", { class: "go", onclick: async (e) => { const b = e.currentTarget; const r = await api.pidGains(+kp.value, +ki.value, +kd.value); flashApplied(b, r.ok); } }, "Apply gains"),
      el("button", { class: "ghost", onclick: async (e) => { const b = e.currentTarget; const r = await api.pidMode(modeSel.value); flashApplied(b, r.ok); } }, "Set mode"),
      el("button", { class: "ghost", onclick: () => api.autotune("start") }, "Autotune"),
      el("button", { class: "ghost", onclick: () => api.autotune("cancel") }, "Cancel")),
    el("p", { class: "muted" }, "Autotune: ", atInfo));
```

and replace it with (note the new `atStartBtn`/`atCancelBtn` consts declared just before `const pid =`):

```js
    el("div", { class: "btns" },
      el("button", { class: "go", onclick: async (e) => { const b = e.currentTarget; const r = await api.pidGains(+kp.value, +ki.value, +kd.value); flashApplied(b, r.ok); } }, "Apply gains"),
      el("button", { class: "ghost", onclick: async (e) => { const b = e.currentTarget; const r = await api.pidMode(modeSel.value); flashApplied(b, r.ok); } }, "Set mode"),
      atStartBtn, atCancelBtn),
    el("p", { class: "muted" }, "Autotune: ", atInfo));
```

Then find the line that begins the PID section:

```js
  const pid = sec("PID TUNING",
```

and insert directly before it:

```js
  const atStartBtn = el("button", { class: "ghost", onclick: () => api.autotune("start") }, "Autotune");
  const atCancelBtn = el("button", { class: "ghost", onclick: () => api.autotune("cancel") }, "Cancel");
```

- [ ] **Step 3: Toggle visibility in the subscribe handler**

Find the start of the subscribe callback:

```js
  const unsub = store.subscribe((d) => {
    const w = d.wifi || {}, st = d.storage || {}, p = (d.thermal || {}).pid || {}, at = p.autotune || {}, disc = d.disc || {}, s = d.system || {}, drv = disc.driver || {};
```

and insert directly after that destructuring line:

```js
    sd.hidden = !featureEnabled(d, "sdLogging");
    atStartBtn.hidden = atCancelBtn.hidden = !featureEnabled(d, "autotune");
```

- [ ] **Step 4: Verify the whole JS test suite still passes**

Run: `for f in test/ui/*.mjs; do node "$f" || exit 1; done`
Expected: each file prints its `... OK` line; exit status 0.

- [ ] **Step 5: Visual check against the mock (manual, optional but recommended)**

Set `MOCK_FEATURES = {"sdLogging": False, "oledUi": True, "autotune": False}` in `tools/mock_server.py`, run `python3 tools/mock_server.py`, open `http://localhost:8000/#settings`, and confirm the **DATA LOG** card and **Autotune/Cancel** buttons are gone; flip them back to `True` and confirm they reappear. Revert `MOCK_FEATURES` to all `True` before committing.

- [ ] **Step 6: Commit**

```bash
git add data/screens/settings.js
git commit -m "feat(ui): hide SD + autotune controls when feature disabled"
```

---

### Task 7: Docs, Roadmap, and full toggle-matrix validation

**Files:**
- Modify: `API.md` (error-code table + new Feature-toggles section)
- Modify: `README.md` (Configuration section note)
- Modify: `Roadmap.md` (tick the scaffold box)

**Interfaces:**
- Consumes: everything above.
- Produces: docs matching the shipped behavior; the final all-false / all-true build evidence.

- [ ] **Step 1: Document the error code in API.md**

In `API.md`, find the error-code table row:

```markdown
| `wifi_ssid_required` | connect without ssid |
```

and insert directly after it:

```markdown
| `feature_disabled` | endpoint's feature toggled off at build time (HTTP 503) |
```

- [ ] **Step 2: Add a Feature-toggles section to API.md**

In `API.md`, find the Storage section header:

```markdown
## Storage / data log
```

and insert directly before it:

```markdown
## Feature toggles

Three compile-time flags in [`include/app_config.hpp`](include/app_config.hpp)
(`AppConfig::Features::kEnableSdLogging`, `kEnableOledUi`, `kEnableAutotune`,
all default `true`) gate optional features. `GET /api/v1/status` advertises their
state so the UI can react:

```jsonc
"features": { "sdLogging": true, "oledUi": true, "autotune": true }
```

When a flag is `false`, that feature's control endpoints return **HTTP 503**
with `code: "feature_disabled"`:

| Flag | Endpoints that return 503 when off |
|------|-------------------------------------|
| `kEnableSdLogging` | `GET /log`, `POST /log/interval`, `POST /sd/erase`, `GET /runs`, `GET /runs/{id}`, `POST /runs/{id}/delete` |
| `kEnableAutotune` | `POST /pid/autotune` |
| `kEnableOledUi` | none (front-panel display only; no HTTP surface) |

`POST /api/v1/run` is never gated — running the reactor is core control; SD
logging only records the run.

```

- [ ] **Step 3: Note toggles in README.md**

In `README.md`, find the end of the Configuration section:

```markdown
`src/system/AppRuntime.cpp` are built from these — change hardware wiring or defaults in one
place.

## License
```

and replace it with:

```markdown
`src/system/AppRuntime.cpp` are built from these — change hardware wiring or defaults in one
place.

Optional features are gated by compile-time toggles in `AppConfig::Features`
(`kEnableSdLogging`, `kEnableOledUi`, `kEnableAutotune`, all default `true`).
Setting one to `false` skips that module's init/runtime, makes its control
endpoints return `503 feature_disabled`, logs its state at boot (`[FEAT] …`),
and hides its web-UI section. See [`API.md`](API.md#feature-toggles).

## License
```

- [ ] **Step 4: Tick the Roadmap box**

In `Roadmap.md`, find:

```markdown
- [ ] **Feature-toggle scaffold** — add `AppConfig::Features::kEnable*` flags; gate init in `AppRuntime::begin()`; `503 + feature_disabled` on disabled control APIs; explicit enabled/disabled startup logs. Targets: SD logging, OLED UI, autotune, (later) OTA/MQTT/ESP-NOW.
```

and change the leading `- [ ]` to `- [x]`:

```markdown
- [x] **Feature-toggle scaffold** — add `AppConfig::Features::kEnable*` flags; gate init in `AppRuntime::begin()`; `503 + feature_disabled` on disabled control APIs; explicit enabled/disabled startup logs. Targets: SD logging, OLED UI, autotune, (later) OTA/MQTT/ESP-NOW.
```

- [ ] **Step 5: Final validation — build with ALL toggles false**

Temporarily set all three flags false in `include/app_config.hpp`
(`kEnableSdLogging = false; kEnableOledUi = false; kEnableAutotune = false;`).

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: `SUCCESS`.

Then revert all three back to `true`.

- [ ] **Step 6: Final validation — build with ALL toggles true (shipping default)**

Run: `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1`
Expected: `SUCCESS`.

- [ ] **Step 7: Final validation — JS test suite**

Run: `for f in test/ui/*.mjs; do node "$f" || exit 1; done`
Expected: all files print `... OK`; exit status 0.

- [ ] **Step 8: Commit**

```bash
git add API.md README.md Roadmap.md include/app_config.hpp
git commit -m "docs: document feature toggles (API/README) + tick Roadmap"
```
```
