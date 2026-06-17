import * as store from "../core/store.js";
import { el, hhmmss, toast, flashApplied } from "../core/ui.js";
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

  inTarget.addEventListener("change", async () => { if ((store.getStatus()?.run || {}).active) { const r = await setpoint({ targetC: +inTarget.value }); flashApplied(inTarget, r.ok); } });
  inRpm.addEventListener("change", async () => { if ((store.getStatus()?.run || {}).active) { const r = await setpoint({ rpm: +inRpm.value }); flashApplied(inRpm, r.ok); } });

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
    const faulted = (sf.probe && sf.probe.resistanceOhms == null) || sf.tripped;
    start.disabled = faulted;
    start.title = faulted ? "Heater probe faulted — Start is blocked" : "";
  });
  return unsub;
}
