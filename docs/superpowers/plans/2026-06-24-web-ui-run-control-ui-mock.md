# Web UI Run Control + Merged Monitor/History (UI + Mock) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restructure the browser control surface — persistent Start/Pause/Resume + Stop bar next to a 2-tab page selector, Run folded into Settings, History merged under Monitor with a run picker — and back it with per-run CSV files in the mock server.

**Architecture:** Keep the existing hash-router (`data/app.js`) and store-subscribed shell. Add DOM-free pure modules (`runparams`, `runstate`, `runs`) that are unit-tested in Node, plus DOM modules (`runbar`, `history-section`) verified in the browser. Extend the mock server (`tools/mock_server.py`) with a per-run registry and the new `/api/v1/run` actions + `/api/v1/runs` endpoints so the whole flow works against the mock.

**Tech Stack:** Vanilla ES modules (`data/`), Node `node:assert/strict` test scripts (`test/ui/*.test.mjs`), Python `aiohttp` mock server (`tools/mock_server.py`).

## Global Constraints

- API contract (mock must match exactly; firmware Plan 2 will implement the same):
  - `POST /api/v1/run` actions: `start {targetC,rpm,durationMin}`, `pause {target: "motor"|"heater"|"all"}`, `resume`, `stop {data: "save"|"discard"}`.
  - `GET /api/v1/runs` → `{ runs: [{ id, label, startedSec, durationSec, bytes, current }] }`.
  - `GET /api/v1/runs/<id>` → `text/csv` for that run.
  - `POST /api/v1/runs/<id>/delete` → `{ ok: true }`.
  - Status JSON `run` gains `id` (int|null) and `pause: { motor: bool, heater: bool }`.
  - `GET /api/v1/log` retained as an alias for the latest run's CSV.
- Validation ranges (unchanged): `rpm` 0..30, `targetC` 0..55.
- Pause = maintenance hold: run timer never stops, CSV logging continues; `resume` clears all holds.
- Auto-stop on duration timeout = **save**.
- Run IDs are integer indices; labels are `Run <id>`.
- CSV header (identical everywhere): `t_ms,running,liquid_c,heater_c,setpoint_c,heater_pct,rpm,load,fault,safety`.
- Pure modules (`runparams`, `runstate`, `runs`) MUST NOT touch the DOM or `localStorage` at import time (storage is injected) so they run under Node.
- Tabs are exactly: `Monitor`, `Settings`.
- Run params defaults: `targetC 36`, `rpm 8`, `durationMin 0`.

---

### Task 1: Mock server — per-run registry, run actions, runs endpoints

**Files:**
- Modify: `tools/mock_server.py`

**Interfaces:**
- Consumes: nothing (first task).
- Produces (HTTP contract used by all later UI tasks):
  - `POST /api/v1/run` with `action ∈ {start,pause,resume,stop}` as in Global Constraints.
  - `GET /api/v1/runs`, `GET /api/v1/runs/{id}`, `POST /api/v1/runs/{id}/delete`.
  - Status `run.id` + `run.pause = {motor,heater}`.

- [ ] **Step 1: Add registry state + helpers**

In `tools/mock_server.py`, add `"pause": {"motor": False, "heater": False}, "runId": None,` to the `state` dict (next to `"motorTestUntil"`). Then, immediately after the `clients = set()` / `_alarm_since = {}` lines, add:

```python
runs = []          # run records: {id,label,started,rows,durationSec,current}
_run_seq = [0]
_tick = [0]
CSV_HEADER = "t_ms,running,liquid_c,heater_c,setpoint_c,heater_pct,rpm,load,fault,safety"


def _start_run():
    _run_seq[0] += 1
    rec = {"id": _run_seq[0], "label": f"Run {_run_seq[0]}",
           "started": time.monotonic(), "rows": [], "durationSec": None, "current": True}
    runs.append(rec)
    state["runId"] = rec["id"]
    state["startMs"] = rec["started"]
    return rec


def _current_run():
    for r in runs:
        if r["current"]:
            return r
    return None


def _finalize_run(save):
    r = _current_run()
    if not r:
        return
    r["current"] = False
    r["durationSec"] = int(time.monotonic() - r["started"])
    if not save:
        runs.remove(r)
    state["runId"] = None


def _run_csv(r):
    return CSV_HEADER + "\n" + "\n".join(r["rows"]) + ("\n" if r["rows"] else "")
```

- [ ] **Step 2: Surface pause + run id in `status()`**

In `status()`, change the `rpm` line to honor a motor pause, and extend the `run` block. Replace:

```python
    testing = time.monotonic() < state["motorTestUntil"]
    rpm = 8.0 if testing else (state["rpm"] if state["running"] else 0.0)
```

with:

```python
    testing = time.monotonic() < state["motorTestUntil"]
    rpm = 8.0 if testing else (state["rpm"] if state["running"] else 0.0)
    if state["pause"]["motor"] and not testing:
        rpm = 0.0
```

and replace the `"run": {...}` entry in the returned dict with:

```python
        "run": {"active": state["running"], "elapsedSec": elapsed,
                "remainingSec": remaining, "durationMin": state["durationMin"],
                "id": state["runId"],
                "pause": {"motor": state["pause"]["motor"],
                          "heater": state["pause"]["heater"]}},
```

- [ ] **Step 3: Record rows + honor pause in `simulate()`**

Replace the whole `if s["running"]:` branch (the block that computes `err`, `hold`, `heaterPct`, `tempC`, `heaterTempC`, `loadBias`, `load`, and the duration check) with:

