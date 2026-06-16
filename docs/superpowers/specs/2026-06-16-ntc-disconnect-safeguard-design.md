# Heater-NTC Disconnect / Fault Safeguard — Design

**Date:** 2026-06-16
**Status:** Approved (pending implementation)
**Builds on:** Phase 4 heater-NTC calibration (`docs/superpowers/specs/2026-06-15-reactor-firmware-api-design.md` §4/§5/§9, P4 plan `2026-06-16-p4-ntc-calibration.md`).

## Motivation

On-hardware validation of P4 (device flashed, NTC **not** connected) revealed that a
disconnected heater NTC does **not** present as a fault. With no probe, the ADC node
floats to ~68 mV (`adcRaw ≈ 85`), which is inside the valid window, so the divider math
returns a finite **~240 Ω** and the Beta curve reports a bogus **~141 °C** —
`thermal.safety.probe.resistanceOhms` showed `240` (not `null`).

The current open/short guard in `Thermistor::readResistanceOhms()` only rejects
**rail-pinned** readings (`vAdc ≤ 1 mV` or `vAdc ≥ Vsupply − 1 mV`). A disconnect (or other
gross fault) that lands the node at a mid-scale voltage slips through as a
plausible-but-wrong number. The dangerous direction is a fault that reads spuriously
**low/cold**: the ~80 °C safety cutoff would never trip while the heater overheats.

**Goal:** Any disconnected or grossly-faulted heater NTC must read as a *fault*
(→ `NaN` → heater forced off / Start refused), never as a plausible number.

## Background — hardware & current behavior

Divider wiring (`include/app_config.hpp`):

```
+3.3V — R39 (10k, kNtcSeriesOhms) — THERM_ADC (GPIO6) — NTC — GND
R_ntc = kNtcSeriesOhms · Vadc / (Vsupply − Vadc)        (Beta curve → °C)
```

Factory NTC params: `R0 = 10 kΩ`, `Beta = 3950`, `T0 = 25 °C`, `Vsupply = 3300 mV`.

Existing fail-safe wiring that this design **reuses** (no change):
- `ThermalController::update()` safety poll (every `kSafetyCheckMs = 200 ms`):
  `over = isnan(heaterC_) || heaterC_ >= heaterSafetyMaxC` → `safetyTrip_ = true; applyOff()`.
- `AppRuntime` telemetry: `isnan(heaterTempC)` → `tempC/heaterTempC` and
  `probe.resistanceOhms` emitted as `null`; `heater_probe_fault` (warn) alarm raised
  **whenever `heaterTempC` is NaN — including when idle**; `safety_tripped` (critical)
  when a run trips.
- `Thermistor::addCalibrationPoint()` already returns `false` on a NaN live resistance.

So the entire fault-handling chain already keys off `readResistanceOhms()` returning
`NaN`. This design widens *when* it returns `NaN` and adds a pre-flight Start block.

## Decisions (locked during brainstorming)

1. **Detection domain:** a raw-**resistance** plausibility band, enforced at the single
   `readResistanceOhms()` chokepoint (calibration-independent; both the safety read and
   calibration-point capture inherit it).
2. **Plausible window:** **0…110 °C**, which maps (via the *factory* Beta) to a resistance
   band of **≈ 529 Ω … 33 618 Ω**. Anything outside → probe fault → `NaN`.
3. **Start behavior:** **pre-flight block** — refuse `enable(true)` when the probe currently
   reads faulted (or already over the safety limit); the run never begins
   (`running` stays `false`).

## Architecture / components

### 1. Plausibility band — `ThermistorCalibration` (pure, host-tested)

The safety band lives with the existing calibration math (header-only, no Arduino deps,
host-TDD). It is **calibration-independent**: it always uses the stored **factory** Beta
params, because "is this resistance electrically possible for this probe" must not move
when a user calibration is applied.

- Extend `begin()` to accept the plausible window with **wide defaults** so the existing
  6 calibration tests are unaffected:
  ```cpp
  void begin(const BetaParams& beta,
             float minPlausibleC = -50.0f,
             float maxPlausibleC = 200.0f);
  ```
  At `begin()`, precompute the resistance bounds from the **factory** Beta:
  `rMaxPlausible_ = R_factory(minPlausibleC)` (cold ⇒ high R),
  `rMinPlausible_ = R_factory(maxPlausibleC)` (hot ⇒ low R), where
  `R_factory(T) = r0·exp(beta·(1/(T+273.15) − 1/(t0C+273.15)))`.
