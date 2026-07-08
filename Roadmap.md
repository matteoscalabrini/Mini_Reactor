# Mini-Reactor — Roadmap

Living plan of record for the ESP32-S3 bioreactor firmware. Checkboxes track
state: `[x]` shipped, `[ ]` planned. Keep in sync with `git log`, `API.md`, and
the specs/plans under `docs/superpowers/`.

> Conformity reference: [Claude.md](Claude.md). Open conformity gaps are tracked
> in the **Conformity & Tech Debt** section below.

---

## Platform Services (reusable, hardware-agnostic)

- [x] WiFi station + AP captive-portal onboarding, async scan (`net/WifiManager`)
  - [x] Stable **AP-only** fallback: setup portal drops STA so the shared radio can't starve the AP (fixes portal unreachable-on-AP); auto-recovers by switching AP→STA every `kApRetryIntervalMs`, never coexisting
- [x] AES-encrypted credential storage in NVS (`security/NvsAes`)
- [x] Async Web API + single telemetry WebSocket (`net/WebInterface`)
- [x] SPIFFS-served dependency-free ES-module SPA (`data/`)
- [x] micro-SD storage over SPI, per-run CSV files + name sidecar (`storage/SdLogger`, `storage/RunFiles`)
- [x] USB-PD 12 V sink negotiation at boot (`power/Husb238`)
- [x] Identity / firmware-version reporting in telemetry
- [ ] OTA update service
- [ ] MQTT publish/subscribe bridge
- [x] ESP-NOW peer sync / binding

## Product Features

- [x] Thermal PID control on DS18B20 liquid probe; gains persisted in NVS (`control/ThermalController`, `control/PidController`)
- [x] Relay autotune mode (`control/RelayAutotune`)
- [x] Adaptive gain-scheduled thermal control — heat/approach/hold regimes with tapered duty
      ceiling (kills 36→40 overshoot); auto-run relay tune below setpoint derives heat+hold
      gains; toggle kEnableAdaptiveThermal. Spec/plan under docs/superpowers/. On-hardware
      overshoot verification pending next run.
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

## HUB (companion device — Waveshare ESP32-S3-Touch-AMOLED-1.75)
Ported from ../Barebone. See docs/superpowers/specs/2026-06-30-hub-board-bringup-design.md.
- [x] Phase 1 — board bring-up: `[env:hub]`, all IC drivers, power-aware toggles, sleep, diagnostics screen
  - [x] AXP2101 PMIC (power authority + battery telemetry)
  - [x] CO5300 AMOLED + slim LVGL runtime
  - [x] CST9217 touch + transform
  - [x] QMI8658 IMU + wake-on-motion
  - [x] PCF85063 RTC
  - [x] TCA9554 IO expander
  - [x] ES8311/ES7210 probe + power-down
  - [x] HubSleepLogic deep-sleep FSM
- [x] Phase 2 — ESP-NOW peer link + reactor binding (both firmwares). Design: docs/superpowers/specs/2026-06-30-hub-espnow-link-design.md (telemetry+control, 1 reactor; WiFi-less HUB; shared `src/sync` transport)
- [x] First-boot touch calibration wizard (ported from ../Barebone): tap 4 orbit targets, brute-force the 8 swap/mirror permutations, persist to NVS (`touchcal`). Fixes CST9217 release detection (event==0x06 gate) + off-target taps. Re-triggerable from the reactor (**Settings → HUB LINK → Recalibrate**, `POST /espnow/recalibrate`, `MsgType::RecalibrateTouch`).
  - [ ] Gravity-aligned UI rotation (IMU) — deferred follow-up; barebone rotates a rendered scene, which the lean HUB UI lacks
- [x] Phase 3 — redesigned fermentation monitoring/control UI (LVGL). Design: docs/superpowers/specs/2026-07-01-hub-fermentation-ui-design.md. True-black round-AMOLED UI in the brand look (cyan `#00b4d8` Doto hero, red STOP, slate controls, white MINI REACTOR wordmark): HOME↔DETAILS swipe carousel, **state-coloured hero temp** (cyan/amber/red), tap-to-confirm STOP/START, IDLE + SAFETY states, radar pairing screen, cross-layout touch calibration. Monitor + Start/Pause/Stop only (no tuning on the hub); hub START uses the reactor's current settings (`kCmdFlagUseCurrent`). Stays within the lean LVGL widget set (LABEL + IMG + `lv_obj` + `lv_anim`).

