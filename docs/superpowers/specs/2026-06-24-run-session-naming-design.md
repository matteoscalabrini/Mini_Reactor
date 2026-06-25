# Run Session Naming — Design

Date: 2026-06-24
Status: Draft (for review)
Scope: Full stack — browser UI (`data/`), mock server (`tools/mock_server.py`), and ESP32 firmware (`src/`, `include/`).

## Summary

Let the operator give a run a human-readable **session name** when configuring it, and surface that name as the run's label in the History picker.

- A new optional **`SESSION NAME`** field is the first input of the Settings → RUN block.
- The name is held in the shared `runparams` (persisted in `localStorage`) and sent with the existing `POST /api/v1/run {action:"start"}` command.
- The backend stores the name **per run** and returns it as the run's `label` in `GET /api/v1/runs`. When no name is given, the label falls back to `Run <id>` (today's behavior).
- The name is fixed at start. There is no rename-after-the-fact in this round.

This directly takes up the *"Renaming/annotating saved runs"* item that the
[web-ui run-control/history design](2026-06-24-web-ui-run-control-history-design.md)
listed as a non-goal — but only the **name-at-start** half of it.

## Goals

1. Operators can label a run with a meaningful name before starting it.
2. The name appears wherever a run is identified (History picker, download filename).
3. The name persists with the run on the device, robust to power loss.
4. Reuse the existing run/label plumbing — `parseRunList` already renders `r.label`.

## Non-goals (this round)

- Renaming or editing a run's name after it has started (no rename endpoint/UI).
- Per-name validation beyond trim/truncate/control-char stripping (no uniqueness, no reserved-word checks).
- Searching/filtering History by name.
- Wall-clock timestamps or any other auto-generated naming.

## Decisions (locked during brainstorming)

| Topic | Decision |
|-------|----------|
| Build scope | Full stack: UI + mock server + firmware. |
| When set | At **start only**, from the RUN settings block. No mid-run or post-hoc rename. |
| Label format | Name **replaces** `Run N`; blank/absent name → `Run N` fallback. |
| Firmware storage | **Per-run sidecar file** `/runs/<id>.name`; written at start, read on list, deleted with the run. |
| Field lifetime | The input keeps its value between runs (persisted in `runparams`); the operator edits/clears it as desired. |
| Constraints | Optional. Trimmed; control characters (CR/LF/Tab and other C0) stripped; truncated to **32 characters**. |

## Data Model

- `runparams` shape extends to `{ targetC, rpm, durationMin, name }` with `name` default `""`.
- A run's `label` is computed by the backend as `name` if non-empty after sanitization, else `Run <id>`.
- Sanitization (applied once, server-side, on start): `trim()` → remove control chars → clamp to 32 chars. The same function is mirrored in the mock server. The UI applies a soft `maxlength=32` for feedback but does not trust the client.

## API Contract (delta)

### `POST /api/v1/run`

```jsonc
{ "action": "start", "targetC": 36, "rpm": 8, "durationMin": 0, "name": "Ethanol distillation" }
```

- `name` is **optional**; when absent or empty after sanitization, the run is unnamed (`Run <id>`).
- All existing `start` validation (rpm 0..30, targetC 0..55) is unchanged. An invalid `name` is never an error — it is sanitized, never rejected.
- `pause` / `resume` / `stop` payloads are unchanged.

### Runs

`GET /api/v1/runs` is unchanged in shape; `label` now carries the name when present:

```jsonc
{ "runs": [ { "id": 7, "label": "Ethanol distillation",
              "startedSec": 1234, "durationSec": 600, "bytes": 48211, "current": true } ] }
```

No new endpoint is introduced (set-at-start ⇒ no rename route).

### Status JSON addition

```jsonc
"run": {
  "active": true, "elapsedSec": 120, "remainingSec": null, "durationMin": 0,
  "id": 7, "name": "Ethanol distillation",   // null/absent when idle or unnamed
  "pause": { "motor": false, "heater": false }
}
```

`run.name` lets the live run show its label immediately without re-fetching `/runs`.

## Frontend Design (`data/`)

