# Web UI: Global Run Control + Merged Monitor/History — Design

Date: 2026-06-24
Status: Draft (for review)
Scope: Full stack — browser UI (`data/`), mock server (`tools/mock_server.py`), and ESP32 firmware (`src/`, `include/`).

## Summary

Restructure the web control surface around a **persistent run-control bar** and **two pages** instead of four:

- The page selector collapses from 4 tabs (`Monitor`, `Run`, `History`, `Settings`) to **2** (`Monitor`, `Settings`).
- A persistent **Start / Stop** control group sits next to the page selector, visible on every page, reflecting live run state.
  - **Start** becomes **Pause** while running (dropdown: pause motor / heater / all), and **Resume** while paused.
  - **Stop** is a dropdown: cancel / stop & save data / stop & discard data.
- The old **Run** page becomes the first block of **Settings** (parameters only; Start/Stop live in the global bar).
- The old **History** page merges into **Monitor** as a section below the live cards, with a run picker, Download CSV + Refresh, and a 10-row table that expands.
- Run data becomes **per-run CSV files** on the SD card. Stopping a run saves or discards its file; History lists/downloads saved runs.

Pause is for **maintenance**: it holds the selected subsystem(s) without ever stopping the run timer or CSV logging.

## Goals

1. One run-control surface available from anywhere, not buried on a Run page.
2. Make "this run's data" a first-class concept (per-run files), with explicit save/discard at stop.
3. Merge History into Monitor so live state and recent data sit together.
4. Keep the existing modular `data/` architecture and command-queue firmware pattern.

## Non-goals (this round)

- Browsing/charting arbitrary historical runs beyond the picker (multi-run comparison, date filters).
- Wall-clock timestamps in run names (no RTC/NTP; run IDs are integer indices).
- Changes to PID, calibration, WiFi, motor, or system settings beyond the Run-block relocation.
- Renaming/annotating saved runs.

## Decisions (locked during brainstorming)

| Topic | Decision |
|-------|----------|
| Build scope | Full stack: UI + mock server + firmware. |
| Pause semantics | Maintenance hold. Timer **never** stops; CSV logging continues. Options: pause motor / heater / all. |
| Resume | Single `Resume` action restores **all** held subsystems. |
| Stop "data" | **Per-run CSV files.** Save keeps the file; discard deletes it. Auto-stop (duration timeout) = save. |
| Stop "cancel" | Just closes the Stop menu; run continues. |
| History source | **Run picker**, defaults to latest. Download + Refresh act on the selected run. |
| Tabs | Collapse to `Monitor` · `Settings`. |
| Run params | Held in a shared module backed by `localStorage` so global Start works from any page. |
| Run-bar architecture | Persistent bar in the nav row driven by a store-subscribed module (mirrors `renderShell`). |
| Run-file identity | Integer index (`/runs/00007.csv`), id = max existing + 1, label `Run 7`. |

## UI Architecture

The existing app is a hash router (`data/app.js`) that wipes `#screen` on navigation, plus store-subscribed shell rendering (header chips + alarm banner). We keep this and add two small modules.

### New / changed files (`data/`)

| File | Change |
|------|--------|
| `index.html` | Nav row gains a run-control bar container next to `.tabs`; remove `Run` and `History` tabs. |
| `app.js` | `screens` map drops `run`/`history`; default route `monitor`; mount the run-bar module once at startup. |
| `core/runparams.js` | **New.** Holds `{ targetC, rpm, durationMin }`; getter/setter + subscribe. Persistence via an **injectable storage** (defaults to `globalThis.localStorage`, falls back to an in-memory object) so it is unit-testable in Node. Defaults `36 / 8 / 0`. |
| `core/runstate.js` | **New, DOM-free.** Pure derivation helpers: `deriveRunbar(status)` → `{ mode: "idle"\|"running"\|"paused", held: "motor"\|"heater"\|"all"\|null, startBlocked }`; `visibleRows(rows, expanded, cap=10)`. Unit-tested in Node. |
| `core/runs.js` | **New, DOM-free.** Pure helpers: `parseRunList(json)` → normalized run array; `latestRunId(runs)` → default picker selection. Unit-tested in Node. |
| `core/runbar.js` | **New, DOM.** Renders the persistent Start/Pause/Resume + Stop control group; subscribes to the store; imports `deriveRunbar` from `runstate.js`; reads `runparams` for Start; owns the two dropdown menus. |
| `core/api.js` | Add `runPause(target)`, `runResume()`, `runStop(data)` (extends existing), `listRuns()`, `runCsvUrl(id)`, `deleteRun(id)`. |
| `screens/settings.js` | Add a `RUN` section as the first block (params + compact status), bound to `runparams`. |
| `screens/monitor.js` | Append a `HISTORY` section using a shared history component. |
| `screens/history-section.js` | **New.** Extracted from `history.js`: run picker + chart + 10-row/expand table + Download/Refresh, scoped to a selected run id. |
| `screens/run.js` | **Removed** (logic split into `runparams` + Settings RUN section). |
| `screens/history.js` | **Removed** (logic moves to `history-section.js`). |
| `app.css` | Styles for the run-bar, dropdown menus, the RUN settings block, and the expandable table. |

