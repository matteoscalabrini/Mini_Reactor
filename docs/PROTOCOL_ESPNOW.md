# Mini-Reactor ESP-NOW Link Protocol (`synclink` v1)

Binary wire protocol for the reactor ↔ HUB ESP-NOW link.
Authoritative source: [`include/sync/SyncFrames.hpp`](../include/sync/SyncFrames.hpp).
All frames are packed little-endian and fit within one 250-byte ESP-NOW packet.
C++ namespace: `synclink`.

---

## Protocol version

`kProtocolVersion = 1`

---

## Message types (`MsgType`)

| Value | Name | Direction | Purpose |
|-------|------|-----------|---------|
| 1 | `Telemetry` | Reactor → HUB | Live reactor state at ~4 Hz |
| 2 | `Command` | HUB → Reactor | Control action (start/stop/setpoint/disc/pause/resume) |
| 3 | `Ack` | Reactor → HUB | Command acknowledgement |
| 4 | `PairRequest` | HUB → Reactor | Pairing handshake initiation |
| 5 | `PairAck` | Reactor → HUB | Pairing handshake response |

---

## Frame header (`Header`) — 4 bytes

| Field | Type | Description |
|-------|------|-------------|
| `version` | `u8` | `kProtocolVersion` (1) |
| `msgType` | `u8` | `MsgType` value |
| `seq` | `u16` | Monotonic sequence number |

---

## Telemetry frame (~74 bytes)

Sent by the reactor at ~4 Hz to the paired HUB. A compact binary projection of the
reactor's full status document. See also: [`API.md`](../API.md).

| Field | Type | Scale / sentinel | Physical meaning |
|-------|------|-----------------|-----------------|
| `hdr` | `Header` | — | MsgType = Telemetry |
| `uptimeSec` | `u32` | — | Seconds since boot |
| `flags` | `u16` | see flag bits below | Run/motor/fault state bitfield |
| `tempC_c` | `i16` | ×100; `kNullI16` = fault/null | Liquid temperature (°C) |
| `setpointC_c` | `i16` | ×100 | Target temperature (°C) |
| `heaterPct_h` | `u8` | ×2 (0..200 → 0..100%) | Heater duty cycle |
| `heaterTempC_d` | `i16` | ×10; `kNullI16` = null | Heater NTC temperature (°C) |
| `processMaxC_d` | `i16` | ×10 | Safety process-max threshold (°C) |
| `rpm_c` | `u16` | ×100 | Disc actual rpm |
| `rpmSetpoint_c` | `u16` | ×100 | Disc setpoint rpm |
| `load` | `i16` | `kNullI16` = null/idle | StallGuard load reading |
| `driverFlags` | `u8` | — | TMC2209 status (ot/otpw/stall/openload/connected) |
| `runId` | `u16` | 0 = idle | Current run identifier |
| `elapsedSec` | `u32` | — | Elapsed run time (s) |
| `remainingSec` | `i32` | -1 = null (run-until-stopped) | Time remaining (s) |
| `durationMin` | `u16` | — | Configured run duration (min); 0 = unlimited |
| `name` | `char[33]` | NUL-terminated, **reserved** | Run name (v1: blank) |
| `alarmBitmask` | `u16` | — | Active alarm bitmask |
| `worstSeverity` | `u8` | 0=none / 1=warn / 2=critical | Highest active alarm severity |

### Null sentinels

| Sentinel | Applies to |
|----------|-----------|
| `kNullI16 = INT16_MIN` | `tempC_c`, `setpointC_c`, `heaterTempC_d`, `processMaxC_d`, `load` |
| `remainingSec = -1` | run-until-stopped mode |
| `runId = 0` | reactor is idle |

### Fixed-point scales

| Constant | Scale | Type | Range example |
|----------|-------|------|---------------|
| `kScaleTempC = 100` | centi-degrees | `i16` | 3581 → 35.81 °C |
| `kScaleHeaterC = 10` | deci-degrees | `i16` | 482 → 48.2 °C |
| `kScaleRpm = 100` | centi-rpm | `u16` | 800 → 8.00 rpm |
| `kScaleHeaterPct = 2` | half-percent | `u8` | 85 → 42.5% |

### `flags` bit definitions