```python
        _tick[0] += 1
        if s["running"]:
            err = s["targetC"] - s["tempC"]
            if s["pause"]["heater"]:
                s["heaterPct"] = 0.0
            else:
                hold = 100.0 * 0.02 * (s["targetC"] - AMBIENT) / 0.6
                s["heaterPct"] = max(0.0, min(100.0, hold + 14.0 * err))
            duty = s["heaterPct"] / 100.0
            s["tempC"] += (0.6 * duty - 0.02 * (s["tempC"] - AMBIENT)) * dt
            s["heaterTempC"] = s["tempC"] + 18.0 * duty
            if s["pause"]["motor"]:
                s["load"] = 0
            else:
                s["loadBias"] = min(120.0, s["loadBias"] + 0.05)
                s["load"] = int(max(0, 380 - s["loadBias"] + random.uniform(-8, 8)))
            rec = _current_run()
            if rec is not None and _tick[0] % 8 == 0:  # ~every 2 s
                rpm_now = 0.0 if s["pause"]["motor"] else s["rpm"]
                t_ms = int((time.monotonic() - rec["started"]) * 1000)
                rec["rows"].append(
                    f'{t_ms},1,{s["tempC"]:.2f},{s["heaterTempC"]:.1f},{s["targetC"]:.1f},'
                    f'{s["heaterPct"]:.0f},{rpm_now:.1f},{s["load"]},0,'
                    f'{1 if s["safetyTripped"] else 0}')
            if s["durationMin"] > 0 and \
               time.monotonic() - s["startMs"] >= s["durationMin"] * 60:
                s["running"] = False
                _finalize_run(True)
```

(The `else:` idle branch below it stays unchanged.)

- [ ] **Step 4: Rewrite `api_run` for start/pause/resume/stop**

Replace the entire `api_run` function with:

```python
async def api_run(req):
    b = await req.json()
    action = b.get("action")
    if action == "start":
        rpm = float(b.get("rpm", 8))
        targetC = float(b.get("targetC", 36))
        if rpm < 0 or rpm > 30:
            return web.json_response(
                {"ok": False, "error": {"code": "out_of_range",
                                        "message": "rpm must be 0..30"}}, status=400)
        if targetC < 0 or targetC > 55:
            return web.json_response(
                {"ok": False, "error": {"code": "out_of_range",
                                        "message": "targetC must be 0..55"}}, status=400)
        rpm = clamp_rpm(rpm)
        if state["heaterProbeFault"]:
            return web.json_response({"ok": True})
        state.update(running=True, targetC=targetC, rpm=rpm, rpmSetpoint=rpm,
                     durationMin=int(b.get("durationMin", 0)))
        state["pause"] = {"motor": False, "heater": False}
        _start_run()
        return web.json_response({"ok": True})
    if action == "pause":
        target = b.get("target")
        if target not in ("motor", "heater", "all"):
            return web.json_response(
                {"ok": False, "error": {"code": "invalid_request",
                                        "message": "target must be motor|heater|all"}}, status=400)
        if state["running"]:
            if target in ("motor", "all"):
                state["pause"]["motor"] = True
            if target in ("heater", "all"):
                state["pause"]["heater"] = True
        return web.json_response({"ok": True})
    if action == "resume":
        state["pause"] = {"motor": False, "heater": False}
        return web.json_response({"ok": True})
    if action == "stop":
        data = b.get("data")
        if data not in ("save", "discard"):
            return web.json_response(
                {"ok": False, "error": {"code": "invalid_request",
                                        "message": "data must be save|discard"}}, status=400)
        if state["running"]:
            state["running"] = False
            state["pause"] = {"motor": False, "heater": False}
            _finalize_run(data == "save")
        return web.json_response({"ok": True})
    return web.json_response(
        {"ok": False, "error": {"code": "invalid_request",
                                "message": "action must be start|pause|resume|stop"}}, status=400)
```

- [ ] **Step 5: Add runs endpoints + repoint `api_log`**

Replace the entire `api_log` function with:

```python
async def api_log(req):
    if runs:
        return web.Response(text=_run_csv(runs[-1]), content_type="text/csv")
    return web.Response(text=CSV_HEADER + "\n", content_type="text/csv")


async def api_runs(req):
    out = []
    for r in runs:
        dur = r["durationSec"] if r["durationSec"] is not None \
            else int(time.monotonic() - r["started"])
        out.append({"id": r["id"], "label": r["label"], "startedSec": int(r["started"]),
                    "durationSec": dur, "bytes": len(_run_csv(r).encode()),
                    "current": r["current"]})
    return web.json_response({"runs": out})


async def api_run_csv(req):
    rid = int(req.match_info["id"])
    for r in runs:
        if r["id"] == rid:
            return web.Response(text=_run_csv(r), content_type="text/csv")
    return web.json_response(
        {"ok": False, "error": {"code": "not_found", "message": "no such run"}}, status=404)


async def api_run_delete(req):
    rid = int(req.match_info["id"])
    for r in list(runs):
        if r["id"] == rid:
            runs.remove(r)
            if state["runId"] == rid:
                state["runId"] = None
            return web.json_response({"ok": True})
    return web.json_response(
        {"ok": False, "error": {"code": "not_found", "message": "no such run"}}, status=404)
```

- [ ] **Step 6: Register routes + seed the demo run**

In `main()`, add these three routes right after the existing `app.router.add_get("/api/v1/log", api_log)` line:

```python
    app.router.add_get("/api/v1/runs", api_runs)
    app.router.add_get("/api/v1/runs/{id}", api_run_csv)
    app.router.add_post("/api/v1/runs/{id}/delete", api_run_delete)
```

Replace the `on_start` coroutine body with:

```python
    async def on_start(a):
        state["pause"] = {"motor": False, "heater": False}
        _start_run()  # anchor the demo's initial running session as Run 1
        a["sim"] = asyncio.create_task(simulate())
```

- [ ] **Step 7: Restart the mock and verify the contract**

Restart the mock (use the interpreter that has `aiohttp` — the one already running it; install with `pip install aiohttp` if needed):