- Expose:
  ```cpp
  bool plausibleResistance(float r) const;   // rMinPlausible_ <= r <= rMaxPlausible_
  ```
- `reset()` keeps the factory window (it already routes through `begin(factory_, …)`); the
  window is a fixed property of the probe, not part of a user calibration, so a refit /
  reset never changes it.
- `resistanceToCelsius()` is **unchanged** — it stays a pure conversion. Plausibility is a
  separate gate applied by the wrapper, so the conversion is never invoked on a bad R.

### 2. Chokepoint guard — `Thermistor::readResistanceOhms()`

```cpp
float Thermistor::readResistanceOhms() {
  const float vAdc = static_cast<float>(readMilliVolts());
  if (vAdc <= 1.0f || vAdc >= cfg_.vSupplyMv - 1.0f) return NAN;   // rail: short / open
  const float r = cfg_.seriesOhms * vAdc / (cfg_.vSupplyMv - vAdc);
  if (!cal_.plausibleResistance(r)) return NAN;                    // out-of-band: fault
  return r;
}
```

The rail guard is retained (it also prevents a divide-by-near-zero). Because
`readCelsius()` and `addCalibrationPoint()` both call `readResistanceOhms()`, a
disconnected probe automatically (a) reads `NaN` °C and (b) cannot be captured as a
calibration point.

### 3. Pre-flight Start block — `ThermalController::enable()`

Change the signature from `void` to `bool`:

```cpp
bool enable(bool on);   // returns the resulting enabled state
```

On `enable(true)`, take a fresh safety read before committing:

```cpp
bool ThermalController::enable(bool on) {
  if (on) {
    heaterC_ = ntc_.readCelsius();
    if (isnan(heaterC_) || heaterC_ >= cfg_.heaterSafetyMaxC) {
      enabled_ = false;          // refuse: probe faulted or already too hot
      applyOff();                // heater stays off; run never begins
      return false;
    }
  }
  enabled_ = on;
  if (!on) { /* existing teardown: cancelAutotune(), applyOff(), etc. */ }
  return enabled_;
}
```

The refusal needs no extra state: `enabled_` staying `false` is the whole signal. The
`safetyTrip_` (critical) flag remains reserved for an **active** run being killed by the
mid-run safety poll; a refused Start surfaces instead as `running:false` plus the
`heater_probe_fault` (warn) alarm (or, for an already-too-hot probe, the visible high
`heaterTempC`).

`Reactor::start()` checks the result and aborts the run on refusal:

```cpp
if (!thermal_.enable(true)) { /* do not mark running; leave reactor idle */ return; }
```

The refusal is observable via `/status` (`running:false`) plus the `heater_probe_fault`
(warn) alarm that is already raised whenever `heaterTempC` is NaN. (Exact teardown
preserved from the current `enable(false)` path — including `cancelAutotune()`.)

### 4. Config knobs

`include/app_config.hpp` → `namespace Thermal`:
```cpp
static constexpr float kNtcMinPlausibleC = 0.0f;
static constexpr float kNtcMaxPlausibleC = 110.0f;
```
`Thermistor::Config` gains:
```cpp
float minPlausibleC = 0.0f;
float maxPlausibleC = 110.0f;
```
Wired where the Thermistor config is built in `AppRuntime`; `Thermistor::begin()` passes
them into `cal_.begin({...}, cfg_.minPlausibleC, cfg_.maxPlausibleC)`.

### 5. Telemetry / alarms — reused, no new firmware paths

| Condition | `probe.resistanceOhms` | `heaterTempC` | Alarm | Heater |
|---|---|---|---|---|
| Probe OK | rounded Ω | °C | — | per control |
| Disconnected/faulted (idle) | `null` | `null` | `heater_probe_fault` (warn) | off |
| Disconnected/faulted, Start pressed | `null` | `null` | `heater_probe_fault` (warn) | **Start refused, stays idle** |
| Fault appears mid-run | `null` | `null` | `heater_probe_fault` + `safety_tripped` (critical) | forced off |

