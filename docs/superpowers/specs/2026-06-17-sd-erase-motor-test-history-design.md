# SD Erase · Motor Test · History — Design

**Date:** 2026-06-17
**Status:** Approved (pending implementation plan)
**Builds on:** the control UI (`2026-06-16-ui-control-app-design.md`, on `feature/ui-rebuild`) and the `/api/v1` firmware contract.

## Goal

Three additions to the reactor control app:
1. **Erase SD card** — wipe all files on the card (not a true FAT reformat) from Settings, behind a typed confirmation.
2. **Motor test** — a one-click, firmware-timed jog of the disc to confirm the drive turns, beside the motor settings.
3. **History panel** — a new tab that reads the SD CSV log and shows a trend chart + a recent-rows table.

> Naming note: this is an **erase** (delete all files), NOT a format — the Arduino SPI `SD` library has no FAT-format call. All names use "erase".

## Decisions (locked during brainstorming)

1. SD: **erase all files** (recursive delete + recreate the empty log), behind a typed-"erase" confirmation modal.
2. Motor test: **firmware-timed jog** (~8 rpm for ~3 s, auto-stop in firmware), refused while a run is active.
3. History: **chart + recent-rows table**, a new 4th tab.

---

## Feature 1 — Erase SD card

### Firmware
- `SdLogger::eraseAll()` — recursively delete every file and directory under root, then recreate the empty log file with its header (`AppConfig::Storage::kLogHeader`). Returns `true` on success (card still mounted, header written). Reuses the existing header-write path (as `clearLog()` does).
  - Recursion with the SPI `SD` lib: `File root = SD.open("/")`; for each entry, `SD.remove(path)` for files, recurse then `SD.rmdir(path)` for dirs. Skip nothing (clears leftover Pi-image files too).
- Endpoint **`POST /api/v1/sd/erase`** → set `pending_.sdErase` → `applyPending()` calls `sd_.eraseAll()`. Async ack `{ok:true}`; the result is observed via `storage` telemetry (`sdMounted`, and `logBytes` once available) / a re-read. Blocks the loop during deletion — acceptable for a one-shot admin op (same shape as `clearLog`).

### UI (Settings → DATA LOG)
- A red **"Erase all files"** button opens a **confirmation modal** (a lightweight overlay in `app.css`/`ui.js`):
  - Warning: "This permanently deletes ALL files on the SD card, including logs."
  - A text input; the **"Erase card"** action button stays disabled until the user types `erase`.
  - Cancel closes the modal. Confirm → `POST /api/v1/sd/erase`, toast "Erasing…", close modal.

## Feature 2 — Motor test

### Firmware
- `Reactor::startMotorTest()` — if **not** `running_`: `motor_.enable(true)`, `motor_.setRpm(kMotorTestRpm)`, set `motorTestUntilMs_ = millis() + kMotorTestMs`. If a run is active, no-op (refused).
- `Reactor::update()` services the test timer **before** its `if (!running_) return;` early-out: when `motorTestUntilMs_ != 0 && millis() >= motorTestUntilMs_` → `motor_.setRpm(0)`, `motor_.enable(false)`, `motorTestUntilMs_ = 0`. So the auto-stop runs even while idle and regardless of the client.
- Constants: `kMotorTestRpm = 8.0f`, `kMotorTestMs = 3000` (in `AppConfig::Motor` or `Reactor`).
- Endpoint **`POST /api/v1/disc/test`** → `pending_.motorTest` → `applyPending()` calls `reactor_.startMotorTest()`. Ack `{ok:true}`.
- Telemetry: no new field required — `disc.rpm` reflects the jog. (Optional `disc.testing` bool is out of scope; the UI uses a local timer for button feedback.)

### UI (Settings → MOTOR)
- A **"Test"** button beside the Apply button in the motor row. Click → `POST /api/v1/disc/test`, button shows "Testing…" and disables for ~3 s (client timer), then restores. Disabled while a run is active (`run.active`). The actual spin/stop is firmware-driven; Monitor shows `disc.rpm` rise then fall.

## Feature 3 — History panel

### Shell
- Add a 4th tab: **Monitor · Run · History · Settings** (History after Run). `index.html` tab list + `app.js` `screens` map gain `history`.