```bash
pkill -f mock_server.py; sleep 1
( cd /Users/matteo/Documents/Github/Mini_Reactor && nohup python3 tools/mock_server.py >/tmp/mock.log 2>&1 & )
sleep 2
curl -s localhost:8000/api/v1/status | python3 -c "import sys,json;d=json.load(sys.stdin);print('run.id',d['run']['id'],'pause',d['run']['pause'])"
curl -s localhost:8000/api/v1/runs | python3 -c "import sys,json;print('runs',[r['id'] for r in json.load(sys.stdin)['runs']])"
curl -s -XPOST localhost:8000/api/v1/run -H 'content-type: application/json' -d '{"action":"pause","target":"motor"}' ; echo
sleep 1; curl -s localhost:8000/api/v1/status | python3 -c "import sys,json;d=json.load(sys.stdin);print('after pause motor: rpm',d['disc']['rpm'],'pause',d['run']['pause'])"
curl -s -XPOST localhost:8000/api/v1/run -H 'content-type: application/json' -d '{"action":"resume"}' ; echo
curl -s -XPOST localhost:8000/api/v1/run -H 'content-type: application/json' -d '{"action":"stop","data":"save"}' ; echo
curl -s -XPOST localhost:8000/api/v1/run -H 'content-type: application/json' -d '{"action":"start","targetC":36,"rpm":8,"durationMin":0}' ; echo
curl -s -XPOST localhost:8000/api/v1/run -H 'content-type: application/json' -d '{"action":"stop","data":"discard"}' ; echo
curl -s localhost:8000/api/v1/runs | python3 -c "import sys,json;print('runs after discard',[r['id'] for r in json.load(sys.stdin)['runs']])"
curl -s localhost:8000/api/v1/runs/1 | head -2
```

Expected: `run.id 1 pause {'motor': False, 'heater': False}`; `runs [1]`; after pause `rpm 0.0 pause {'motor': True, ...}`; every command prints `{"ok": true}`; the discarded run (id 2) is absent so `runs after discard [1]`; `GET /runs/1` prints the CSV header line then a data row.

- [ ] **Step 8: Commit**

```bash
git add tools/mock_server.py
git commit -m "feat(mock): per-run files, pause/resume, stop-data, /runs endpoints

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: `runparams.js` — shared run parameters

**Files:**
- Create: `data/core/runparams.js`
- Test: `test/ui/runparams.test.mjs`

**Interfaces:**
- Consumes: nothing.
- Produces: `createRunParams(storage?) -> { get(): {targetC,rpm,durationMin}, set(patch): void, subscribe(fn): ()=>void }`; named export `runParams` (singleton using `localStorage` when present, else in-memory).

- [ ] **Step 1: Write the failing test**

Create `test/ui/runparams.test.mjs`:

```javascript
import assert from "node:assert/strict";
import { createRunParams } from "../../data/core/runparams.js";

function shim() {
  const m = {};
  return { getItem: (k) => (k in m ? m[k] : null), setItem: (k, v) => { m[k] = v; }, _m: m };
}

// defaults when storage empty
let s = shim();
let rp = createRunParams(s);
assert.deepEqual(rp.get(), { targetC: 36, rpm: 8, durationMin: 0 });

// set merges + persists
rp.set({ targetC: 42 });
assert.equal(rp.get().targetC, 42);
assert.equal(rp.get().rpm, 8);
assert.ok(s._m["reactor.runparams"].includes("42"));

// a fresh instance reads persisted values
let rp2 = createRunParams(s);
assert.equal(rp2.get().targetC, 42);

// subscribe fires immediately + on change, unsub stops it
let seen = [];
const unsub = rp2.subscribe((v) => seen.push(v.targetC));
assert.equal(seen[0], 42);
rp2.set({ targetC: 30 });
assert.equal(seen[1], 30);
unsub();
rp2.set({ targetC: 99 });
assert.equal(seen.length, 2);

console.log("runparams.test OK");
```

- [ ] **Step 2: Run test to verify it fails**

Run: `node test/ui/runparams.test.mjs`
Expected: FAIL — `Cannot find module .../data/core/runparams.js`.

- [ ] **Step 3: Write minimal implementation**

Create `data/core/runparams.js`:

```javascript
// Shared run parameters (target / rpm / duration), persisted so the global
// Start button works from any page. Storage is injectable for host tests.
const DEFAULTS = { targetC: 36, rpm: 8, durationMin: 0 };
const KEY = "reactor.runparams";

function memoryStorage() {
  const m = {};
  return { getItem: (k) => (k in m ? m[k] : null), setItem: (k, v) => { m[k] = v; } };
}

export function createRunParams(storage) {
  const store = storage
    || (typeof globalThis !== "undefined" && globalThis.localStorage) || memoryStorage();
  const subs = new Set();
  let state = { ...DEFAULTS };
  try {
    const raw = store.getItem(KEY);
    if (raw) state = { ...DEFAULTS, ...JSON.parse(raw) };
  } catch (_) { /* keep defaults */ }

  const get = () => ({ ...state });
  function set(patch) {
    state = { ...state, ...patch };
    try { store.setItem(KEY, JSON.stringify(state)); } catch (_) { /* ignore */ }
    for (const fn of subs) fn(get());
  }
  function subscribe(fn) { subs.add(fn); fn(get()); return () => subs.delete(fn); }
  return { get, set, subscribe };
}

export const runParams = createRunParams();
```

- [ ] **Step 4: Run test to verify it passes**

Run: `node test/ui/runparams.test.mjs`
Expected: PASS — prints `runparams.test OK`.

- [ ] **Step 5: Commit**

```bash
git add data/core/runparams.js test/ui/runparams.test.mjs
git commit -m "feat(ui): runparams shared store with injectable persistence

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: `runstate.js` — run-bar state + table slicing

**Files:**
- Create: `data/core/runstate.js`
- Test: `test/ui/runstate.test.mjs`

**Interfaces:**
- Consumes: nothing.
- Produces: `deriveRunbar(status) -> { mode: "idle"|"running"|"paused", held: "motor"|"heater"|"all"|null, startBlocked: boolean }`; `visibleRows(rows, expanded, cap=10) -> array`.

- [ ] **Step 1: Write the failing test**

Create `test/ui/runstate.test.mjs`:

