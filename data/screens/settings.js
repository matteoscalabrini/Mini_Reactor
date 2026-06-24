import * as store from "../core/store.js";
import { el, toast, flashApplied, hhmmss } from "../core/ui.js";
import { runParams } from "../core/runparams.js";
import * as api from "../core/api.js";
import { pollScan } from "../core/wifiscan.js";

const sec = (title, ...body) => el("div", { class: "card set-sec" }, el("h3", {}, title), ...body);
const field = (label, input) => el("div", { class: "field" }, el("label", {}, label), input);

export function mount(root) {
  // WiFi
  const ssidSel = el("select", {}, el("option", { value: "" }, "— scan for networks —"));
  const pass = el("input", { type: "password", placeholder: "network password" });
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  const scanBtn = el("button", { class: "ghost" }, "Scan");
  let scanning = false;
  scanBtn.addEventListener("click", async () => {
    if (scanning) return;  // ignore re-clicks while a scan is running
    scanning = true; scanBtn.disabled = true; scanBtn.textContent = "Scanning…";
    try {
      const nets = await pollScan(api.wifiScan, sleep);
      ssidSel.innerHTML = "";
      ssidSel.append(el("option", { value: "" }, nets.length ? "— select —" : "— no networks found —"));
      nets.sort((a, b) => b.rssi - a.rssi).forEach((n) =>
        ssidSel.append(el("option", { value: n.ssid }, `${n.ssid} ${n.secure ? "🔒" : ""} ${n.rssi}dBm`)));
    } finally {
      scanning = false; scanBtn.disabled = false; scanBtn.textContent = "Scan";
    }
  });
  const wifiInfo = el("p", { class: "muted" }, "—");
  const wifi = sec("WIFI", wifiInfo,
    field("NETWORK", el("div", { class: "row" }, ssidSel, scanBtn)), field("PASSWORD", pass),
    el("div", { class: "btns" },
      el("button", { class: "go", onclick: () => { if (!ssidSel.value) return toast("Select a network"); api.wifiConnect(ssidSel.value, pass.value); toast("Connecting…", "ok"); } }, "Connect"),
      el("button", { class: "ghost", onclick: () => { if (confirm("Forget WiFi and return to setup AP?")) api.wifiForget(); } }, "Forget")));

  // SD / log
  const sdInfo = el("p", { class: "muted" }, "—");
  function eraseModal() {
    const input = el("input", { type: "text", placeholder: "type: erase" });
    const ok = el("button", { class: "stop", disabled: "", onclick: async () => { ok.disabled = true; await api.post("/api/v1/sd/erase"); toast("Erasing…", "ok"); bg.remove(); } }, "Erase card");
    input.addEventListener("input", () => { if (input.value.trim() === "erase") ok.removeAttribute("disabled"); else ok.setAttribute("disabled", ""); });
    const bg = el("div", { class: "modal-bg" },
      el("div", { class: "modal" },
        el("h4", {}, "Erase SD card"),
        el("p", {}, "This permanently deletes ALL files on the card, including logs. Type \"erase\" to confirm."),
        el("div", { class: "field" }, input),
        el("div", { class: "btns" }, ok, el("button", { class: "ghost", onclick: () => bg.remove() }, "Cancel"))));
    document.body.append(bg);
  }
  const sd = sec("DATA LOG", sdInfo,
    el("div", { class: "btns" },
      el("a", { class: "btn", href: "/api/v1/log", download: "reactor_log.csv" }, "⬇ Download CSV"),
      el("button", { class: "ghost", onclick: () => { if (confirm("Clear the SD log?")) api.logClear(); } }, "Clear log"),
      el("button", { class: "stop", onclick: eraseModal }, "Erase all files")));

  // PID + autotune
  const kp = el("input", { type: "number", step: "0.001" }), ki = el("input", { type: "number", step: "0.0001" }), kd = el("input", { type: "number", step: "0.01" });
  const modeSel = el("select", {}, el("option", { value: "auto" }, "Auto"), el("option", { value: "manual" }, "Manual"));
  const atInfo = el("span", { class: "v" }, "—");
  const pid = sec("PID TUNING",
    el("div", { class: "row" }, field("Kp", kp), field("Ki", ki), field("Kd", kd)),
    field("MODE", modeSel),
    el("div", { class: "btns" },
      el("button", { class: "go", onclick: async (e) => { const b = e.currentTarget; const r = await api.pidGains(+kp.value, +ki.value, +kd.value); flashApplied(b, r.ok); } }, "Apply gains"),
      el("button", { class: "ghost", onclick: async (e) => { const b = e.currentTarget; const r = await api.pidMode(modeSel.value); flashApplied(b, r.ok); } }, "Set mode"),
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
  const testBtn = el("button", { class: "ghost", onclick: async () => {
    testBtn.disabled = true; testBtn.textContent = "Testing…";
    await api.post("/api/v1/disc/test");
    setTimeout(() => { testBtn.disabled = false; testBtn.textContent = "Test"; }, 3200);
  } }, "Test");
  const motor = sec("MOTOR",
    el("div", { class: "row" }, field("CURRENT mA", curMa), field("MICROSTEPS", micro), field("DIRECTION", dirSel)),
    el("div", { class: "btns" },
      el("button", { class: "go", onclick: async (e) => { const b = e.currentTarget; const r = await api.disc({ currentMa: +curMa.value, microsteps: +micro.value, direction: dirSel.value }); flashApplied(b, r.ok); } }, "Apply"),
      testBtn));

  // System (read-only)
  const sysInfo = el("p", { class: "muted" }, "—");
  const sys = sec("SYSTEM", sysInfo);

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

  root.append(runSec, wifi, sd, pid, cal, motor, sys);
  api.getCalibration().then((r) => { if (r.body) calInfo.textContent = `Method ${r.body.method} · ${r.body.calibrated ? "calibrated" : "factory"} · ${(r.body.points || []).length} point(s)`; });

  let primed = false;
  const unsub = store.subscribe((d) => {
    const w = d.wifi || {}, st = d.storage || {}, p = (d.thermal || {}).pid || {}, at = p.autotune || {}, disc = d.disc || {}, s = d.system || {}, drv = disc.driver || {};
    wifiInfo.textContent = `${w.connected ? "Station" : w.mode === "ap" ? "Access point" : "Offline"} · ${w.ssid || "—"} · ${w.ip || "—"}`;
    sdInfo.textContent = st.sdMounted ? "Card mounted · logging" : "No card";
    atInfo.textContent = at.active ? `running ${at.progress || 0}%` : (at.result || "idle");
    sysInfo.textContent = `Firmware ${s.firmware || "—"} · heap ${s.freeHeap || "—"} · VBUS ${s.vbus || "—"} · driver ${drv.version || "—"} (${drv.connected ? "ok" : "—"})`;
    if (!testBtn.disabled || testBtn.textContent === "Test") testBtn.disabled = (d.run || {}).active === true;
    if (!primed) {
      primed = true;
      kp.value = p.kp ?? ""; ki.value = p.ki ?? ""; kd.value = p.kd ?? ""; if (p.mode) modeSel.value = p.mode;
      curMa.value = disc.currentMa ?? ""; micro.value = disc.microsteps ?? ""; if (disc.direction) dirSel.value = disc.direction;
    }
    const run = d.run || {};
    const paused = run.pause && (run.pause.motor || run.pause.heater);
    stateV.textContent = run.active ? (paused ? "PAUSED" : "RUNNING") : "IDLE";
    elapsedV.textContent = hhmmss(run.elapsedSec || 0);
    remainV.textContent = run.remainingSec == null
      ? (run.active ? "∞" : "—") : hhmmss(run.remainingSec);
  });
  return unsub;
}
