# Mini-Reactor

**Browser-controlled rotating-disc bioreactor firmware for the ESP32-S3.**

Closed-loop thermal control, a UART-driven stepper agitator, USB-PD power negotiation,
SD-card data logging, and a self-hosted web dashboard (REST + WebSocket) — all on a single
ESP32-S3-WROOM-1, with no app, no cloud, and no blocking delays.

![platform](https://img.shields.io/badge/platform-ESP32--S3-informational)
![framework](https://img.shields.io/badge/framework-Arduino-blue)
![build](https://img.shields.io/badge/build-PlatformIO-orange)
![firmware](https://img.shields.io/badge/firmware-1.0.0-success)

---

## Overview

The Mini-Reactor maintains a liquid bath at a target temperature while a rotating disc
agitates the culture at a set rpm — e.g. for bacterial-cellulose fermentation. The firmware
runs a PID loop on a liquid probe, drives the disc through a TMC2209 over single-wire UART,
logs telemetry to SD, and serves a live control UI you reach from any browser on the LAN.

Everything is controlled over `http://<device-ip>/` — the device hosts the UI from on-board
flash and streams telemetry over a WebSocket at ~4 Hz.

## Features

- **Thermal control** — PID on a DS18B20 liquid probe (the process value); auto / manual /
  relay-autotune modes; gains persisted in NVS.
- **Independent safety** — a heater-mounted NTC acts as an over-temp high-limit that forces
  the heater off, separate from the control loop. A disconnected probe is treated as a fault
  and refuses to start a run.
- **Disc agitator** — TMC2209 in single-wire UART mode; motion via the `VACTUAL` register (no
  STEP/DIR pulsing). rpm-based control, configurable current / microsteps / direction
  (persisted), StallGuard load readout, full `DRV_STATUS` fault decoding, and a one-click test
  jog.
- **USB-PD power** — a HUSB238 PD sink negotiates 12 V for the motor/heater rail at boot.
- **Data logging** — micro-SD over SPI, CSV rows at a fixed interval; download / rotate / full
  erase from the UI.
- **Networking** — WiFi station with an AP captive-portal for onboarding; async network scan;
  credentials stored AES-encrypted in NVS.
- **Web control surface** — async HTTP + a single telemetry WebSocket (`ESPAsyncWebServer`);
  a dependency-free ES-module SPA served from SPIFFS (Monitor / Run / History / Settings).
- **Heater-NTC calibration** — capture reference points and fit offset / Beta / Steinhart by
  point count.

## Hardware

- **MCU:** ESP32-S3-WROOM-1 (native USB-CDC serial).
- **Agitator driver:** TMC2209 (U4), single-wire UART, 0.11 Ω sense, motor coils on J2.
- **Power:** USB-C → HUSB238 USB-PD sink → **+12 V** rail (motor + heater); TPS563390 buck → 3.3 V logic.
- **Sensors:** DS18B20 liquid probe (1-Wire), 10 k/3950 NTC heater safety probe (ADC).
- **Heater:** low-side MOSFET driven by LEDC PWM.
- **Storage:** micro-SD on the FSPI bus.

> ⚠️ The +12 V rail is gated by the HUSB238 on PD attach. Powering the board from a plain USB
> port (no PD) leaves the rail off, so the agitator cannot spin even though the TMC2209 is
> alive on UART. Use a 12 V-capable USB-C PD source to drive the motor/heater.

Full GPIO map and bus details: [`docs/PINOUT.md`](docs/PINOUT.md). Board schematic:
[`Resources/Board Schematic.pdf`](Resources/Board%20Schematic.pdf).

## Architecture

A layered, hardware-agnostic core assembled at the top:

- **Devices** (`motor/`, `heater/`, `sensor/`, `power/`, `storage/`, `net/`) — thin drivers
  over the hardware.
- **Controllers** (`control/`) — `ThermalController` (PID + safety), `Reactor` (run
  orchestration, rpm/setpoint, persistence).
- **System** (`system/AppRuntime`) — builds every device's config from `app_config.hpp`, owns
  the global objects, and runs a non-blocking cooperative scheduler: WiFi FSM → PID → run
  timer → web/WS update → periodic SD logging, with no blocking `delay()` so the dashboard
  stays live.

Pure logic (`RpmKinematics`, `PidController`, `RelayAutotune`, `ThermistorCalibration`,
`AlarmTracker`, `DrvStatus`) is dependency-free and host-unit-tested.

## Repository layout

```
platformio.ini          PlatformIO project (esp32-s3-devkitc-1 + native test env)
partitions.csv          flash partition table
include/                headers
  app_config.hpp        single source of truth for pins, buses, and defaults
  features/             product features: control/ heater/ motor/ sensor/ ui/
  net/ storage/ security/ power/ system/   reusable platform services
src/                    implementations mirroring include/ (incl. src/features/*)
data/                   web UI served from SPIFFS (index.html, app.js, app.css, core/, screens/)
test/                   Unity unit tests (run on the native env)
tools/mock_server.py    aiohttp mock of the /api/v1 REST + WebSocket contract for UI dev
docs/                   PINOUT.md, openapi.yaml, design specs & plans
Resources/              board schematic
```

## Build & flash

Requires [PlatformIO](https://platformio.org/). The board enumerates as native USB-CDC at
115200 baud.

```bash
# Build firmware
pio run -e esp32-s3-devkitc-1

# Flash firmware
pio run -e esp32-s3-devkitc-1 -t upload

# Upload the web UI to the SPIFFS filesystem (run after changing anything in data/)
pio run -e esp32-s3-devkitc-1 -t uploadfs

# Serial monitor (prints the device IP at boot)
pio device monitor
```

There is no mDNS responder — read the device IP from the boot log and open
`http://<device-ip>/`. If WiFi can't join a saved network it falls back to the
`MiniReactor-Setup` AP at `192.168.4.1` for onboarding.

### HUB companion device (Waveshare ESP32-S3-Touch-AMOLED-1.75)

A second firmware target runs on the HUB — an AMOLED touch controller board that will provide
the fermentation monitoring UI (Phase 3) and ESP-NOW link to the reactor (Phase 2).

```bash
# Build HUB firmware
~/.platformio/penv/bin/pio run -e hub

# Flash HUB firmware (first flash — new board, new partition table partitions_hub_16mb.csv)
~/.platformio/penv/bin/pio run -e hub -t erase     # full chip erase (required after a partition change)
~/.platformio/penv/bin/pio run -e hub -t upload
```

> **First flash:** `partitions_hub_16mb.csv` is a new partition table — use a full erase + flash
> sequence. Skipping erase after a partition change is not acceptable (Partition Change Rule).

Phase 1 = board bring-up (all IC drivers online, diagnostics screen, deep-sleep FSM).
GPIO map: [`docs/PINOUT_HUB.md`](docs/PINOUT_HUB.md).
Design spec: [`docs/superpowers/specs/2026-06-30-hub-board-bringup-design.md`](docs/superpowers/specs/2026-06-30-hub-board-bringup-design.md).

## Web UI & API

The UI is a vanilla ES-module SPA (no build step) with four tabs: **Monitor** (live reading +
chart), **Run** (start/stop, target/rpm/duration), **History** (log chart + table), and
**Settings** (WiFi, PID, calibration, motor, SD).

The control surface is REST under `http://<device-ip>/api/v1`, plus one read-only telemetry
WebSocket at `ws://<device-ip>/ws` that pushes the full status object ~4 Hz.

| Method | Path | Purpose |
|--------|------|---------|
| `GET`  | `/api/v1/status` | full telemetry snapshot |
| `POST` | `/api/v1/run` | start / stop a run |
| `POST` | `/api/v1/setpoint` | live target °C / rpm |
| `POST` | `/api/v1/disc` | drive params (current, microsteps, direction, rpm) |
| `POST` | `/api/v1/disc/test` | brief agitator test jog |
| `POST` | `/api/v1/pid`, `/api/v1/pid/autotune` | gains / mode / autotune |
| `GET`/`POST` | `/api/v1/calibration[...]` | heater-NTC calibration |
| `GET`  | `/api/v1/wifi/scan`, `POST .../connect`, `.../forget` | WiFi |
| `GET`  | `/api/v1/log`, `POST .../log/clear`, `POST /api/v1/sd/erase` | SD log |

Full reference: [`API.md`](API.md) · OpenAPI: [`docs/openapi.yaml`](docs/openapi.yaml).

## Local UI development (no hardware)

`tools/mock_server.py` fakes the entire firmware contract — nested telemetry, a first-order
thermal model, the rpm disc, NTC safety probe, and the async WiFi scan — so you can develop
the UI in a browser with no board attached.

```bash
pip install aiohttp
python tools/mock_server.py
# open http://localhost:8000
```

It serves the live `data/` directory, so edits to the UI show up on refresh.

## Testing

Pure logic is covered by Unity tests that run on the host:

```bash
pio test -e native
```

## Configuration

All pin assignments, bus parameters, and device defaults live in
[`include/app_config.hpp`](include/app_config.hpp) as scoped `constexpr` values (e.g.
`AppConfig::Motor::kCurrentMilliamps`). The per-device `Config` structs in
`src/system/AppRuntime.cpp` are built from these — change hardware wiring or defaults in one
place.

Optional features are gated by compile-time toggles in `AppConfig::Features`
(`kEnableSdLogging`, `kEnableOledUi`, `kEnableAutotune`, all default `true`).
Setting one to `false` skips that module's init/runtime, makes its control
endpoints return `503 feature_disabled`, logs its state at boot (`[FEAT] …`),
and hides its web-UI section. See [`API.md`](API.md#feature-toggles).

## License

Not yet specified.
