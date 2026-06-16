# Mini-Reactor Control UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the stale dark UI in `data/` with a light, tabbed (Monitor/Run/Settings) vanilla-JS control app on the live `/api/v1` + WebSocket contract, fully exercisable against `tools/mock_server.py`.

**Architecture:** Buildless static SPA in `data/`: one `index.html` shell (top bar + segmented tabs + alarm banner + `#screen` slot), a `core/` data layer (`store` pub/sub, `ws` transport-in, `api` transport-out, `ui` render helpers, `chart` SVG), and `screens/{monitor,run,settings}.js` mounted by hash route. Pure logic (store/ui/chart) is host-unit-tested with Node; screens are verified by headless-Chrome screenshots against the mock.

**Tech Stack:** Vanilla ES modules (no build step), CSS custom properties, bundled Doto+Inter woff2, served from ESP32 SPIFFS. Dev/test: `tools/mock_server.py` (serves `data/` + fakes REST/WS), Node 22 (unit tests), headless Chrome (screenshots).

**Spec:** `docs/superpowers/specs/2026-06-16-ui-control-app-design.md`. Visual reference (in-repo): `.superpowers/brainstorm/58139-1781527304/content/{monitor,nav-shell,architecture}.html`.

---

## Baseline assumptions

- Branch already checked out: `feature/ui-rebuild` (off `main`). Design + spec already committed (`29a3aca`).
- Tools (not on PATH unless noted): mock `=/tmp/pio-venv/bin/python tools/mock_server.py` (serves `http://localhost:8000`); Node `=node` (v22, on PATH); Chrome `="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"`; PlatformIO `=/tmp/pio-venv/bin/pio`.
- **SHARED-WORKTREE git rule:** never `git checkout`/`switch`/`reset`/`stash` — only `git add` + `git commit` and read-only `git show`/`diff`/`log`.
- **Screenshot helper** (used throughout). Start the mock once per verification, deep-link a tab via the URL hash, screenshot, then Read the PNG:
  ```bash
  /tmp/pio-venv/bin/python tools/mock_server.py >/tmp/mock.log 2>&1 &  MOCK=$!
  node -e "setTimeout(()=>{},1500)"   # let it bind
  "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless=new --disable-gpu \
    --hide-scrollbars --window-size=1120,1000 --screenshot=/tmp/ui.png "http://localhost:8000/#monitor"
  kill $MOCK 2>/dev/null
  ```
  Then Read `/tmp/ui.png` to confirm the render. To list console errors instead, replace `--screenshot=...` with `--dump-dom` and inspect, or add `--enable-logging=stderr --v=1` and grep `/tmp/mock.log` is N/A (browser logs go to stderr).

## Scope

In: the full app (shell, data layer, Monitor, Run, Settings) per the spec. Out: SD format / system restart (no endpoints), auth, recipes, persisted history.

## File structure

| File | Responsibility | Action |
|---|---|---|
| `data/index.html` | Shell markup (top bar, tabs, alarm banner, `#screen`). | Replace |
| `data/app.css` | Tokens, `@font-face`, shell, tabs, banner, card components. | Replace |
| `data/app.js` | Boot: fonts/shell, hash-tab routing, start WS, mount active screen. | Replace |
| `data/package.json` | `{"type":"module"}` so Node parses `data/**/*.js` as ESM (test-only). | Create |
| `data/core/store.js` | Telemetry + connection state; pub/sub. | Create |
| `data/core/ui.js` | Pure formatters + DOM helpers (`el`, `pill`, `bar`, `toast`). | Create |
| `data/core/api.js` | REST wrappers for every `/api/v1` command. | Create |
| `data/core/ws.js` | `/ws` client, JSON→store, auto-reconnect. | Create |
| `data/core/chart.js` | Ring buffer + scaling + SVG trend render. | Create |
| `data/screens/monitor.js` | Read-only dashboard. | Create |
| `data/screens/run.js` | Run controls + live state. | Create |
| `data/screens/settings.js` | WiFi/SD/PID/autotune/calibration/motor/system. | Create |
| `data/fonts/{inter,doto}.woff2` | Bundled fonts. | Create |
| `test/ui/{store,ui,chart}.test.mjs` | Node unit tests for pure logic. | Create |
| `tools/mock_server.py` | Already round-trips `/disc` + `/pid` mode (verified); verification only. | — |
| `data/logo.svg` | Keep. | — |

> The old `data/app.js`, `data/app.css`, `data/index.html` are fully replaced.

---

### Task 1: Shell skeleton + tokens + fonts

**Files:** replace `data/index.html`, `data/app.css`, `data/app.js`; create `data/fonts/`.

- [ ] **Step 1: Download the fonts**

```bash
mkdir -p data/fonts
curl -sL -o data/fonts/inter.woff2 "https://cdn.jsdelivr.net/npm/@fontsource-variable/inter/files/inter-latin-wght-normal.woff2"
curl -sL -o data/fonts/doto.woff2  "https://cdn.jsdelivr.net/npm/@fontsource-variable/doto/files/doto-latin-wght-normal.woff2"
ls -l data/fonts   # both > 10 KB
```

- [ ] **Step 2: Write `data/index.html`** (replace entire file)

```html
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>Mini-Reactor · Control</title>
<link rel="icon" href="/logo.svg" type="image/svg+xml">
<link rel="stylesheet" href="/app.css">
</head>
<body>
<div class="app">
  <header class="topbar">
    <div class="brand">MINI<b>·</b>REACTOR</div>
    <div class="status" id="chips"></div>
  </header>
  <nav class="tabs" id="tabs">
    <a class="tab" href="#monitor" data-tab="monitor">Monitor</a>
    <a class="tab" href="#run" data-tab="run">Run</a>
    <a class="tab" href="#settings" data-tab="settings">Settings</a>
  </nav>
  <div class="banner" id="banner" hidden></div>
  <main id="screen"></main>
</div>
<script type="module" src="/app.js"></script>
</body>
</html>
```

- [ ] **Step 3: Write `data/app.css`** (replace entire file)

