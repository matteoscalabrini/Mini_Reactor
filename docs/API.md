# Mini-Reactor API Reference (`/api/v1`)

**As-built — Phases 1–3.** This documents the endpoints the firmware actually serves today
(merged to `main`, build `1.0.0`). Endpoints planned for later phases are listed at the
end under [Planned](#planned-not-yet-implemented) and are **not** callable yet.

For the full design rationale and the future contract, see
`docs/superpowers/specs/2026-06-15-reactor-firmware-api-design.md`.

## Conventions

- **Base URL:** `http://<device-ip>/api/v1` (HTTP, port 80). Telemetry also streams over `ws://<device-ip>/ws`.
- **Transport:** REST for commands/config; one WebSocket (`/ws`) pushes the full telemetry object ~4 Hz. On connect, the WS sends an immediate snapshot.
- **Access control:** open on the LAN — no auth (`Access-Control-Allow-Origin: *`).
- **Content type:** request bodies are JSON; responses are JSON (except `GET /log`, which is CSV).
- **Units in field names:** `tempC`, `rpm`, `currentMa`, `elapsedSec`, `freeHeap`. `null` = unknown/absent.
- **Success:** `2xx` with `{ "ok": true }` (command acks) or the resource body (GETs).
- **Error:** `4xx/5xx` with `{ "ok": false, "error": { "code": "<machine_code>", "message": "<text>" } }`.

### Error codes (Phase 1)

`invalid_request` · `out_of_range` · `wifi_ssid_required` · `no_log` · `not_found`

---

## Telemetry object

Returned by `GET /api/v1/status` and pushed on `/ws` (identical). Fields:

```jsonc
{
  "apiVersion": "1.0",
  "uptimeSec": 15128,

  "system":  { "firmware": "1.0.0", "freeHeap": 142000, "vbus": "12V", "sdMounted": true },

  "thermal": {
    "tempC": 36.0,            // DS18B20 LIQUID temp = PID process value; null on probe fault
    "setpointC": 36.0,
    "errorC": 0.0,            // setpoint - temp; null when temp is null
    "heaterPct": 41.0,       // 0..100 PWM duty
    "fault": false,          // liquid (DS18B20) probe disconnected
    "safety": {              // heater-mounted NTC high-limit, independent of the PID
      "tripped": false,      // heater force-off by the safety limit
      "heaterTempC": 44.0,   // NTC heater-probe temp; null on probe fault
      "heaterMaxC": 80.0,    // over-temp cutoff
      "processMaxC": 55.0    // liquid sanity ceiling
    },
    "pid": {                 // bath PID controller
      "kp": 0.08, "ki": 0.0015, "kd": 0.4,   // active gains
      "p": 0.0, "i": 0.31, "d": -0.02,        // last term contributions
      "out": 0.41,                            // 0..1 controller output (duty)
      "mode": "auto",                         // "auto" | "manual" | "autotune"
      "autotune": { "active": false, "progress": 0, "result": null }  // result null|"ok"|"failed"
    }
  },

  "disc": {                  // the rotating-disc drive
    "running": true,
    "rpm": 8.0,              // commanded disc rpm (0 when stopped)
    "rpmSetpoint": 8.0,
    "direction": "cw",       // "cw" | "ccw"
    "currentMa": 600,        // RMS coil current
    "microsteps": 16,
    "enabled": true,
    "load": 142,             // StallGuard SG_RESULT load index; null when stopped/offline
    "driver": {              // TMC2209
      "version": "0x21",
      "connected": true,
      "flags": { "otpw": false, "ot": false, "stall": false,
                 "openLoadA": false, "openLoadB": false, "shortA": false, "shortB": false }
    }
  },

  "run":     { "active": true, "elapsedSec": 2882, "remainingSec": 4318, "durationMin": 120 },
                                                          // remainingSec null when durationMin == 0
  "wifi":    { "mode": "sta", "connected": true, "ssid": "LAB-NET-5G",
               "ip": "192.168.1.42", "rssi": -54 },       // rssi null in AP mode
  "storage": { "sdMounted": true, "logBytes": null, "logging": true },
  "alarms":  [ ]            // active alarms; e.g. { "code": "safety_tripped", "severity": "critical", "since": 15010 }
}
```

**Alarm codes:** `sensor_fault` (liquid probe, `warn`), `heater_probe_fault` (NTC, `warn`),
`safety_tripped` (heater force-off, `critical`), `driver_ot` (`critical`), `driver_otpw` (`warn`),
`driver_stall` (`warn`), `driver_open_load` (`warn`). Each alarm:
`{ "code", "severity": "info"|"warn"|"critical", "since": <uptimeSec when first raised> }`.
`since` is set when the alarm first appears and held while it stays active.

---

## Endpoints

### Status / telemetry

#### `GET /api/v1/status`
Returns the [telemetry object](#telemetry-object).

#### `WS /ws`
WebSocket. Pushes the telemetry object on connect and every ~250 ms thereafter. Read-only (the
server ignores inbound messages).

### Run / process

#### `POST /api/v1/run`
Start or stop a run.

Body:
```jsonc
{ "action": "start" | "stop",
  "targetC": 36.0,      // start only, optional (default 36); rejected if <0 or >55
  "rpm": 8.0,           // start only, optional (default 8); rejected if <0 or >30; 0 = disc stopped
  "durationMin": 120 }  // start only, optional (default 0 = run until stopped)
```
- `200 { "ok": true }`
- `400 out_of_range` — `rpm` outside 0..30 or `targetC` outside 0..55
- `400 invalid_request` — `action` not `start`/`stop`

> rpm is additionally clamped to **[0.5, 30]** internally (values 0 < rpm < 0.5 become 0.5; `rpm: 0` keeps the disc stopped).

#### `POST /api/v1/setpoint`
Live setpoint changes without restarting the run. Both fields optional.

Body: `{ "targetC": 36.0, "rpm": 8.0 }`
- `200 { "ok": true }`
- `400 out_of_range` — `targetC` outside 0..55

> `rpm` here is the same setpoint as `/disc`'s `rpm` and is clamped (not rejected) to [0.5, 30] / 0.

### Disc drive

#### `POST /api/v1/disc`
Set drive parameters. All fields optional; only those present are applied. `currentMa`/`microsteps`/`direction` persist to NVS.

Body:
```jsonc
{ "rpm": 12.0,            // clamped to [0.5,30] / 0
  "currentMa": 600,       // RMS coil current; clamped to 100..1500
  "microsteps": 16,       // 1,2,4,8,16,32,64,128,256
  "direction": "cw",      // "cw" | "ccw"
  "enabled": true }       // false = de-energise + stop the driver
```
- `200 { "ok": true }`

### Thermal / PID

#### `POST /api/v1/pid`
Set PID gains and/or control mode. To change gains, send `kp`, `ki`, `kd` **together** (all three are required as a set); they persist to NVS. `mode` is independent of gains.

Body: `{ "kp": 0.08, "ki": 0.0015, "kd": 0.4, "mode": "auto" }`  ·  `mode`: `"auto"` | `"manual"`
- `200 { "ok": true }`

> `manual` freezes the heater at its last duty. The active gains + live terms are in `thermal.pid` (read from `/status`).

#### `POST /api/v1/pid/autotune`
Start or cancel a relay (Åström-Hägglund) autotune around the current setpoint. On success it applies + persists the tuned gains and returns to `auto`; live progress/result are in `thermal.pid.autotune`.

Body: `{ "action": "start" | "cancel" }`
- `200 { "ok": true }`
- `400 invalid_request` — `action` not `start`/`cancel`

> There are no `GET /pid` / `GET /pid/autotune` endpoints — the same data is in `/status` `thermal.pid`.

### WiFi

#### `GET /api/v1/wifi/scan`
Returns cached scan results and triggers a fresh async scan. Poll until `scanning` is `false`.
```jsonc
{ "scanning": false, "networks": [ { "ssid": "LAB-NET-5G", "rssi": -42, "secure": true } ] }
```

#### `POST /api/v1/wifi/connect`
Body: `{ "ssid": "LAB-NET-5G", "password": "…" }` (password optional for open networks)
- `200 { "ok": true }`
- `400 wifi_ssid_required` — empty/missing `ssid`

#### `POST /api/v1/wifi/forget`
Clears stored credentials and returns to the setup AP. `200 { "ok": true }`.

### Storage / SD

#### `GET /api/v1/log`
Streams the telemetry CSV (`text/csv`). Header:
```
t_ms,running,liquid_c,heater_c,setpoint_c,heater_pct,rpm,load,fault,safety
```
(`load` is the StallGuard SG_RESULT while running, else `0`.)
- `200` CSV body
- `503 no_log` — no SD card mounted or no log file

#### `POST /api/v1/log/clear`
Rotates/clears the SD log. `200 { "ok": true }`.

---

## Notes for clients (UI)

- Develop against the **mock server** (no hardware): `python tools/mock_server.py` → `http://localhost:8000`. It mirrors this contract exactly, including the error envelopes and rpm clamping, and simulates the thermal model + fault branches.
- Prefer the **WebSocket** for live values; use `GET /status` for an initial snapshot or polling fallback.
- Treat any `null` numeric as "unknown" (probe fault, AP mode, no duration), not zero.

---

## Planned (not yet implemented)

These are specified in the design doc but **return `404 not_found` today**. They arrive in later phases:

| Phase | Endpoints / telemetry additions |
|---|---|
| **P4** — NTC calibration | `GET /api/v1/calibration`, `POST /api/v1/calibration/{point,compute,reset}`; `thermal.safety.probe` telemetry |
| **SD / system** | `GET /api/v1/sd`, `POST /api/v1/sd/format`, `GET /api/v1/system`, `POST /api/v1/system/restart`; `storage.logBytes` |