```javascript
import assert from "node:assert/strict";
import { deriveRunbar, visibleRows } from "../../data/core/runstate.js";

assert.deepEqual(deriveRunbar({ run: { active: false } }),
  { mode: "idle", held: null, startBlocked: false });
assert.deepEqual(deriveRunbar({ run: { active: true } }),
  { mode: "running", held: null, startBlocked: false });
assert.deepEqual(deriveRunbar({ run: { active: true, pause: { motor: true } } }),
  { mode: "paused", held: "motor", startBlocked: false });
assert.equal(deriveRunbar({ run: { active: true, pause: { heater: true } } }).held, "heater");
assert.equal(deriveRunbar({ run: { active: true, pause: { motor: true, heater: true } } }).held, "all");
assert.equal(deriveRunbar({ run: { active: false },
  thermal: { safety: { probe: { resistanceOhms: null } } } }).startBlocked, true);
assert.equal(deriveRunbar({ run: { active: false },
  thermal: { safety: { tripped: true } } }).startBlocked, true);
assert.equal(deriveRunbar(undefined).mode, "idle");

const rows = Array.from({ length: 25 }, (_, i) => i);
assert.equal(visibleRows(rows, false).length, 10);
assert.equal(visibleRows(rows, true).length, 25);
assert.deepEqual(visibleRows(rows, false), [0, 1, 2, 3, 4, 5, 6, 7, 8, 9]);
assert.deepEqual(visibleRows(null, false), []);

console.log("runstate.test OK");
```

- [ ] **Step 2: Run test to verify it fails**

Run: `node test/ui/runstate.test.mjs`
Expected: FAIL — `Cannot find module .../data/core/runstate.js`.

- [ ] **Step 3: Write minimal implementation**

Create `data/core/runstate.js`:

```javascript
// Pure derivation for the run-control bar + history table. No DOM.
export function deriveRunbar(status) {
  const run = (status && status.run) || {};
  const safety = ((status && status.thermal) || {}).safety || {};
  const pause = run.pause || {};
  const active = !!run.active;
  const motor = !!pause.motor;
  const heater = !!pause.heater;
  const paused = active && (motor || heater);
  const mode = !active ? "idle" : paused ? "paused" : "running";
  const held = !paused ? null : (motor && heater) ? "all" : motor ? "motor" : "heater";
  const probeFault = !!(safety.probe && safety.probe.resistanceOhms == null);
  const startBlocked = probeFault || !!safety.tripped;
  return { mode, held, startBlocked };
}

// Rows are newest-first; show the first `cap` unless expanded.
export function visibleRows(rows, expanded, cap = 10) {
  if (!Array.isArray(rows)) return [];
  return expanded ? rows.slice() : rows.slice(0, cap);
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `node test/ui/runstate.test.mjs`
Expected: PASS — prints `runstate.test OK`.

- [ ] **Step 5: Commit**

```bash
git add data/core/runstate.js test/ui/runstate.test.mjs
git commit -m "feat(ui): runstate derivation + table row slicing

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: `runs.js` — saved-run list helpers

**Files:**
- Create: `data/core/runs.js`
- Test: `test/ui/runs.test.mjs`

**Interfaces:**
- Consumes: nothing.
- Produces: `parseRunList(json) -> [{id,label,startedSec,durationSec,bytes,current}]`; `latestRunId(runs) -> number|null`.

- [ ] **Step 1: Write the failing test**

Create `test/ui/runs.test.mjs`:

```javascript
import assert from "node:assert/strict";
import { parseRunList, latestRunId } from "../../data/core/runs.js";

assert.deepEqual(parseRunList({}), []);
assert.deepEqual(parseRunList(null), []);
assert.equal(latestRunId([]), null);
assert.equal(latestRunId(null), null);

const list = parseRunList({ runs: [
  { id: 3, startedSec: 10, durationSec: 60, bytes: 100 },
  { id: 5, label: "Run 5", current: true },
] });
assert.equal(list.length, 2);
assert.equal(list[0].label, "Run 3");      // label fallback
assert.equal(list[0].current, false);
assert.equal(list[1].current, true);

assert.equal(latestRunId(list), 5);        // current wins

const noCur = parseRunList({ runs: [{ id: 2 }, { id: 7 }, { id: 4 }] });
assert.equal(latestRunId(noCur), 7);       // else max id

console.log("runs.test OK");
```

- [ ] **Step 2: Run test to verify it fails**

Run: `node test/ui/runs.test.mjs`
Expected: FAIL — `Cannot find module .../data/core/runs.js`.

- [ ] **Step 3: Write minimal implementation**

Create `data/core/runs.js`:

```javascript
// Pure helpers for the saved-run list. No DOM.
export function parseRunList(json) {
  const runs = (json && json.runs) || [];
  return runs.map((r) => ({
    id: r.id,
    label: r.label || `Run ${r.id}`,
    startedSec: r.startedSec == null ? null : r.startedSec,
    durationSec: r.durationSec == null ? null : r.durationSec,
    bytes: r.bytes || 0,
    current: !!r.current,
  }));
}

// Default picker selection: the current run if any, else the highest id.
export function latestRunId(runs) {
  if (!runs || !runs.length) return null;
  const cur = runs.find((r) => r.current);
  if (cur) return cur.id;
  return runs.reduce((m, r) => (r.id > m ? r.id : m), runs[0].id);
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `node test/ui/runs.test.mjs`
Expected: PASS — prints `runs.test OK`.

- [ ] **Step 5: Commit**

```bash
git add data/core/runs.js test/ui/runs.test.mjs
git commit -m "feat(ui): saved-run list parsing + latest-run selection

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 5: API client extensions

**Files:**
- Modify: `data/core/api.js:16-17`

**Interfaces:**
- Consumes: existing `get`, `post` from `api.js`.
- Produces: `runStart(targetC,rpm,durationMin)`, `runPause(target)`, `runResume()`, `runStop(data)`, `listRuns()`, `runCsvUrl(id)`, `deleteRun(id)`.

- [ ] **Step 1: Replace the run helpers**

In `data/core/api.js`, replace these two lines:

