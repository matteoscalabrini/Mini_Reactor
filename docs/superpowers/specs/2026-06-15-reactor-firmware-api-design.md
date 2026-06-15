# Mini-Reactor — Firmware Control & API Reference Design

**Date:** 2026-06-15
**Status:** Approved design (pre-implementation)
**Approach:** Firmware-first. Define the complete `/api/v1` contract up front; implement
in vertical phases. The redesigned web UI is a **later** effort built against this reference.

---

## 1. Context

Mini-Reactor is a benchtop **rotating-disc bioreactor** for producing **bacterial cellulose**.
A TMC2209-driven stepper turns a shaft of discs that sit **semi-submerged** in the medium; as
the discs rotate between liquid and air, cellulose accumulates on them as the yield. Rotation is
therefore part of the bioprocess, not stirring. A PWM heater, driven by a PID on the DS18B20 liquid
probe, holds the bath temperature; a heater-mounted NTC provides an independent over-temp cutoff.
The system's nominal operating point is **8 rpm at 36 °C**.

The firmware (ESP32-S3, Arduino/PlatformIO) already runs the reactor and serves a dark
"mission-control" web UI over an ad-hoc REST + WebSocket API. This effort replaces that API with
a complete, versioned, documented contract and expands control capability, then (separately)
rebuilds the UI against it.

### Existing firmware (verified)
- **Two-probe thermal sensing (already implemented).** `Ds18b20` (GPIO42, 1-Wire, 12-bit async
  ~0.75 s) is the **liquid temperature** = PID process value (`NAN` on disconnect). The 10k **NTC**
  (`Thermistor`, Beta) is a **heater-mounted safety high-limit**, polled independently.
- `ThermalController` already runs a **real PID** on the liquid probe (Kp/Ki/Kd, anti-windup) with
  a two-tier safety cutoff: NTC over-temp (`heaterSafetyMaxC` ~80 °C → force-off until cooled) and a
  liquid sanity ceiling (`processMaxC` ~55 °C). Gains are compile-time only. `Reactor`'s telemetry
  already carries `liquidTempC`, `heaterTempC`, and `safetyTripped`.
- `Tmc2209Motor` spins via the chip's `VACTUAL` register over single-wire UART, with
  `setCurrentMilliamps`, `setMicrosteps`, and raw `DRV_STATUS` access. Speed is exposed only as a
  vague "percent of top speed."
- `Thermistor` (heater NTC) converts via the **Beta equation**; no calibration yet.
- `WebInterface` serves unversioned `/api/*` + `/ws`; status JSON is hand-built.
- Persisted config today: WiFi creds only (NVS). `NvsAes` (encrypted NVS) is available for reuse.

## 2. Goals / Non-goals

**Goals**
1. A complete, versioned **`/api/v1` reference** covering every capability — the durable contract.
2. Four new/expanded capabilities, firmware-first, each end-to-end with API + mock support:
   disc-drive **rpm** control, **motor telemetry/diagnostics**, **PID runtime config + autotune**,
   **heater-NTC calibration**.
3. Keep the mock server (`tools/mock_server.py`) in lockstep so the future UI is buildable
   without hardware.

