# HUB Companion Device — Phase 1: Board Bring-up — Design

**Date:** 2026-06-30
**Branch:** `feature/hub-board-bringup` (off `main`)
**Roadmap item:** new **HUB (companion device)** section → "Phase 1: board bring-up"
**Source firmware:** `../Barebone` (`[env:hub]`, `PRODUCT_MODE=5`)
**Conformity drivers:** `Claude.md` → Modularity Requirements, Toggle Behavior Contract,
Tooling Command Rule, Partition Change Rule.

## Summary

Port the **HUB** device — a Waveshare **ESP32-S3-Touch-AMOLED-1.75** board (ESP32-S3
N16R8, 16 MB flash / 8 MB PSRAM) — from the Barebone firmware into this repo as a
**separate companion device** (its own build env), to act as a touchscreen control device
for fermentation/monitoring.

This spec covers **Phase 1 only: board bring-up**. The deliverable is a firmware that
boots on the HUB board, brings up and probes **every** onboard IC, manages each IC's power
state for best battery life, drives the AMOLED + touch via LVGL, and proves the hardware
with an on-device diagnostics screen. **No networking and no product UI** in this phase.

Later phases (separate spec → plan cycles):
- **Phase 2** — ESP-NOW peer link + reactor binding (spans HUB and reactor firmwares;
  realizes the Roadmap's "ESP-NOW multi-reactor sync + binding" backlog item).
- **Phase 3** — redesigned fermentation monitoring/control UI (LVGL).

## Goals

- A new `[env:hub]` build environment in this repo, on the **same** pioarduino 55.03.39 /
  Arduino-ESP32 3.3.x / ESP-IDF 5.5.4 platform the reactor already uses (no toolchain port).
- Import **all** HUB hardware drivers so each IC can be **correctly powered down** when
  unused — the explicit motive for importing everything is power savings, not feature use.
- Reshape the imported code to this repo's modular conventions (thin per-IC drivers +
  `Config` structs + an orchestrator), **preserving Barebone's tested register-level
  bring-up sequences verbatim** (approach "C").
- On-device diagnostics screen + explicit per-IC startup logs proving the import.
- The reactor product (`[env:esp32-s3-devkitc-1]`) remains **byte-unaffected**.

## Non-goals (YAGNI — deferred to later phases)

- No ESP-NOW, WiFi, BLE, web server, or any networking (Phase 2).
- No fermentation monitoring/control UI (Phase 3); the bring-up screen is throwaway.
- No audio playback/record — codecs (ES8311) and mic ADC (ES7210) are **probed then
  powered down**, never exercised.
- No runtime/NVS-configurable toggles — compile-time `constexpr` only (repo convention).
- No reactor firmware changes.

## Hardware reference (Barebone → this repo)

Board: Waveshare ESP32-S3-Touch-AMOLED-1.75. Shared main I2C bus: **SCL=GPIO14,
SDA=GPIO15** @ 400 kHz.

| Block | IC | I2C addr | Barebone source | Phase-1 role |
|---|---|---|---|---|
| Display | CO5300 466×466 AMOLED (QSPI) | — | `features/hubdisplay/HubDisplayPanel`, `HubDisplayFeature` | drive via LVGL |
| Touch | CST9217 | 0x5A | inside `HubFeature` + `HubTouchTransform` | touch input + transform |
| Power/battery | AXP2101 PMIC/charger | 0x34 | `HubFeature::initializeAxp2101()` | rail authority + battery telemetry |
| IMU | QMI8658 6-axis | 0x6B (fb 0x6A) | `sensors/Qmi8658` | wake-on-motion + accel readout |
| RTC | PCF85063 | 0x51 | `HubFeature` | wall-clock time |
| IO expander | TCA9554 | 0x20 | `HubFeature` | spare GPIO, INT1 redirect |
| Audio codec | ES8311 | 0x18 | `features/hubaudio/*` | probe → power down |
| Mic ADC | ES7210 | 0x40 | `features/hubaudio/*` | probe → power down (off by default) |
| Sleep | — | — | `power/HubSleepLogic` | deep-sleep FSM + wake sources |

**Pinout** (from Barebone `app_config.hpp::PinoutHub`, to be re-homed in this repo and
documented in `docs/PINOUT_HUB.md`):

- Display QSPI: CS=GPIO12, SCLK=GPIO38, D0–D3=GPIO4/5/6/7, RST=GPIO39
- Touch: IRQ=GPIO11 (active-low, EXT1 wake), RST=GPIO40
- I2C: SDA=GPIO15, SCL=GPIO14
- Audio I2S: MCLK=GPIO42, BCLK=GPIO9, WS=GPIO45, DOUT=GPIO8, DIN=GPIO10, AMP_EN=GPIO46
- IMU: INT2(wake)=GPIO21 direct; INT1 via TCA9554
- SD (3-line, unused in Phase 1): CMD=GPIO1, CLK=GPIO2, D0=GPIO3
- System: BOOT=GPIO0

