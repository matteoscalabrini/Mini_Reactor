# HUB Companion Device — Phase 2: ESP-NOW Peer Link + Binding — Design

**Date:** 2026-06-30
**Branch:** `feature/hub-espnow-link` (off `main`)
**Roadmap item:** **HUB (companion device)** → "Phase 2 — ESP-NOW peer link + reactor binding";
realizes the Platform-Services / Backlog item "ESP-NOW (multi-reactor) sync + binding".
**Spans both firmwares:** reactor (`[env:esp32-s3-devkitc-1]`) **and** HUB (`[env:hub]`).
**Porting reference:** `../Barebone` `src/network/SyncRuntime.inl` + `include/network/sync/*`
(working ESP-NOW transport, packed-struct protocol, button pairing, NVS persistence).
**Conformity drivers:** `Claude.md` → Modularity Requirements (reusable services separable
from product logic), Toggle Behavior Contract, API/UI Consistency, Tooling Command Rule,
Partition Change Rule, YAGNI / incremental.

## Summary

Add a **bound, bidirectional ESP-NOW link** so the HUB monitors and controls **one**
reactor. The reactor broadcasts compact telemetry; the HUB renders it; the HUB sends
control commands back; a deliberate two-button pairing step binds the pair. The reactor
has **no** ESP-NOW today, so this adds a **responder** to the reactor and a **client** to
the HUB, on top of a **shared reusable transport service**.

Scope, locked during brainstorming:
- **Telemetry + control, ONE reactor.** (Multi-reactor is a later phase.)
- **Command set:** run start/stop, live setpoint, disc params, disc test, **pause/resume**
  (the last reusing control already present in `control/Reactor`).
- **HUB is WiFi-less, ESP-NOW only** — lowest power, no credentials. It discovers the
  reactor's radio channel by sweeping during pairing and persists it.
- **Lightweight command Ack** (`accepted | rejected | feature_disabled`).
- Transport lives under a **new `src/sync/`** platform-service path, shared by both products.

## Goals

- A reusable `src/sync/EspNowLink` transport (init, peer/channel, send with resend,
  ISR-recv → queue → drain, dedup, binding) compiled **byte-identically into both products**.
- A pure, host-testable codec (`SyncCodec`) for the packed frames — no Arduino deps.
- Reactor **responder**: encode telemetry from live state, apply incoming commands through
  the **existing pending-command path** (identical semantics to REST), reply with Ack, own
  the pairing window (`POST /api/v1/espnow/pair`).
- HUB **client**: decode telemetry → drive a minimal Unpaired / Searching / Paired UI,
  send commands on touch, manage channel discovery + link-loss.
- Feature-toggle gated on **each** product (`kEnableEspNow`); disabled ⇒ no init, no radio,
  explicit log, `503 feature_disabled` on the reactor pair endpoint.
- Reactor product remains functionally unchanged when ESP-NOW is disabled.

## Non-goals (YAGNI — deferred)

- **No multi-reactor / mesh / topology sharing.** One bound peer per device. (Barebone's
  `PeerShare`/`PeerRemove` mesh machinery is intentionally **not** ported.)
- **No payload encryption** in v1 (security = physical proximity + mutual deliberate
  pairing). ESP-NOW LMK/PMK noted as a future hardening step.
- **No HUB WiFi** (no STA, no web/LAN endpoint on the HUB).
- **No full fermentation UI** — the HUB gets only the minimal pairing + telemetry screens
  needed to prove the link. The redesigned UI is Phase 3.
- **No reactor control-logic changes** beyond wiring pause/resume opcodes to the existing
  `setMotorPaused()` / `setFullHold()` and surfacing the flags. (Optionally also closing the
  REST pause/resume gap — cheap, but kept out of the critical path.)

## Architecture

### Two-product shared service

```
include/sync/SyncFrames.hpp          packed structs, opcodes, msg/role enums, flag bits
include/sync/SyncCodec.hpp           pure encode/decode + fixed-point scaling (no Arduino)
src/sync/SyncCodec.cpp
include/sync/EspNowLink.hpp          transport: init, peer/channel, send (3× resend),
src/sync/EspNowLink.cpp              ISR-recv → FreeRTOS queue → drain, dedup ring, binding
```
- `build_src_filter` adds `src/sync/**` to **both** `[env:esp32-s3-devkitc-1]` and `[env:hub]`.
- `EspNowLink` is **product-agnostic**: it knows frames, peers, channels, NVS binding — not
  reactor or HUB semantics. Each product supplies callbacks (`onTelemetry`, `onCommand`,
  `onAck`, `onPaired`).

### Product adapters (thin, product-specific)

