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

- [ ] **IDF 5.5.x port** — currently on `espressif32@6.9.0` → Arduino 2.0.17 → IDF 4.4.7. Port on `feature/idf55-port` (pioarduino platform, Arduino 3.x). See `docs/superpowers/plans/` port plan.
- [ ] **Feature-toggle scaffold** — add `AppConfig::Features::kEnable*` flags; gate init in `AppRuntime::begin()`; `503 + feature_disabled` on disabled control APIs; explicit enabled/disabled startup logs. Targets: SD logging, OLED UI, autotune, (later) OTA/MQTT/ESP-NOW.
- [ ] **`features/` module layout** — move product features under `include/features/*` and `src/features/*`, keeping platform services separate. Sequence after the toggle scaffold to avoid churn.
- [x] `main.cpp` orchestration-only (delegates to `AppRuntime`)
- [x] `API.md` matches exposed endpoints; mock-only affordances flagged

## Backlog / Future

- [ ] OTA firmware update (pairs with partition-change reflash rule)
- [ ] MQTT telemetry/control bridge
- [ ] ESP-NOW multi-reactor sync + binding
- [ ] Authentication on the web control surface