> Note GPIO conflicts vs. the reactor pinout are irrelevant — this is a **different
> physical board** with its own `PinoutHub`. The reactor's `app_config.hpp` pin block is
> untouched.

## Architecture

### Two-product repo

This repo becomes a two-product repo. The reactor is unchanged; the HUB is a second build
env. Each product has its own entry point and `build_src_filter` so neither product's code
links into the other's binary.

- `[env:esp32-s3-devkitc-1]` (reactor, unchanged): `build_src_filter` excludes
  `src/features/hub/**`.
- `[env:hub]` (new): board `esp32-s3-devkitc1-n16r8`, `board_build.partitions =
  partitions_hub_16mb.csv`, LVGL 8.3.11 lib dep, same pioarduino platform block as the
  reactor. `build_src_filter` includes `src/features/hub/**` and excludes the reactor's
  product features (`control/heater/motor/sensor/ui`), `system/AppRuntime.cpp`,
  `net/WebInterface.cpp`, `net/WifiManager.cpp`, and `main.cpp`. Phase 1 needs **no**
  shared networking services (they join in Phase 2); `NvsAes` is pulled in only if touch
  calibration is persisted, otherwise calibration uses compile-time defaults.

LVGL config: `-D LV_CONF_INCLUDE_SIMPLE`, `-D LV_CONF_PATH=include/hub/lv_conf.h` (ported
from Barebone, hub-only). PSRAM enabled via `custom_sdkconfig` (mirrors Barebone hub).

### Module layout (approach C — modular shell, ported internals)

```
include/features/hub/ (and src/ mirror)
  HubRuntime.{hpp,cpp}        orchestrator: owns shared Wire/I2C bus; ordered
                              power-up → probe → run → power-down; cooperative
                              scheduler (no blocking delay), mirrors AppRuntime
  power/Axp2101.{hpp,cpp}     PMIC: rail control (authority) + battery/charge telemetry
  sensor/Qmi8658.{hpp,cpp}    6-axis IMU + wake-on-motion config
  rtc/Pcf85063.{hpp,cpp}      RTC read/set
  touch/Cst9217.{hpp,cpp}     cap touch + HubTouchTransform (axis swap/mirror/calibration)
  io/Tca9554.{hpp,cpp}        GPIO expander
  audio/Es8311.{hpp,cpp}      codec  ── probed then powered down
  audio/Es7210.{hpp,cpp}      mic ADC ── probed then powered down
  display/Co5300Panel.{hpp,cpp}  low-level QSPI panel driver (verbatim register seq)
  display/HubDisplay.{hpp,cpp}   LVGL runtime: framebuffer (PSRAM), flush cb, tick
  sleep/HubSleepLogic.{hpp,cpp}  deep-sleep FSM + wake-source config
  ui/BringupScreen.{hpp,cpp}     LVGL diagnostics screen
include/hub/lv_conf.h          ported LVGL config (hub-only)
src/features/hub/main_hub.cpp  hub entry point: setup()/loop() → HubRuntime
```

### Config — single source of truth in `app_config.hpp`

Following the reactor pattern, all hub pins/addresses/defaults live as scoped `constexpr`
in `include/app_config.hpp`:

- `PinoutHub` — the GPIO map above.
- `AppConfig::HubFeatures` — per-peripheral toggles (see Power contract).
- `Hub*` tuning namespaces — I2C addresses, AXP2101 charge limits (4.2 V / 1.0 A / 50 mA
  precharge / 25 mA termination), QMI8658 ODR + wake threshold (~220 mg), display sizing,
  poll intervals.

Each driver takes a `Config` struct built by `HubRuntime` — exactly the reactor's
`AppRuntime` → per-device `Config` pattern.

### "Ported internals" rule

Every driver's register-level sequence (probe, init, power-up, sleep, power-down) is
**copied verbatim** from the corresponding Barebone source to preserve its tested ordering.
Only the *surrounding structure* is reshaped: split the `HubFeature` god-class into thin
per-IC drivers, replace Barebone's `PRODUCT_MODE` / `BoardProfile::Id` / `PIN_SELECT`
machinery with this repo's `app_config.hpp` `constexpr`, and add `Config` structs.

## Power-aware toggle contract

`AppConfig::HubFeatures` exposes one flag per peripheral: `kEnableTouch`, `kEnableImu`,
`kEnableRtc`, `kEnableIoExpander`, `kEnableAudio`, `kEnableSleep`, `kEnablePmicTelemetry`.
Defaults: all `true` **except** `kEnableAudio = false` (Barebone keeps audio off).

This extends `Claude.md`'s Toggle Behavior Contract with an explicit **power-down** step:

| Toggle | Behavior |
|---|---|
| `true` | init IC → enable runtime polling → expose state |
| `false` | **issue the IC's lowest-power command** (IC sleep register and/or AXP2101 rail cut), then skip all runtime polling |

A disabled peripheral therefore draws as close to zero current as the silicon allows — the
driver exists precisely so it can be turned *off* correctly. Per-IC startup logs are
explicit: `[HUB] imu: enabled` / `[HUB] audio: disabled (powered down)`.