## Conformity & Tech Debt (from Claude.md audit — 2026-06-25)

- [x] **IDF 5.5.x port** — `feature/idf55-port` now on pioarduino `55.03.39` → Arduino-ESP32 **3.3.9** → ESP-IDF **5.5.4** (was `espressif32@6.9.0`/2.0.17/4.4.7). Heater LEDC migrated to pin-based API; ADC attenuation set as global default for the 3.x one-shot driver. Clean build green; verified on hardware (boot banner `ESP-IDF v5.5.4`, `GET /api/v1/status` 200). See `docs/superpowers/plans/2026-06-25-idf55-port.md`.
- [x] **Feature-toggle scaffold** — add `AppConfig::Features::kEnable*` flags; gate init in `AppRuntime::begin()`; `503 + feature_disabled` on disabled control APIs; explicit enabled/disabled startup logs. Targets: SD logging, OLED UI, autotune, (later) OTA/MQTT/ESP-NOW.
- [x] **`features/` module layout** — product features moved under `include/features/*` and `src/features/*` (control, heater, motor, sensor, ui); platform services (net, storage, security, power, system) kept separate.
- [x] `main.cpp` orchestration-only (delegates to `AppRuntime`)
- [x] `API.md` matches exposed endpoints; mock-only affordances flagged

## Stability — network death after 30min–2h uptime (investigation, 2026-07-08)

Symptom: web UI + hub link + ping all die after variable uptime (~30min…1h50m observed);
control loop (heater/PID/motor) keeps running. Firmware audit found no unbounded leak in
app code; WS/AsyncTCP library layer (AsyncTCP 3.3.2 / ESPAsyncWebServer 3.6.0) is bounded
(`closeWhenFull=true`, 32-msg queue cap). Live evidence 2026-07-08: device off-LAN (no ARP),
**no `MiniReactor-Setup` AP beaconing** → points at heap/driver starvation, not AP-fallback exile.

- [x] Heap diagnostics in `system.*` status fields (minFreeHeap, largestBlock, freeDma, minFreeDma)
- [x] `tools/heapmon.py` — network-side capture harness (CSV until death + held WS client to exercise
      the push path; ping fallback distinguishes http_dead vs ip_dead)
- [x] Evidence run (bench, 2026-07-08): death reproduced in 5.5 min; heap **healthy** at death
      (~209k free, largestBlock 164k) — leak/fragmentation ruled out. RSSI faded −82→−93 at death;
      `WL_CONNECTED` stayed true through a 61 s outage (FSM blind). Captures: `tools/heapmon-bench1.csv`,
      `tools/serial-bench1.log`.
- [x] **Root cause: WiFi modem power-save (default ON) + marginal RSSI** — PS DTIM misses make the AP's
      buffered unicast undeliverable (ping/ARP/TCP die) while beacons still arrive, so the FSM never
      reconnects. Fix: `WiFi.setSleep(false)` in `WifiManager::begin()` (also required for ESP-NOW RX).
      A/B verified: PS on = dead at 5.5 min; PS off = clean 30+ min soak at same RSSI, same load.
- [ ] Hardening pass from audit: hostname-before-mode, NVS save-only-on-change, cache TMC/PD reads
      out of the 10 Hz status build, status build at push rate, `availableForWriteAll()` WS gate,
      event-driven runs-list rebuild (not 1 Hz SD enumeration), WiFi-stack-only self-heal watchdog
      (never `ESP.restart()` while a run is active)

## Backlog / Future

- [ ] OTA firmware update (pairs with partition-change reflash rule)
- [ ] MQTT telemetry/control bridge
- [ ] ESP-NOW multi-reactor sync + binding (single-reactor link shipped Phase 2)
- [ ] Authentication on the web control surface