```css
@font-face{font-family:"Inter";src:url("/fonts/inter.woff2") format("woff2");font-weight:100 900;font-display:swap}
@font-face{font-family:"Doto";src:url("/fonts/doto.woff2") format("woff2");font-weight:100 900;font-display:swap}
:root{
  --bg:#eaeff3;--surface:#fff;--surface-2:#f4f8fb;--border:#dde6ec;--border-2:#e9eff4;
  --ink:#14242e;--muted:#5c727f;--faint:#9fb1bc;
  --accent:#1c9cd8;--accent-deep:#0e7bb0;--accent-soft:#e6f5fc;
  --ok:#27ae60;--warn:#e8920c;--bad:#e2483a;
  --shadow:0 1px 2px rgba(20,40,55,.04),0 10px 30px rgba(20,40,55,.06);
  --mono:"Doto",ui-monospace,monospace;--sans:"Inter",system-ui,sans-serif;
}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font-family:var(--sans);line-height:1.45}
.app{max-width:1080px;margin:0 auto;padding:18px}
.topbar{display:flex;align-items:center;justify-content:space-between;gap:12px;flex-wrap:wrap;
  background:var(--surface);border:1px solid var(--border);border-radius:14px;padding:12px 18px;box-shadow:var(--shadow)}
.brand{font-family:var(--mono);font-weight:800;font-size:19px;letter-spacing:.05em}
.brand b{color:var(--accent)}
.status{display:flex;gap:8px;flex-wrap:wrap}
.chip{display:flex;align-items:center;gap:6px;background:var(--surface-2);border:1px solid var(--border);border-radius:9px;padding:6px 10px}
.chip .k{font-size:9px;letter-spacing:.1em;color:var(--faint);font-weight:700}
.chip .v{font-size:12px;font-weight:600}
.dot{width:7px;height:7px;border-radius:50%;background:var(--faint)}
.dot.on{background:var(--ok)} .dot.off{background:var(--bad)}
.tabs{display:flex;gap:4px;background:var(--surface);border:1px solid var(--border);border-radius:11px;
  padding:5px;margin:14px 0;width:max-content;box-shadow:var(--shadow)}
.tab{font-size:13px;font-weight:600;color:var(--muted);padding:8px 20px;border-radius:8px;text-decoration:none}
.tab.on{background:var(--accent);color:#fff;box-shadow:0 2px 8px rgba(28,156,216,.35)}
.banner{border-radius:11px;padding:10px 14px;margin-bottom:14px;font-size:13px;font-weight:600;display:flex;gap:10px;flex-wrap:wrap}
.banner.warn{background:#fdf2e0;color:#9a6207;border:1px solid #f4dcae}
.banner.critical{background:#fde7e4;color:#a32c1f;border:1px solid #f3c3bc}
.grid{display:grid;grid-template-columns:1.55fr 1fr;gap:14px}
@media(max-width:820px){.grid{grid-template-columns:1fr}}
.side{display:flex;flex-direction:column;gap:14px}
.card{background:var(--surface);border:1px solid var(--border);border-radius:14px;padding:16px 18px;box-shadow:var(--shadow)}
.card h3{margin:0 0 12px;font-size:10.5px;letter-spacing:.16em;color:var(--muted);font-weight:700;display:flex;justify-content:space-between;align-items:center}
.lbl{font-size:9px;letter-spacing:.13em;color:var(--faint);font-weight:700}
.pill{font-size:10px;font-weight:700;letter-spacing:.08em;padding:4px 10px;border-radius:20px}
.pill.on{background:var(--accent-soft);color:var(--accent-deep)} .pill.off{background:var(--surface-2);color:var(--faint)}
.pill.bad{background:#fde7e4;color:var(--bad)} .pill.warn{background:#fdf2e0;color:var(--warn)}
.reading{font-family:var(--mono);font-weight:800;font-size:72px;line-height:.85;color:var(--accent-deep)}
.reading .u{font-size:24px;color:var(--accent);margin-left:4px}
.hero{display:flex;align-items:flex-end;justify-content:space-between;gap:18px;flex-wrap:wrap}
.meta{display:flex;gap:22px;text-align:right}
.meta .v{font-family:var(--mono);font-weight:700;font-size:22px}
.meta .v.up{color:var(--warn)} .meta .vs{font-size:11px;color:var(--muted)}
.n{font-family:var(--mono);font-weight:700;font-size:20px} .n .u{font-size:11px;color:var(--accent);margin-left:2px}
.kv{display:flex;justify-content:space-between;align-items:baseline;margin:9px 0}
.bar{height:9px;background:var(--surface-2);border:1px solid var(--border-2);border-radius:6px;overflow:hidden;margin-top:4px}
.bar>i{display:block;height:100%;background:linear-gradient(90deg,var(--accent),var(--accent-deep));border-radius:6px}
.stat3{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;text-align:center}
.stat3 .v{font-family:var(--mono);font-weight:700;font-size:16px;margin-top:3px}
.chart{margin-top:16px;background:var(--surface-2);border:1px solid var(--border-2);border-radius:10px;padding:10px}
.legend{display:flex;gap:16px;margin-top:6px;font-size:10px;color:var(--muted)}
.legend i{display:inline-block;width:14px;border-top:2px solid var(--accent);margin-right:5px}
.legend i.sp{border-top:2px dashed var(--faint)}
.field{margin:12px 0}
.field label{display:flex;justify-content:space-between;font-size:11px;font-weight:600;color:var(--muted);margin-bottom:5px}
.field input,.field select{width:100%;font:inherit;padding:9px 11px;border:1px solid var(--border);border-radius:9px;background:var(--surface-2)}
.row{display:flex;gap:8px}
.btns{display:flex;gap:8px;flex-wrap:wrap;margin-top:12px}
button,.btn{font:inherit;font-weight:600;border:1px solid var(--border);background:var(--surface);color:var(--ink);
  padding:9px 16px;border-radius:9px;cursor:pointer;text-decoration:none;display:inline-block}
button.go{background:var(--accent);border-color:var(--accent);color:#fff}
button.stop{background:var(--bad);border-color:var(--bad);color:#fff}
button.ghost{background:var(--surface-2)} button:disabled{opacity:.5;cursor:not-allowed}
.toast{position:fixed;bottom:18px;left:50%;transform:translateX(-50%);padding:10px 16px;border-radius:10px;
  font-size:13px;font-weight:600;box-shadow:var(--shadow);z-index:9}
.toast.err{background:var(--bad);color:#fff} .toast.ok{background:var(--ok);color:#fff}
.set-sec{margin-bottom:14px}
.muted{color:var(--muted);font-size:12px}
```

- [ ] **Step 4: Write `data/app.js`** (replace entire file — minimal shell + hash routing; screens stubbed until later tasks)

```js
const screens = {
  monitor: (root) => { root.innerHTML = '<div class="card">Monitor — wiring pending.</div>'; },
  run: (root) => { root.innerHTML = '<div class="card">Run — wiring pending.</div>'; },
  settings: (root) => { root.innerHTML = '<div class="card">Settings — wiring pending.</div>'; },
};
let teardown = null;
function route() {
  const name = (location.hash.replace('#', '') || 'monitor');
  const tab = screens[name] ? name : 'monitor';
  document.querySelectorAll('.tab').forEach((a) => a.classList.toggle('on', a.dataset.tab === tab));
  const root = document.getElementById('screen');
  if (typeof teardown === 'function') teardown();
  root.innerHTML = '';
  teardown = screens[tab](root) || null;
}
window.addEventListener('hashchange', route);
route();
```