| Bit | Constant | Meaning |
|-----|----------|---------|
| 0 | `kFlagRunActive` | A run is in progress |
| 1 | `kFlagMotorRunning` | Disc motor is spinning |
| 2 | `kFlagHeaterActive` | Heater output > 0 |
| 3 | `kFlagThermalFault` | Liquid probe fault |
| 4 | `kFlagSafetyTripped` | Heater NTC safety tripped |
| 5 | `kFlagProbeFault` | Heater NTC probe fault |
| 6 | `kFlagSdMounted` | SD card mounted |
| 7 | `kFlagMotorPaused` | Motor paused |
| 8 | `kFlagFullHold` | Full hold (motor + heater paused) |

---

## Command frame (~49 bytes)

Sent by the HUB to the reactor to issue a control action.

| Field | Type | Description |
|-------|------|-------------|
| `hdr` | `Header` | MsgType = Command |
| `opcode` | `u8` | `Opcode` value |
| `presentMask` | `u8` | Field-presence bitmask (Setpoint / Disc) |
| `targetC_c` | `i16` | Target temp ×100 (Setpoint / RunStart) |
| `rpm_c` | `u16` | rpm ×100 (Setpoint / RunStart / Disc) |
| `durationMin` | `u16` | Run duration in minutes (RunStart; 0 = unlimited) |
| `currentMa` | `u16` | Motor current mA (Disc) |
| `microsteps` | `u8` | Microstep divisor (Disc) |
| `flags` | `u8` | Boolean values + pause target (see below) |
| `name` | `char[33]` | Run name, NUL-terminated (RunStart) |

### Opcodes

| Value | Name | Description |
|-------|------|-------------|
| 1 | `RunStart` | Start a run (`targetC_c`, `rpm_c`, `durationMin`, `name`) |
| 2 | `RunStop` | Stop the current run (`kCmdFlagRunStopSave` in `flags` selects save vs discard) |
| 3 | `Setpoint` | Update target temp / rpm mid-run (fields selected by `presentMask`) |
| 4 | `Disc` | Update motor drive parameters (fields selected by `presentMask`) |
| 5 | `DiscTest` | Brief test jog (~8 rpm, 3 s); ignored while a run is active |
| 6 | `Pause` | Pause motor or all (target in `flags` low nibble) |
| 7 | `Resume` | Resume from pause |

### `presentMask` bits — Setpoint opcode

| Bit | Constant | Field present |
|-----|----------|--------------|
| 0 | `kSetHasTargetC` | `targetC_c` is valid |
| 1 | `kSetHasRpm` | `rpm_c` is valid |

### `presentMask` bits — Disc opcode

| Bit | Constant | Field present |
|-----|----------|--------------|
| 0 | `kDiscHasRpm` | `rpm_c` is valid |
| 1 | `kDiscHasCurrent` | `currentMa` is valid |
| 2 | `kDiscHasMicro` | `microsteps` is valid |
| 3 | `kDiscHasDir` | direction from `kCmdFlagDiscReverse` in `flags` |
| 4 | `kDiscHasEnabled` | enabled state from `kCmdFlagDiscEnabled` in `flags` |

### `flags` bit definitions

| Bit | Constant | Meaning |
|-----|----------|---------|
| 0 | `kCmdFlagDiscReverse` | Disc direction: 0=cw, 1=ccw |
| 1 | `kCmdFlagDiscEnabled` | Disc enabled state |
| 2 | `kCmdFlagRunStopSave` | Stop: save=1, discard=0 |

### Pause targets (`flags` low nibble when `Pause`)

| Value | Constant | Target |
|-------|----------|--------|
| 1 | `kPauseTargetMotor` | Pause motor only |
| 2 | `kPauseTargetAll` | Pause motor + heater (full hold) |

---

## Ack frame (9 bytes)

Sent by the reactor in response to every `Command`.

| Field | Type | Description |
|-------|------|-------------|
| `hdr` | `Header` | MsgType = Ack |
| `ackSeq` | `u16` | Echoes `Command.hdr.seq` |
| `opcode` | `u8` | Echoes `Command.opcode` |
| `result` | `u8` | `AckResult` value |
| `errorCode` | `u8` | `AckError` value |

### `AckResult` codes

| Value | Name | Meaning |
|-------|------|---------|
| 0 | `Accepted` | Command queued successfully |
| 1 | `Rejected` | Command rejected (see `errorCode`) |
| 2 | `FeatureDisabled` | Feature toggled off at build time |

### `AckError` codes

