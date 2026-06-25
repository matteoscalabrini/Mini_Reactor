# Mini-Reactor HTTP API (`/api/v1`)

REST + WebSocket control surface served by the ESP32-S3 firmware
([`src/net/WebInterface.cpp`](src/net/WebInterface.cpp)). The browser UI in
[`data/`](data/) is the primary client; [`tools/mock_server.py`](tools/mock_server.py)
reimplements this contract for UI development off-device.

- **Base URL:** `http://<device-ip>/` (port 80, no mDNS — get the IP from the serial boot log)
- **Auth:** none (LAN device)
- **CORS:** every response sends `Access-Control-Allow-Origin: *`
- **Content type:** all bodies are JSON; all POSTs with a body must send `Content-Type: application/json`

## Conventions

**Command model — async ack.** Mutating endpoints (`POST`) do **not** apply the
change inline. The async handler validates the request, queues a pending command
under a mutex, and returns immediately. The command is drained and applied in the
main loop (`applyPending()`), and every applied command is echoed to serial as
`[CMD] …`. Callers observe the *effect* by polling `GET /api/v1/status` or via the
`/ws` telemetry feed — **not** from the POST response.

**Success envelope:**
```json
{ "ok": true }
```
Read endpoints (`GET /status`, `/runs`, `/calibration`, `/wifi/scan`) return their
payload directly instead of `{ok:true}`. File downloads return the raw file.

**Error envelope** (HTTP 400/404/503):
```json
{ "ok": false, "error": { "code": "out_of_range", "message": "rpm must be 0..30" } }
```

| Code | When |
|------|------|
| `invalid_request` | missing/invalid action or field |
| `out_of_range` | value outside accepted bounds |
| `not_found` | unknown endpoint or run id |
| `no_log` | SD not mounted / no log file |
| `wifi_ssid_required` | connect without ssid |

---

## Telemetry

### `GET /api/v1/status`
Returns the cached status document (rebuilt in the loop ~10 Hz). This is the single
source of truth for all live state. Shape:

```jsonc
{
  "apiVersion": "1.0",
  "uptimeSec": 1234,
  "system":  { "firmware": "1.0.0", "freeHeap": 210000, "vbus": "12V", "sdMounted": true },
  "thermal": {
    "tempC": 35.81,            // null when sensorFault
    "errorC": 0.19,            // setpoint - temp; null when sensorFault
    "setpointC": 36.0,
    "heaterPct": 42.5,
    "fault": false,
    "safety": {
      "tripped": false,
      "heaterTempC": 48.2,     // null when heater NTC faulted
      "heaterMaxC": 80.0,
      "processMaxC": 55.0,
      "probe": {               // heater NTC safety probe
        "adcRaw": 1820,
        "resistanceOhms": 9120, // null when faulted -> UI blocks Start
        "calibrated": false,
        "method": "beta"
      }
    },
    "pid": {
      "kp": 0.08, "ki": 0.0015, "kd": 0.4,
      "p": 0.0, "i": 0.0, "d": 0.0, "out": 0.425, "mode": "auto",
      "autotune": { "active": false, "progress": 0, "result": null }
    }
  },
  "disc": {
    "running": true, "rpm": 8.0, "rpmSetpoint": 8.0, "direction": "cw",
    "currentMa": 600, "microsteps": 16, "enabled": true,
    "load": 120,               // StallGuard; null when idle or driver offline
    "driver": {
      "version": "0x21", "connected": true,
      "flags": { "otpw": false, "ot": false, "stall": false,
                 "openLoadA": false, "openLoadB": false, "shortA": false, "shortB": false }
    }
  },
  "run": {
    "active": true,
    "elapsedSec": 65,
    "remainingSec": null,      // null when durationMin == 0 (run-until-stopped)
    "durationMin": 0,
    "id": 7,                   // current run id; null when idle
    "name": "Ethanol distillation"  // null when unnamed or idle
  },
  "wifi": { "mode": "sta", "connected": true, "ssid": "LAB-NET", "ip": "192.168.1.42", "rssi": -55 },
  "storage": {
    "sdMounted": true,
    "logBytes": null,          // not yet reported
    "logging": true,
    "logIntervalSec": 10       // current SD row cadence (see /log/interval)
  },
  "alarms": [
    { "code": "sensor_fault", "severity": "warn", "since": 1200 }
  ]
}
```