```javascript
export const runStart = (targetC, rpm, durationMin) => post("/api/v1/run", { action: "start", targetC, rpm, durationMin });
export const runStop = () => post("/api/v1/run", { action: "stop" });
```

with:

```javascript
export const runStart = (targetC, rpm, durationMin) => post("/api/v1/run", { action: "start", targetC, rpm, durationMin });
export const runPause = (target) => post("/api/v1/run", { action: "pause", target });
export const runResume = () => post("/api/v1/run", { action: "resume" });
export const runStop = (data) => post("/api/v1/run", { action: "stop", data });
export const listRuns = () => get("/api/v1/runs");
export const runCsvUrl = (id) => `/api/v1/runs/${id}`;
export const deleteRun = (id) => post(`/api/v1/runs/${id}/delete`);
```

- [ ] **Step 2: Verify the module still parses**

Run: `node -e "import('./data/core/api.js').then(m=>console.log(typeof m.runPause, typeof m.listRuns, m.runCsvUrl(7)))"`
Expected: prints `function function /api/v1/runs/7`.

- [ ] **Step 3: Commit**

```bash
git add data/core/api.js
git commit -m "feat(ui): api helpers for pause/resume/stop-data + runs

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 6: Persistent run-control bar + nav restructure

**Files:**
- Create: `data/core/runbar.js`
- Modify: `data/index.html:16-21` (nav block)
- Modify: `data/app.js:5-10` (imports + screens map) and `data/app.js:57-59` (mount runbar)
- Modify: `data/app.css` (append run-bar + nav-row styles)

**Interfaces:**
- Consumes: `store` (`subscribe`, `getStatus`), `runParams.get`, `deriveRunbar`, `runStart/runPause/runResume/runStop`, `el`, `toast`.
- Produces: `mount(container) -> teardown` (default usage from `app.js`).

- [ ] **Step 1: Create the run-bar module**

Create `data/core/runbar.js`:

```javascript
import * as store from "./store.js";
import { runParams } from "./runparams.js";
import { deriveRunbar } from "./runstate.js";
import { runStart, runPause, runResume, runStop } from "./api.js";
import { el, toast } from "./ui.js";

const _wraps = [];
function closeAllMenus() { for (const w of _wraps) w._close && w._close(); }

// A button that toggles an attached dropdown of { label, kind?, onClick } items.
function menuButton(label, cls, items) {
  const menu = el("div", { class: "rb-menu", hidden: "" },
    ...items.map((it) => el("button", {
      class: `rb-item ${it.kind || ""}`,
      onclick: (e) => { e.stopPropagation(); close(); it.onClick(); },
    }, it.label)));
  const btn = el("button", { class: cls, onclick: (e) => {
    e.stopPropagation();
    const wasClosed = menu.hasAttribute("hidden");
    closeAllMenus();
    if (wasClosed) menu.removeAttribute("hidden");
  } }, label);
  function close() { menu.setAttribute("hidden", ""); }
  const wrap = el("div", { class: "rb-wrap" }, btn, menu);
  wrap._close = close;
  _wraps.push(wrap);
  return { wrap, btn };
}

export function mount(container) {
  const start = el("button", { class: "go rb-btn" }, "▶ Start");
  const resume = el("button", { class: "go rb-btn" }, "▶ Resume");
  const held = el("span", { class: "rb-held" }, "");
  const pause = menuButton("⏸ Pause ▾", "rb-btn warn", [
    { label: "Pause motor", onClick: () => runPause("motor") },
    { label: "Pause heater", onClick: () => runPause("heater") },
    { label: "Pause all", onClick: () => runPause("all") },
  ]);
  const stop = menuButton("■ Stop ▾", "stop rb-btn", [
    { label: "Cancel", onClick: () => {} },
    { label: "Stop & save data", onClick: () => runStop("save") },
    { label: "Stop & discard data", kind: "danger",
      onClick: () => { if (confirm("Discard this run's data?")) runStop("discard"); } },
  ]);

  start.addEventListener("click", async () => {
    const p = runParams.get();
    if (p.rpm < 0 || p.rpm > 30) return toast("rpm must be 0–30");
    if (p.targetC < 0 || p.targetC > 55) return toast("target must be 0–55 °C");
    const res = await runStart(p.targetC, p.rpm, p.durationMin);
    if (!res.ok) toast(res.body && res.body.error ? res.body.error.message : "start failed");
  });
  resume.addEventListener("click", () => runResume());

  container.append(start, pause.wrap, resume, held, stop.wrap);
  document.addEventListener("click", closeAllMenus);

  function update(status) {
    const { mode, held: heldWhich, startBlocked } = deriveRunbar(status);
    start.hidden = mode !== "idle";
    pause.wrap.hidden = mode !== "running";
    resume.hidden = mode !== "paused";
    stop.wrap.querySelector("button").disabled = mode === "idle";
    start.disabled = startBlocked;
    start.title = startBlocked ? "Heater probe faulted — Start is blocked" : "";
    held.textContent = heldWhich ? `${heldWhich} held` : "";
    if (mode === "idle") closeAllMenus();
  }

  const unsub = store.subscribe(update);
  update(store.getStatus());
  return () => { unsub(); document.removeEventListener("click", closeAllMenus); };
}
```

- [ ] **Step 2: Restructure the nav in `index.html`**

In `data/index.html`, replace the `<nav class="tabs" id="tabs"> ... </nav>` block (the four `<a class="tab">` lines) with:

```html
  <div class="navrow">
    <nav class="tabs" id="tabs">
      <a class="tab" href="#monitor" data-tab="monitor">Monitor</a>
      <a class="tab" href="#settings" data-tab="settings">Settings</a>
    </nav>
    <div class="runbar" id="runbar"></div>
  </div>
```

- [ ] **Step 3: Wire `app.js` — drop run/history screens, mount the bar**

In `data/app.js`, replace:

```javascript
import * as monitor from "./screens/monitor.js";
import * as run from "./screens/run.js";
import * as history from "./screens/history.js";
import * as settings from "./screens/settings.js";

