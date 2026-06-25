# On-Device OLED Control UI — Design

**Date:** 2026-06-19
**Status:** Approved (pending implementation plan)
**Builds on:** the `Reactor` control surface (`include/control/Reactor.hpp`), `ReactorTelemetry`,
and the live status shape in `AppRuntime::buildStatusJson()`. Distinct from the **web** UI
(`2026-06-16-ui-control-app-design.md`) — this is the physical front panel.
**Reference:** `../Wheather_station_01` drives the same panel with U8g2
(`U8G2_SH1107_PIMORONI_128X128_*`).

## Goal

A physical front-panel UI on the 128×128 mono OLED so the reactor can be **monitored and
given core control at the bench, with no phone/browser**. Monitoring is the default view
(temperature first); a short list of control actions lives one click deep; and two hard
buttons give instant motor / motor+heater holds. Advanced configuration (PID tuning,
autotune, NTC calibration, disc parameters, WiFi onboarding) stays **web-only**.

## Decisions (locked during brainstorming)

1. **Scope:** monitor + core control. On-device actions = Start/Stop a run, adjust target
   temperature, adjust disc speed, motor/heater pause holds, and an Info screen.
   Everything else stays in the web UI.
2. **Interaction model:** monitoring **home dashboard**; **encoder push opens a control
   menu** (submenus for the adjustable values). Not a single-screen inline editor.
3. **Dashboard priorities:** liquid **temperature is the hero** (large font), plus disc
   **RPM** and **elapsed run time**; secondary: setpoint, heater %, run state, link status.
4. **Hard buttons (act globally, only meaningful during a run):**
   - **B1 (GPIO47)** = toggle **motor pause** — disc OFF, heater keeps running.
   - **B2 (GPIO48)** = toggle **full hold** — disc OFF **and** heater OFF.
   - **B3 (GPIO4)**  = toggle the **Info** screen.
5. **Pause semantics:** toggling pause/resume; the run stays **active** and the **elapsed
   timer keeps counting wall-time since Start** in all pause states. Only the menu's
   **Stop** ends a run. Safety trips override everything.
6. **No dedicated Back button** (all three buttons are spoken for) → **long-press the
   encoder = Home** (universal escape back to the dashboard), and the menu carries an
   explicit **Back** item.
7. **Architecture:** dedicated `ui/` module — `Display` + `InputManager` + `UiController`
   (screen state machine). The state machine is pure logic, unit-tested on the existing
   `env:native`/unity setup.
8. **Rendering:** U8g2 **full-buffer** (`_F_`), **hardware I2C** on the existing bus.

## Hardware & integration

> **Amended during on-device bring-up:** the OLED was originally specced on the *shared*
> primary I2C bus (GPIO1/2). On hardware, the SH1107 on that bus electrically disrupted the
> HUSB238 (it dropped to `device_not_found` whenever the OLED's SDA/SCL were connected,
> independent of bus clock). The OLED was moved to its **own bus on the free UART0 pins
> GPIO43/44**; the HUSB238 keeps the primary bus to itself. The hardware I2C1 peripheral
> (`Wire1`) would *not* drive those pads reliably (address ACKed, panel stayed dark, even with
> UART0 detached), so the OLED uses **software (bit-banged) I2C** there — which works. Table
> below reflects the shipped wiring.

| Element | Wiring | Notes |
|---|---|---|
| OLED | SH1107 128×128, I2C @ **0x3C**, on **SDA GPIO43 / SCL GPIO44** — dedicated **software I2C** (J6) | Isolated from HUSB238 on the primary bus |
| Encoder A / B | GPIO8 / GPIO9 | Quadrature, interrupt-driven |
| Encoder push (SW) | GPIO41 | Short press = select; long press (~600 ms) = Home |
| Button B1 / B2 / B3 | GPIO47 / GPIO48 / GPIO4 | Debounced (~25 ms) |

Pins per `docs/PINOUT.md` and `AppConfig::Ui`/board schematic. The display constructor is
`U8G2_SH1107_PIMORONI_128X128_F_SW_I2C` (bit-banged on GPIO43/44; full buffer ≈ 2 KB).
`Display::begin()` first `gpio_reset_pin()`s 43/44 to detach UART0, then u8g2 drives them.

**I2C buses (two, independent):** the primary `Wire` (GPIO1/2, 100 kHz) is brought up once in
`AppRuntime` for the HUSB238. The OLED is bit-banged software I2C on GPIO43/44 (no peripheral),
so the two never interact.

## Architecture / components

