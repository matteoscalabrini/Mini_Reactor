# HUB Companion Device — Phase 3: Fermentation Monitoring UI — Design

**Date:** 2026-07-01
**Branch:** `feature/hub-ui-redesign` (off `feature/hub-espnow-link` — Phase 2 is not yet
merged to `main`, and this UI depends on its `HubLink` / screens)
**Roadmap item:** **HUB (companion device)** → "Phase 3 — redesigned fermentation
monitoring/control UI (LVGL)".
**Builds on:** Phase 2 ESP-NOW link (`docs/superpowers/specs/2026-06-30-hub-espnow-link-design.md`).
Telemetry, commands, pairing, link-loss and touch calibration already exist; this replaces the
placeholder `EspNowScreen` with the real product UI.
**Spans both firmwares:** HUB (`[env:hub]`) for the UI; a **small** reactor
(`[env:esp32-s3-devkitc-1]`) responder branch for hub-initiated START.
**Visual reference:** brainstorm mockups in `.superpowers/brainstorm/79000-1782910527/content/`
(`home-locked.html`, `screen-map.html`, `cal-cross.html`, `cal-pair.html`).
**Conformity drivers:** `Claude.md` → Modularity Requirements, Toggle Behavior Contract,
API/UI Consistency, YAGNI / incremental, lean LVGL build.

## Summary

Give the HUB a **pleasant, glanceable, brand-consistent** fermentation UI for the round
466×466 AMOLED. It is a **desk/bench companion** you glance at from across a room (or outside
an enclosed chamber) **and** carry to control the reactor. It **monitors** the bound reactor
and offers exactly three controls — **Start, Pause/Resume, Stop** — nothing more. All
parameter tuning stays on the reactor's web UI.

Design language (matches the reactor web SPA):
- **True-black background** — AMOLED pixels off = the dominant battery win on this device.
- **Brand palette:** cyan `#00b4d8` (accent/hero), red `#e22626` (stop/fault), slate
  `#6e8595` (secondary controls/labels), plus state amber `#ffb020`.
- **Type:** **Doto** (segmented, the web readout font) for numbers; **Inter** for text; the
  white **MINI REACTOR** wordmark as a small brand lockup.
- The whole design is deliberately built from **labels, images, and animated rectangles/circles
  only** — no arc/meter/chart/bar widgets — so the lean LVGL config stays lean.

Scope, locked during brainstorming:
- **Monitor + Start / Pause / Stop only.** No setpoint/RPM adjust on the hub (removed as overkill).
- **Hero temperature is state-colored:** cyan at/near target → amber while heating → red on
  safety trip / probe fault.
- **STOP and START both raise a confirm overlay** (a portable device must not stop/start a live
  ferment on an accidental tap).
- **START uses the reactor's current settings** (no values chosen on the hub).
- Two-screen **swipe carousel** (HOME ↔ DETAILS); pairing + calibration are full-screen flows.

## Goals

- Replace `EspNowScreen` with a small set of focused, independently-testable UI modules under
  `include/features/hub/ui/*` + `src/features/hub/ui/*`.
- Render live telemetry already decoded by `HubLink` (Phase 2) with **zero new transport code**;
  add only the missing **START** command path.
- Keep the LVGL widget set unchanged (LABEL + IMG + `lv_obj`, all already enabled). Add only
  **fonts** (subset Doto for the hero + one small text font) and **one image asset** (wordmark).
- Preserve the existing power/sleep behavior: any touch resets the idle timer; screens never
  fight `HubSleepLogic`.
- Feature-toggle clean: with `kEnableDisplay` / `kEnableEspNow` off, no screens initialize and
  the existing disabled-path logs are unchanged.

## Non-goals (YAGNI — deferred)

- **No parameter adjust on the hub** (target/RPM/current/duration). Web UI owns tuning.
- **No multi-reactor UI** — one bound peer, exactly as Phase 2.
- **No charts/graphs/history** on the hub (no CHART widget, no PSRAM framebuffer churn).
- **No gravity-aligned rotation** (separate deferred roadmap item; the lean UI renders upright).
- **No audio/haptic alerting** in v1 (codec stays powered down; a red SAFETY screen is the alert).
- **No new REST endpoints** — the reactor's web API is unchanged except the internal ESP-NOW
  START branch.

## Screens

All screens: pure-black (`#000`) full-round background, content kept within the safe inner
circle (corners are physically clipped).

### 1. HOME (carousel page 1 — monitor + controls)

```
        ●linked   [MINI REACTOR]   84%          <- top status strip
             SOURDOUGH #7 · 6h 02               <- run name + elapsed (slate)
                 30.4°                           <- HERO temp, Doto, STATE-COLORED
        target 30.0° · heat 42% · 120 rpm        <- subline (Inter, muted)
                  • ○                             <- cyan page dots (page 1 of 2)
          [ ❚❚ PAUSE ]   [ ■ STOP ]              <- slate ghost + red
```

- **Status strip:** `●linked` (cyan dot; dot→red + "SAFETY" text on fault) · white wordmark ·
  battery %.