const screens = { monitor, run, history, settings };
```

with:

```javascript
import * as monitor from "./screens/monitor.js";
import * as settings from "./screens/settings.js";
import { mount as mountRunbar } from "./core/runbar.js";

const screens = { monitor, settings };
```

Then, at the bottom of the file, change:

```javascript
route();
get("/api/v1/status").then((r) => { if (r.body && r.body.apiVersion) store.setStatus(r.body); });
startWs();
```

to:

```javascript
mountRunbar(document.getElementById("runbar"));
route();
get("/api/v1/status").then((r) => { if (r.body && r.body.apiVersion) store.setStatus(r.body); });
startWs();
```

- [ ] **Step 4: Append styles to `app.css`**

Add to the end of `data/app.css`:

```css
.navrow{display:flex;justify-content:space-between;align-items:center;gap:12px;flex-wrap:wrap;margin:14px 0}
.tabs{margin:0}
.runbar{display:flex;align-items:center;gap:8px;flex-wrap:wrap}
.rb-btn{padding:9px 16px}
.rb-wrap{position:relative;display:inline-block}
.rb-held{font-size:11px;font-weight:700;letter-spacing:.08em;color:var(--warn);text-transform:uppercase}
.rb-menu{position:absolute;right:0;top:calc(100% + 6px);z-index:30;background:var(--surface);
  border:1px solid var(--border);border-radius:10px;box-shadow:var(--shadow);padding:6px;
  min-width:190px;display:flex;flex-direction:column;gap:2px}
.rb-menu[hidden]{display:none}
.rb-item{width:100%;text-align:left;border:none;background:transparent;padding:9px 12px;
  border-radius:7px;font-weight:600}
.rb-item:hover{background:var(--surface-2)}
.rb-item.danger{color:var(--bad)}
.rb-tools{display:flex;gap:8px;align-items:center}
.rb-picker{font:inherit;padding:5px 8px;border:1px solid var(--border);border-radius:8px;
  background:var(--surface-2);max-width:160px}
.rb-more{margin-top:8px}
.history-wrap{margin-top:14px}
```

- [ ] **Step 5: Verify in the browser**

With the mock running, open `http://localhost:8000`. Confirm: two tabs (Monitor, Settings); the run bar shows `⏸ Pause ▾` and `■ Stop ▾` (demo run is active). Click `Pause ▾` → `Pause motor`; the bar switches to `▶ Resume` with a `motor held` hint and Monitor's agitator rpm drops to 0. Click `Resume`. Click `Stop ▾` → `Stop & save data`; the bar shows `▶ Start`, `Stop ▾` disabled. Click `Start`; the bar returns to Pause/Stop. (Note: the Run inputs live in Settings — Task 7. Start uses the persisted defaults 36/8/0 until then.)

- [ ] **Step 6: Commit**

```bash
git add data/core/runbar.js data/index.html data/app.js data/app.css
git commit -m "feat(ui): persistent run-control bar + 2-tab nav

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 7: Fold Run into Settings, remove the Run screen

**Files:**
- Modify: `data/screens/settings.js`
- Delete: `data/screens/run.js`

**Interfaces:**
- Consumes: `runParams` (`get`, `set`), `api.setpoint`, `hhmmss`, `el`, `flashApplied`, `sec`, `field`, `store`.
- Produces: a `RUN` section rendered as the first block of Settings.

- [ ] **Step 1: Add imports**

In `data/screens/settings.js`, replace:

```javascript
import { el, toast, flashApplied } from "../core/ui.js";
```

with:

```javascript
import { el, toast, flashApplied, hhmmss } from "../core/ui.js";
import { runParams } from "../core/runparams.js";
```

- [ ] **Step 2: Build the RUN section**

In `mount(root)`, immediately before the line `root.append(wifi, sd, pid, cal, motor, sys);`, insert:

```javascript
  // RUN (relocated from the old Run page; Start/Stop live in the global run bar)
  const p0 = runParams.get();
  const inTarget = el("input", { type: "number", step: "0.5", min: "0", max: "55", value: p0.targetC });
  const inRpm = el("input", { type: "number", step: "1", min: "0", max: "30", value: p0.rpm });
  const inDur = el("input", { type: "number", min: "0", value: p0.durationMin,
    placeholder: "minutes (0 = until stopped)" });
  const stateV = el("div", { class: "v" }, "IDLE");
  const elapsedV = el("div", { class: "v" }, "—"), remainV = el("div", { class: "v" }, "—");

  inTarget.addEventListener("change", async () => {
    runParams.set({ targetC: +inTarget.value });
    if ((store.getStatus()?.run || {}).active) {
      const r = await api.setpoint({ targetC: +inTarget.value }); flashApplied(inTarget, r.ok);
    }
  });
  inRpm.addEventListener("change", async () => {
    runParams.set({ rpm: +inRpm.value });
    if ((store.getStatus()?.run || {}).active) {
      const r = await api.setpoint({ rpm: +inRpm.value }); flashApplied(inRpm, r.ok);
    }
  });
  inDur.addEventListener("change", () => runParams.set({ durationMin: +inDur.value }));

  const runSec = sec("RUN",
    field("TARGET TEMPERATURE °C", inTarget),
    field("AGITATOR SPEED rpm", inRpm),
    field("DURATION min", inDur),
    el("div", { class: "stat3" },
      el("div", {}, el("div", { class: "lbl" }, "STATE"), stateV),
      el("div", {}, el("div", { class: "lbl" }, "ELAPSED"), elapsedV),
      el("div", {}, el("div", { class: "lbl" }, "REMAINING"), remainV)));
```

Then change `root.append(wifi, sd, pid, cal, motor, sys);` to:

```javascript
  root.append(runSec, wifi, sd, pid, cal, motor, sys);