### Run-control bar — state machine

State is derived from telemetry (`store.getStatus()`): `run.active`, `run.pause = { motor, heater }`.

| Derived state | Condition | Left button | Right button |
|---------------|-----------|-------------|--------------|
| Idle | `!run.active` | `▶ Start` (enabled) | `■ Stop ▾` (disabled) |
| Running | `run.active && !paused` | `⏸ Pause ▾` | `■ Stop ▾` (enabled) |
| Paused | `run.active && (pause.motor || pause.heater)` | `▶ Resume` | `■ Stop ▾` (enabled) |

- `paused = pause.motor || pause.heater`.
- **Pause ▾** menu: `Pause motor` → `runPause("motor")`, `Pause heater` → `runPause("heater")`, `Pause all` → `runPause("all")`.
- **Resume** → `runResume()` (clears all holds).
- **Stop ▾** menu: `Cancel` (close menu), `Stop & save data` → `runStop("save")`, `Stop & discard data` → `runStop("discard")`.
- Held-subsystem hint shown next to Resume (e.g. `motor held`, `heater held`, `all held`).
- Start disabled while a heater-probe fault / safety trip blocks starting (preserve current `run.js` guard: faulted probe → Start blocked, with title hint).
- Menus close on outside click / Escape / selection.

### Settings — RUN block (first section)

- Inputs: `TARGET TEMPERATURE °C`, `AGITATOR SPEED rpm`, `DURATION min` — bound to `runparams` (change → persist).
- While a run is active, `targetC`/`rpm` changes also push live `setpoint` (current `run.js` behavior, with `flashApplied` feedback). Duration is read-only mid-run (no live-duration API).
- Compact status line: state pill (`IDLE`/`RUNNING`/`PAUSED`) · `ELAPSED` · `REMAINING`.
- No Start/Stop here.

### Monitor — HISTORY section (appended below live cards)

- Section header: **run picker** (`<select>`, options from `listRuns()`, default = latest/current) on the left; **⬇ Download CSV** (links to `runCsvUrl(selectedId)`) and **Refresh** on the right.
- Chart: existing liquid-vs-setpoint render for the selected run.
- Table: shows **max 10 most-recent rows**; a footer toggle **`Show all ▾` / `Show less ▲`** expands to the full fetched set.
- Refresh reloads the selected run; switching the picker reloads + updates the Download link.

## API Contract

### `POST /api/v1/run`

```jsonc
{ "action": "start",  "targetC": 36, "rpm": 8, "durationMin": 0 }
{ "action": "pause",  "target": "motor" | "heater" | "all" }
{ "action": "resume" }
{ "action": "stop",   "data": "save" | "discard" }
```

- Validation: `start` keeps existing range checks (rpm 0..30, targetC 0..55). `pause` requires a valid `target`; `stop` requires `data` ∈ {save, discard}; otherwise `400 invalid_request`.
- All actions ack async (`{ "ok": true }`); effects observed via telemetry / `GET /runs`.
- `pause`/`resume`/`stop` while idle: no-op ack (mirrors firmware queue semantics).

### Runs

```
GET  /api/v1/runs              → { "runs": [ { "id": 7, "label": "Run 7",
                                              "startedSec": 1234, "durationSec": 600,
                                              "bytes": 48211, "current": true } ] }
GET  /api/v1/runs/<id>         → text/csv  (that run's file; download)
POST /api/v1/runs/<id>/delete  → { "ok": true }
```

- `current: true` marks the in-progress run (present only while running).
- `GET /api/v1/log` retained as an alias for the **latest** run's CSV (back-compat for any existing link).

### Status JSON additions