- [ ] **Step 5: Verify the shell renders**

Use the screenshot helper (Baseline) with `http://localhost:8000/#monitor`, then with `/#run` and `/#settings`. Read `/tmp/ui.png` after each.
Expected: light top bar with `MINI·REACTOR` wordmark (Doto), three segmented tabs (active highlighted in accent), placeholder card. Tab switches with the hash.

- [ ] **Step 6: Commit**

```bash
git add data/index.html data/app.css data/app.js data/fonts
git commit -m "feat(ui): shell skeleton, design tokens, bundled fonts"
```

---

### Task 2: Core store + ui helpers (TDD)

**Files:** create `data/package.json`, `data/core/store.js`, `data/core/ui.js`, `test/ui/store.test.mjs`, `test/ui/ui.test.mjs`.

- [ ] **Step 1: Write the failing tests**

`data/package.json`:
```json
{ "type": "module", "private": true }
```

`test/ui/store.test.mjs`:
```js
import assert from "node:assert/strict";
import * as store from "../../data/core/store.js";

let seen = null, conn = null;
const off = store.subscribe((s) => { seen = s; });
store.subscribeConn((v) => { conn = v; });

store.setStatus({ uptimeSec: 5 });
assert.equal(seen.uptimeSec, 5, "subscriber receives status");
assert.equal(store.getStatus().uptimeSec, 5, "getStatus returns latest");

off();
store.setStatus({ uptimeSec: 9 });
assert.equal(seen.uptimeSec, 5, "unsubscribe stops updates");

store.setOnline(true);
assert.equal(conn, true, "conn subscriber notified");
assert.equal(store.isOnline(), true);
console.log("store.test OK");
```

`test/ui/ui.test.mjs`:
```js
import assert from "node:assert/strict";
import { hhmmss, fixed, dash } from "../../data/core/ui.js";

assert.equal(hhmmss(3661), "01:01:01");
assert.equal(hhmmss(-5), "00:00:00");
assert.equal(fixed(30.45, 1), "30.5");
assert.equal(fixed(null), "—");
assert.equal(fixed(NaN), "—");
assert.equal(dash(null), "—");
assert.equal(dash(0), 0);
console.log("ui.test OK");
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `node test/ui/store.test.mjs; node test/ui/ui.test.mjs`
Expected: FAIL — `Cannot find module .../data/core/store.js`.

- [ ] **Step 3: Implement `data/core/store.js`**

```js
// Reactive telemetry + connection state. No DOM.
const subs = new Set();
const connSubs = new Set();
let state = null;
let online = false;

export function setStatus(s) { state = s; for (const fn of subs) fn(state); }
export function getStatus() { return state; }
export function subscribe(fn) { subs.add(fn); if (state) fn(state); return () => subs.delete(fn); }

export function setOnline(v) { online = v; for (const fn of connSubs) fn(v); }
export function isOnline() { return online; }
export function subscribeConn(fn) { connSubs.add(fn); fn(online); return () => connSubs.delete(fn); }
```

- [ ] **Step 4: Implement `data/core/ui.js`**

```js
// Pure formatters (host-tested) + DOM helpers (browser-only; not called at import).
export const hhmmss = (s) => {
  s = Math.max(0, s | 0);
  const h = (s / 3600) | 0, m = ((s % 3600) / 60) | 0, x = s % 60;
  return [h, m, x].map((n) => String(n).padStart(2, "0")).join(":");
};
export const fixed = (v, d = 1) =>
  (v === null || v === undefined || Number.isNaN(v)) ? "—" : Number(v).toFixed(d);
export const dash = (v) => (v === null || v === undefined) ? "—" : v;

export function el(tag, attrs = {}, ...kids) {
  const e = document.createElement(tag);
  for (const [k, val] of Object.entries(attrs)) {
    if (val === null || val === undefined) continue;
    if (k === "class") e.className = val;
    else if (k === "html") e.innerHTML = val;
    else if (k.startsWith("on") && typeof val === "function") e.addEventListener(k.slice(2), val);
    else e.setAttribute(k, val);
  }
  for (const kid of kids.flat()) {
    if (kid === null || kid === undefined || kid === false) continue;
    e.append(kid.nodeType ? kid : document.createTextNode(String(kid)));
  }
  return e;
}
export const pill = (text, kind) => el("span", { class: `pill ${kind}` }, text);
export function bar(pct) {
  const p = Math.max(0, Math.min(100, pct || 0));
  return el("div", { class: "bar" }, el("i", { style: `width:${p}%` }));
}
export function toast(msg, kind = "err") {
  const t = el("div", { class: `toast ${kind}` }, msg);
  document.body.append(t);
  setTimeout(() => t.remove(), 2600);
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `node test/ui/store.test.mjs; node test/ui/ui.test.mjs`
Expected: `store.test OK` and `ui.test OK`.

- [ ] **Step 6: Commit**

```bash
git add data/package.json data/core/store.js data/core/ui.js test/ui/store.test.mjs test/ui/ui.test.mjs
git commit -m "feat(ui): core store + ui helpers (host-tested)"
```

---

### Task 3: Core ws + api + boot wiring

**Files:** create `data/core/ws.js`, `data/core/api.js`; modify `data/app.js`.

- [ ] **Step 1: Implement `data/core/api.js`**

```js
// REST transport-out. Every command acks async; callers observe results via telemetry.
async function req(method, path, body) {
  try {
    const r = await fetch(path, {
      method,
      headers: body ? { "Content-Type": "application/json" } : {},
      body: body ? JSON.stringify(body) : undefined,
    });
    const j = await r.json().catch(() => ({}));
    return { ok: r.ok && j.ok !== false, status: r.status, body: j };
  } catch (_) { return { ok: false, status: 0, body: {} }; }
}
export const get = (p) => req("GET", p);
export const post = (p, b) => req("POST", p, b || {});

export const runStart = (targetC, rpm, durationMin) => post("/api/v1/run", { action: "start", targetC, rpm, durationMin });
export const runStop = () => post("/api/v1/run", { action: "stop" });
export const setpoint = (o) => post("/api/v1/setpoint", o);
export const disc = (o) => post("/api/v1/disc", o);
export const pidGains = (kp, ki, kd) => post("/api/v1/pid", { kp, ki, kd });
export const pidMode = (mode) => post("/api/v1/pid", { mode });
export const autotune = (action) => post("/api/v1/pid/autotune", { action });
export const getCalibration = () => get("/api/v1/calibration");
export const calPoint = (referenceC) => post("/api/v1/calibration/point", { referenceC });
export const calCompute = () => post("/api/v1/calibration/compute");
export const calReset = () => post("/api/v1/calibration/reset");
export const wifiScan = () => get("/api/v1/wifi/scan");
export const wifiConnect = (ssid, password) => post("/api/v1/wifi/connect", { ssid, password });
export const wifiForget = () => post("/api/v1/wifi/forget");
export const logClear = () => post("/api/v1/log/clear");
```

- [ ] **Step 2: Implement `data/core/ws.js`**

```js
import { setStatus, setOnline } from "./store.js";

let backoff = 1000;
export function start() {
  const ws = new WebSocket(`ws://${location.host}/ws`);
  ws.onopen = () => { setOnline(true); backoff = 1000; };
  ws.onclose = () => { setOnline(false); setTimeout(start, backoff); backoff = Math.min(backoff * 2, 10000); };
  ws.onmessage = (e) => { try { setStatus(JSON.parse(e.data)); } catch (_) {} };
}
```

- [ ] **Step 3: Rewrite `data/app.js`** to render the shell chips + banner from the store and start the WS. (Screens still stubbed; replaced in later tasks.)

```js
import * as store from "./core/store.js";
import { start as startWs } from "./core/ws.js";
import { get } from "./core/api.js";
import { el, clear } from "./core/ui.js";

