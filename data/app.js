/* MINI-REACTOR control client.
   Live telemetry over WebSocket (/ws); commands via fetch() to /api/*. */
"use strict";
const $ = (id) => document.getElementById(id);

// ── Telemetry history for the chart ──
const MAXPTS = 150;
const histT = [];   // measured temperature
const histS = [];   // setpoint
let dragging = false; // suppress slider echo while the user drags

// ── Helpers ──
const hhmmss = (s) => {
  s = Math.max(0, s | 0);
  const h = (s / 3600) | 0, m = ((s % 3600) / 60) | 0, x = s % 60;
  return [h, m, x].map((n) => String(n).padStart(2, "0")).join(":");
};
const post = (url, body) =>
  fetch(url, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body || {}),
  }).catch(() => {});

// ── Render incoming status ──
function render(d) {
  const r = d.reactor || {};
  const fault = r.fault;          // liquid (DS18B20) probe fault
  const safety = r.safety;        // heater NTC over-temp cutout
  const t = (r.tempC === null || r.tempC === undefined) ? NaN : r.tempC;
  const hC = (r.heaterC === null || r.heaterC === undefined) ? NaN : r.heaterC;

  $("temp").textContent = isNaN(t) ? "--.-" : t.toFixed(1);
  $("temp").style.color = (safety || fault) ? "var(--red)" : "#eafff5";

  // Status tag priority: safety cutout > probe fault > regulating > monitor.
  const tag = $("temp-tag");
  if (safety) { tag.textContent = "⚠ SAFETY CUTOUT"; tag.style.background = "var(--red)"; }
  else if (fault) { tag.textContent = "PROBE FAULT"; tag.style.background = "var(--amber-d)"; }
  else if (r.running) { tag.textContent = "REGULATING"; tag.style.background = "var(--green-d)"; }
  else { tag.textContent = "MONITOR"; tag.style.background = "var(--line)"; }

  $("setp").textContent = (r.setpointC ?? 0).toFixed(1);
  $("heaterc").textContent = isNaN(hC) ? "--" : hC.toFixed(0);
  $("heaterc").style.color = safety ? "var(--red)" : "var(--green)";
  const err = isNaN(t) ? NaN : (r.setpointC - t);
  $("err").textContent = isNaN(err) ? "--" : (err > 0 ? "+" : "") + err.toFixed(1);

  const hp = Math.max(0, Math.min(100, r.heaterPct || 0));
  $("heatfill").style.width = hp + "%";
  $("heatpct").textContent = Math.round(hp) + "%";

  // Agitator
  const mp = r.motorPct || 0;
  $("motor-val").textContent = Math.round(mp);
  $("motor-rpm").textContent = Math.round(mp / 100 * 60); // 100% ≈ 60 RPM
  $("motorfill").style.width = mp + "%";

  // Run state
  const st = $("rs-state");
  st.textContent = r.running ? "RUNNING" : "IDLE";
  st.className = r.running ? "running" : "idle";
  $("rs-elapsed").textContent = hhmmss(r.elapsedSec || 0);
  $("rs-remain").textContent = r.running && r.remainingSec ? hhmmss(r.remainingSec)
    : (r.running && !r.durationMin ? "∞" : "—");

  // Header strip
  const w = d.wifi || {};
  $("st-link").textContent = w.connected ? "ONLINE" : (w.ap ? "AP MODE" : "····");
  $("st-vbus").textContent = d.vbus || "··";
  $("st-sd").textContent = d.sdMounted ? "OK" : "NONE";
  $("st-uptime").textContent = hhmmss(d.uptimeSec || 0);
  $("foot-status").textContent = "telemetry nominal · " + (w.ip || "");

  // Network panel
  $("net-ip").textContent = w.ip || "—";
  $("net-mode").textContent = w.connected ? "STATION" : (w.ap ? "ACCESS POINT" : "OFFLINE");
  $("sdstate").textContent = d.sdMounted ? "MOUNTED" : "NOT PRESENT";

  // Chart buffers
  if (!isNaN(t)) {
    histT.push(t); histS.push(r.setpointC || 0);
    if (histT.length > MAXPTS) { histT.shift(); histS.shift(); }
    drawChart();
  }
}

// ── Hand-rolled canvas chart (temp vs setpoint) ──
const cv = $("chart"), cx = cv.getContext("2d");
function sizeCanvas() {
  const r = cv.getBoundingClientRect(), dpr = window.devicePixelRatio || 1;
  cv.width = r.width * dpr; cv.height = r.height * dpr;
  cx.setTransform(dpr, 0, 0, dpr, 0, 0);
}
window.addEventListener("resize", () => { sizeCanvas(); drawChart(); });