New module under `src/ui/` + `include/ui/`, mirroring the existing `src/<area>/` layout.

### 1. `Display` (hardware wrapper, on-device)
Owns the U8g2 object; exposes a small draw API the controller calls (`beginFrame()`,
`drawDashboard(view)`, `drawMenu(view)`, `drawEdit(view)`, `drawInfo(view)`,
`drawFaultBanner(view)`, `endFrame()`). No reactor knowledge — it receives plain view
structs (POD) describing what to render. Keeps font/layout choices isolated here.

### 2. `InputManager` (hardware wrapper, on-device)
Reads the encoder and buttons, emits a queue of discrete `UiEvent`s:
`RotateCW`, `RotateCCW`, `EncShortPush`, `EncLongPush`, `Btn1`, `Btn2`, `Btn3`.
Encoder A/B handled via `attachInterrupt` updating a volatile step counter; buttons polled
+ debounced. Detents coalesced so one physical click = one rotate event.

### 3. `UiController` (pure logic, host-tested)
The screen state machine. Inputs: a `ReactorSnapshot` (a POD it is handed each tick — the
fields it needs, decoupled from `ReactorTelemetry`) and a stream of `UiEvent`s. Outputs:
the current **view struct** (for `Display`) and **control intents** issued through a narrow
`ReactorControl` interface (see below). No `Arduino.h`, no U8g2 → compiles and runs under
`env:native`.

**`ReactorControl` interface** (so the controller is testable with a fake):
```
struct ReactorControl {
  virtual void startRun(float targetC, float rpm) = 0;
  virtual void stopRun() = 0;
  virtual void setTargetC(float c) = 0;
  virtual void setRpm(float rpm) = 0;
  virtual void setMotorPaused(bool) = 0;   // B1
  virtual void setFullHold(bool) = 0;       // B2
};
```
On device, a thin adapter forwards these to the real `Reactor`. In tests, a fake records the
calls and asserts the state machine drives them correctly.

### Glue in `AppRuntime`
- Construct `Display`, `InputManager`, the `Reactor` adapter, and `UiController`.
- In the main loop: pump `InputManager` → feed events to `UiController`; build a
  `ReactorSnapshot` from `g_reactor.telemetry()` (+ WiFi/SD/firmware for Info);
  `controller.tick(snapshot, events)`; then `display.render(controller.view())`.
- **Cadence:** input every loop; redraw throttled to ~15 Hz, decoupled from the PID.

## Screen state machine

States: `DASHBOARD`, `MENU`, `EDIT_TARGET`, `EDIT_SPEED`, `INFO`.
A **fault overlay** (`drawFaultBanner`) is drawn on top of any state when
`sensorFault || safetyTripped` is set.

```
DASHBOARD --EncShortPush--> MENU
DASHBOARD --Btn3--> INFO

MENU rotate -> move selection
MENU EncShortPush on:
   "Start run"/"Stop run" -> startRun(...)/stopRun(); stay in MENU
   "Target temp"          -> EDIT_TARGET
   "Speed"                -> EDIT_SPEED
   "Info"                 -> INFO
   "Back"                 -> DASHBOARD

EDIT_TARGET / EDIT_SPEED rotate -> adjust (clamped); EncShortPush -> apply+persist -> MENU
INFO EncShortPush or Btn3 -> DASHBOARD

ANY STATE:
   EncLongPush -> DASHBOARD (Home)
   Btn1 -> toggle motor pause      (no-op if not running)
   Btn2 -> toggle full hold        (no-op if not running)

DASHBOARD / INFO: rotate is ignored (navigation only matters in MENU/EDIT).
```

### Screens (128×128 mono)

**Dashboard** — temperature hero, rpm + elapsed time, state/link, pause indicator:
```
+----------------------------+
| RUNNING            wifi ok |
|                            |
|     36.2 C                 |   <- large hero font
|     target 36.0            |
|                            |
|  RPM  8.0     time 12:34   |
|  heater  42%   MOTOR PAUSE |   <- pause/hold reason when active
+----------------------------+
```
State line shows `STOPPED | RUNNING | MOTOR PAUSE | HOLD: M+H`. `time` = `elapsedSec`
formatted `mm:ss` (or `h:mm:ss` past an hour). When `sensorFault`, the hero temp shows
`-- C` and the fault banner overlays.

**Control menu** (encoder push from dashboard):
```
+----------------------------+
| CONTROL                    |
| > Start run                |   "Stop run" while running
|   Target temp     36.0 C   |
|   Speed           8.0 rpm  |
|   Info                     |
|   Back                     |
+----------------------------+
```