- **Hero temperature** color rule (one shared helper, unit-tested on the native host):
  - **red** `#ff4d4f` if `safetyTripped || probeFault || tempValid == false`,
  - else **amber** `#ffb020` if `heaterActive` (actively heating toward target),
  - else **cyan** `#00b4d8` (at/near target / steady).
- **PAUSE** toggles: shows `❚❚ PAUSE` when running → `▶ RESUME` when paused (`sendPause`/resume).
- **STOP** → STOP confirm overlay.
- When **no run is active**, HOME renders the **IDLE** variant (below) instead of PAUSE/STOP.

### 2. DETAILS (carousel page 2 — deeper telemetry)

A compact key→value list (Inter labels, Doto values), same strip on top, page dots `○ •`:

| Row          | Source                                             |
|--------------|----------------------------------------------------|
| Heater       | `heaterTempC_d` (÷10)                               |
| Process max  | `processMaxC_d` (÷10)                               |
| Load (SG)    | `load` (StallGuard; `--` when `kNullI16`)          |
| Remaining    | `remainingSec` (`∞` when `-1` run-until-stopped)    |
| Battery      | AXP `batteryVoltageMv` / `batteryPercent` (+charging)|
| Link         | `sweepChannel()` / `linkAlive()` → `ch6 · ok`      |

### 3. Overlays / transient states

- **STOP confirm** — `STOP RUN?` + `SOURDOUGH #7 · 6h 02`; `[ CANCEL ]` (ghost) / `[ YES, STOP ]`
  (red). Cancel returns to the page you came from.
- **START confirm** — from IDLE: `START RUN?` + a one-line summary of the reactor's current
  settings (`30.0° · 120 rpm · until stop`); `[ CANCEL ]` / `[ ▶ START ]` (cyan).
- **SAFETY state** — red status strip (`● SAFETY`), red hero temp, red caption
  (`OVER-TEMP CUTOFF · heater forced off · check reactor`), single red `■ STOP`. Driven by
  `safetyTripped` / `worstSeverity == critical`.
- **IDLE state** (HOME when `runActive == false`) — `idle` strip, dim slate ambient temp,
  caption `ambient · ready`, single cyan `▶ START`.

### 4. PAIRING (full-screen, replaces Phase 2 unpaired/searching)

- **Unpaired:** centered wordmark, `NOT PAIRED` (Doto, slate), instruction
  "Tap **PAIR**, then press **Pair HUB** on the reactor.", single cyan `🔗 PAIR` button.
- **Searching:** radar **pulse animation** (`lv_anim` scaling/​fading concentric `lv_obj`
  circles) around a cyan hub dot, `SEARCHING · CH n` live channel (`sweepChannel()`),
  instruction "Open the reactor's pairing window.", `[ CANCEL ]`.

### 5. CALIBRATION (full-screen, first-boot + reactor-triggered recalibrate)

Refines the existing `CalibrationScreen`:
- **MINI REACTOR wordmark dead-center**; title "Touch the dots"; `STEP n / 4` (Doto) below it.
- **Four small round targets on the cross (cardinal N/E/S/W)**, each ~48 px:
  - **active** = solid cyan disc + pulsing ring (`lv_anim`), digit centered,
  - **done** = transparent with green `#37d67a` ring + check,
  - **pending** = dashed slate ghost with its step number.
- The solver / persistence / release-gate logic is **unchanged** (Phase 2); this is a visual
  reskin driven by the same `showStep(index, count, x, y)` contract, extended to also render the
  done/pending siblings.

## Navigation & interaction

- **Swipe left/right** switches HOME ↔ DETAILS via LVGL gesture events
  (`LV_EVENT_GESTURE` → `lv_indev_get_gesture_dir`) on the active screen — **core LVGL, no new
  widget config**. Two persistent screen objects; the gesture swaps which is loaded (or a single
  screen with two child containers toggled). Page dots reflect the active page.
- **Overlays** are separate top-layer screens/containers shown over the current page; CANCEL
  restores the prior page. Only one overlay at a time.
- **PAUSE/RESUME** is a single toggle button bound to `HubLink::sendPause(mode)` /
  `sendPause(0)`, driven by the `motorPaused` / `fullHold` flags.
- Every touch (tap, swipe, button) already flows through the Phase-2 poll that sets
  `g_lastActivityMs`, so the deep-sleep idle timer keeps working untouched.

## START command (the one cross-firmware change)

The hub has no adjust UI, so START must mean **"start a run with the reactor's currently
configured settings."** The reactor's responder currently maps `Opcode::RunStart` to
`web_.cmdRunStart(targetC, rpm, durationMin, name)` with **explicit** values.

- **HUB:** add `HubLink::sendStart()` that sends `Opcode::RunStart` with a new
  `kCmdFlagUseCurrent` bit set (payload target/rpm/duration/name left zero/empty).
- **Reactor:** in `EspNowResponder`, when `RunStart` arrives with `kCmdFlagUseCurrent`, call a
  "start with current config" path instead of the explicit one — i.e. begin a run using the
  reactor's live thermal setpoint + disc rpm + its configured/default duration (run-until-stopped
  when unset). Web-initiated RunStart is unaffected (bit clear → existing explicit path).