function drawChart() {
  const W = cv.clientWidth, H = cv.clientHeight;
  cx.clearRect(0, 0, W, H);
  if (!histT.length) return;

  let lo = Infinity, hi = -Infinity;
  for (const a of [histT, histS]) for (const v of a) { if (v < lo) lo = v; if (v > hi) hi = v; }
  if (!isFinite(lo)) return;
  const pad = Math.max(1, (hi - lo) * 0.15); lo -= pad; hi += pad;
  if (hi - lo < 2) { const m = (hi + lo) / 2; lo = m - 1; hi = m + 1; }
  const X = (i, n) => (i / Math.max(1, n - 1)) * (W - 8) + 4;
  const Y = (v) => H - 6 - ((v - lo) / (hi - lo)) * (H - 12);

  // grid + axis labels
  cx.strokeStyle = "rgba(23,48,67,.7)"; cx.fillStyle = "#4d7585";
  cx.font = "9px monospace"; cx.lineWidth = 1;
  for (let g = 0; g <= 4; g++) {
    const y = 4 + g * (H - 8) / 4, val = hi - g * (hi - lo) / 4;
    cx.beginPath(); cx.moveTo(0, y); cx.lineTo(W, y); cx.stroke();
    cx.fillText(val.toFixed(1), 3, y - 2);
  }

  const line = (arr, color, w, dash) => {
    cx.strokeStyle = color; cx.lineWidth = w; cx.setLineDash(dash || []);
    cx.beginPath();
    arr.forEach((v, i) => { const x = X(i, arr.length), y = Y(v); i ? cx.lineTo(x, y) : cx.moveTo(x, y); });
    cx.stroke(); cx.setLineDash([]);
  };
  line(histS, "rgba(255,180,58,.85)", 1.5, [5, 4]);          // setpoint (amber dashed)
  cx.shadowBlur = 8; cx.shadowColor = "rgba(55,247,164,.7)";
  line(histT, "#37f7a4", 2);                                  // measured (green glow)
  cx.shadowBlur = 0;
}

// ── WebSocket with auto-reconnect ──
let ws;
function connect() {
  ws = new WebSocket(`ws://${location.host}/ws`);
  ws.onopen = () => { $("led").className = "led on"; };
  ws.onclose = () => { $("led").className = "led off"; $("st-link").textContent = "····"; setTimeout(connect, 2000); };
  ws.onmessage = (e) => { try { render(JSON.parse(e.data)); } catch (_) {} };
}

// ── Controls ──
$("in-motor").addEventListener("input", () => {
  dragging = true; $("t-motor").textContent = $("in-motor").value;
});
$("in-motor").addEventListener("change", () => {
  dragging = false;
  post("/api/setpoint", { motorPercent: +$("in-motor").value });
});
$("in-target").addEventListener("input", () => { $("t-target").textContent = (+$("in-target").value).toFixed(1); });
$("in-target").addEventListener("change", () => { post("/api/setpoint", { targetC: +$("in-target").value }); });
$("in-dur").addEventListener("input", () => {
  const v = +$("in-dur").value; $("t-dur").textContent = v > 0 ? v + " min" : "∞";
});

$("btn-start").onclick = () => post("/api/run", {
  action: "start",
  targetC: +$("in-target").value,
  motorPercent: +$("in-motor").value,
  durationMin: +$("in-dur").value,
});
$("btn-stop").onclick = () => post("/api/run", { action: "stop" });
$("btn-clear").onclick = () => { if (confirm("Clear the SD data log?")) post("/api/log/clear", {}); };

// ── Network ──
async function doScan() {
  $("btn-scan").textContent = "···";
  try {
    for (let i = 0; i < 8; i++) {
      const r = await (await fetch("/api/wifi/scan")).json();
      if (!r.scanning && r.networks) {
        const sel = $("net-ssid");
        sel.innerHTML = '<option value="">— select network —</option>';
        r.networks.sort((a, b) => b.rssi - a.rssi).forEach((n) => {
          const o = document.createElement("option");
          o.value = n.ssid;
          o.textContent = `${n.ssid}  ${n.secure ? "🔒" : "  "} ${n.rssi}dBm`;
          sel.appendChild(o);
        });
        break;
      }
      await new Promise((r) => setTimeout(r, 700));
    }
  } catch (_) {}
  $("btn-scan").textContent = "SCAN";
}
$("btn-scan").onclick = doScan;
$("btn-connect").onclick = () => {
  const ssid = $("net-ssid").value;
  if (!ssid) { alert("Select a network first."); return; }
  post("/api/wifi/connect", { ssid, password: $("net-pass").value });
  $("foot-status").textContent = "connecting to " + ssid + "…";
};
$("btn-forget").onclick = () => { if (confirm("Forget WiFi and return to setup AP?")) post("/api/wifi/forget", {}); };

// ── Boot ──
sizeCanvas();
fetch("/api/status").then((r) => r.json()).then(render).catch(() => {});
connect();