```

- [ ] **Step 3: Update the RUN status line on telemetry**

Inside the existing `store.subscribe((d) => { ... })` callback in `settings.js`, add at the end of the callback body (just before the closing `});`):

```javascript
    const run = d.run || {};
    const paused = run.pause && (run.pause.motor || run.pause.heater);
    stateV.textContent = run.active ? (paused ? "PAUSED" : "RUNNING") : "IDLE";
    elapsedV.textContent = hhmmss(run.elapsedSec || 0);
    remainV.textContent = run.remainingSec == null
      ? (run.active ? "∞" : "—") : hhmmss(run.remainingSec);
```

- [ ] **Step 4: Delete the old Run screen**

Run: `git rm data/screens/run.js`
Expected: `rm 'data/screens/run.js'`. (Its tab and import were already removed in Task 6.)

- [ ] **Step 5: Verify in the browser**

Reload `http://localhost:8000#settings`. Confirm: `RUN` is the first card with target/rpm/duration inputs and a STATE/ELAPSED/REMAINING row that tracks the live run. Edit `AGITATOR SPEED` while running → a `✓ applied` note appears and Monitor's rpm follows. Edit target/rpm while idle, then click the run bar's `Start` → the run begins with those values. Confirm there is no Run tab and `#run` falls back to Monitor.

- [ ] **Step 6: Commit**

```bash
git add data/screens/settings.js
git commit -m "feat(ui): fold Run controls into Settings as the first block

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 8: Merge History under Monitor

**Files:**
- Create: `data/screens/history-section.js`
- Modify: `data/screens/monitor.js`
- Delete: `data/screens/history.js`

**Interfaces:**
- Consumes: `el`, `fixed`, `hhmmss`, `clear`, `parseCsv`, `render`, `listRuns`, `runCsvUrl`, `parseRunList`, `latestRunId`, `visibleRows`.
- Produces: `mount(root) -> null` rendering the HISTORY card (picker + chart + 10-row/expand table + Download/Refresh).

- [ ] **Step 1: Create the history-section component**

Create `data/screens/history-section.js`:

```javascript
import { el, fixed, hhmmss, clear } from "../core/ui.js";
import { parseCsv } from "../core/csv.js";
import { render } from "../core/chart.js";
import { listRuns, runCsvUrl } from "../core/api.js";
import { parseRunList, latestRunId } from "../core/runs.js";
import { visibleRows } from "../core/runstate.js";

export function mount(root) {
  const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
  svg.setAttribute("width", "100%"); svg.setAttribute("height", "170");
  svg.setAttribute("preserveAspectRatio", "none");
  const picker = el("select", { class: "rb-picker", onchange: () => load() });
  const dl = el("a", { class: "btn", href: "#", download: "run.csv" }, "⬇ Download CSV");
  const refresh = el("button", { class: "ghost",
    onclick: async () => { await refreshRuns(); load(); } }, "Refresh");
  const status = el("span", { class: "muted" }, "—");
  const tableWrap = el("div", {});
  let expanded = false;

  root.append(
    el("div", { class: "card" },
      el("h3", {}, "HISTORY", el("span", { class: "rb-tools" }, picker, dl, refresh)),
      el("div", { class: "chart" }, svg,
        el("div", { class: "legend" }, el("span", {}, el("i", {}), "Liquid °C"),
          el("span", {}, el("i", { class: "sp" }), "Setpoint °C"),
          el("span", { style: "margin-left:auto" }, status))),
      tableWrap));

  const idx = (header, name) => header.indexOf(name);
  const downsample = (arr, max = 200) => {
    const stride = Math.ceil(arr.length / max) || 1;
    return arr.filter((_, i) => i % stride === 0);
  };
  const selectedId = () => (picker.value ? +picker.value : null);

  async function refreshRuns() {
    const prev = selectedId();
    const r = await listRuns().catch(() => null);
    const runs = parseRunList(r && r.body);
    clear(picker);
    if (!runs.length) { picker.append(el("option", { value: "" }, "— no runs —")); return; }
    runs.sort((a, b) => b.id - a.id).forEach((run) =>
      picker.append(el("option", { value: run.id }, run.label + (run.current ? " (live)" : ""))));
    const want = (prev && runs.some((x) => x.id === prev)) ? prev : latestRunId(runs);
    picker.value = String(want);
  }

  function renderTable(header, rows) {
    clear(tableWrap);
    const iT = idx(header, "t_ms"), iL = idx(header, "liquid_c"), iS = idx(header, "setpoint_c"),
          iH = idx(header, "heater_c"), iD = idx(header, "heater_pct"), iR = idx(header, "rpm"),
          iLoad = idx(header, "load"), iF = idx(header, "fault"), iSaf = idx(header, "safety");
    const newestFirst = rows.slice().reverse();
    const shown = visibleRows(newestFirst, expanded, 10);
    const head = el("tr", {}, ["TIME", "LIQUID", "HEATER", "SETPT", "DUTY%", "RPM", "LOAD", "FLAG"]
      .map((h) => el("th", {}, h)));
    const body = shown.map((r) => el("tr", {},
      el("td", {}, hhmmss(Math.round((+r[iT] || 0) / 1000))),
      el("td", {}, fixed(+r[iL], 1)), el("td", {}, fixed(+r[iH], 1)), el("td", {}, fixed(+r[iS], 1)),
      el("td", {}, fixed(+r[iD], 0)), el("td", {}, fixed(+r[iR], 1)), el("td", {}, r[iLoad] || "—"),
      el("td", {}, (r[iF] === "1" ? "FAULT" : r[iSaf] === "1" ? "TRIP" : "ok"))));
    tableWrap.append(el("table", { class: "htable" }, el("thead", {}, head), el("tbody", {}, body)));
    if (newestFirst.length > 10) {
      tableWrap.append(el("button", { class: "ghost rb-more",
        onclick: () => { expanded = !expanded; renderTable(header, rows); } },
        expanded ? "Show less ▲" : `Show all (${newestFirst.length}) ▾`));
    }
  }

  async function load() {
    const id = selectedId();
    if (id == null) {
      status.textContent = "No runs"; clear(tableWrap);
      while (svg.firstChild) svg.removeChild(svg.firstChild);
      dl.removeAttribute("href"); return;
    }
    dl.href = runCsvUrl(id); dl.download = `run_${id}.csv`;
    refresh.disabled = true; refresh.textContent = "…"; status.textContent = "loading…";
    try {
      const res = await fetch(runCsvUrl(id)).catch(() => null);
      const text = res && res.ok ? await res.text() : "";
      const { header, rows } = parseCsv(text);
      if (!rows.length) {
        status.textContent = "No log data"; clear(tableWrap);
        while (svg.firstChild) svg.removeChild(svg.firstChild); return;
      }
      const iL = idx(header, "liquid_c"), iS = idx(header, "setpoint_c");
      const valid = rows.filter((r) => !Number.isNaN(+r[iL]) && !Number.isNaN(+r[iS]));
      const ds = downsample(valid);
      render(svg, { t: ds.map((r) => +r[iL]), s: ds.map((r) => +r[iS]) });
      status.textContent = `${rows.length} rows`;
      expanded = false;
      renderTable(header, rows);
    } finally {
      refresh.disabled = false; refresh.textContent = "Refresh";
    }
  }

  (async () => { await refreshRuns(); load(); })();
  return null;
}
```

- [ ] **Step 2: Mount the section inside Monitor**

In `data/screens/monitor.js`, add to the imports at the top:

```javascript
import * as historySection from "./history-section.js";
```

Then replace the trailing:

```javascript
  return unsub;
}
```

with:

```javascript
  const histRoot = el("div", { class: "history-wrap" });
  root.append(histRoot);
  const histTeardown = historySection.mount(histRoot);

  return () => { unsub(); if (histTeardown) histTeardown(); };
}
```

- [ ] **Step 3: Delete the old History screen**

Run: `git rm data/screens/history.js`
Expected: `rm 'data/screens/history.js'`. (Its tab and import were already removed in Task 6.)

- [ ] **Step 4: Verify in the browser**

Reload `http://localhost:8000#monitor`. Below the live cards, confirm a `HISTORY` card with a run picker (defaulting to the live run) plus `⬇ Download CSV` and `Refresh` in the top-right. The chart and a ≤10-row table render; if the run has >10 rows a `Show all (N) ▾` toggle expands/collapses it. Stop & save the run, click `Refresh`, and confirm the finished run is selectable and `Download CSV` fetches `/api/v1/runs/<id>`. Confirm there is no History tab and `#history` falls back to Monitor.

