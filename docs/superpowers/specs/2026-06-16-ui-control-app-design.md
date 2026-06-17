# Mini-Reactor Control UI — Design

**Date:** 2026-06-16
**Status:** Approved (pending implementation plan)
**Builds on:** the complete `/api/v1` firmware contract (P1–P4 + NTC safeguard, all on `main`), the firmware API design spec (`2026-06-15-reactor-firmware-api-design.md` §4/§5/§12), and the prior visual brainstorm in `.superpowers/brainstorm/58139-1781527304/` (`nav-shell.html`, `monitor.html`, `architecture.html`).

## Goal

Replace the stale dark "terminal" single-page UI in `data/` (built before the API work; it targets the old `/api/*` schema and won't run against `/api/v1`) with a light, tabbed, prosumer control app — **Monitor / Run / Settings** — that consumes the live `/api/v1` + WebSocket contract and exposes every shipped feature (rpm + StallGuard, PID + autotune, NTC calibration + safety probe, WiFi, SD log, alarms). Fully testable against `tools/mock_server.py` with no hardware.

## Decisions (locked during brainstorming)

1. **Direction:** full §12 redesign, building directly on the prior prototypes (top-bar + segmented-tabs frame from `monitor.html`, its design tokens, Doto + Inter).
2. **Scope:** one spec/plan covering the shell **and** all three screens (Monitor, Run, Settings).
3. **Tech:** structured **vanilla**, no build step — HTML + CSS (design tokens) + ES modules, served as static files from SPIFFS, developed against the mock. No framework, no Node toolchain.
4. **Fonts:** bundle Doto + Inter **woff2 in SPIFFS** (`data/fonts/`) via `@font-face`, with a system fallback stack — so the UI renders correctly offline and on the device's own setup AP.

## Design language

From `monitor.html` (authoritative). Tokens in `:root`:

```
--bg:#eaeff3; --surface:#fff; --surface-2:#f4f8fb; --border:#dde6ec; --border-2:#e9eff4;
--ink:#14242e; --muted:#5c727f; --faint:#9fb1bc;
--accent:#1c9cd8; --accent-deep:#0e7bb0; --accent-soft:#e6f5fc;
--ok:#27ae60; --warn:#e8920c; --bad:#e2483a;
--shadow:0 1px 2px rgba(20,40,55,.04),0 10px 30px rgba(20,40,55,.06);
--mono:"Doto",ui-monospace,monospace; --sans:"Inter",system-ui,sans-serif;
```

Doto (variable, weights ~500–900) for the wordmark + all numeric readouts; Inter (400/500/600/700) for body/labels. Cards: white surface, 1px border, 14px radius, soft shadow. Section headers: 10–11px, letter-spaced, `--muted`. Severity colors: `--ok/--warn/--bad`.

## Architecture & file structure

A single `index.html` holds the shell (top bar + tabs + a `#screen` content slot). ES modules, loaded with `<script type="module">`; no bundler. Tab switch tears down the previous screen and mounts the next; only the active screen renders.

```
data/
  index.html            shell markup (top bar, tabs, alarm banner, #screen slot)
  app.css               tokens + shell + shared component styles
  app.js                boot: mount shell, wire tabs, start WS, route active screen
  core/
    store.js            holds latest telemetry; subscribe(fn) / set(status); last-frame cache
    ws.js               /ws client, JSON parse -> store.set, auto-reconnect w/ backoff, online state
    api.js              REST helpers: get(path), post(path, body) -> {ok,...}; typed wrappers per endpoint
    ui.js               DOM helpers: el(), fmt (hh:mm:ss, fixed, dash-on-null), bar(), pill(), toast()
    chart.js            SVG trend chart (measured vs setpoint); samples the store at a fixed
                        cadence (~1 pt / 2 s) into a ~150-point ring buffer (~5 min)
  screens/
    monitor.js          read-only dashboard (subscribes to store)
    run.js              run controls + live run state
    settings.js         wifi / sd / pid+autotune / calibration / motor / system sub-sections
  fonts/
    doto.woff2  inter.woff2   (bundled; @font-face in app.css)
  logo.svg              (kept/refreshed)
```

Each module has one responsibility and a small interface: `store` (state + pub/sub), `ws` (transport in), `api` (transport out), `ui`/`chart` (render helpers), `screens/*` (`mount(rootEl)` / `unmount()` that subscribe to the store and issue commands via `api`). The stale `app.js`/`app.css`/`index.html` are replaced.

## Data flow

- One WebSocket to `/ws` receives the **full status JSON** (~10 Hz, identical shape to `GET /api/v1/status`). `ws.js` parses each frame into `store`. Screens subscribe; the active screen re-renders on each frame.
- Commands are `fetch` POSTs via `api.js`. The firmware is **async-queue**: a command acks `{ok:true}` immediately and the effect appears in the next telemetry frame (no synchronous result body). The UI therefore reflects state from the store, not from command responses — e.g. a refused Start shows `run.active:false` + the alarm banner, never a phantom "running".
- On WS drop: `store` flips to an offline state, the shell shows a disconnected indicator, and `ws.js` reconnects with backoff. Initial paint uses `GET /api/v1/status` before the socket is up.

## Shell

- **Top bar:** Doto wordmark `MINI·REACTOR`; right-aligned status chips — LINK (ip or "AP"/offline), VBUS (`system.vbus`), SD (`system.sdMounted`), UP (`uptimeSec`), and a WS-connection dot.
- **Segmented tabs:** Monitor / Run / Settings (pill control; active = accent). Client-side switch; default Monitor.
- **Global alarm banner:** when `alarms[]` is non-empty, a severity-colored strip below the tabs lists active codes (mapped to human labels, e.g. `heater_probe_fault → "Heater probe fault"`, `safety_tripped → "Safety cutout"`, `driver_ot → "Driver over-temp"`). `critical` = `--bad`, `warn` = `--warn`. Always visible regardless of active tab.
- Responsive: tabs/chips wrap under the brand below ~640px.

## Screen — Monitor (read-only)

Per `monitor.html` plus the `architecture.html` feature list. Grid `1.55fr / 1fr`, collapses to one column < 820px.

- **Bath temperature (hero):** `thermal.tempC` (Doto, large, `--accent-deep`) + °C; meta `thermal.setpointC`, `thermal.errorC` (colored by sign); a status pill (REGULATING when `run.active`, else MONITOR; PROBE FAULT when `thermal.fault`; SAFETY CUTOUT when `thermal.safety.tripped`). `null` tempC → `--.-` + fault styling.
- **Trend chart** (`core/chart.js`): measured (solid accent) vs setpoint (dashed faint); the chart samples the store at a fixed cadence (~1 pt / 2 s) into a ~150-point ring buffer (≈ "last 5 min"), decoupled from the ~10 Hz telemetry rate; SVG area+line.
- **Run state card:** `run.active` pill; elapsed / remaining / duration (`run.elapsedSec`, `run.remainingSec` null → "—", `run.durationMin` 0 → "∞").
- **Agitator card:** `disc.rpm` + a SPEED bar (rpm vs max 30); **StallGuard load** (`disc.load`, null when idle/disconnected → "—").
- **Heater duty card:** `thermal.heaterPct` + PWM bar.
- **Heater safety probe card:** `thermal.safety.probe` — `calibrated` (Yes/No), `method` (beta/offset/steinhart), `resistanceOhms` (null → "fault"), `heaterTempC` vs `heaterMaxC`; shows FAULT (null/`heater_probe_fault`) or OVER-LIMIT (`safety.tripped`) prominently (ties into the NTC safeguard).

## Screen — Run (controls)

- **Inputs:** target temp (number, 0–55 °C), target rpm (slider + number, 0–30), duration (number minutes, 0 = ∞).
- **Actions:** Start → `POST /api/v1/run {action:"start", targetC, rpm, durationMin}`; Stop → `{action:"stop"}`. Client-side range guards mirror the firmware (rpm 0–30, targetC 0–55); a 400 `out_of_range` surfaces as an inline error.
- **Live adjust while running:** target temp / rpm changes post to `POST /api/v1/setpoint {targetC?, rpm?}` (don't restart the run).
- **Live run state:** state pill (`run.active`), elapsed, remaining. If Start is **refused** (probe fault → `run.active` stays false), the alarm banner explains why; the Start button can show a disabled/blocked hint when `thermal.safety.probe` is faulted.

## Screen — Settings (only features with shipped endpoints)

- **WiFi:** current `wifi.mode/connected/ssid/ip/rssi`; Scan (`GET /api/v1/wifi/scan`, poll until results) → network list; Connect (`POST /api/v1/wifi/connect {ssid,password}`); Forget (`POST /api/v1/wifi/forget`).
- **SD / data log:** `storage.sdMounted/logging` (`logBytes` is currently `null` → show "—"/unknown); Download CSV (`GET /api/v1/log`); Clear (`POST /api/v1/log/clear`, confirm).
- **PID tuning:** kp/ki/kd inputs (prefilled from `thermal.pid`) → `POST /api/v1/pid {kp,ki,kd}`; mode auto/manual → `POST /api/v1/pid {mode}`; live P/I/D + output from `thermal.pid`.
- **Autotune:** Start/Cancel → `POST /api/v1/pid/autotune {action}`; live `thermal.pid.autotune.active/progress/result`.
- **Heater-NTC calibration:** probe state (`thermal.safety.probe`); `GET /api/v1/calibration` (method/calibrated/points); Capture point with a reference-°C input → `POST /api/v1/calibration/point {referenceC}`; Compute → `/compute`; Reset → `/reset` (confirm). Results observed via telemetry / re-GET (async).
- **Motor config:** current mA, microsteps, direction (cw/ccw) from `disc.*` → `POST /api/v1/disc {currentMa, microsteps, direction}`.
- **System (read-only):** `system.firmware`, `uptimeSec`, `system.freeHeap`, `system.vbus`, `disc.driver.version/connected`.

**Not built (no firmware endpoint):** SD **format** and system **restart** — the API design defers SD/system endpoints. These are omitted from the UI (not shown as dead buttons); revisit when those endpoints ship.

## API contract reference (for the plan)

WS `/ws` and `GET /api/v1/status` return the same JSON:

```
apiVersion, uptimeSec,
system:{firmware,freeHeap,vbus,sdMounted},
thermal:{tempC|null, errorC|null, setpointC, heaterPct, fault,
  safety:{tripped, heaterTempC|null, heaterMaxC, processMaxC,
          probe:{adcRaw, resistanceOhms|null, calibrated, method}},
  pid:{kp,ki,kd,p,i,d,out,mode, autotune:{active,progress,result|null}}},
disc:{running, rpm, rpmSetpoint, direction, currentMa, microsteps, enabled, load|null,
  driver:{version, connected, flags:{otpw,ot,stall,openLoadA,openLoadB,shortA,shortB}}},
run:{active, elapsedSec, remainingSec|null, durationMin},
wifi:{mode, connected, ssid, ip, rssi|null},
storage:{sdMounted, logBytes|null, logging},
alarms:[{code, severity, since}]
```

Commands (all ack `{ok:true}` async; errors `{ok:false,error:{code,message}}`):

| Method · Path | Body | Notes |
|---|---|---|
| POST `/api/v1/run` | `{action:"start", targetC(0–55), rpm(0–30), durationMin}` / `{action:"stop"}` | 400 `out_of_range` on bounds |
| POST `/api/v1/setpoint` | `{targetC?, rpm?}` | live changes |
| POST `/api/v1/disc` | `{rpm?, currentMa?, microsteps?, direction?(cw\|ccw), enabled?}` | motor config |
| POST `/api/v1/pid` | `{kp,ki,kd}` and/or `{mode:"auto"\|"manual"}` | gains require all three |
| POST `/api/v1/pid/autotune` | `{action:"start"\|"cancel"}` | |
| GET `/api/v1/calibration` | — | `{method,calibrated,points:[{referenceC,resistanceOhms}]}` |
| POST `/api/v1/calibration/point` | `{referenceC}` | 400 if missing |
| POST `/api/v1/calibration/{compute,reset}` | `{}` | |
| GET `/api/v1/wifi/scan` | — | `{scanning, networks:[{ssid,rssi,secure}]}` |
| POST `/api/v1/wifi/connect` | `{ssid,password}` | |
| POST `/api/v1/wifi/forget` | `{}` | |
| GET `/api/v1/log` | — | CSV download |
| POST `/api/v1/log/clear` | `{}` | |

## Error handling & states

- `null` telemetry fields render as `—` / fault styling (never `NaN`/`null` text).
- Failed command (non-2xx or network error) → inline field error or a toast; the UI never assumes success — it waits for the next telemetry frame.
- Alarm/safety/fault states are surfaced in the banner and on the relevant Monitor card.
- WS offline → disconnected indicator + greyed live values + auto-reconnect; REST still usable for one-shot reads.

## Testing & dev loop

- `tools/mock_server.py` **serves `data/` statically** (`add_static("/", DATA)`) and fakes the full REST + `/ws`. Dev loop: run the mock → open `http://localhost:8000` in a browser → exercise each screen. The mock already simulates thermal model, rpm/load, calibration, autotune, and a `debug/probe-fault` toggle.
- Verification: drive each screen in a real browser against the mock and capture screenshots (Monitor live values + chart; Run start/stop incl. refused-start with `debug/probe-fault`; each Settings sub-section). Then on-device after `pio run -t uploadfs` + a browser against the device IP.
- The mock may need small additions to fully exercise Settings (e.g. `/disc` motor-config echo, PID mode echo) — the plan will reconcile mock gaps against this contract.

## Out of scope (later)

- SD **format** and system **restart** (no firmware endpoints yet).
- Auth token on write endpoints (firmware-deferred).
- Multi-stage run profiles/recipes.
- Historical/persisted charts (the trend is a live client-side ring buffer only).

## File-by-file summary

| File | Action |
|---|---|
| `data/index.html` | Replace — shell (top bar, tabs, alarm banner, `#screen`). |
| `data/app.css` | Replace — tokens + shell + shared components; `@font-face` for bundled fonts. |
| `data/app.js` | Replace — boot/mount/tab-routing/WS start. |
| `data/core/{store,ws,api,ui,chart}.js` | Create — data layer + render helpers. |
| `data/screens/{monitor,run,settings}.js` | Create — the three screens. |
| `data/fonts/{doto,inter}.woff2` | Create — bundled fonts. |
| `data/logo.svg` | Keep/refresh. |
| `tools/mock_server.py` | Modify only if needed to exercise Settings (motor/PID-mode echo). |