```jsonc
"run": {
  "active": true, "elapsedSec": 120, "remainingSec": null, "durationMin": 0,
  "id": 7,                          // current run file id (null when idle)
  "pause": { "motor": false, "heater": false }
}
```

## Firmware Design

### `SdLogger`

Add a per-run file concept alongside the existing single-log API:

- `startRun(label)` → choose next id (scan `/runs`, max + 1), open `/runs/<id>.csv`, write header, mark it current. Returns id (or 0 on failure).
- `appendLine(line)` → append to the **current run file** when one is open; otherwise fall back to the legacy `/log.csv` (so idle logging still works if needed).
- `endRun(bool save)` → close the current file; if `!save`, delete it; clear current.
- `listRuns()` → enumerate `/runs/*.csv` with id, byte size (label/duration derived in the loop/status builder).
- `runPath(id)` / `deleteRun(id)`.
- `eraseAll()` extends to clear `/runs` too.

File layout: `/runs/<5-digit-zero-padded-id>.csv` (e.g. `/runs/00007.csv`). Header identical to the current CSV header.

### `WebInterface`

- Extend the `/api/v1/run` handler for `pause`/`resume`/`stop{data}`.
- Add `GET /api/v1/runs`, `GET /api/v1/runs/<id>` (stream file), `POST /api/v1/runs/<id>/delete`.
- `Pending` struct gains: `pauseMotor`, `pauseHeater` (set/clear), `resume`, `stopData` (enum save/discard), plus a `deleteRunId`. Drained in `applyPending()` in loop context (no hardware/SD work in async callbacks).
- Status JSON builder adds `run.id` and `run.pause`.

### `Reactor` / control loop

- Pause = hold the selected output(s): motor paused → disc drive disabled; heater paused → heater output forced to 0 (PID held). The run stays active; elapsed/remaining keep advancing; CSV logging continues.
- Resume = restore normal motor/heater operation.
- Run lifecycle: `start` → `SdLogger::startRun`; periodic loop logging → `appendLine`; `stop save` → `endRun(true)`; `stop discard` → `endRun(false)`; duration timeout → `endRun(true)` (auto-save).
- Expose pause flags + current run id for the status builder.

## Mock Server (`tools/mock_server.py`)

- Add `pause`/`resume`/`stop{data}` to `api_run`; track `state["pause"] = {motor, heater}` and apply to the simulated rpm/heater (paused subsystem reads 0 but the run keeps elapsing).
- Maintain an in-memory list of runs: starting a run creates a current run record (id = max + 1) accumulating sampled rows; `stop save` finalizes it; `stop discard` drops it; duration timeout finalizes.
- `GET /api/v1/runs`, `GET /api/v1/runs/<id>` (returns CSV from the stored rows), `POST /api/v1/runs/<id>/delete`.
- `status()` adds `run.id` + `run.pause`.
- Keep `/api/v1/log` returning the latest run's CSV.

## Testing

- **Host unit tests** — new `test/ui/*.test.mjs` files matching the existing convention (plain Node, `node:assert/strict`, importing pure logic from `data/core/`; no DOM/`localStorage`):
  - `test/ui/runparams.test.mjs` — defaults, set/persist round-trip via an injected storage shim, subscribe/notify.
  - `test/ui/runstate.test.mjs` — `deriveRunbar(status)` for idle/running/paused + held-label + `startBlocked`; `visibleRows` 10-row cap and expand/collapse.
  - `test/ui/runs.test.mjs` — `parseRunList` and `latestRunId` (default selection = latest).
- **Mock server**: manual browser walk-through of the full flow (start → pause each subsystem → resume → stop save / discard; History picker + download + expand).
- **Firmware**: per-run file lifecycle on hardware — flash, drive via HTTP (per the saved hardware-validation setup), verify files appear/delete under `/runs`, pause holds the right subsystem without stopping the timer.

## Risks / Open considerations

- SD enumeration cost: `listRuns()` scans a directory each call; runs are few and the call is on-demand (picker/refresh), so acceptable.
- Back-compat: any external consumer of `/api/v1/log` keeps working via the latest-run alias.
- Run id reuse after deletion: ids are monotonic from `max + 1`; deleting the highest id could let a future run reuse it. Acceptable for this round (no external references to ids persist).
- `localStorage` run params can drift from firmware-applied setpoint after a live edit; the Settings status line reflects telemetry, the inputs reflect last-entered params — matches current behavior.
