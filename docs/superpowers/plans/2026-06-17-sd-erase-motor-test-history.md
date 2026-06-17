# SD Erase · Motor Test · History Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add three features to the reactor control app — erase the SD card (delete all files), a firmware-timed motor jog "Test", and a History tab that reads the SD CSV log into a chart + recent-rows table.

**Architecture:** Firmware gains `SdLogger::eraseAll()` + `Reactor::startMotorTest()` and two POST endpoints. The UI gains a pure host-tested CSV parser (`core/csv.js`), a History screen (reuses `core/chart.js`), and Settings additions (erase modal + Test button). The mock simulates all three so it's verifiable without hardware.

**Tech Stack:** C++17 / Arduino-ESP32 / SD (SPI), vanilla ES modules, Node 22 (unit tests), headless Chrome (screenshots), Python/aiohttp (mock).

**Spec:** `docs/superpowers/specs/2026-06-17-sd-erase-motor-test-history-design.md`.

---

## Baseline assumptions

- Branch `feature/ui-rebuild` (current). Design committed (`3592755`).
- Tools: mock `/tmp/pio-venv/bin/python tools/mock_server.py` (serves `data/` + API at `http://localhost:8000`); Node `node`; Chrome `"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"`; PlatformIO `/tmp/pio-venv/bin/pio`.
- **SHARED-WORKTREE git rule:** only `git add`/`git commit` + read-only git; never checkout/switch/reset/stash.
- **Screenshot helper:**
  ```bash
  /tmp/pio-venv/bin/python tools/mock_server.py >/tmp/mock.log 2>&1 &  MOCK=$!
  node -e "setTimeout(()=>{},1500)"
  "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless=new --disable-gpu \
    --hide-scrollbars --virtual-time-budget=4000 --window-size=1120,1100 --screenshot=/tmp/ui.png "http://localhost:8000/#history"
  kill $MOCK 2>/dev/null
  ```
  Then Read `/tmp/ui.png`.

## File structure

| File | Responsibility | Action |
|---|---|---|
| `data/core/csv.js` | Pure CSV parser. | Create |
| `test/ui/csv.test.mjs` | parseCsv tests. | Create |
| `include/storage/SdLogger.hpp` / `.cpp` | `eraseAll()` + recursive delete. | Modify |
| `include/app_config.hpp` | `kMotorTestRpm`, `kMotorTestMs`. | Modify |
| `include/control/Reactor.hpp` / `.cpp` | `startMotorTest()` + test-timer in `update()`. | Modify |
| `include/net/WebInterface.hpp` / `src/net/WebInterface.cpp` | Pending `sdErase`/`motorTest`; `POST /sd/erase`, `/disc/test`. | Modify |
| `tools/mock_server.py` | `/sd/erase`, `/disc/test`, synthetic `/log` CSV. | Modify |
| `data/index.html` | History tab. | Modify |
| `data/app.js` | Register `history` screen. | Modify |
| `data/app.css` | Modal + table styles. | Modify |
| `data/screens/history.js` | Log reader (chart + table). | Create |
| `data/screens/settings.js` | Erase modal + Test button. | Modify |

---

### Task 1: CSV parser (pure, TDD)

**Files:** create `data/core/csv.js`, `test/ui/csv.test.mjs`.

- [ ] **Step 1: Write the failing test** — `test/ui/csv.test.mjs`:

```js
import assert from "node:assert/strict";
import { parseCsv } from "../../data/core/csv.js";

let r = parseCsv("a,b,c\n1,2,3\n4,5,6\n");
assert.deepEqual(r.header, ["a", "b", "c"]);
assert.equal(r.rows.length, 2);
assert.deepEqual(r.rows[1], ["4", "5", "6"]);

// blank and trailing lines are ignored
r = parseCsv("h1,h2\n\n10,20\n\n");
assert.equal(r.rows.length, 1);
assert.deepEqual(r.rows[0], ["10", "20"]);

// empty input -> empty
assert.deepEqual(parseCsv("").rows, []);
assert.deepEqual(parseCsv("").header, []);
console.log("csv.test OK");
```

- [ ] **Step 2: Run to verify it fails** — `node test/ui/csv.test.mjs` → FAIL (`Cannot find module .../csv.js`).