- **Reactor responder** — `src/net/EspNowResponder.{hpp,cpp}` (platform-adjacent; pairs with
  `WebInterface`), wired from `AppRuntime`. Builds `Telemetry` from the same live state the
  status doc uses; routes decoded `Command`s into the **existing pending-command queue**
  (`applyPending()` path) so ESP-NOW and REST share one validation/apply path; emits `Ack`.
- **HUB client** — extends `HubRuntime`: owns an `EspNowLink`, decodes telemetry into a UI
  snapshot, runs the channel sweep when unpaired, sends `Command`s on touch input.

### Reuse of the reactor's pending-command path

Mutating REST endpoints validate, queue a pending command under a mutex, and apply it in the
loop (`applyPending()`, echoed as `[CMD] …`). The responder feeds the **same** queue, so an
ESP-NOW `SETPOINT` and a `POST /api/v1/setpoint` are indistinguishable downstream. The exact
shared enqueue signature is an implementation detail resolved in planning; the design
requirement is **one apply path, not two**.

## Protocol (`SyncFrames.hpp`)

Little-endian, `__attribute__((packed))`. `version = 1`; mismatched versions are dropped.

```c
enum class MsgType  : uint8_t { Telemetry=1, Command=2, Ack=3, PairRequest=4, PairAck=5 };
enum class DeviceRole : uint8_t { Reactor=1, Hub=2 };

struct Header { uint8_t version; uint8_t msgType; uint16_t seq; };   // 4 B
```

### Telemetry (reactor → HUB, ~80 B)

| Field | Type | Enc | Notes |
|---|---|---|---|
| hdr | Header | — | msgType=Telemetry |
| uptimeSec | u32 | — | |
| flags | u16 | bits | run/motorRun/motorEn/heater/thermalFault/safetyTripped/probeFault/sdMounted/logging/pidAuto/autotuneActive/**motorPaused**/**fullHold** |
| tempC | i16 | ×100 | `INT16_MIN` = fault/null |
| setpointC | i16 | ×100 | |
| heaterPct | u8 | ×2 | 0..200 ⇒ 0..100 % |
| heaterTempC | i16 | ×10 | safety NTC; `INT16_MIN` = null |
| processMaxC, heaterMaxC | i16×2 | ×10 | safety limits |
| rpm, rpmSetpoint | u16×2 | ×100 | |
| currentMa | u16 | — | |
| microsteps | u8 | — | |
| load | i16 | — | StallGuard; `INT16_MIN` = null |
| driverFlags | u8 | bits | ot/otpw/stall/olA/olB/shA/shB/connected |
| runId | u16 | — | 0 = idle |
| elapsedSec | u32 | — | |
| remainingSec | i32 | — | −1 = until-stopped/null |
| durationMin | u16 | — | |
| name | char[33] | — | NUL-terminated session name |
| alarmBitmask | u16 | bits | sensor_fault/heater_probe_fault/safety_tripped/driver_ot/driver_otpw/driver_stall/driver_open_load |
| worstSeverity | u8 | — | 0 none · 1 warn · 2 critical |
| autotuneProgress | u8 | — | 0..100 |

### Command (HUB → reactor)

```c
enum class Opcode : uint8_t {
  RunStart=1, RunStop=2, Setpoint=3, Disc=4, DiscTest=5, Pause=6, Resume=7 };
struct Command { Header hdr; uint8_t opcode; uint8_t args[/*opcode-specific, ≤40*/]; };
```
- **RunStart**: `targetC(i16×100), rpm(u16×100), durationMin(u16), name[33]`.
- **RunStop**: `save(u8)` (1=save, 0=discard).
- **Setpoint**: `presentMask(u8) + targetC(i16×100) + rpm(u16×100)` (mask says which apply).
- **Disc**: `presentMask(u8) + rpm + currentMa + microsteps + dir + enabled`.
- **DiscTest**: no args (ignored while a run is active — same rule as REST).
- **Pause**: `target(u8)` — `1 = motor` ⇒ `setMotorPaused(true)` (B1: disc off, heater on);
  `2 = all` ⇒ `setFullHold(true)` (B2: disc + heater off).
- **Resume**: no args ⇒ clears both holds.

### Ack (reactor → HUB)

```c
enum class AckResult : uint8_t { Accepted=0, Rejected=1, FeatureDisabled=2 };
struct Ack { Header hdr; uint16_t ackSeq; uint8_t opcode; uint8_t result; uint8_t errorCode; };
```
`ackSeq` echoes the command's `seq`; `errorCode` maps to the REST codes
(`invalid_request`, `out_of_range`, `not_found`, `feature_disabled`).

### PairRequest / PairAck

```c
struct PairRequest { Header hdr; uint8_t role; uint8_t mac[6]; char name[33]; }; // role=Hub
struct PairAck     { Header hdr; uint8_t role; uint8_t mac[6]; uint8_t channel; char name[33]; };
```
`PairAck.channel` is the reactor's current `WiFi.channel()`; the HUB locks to it.