- [ ] **Step 5: Commit**

```bash
git add data/screens/history-section.js data/screens/monitor.js
git commit -m "feat(ui): merge History under Monitor with a run picker

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 9: Full verification pass

**Files:**
- None (verification only).

**Interfaces:**
- Consumes: everything above.
- Produces: confidence the feature is whole.

- [ ] **Step 1: Run all UI unit tests**

Run: `for f in test/ui/*.test.mjs; do echo "== $f"; node "$f" || exit 1; done`
Expected: each prints its `... OK` line (including the new `runparams`, `runstate`, `runs`), exit status 0.

- [ ] **Step 2: Grep for dangling references to removed screens**

Run: `grep -rn "screens/run.js\|screens/history.js\|#run\b\|data-tab=\"run\"\|data-tab=\"history\"" data/ || echo "clean"`
Expected: `clean` (no references to the deleted Run/History screens remain).

- [ ] **Step 3: End-to-end browser walkthrough**

With the mock running, at `http://localhost:8000` walk the full flow once: Settings → set target/rpm/duration; run bar `Start`; `Pause ▾ → Pause heater` (Monitor heater duty → 0, timer keeps counting, bar shows `Resume` + `heater held`); `Resume`; `Pause ▾ → Pause all`; `Resume`; `Stop ▾ → Stop & save data`; Monitor HISTORY → Refresh, select the saved run, expand the table, Download CSV; `Start` a new run; `Stop ▾ → Stop & discard data`; Refresh HISTORY and confirm the discarded run is gone. No console errors.

- [ ] **Step 4: Final commit (if any docs/notes changed)**

If nothing changed in this task, skip. Otherwise:

```bash
git add -A
git commit -m "test(ui): verification pass for run control + merged history

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Self-Review

**Spec coverage:**
- Persistent Start/Pause/Resume + Stop bar next to page selector → Tasks 6 (bar), 1 (backend).
- Pause menu (motor/heater/all), timer never stops, logging continues → Tasks 1, 6; verified Task 9 Step 3.
- Resume restores all holds → Tasks 1, 6.
- Stop menu (cancel / save / discard), per-run files → Tasks 1, 6.
- Run folded into Settings as first block; Run tab/screen removed → Task 7 (+ tab removal in Task 6).
- History merged under Monitor; picker default latest; Download+Refresh top-right; 10-row table + expand → Tasks 8, 3, 4; History tab/screen removed in Tasks 6/8.
- Tabs collapse to Monitor·Settings → Task 6.
- Run params in localStorage, global Start from any page → Tasks 2, 6, 7.
- `run.pause` + `run.id` telemetry; `/api/v1/runs*`; `/log` alias → Task 1.
- Host unit tests (runparams/runstate/runs) → Tasks 2–4; aggregate run Task 9.
- Firmware per-run files, pause/resume, endpoints → **Plan 2** (out of scope here; this plan targets UI + mock).

**Placeholder scan:** No TBD/TODO; every code step shows complete code; every command lists expected output.

**Type consistency:** `deriveRunbar` returns `{mode,held,startBlocked}` — consumed exactly so in `runbar.js` Step 1. `visibleRows(rows,expanded,cap)` used with the same signature in `history-section.js`. `parseRunList`/`latestRunId` shapes match `runs.js` and their consumer. `runCsvUrl(id)` returns `/api/v1/runs/<id>`, matching the mock route `"/api/v1/runs/{id}"`. `runStop(data)` / `runPause(target)` / `runResume()` signatures match both `api.js` (Task 5) and the mock handler (Task 1). Status `run.pause = {motor,heater}` and `run.id` produced in Task 1 Step 2, consumed in Tasks 3/6/7.