Alarm codes the firmware emits: `sensor_fault`, `heater_probe_fault`,
`safety_tripped`, `driver_ot`, `driver_otpw`, `driver_stall`, `driver_open_load`
(severity `warn` or `critical`).

> **Gap:** the UI's pause logic reads `run.pause.{motor,heater}`
> ([`runstate.js`](data/core/runstate.js)), but the firmware `run` object does **not**
> emit a `pause` object. See [Connection audit](#connection-audit).

### `WS /ws`
On connect, the server sends one status snapshot immediately, then broadcasts the
same status JSON as `GET /api/v1/status` at `wsPushPeriodMs` (250 ms ≈ 4 Hz). The
socket is push-only; the client sends nothing.

---

## Run control

### `POST /api/v1/run`
Start or stop a run. Body field `action` selects the operation.

**Start:**
```json
{ "action": "start", "targetC": 36.0, "rpm": 8.0, "durationMin": 0, "name": "Ethanol distillation" }
```
- `targetC` 0–55 (default 36), `rpm` 0–30 (default 8), `durationMin` ≥0 (default 0 = until stopped)
- `name` optional, ≤32 chars; firmware sanitizes (strips control chars, trims). Empty ⇒ unnamed.
- On success the reactor starts **and**, only if `reactor.running()` becomes true, a
  per-run CSV `/runs/NNNNN.csv` is created (plus `/runs/NNNNN.name` when named).
  If the pre-flight `thermal.enable()` refuses (faulted/missing probe), the run stays
  idle and **no run file is created** — the `{ok:true}` ack only means the command was queued.

**Stop:**
```json
{ "action": "stop", "data": "save" }   // "save" (default) | "discard"
```
`discard` deletes the in-progress CSV + sidecar; `save` keeps them.

- ❌ `action: "pause"` and `action: "resume"` are **rejected** (HTTP 400
  `invalid_request`, "action must be start|stop"). The UI sends these — see
  [Connection audit](#connection-audit).

### `GET /api/v1/runs`
Saved-run index, served from a ~1 Hz loop-built cache:
```json
{ "runs": [ { "id": 7, "label": "Ethanol distillation", "bytes": 10240, "current": true } ] }
```
`label` is the sidecar name or `"Run <id>"`. `current:true` marks the in-progress run.

### `GET /api/v1/runs/{id}`
Downloads that run's CSV (`text/csv`, attachment). 404 `not_found` if absent. The
server names the attachment after the on-disk file (`00007.csv`); the UI overrides
the saved filename with the **session name** (`jimbo.csv`, or `run_<id>.csv` when
unnamed) via the download link, since UI and API are same-origin.

### `POST /api/v1/runs/{id}/delete`
Queues deletion of a run's CSV + sidecar. (The open run is discarded if targeted.)

> Both `{id}` routes are regex routes — the firmware build needs
> `-DASYNCWEBSERVER_REGEX=1` (set in [`platformio.ini`](platformio.ini)).

---

## Live setpoint

### `POST /api/v1/setpoint`
Change target temperature and/or speed of a running process without restarting.
```json
{ "targetC": 37.5, "rpm": 10 }   // either field optional
```
`targetC` 0–55. Out-of-range `targetC` ⇒ 400.

---

## Disc / motor

### `POST /api/v1/disc`
Drive parameters (any subset):
```json
{ "rpm": 8, "currentMa": 600, "microsteps": 16, "direction": "cw", "enabled": true }
```
`direction`: `"cw"` | `"ccw"`. `currentMa` is clamped to 100–1500 in firmware.

### `POST /api/v1/disc/test`
Brief jog (~8 rpm, 3 s) to confirm the drive turns. No body. Ignored while a run is active.

---

## Thermal PID & calibration

### `POST /api/v1/pid`
Gains and/or mode:
```json
{ "kp": 0.08, "ki": 0.0015, "kd": 0.4, "mode": "auto" }
```
Gains apply only when all three of `kp`,`ki`,`kd` are present. `mode` is a string
(e.g. `"auto"` | `"manual"`).

### `POST /api/v1/pid/autotune`
```json
{ "action": "start" }   // "start" | "cancel"
```
Progress/result surface in `status.thermal.pid.autotune`.

### `GET /api/v1/calibration`
```json
{ "method": "beta", "calibrated": false,
  "points": [ { "referenceC": 25.0, "resistanceOhms": 10000 } ] }
```

### `POST /api/v1/calibration/point`
Capture a calibration point at the known reference temperature:
```json
{ "referenceC": 25.0 }   // required
```

### `POST /api/v1/calibration/compute`
No body. Fits offset/Beta/Steinhart by point count; result via `GET /calibration`.

### `POST /api/v1/calibration/reset`
No body. Reverts to factory Beta.

---

## Storage / data log

**Run-only logging:** the card holds nothing but per-run files. While a run is
open, one CSV row is appended to `/runs/NNNNN.csv` every `logIntervalSec`; when no
run is open, nothing is written. There is no always-on `/reactor_log.csv`. Header:
`t_ms,running,liquid_c,heater_c,setpoint_c,heater_pct,rpm,load,fault,safety`.

### `GET /api/v1/log`
Convenience alias that downloads the **latest run's** CSV (the newest `/runs`
file). The newest run id is resolved from a loop-built cache, so this handler does
**not** enumerate the card from the async task. 503 `no_log` when the card isn't
mounted or no runs exist yet. The UI's "Download latest run" button resolves the
newest run from `GET /runs` and saves it under the **session name** (`jimbo.csv`).
Equivalent to `GET /api/v1/runs/{latestId}`.

### `POST /api/v1/log/interval`
Set the SD row cadence (persisted in NVS, namespace `sdlog`):
```json
{ "seconds": 10 }   // required, 1..3600
```
Out-of-range ⇒ 400 `out_of_range`. Current value is reported in
`status.storage.logIntervalSec`.

### `POST /api/v1/sd/erase`
No body. **Destructive** — deletes every file/dir on the card (all runs). The open
run is closed and discarded first.

---

## WiFi

### `GET /api/v1/wifi/scan`
Triggers a fresh scan **and** returns the last cached scan:
```json
{ "scanning": false, "networks": [ { "ssid": "LAB-NET", "rssi": -55, "secure": true } ] }
```
Poll until `scanning:false` for fresh results.

### `POST /api/v1/wifi/connect`
```json
{ "ssid": "LAB-NET", "password": "secret" }   // ssid required; password optional (open net)
```

### `POST /api/v1/wifi/forget`
No body. Clears stored credentials and returns to the setup AP.

---

## Static assets & SPA fallback
`GET /` and any non-`/api/` path serve the UI from SPIFFS (`index.html`,
`app.css`, `app.js`, `wordmark.svg`, …) with `index.html` as the SPA fallback.
Unknown `/api/` paths return 404 `not_found`.

---

## Connection audit

UI client = [`data/core/api.js`](data/core/api.js) + direct calls in
[`data/screens/settings.js`](data/screens/settings.js) /
[`data/app.js`](data/app.js). FW = [`src/net/WebInterface.cpp`](src/net/WebInterface.cpp).
Mock = [`tools/mock_server.py`](tools/mock_server.py).

| Endpoint / action | UI calls | Firmware | Mock | Status |
|---|---|---|---|---|
| `GET /status` | ✅ | ✅ | ✅ | OK |
| `WS /ws` | ✅ | ✅ | ✅ | OK |
| `POST /run` start | ✅ | ✅ | ✅ | OK |
| `POST /run` stop | ✅ | ✅ | ✅ | OK |
| **`POST /run` pause** | ✅ `runPause()` | ❌ **400** | ✅ | **MISSING in FW** |
| **`POST /run` resume** | ✅ `runResume()` | ❌ **400** | ✅ | **MISSING in FW** |
| `status.run.pause` field | ✅ reads it | ❌ not emitted | ✅ | **MISSING in FW** |
| `GET /runs` | ✅ | ✅ | ✅ | OK |
| `GET /runs/{id}` | ✅ | ✅ | ✅ | OK |
| `POST /runs/{id}/delete` | ✅ | ✅ | ✅ | OK |
| `POST /setpoint` | ✅ | ✅ | ✅ | OK |
| `POST /disc` | ✅ | ✅ | ✅ | OK |
| `POST /disc/test` | ✅ | ✅ | ✅ | OK |
| `POST /pid` | ✅ | ✅ | ✅ | OK |
| `POST /pid/autotune` | ✅ | ✅ | ✅ | OK |
| `GET /calibration` | ✅ | ✅ | ✅ | OK |
| `POST /calibration/{point,compute,reset}` | ✅ | ✅ | ✅ | OK |
| `GET /log` (download latest run) | ✅ | ✅ | ✅ | OK |
| `POST /log/interval` | ✅ | ✅ | ✅ | OK |
| `POST /sd/erase` | ✅ | ✅ | ✅ | OK |
| `GET /wifi/scan` | ✅ | ✅ | ✅ | OK |
| `POST /wifi/connect` | ✅ | ✅ | ✅ | OK |
| `POST /wifi/forget` | ✅ | ✅ | ✅ | OK |
| `POST /debug/probe-fault` | ❌ | ❌ | ✅ | Mock-only test affordance |

**Only gap: run pause/resume** (the deferred "Plan B"). The UI ships Pause ▾ /
Resume controls and derives a `paused` state from `status.run.pause`, but the
firmware neither accepts the actions nor emits the field. To close it the firmware
needs: (1) `action:"pause"` with `target` ∈ `motor|heater|all`, (2) `action:"resume"`,
and (3) a `run.pause:{motor,heater}` object in the status document.

### Route registration order (firmware footgun)

ESPAsyncWebServer matches handlers in registration order, and a plain handler for
`/api/v1/x` also matches any URL starting with `/api/v1/x/` (built-in prefix
match; the JSON body handler does the same and has **no** regex support). So a
parent route registered before a same-method child **swallows** the child. The
firmware must register the specific child first. Affected pairs (all fixed —
children registered first):

| Child (must be first) | Parent that would shadow it |
|---|---|
| `GET /api/v1/runs/{id}` (regex) | `GET /api/v1/runs` |
| `POST /api/v1/pid/autotune` | `POST /api/v1/pid` |
| `POST /api/v1/disc/test` | `POST /api/v1/disc` |

`/api/v1/calibration/*` is **not** affected because its parent `/api/v1/calibration`
is GET while the children are POST (method mismatch ⇒ no shadowing).

---

## Notes on SD logging (why a run may show no data)

Logging is **run-only**: the card is written only while a run is open, into
`/runs/NNNNN.csv`. If a run appears to have no data, check in order:

1. **Card mounted?** `status.storage.sdMounted` / `system.sdMounted` must be `true`.
   The boot log prints the mount result and a write/read self-test
   (`read/write test: PASS`). If it failed, nothing is written.
2. **Is a run actually open?** A row is written only when `reactor.running()` is
   true and `startRun()` opened a file (`status.run.id` is non-null). `start()` is
   refused — and the UI disables Start — when the heater NTC is faulted
   (`status.thermal.safety.probe.resistanceOhms == null`).
3. **Row cadence is `status.storage.logIntervalSec`** (NVS-persisted, 1..3600 s). A
   large value (e.g. left high from testing `/log/interval`) means rows appear rarely.
4. **Just started?** A freshly opened run file has only the header until the first
   interval elapses. The Monitor → HISTORY card does **not** auto-refresh; click
   *Refresh* to re-read a live run (known minor UI limitation).
5. **`mounted_` latches false on the first failed write** and does not re-mount
   until reboot — a transient SPI/FS error stops logging for the session.

> Both download endpoints serve the same per-run files: `GET /runs/{id}` (a chosen
> run) and `GET /log` (the latest run). Neither enumerates the card from the async
> task — `/log` uses a loop-built cache of the newest run id.