const screens = {
  monitor: (root) => { root.append(el("div", { class: "card" }, "Monitor — wiring pending.")); },
  run: (root) => { root.append(el("div", { class: "card" }, "Run — wiring pending.")); },
  settings: (root) => { root.append(el("div", { class: "card" }, "Settings — wiring pending.")); },
};

let teardown = null;
function route() {
  const name = location.hash.replace("#", "") || "monitor";
  const tab = screens[name] ? name : "monitor";
  document.querySelectorAll(".tab").forEach((a) => a.classList.toggle("on", a.dataset.tab === tab));
  const root = document.getElementById("screen");
  if (typeof teardown === "function") teardown();
  root.innerHTML = "";
  teardown = screens[tab](root) || null;
}

const ALARM_LABELS = {
  sensor_fault: "Liquid probe fault", heater_probe_fault: "Heater probe fault",
  safety_tripped: "Safety cutout", driver_ot: "Driver over-temp", driver_otpw: "Driver over-temp warning",
  driver_stall: "Motor stall", driver_open_load: "Motor open load",
};
function chip(k, v, dotClass) {
  return el("div", { class: "chip" }, dotClass ? el("span", { class: `dot ${dotClass}` }) : null,
    el("span", { class: "k" }, k), el("span", { class: "v" }, v));
}
function renderShell(d) {
  const chips = document.getElementById("chips");
  clear(chips);
  const w = d.wifi || {};
  const link = w.connected ? (w.ip || "online") : (w.mode === "ap" ? "AP" : "offline");
  chips.append(
    chip("LINK", link, store.isOnline() ? "on" : "off"),
    chip("VBUS", (d.system && d.system.vbus) || "—"),
    chip("SD", d.system && d.system.sdMounted ? "OK" : "—"),
    chip("UP", hhmmssLocal(d.uptimeSec || 0)),
  );
  const banner = document.getElementById("banner");
  const al = d.alarms || [];
  if (!al.length) { banner.hidden = true; banner.textContent = ""; }
  else {
    const crit = al.some((a) => a.severity === "critical");
    banner.hidden = false;
    banner.className = `banner ${crit ? "critical" : "warn"}`;
    banner.textContent = al.map((a) => ALARM_LABELS[a.code] || a.code).join(" · ");
  }
}
const hhmmssLocal = (s) => { s = Math.max(0, s | 0); const h = (s / 3600) | 0, m = ((s % 3600) / 60) | 0, x = s % 60; return [h, m, x].map((n) => String(n).padStart(2, "0")).join(":"); };

window.addEventListener("hashchange", route);
store.subscribe(renderShell);
store.subscribeConn(() => { const d = store.getStatus(); if (d) renderShell(d); });
route();
get("/api/v1/status").then((r) => { if (r.body && r.body.apiVersion) store.setStatus(r.body); });
startWs();
```

(Add `export function clear(node){ while(node.firstChild) node.removeChild(node.firstChild); }` to `data/core/ui.js`.)

- [ ] **Step 4: Verify live chips + alarm banner against the mock**

Use the screenshot helper at `http://localhost:8000/#monitor`. Then, to confirm the alarm banner, run the mock and:
```bash
/tmp/pio-venv/bin/python tools/mock_server.py >/tmp/mock.log 2>&1 &  MOCK=$!
node -e "setTimeout(()=>{},1500)"
curl -s -XPOST localhost:8000/api/v1/debug/probe-fault -H 'content-type: application/json' -d '{"fault":true}' >/dev/null
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless=new --disable-gpu --hide-scrollbars \
  --window-size=1120,1000 --screenshot=/tmp/ui.png "http://localhost:8000/#monitor"
kill $MOCK 2>/dev/null
```
Read `/tmp/ui.png`. Expected: status chips show LINK ip / VBUS 12V / SD OK / UP, connection dot green; a red **critical** banner reads "Heater probe fault".

- [ ] **Step 5: Commit**

```bash
git add data/core/api.js data/core/ws.js data/core/ui.js data/app.js
git commit -m "feat(ui): WS+REST data layer, live status chips, alarm banner"
```

---

### Task 4: Trend chart (TDD)

**Files:** create `data/core/chart.js`, `test/ui/chart.test.mjs`.

- [ ] **Step 1: Write the failing test**

`test/ui/chart.test.mjs`:
```js
import assert from "node:assert/strict";
import { makeBuffer, sample, bounds, points } from "../../data/core/chart.js";

const b = makeBuffer(3, 1000);
assert.equal(sample(b, 0, 10, 12), true, "first sample taken");
assert.equal(sample(b, 500, 11, 12), false, "throttled within period");
assert.equal(sample(b, 1000, 20, 12), true, "sample after period");
assert.equal(sample(b, 2000, NaN, 12), false, "NaN skipped");
assert.equal(sample(b, 3000, 30, 12), true);
assert.equal(sample(b, 4000, 40, 12), true);
assert.deepEqual(b.t, [20, 30, 40], "ring buffer caps at maxPts");

const bb = bounds(b);
assert.ok(bb.lo < 12 && bb.hi > 40, "bounds padded around data");
const pts = points([0, 10], { lo: 0, hi: 10 }, 100, 50);
assert.deepEqual(pts[0], [0, 50], "first point bottom-left");
assert.deepEqual(pts[1], [100, 0], "last point top-right");
console.log("chart.test OK");
```