## Pairing, binding & channel discovery

Mutual, deliberate (Bluetooth-style) — both ends must opt in within the window:

1. **Reactor:** `POST /api/v1/espnow/pair` (button on the web **Settings** page) opens a
   **60 s** pairing window; the reactor answers `PairRequest`s on its current WiFi channel.
2. **HUB:** the on-screen **Pair** button (Unpaired UI) starts a **channel sweep** — for each
   channel `1..N` (region max), `esp_wifi_set_channel(ch)`, broadcast `PairRequest`, listen
   briefly for a `PairAck`.
3. On `PairRequest`, the reactor validates `role == Hub`, records the HUB MAC, unicasts
   `PairAck`.
4. On `PairAck`, the HUB **locks** the channel, adds the reactor as an ESP-NOW peer, and both
   persist binding to NVS. HUB transitions Unpaired → Paired; telemetry begins.

**Channel handling (Barebone-faithful + sweep):** peers added with `peerInfo.channel = 0`
(follow current radio channel), `ifidx = WIFI_IF_STA`, `encrypt = false`. The reactor never
leaves its WiFi channel. The HUB sets its radio channel explicitly (it has no STA to follow):
on boot it tries the **persisted** channel first and only re-sweeps if the reactor is silent.

**NVS persistence — namespace `espnow`** (plain `Preferences`; MAC/channel are not secret, so
**not** routed through `NvsAes`):

| Key | Type | Meaning |
|---|---|---|
| `peerMac` | 6 B | bound peer MAC |
| `channel` | u8 | last-locked channel |
| `bound` | bool | binding present |

Re-pairing overwrites; an "unpair/forget" action clears the keys (HUB button + reactor
`POST /api/v1/espnow/forget`, mirroring `wifi/forget`).

## Cadence & reliability

