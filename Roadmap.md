# Mini-Reactor — Roadmap

Living plan of record for the ESP32-S3 bioreactor firmware. Checkboxes track
state: `[x]` shipped, `[ ]` planned. Keep in sync with `git log`, `API.md`, and
the specs/plans under `docs/superpowers/`.

> Conformity reference: [Claude.md](Claude.md). Open conformity gaps are tracked
> in the **Conformity & Tech Debt** section below.

---

## Platform Services (reusable, hardware-agnostic)

- [x] WiFi station + AP captive-portal onboarding, async scan (`net/WifiManager`)
- [x] AES-encrypted credential storage in NVS (`security/NvsAes`)
- [x] Async Web API + single telemetry WebSocket (`net/WebInterface`)
- [x] SPIFFS-served dependency-free ES-module SPA (`data/`)
- [x] micro-SD storage over SPI, per-run CSV files + name sidecar (`storage/SdLogger`, `storage/RunFiles`)
- [x] USB-PD 12 V sink negotiation at boot (`power/Husb238`)
- [x] Identity / firmware-version reporting in telemetry
- [ ] OTA update service
- [ ] MQTT publish/subscribe bridge
- [ ] ESP-NOW peer sync / binding

## Product Features

- [x] Thermal PID control on DS18B20 liquid probe; gains persisted in NVS (`control/ThermalController`, `control/PidController`)
- [x] Relay autotune mode (`control/RelayAutotune`)
- [x] Independent NTC over-temp safety high-limit + disconnected-probe start refusal (`sensor/Thermistor`, `system/AlarmTracker`)
- [x] Heater-NTC calibration (offset / Beta / Steinhart by point count) (`sensor/ThermistorCalibration`)
- [x] Disc agitator: TMC2209 UART `VACTUAL` rpm control, current/microsteps/direction persisted (`motor/Tmc2209Motor`, `motor/RpmKinematics`)
- [x] StallGuard load readout + `DRV_STATUS` fault decode + one-click test jog (`motor/DrvStatus`)
- [x] Heater LEDC PWM drive (`heater/Heater`)
- [x] Run lifecycle: start/stop, duration (days/hrs/min), pause/resume, full-hold (`control/Reactor`)
- [x] Run history + session naming (per-run files, `/runs` routes) (`storage/RunFiles`)
- [x] Configurable data-log interval via `POST /api/v1/log/interval`
- [x] Front-panel OLED UI: SH1107 on dedicated Wire1 bus, encoder + 3 buttons (`ui/*`)
- [x] Web SPA: Monitor / Run / History / Settings, persistent run-control bar

## Conformity & Tech Debt (from Claude.md audit — 2026-06-25)

- [x] **IDF 5.5.x port** — `feature/idf55-port` now on pioarduino `55.03.39` → Arduino-ESP32 **3.3.9** → ESP-IDF **5.5.4** (was `espressif32@6.9.0`/2.0.17/4.4.7). Heater LEDC migrated to pin-based API; ADC attenuation set as global default for the 3.x one-shot driver. Clean build green; verified on hardware (boot banner `ESP-IDF v5.5.4`, `GET /api/v1/status` 200). See `docs/superpowers/plans/2026-06-25-idf55-port.md`.
- [x] **Feature-toggle scaffold** — add `AppConfig::Features::kEnable*` flags; gate init in `AppRuntime::begin()`; `503 + feature_disabled` on disabled control APIs; explicit enabled/disabled startup logs. Targets: SD logging, OLED UI, autotune, (later) OTA/MQTT/ESP-NOW.
- [x] **`features/` module layout** — product features moved under `include/features/*` and `src/features/*` (control, heater, motor, sensor, ui); platform services (net, storage, security, power, system) kept separate.
- [x] `main.cpp` orchestration-only (delegates to `AppRuntime`)
- [x] `API.md` matches exposed endpoints; mock-only affordances flagged

## Backlog / Future

- [ ] OTA firmware update (pairs with partition-change reflash rule)
- [ ] MQTT telemetry/control bridge
- [ ] ESP-NOW multi-reactor sync + binding
- [ ] Authentication on the web control surface