- [ ] **Step 2: Run to verify it fails**

Run: `node test/ui/chart.test.mjs`
Expected: FAIL — `Cannot find module .../data/core/chart.js`.

- [ ] **Step 3: Implement `data/core/chart.js`**

```js
// Trend ring buffer (pure) + SVG render. Sampling is decoupled from telemetry rate.
export function makeBuffer(maxPts = 150, periodMs = 2000) { return { maxPts, periodMs, t: [], s: [], last: -1e15 }; }

export function sample(buf, now, tempC, setpointC) {
  if (tempC === null || tempC === undefined || Number.isNaN(tempC)) return false;
  if (now - buf.last < buf.periodMs) return false;
  buf.last = now;
  buf.t.push(tempC); buf.s.push(setpointC);
  if (buf.t.length > buf.maxPts) { buf.t.shift(); buf.s.shift(); }
  return true;
}
export function bounds(buf) {
  let lo = Infinity, hi = -Infinity;
  for (const arr of [buf.t, buf.s]) for (const v of arr) { if (v < lo) lo = v; if (v > hi) hi = v; }
  if (!isFinite(lo)) return null;
  const pad = Math.max(0.5, (hi - lo) * 0.15); lo -= pad; hi += pad;
  if (hi - lo < 2) { const m = (hi + lo) / 2; lo = m - 1; hi = m + 1; }
  return { lo, hi };
}
export function points(arr, b, W, H) {
  const n = arr.length;
  return arr.map((v, i) => [(n <= 1 ? 0 : i / (n - 1)) * W, H - ((v - b.lo) / (b.hi - b.lo)) * H]);
}
const NS = "http://www.w3.org/2000/svg";
function path(d) { const p = document.createElementNS(NS, "path"); p.setAttribute("d", d); return p; }
export function render(svg, buf) {
  const W = 600, H = 170; svg.setAttribute("viewBox", `0 0 ${W} ${H}`);
  while (svg.firstChild) svg.removeChild(svg.firstChild);
  const b = bounds(buf); if (!b) return;
  const tp = points(buf.t, b, W, H), sp = points(buf.s, b, W, H);
  const line = (pts) => pts.map((p, i) => `${i ? "L" : "M"}${p[0].toFixed(1)},${p[1].toFixed(1)}`).join(" ");
  if (sp.length) { const e = path(line(sp)); e.setAttribute("fill", "none"); e.setAttribute("stroke", "#9fb1bc"); e.setAttribute("stroke-width", "2"); e.setAttribute("stroke-dasharray", "6 5"); svg.append(e); }
  if (tp.length) {
    const area = path(`${line(tp)} L${W},${H} L0,${H} Z`); area.setAttribute("fill", "rgba(28,156,216,.16)"); svg.append(area);
    const e = path(line(tp)); e.setAttribute("fill", "none"); e.setAttribute("stroke", "#1c9cd8"); e.setAttribute("stroke-width", "2.5"); e.setAttribute("stroke-linejoin", "round"); svg.append(e);
  }
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `node test/ui/chart.test.mjs`
Expected: `chart.test OK`.

- [ ] **Step 5: Commit**

```bash
git add data/core/chart.js test/ui/chart.test.mjs
git commit -m "feat(ui): trend chart ring-buffer + SVG render (host-tested)"
```

---

### Task 5: Monitor screen

**Files:** create `data/screens/monitor.js`; modify `data/app.js` (mount it).

- [ ] **Step 1: Implement `data/screens/monitor.js`**

```js
import * as store from "../core/store.js";
import { el, fixed, hhmmss, bar, pill } from "../core/ui.js";
import { makeBuffer, sample, render } from "../core/chart.js";