- **Telemetry ~4 Hz** (250 ms — mirrors the WS push period). Telemetry **is** the heartbeat.
- **HUB link-loss after ~3 s** without telemetry ⇒ show "disconnected", then re-sweep for the
  channel (handles the reactor's router rechannelling / roaming).
- **Send:** 3× back-to-back resend (Barebone pattern), best-effort, no ACK loop for telemetry.
- **Commands:** HUB resends on no-Ack within ~300 ms, ≤3 attempts, then surfaces "no response".
- **Dedup:** small `(seq)` ring on each receiver drops the 3× duplicates. (Single peer ⇒ MAC
  need not key the ring, but keep `(mac, seq)` for forward-compat.)
- **ISR safety:** the `esp_now_recv` callback only copies bytes into a FreeRTOS queue; all
  decode/validation/apply happens in the drained loop context, never in the ISR.

## Toggle contract (`Claude.md`)

- `AppConfig::Features::kEnableEspNow` (reactor) and `AppConfig::HubFeatures::kEnableEspNow`
  (HUB) — compile-time `constexpr`, default `true`.
- `false` ⇒ no `esp_now_init`, no peer/channel calls, no radio use for ESP-NOW; explicit
  startup log (`[ESPNOW] disabled` / `[HUB] espnow: disabled`); the reactor's `/espnow/pair`
  and `/espnow/forget` return **HTTP 503 `feature_disabled`**.
- Reactor `GET /api/v1/status` advertises the flag under `features` (`"espnow": true`).

## Integration points

### Reactor (`AppRuntime` + `EspNowResponder`)
- `begin()`: after `g_wifi.begin()` / `g_web.begin()`, init the responder (gated) — ESP-NOW
  rides the already-up WiFi radio/channel.
- loop: drain the responder queue; build + send telemetry at the 4 Hz cadence; apply commands
  via the shared pending path; emit Acks.
- New endpoints in `WebInterface`: `POST /api/v1/espnow/pair`, `POST /api/v1/espnow/forget`;
  `status.features.espnow`.

### HUB (`HubRuntime`)
- `begin()`: init `EspNowLink` (gated). If `bound`, set channel + add peer + listen; else
  Unpaired.
- `tick()`: drain link events → telemetry snapshot → UI; run sweep when searching; manage the
  link-loss timer; deep-sleep interaction — **defer sleep while Searching/Paired-active**, and
  on wake try the persisted channel before re-sweeping. (Coexists with the Phase-1 sleep FSM.)
- **UI (minimal, LVGL):** three states — **Unpaired** (Pair button), **Searching** (sweep
  progress), **Paired** (live telemetry: temp/setpoint/heater, rpm, run id/name/elapsed,
  alarms). Replaces the throwaway bring-up screen as the HUB's default screen; Phase 3
  redesigns. Command controls (stop, setpoint, pause) may be stubbed minimally here — full
  control UX is Phase 3; the command **path** is proven by at least one live control (e.g.
  Stop + Pause) end-to-end.

## Pause/resume wiring (cheap — control already exists)

`control/Reactor` already implements `setMotorPaused(bool)` (B1), `setFullHold(bool)` (B2),
and exposes `motorPaused`/`fullHold` in its telemetry struct (front-panel driven today). This
phase only: (1) maps `Pause/Resume` opcodes to those setters, (2) surfaces the two bits in the
ESP-NOW telemetry `flags`. Closing the REST gap (`action:"pause"/"resume"` + `run.pause` in the
status doc) is an **optional** low-cost add noted for `API.md`, not on the critical path.

## Error handling — graceful degradation

- ESP-NOW init failure ⇒ log, mark link unavailable, continue (reactor keeps REST; HUB shows
  "link unavailable"). Never halts boot.
- Unknown/oversized/short frames and version mismatches are dropped with a counter, not a crash.
- A rejected command yields an `Ack(Rejected, code)`; the HUB shows the reason instead of
  silently failing.
- Sweep exhausts all channels with no `PairAck` ⇒ HUB returns to Unpaired with "no reactor
  found".

## Testing

- **Native unit tests** (`native` env, existing pattern):
  - `SyncCodec` encode→decode round-trip for every frame (incl. fixed-point scaling edges:
    `INT16_MIN` nulls, `remainingSec == -1`, name truncation/NUL).
  - Command validation (ranges mirror REST: `targetC` 0–55, `rpm` 0–30, `currentMa` clamp).
  - `seq` dedup ring behavior.
- **On-device:** pairing handshake (dual-button, sweep finds channel, persists across reboot);
  telemetry render at 4 Hz; each command path + its Ack (Accepted / Rejected / FeatureDisabled);
  link-loss + re-sweep; toggle-off builds for both products.

## Build, partition & validation (`Claude.md`)

- Full-path PlatformIO: `~/.platformio/penv/bin/pio run -e <hub|esp32-s3-devkitc-1|native>`.
  (First clean build may hit the benign pioarduino `command not found` once — re-run.)
- **No partition change** (ESP-NOW adds code, not a new flash region) ⇒ no forced erase. NVS
  `espnow` namespace is created on first use.
- Build passes for **both** products with `kEnableEspNow = true` **and** `= false`.
- `native` tests green.
- Reactor behavior unchanged when ESP-NOW is disabled.
- No changes outside `MINI_REACTOR`.

## Docs to update

- `API.md`: `POST /api/v1/espnow/pair`, `POST /api/v1/espnow/forget`, `status.features.espnow`,
  the 503-when-disabled rows, and a note that ESP-NOW telemetry is a compact binary projection
  of the status doc (link to the protocol doc).
- `docs/PROTOCOL_ESPNOW.md` (new): frame layout, opcodes, pairing sequence, channel rules.
- `Roadmap.md`: check **Phase 2** sub-item and the Platform-Services / Backlog ESP-NOW items.
- `README.md`: note the shared `src/sync` service across the two products.

## Implementation phasing (one spec — ordered tasks)

1. `SyncFrames.hpp` + `SyncCodec` (+ native round-trip/validation tests).
2. `EspNowLink` transport: init, peer/channel, send (3× resend), ISR→queue→drain, dedup.
3. Reactor responder: telemetry encode from live state; route commands to the existing apply
   path; emit Ack. (Toggle-gated, default behavior unchanged when off.)
4. Pairing/binding: both sides, channel sweep, NVS `espnow`, `/espnow/pair` + `/espnow/forget`.
5. HUB client + minimal Unpaired/Searching/Paired UI; at least one live control end-to-end.
6. Pause/resume opcode wiring + telemetry flags.
7. Docs + both-product toggle-off builds + on-device validation.

## Open questions / risks

- **Shared apply path:** the cleanest hook into the reactor's pending-command queue is TBD at
  planning time (the responder must reach the same mutexed queue `WebInterface` uses). Risk:
  needing a small refactor to expose an internal enqueue API. Mitigation: factor a tiny
  command-intake interface both transports call.
- **Channel sweep vs deep sleep (HUB):** sweeping is radio-on and a few hundred ms/channel;
  must not fight the Phase-1 sleep FSM. Design: only sweep on explicit Pair or on wake-with-no-
  telemetry; never sweep while idle-armed.
- **Region channel count:** sweep `1..13` is broadest; if the build pins a country (`US`=11),
  match it. Confirm against the WiFi region the reactor uses.
- **ISR callback signature drift:** IDF 5.5 uses `wifi_tx_info_t*` in the send cb and a
  `esp_now_recv_info_t*` in the recv cb — port against the in-tree IDF, not Barebone's
  conditionals.
- **Two reactors in range during pairing:** single-peer model + 60 s mutual window makes this
  unlikely, but the HUB should bind the **first** `PairAck` and ignore the rest within a sweep.
```