- [ ] **Step 3: Implement** — `data/core/csv.js`:

```js
// Minimal CSV parser for the reactor log. Numeric coercion happens at the call site.
export function parseCsv(text) {
  const lines = String(text).split(/\r?\n/).filter((l) => l.trim().length);
  if (!lines.length) return { header: [], rows: [] };
  return { header: lines[0].split(","), rows: lines.slice(1).map((l) => l.split(",")) };
}
```

- [ ] **Step 4: Run to verify it passes** — `node test/ui/csv.test.mjs` → `csv.test OK`.

- [ ] **Step 5: Commit**

```bash
git add data/core/csv.js test/ui/csv.test.mjs
git commit -m "feat(ui): host-tested CSV parser"
```

---

### Task 2: Firmware — SD eraseAll + endpoint

**Files:** modify `include/storage/SdLogger.hpp`, `src/storage/SdLogger.cpp`, `include/net/WebInterface.hpp`, `src/net/WebInterface.cpp`.

- [ ] **Step 1: Declare in `include/storage/SdLogger.hpp`**

In `public:`, after the `clearLog()` declaration:
```cpp
  /* eraseAll() — Delete every file/dir on the card, then recreate the empty log
   * with its header. Destructive. Returns success. */
  bool eraseAll();
```
In `private:`, after `bool mounted_ = false;`:
```cpp
  void removeRecursive(const char* path);
```

- [ ] **Step 2: Implement in `src/storage/SdLogger.cpp`** — add after `clearLog()`:

```cpp
void SdLogger::removeRecursive(const char* path) {
  // Re-open the directory each iteration and delete its first child, so we never
  // delete entries mid-iteration (unreliable on FAT). Recurse into subdirectories.
  while (true) {
    File dir = SD.open(path);
    if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return; }
    File entry = dir.openNextFile();
    if (!entry) { dir.close(); return; }  // empty
    String p = entry.path();
    const bool isDir = entry.isDirectory();
    entry.close();
    dir.close();
    if (isDir) { removeRecursive(p.c_str()); SD.rmdir(p.c_str()); }
    else { SD.remove(p.c_str()); }
  }
}

bool SdLogger::eraseAll() {
  if (!mounted_) return false;
  removeRecursive("/");
  return clearLog();  // recreate the empty log with its header
}
```

- [ ] **Step 3: WebInterface — pending flag + route + apply**

In `include/net/WebInterface.hpp`, in `struct Pending`, after `bool calReset = false;`:
```cpp
    bool sdErase = false;
    bool motorTest = false;
```

In `src/net/WebInterface.cpp` `registerRoutes()`, after the `/api/v1/calibration/reset` handler block:
```cpp
  // ── POST sd/erase ──
  server_->on("/api/v1/sd/erase", HTTP_POST, [this](AsyncWebServerRequest* req) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    pending_.sdErase = true;
    xSemaphoreGive(mutex_);
    sendOk(req);
  });
```

In `applyPending()`, after `if (p.logClear) sd_.clearLog();`:
```cpp
  if (p.sdErase) sd_.eraseAll();
```

- [ ] **Step 4: Compile** — `/tmp/pio-venv/bin/pio run -e esp32-s3-devkitc-1` → `SUCCESS`.

- [ ] **Step 5: Commit**

```bash
git add include/storage/SdLogger.hpp src/storage/SdLogger.cpp include/net/WebInterface.hpp src/net/WebInterface.cpp
git commit -m "feat(sd): erase-all endpoint (recursive delete + fresh log)"
```

---

### Task 3: Firmware — motor test + endpoint

**Files:** modify `include/app_config.hpp`, `include/control/Reactor.hpp`, `src/control/Reactor.cpp`, `src/net/WebInterface.cpp`.

- [ ] **Step 1: Config constants** — in `include/app_config.hpp`, in `namespace Motor`, after `static constexpr uint16_t kCurrentMilliamps = 600;     // RMS, generic NEMA-17`:

```cpp
static constexpr float    kMotorTestRpm = 8.0f;    // disc speed for the one-click test
static constexpr uint32_t kMotorTestMs  = 3000;    // test jog duration
```