All of the above already follow from `readResistanceOhms()` → `NaN`; only the
mid-run-trip and idle-warn paths exist today, and the new band feeds them.

### 6. Mock server

`tools/mock_server.py` gains a way to simulate a disconnected/faulted probe so the future
UI and integration tests can exercise it:
- A state flag (e.g. `ntcFault`, or an out-of-band `ntcR`) such that `status()` emits
  `probe.resistanceOhms: null`, `heaterTempC: null`, and adds the `heater_probe_fault`
  alarm; and `/run` (start) is refused (stays `running:false`) while the fault is active.
- Mirrors the firmware contract; no new endpoints required beyond an optional debug toggle
  to set/clear the simulated fault.

## Data flow (disconnected probe, operator presses Start)

```
ADC floats out-of-band
  → Thermistor::readResistanceOhms(): r computed, !plausibleResistance(r) → NaN
  → Thermistor::readCelsius() → NaN
  → Reactor::start() → ThermalController::enable(true):
        fresh readCelsius() == NaN → refuse, enabled_ stays false, return false
  → Reactor::start() aborts → running stays false
  → AppRuntime telemetry: heaterTempC null, probe.resistanceOhms null,
        heater_probe_fault (warn) raised
```

## Testing

- **Host TDD (`test/test_thermistor_calibration/`)** — extend with `plausibleResistance`
  cases against the 0…110 °C window:
  - plausible at `10 000 Ω` (25 °C) and near both edges just inside the band;
  - **fault** at the observed `~240 Ω` (~140 °C) and at `~50 000 Ω` (~−7 °C);
  - boundary values at `R(0 °C) ≈ 33 618 Ω` and `R(110 °C) ≈ 529 Ω`;
  - confirm the existing 6 tests still pass (wide default window keeps them permissive).
- **Compile** `esp32-s3-devkitc-1` clean; full native suite green.
- **Mock** — curl checks: disconnected-probe simulation yields `null` telemetry +
  `heater_probe_fault`, and Start is refused.
- **On-device** — with the NTC unplugged: `/status` now shows `probe.resistanceOhms: null`,
  `heaterTempC: null`, a `heater_probe_fault` alarm, and `POST /run` (start) leaves
  `running:false`. (When the real probe + reference thermometer arrive: confirm a genuine
  reading is in-band and Start is allowed.)

## Known limitation (out of scope)

A plausibility band catches **gross** faults — a disconnect/short/open whose electrical
signature lands outside the band. It **cannot** catch a subtle fault that reads
plausibly-but-wrong *within* the band (e.g. a partial connection reading 25 °C while the
heater is at 90 °C). Detecting that requires cross-validation against the DS18B20 liquid
probe (e.g. flag when the heater is energized but the NTC fails to rise, or the two probes
disagree implausibly). That is deferred — the DS18B20 is trusted at factory accuracy and is
not connected yet.

## File-by-file change summary

| File | Change |
|---|---|
| `include/sensor/ThermistorCalibration.hpp` | `begin()` gains plausible-window params (wide defaults); precompute factory-based `rMin/rMaxPlausible_`; add `plausibleResistance(float)`. |
| `test/test_thermistor_calibration/test_main.cpp` | Add `plausibleResistance` band tests (TDD). |
| `include/sensor/Thermistor.hpp` / `src/sensor/Thermistor.cpp` | `Config.minPlausibleC/maxPlausibleC`; pass them to `cal_.begin(...)`; band guard in `readResistanceOhms()`. |
| `include/control/ThermalController.hpp` / `src/control/ThermalController.cpp` | `enable()` → `bool` with pre-flight probe/over-temp check; preserve existing `enable(false)` teardown. |
| `src/control/Reactor.cpp` | `start()` aborts (stays idle) when `thermal_.enable(true)` returns `false`. |
| `include/app_config.hpp` | `kNtcMinPlausibleC = 0`, `kNtcMaxPlausibleC = 110`. |
| `src/system/AppRuntime.cpp` | Wire the plausible-window config into the Thermistor `Config`. |
| `tools/mock_server.py` | Simulate a disconnected/faulted probe (null telemetry + `heater_probe_fault` + refused Start). |