**AXP2101 is the power authority, not an ordinary toggle.** It always initializes (it gates
every other rail). `kEnablePmicTelemetry` only governs whether battery %/voltage/charge/
VBUS are polled and reported; **rail control is unconditional**. At boot `HubRuntime` brings
rails up in the correct order, then immediately cuts rails for any disabled peripheral
(audio rail, ES7210, etc.).

**Couplings to document:**
- `kEnableImu` doubles as the **wake-on-motion** enable. With it off, wake is touch-only.
- RTC sits on its own always-on rail and survives deep sleep regardless of `kEnableRtc`
  (the toggle governs polling/reporting, not the RTC's own timekeeping).

## Scheduler & sleep

Cooperative scheduler in `HubRuntime::loop()`, no blocking `delay()` (mirrors the reactor):

```
AXP2101 telemetry        ~1 Hz   (when kEnablePmicTelemetry)
I2C probe/refresh        ~1 Hz   (RTC, IO expander)
touch read               IRQ on GPIO11, else ~60 Hz   (when kEnableTouch)
IMU read                 ~50–100 Hz   (when kEnableImu)
LVGL tick + render       ~30 fps
sleep FSM evaluation     each loop   (when kEnableSleep)
```

**Sleep/wake** (`HubSleepLogic`, gated by `kEnableSleep`): ported FSM
`ACTIVE → IDLE_ARMED → deep sleep`. Before sleeping: suspend display, power down codecs,
arm wake sources — touch IRQ (GPIO11, EXT1) and QMI8658 wake-on-motion (INT2/GPIO21,
~220 mg). On wake: fast-resume path. When `kEnableSleep = false`, the device stays awake
(bench/bring-up mode) but per-IC power-down still applies.

## Bring-up / diagnostics screen

`ui/BringupScreen` (LVGL) — the on-device proof of the import and the only UI in Phase 1
(replaced by the Phase 3 redesign). Shows:

- **Per-IC status row**: AXP2101 / QMI8658 / PCF85063 / CST9217 / TCA9554 / ES8311 /
  ES7210, each green (probed OK) / red (absent or error) / grey (toggled off).
- **Power**: battery %, voltage, charging state, VBUS present (AXP2101).
- **Touch test**: tap target echoing transformed X/Y (proves CST9217 + transform).
- **IMU**: live accel/gyro readout (proves QMI8658).
- **RTC**: current time. **System**: firmware string, free heap/PSRAM.

## Error handling — graceful degradation, never halt

- Each probe yields `present | absent | error`. A missing/faulty IC logs
  `[HUB] <ic>: not found` and shows red on the screen, but **boot continues** — a dead
  optional IC must not brick bring-up.
- I2C transactions are per-call guarded (timeout + bounded retry).
- Display init failure falls back to serial-only logging.
- Sleep FSM is guarded: a missing IMU degrades to touch-only wake instead of crashing.

## Testing

- **Host unit tests** (`native` env, existing repo pattern): touch transform
  (axis swap/mirror/calibration), sleep FSM transitions, battery-percent mapping, IMU
  status normalization — all dependency-free pure logic.
- **On-device**: bring-up screen (all ICs green) + `[HUB]` serial logs.

## Build, partition & validation (Claude.md)

- Build with full path: `~/.platformio/penv/bin/pio run -e hub`. (First clean build may hit
  the benign pioarduino `command not found` once — re-run, per `Claude.md`.)
- Build passes for `-e hub`, **and** passes with **each** `HubFeatures::kEnable*` flag set
  to `false`.
- Reactor env still builds unchanged.
- New `partitions_hub_16mb.csv` (3 MB ×2 OTA + ~10 MB SPIFFS) ⇒ **full reflash** required
  for the HUB board (Partition Change Rule). The HUB is a new device, so this is the first
  flash anyway.
- Docs: new `docs/PINOUT_HUB.md`; new **HUB (companion device)** section in `Roadmap.md`
  with Phase-1 checkboxes; README note on the second product/env. No `API.md` change
  (Phase 1 exposes no HTTP surface).
- No changes outside the `MINI_REACTOR` project.

## Phasing (recap)

| Phase | Scope | Spec |
|---|---|---|
| **1 (this)** | Board bring-up: env, drivers, power mgmt, sleep, diagnostics screen | this doc |
| 2 | ESP-NOW peer link + reactor binding (both firmwares) | future |
| 3 | Redesigned fermentation monitoring/control UI (LVGL) | future |

## Open questions / risks

- **Barebone coupling**: `HubFeature`/display/sleep may reference Barebone-only helpers
  (logging, shared `MotionSensor::Status`, networking hooks). The plan must inventory and
  stub/replace these so the hub modules are self-contained. Confirmed self-contained at the
  I2C/probe level by exploration; to be verified file-by-file during planning.
- **LVGL footprint**: 8.3.11 + framebuffers must fit alongside PSRAM config; mirror
  Barebone's `custom_sdkconfig` PSRAM flags.
- **CO5300 QSPI timing**: copy panel init verbatim; do not "optimize" the sequence.