**Non-goals (this effort)**
- Building the new UI (separate, later phase — visual direction recorded in §12 so it isn't lost).
- Authentication (v1 is open on LAN; a token mechanism is reserved — §3).
- Multi-step run profiles/recipes and remote/cloud connectivity (future).

## 3. API conventions

- **Transport:** REST for commands/config (request→response); a single WebSocket `/ws` pushes the
  full telemetry object at `AppConfig::Web::kWsPushPeriodMs` (~4 Hz). On connect, the client
  receives an immediate snapshot.
- **Namespace:** all endpoints under **`/api/v1/`**. The old unversioned `/api/*` routes are
  **removed**. Telemetry carries a top-level `apiVersion` string (`"1.0"`).
- **Access control:** **open on the LAN** for v1 (`Access-Control-Allow-Origin: *`, no auth).
  Reserved for later: an optional `X-Reactor-Token` header gated on write (POST) endpoints; the
  spec keeps endpoint semantics token-ready (writes are already separated from reads).
- **Success envelope:** `2xx` with `{ "ok": true, ... }` (command acks) or the requested resource
  body (GETs). **Error envelope:** appropriate `4xx/5xx` with
  `{ "ok": false, "error": { "code": "<machine_code>", "message": "<human text>" } }`.
- **Naming/units:** camelCase keys with units in the name — `tempC`, `rpm`, `currentMa`,
  `elapsedSec`, `freeHeap`. Booleans are `true/false`; absent/unknown numerics are `null`.
- **Persistence:** a single `ConfigStore` over NVS (reusing `NvsAes`) holds setpoints, PID gains,
  motor params, and heater-NTC calibration. WiFi creds keep their existing namespace.

### Error codes (initial set)
`invalid_request` · `out_of_range` · `busy` (e.g. autotune running) · `not_running` ·
`sd_not_mounted` · `no_log` · `wifi_ssid_required` · `calibration_incomplete` ·
`driver_offline` · `not_supported`.

## 4. Telemetry object — `GET /api/v1/status` and `/ws`

One JSON object, identical on both. Field reference:

```jsonc
{
  "apiVersion": "1.0",
  "uptimeSec": 15128,

  "system": {
    "firmware": "1.2.0",       // string  build version
    "freeHeap": 142000,        // int     bytes
    "vbus": "12V",             // string  negotiated USB-PD profile
    "sdMounted": true          // bool
  },

  "thermal": {
    "tempC": 36.0,             // float|null   DS18B20 LIQUID temp = PID process value; null on fault
    "setpointC": 36.0,         // float
    "errorC": 0.0,             // float        setpoint - temp (null if temp null)
    "heaterPct": 41.0,         // float 0..100 PWM duty
    "fault": false,            // bool         liquid probe (DS18B20) disconnected/faulted
    "pid": {
      "kp": 0.08, "ki": 0.0015, "kd": 0.4,   // float  active gains
      "p": 0.0, "i": 0.31, "d": -0.02,        // float  last term contributions
      "out": 0.41,                            // float 0..1  controller output (duty)
      "mode": "auto",                         // "auto" | "manual" | "autotune"
      "autotune": { "active": false, "progress": 0, "result": null }
                                              // progress 0..100; result null|"ok"|"failed"
    },
    "safety": {                  // heater-mounted NTC high-limit, independent of the PID
      "tripped": false,          // bool       heater force-off by the safety limit
      "heaterTempC": 44.0,       // float|null NTC heater-probe temp; null on probe fault
      "heaterMaxC": 80.0,        // float      NTC over-temp cutoff (heaterSafetyMaxC)
      "processMaxC": 55.0,       // float      liquid sanity ceiling
      "probe": {                 // NTC raw + calibration state — the P4 calibration target
        "adcRaw": 1820,          // int 0..4095   raw ADC (for calibration capture)
        "resistanceOhms": 9120,  // float         computed NTC resistance
        "calibrated": false,     // bool          user calibration applied vs Beta default
        "method": "beta"         // "beta" | "offset" | "steinhart"
      }
    }
  },

  "disc": {                     // the rotating-disc drive (was "agitator/motorPct")
    "running": true,            // bool   shaft turning
    "rpm": 8.0,                 // float  commanded disc rpm
    "rpmSetpoint": 8.0,         // float
    "direction": "cw",          // "cw" | "ccw"
    "currentMa": 600,           // int    RMS coil current
    "microsteps": 16,           // int
    "enabled": true,            // bool   driver outputs enabled
    "load": 142,                // int|null  StallGuard SG_RESULT (relative load index,
                                //           ~0..510, lower = higher load; not a unit)
    "driver": {
      "version": "0x21",        // string  chip version byte
      "connected": true,        // bool    UART link OK
      "flags": {                // decoded from DRV_STATUS
        "otpw": false,          // over-temperature prewarning
        "ot": false,            // over-temperature shutdown
        "stall": false,         // StallGuard stall
        "openLoadA": false, "openLoadB": false,
        "shortA": false, "shortB": false
      }
    }
  },

  "run": {
    "active": true,             // bool
    "elapsedSec": 2882,         // int
    "remainingSec": 4318,       // int|null  null when durationMin == 0 (indefinite)
    "durationMin": 120          // int       0 = run until stopped
  },

  "wifi": {
    "mode": "sta",              // "sta" | "ap"
    "connected": true,          // bool
    "ssid": "LAB-NET-5G",       // string
    "ip": "192.168.1.42",       // string
    "rssi": -54                 // int|null  dBm (null in AP mode)
  },

  "storage": {
    "sdMounted": true,          // bool
    "logBytes": 184320,         // int|null  current CSV size
    "logging": true             // bool
  },

  "alarms": [                   // active alarms; empty when clear
    { "code": "safety_tripped", "severity": "critical", "since": 15010 }
  ]
}
```

**Accuracy notes**
- TMC2209 reports **no numeric driver temperature** — only `otpw`/`ot` flags, exposed as booleans.
- `disc.load` is StallGuard's relative `SG_RESULT`, surfaced as a load *index* with that caveat,
  not a physical unit. Useful as a biofilm-growth/jam trend, not an absolute measurement.
- `thermal.tempC` is the **DS18B20 liquid** (the controlled value). The **NTC** appears only under
  `thermal.safety` — it is a heater-element high-limit, not the process measurement.

## 5. REST endpoint reference (`/api/v1`)

### Status
- **`GET /status`** → the §4 object. (Also pushed on `/ws`.)

### Run / process
- **`POST /run`** — start/stop a run.
  Body: `{ "action": "start"|"stop", "targetC"?: float, "rpm"?: float, "durationMin"?: int }`.
  On `start`, omitted fields fall back to persisted defaults (36 °C, 8 rpm, 0). → `{ "ok": true }`.
  Errors: `out_of_range` (rpm 0.5–30, targetC 0–`processMaxC` ≈ 55).
- **`POST /setpoint`** — live changes without restarting the run.
  Body: `{ "targetC"?: float, "rpm"?: float }`. → `{ "ok": true }`. (`rpm` here is the same
  setpoint as `POST /disc`'s `rpm`; `/setpoint` is the process-oriented alias, `/disc` also
  covers drive params.)

### Disc drive
- **`POST /disc`** — drive parameters.
  Body: `{ "rpm"?: float, "currentMa"?: int(100..1500), "microsteps"?: int{1,2,4,8,16,32,64,128,256},
  "direction"?: "cw"|"ccw", "enabled"?: bool }`. Persists `currentMa`/`microsteps`/`direction`.
  → `{ "ok": true }`. Errors: `out_of_range`, `driver_offline`.

### Thermal / PID
- **`GET /pid`** → `{ "kp", "ki", "kd", "mode", "autotune": {...} }`.
- **`POST /pid`** — `{ "kp"?, "ki"?, "kd"?, "mode"?: "auto"|"manual" }`. Persists gains. `manual`
  holds the last output (heater frozen at current duty) for bump tests. → `{ "ok": true }`.
- **`POST /pid/autotune`** — `{ "action": "start"|"cancel" }`. `start` runs a relay
  (Åström-Hägglund) autotune around the current setpoint. → `{ "ok": true }`. Error: `busy`.
- **`GET /pid/autotune`** → `{ "active": bool, "progress": 0..100, "result": null|"ok"|"failed",
  "suggested": { "kp", "ki", "kd" }|null }`. Suggested gains are applied+persisted on success.

### Heater-NTC calibration
Targets the **heater-mounted NTC safety probe** (makes the ~80 °C cutoff accurate). The DS18B20
liquid probe is trusted as-is (factory ±0.5 °C) and is **not** calibrated.
- **`GET /calibration`** → `{ "method", "coefficients": {...}, "points": [ { "referenceC", "resistanceOhms" } ] }`.
- **`POST /calibration/point`** — `{ "referenceC": float }`. Captures the *current* NTC resistance
  paired with the operator's reference-thermometer reading at the heater. → `{ "ok": true, "points": N }`.
- **`POST /calibration/compute`** — fits and persists: 1 point → offset on Beta; 2 → Beta refit;
  ≥3 → Steinhart-Hart (A,B,C). → `{ "ok": true, "method", "coefficients" }`. Error: `calibration_incomplete`.
- **`POST /calibration/reset`** — back to factory Beta. → `{ "ok": true }`.

### Storage / SD
- **`GET /log`** — streams `reactor_log.csv` (`text/csv`). Error: `no_log` / `sd_not_mounted`.
- **`POST /log/clear`** — rotate/clear the log. → `{ "ok": true }`.
- **`GET /sd`** → `{ "mounted", "logBytes", "totalBytes", "usedBytes" }`.
- **`POST /sd/format`** — **new**. Reformats the card (FAT). Destroys all data; requires
  `{ "confirm": true }`. → `{ "ok": true }`. Errors: `sd_not_mounted`, `invalid_request`.

### WiFi
- **`GET /wifi`** → the `wifi` telemetry block.
- **`GET /wifi/scan`** → `{ "scanning": bool, "networks": [ { "ssid", "rssi", "secure" } ] }`
  (triggers a fresh async scan; poll until `scanning:false`).
- **`POST /wifi/connect`** — `{ "ssid", "password"? }`. Error: `wifi_ssid_required`.
- **`POST /wifi/forget`** — clears creds, returns to setup AP. → `{ "ok": true }`.

### System
- **`GET /system`** → `{ "firmware", "freeHeap", "uptimeSec", "board", "resetReason" }`.
- **`POST /system/restart`** — reboots after the response flushes. → `{ "ok": true }`.

## 6. CSV log format

The header changes to match rpm + telemetry (a logged-format bump; the UI's download is unaffected):

```
t_ms,running,liquid_c,heater_c,setpoint_c,heater_pct,rpm,load,fault,safety
```

`liquid_c` = DS18B20 process value, `heater_c` = NTC safety probe, `safety` = trip flag (0/1).
The full header lands in **P1**; the `load` column is written empty/`0` until **P2** populates
StallGuard, avoiding a second format bump.

## 7. Firmware architecture changes

- **`Tmc2209Motor`** — add `setRpm(float rpm)` converting rpm→VACTUAL using
  `stepsPerRev * microsteps` (direct 1:1 drivetrain; `8 rpm ≈ 427 µsteps/s ≈ VACTUAL 597`).
  Add `loadSGResult()` and a `Status drvStatusDecoded()` returning the flags struct. Add
  `stepsPerRev` (default 200) to `Config`.
- **`ThermalController`** — already does liquid-PID + two-tier safety. Add `setGains/gains`,
  `setMode/mode` (`auto|manual|autotune`), P/I/D term getters, and an `Autotune` helper (relay
  method on the **liquid** PV; computes `Ku`, `Tu` → Tyreus-Luyben gains). Load gains from
  `ConfigStore` at `begin()`.
- **`Ds18b20` (liquid)** — already implemented; only needs exposing through telemetry. Not calibrated.
- **`Thermistor` (heater NTC)** — add a calibration model: `method ∈ {beta,offset,steinhart}` with
  stored coefficients; `readResistanceOhms()` / `readRawAdc()` for capture; persist via `ConfigStore`.
  Serves the safety high-limit, not the process value.
- **`ConfigStore` (new)** — typed get/set over NVS (`NvsAes`), namespace `reactor`/`cal`:
  setpointC, rpm, durationMin defaults, PID gains, motor currentMa/microsteps/direction,
  calibration coefficients+points.
- **`Reactor`** — `start(targetC, rpm, durationMin)` / `setRpm` replace the percent API;
  expose the richer disc + thermal state.
- **`WebInterface`** — register `/api/v1/*`, build the §4 JSON, standard error envelope,
  `/sd/format`, `/disc`, `/pid*`, `/calibration*`; aggregate `alarms[]`.
- **`AppRuntime`** — wire `ConfigStore`; default operating point 36 °C / 8 rpm.
- **Alarms** — derived each control tick from: `sensor_fault` (liquid DS18B20),
  `heater_probe_fault` (NTC), `safety_tripped` (heater force-off — critical), `liquid_high`
  (near `processMaxC`), `driver_ot`/`driver_otpw`/`driver_stall`/`driver_open_load`, `sd_missing`,
  `wifi_lost`, `autotune_failed`. Each: `{ code, severity: info|warn|critical, since }`.

## 8. Autotune (relay method) — summary

With the loop in `autotune` mode, drive the heater as a relay (full/zero duty) bracketing the
setpoint. Measure steady oscillation amplitude `a` and period `Tu`; ultimate gain
`Ku = 4d / (π a)` (`d` = relay half-amplitude). Derive gains (Tyreus-Luyben for a slow thermal
bath: `Kp = 0.45 Ku`, `Ki = Kp / (2.2 Tu)`, `Kd = Kp·Tu/6.3`). `progress` tracks observed cycles
(needs ~3–4); `result:"failed"` if no clean oscillation within a timeout. On success, gains are
applied + persisted; on failure, the prior gains are restored.

## 9. Heater-NTC calibration — summary

Calibrates the **heater-mounted NTC** (the safety probe), so the over-temp cutoff is accurate; the
DS18B20 liquid probe is trusted at factory accuracy. Operator captures (reference °C, measured Ω)
points near the safety range. `compute` fits by point count: 1 → constant offset on the Beta curve;
2 → refit Beta (`R0`,`Beta`); ≥3 → solve Steinhart-Hart `1/T = A + B·ln(R) + C·ln(R)³`. Coefficients
+ points persist in NVS and survive reboot; `reset` reverts to factory Beta constants.

## 10. Build phasing (firmware-first, vertical)

Each phase ships firmware + the matching `/api/v1` slice + an updated `mock_server.py`.

- **P1 — Disc drive rpm control:** `setRpm`, `ConfigStore`, `/disc`, `/run`+`/setpoint` use rpm,
  `disc` telemetry (rpm/current/microsteps/direction/enabled), CSV header bump, rename throughout.
- **P2 — Motor telemetry & diagnostics:** `SG_RESULT` load, `DRV_STATUS` flag decode,
  `disc.driver`, alarm sources, mock simulation of load/faults.
- **P3 — PID config + autotune:** runtime gains + mode + persistence, relay autotune,
  `/pid` + `/pid/autotune`, `thermal.pid` telemetry.
- **P4 — Heater-NTC calibration:** calibration model + capture/compute/reset on the safety NTC,
  `/calibration*`, `thermal.safety.probe` telemetry. (DS18B20 liquid left at factory accuracy.)

## 11. Mock server

`tools/mock_server.py` mirrors `/api/v1` and the §4 object, simulating: the liquid first-order
thermal model (already present) + a synthetic NTC heater-probe and safety-trip state, rpm + a
synthetic StallGuard `load` that drifts with a "biofilm" term, occasional driver flags, and
calibration/autotune state machines — so the future UI is fully exercisable.

## 12. Later: UI phase (recorded direction)

When the UI is rebuilt against this reference, these validated decisions apply:
- **Light theme**, simple, "prosumer lab tool" feel. Accent: **water blue `#1C9CD8`**.
- Accent font **Doto** (Google Fonts) for the wordmark + numeric readouts only; **Inter** for
  body/labels.
- **Shell:** top bar (brand + live status chips) with **segmented tabs**.
- **Three sections:** **Monitor** (read-only live dashboard — liquid temp + trend chart, a heater
  safety-probe indicator, disc rpm + load, heater duty, run state, alarms), **Run** (target temp /
  target rpm / duration, start-stop), **Settings** (WiFi rebuilt, SD manage/format, PID tuning +
  autotune, heater-NTC calibration, motor config, system). Monitor layout was prototyped and approved.

## 13. Open items
- Auth **token** mechanism (write endpoints) — deferred; semantics kept token-ready.
- Run **profiles/recipes** (multi-stage temp/rpm over time) — future.