- [ ] **Step 2: Reactor — declare** — in `include/control/Reactor.hpp`, in `public:` after `void update();`:
```cpp
  /* startMotorTest() — Brief disc jog to confirm the drive turns (no-op during a run). */
  void startMotorTest();
```
In `private:`, after `bool running_ = false;`:
```cpp
  uint32_t motorTestUntilMs_ = 0;  // 0 = no test active
```

- [ ] **Step 3: Reactor — implement** — in `src/control/Reactor.cpp`:

Add the method (e.g., after `stop()`):
```cpp
void Reactor::startMotorTest() {
  if (running_) return;  // don't interfere with an active run
  motor_.enable(true);
  motor_.setRpm(AppConfig::Motor::kMotorTestRpm);
  motorTestUntilMs_ = millis() + AppConfig::Motor::kMotorTestMs;
}
```

Replace `update()` so the test timer is serviced even while idle:
```cpp
void Reactor::update() {
  if (motorTestUntilMs_ != 0 && millis() >= motorTestUntilMs_) {
    motor_.setRpm(0);
    motor_.enable(false);
    motorTestUntilMs_ = 0;
  }
  if (!running_) return;
  if (durationMin_ > 0) {
    const uint32_t elapsedMs = millis() - startMs_;
    if (elapsedMs >= static_cast<uint32_t>(durationMin_) * 60000UL) {
      stop();
    }
  }
}
```

(`AppConfig` is already included by `Reactor.cpp`. If the compiler disagrees, add `#include "app_config.hpp"`.)

- [ ] **Step 4: WebInterface — route + apply** — in `src/net/WebInterface.cpp` `registerRoutes()`, after the `/api/v1/sd/erase` handler:
```cpp
  // ── POST disc/test (timed motor jog) ──
  server_->on("/api/v1/disc/test", HTTP_POST, [this](AsyncWebServerRequest* req) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    pending_.motorTest = true;
    xSemaphoreGive(mutex_);
    sendOk(req);
  });
```
In `applyPending()`, after `if (p.sdErase) sd_.eraseAll();`:
```cpp
  if (p.motorTest) reactor_.startMotorTest();
```

- [ ] **Step 5: Compile + native tests** — `/tmp/pio-venv/bin/pio run -e esp32-s3-devkitc-1` → `SUCCESS`; `/tmp/pio-venv/bin/pio test -e native` → all pass.

- [ ] **Step 6: Commit**

```bash
git add include/app_config.hpp include/control/Reactor.hpp src/control/Reactor.cpp src/net/WebInterface.cpp
git commit -m "feat(motor): firmware-timed disc test jog + endpoint"
```

---

### Task 4: Mock — /sd/erase, /disc/test, synthetic /log

**Files:** modify `tools/mock_server.py`.

- [ ] **Step 1: Test state** — in the `state` dict (near `calMethod`), add:
```python
    "motorTestUntil": 0.0,
```

- [ ] **Step 2: Show the test jog in `status()`** — find the line computing rpm for display:
```python
    rpm = state["rpm"] if state["running"] else 0.0
```
replace with:
```python
    testing = time.monotonic() < state["motorTestUntil"]
    rpm = 8.0 if testing else (state["rpm"] if state["running"] else 0.0)
```

- [ ] **Step 3: Synthetic log** — replace `api_log`:
```python
async def api_log(req):
    header = "t_ms,running,liquid_c,heater_c,setpoint_c,heater_pct,rpm,load,fault,safety"
    rows, sp, t = [header], 36.0, 20.0
    for i in range(40):
        t += (sp - t) * 0.12
        heater = t + 8.0
        duty = max(0.0, min(100.0, (sp - t) * 18.0))
        rows.append(f"{i*2000},1,{t:.2f},{heater:.1f},{sp:.1f},{duty:.0f},8.0,{420+i},0,0")
    return web.Response(text="\n".join(rows) + "\n", content_type="text/csv")
```

- [ ] **Step 4: New handlers** — add near `api_log_clear`:
```python
async def api_sd_erase(req):
    return web.json_response({"ok": True})


async def api_disc_test(req):
    state["motorTestUntil"] = time.monotonic() + 3.0
    return web.json_response({"ok": True})
```

Register in `main()` after the calibration routes:
```python
    app.router.add_post("/api/v1/sd/erase", api_sd_erase)
    app.router.add_post("/api/v1/disc/test", api_disc_test)
```