### `data/screens/history.js`
- On mount (and on **Refresh**): `GET /api/v1/log` → CSV text.
  - Non-200 / empty → show "No log data." (the endpoint 404s when no SD/log).
  - Parse CSV: header `t_ms,running,liquid_c,heater_c,setpoint_c,heater_pct,rpm,load,fault,safety`. A small pure parser (`core/csv.js` `parseCsv(text) -> {header, rows}`), host-tested.
- **Trend chart** (reuses `core/chart.js` `render`): build a buffer from the parsed rows — `t` = `liquid_c`, `s` = `setpoint_c` — **downsampled** to ≤ ~200 points (stride = ceil(n/200)), and `render()` it. (x-axis is sample order, matching the live Monitor chart.)
- **Recent-rows table**: the last ~50 rows, newest first — columns: time (`t_ms`, the device uptime in ms at that row, rendered as `h:mm:ss`), liquid °C, heater °C, setpoint °C, duty %, rpm, load, and a fault/safety flag. Plain styled `<table>`.
- A **Refresh** button re-fetches. Loads automatically on first mount.
- *Known limitation:* fetches and parses the whole CSV in-browser — fine for typical logs (clearable via erase/clear); server-side pagination/tailing deferred.

### `data/core/csv.js` (pure, host-tested)
- `parseCsv(text)` → `{ header: string[], rows: string[][] }`. Splits on newlines, ignores blank/trailing lines, splits cells on `,`. Numeric coercion happens at the call site (per column).

## Mock support (`tools/mock_server.py`)
- `POST /api/v1/sd/erase` → clears simulated log/SD state, returns `{ok:true}`.
- `POST /api/v1/disc/test` → sets `state["rpm"]` to ~8 for ~3 s then 0 (mirrors the firmware jog), returns `{ok:true}`.
- `GET /api/v1/log` already serves a CSV (used by Download). Ensure it returns a small **synthetic multi-row CSV** with the real header so History renders a chart + table without hardware. (Currently the mock's `/log` returns a placeholder; extend it to emit ~30–60 plausible rows.)

## API additions (summary)

| Method · Path | Body | Effect |
|---|---|---|
| POST `/api/v1/sd/erase` | `{}` | Delete all SD files; recreate empty log. Ack async. |
| POST `/api/v1/disc/test` | `{}` | Firmware-timed disc jog (~8 rpm, ~3 s, auto-stop). Refused while running. |
| GET `/api/v1/log` | — | (existing) CSV download — also consumed by History. |

## Testing
- **Host (Node):** `parseCsv` unit tests (header + rows, blank-line handling, numeric values at call site).
- **Firmware:** compiles clean; `pio test -e native` unaffected.
- **Mock + headless Chrome:** erase modal (typed-`erase` gate enables the button), motor Test button ("Testing…" + disabled-while-running), History tab (chart + recent-rows table from the synthetic CSV).
- **On-device:** erase clears the card (Pi-image files gone, fresh log); Test spins the disc ~3 s then stops; History shows real logged rows.

## File-by-file summary

| File | Action |
|---|---|
| `include/storage/SdLogger.hpp` / `src/storage/SdLogger.cpp` | Add `eraseAll()`. |
| `include/control/Reactor.hpp` / `src/control/Reactor.cpp` | Add `startMotorTest()` + test-timer servicing in `update()`. |
| `include/app_config.hpp` | `kMotorTestRpm`, `kMotorTestMs`. |
| `include/net/WebInterface.hpp` / `src/net/WebInterface.cpp` | `Pending.sdErase` + `Pending.motorTest`; `POST /sd/erase`, `POST /disc/test`; consume in `applyPending`. |
| `data/index.html` | Add the History tab. |
| `data/app.js` | Register the `history` screen. |
| `data/app.css` | Modal + table styles. |
| `data/core/csv.js` | Create — `parseCsv` (pure). |
| `data/screens/history.js` | Create — log reader (chart + table). |
| `data/screens/settings.js` | Erase modal trigger; motor Test button. |
| `test/ui/csv.test.mjs` | Create — `parseCsv` tests. |
| `tools/mock_server.py` | `/sd/erase`, `/disc/test`, synthetic `/log` CSV. |

## Out of scope
- True FAT reformat; SD free-space/usage reporting (`logBytes` stays best-effort).
- History pagination / server-side log tailing; per-column chart selection.
- A `disc.testing` telemetry flag (UI uses a local timer).
