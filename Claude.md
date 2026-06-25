Follow YAGNI principles, and one-liner solutions.

This firmware must stay modular and feature-toggle driven.


## Behaviours
- You will always give multiple choice options on device new feature brainstorming.
- The firmware must rely on Idf 5.5.x and relative arduino version. If firmware is not compliant, start a new branch and attempt a port.
- The user is primarly a visual type. He will understand better with visual examples, if needed spin up the brainstorming web server feature to make yourself clear.
- Keep the Roadmap.md always up to date. All entries must have next to them checkboxes, if a feature has been implemented, mark the checkbox as active. If the roadmap does not exist, start a session with the user to tacle it. 
- When you exceed 50% context utilization you will prompt the user to /compact or start a new sesssion /clear. - when doing this you provide the user a comprehensive prompt to continue the session.

## Modularity Requirements
- Keep reusable platform services always separable from product logic:
- Reusable services: WiFi, Web API, OTA, MQTT, ESP-NOW sync/binding, storage, identity.
- Product features must live in dedicated modules under `include/features/*` and `src/features/*`.
- Avoid direct cross-feature dependencies. Use small interfaces/callbacks where needed.

## Toggle Behavior Contract
- `true`: initialize module, expose runtime behavior, allow control APIs.
- `false`: skip initialization, skip runtime polling/actions, return explicit API errors where applicable (for example `503` + `feature_disabled`).
- Keep startup logs explicit about enabled/disabled state.

## API and UI Consistency
- API docs must match real exposed endpoints in `src/main.cpp`.
- If a feature is toggle-gated, document the disabled behavior in `API.md`.
- UI sections for a feature must be conditionally hidden or gracefully non-operational when that feature is disabled.

## Implementation Style
- Prefer incremental changes, not broad rewrites.
- Keep `main.cpp` orchestration-focused; move parsing/logic into modular headers/sources/inl when it grows.
- Preserve backward compatibility for existing endpoints unless intentionally versioned.

## Tooling Command Rule
- Always use full path for PlatformIO commands.
- Preferred command path: `~/.platformio/penv/bin/pio`.
- Do not rely on `pio`/`platformio` being present in shell `PATH`.

## Validation Checklist (Before Delivery)
- Build passes for the relevant product environment (`~/.platformio/penv/bin/pio run -e <env>`).
- Build passes with feature toggle set to `false` for changed module.
- Endpoint documentation updated (`README.md`, `API.md`).
- No changes made outside the `MINI_REACTOR` project.

## Partition Change Rule
- If partition layout changes, require full reflash sequence (substitute target env):
- Skipping erase after partition changes is not acceptable.