| File | Change |
|------|--------|
| `core/runparams.js` | Add `name: ""` to `DEFAULTS`; included in get/set/persist. No other logic change. |
| `screens/settings.js` | Add a `SESSION NAME` text input as the **first** field of the RUN block; `maxlength="32"`, placeholder. On `change`, `runParams.set({ name })`. Not pushed live mid-run (only meaningful at start). |
| `core/api.js` | `runStart(targetC, rpm, durationMin, name)` adds `name` to the body. |
| `core/runbar.js` | Start handler reads `runParams.get().name` and passes it to `runStart`. |
| `core/runs.js` | No change. `parseRunList` already maps `r.label` (which now carries the name). |
| `screens/history-section.js` | No structural change. Optional polish: set the Download link's `download` attribute to a filesystem-safe version of the selected run's label so saved CSVs are recognizable. |

The `SESSION NAME` field follows the existing `field(label, input)` pattern and the
current app styling (no new CSS required; it is a standard text input).

## Firmware Design (`src/`, `include/`)

### `SdLogger`

- `startRun(label)` — existing behavior (choose next id, open `/runs/<id>.csv`, write header). When `label` is non-empty, also write it as a single line to `/runs/<id>.name`.
- `listRuns()` — for each enumerated `/runs/<id>.csv`, read the sibling `/runs/<id>.name` if present and use its contents as the label; otherwise `Run <id>`.
- `endRun(save)` — on discard, delete both `/runs/<id>.csv` and `/runs/<id>.name`.
- `deleteRun(id)` — delete both files.
- `eraseAll()` — already clears `/runs` recursively, so sidecars are covered.
- A helper exposes the current run's name for the status builder (held in memory while a run is open).

Sidecar format: a single UTF-8 line, already sanitized (≤32 chars, no control chars).
File path: `/runs/<5-digit-zero-padded-id>.name` (e.g. `/runs/00007.name`).

### `WebInterface`

- The `/api/v1/run` `start` handler reads `o["name"] | ""`, sanitizes it (trim, strip control chars, truncate to 32), and carries it into the `Pending` struct (fixed-size char buffer to avoid heap churn in the async callback).
- `applyPending()` passes the sanitized name to `SdLogger::startRun`.
- The status JSON builder adds `run.name` from the current run (null when idle/unnamed).
- `GET /api/v1/runs` label population uses the sidecar via `SdLogger::listRuns()`.

### `Reactor` / control loop

- No behavioral change. The run lifecycle already calls `startRun` on start; the name simply rides along as an argument.

## Mock Server (`tools/mock_server.py`)

- `api_run` `start` accepts `name`, applies the same sanitization (trim / strip control chars / truncate 32), and stores it on the in-memory run record.
- `GET /api/v1/runs` returns `label = name or f"Run {id}"`.
- `status()` adds `run.name` for the current run.
- Mirrors the firmware so the browser walk-through reflects real behavior.

## Testing

- **Host unit test** — extend `test/ui/runparams.test.mjs`:
  - `name` defaults to `""`.
  - `set({ name })` persists and round-trips via the injected storage shim.
  - existing `targetC`/`rpm`/`durationMin` behavior is unaffected.
  - (`runs.js` label rendering is already covered by `test/ui/runs.test.mjs`; add a case asserting a provided `label` is preserved and a missing one falls back to `Run <id>` if not already present.)
- **Mock-server walk-through** (browser): type a name → Start → confirm History picker shows the name with `(live)`; start a second run with a blank name → confirm it shows `Run <id>`.
- **Firmware (hardware)**: per the saved hardware-validation setup — start a named run, verify `/runs/<id>.name` exists and the picker shows the name; stop-and-discard (or delete) and verify the sidecar is removed; start with no name and verify `Run <id>`.

## Risks / Open considerations

- **Extra SD I/O on list**: `listRuns()` now opens a small sidecar per run. Runs are few and the call is on-demand (picker/refresh), so the cost is acceptable.
- **Orphaned sidecars**: if a `.csv` is removed without its `.name` (e.g. external tooling), `listRuns()` simply ignores a sidecar with no matching CSV; a CSV with no sidecar falls back to `Run <id>`. Both degrade gracefully.
- **Encoding**: names are stored/returned as UTF-8; the 32-char truncation is by character intent but implemented as a byte clamp on the firmware — acceptable for ASCII lab names, with a note that multi-byte names may truncate slightly shorter.
- **Client/firmware drift**: the UI shows the last-entered name in the field even after a run starts; the authoritative label always comes from `/runs` / `run.name`. Matches the existing runparams-vs-telemetry behavior.
