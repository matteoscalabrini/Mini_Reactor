# Feature-Toggle Scaffold — Design

**Date:** 2026-06-25
**Branch:** `feature/feature-toggles` (off `feature/web-ui-run-control`)
**Roadmap item:** Conformity & Tech Debt → "Feature-toggle scaffold"
**Conformity driver:** `Claude.md` → "Toggle Behavior Contract"

## Goal

Satisfy `Claude.md`'s Toggle Behavior Contract for three features — **SD logging**,
**OLED UI**, and **PID autotune** — via compile-time flags that, when `false`:

- skip the module's initialization in `AppRuntime::begin()`,
- skip its runtime polling/actions in `AppRuntime::tick()`,
- make its control-API endpoints return **HTTP 503** with body
  `{"ok":false,"error":{"code":"feature_disabled","message":"..."}}`,
- are reported in startup logs as explicitly enabled/disabled,
- and have their web-UI sections **hidden** when off.

Defaults are all `true`, so the shipped behavior is unchanged.

## Non-goals (YAGNI)

- No `features/` directory relocation (separate, later Roadmap item — sequenced after
  this to avoid churn).
- No runtime/NVS-configurable toggles. These are compile-time `constexpr` only.
- No new toggles beyond the three named (OTA/MQTT/ESP-NOW come with those features).
- No OLED web-UI control surface (none exists today; nothing to hide).

## Design

### 1. Config surface — `AppConfig::Features`

New namespace in `include/app_config.hpp`:

```cpp
namespace Features {
static constexpr bool kEnableSdLogging = true;
static constexpr bool kEnableOledUi    = true;  // OLED blit only; encoder/buttons stay live
static constexpr bool kEnableAutotune  = true;
}  // namespace Features
```

Because these are compile-time constants, `src/net/WebInterface.cpp` simply
`#include "app_config.hpp"` and reads them directly — no plumbing through
`WebInterface::Config`, no runtime fields. Disabled branches compile and are
dead-code-eliminated, so the build passes with any flag set to `false`.

**Rejected alternative:** thread the flags through `WebInterface::Config`. Pure
overhead for a compile-time value; violates YAGNI / one-liner preference.

### 2. Per-feature gating

| Feature | `begin()` init | `tick()` runtime | API → 503 `feature_disabled` | Web UI |
|---|---|---|---|---|
| **SD logging** | skip `g_sd.begin()` + `g_sd.checkAndReport()` | skip log-append block + runs/latest cache block | `GET /api/v1/log`, `POST /api/v1/log/interval`, `POST /api/v1/sd/erase`, `GET /api/v1/runs`, `GET /api/v1/runs/{id}`, `POST /api/v1/runs/{id}/delete` | hide **DATA LOG** card |
| **OLED UI** | skip `g_display.begin()` only | skip `g_display.render()` only (keep `g_input.poll()` + `g_ui.handle()`) | none | none |
| **Autotune** | none (mode lives inside `g_thermal`) | none | `POST /api/v1/pid/autotune` | hide **Autotune** + **Cancel** buttons |

Notes:

- **`POST /api/v1/run` stays ungated.** Running the reactor is core control; SD logging
  only records it. The single SD call on the core path —
  `if (reactor_.running()) sd_.startRun(...)` in `WebInterface::applyPending()` — gets a
  one-line `AppConfig::Features::kEnableSdLogging &&` guard so a run can start without a
  card recording it.
- `SdLogger::mounted()` returns `false` until `begin()` succeeds, so skipping init makes
  every SD-derived status field (`system.sdMounted`, `storage.*`, `run.id/name`) degrade
  to false/null with no extra code.
- Autotune has no separate init or tick (it's a mode inside `g_thermal.update()`), so its
  gating is entirely: the 503 endpoint guard, the hidden UI buttons, the `features` map,
  and the startup log line.

### 3. 503 helper (WebInterface.cpp)

Add a file-local one-liner beside the existing `sendError`:

```cpp
// Returns true (and sends 503) when the feature is disabled; caller returns.
static bool featureGate(AsyncWebServerRequest* req, bool enabled) {
  if (enabled) return false;
  sendError(req, 503, "feature_disabled", "feature disabled at build time");
  return true;
}
```

Each gated handler begins with, e.g.:

```cpp
if (featureGate(req, AppConfig::Features::kEnableSdLogging)) return;
```

### 4. Startup logs

A `[FEAT]` block near the top of `AppRuntime::begin()`:

```
[FEAT] SD logging: enabled
[FEAT] OLED UI:    disabled
[FEAT] autotune:   enabled
```

### 5. Status JSON + SPA

`buildStatusJson()` in `AppRuntime.cpp` adds a top-level map:

```json
"features": { "sdLogging": true, "oledUi": false, "autotune": true }
```

New pure helper `data/core/features.js`:

```js
// Feature availability from /status. Missing map or key => enabled (back-compat
// with firmware that predates the features map).
export function featureEnabled(status, key) {
  const f = status && status.features;
  if (!f || f[key] == null) return true;
  return !!f[key];
}
```

`data/screens/settings.js` toggles `.hidden` on the SD (**DATA LOG**) card and on the
two autotune buttons inside its existing `store.subscribe(...)` handler — reactive, so it
works regardless of whether status arrives before or after the screen mounts.

### 6. Tests (TDD)

Write `test/ui/features.test.mjs` **first** (it imports `data/core/features.js`, which does
not yet exist → fails), then implement the helper:

- `featureEnabled({}, "sdLogging")` → `true` (no map)
- `featureEnabled({features:{}}, "sdLogging")` → `true` (key absent)
- `featureEnabled({features:{sdLogging:false}}, "sdLogging")` → `false`
- `featureEnabled({features:{sdLogging:true}}, "sdLogging")` → `true`
- `featureEnabled(null, "autotune")` → `true`

Run with `node test/ui/features.test.mjs` (matches the existing top-level-assert
convention; the file prints `features.test OK` on success).

## Validation checklist (before delivery)

1. `~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1` passes with all three flags `true`.
2. Build passes again with each flag individually set to `false` (at least one full build
   with all three `false`).
3. `node test/ui/features.test.mjs` passes; existing `test/ui/*.mjs` still pass.
4. `API.md` documents the 503 `feature_disabled` behavior for each gated endpoint.
5. `README.md` notes the feature toggles and their default state.
6. `Roadmap.md` "Feature-toggle scaffold" box ticked.
7. No changes outside the `Mini_Reactor` project.

## Files touched

- `include/app_config.hpp` — `AppConfig::Features` namespace.
- `src/system/AppRuntime.cpp` — `[FEAT]` startup logs; gate SD/OLED init + tick; `features` map in status JSON.
- `src/net/WebInterface.cpp` — `featureGate` helper; 503 guards on SD + autotune endpoints; `startRun` guard.
- `data/core/features.js` — new pure helper.
- `data/screens/settings.js` — hide DATA LOG card + autotune buttons when disabled.
- `test/ui/features.test.mjs` — new test.
- `API.md`, `README.md`, `Roadmap.md` — docs.