export function mount(root) {
  const buf = makeBuffer(150, 2000);
  const heroVal = el("span", {}, "—"), heroPill = el("span", { class: "pill off" }, "MONITOR");
  const setp = el("div", { class: "v" }, "—"), errv = el("div", { class: "v" }, "—");
  const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
  svg.setAttribute("width", "100%"); svg.setAttribute("height", "170"); svg.setAttribute("preserveAspectRatio", "none");

  const runPill = el("span", { class: "pill off" }, "IDLE");
  const elapsed = el("div", { class: "v" }, "—"), remain = el("div", { class: "v" }, "—"), dur = el("div", { class: "v" }, "—");
  const rpmN = el("span", { class: "n" }, "—"), loadN = el("span", { class: "n" }, "—");
  const rpmBar = bar(0), heatN = el("span", { class: "n" }, "—"), heatBar = bar(0);
  const probeState = el("span", { class: "pill off" }, "—"), probeMethod = el("div", { class: "v" }, "—"),
        probeRes = el("div", { class: "v" }, "—"), probeTemp = el("div", { class: "v" }, "—");

  root.append(
    el("div", { class: "grid" },
      el("div", { class: "card" },
        el("h3", {}, "BATH TEMPERATURE", heroPill),
        el("div", { class: "hero" },
          el("div", { class: "reading" }, heroVal, el("span", { class: "u" }, "°C")),
          el("div", { class: "meta" },
            el("div", {}, el("div", { class: "lbl" }, "SETPOINT"), setp),
            el("div", {}, el("div", { class: "lbl" }, "ERROR"), errv))),
        el("div", { class: "chart" }, svg,
          el("div", { class: "legend" }, el("span", {}, el("i", {}), "Measured"),
            el("span", {}, el("i", { class: "sp" }), "Setpoint"),
            el("span", { style: "margin-left:auto" }, "last 5 min")))),
      el("div", { class: "side" },
        el("div", { class: "card" }, el("h3", {}, "RUN STATE", runPill),
          el("div", { class: "stat3" },
            el("div", {}, el("div", { class: "lbl" }, "ELAPSED"), elapsed),
            el("div", {}, el("div", { class: "lbl" }, "REMAINING"), remain),
            el("div", {}, el("div", { class: "lbl" }, "DURATION"), dur))),
        el("div", { class: "card" }, el("h3", {}, "AGITATOR"),
          el("div", { class: "kv" }, el("span", { class: "lbl" }, "SPEED"), el("span", {}, rpmN, el("span", { class: "u", style: "color:var(--accent)" }, " rpm"))),
          rpmBar,
          el("div", { class: "kv" }, el("span", { class: "lbl" }, "LOAD"), loadN)),
        el("div", { class: "card" }, el("h3", {}, "HEATER DUTY"),
          el("div", { class: "kv" }, heatN, el("span", { class: "lbl", style: "align-self:center" }, "PWM")), heatBar),
        el("div", { class: "card" }, el("h3", {}, "HEATER SAFETY PROBE", probeState),
          el("div", { class: "stat3" },
            el("div", {}, el("div", { class: "lbl" }, "METHOD"), probeMethod),
            el("div", {}, el("div", { class: "lbl" }, "RESIST Ω"), probeRes),
            el("div", {}, el("div", { class: "lbl" }, "TEMP °C"), probeTemp))))));

  const unsub = store.subscribe((d) => {
    const th = d.thermal || {}, sf = th.safety || {}, pr = sf.probe || {}, run = d.run || {}, disc = d.disc || {};
    heroVal.textContent = fixed(th.tempC, 1);
    setp.textContent = fixed(th.setpointC, 1); errv.textContent = fixed(th.errorC, 1);
    errv.className = "v" + ((th.errorC || 0) > 0 ? " up" : "");
    heroPill.className = "pill " + (sf.tripped ? "bad" : th.fault ? "warn" : run.active ? "on" : "off");
    heroPill.textContent = sf.tripped ? "SAFETY CUTOUT" : th.fault ? "PROBE FAULT" : run.active ? "REGULATING" : "MONITOR";
    runPill.className = "pill " + (run.active ? "on" : "off"); runPill.textContent = run.active ? "RUNNING" : "IDLE";
    elapsed.textContent = hhmmss(run.elapsedSec || 0);
    remain.textContent = run.remainingSec == null ? (run.active ? "∞" : "—") : hhmmss(run.remainingSec);
    dur.textContent = run.durationMin ? run.durationMin + "m" : "∞";
    rpmN.textContent = fixed(disc.rpm, 1); loadN.textContent = disc.load == null ? "—" : disc.load;
    rpmBar.firstChild.style.width = Math.min(100, ((disc.rpm || 0) / 30) * 100) + "%";
    heatN.innerHTML = ""; heatN.append(fixed(th.heaterPct, 0), el("span", { class: "u" }, "%"));
    heatBar.firstChild.style.width = Math.max(0, Math.min(100, th.heaterPct || 0)) + "%";
    const fault = pr.resistanceOhms == null || sf.tripped;
    probeState.className = "pill " + (sf.tripped ? "bad" : pr.resistanceOhms == null ? "warn" : pr.calibrated ? "on" : "off");
    probeState.textContent = sf.tripped ? "OVER-LIMIT" : pr.resistanceOhms == null ? "FAULT" : pr.calibrated ? "CALIBRATED" : "FACTORY";
    probeMethod.textContent = pr.method || "—";
    probeRes.textContent = pr.resistanceOhms == null ? "—" : pr.resistanceOhms;
    probeTemp.textContent = fixed(sf.heaterTempC, 0);
    if (sample(buf, Date.now(), th.tempC, th.setpointC)) render(svg, buf);
  });
  return unsub;
}
```

- [ ] **Step 2: Mount it in `data/app.js`** — replace the `screens` object and make `route` call `mount/teardown`:

Replace the `screens` declaration in `data/app.js` with:
```js
import * as monitor from "./screens/monitor.js";

const screens = {
  monitor,
  run: { mount: (root) => { root.append(el("div", { class: "card" }, "Run — wiring pending.")); } },
  settings: { mount: (root) => { root.append(el("div", { class: "card" }, "Settings — wiring pending.")); } },
};
```
And update `route()` to use `.mount`:
```js
  teardown = screens[tab].mount(root) || null;
