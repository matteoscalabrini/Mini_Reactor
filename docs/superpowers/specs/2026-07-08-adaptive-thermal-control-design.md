# Adaptive gain-scheduled thermal control — design

**Date:** 2026-07-08
**Status:** design, pending review
**Problem:** the liquid PID overshoots badly — setpoint 36 °C reached ~40 °C on a real
run. Root cause is structural, not a bad tune: the heater has real thermal mass and the
DS18B20 reads the *liquid*, so the loop drives near-100 % duty almost to setpoint; the tiny
`Ki` (0.0015) unwinds too slowly to stop pushing heat past target, the derivative is far too
weak to brake, and there is no approach limiting. A single fixed gain set cannot both heat
fast and land softly.

## Goal

Dynamic control that heats quickly, lands on setpoint without overshoot, and self-tunes —
without the instability risk of continuous online re-identification (explicitly out of scope,
YAGNI for a slow bath). Overshoot is worse than undershoot for the culture: the design biases
toward approaching slowly and **never exceeding setpoint**, accepting a slightly longer settle.

## Approach: two gain regimes selected by distance-to-setpoint

Let `error = setpoint − pv` and `band` = approach-band width (default 3.0 °C).

| Region | Condition | Gains | Max duty (ceiling) |
|--------|-----------|-------|--------------------|
| Heat | `error ≥ band` | heat set (aggressive) | `dutyMax` (1.0) |
| Approach | `0 ≤ error < band` | lerp(hold→heat by `error/band`) | lerp(`holdDutyCap`→`dutyMax`) |
| Hold / over | `error < 0` (pv ≥ setpoint) | hold set (gentle) | `holdDutyCap` (default 0.6) |

Gains and the duty ceiling **blend linearly** across the band — no lurch at the boundary.

**Why this removes the overshoot:** the tapered duty ceiling is passed to the PID as its
`outMax`. The existing conditional-integration anti-windup then automatically bleeds the
integrator down as the ceiling shrinks near setpoint — so the element sheds its stored heat
*before* the liquid arrives, instead of dumping it afterward. No explicit "integral reset on
crossing" is needed (it risks droop); the tapered ceiling + anti-windup give the soft landing.

Two supporting changes in the PID core:
- **Derivative-on-measurement** (`−(pv − prevPv)/dt`) instead of derivative-on-error: brakes on
  rising liquid temperature and removes the derivative kick when the setpoint is changed live.
- Gains are set **per sample** from the schedule before each `step()`.

## Components (preserves the modular, host-testable pattern)

- **`GainSchedule` (new, pure header `include/features/control/GainSchedule.hpp`)** — given
  `(setpoint, pv)` returns effective `{kp, ki, kd, dutyCeil}` for this sample. No LVGL/Arduino
  deps; host-unit-tested like `PidController`/`RelayAutotune`. One clear job: map distance-to-
  setpoint → gains + ceiling.
- **`PidController`** — add `setDerivativeOnMeasurement(bool)` (default `false`, so existing
  behaviour and tests are unchanged) and track `prevPv_`.
- **`ThermalController`** — owns a `GainSchedule`; in Auto mode each sample: query schedule →
  `pid_.setGains(...)` → `pid_.step(setpoint, pv, dt, dutyMin, dutyCeil)`. Persists both gain
  sets + a `tuned` flag in NVS. Falls back to today's single-PID path when the feature toggle
  is off.

## Dynamic autotune (dual-set, auto-run, safe)

The relay autotune stays, with three changes:

1. **Derives both gain sets** from the identified `Ku`/`Tu`: `hold` = the current conservative
   formula; `heat` = `hold` scaled up (`heatKpScale` default 1.8×, with proportionally less
   integral influence) for fast ramp.
2. **Auto-runs once when untuned:** if NVS `tuned == false`, starting a run first enters
   Autotune, then applies the derived schedule and sets `tuned = true`. Gated by
   `kEnableAdaptiveThermal`; a manual trigger (existing `/pid/autotune`) still works and re-runs
   on demand.
3. **Tunes at a reduced setpoint** (`setpoint − tuneMarginC`, default 3 °C) so the relay
   oscillation stays safely *below* the target — the culture never sees overshoot during
   commissioning. Bath dynamics at 33 vs 36 °C are effectively identical, so the gains transfer.
   **← confirm at review: acceptable to auto-run the tune with culture present, at reduced
   setpoint? Alternative is manual-tune-only with gain-scheduling applied to existing gains.**

## Config (`AppConfig::Thermal`) + toggle

New constants: `heatKp/Ki/Kd`, `holdKp/Ki/Kd` (hold defaults = today's 0.08/0.0015/0.4; heat =
scaled), `kApproachBandC = 3.0`, `kHoldDutyCap = 0.6`, `kTuneMarginC = 3.0`, `kHeatKpScale = 1.8`.
New toggle `AppConfig::Features::kEnableAdaptiveThermal` (default `true`); `false` → exact
current fixed-PID behaviour (toggle contract: init/runtime/logs).

## API / telemetry

`/api/v1/status` `thermal.pid` gains: currently the single active set — extend to add
`regime` (`"heat"|"approach"|"hold"`), `dutyCeil`, `tuned`, and both gain sets under
`pid.schedule`. Document in `API.md` including the disabled-toggle behaviour. UI change is a
later, separate task (this spec is firmware-only).

## Testing (native `pio test -e native`)

- `test_gain_schedule` (new): heat/approach/hold selection, linear blend at band edges, ceiling
  taper, over-setpoint clamp.
- `test_pid_controller` (extend): derivative-on-measurement produces no setpoint-change kick;
  legacy derivative-on-error path unchanged.
- `test_relay_autotune` (extend): dual-set derivation (heat = scaled hold), reduced-setpoint tune.

## Non-goals

- Continuous online model re-identification / self-adapting-every-cycle control (instability
  risk, unneeded for a slow bath).
- UI redesign for the new fields (separate task).
- No deployment to the in-progress "Mini FC 3" run; this ships for the next run.

## Rollback

Feature toggle `kEnableAdaptiveThermal = false` restores the current controller exactly. No
partition change; standard flash.