- [ ] **Step 5: Verify**

```bash
/tmp/pio-venv/bin/python -c "import ast; ast.parse(open('tools/mock_server.py').read()); print('syntax ok')"
/tmp/pio-venv/bin/python tools/mock_server.py >/tmp/mock.log 2>&1 &  MOCK=$!
node -e "setTimeout(()=>{},1500)"
echo "log rows:"; curl -s localhost:8000/api/v1/log | wc -l
echo "erase:"; curl -s -XPOST localhost:8000/api/v1/sd/erase -d '{}'; echo
echo "disc/test then rpm:"; curl -s -XPOST localhost:8000/api/v1/disc/test -d '{}' >/dev/null
curl -s localhost:8000/api/v1/status | node -e "let s='';process.stdin.on('data',d=>s+=d).on('end',()=>console.log('rpm',JSON.parse(s).disc.rpm))"
kill $MOCK 2>/dev/null
```
Expected: log shows ~41 lines; erase → `{"ok": true}`; after disc/test the status `disc.rpm` is `8`.

- [ ] **Step 6: Commit**

```bash
git add tools/mock_server.py
git commit -m "feat(mock): /sd/erase, /disc/test, synthetic /log CSV"
```

---

### Task 5: UI — History tab + screen

**Files:** modify `data/index.html`, `data/app.js`, `data/app.css`; create `data/screens/history.js`.

- [ ] **Step 1: Tab** — in `data/index.html`, replace the tabs nav:
```html
  <nav class="tabs" id="tabs">
    <a class="tab" href="#monitor" data-tab="monitor">Monitor</a>
    <a class="tab" href="#run" data-tab="run">Run</a>
    <a class="tab" href="#settings" data-tab="settings">Settings</a>
  </nav>
```
with:
```html
  <nav class="tabs" id="tabs">
    <a class="tab" href="#monitor" data-tab="monitor">Monitor</a>
    <a class="tab" href="#run" data-tab="run">Run</a>
    <a class="tab" href="#history" data-tab="history">History</a>
    <a class="tab" href="#settings" data-tab="settings">Settings</a>
  </nav>
```

- [ ] **Step 2: Table styles** — append to `data/app.css`:
```css
.htable{width:100%;border-collapse:collapse;font-size:12px;margin-top:8px}
.htable th{text-align:right;font-size:9px;letter-spacing:.1em;color:var(--faint);font-weight:700;padding:6px 8px;border-bottom:1px solid var(--border)}
.htable td{text-align:right;padding:5px 8px;border-bottom:1px solid var(--border-2);font-family:var(--mono)}
.htable td:first-child,.htable th:first-child{text-align:left}
```