```

- [ ] **Step 3: Verify Monitor against the mock**

Screenshot helper at `http://localhost:8000/#monitor`; Read `/tmp/ui.png`.
Expected: hero bath temp in Doto with setpoint/error, a trend chart line, RUN STATE / AGITATOR (rpm+load) / HEATER DUTY / HEATER SAFETY PROBE cards populated from the mock. (Take a second screenshot a few seconds apart to confirm the chart accrues points — the mock's temp drifts.)

- [ ] **Step 4: Commit**

```bash
git add data/screens/monitor.js data/app.js
git commit -m "feat(ui): Monitor screen"
```

---

### Task 6: Run screen

**Files:** create `data/screens/run.js`; modify `data/app.js`.

- [ ] **Step 1: Implement `data/screens/run.js`**

```js
import * as store from "../core/store.js";
import { el, hhmmss, toast } from "../core/ui.js";
import { runStart, runStop, setpoint } from "../core/api.js";

export function mount(root) {
  const inTarget = el("input", { type: "number", step: "0.5", min: "0", max: "55", value: "36" });
  const inRpm = el("input", { type: "number", step: "1", min: "0", max: "30", value: "8" });
  const inDur = el("input", { type: "number", min: "0", value: "0", placeholder: "minutes (0 = until stopped)" });
  const statePill = el("span", { class: "pill off" }, "IDLE");
  const elapsed = el("div", { class: "v" }, "—"), remain = el("div", { class: "v" }, "—");

  const start = el("button", { class: "go", onclick: async () => {
    const t = +inTarget.value, r = +inRpm.value, d = +inDur.value;
    if (r < 0 || r > 30) return toast("rpm must be 0–30");
    if (t < 0 || t > 55) return toast("target must be 0–55 °C");
    const res = await runStart(t, r, d);
    if (!res.ok) toast(res.body.error ? res.body.error.message : "start failed");
  } }, "▶ Start run");
  const stop = el("button", { class: "stop", onclick: () => runStop() }, "■ Stop");

  // Live adjust while running.
  inTarget.addEventListener("change", () => { if ((store.getStatus()?.run || {}).active) setpoint({ targetC: +inTarget.value }); });
  inRpm.addEventListener("change", () => { if ((store.getStatus()?.run || {}).active) setpoint({ rpm: +inRpm.value }); });

  root.append(el("div", { class: "grid" },
    el("div", { class: "card" }, el("h3", {}, "RUN CONTROL"),
      el("div", { class: "field" }, el("label", {}, "TARGET TEMPERATURE", el("span", {}, "°C")), inTarget),
      el("div", { class: "field" }, el("label", {}, "AGITATOR SPEED", el("span", {}, "rpm")), inRpm),
      el("div", { class: "field" }, el("label", {}, "DURATION", el("span", {}, "min")), inDur),
      el("div", { class: "btns" }, start, stop)),
    el("div", { class: "side" },
      el("div", { class: "card" }, el("h3", {}, "STATUS", statePill),
        el("div", { class: "stat3" },
          el("div", {}, el("div", { class: "lbl" }, "ELAPSED"), elapsed),
          el("div", {}, el("div", { class: "lbl" }, "REMAINING"), remain),
          el("div", {}, el("div", { class: "lbl" }, "DURATION"), el("div", { class: "v" }, "—")))))));

  const unsub = store.subscribe((dts) => {
    const run = dts.run || {}, th = dts.thermal || {}, sf = th.safety || {};
    statePill.className = "pill " + (run.active ? "on" : "off");
    statePill.textContent = run.active ? "RUNNING" : "IDLE";
    elapsed.textContent = hhmmss(run.elapsedSec || 0);
    remain.textContent = run.remainingSec == null ? (run.active ? "∞" : "—") : hhmmss(run.remainingSec);
    // Reflect a refused/over-limit probe on the Start button (firmware refuses the run).
    const faulted = (sf.probe && sf.probe.resistanceOhms == null) || sf.tripped;
    start.disabled = faulted;
    start.title = faulted ? "Heater probe faulted — Start is blocked" : "";
  });
  return unsub;
}
```

- [ ] **Step 2: Wire into `data/app.js`** — add `import * as run from "./screens/run.js";` and set `run` in the `screens` object (replace the placeholder `run` entry with `run,`).

- [ ] **Step 3: Verify Run against the mock**

Screenshot at `http://localhost:8000/#run`; Read `/tmp/ui.png` — three inputs + Start/Stop + STATUS card.
Then the refused-start path:
```bash
/tmp/pio-venv/bin/python tools/mock_server.py >/tmp/mock.log 2>&1 &  MOCK=$!
node -e "setTimeout(()=>{},1500)"
curl -s -XPOST localhost:8000/api/v1/debug/probe-fault -H 'content-type: application/json' -d '{"fault":true}' >/dev/null
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless=new --disable-gpu --hide-scrollbars \
  --window-size=1120,1000 --screenshot=/tmp/ui.png "http://localhost:8000/#run"
kill $MOCK 2>/dev/null
```
Read `/tmp/ui.png`. Expected: the **Start button is disabled** and the critical alarm banner shows "Heater probe fault".

- [ ] **Step 4: Commit**

```bash
git add data/screens/run.js data/app.js
git commit -m "feat(ui): Run screen (controls + refused-start reflection)"
```

---

### Task 7: Settings screen

**Files:** create `data/screens/settings.js`; modify `data/app.js`.

- [ ] **Step 1: Implement `data/screens/settings.js`**

```js
import * as store from "../core/store.js";
import { el, fixed, toast } from "../core/ui.js";
import * as api from "../core/api.js";

const sec = (title, ...body) => el("div", { class: "card set-sec" }, el("h3", {}, title), ...body);
const field = (label, input) => el("div", { class: "field" }, el("label", {}, label), input);

export function mount(root) {
  // WiFi
  const ssidSel = el("select", {}, el("option", { value: "" }, "— scan for networks —"));
  const pass = el("input", { type: "password", placeholder: "network password" });
  const scanBtn = el("button", { class: "ghost", onclick: async () => {
    scanBtn.textContent = "···";
    for (let i = 0; i < 8; i++) {
      const r = await api.wifiScan();
      if (r.body && r.body.networks && !r.body.scanning) {
        ssidSel.innerHTML = ""; ssidSel.append(el("option", { value: "" }, "— select —"));
        r.body.networks.sort((a, b) => b.rssi - a.rssi).forEach((n) =>
          ssidSel.append(el("option", { value: n.ssid }, `${n.ssid} ${n.secure ? "🔒" : ""} ${n.rssi}dBm`)));
        break;
      }
      await new Promise((res) => setTimeout(res, 700));
    }
    scanBtn.textContent = "Scan";
  } }, "Scan");
  const wifiInfo = el("p", { class: "muted" }, "—");
  const wifi = sec("WIFI", wifiInfo,
    field("NETWORK", el("div", { class: "row" }, ssidSel, scanBtn)), field("PASSWORD", pass),
    el("div", { class: "btns" },
      el("button", { class: "go", onclick: () => { if (!ssidSel.value) return toast("Select a network"); api.wifiConnect(ssidSel.value, pass.value); toast("Connecting…", "ok"); } }, "Connect"),
      el("button", { class: "ghost", onclick: () => { if (confirm("Forget WiFi and return to setup AP?")) api.wifiForget(); } }, "Forget")));

  // SD / log
  const sdInfo = el("p", { class: "muted" }, "—");
  const sd = sec("DATA LOG", sdInfo,
    el("div", { class: "btns" },
      el("a", { class: "btn", href: "/api/v1/log", download: "reactor_log.csv" }, "⬇ Download CSV"),
      el("button", { class: "ghost", onclick: () => { if (confirm("Clear the SD log?")) api.logClear(); } }, "Clear log")));

  // PID + autotune
  const kp = el("input", { type: "number", step: "0.001" }), ki = el("input", { type: "number", step: "0.0001" }), kd = el("input", { type: "number", step: "0.01" });
  const modeSel = el("select", {}, el("option", { value: "auto" }, "Auto"), el("option", { value: "manual" }, "Manual"));
  const atInfo = el("span", { class: "v" }, "—");
  const pid = sec("PID TUNING",
    el("div", { class: "row" }, field("Kp", kp), field("Ki", ki), field("Kd", kd)),
    field("MODE", modeSel),
    el("div", { class: "btns" },
      el("button", { class: "go", onclick: () => api.pidGains(+kp.value, +ki.value, +kd.value) }, "Apply gains"),
      el("button", { class: "ghost", onclick: () => api.pidMode(modeSel.value) }, "Set mode"),
      el("button", { class: "ghost", onclick: () => api.autotune("start") }, "Autotune"),
      el("button", { class: "ghost", onclick: () => api.autotune("cancel") }, "Cancel")),
    el("p", { class: "muted" }, "Autotune: ", atInfo));

  // Calibration
  const refC = el("input", { type: "number", step: "0.1", placeholder: "reference °C" });
  const calInfo = el("p", { class: "muted" }, "—");
  const cal = sec("HEATER-NTC CALIBRATION", calInfo,
    field("REFERENCE TEMPERATURE", refC),
    el("div", { class: "btns" },
      el("button", { class: "go", onclick: () => { api.calPoint(+refC.value); toast("Point queued", "ok"); } }, "Capture point"),
      el("button", { class: "ghost", onclick: () => api.calCompute() }, "Compute"),
      el("button", { class: "ghost", onclick: () => { if (confirm("Reset calibration to factory?")) api.calReset(); } }, "Reset")));

  // Motor
  const curMa = el("input", { type: "number", step: "10", min: "0" }), micro = el("input", { type: "number", step: "1" });
  const dirSel = el("select", {}, el("option", { value: "cw" }, "CW"), el("option", { value: "ccw" }, "CCW"));
  const motor = sec("MOTOR",
    el("div", { class: "row" }, field("CURRENT mA", curMa), field("MICROSTEPS", micro), field("DIRECTION", dirSel)),
    el("div", { class: "btns" }, el("button", { class: "go", onclick: () => api.disc({ currentMa: +curMa.value, microsteps: +micro.value, direction: dirSel.value }) }, "Apply")));

  // System (read-only)
  const sysInfo = el("p", { class: "muted" }, "—");
  const sys = sec("SYSTEM", sysInfo);

  root.append(wifi, sd, pid, cal, motor, sys);
  api.getCalibration().then((r) => { if (r.body) calInfo.textContent = `Method ${r.body.method} · ${r.body.calibrated ? "calibrated" : "factory"} · ${(r.body.points || []).length} point(s)`; });

  let primed = false;
  const unsub = store.subscribe((d) => {
    const w = d.wifi || {}, st = d.storage || {}, p = (d.thermal || {}).pid || {}, at = p.autotune || {}, disc = d.disc || {}, s = d.system || {}, drv = disc.driver || {};
    wifiInfo.textContent = `${w.connected ? "Station" : w.mode === "ap" ? "Access point" : "Offline"} · ${w.ssid || "—"} · ${w.ip || "—"}`;
    sdInfo.textContent = st.sdMounted ? "Card mounted · logging" : "No card";
    atInfo.textContent = at.active ? `running ${at.progress || 0}%` : (at.result || "idle");
    sysInfo.textContent = `Firmware ${s.firmware || "—"} · heap ${s.freeHeap || "—"} · VBUS ${s.vbus || "—"} · driver ${drv.version || "—"} (${drv.connected ? "ok" : "—"})`;
    if (!primed) { // prefill editable fields once
      primed = true;
      kp.value = p.kp ?? ""; ki.value = p.ki ?? ""; kd.value = p.kd ?? ""; if (p.mode) modeSel.value = p.mode;
      curMa.value = disc.currentMa ?? ""; micro.value = disc.microsteps ?? ""; if (disc.direction) dirSel.value = disc.direction;
    }
  });
  return unsub;
}
```

- [ ] **Step 2: Wire into `data/app.js`** — add `import * as settings from "./screens/settings.js";` and set `settings` in the `screens` object (replace the placeholder `settings` entry with `settings,`).

- [ ] **Step 3: Verify Settings against the mock**

Screenshot at `http://localhost:8000/#settings`; Read `/tmp/ui.png`.
Expected: WIFI (info + scan + connect/forget), DATA LOG (download/clear), PID TUNING (Kp/Ki/Kd prefilled + mode + autotune), HEATER-NTC CALIBRATION (reference input + capture/compute/reset + state line), MOTOR (current/microsteps/direction prefilled), SYSTEM (firmware/heap/vbus/driver). No SD-format or restart buttons.

- [ ] **Step 4: Commit**

```bash
git add data/screens/settings.js data/app.js
git commit -m "feat(ui): Settings screen (wifi/sd/pid/autotune/calibration/motor/system)"
```

---

### Task 8: Final verification

**Files:** none (verification only). The mock already echoes `/disc` (`currentMa/microsteps/direction`) and `/pid` (`kp/ki/kd` + `mode`) into `state` and `status()` — verified at plan time, so no mock change is needed. Settings round-trips are already visible.

- [ ] **Step 1: Confirm the mock round-trips config (no change expected)**

```bash
/tmp/pio-venv/bin/python tools/mock_server.py >/tmp/mock.log 2>&1 &  MOCK=$!
node -e "setTimeout(()=>{},1500)"
curl -s -XPOST localhost:8000/api/v1/disc -H 'content-type: application/json' -d '{"currentMa":900,"microsteps":32,"direction":"ccw"}' >/dev/null
curl -s -XPOST localhost:8000/api/v1/pid  -H 'content-type: application/json' -d '{"mode":"manual"}' >/dev/null
curl -s localhost:8000/api/v1/status | node -e "let s='';process.stdin.on('data',d=>s+=d).on('end',()=>{const d=JSON.parse(s);console.log('disc',d.disc.currentMa,d.disc.microsteps,d.disc.direction,'| pid.mode',d.thermal.pid.mode);})"
kill $MOCK 2>/dev/null
```
Expected: `disc 900 32 ccw | pid.mode manual`. If so, no edit — proceed. (Only if a field does NOT round-trip, add the missing `state[...]=...` line in the matching handler and the field in `status()`, mirroring the firmware contract.)

- [ ] **Step 2: Run the full Node test suite**

Run: `node test/ui/store.test.mjs && node test/ui/ui.test.mjs && node test/ui/chart.test.mjs`
Expected: `store.test OK`, `ui.test OK`, `chart.test OK`.

- [ ] **Step 3: Full screenshot walkthrough**

For each of `#monitor`, `#run`, `#settings`: run the screenshot helper and Read the PNG. Confirm each screen renders with live mock data, the tab highlight follows the hash, and no error overlay/blank screen. Capture one with `debug/probe-fault` set to confirm the banner + disabled Start.

- [ ] **Step 4: On-device upload (when the device is connected)**

```bash
/tmp/pio-venv/bin/pio run -e esp32-s3-devkitc-1 -t uploadfs --upload-port /dev/cu.usbmodem8401
```
Then browse to the device IP (from its serial boot `[WIFI] connected, IP …` line) and confirm the UI loads from SPIFFS, fonts render, and live telemetry/commands work. (Skip if the device is offline; the mock walkthrough is the gating verification.)

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat(ui): reconcile mock config echo; final verification" --allow-empty
```

---

## Phase done — definition of done

- `node test/ui/*.test.mjs` all pass (store, ui, chart).
- Against the mock in headless Chrome: Monitor (hero temp + chart + side cards + safety probe), Run (controls + live state + refused-start disables Start), Settings (all six sub-sections, no dead buttons) all render and update from live telemetry; the alarm banner appears on `heater_probe_fault`.
- No build step: the app is plain static files in `data/`, served unchanged by the mock and (after `uploadfs`) by the firmware.
- On-device (when available): UI loads from SPIFFS with bundled fonts and drives the real reactor.

## Out of scope (later)
- SD format / system restart (no firmware endpoints).
- Auth token, run profiles/recipes, persisted historical charts.
- Click-through interaction tests (no Playwright); functional flows are verified by screenshot + on-device.