- This is **backward-compatible** and adds one flag + one branch; no protocol struct changes.
- The Ack path (`accepted | rejected | feature_disabled`) is reused so the hub can toast/relabel
  START on rejection.

## Firmware implications (lean LVGL)

**Widgets:** none added. LABEL + IMG + `lv_obj` (rounded rects/circles) + `lv_anim` cover
everything (dots, buttons, pulse/radar animations, calibration targets). `LV_USE_ARC/BAR/METER/
CHART/SLIDER` stay `0`.

**Fonts:** currently only `lv_font_montserrat_12`. Add, via the LVGL font converter, C-array fonts:
- **Doto hero** at the large hero size (~90–96 px) **subset to `0-9 . ° C -`** only → small flash.
- **Doto small** (~14 px, digits + basic latin) for DETAILS values / step counter.
- **Inter/Montserrat ~14** for labels/instructions — reuse the built-in Montserrat (already
  compiled) to save flash, or add a subset Inter for exact brand match (decide in plan).
- Fonts are gated behind the display feature; document flash delta in the plan.

**Image asset:** the white **MINI REACTOR wordmark** as one LVGL image (C array, small
monochrome; RGB565 or A8 recolorable). `LV_USE_IMG` is already `1`.

**Memory:** no new full-screen framebuffers beyond LVGL's existing draw buffer; the hero font
subset and one small image are the only meaningful flash additions.

## Modularity & toggles (Claude.md)

- New product-feature modules under `include/features/hub/ui/` + `src/features/hub/ui/`:
  - `HomeScreen` (monitor + controls + IDLE/SAFETY variants),
  - `DetailsScreen`,
  - `PairScreen` (renamed/reworked from `EspNowScreen`),
  - reuse + reskin `CalibrationScreen`,
  - a tiny `HubUi` coordinator (owns the two carousel pages + overlays + gesture routing),
    called from `HubRuntime::tick()` in place of the current `EspNowScreen::update`.
- No cross-feature deps: screens receive plain view-structs (as `EspNowScreen::View` does today);
  `HubRuntime` maps `HubLink` telemetry + AXP state into those structs.
- **Toggle contract:** `kEnableDisplay == false` → no screens created (existing guard);
  `kEnableEspNow == false` → BringupScreen path unchanged; calibration gated by
  `kEnableTouchCalibration` as today.

## Testing

- **Native host tests** (`[env:native]`, no LVGL): pure view-mapping helpers — the hero
  **color-state** function (temp/flags → cyan/amber/red), remaining-time formatter (`∞`/`h m`),
  battery/link formatters, and the `kCmdFlagUseCurrent` encode/decode in `SyncCodec`. These are
  the logic worth unit-testing; LVGL rendering is verified on hardware.
- **Reactor responder test:** `RunStart + kCmdFlagUseCurrent` routes to the "current settings"
  path; without the bit, the explicit path is unchanged.
- **On-device:** build `[env:hub]`, verify swipe HOME↔DETAILS, state colors on a live heat cycle,
  STOP/START confirm round-trips (Ack), pairing radar, and the recalibrate flow (unchanged).
- **Toggle build:** `[env:hub]` with `kEnableDisplay=false` still links and boots (panel asleep).

## Validation checklist (before delivery)

- `~/.platformio/penv/bin/pio run -e hub` green; `-e esp32-s3-devkitc-1` green (START branch).
- `-e hub` with `kEnableDisplay=false` builds and boots.
- `Roadmap.md` Phase 3 checkbox flipped; `API.md` note that hub START maps to "reactor current
  settings" (no new endpoint).
- No changes outside `MINI_REACTOR`.

## Open decisions — all resolved in brainstorming

| Decision            | Resolution                                                        |
|---------------------|-------------------------------------------------------------------|
| Hub purpose         | Desk/bench companion: monitor **+** Start/Pause/Stop; carryable.  |
| Home direction      | **C** (app + controls), refined.                                  |
| Logo treatment      | White **MINI REACTOR wordmark** centered in the top strip.        |
| Hero color          | **State-colored** (cyan / amber / red).                           |
| STOP safety         | **Tap → confirm** overlay (START too).                            |
| Parameter adjust    | **Dropped** — web UI owns tuning.                                 |
| START behavior      | **Reactor's current settings** (new `kCmdFlagUseCurrent`).        |
| Calibration targets | **Round dots, cross (cardinal) layout, small**, wordmark center.  |

## Risks

- **Large hero font flash:** mitigated by subsetting Doto to `0-9 . ° C -`.
- **Doto glyph coverage:** confirm the woff2→LVGL conversion includes `°`; fall back to drawing
  the degree ring as a tiny `lv_obj` if the glyph is absent.
- **Gesture vs. button tap conflict:** ensure horizontal swipe is only recognized outside the
  button hit areas (buttons consume the press) so PAUSE/STOP don't eat swipes and vice-versa.