**Edit value** (Target / Speed):
```
+----------------------------+
| SET TARGET TEMP            |
|                            |
|        > 36.5 C <          |   rotate = +/- step
|                            |
|   push: apply   long: home |
+----------------------------+
```
Steps: target **0.5 °C**, speed **0.5 rpm**, clamped to `Reactor::Config`
(`minRpm`/`maxRpm`; target to a sane 0…`processMaxC` window). Apply calls
`setTargetC`/`setRpm` (live + persisted). Editing is allowed whether or not a run is active.

**Info:**
```
+----------------------------+
| INFO                       |
| WiFi  DFL IoT              |
| IP    10.77.77.250         |
| RSSI  -65 dBm              |
| SD    not mounted          |
| fw    1.0.0                |
+----------------------------+
```

**Fault banner** (overlay, any screen): e.g. `! LIQUID PROBE FAULT` /
`! SAFETY TRIP — HEATER OFF`, high-contrast, redrawn until cleared.

## Reactor API additions (pause/hold)

Pause is a new reactor concept. Add two independent toggle flags and integrate so existing
control paths are unaffected when both are false.

- **State:** `bool motorPaused_`, `bool fullHold_` (both cleared by `start()` and `stop()`).
- **Methods:** `setMotorPaused(bool)`, `setFullHold(bool)` (UI toggles by passing the negation
  of the current telemetry flag).
- **Effective outputs each `update()`:**
  - Motor runs only if `running_ && !motorPaused_ && !fullHold_`; otherwise disc disabled
    (`setDiscEnabled(false)`), with `rpm_` retained so resume restores speed.
  - Heater suppressed when `fullHold_` → `ThermalController` gains
    `setInhibited(bool)`: while inhibited, `update()` forces `applyOff()` (duty 0) but keeps
    `enabled_`/mode; on un-inhibit it **resets the PID integrator** to avoid a wind-up bump.
  - Safety trip still overrides (heater already forced off).
- **Telemetry:** add `bool motorPaused`, `bool fullHold` to `ReactorTelemetry`; the UI
  derives the dashboard state text. (The web `/api/v1/status` may surface these later — out
  of scope here, but the fields make it free.)

## Error / fault handling

- **No display on the bus** (I2C NACK at boot): log it over Serial and continue headless —
  the reactor and web UI run normally without the panel. UI render becomes a no-op.
- **Probe / safety faults:** surfaced as the fault overlay; pause buttons remain functional;
  Start is still gated by the existing pre-flight safety block in firmware.
- **Encoder/button bounce:** debounce in `InputManager`; detent coalescing prevents double
  steps.
- **I2C contention** with HUSB238: both are infrequent, low-rate transactions on the same
  bus; share one `Wire` instance, no concurrent access (single-threaded loop).

## Testing strategy

- **Host unit tests (`env:native`, unity)** for `UiController`: drive it with scripted
  `UiEvent` sequences + crafted `ReactorSnapshot`s against a **fake `ReactorControl`**, and
  assert (a) state transitions, (b) the exact control calls (e.g. push on "Start run" →
  `startRun(target,rpm)`; Btn1 while running → `setMotorPaused(true)` then `false`;
  EncLongPush from EDIT → returns to DASHBOARD without applying), (c) clamping at rpm/temp
  limits, (d) fault overlay selection.
- **Host unit tests** for the pause logic in `Reactor`/`ThermalController` (effective
  motor/heater enable truth table; integrator reset on resume).
- **On-device verification:** flash, confirm the dashboard renders live temperature, encoder
  navigates, edits apply (cross-check against `/api/v1/status`), B1/B2 holds take effect
  (disc stops; heater duty → 0 on B2), B3 toggles Info, long-press returns Home.

## Out of scope (YAGNI)

PID/autotune UI, NTC calibration UI, disc current/microstep/reverse config, WiFi onboarding,
run-duration entry (runs start open-ended; duration stays web-only), screensaver/dimming,
multi-language. Info screen is read-only.

## Open items to confirm at implementation

1. Exact U8g2 constructor enum for this panel (`U8G2_SH1107_PIMORONI_128X128_F_HW_I2C` vs.
   a `SEEED`/generic SH1107 variant) — confirm against the physical module + an I2C scan.
2. OLED I2C address (0x3C vs 0x3D).
3. Hero font choice/size that fits `nn.n C` within 128 px at high legibility.
4. Whether the encoder needs internal pull-ups or has external ones on-board.