| Value | Name | Meaning |
|-------|------|---------|
| 0 | `None` | No error (Accepted) |
| 1 | `InvalidRequest` | Missing or invalid field |
| 2 | `OutOfRange` | Value outside accepted bounds |
| 3 | `NotFound` | Unknown run id or resource |

---

## PairRequest frame (~44 bytes)

Sent by the HUB (broadcast, channel sweep) to initiate pairing.

| Field | Type | Description |
|-------|------|-------------|
| `hdr` | `Header` | MsgType = PairRequest |
| `role` | `u8` | `DeviceRole::Hub` (2) |
| `mac` | `u8[6]` | HUB MAC address |
| `name` | `char[33]` | HUB name, NUL-terminated |

---

## PairAck frame (~45 bytes)

Sent by the reactor to accept a pairing request.

| Field | Type | Description |
|-------|------|-------------|
| `hdr` | `Header` | MsgType = PairAck |
| `role` | `u8` | `DeviceRole::Reactor` (1) |
| `mac` | `u8[6]` | Reactor MAC address |
| `channel` | `u8` | Reactor's current WiFi channel |
| `name` | `char[33]` | Reactor name, NUL-terminated |

---

## Pairing and channel-sweep sequence

Pairing is a mutual two-button action: the user must trigger both sides within the 60 s window.

1. **Open reactor window** — user presses "Pair HUB" in the reactor web Settings tab (or `POST /api/v1/espnow/pair`). The reactor opens a 60 s acceptance window; it will accept the next `PairRequest` it receives.
2. **HUB initiates sweep** — user taps the PAIR tile on the HUB screen. The HUB (WiFi-less) does not know the reactor's WiFi channel. It iterates over channels 1..13, briefly sending broadcast `PairRequest` frames on each, listening for a `PairAck`.
3. **Reactor responds** — when the reactor receives a `PairRequest` during the open window, it registers the HUB's MAC as an ESP-NOW peer and sends a `PairAck` containing its current WiFi channel.
4. **HUB locks channel** — the HUB receives the `PairAck`, records the reactor's MAC and channel to NVS, and switches to that fixed channel.
5. **Telemetry flow begins** — the reactor adds the HUB as an ESP-NOW peer and starts pushing `Telemetry` frames at ~4 Hz.
6. **Reboot persistence** — on reboot the HUB reads the saved MAC + channel from NVS and reconnects directly without re-sweeping.
7. **Forget** — `POST /api/v1/espnow/forget` on the reactor clears the binding. The HUB detects the loss, shows DISCONNECTED, and re-sweeps to find the reactor again.

---

## v1 simplifications

The following are intentional constraints in the v1 implementation:

- **Single peer** — only one reactor is supported. The reactor tracks one HUB MAC; the HUB tracks one reactor MAC.
- **No payload encryption** — ESP-NOW frames are sent in plaintext. The link is intended for LAN/lab use only.
- **`load`, `runId`, `name` reserved** — the `load` (StallGuard raw value), `runId`, and `name` fields in the `Telemetry` frame are transmitted but not yet populated by the reactor's telemetry builder. Receivers should treat them as informational placeholders in v1.

---

## Manual on-device validation (pending)

The toggle-matrix build gate has passed (see Task 7 build report). The steps below must be performed on real hardware before declaring Phase 2 complete. They have **not** been executed in CI — hardware flashing is not available in the automated build environment.

1. Flash reactor + HUB firmware. HUB should boot and display "Not paired".
2. Tap **PAIR** on the HUB screen (→ "Searching... ch N"). Then press **"Pair HUB"** in the reactor web Settings tab (or `curl -X POST http://<reactor-ip>/api/v1/espnow/pair`).
3. HUB should transition to **Paired**; telemetry updates at ~4 Hz; temp/setpoint/rpm values match the reactor web UI.
4. Reboot the HUB → it should reconnect on the saved channel without re-sweeping.
5. From the HUB, send **Stop** and **Pause** → reactor reacts (serial prints `[CMD] ...`), telemetry reflects the new state, HUB log shows the Ack.
6. Run `curl -X POST http://<reactor-ip>/api/v1/espnow/forget` → HUB shows **DISCONNECTED** then re-sweeps.
7. Build both products with `kEnableEspNow = false` → boot logs show `[FEAT] espnow: disabled`; `POST /api/v1/espnow/pair` returns HTTP 503 `feature_disabled`.