- [ ] **Step 3: Implement `data/screens/history.js`** (the log is CSV, so it's fetched as text directly — not via `api.get()`, which JSON-parses)

```js
import { el, fixed, hhmmss, clear } from "../core/ui.js";
import { parseCsv } from "../core/csv.js";
import { render } from "../core/chart.js";

export function mount(root) {
  const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
  svg.setAttribute("width", "100%"); svg.setAttribute("height", "170"); svg.setAttribute("preserveAspectRatio", "none");
  const tableWrap = el("div", {});
  const status = el("span", { class: "muted" }, "—");
  const refresh = el("button", { class: "ghost", onclick: () => load() }, "Refresh");

  root.append(
    el("div", { class: "card" }, el("h3", {}, "HISTORY", refresh),
      el("div", { class: "chart" }, svg,
        el("div", { class: "legend" }, el("span", {}, el("i", {}), "Liquid °C"),
          el("span", {}, el("i", { class: "sp" }), "Setpoint °C"),
          el("span", { style: "margin-left:auto" }, status)))),
    el("div", { class: "card" }, el("h3", {}, "RECENT ROWS"), tableWrap));

  const idx = (header, name) => header.indexOf(name);
  const downsample = (arr, max = 200) => { const stride = Math.ceil(arr.length / max) || 1; return arr.filter((_, i) => i % stride === 0); };

  async function load() {
    refresh.disabled = true; refresh.textContent = "…"; status.textContent = "loading…";
    const res = await fetch("/api/v1/log").catch(() => null);
    refresh.disabled = false; refresh.textContent = "Refresh";
    const text = res && res.ok ? await res.text() : "";
    const { header, rows } = parseCsv(text);
    if (!rows.length) { status.textContent = "No log data"; while (svg.firstChild) svg.removeChild(svg.firstChild); clear(tableWrap); return; }
    const iT = idx(header, "t_ms"), iL = idx(header, "liquid_c"), iS = idx(header, "setpoint_c"),
          iH = idx(header, "heater_c"), iD = idx(header, "heater_pct"), iR = idx(header, "rpm"),
          iLoad = idx(header, "load"), iF = idx(header, "fault"), iSaf = idx(header, "safety");
    const liquid = downsample(rows.map((r) => +r[iL]).filter((v) => !Number.isNaN(v)));
    const setp = downsample(rows.map((r) => +r[iS]).filter((v) => !Number.isNaN(v)));
    render(svg, { t: liquid, s: setp });
    status.textContent = `${rows.length} rows`;

    clear(tableWrap);
    const head = el("tr", {}, ["TIME", "LIQUID", "HEATER", "SETPT", "DUTY%", "RPM", "LOAD", "FLAG"].map((h) => el("th", {}, h)));
    const body = rows.slice(-50).reverse().map((r) => el("tr", {},
      el("td", {}, hhmmss(Math.round((+r[iT] || 0) / 1000))),
      el("td", {}, fixed(+r[iL], 1)), el("td", {}, fixed(+r[iH], 1)), el("td", {}, fixed(+r[iS], 1)),
      el("td", {}, fixed(+r[iD], 0)), el("td", {}, fixed(+r[iR], 1)), el("td", {}, r[iLoad] || "—"),
      el("td", {}, (r[iF] === "1" ? "FAULT" : r[iSaf] === "1" ? "TRIP" : "ok"))));
    tableWrap.append(el("table", { class: "htable" }, el("thead", {}, head), el("tbody", {}, body)));
  }

  load();
  return null;
}
```

- [ ] **Step 4: Register the screen** — in `data/app.js`, add `import * as history from "./screens/history.js";` and update the `screens` map to `const screens = { monitor, run, history, settings };`.

- [ ] **Step 5: Verify** — screenshot helper at `http://localhost:8000/#history`; Read `/tmp/ui.png`.
Expected: a HISTORY card with a rising trend line (liquid vs setpoint) + "40 rows", and a RECENT ROWS table (newest first) with time/liquid/heater/setpoint/duty/rpm/load/flag.

- [ ] **Step 6: Commit**

```bash
git add data/index.html data/app.js data/app.css data/screens/history.js
git commit -m "feat(ui): History tab (log chart + recent-rows table)"
```

---

### Task 6: UI — Settings erase modal + motor Test button

**Files:** modify `data/screens/settings.js`, `data/app.css`.

- [ ] **Step 1: Modal styles** — append to `data/app.css`:
```css
.modal-bg{position:fixed;inset:0;background:rgba(20,40,55,.45);display:flex;align-items:center;justify-content:center;z-index:20}
.modal{background:var(--surface);border:1px solid var(--border);border-radius:14px;box-shadow:var(--shadow);padding:20px;max-width:420px;width:90%}
.modal h4{margin:0 0 8px;font-size:15px}
.modal p{color:var(--muted);font-size:13px;margin:0 0 12px}
```

- [ ] **Step 2: Settings — erase modal** — in `data/screens/settings.js`, replace the SD section block:
```js
  const sd = sec("DATA LOG", sdInfo,
    el("div", { class: "btns" },
      el("a", { class: "btn", href: "/api/v1/log", download: "reactor_log.csv" }, "⬇ Download CSV"),
      el("button", { class: "ghost", onclick: () => { if (confirm("Clear the SD log?")) api.logClear(); } }, "Clear log")));
```
with (adds the danger "Erase all files" button + a typed-confirm modal):
```js
  function eraseModal() {
    const input = el("input", { type: "text", placeholder: "type: erase" });
    const ok = el("button", { class: "stop", disabled: "", onclick: async () => { await api.post("/api/v1/sd/erase"); toast("Erasing…", "ok"); bg.remove(); } }, "Erase card");
    input.addEventListener("input", () => { if (input.value.trim() === "erase") ok.removeAttribute("disabled"); else ok.setAttribute("disabled", ""); });
    const bg = el("div", { class: "modal-bg" },
      el("div", { class: "modal" },
        el("h4", {}, "Erase SD card"),
        el("p", {}, "This permanently deletes ALL files on the card, including logs. Type \"erase\" to confirm."),
        el("div", { class: "field" }, input),
        el("div", { class: "btns" }, ok, el("button", { class: "ghost", onclick: () => bg.remove() }, "Cancel"))));
    document.body.append(bg);
  }
  const sd = sec("DATA LOG", sdInfo,
    el("div", { class: "btns" },
      el("a", { class: "btn", href: "/api/v1/log", download: "reactor_log.csv" }, "⬇ Download CSV"),
      el("button", { class: "ghost", onclick: () => { if (confirm("Clear the SD log?")) api.logClear(); } }, "Clear log"),
      el("button", { class: "stop", onclick: eraseModal }, "Erase all files")));
```

- [ ] **Step 3: Settings — motor Test button** — in `data/screens/settings.js`, replace the motor section block:
```js
  const motor = sec("MOTOR",
    el("div", { class: "row" }, field("CURRENT mA", curMa), field("MICROSTEPS", micro), field("DIRECTION", dirSel)),
    el("div", { class: "btns" }, el("button", { class: "go", onclick: () => api.disc({ currentMa: +curMa.value, microsteps: +micro.value, direction: dirSel.value }) }, "Apply")));
```
with:
```js
  const testBtn = el("button", { class: "ghost", onclick: async () => {
    testBtn.disabled = true; testBtn.textContent = "Testing…";
    await api.post("/api/v1/disc/test");
    setTimeout(() => { testBtn.disabled = false; testBtn.textContent = "Test"; }, 3200);
  } }, "Test");
  const motor = sec("MOTOR",
    el("div", { class: "row" }, field("CURRENT mA", curMa), field("MICROSTEPS", micro), field("DIRECTION", dirSel)),
    el("div", { class: "btns" },
      el("button", { class: "go", onclick: () => api.disc({ currentMa: +curMa.value, microsteps: +micro.value, direction: dirSel.value }) }, "Apply"),
      testBtn));
```

- [ ] **Step 4: Disable Test during a run** — in `data/screens/settings.js`, inside the existing `store.subscribe((d) => { ... })` callback, add (after the `sysInfo.textContent = ...` line):
```js
    if (!testBtn.disabled || testBtn.textContent === "Test") testBtn.disabled = (d.run || {}).active === true;
```

(`api.post` already exists in `core/api.js`.)

- [ ] **Step 5: Verify** — screenshot helper at `http://localhost:8000/#settings`; Read `/tmp/ui.png`. Confirm a red "Erase all files" button in DATA LOG and a "Test" button beside "Apply" in MOTOR. Then exercise the modal/test:
```bash
/tmp/pio-venv/bin/python tools/mock_server.py >/tmp/mock.log 2>&1 &  MOCK=$!
node -e "setTimeout(()=>{},1500)"
curl -s -XPOST localhost:8000/api/v1/disc/test -d '{}'; echo " (disc/test ok)"
curl -s -XPOST localhost:8000/api/v1/sd/erase -d '{}'; echo " (erase ok)"
kill $MOCK 2>/dev/null
```
Expected: both POSTs return `{"ok": true}`; screenshot shows the new buttons.

- [ ] **Step 6: Commit**

```bash
git add data/screens/settings.js data/app.css
git commit -m "feat(ui): SD erase modal + motor Test button"
```

---

## Definition of done

- `node test/ui/csv.test.mjs` passes (plus the existing UI tests).
- `pio run -e esp32-s3-devkitc-1` builds clean; `pio test -e native` passes.
- Against the mock (headless Chrome): History tab shows the log chart + recent-rows table; Settings shows the "Erase all files" red button (typed-"erase" modal) and a "Test" button beside Apply (disabled while running).
- `POST /api/v1/sd/erase` and `POST /api/v1/disc/test` ack `{ok:true}` on firmware and mock.
- **On-device:** Erase clears the card (Pi-image files gone, fresh log); Test spins the disc ~3 s then stops; History shows real logged rows.

## Out of scope
- True FAT reformat; SD usage/free-space; History pagination; `disc.testing` telemetry.
