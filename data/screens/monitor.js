import * as store from "../core/store.js";
import { el, fixed, hhmmss, bar } from "../core/ui.js";
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
    probeState.className = "pill " + (sf.tripped ? "bad" : pr.resistanceOhms == null ? "warn" : pr.calibrated ? "on" : "off");
    probeState.textContent = sf.tripped ? "OVER-LIMIT" : pr.resistanceOhms == null ? "FAULT" : pr.calibrated ? "CALIBRATED" : "FACTORY";
    probeMethod.textContent = pr.method || "—";
    probeRes.textContent = pr.resistanceOhms == null ? "—" : pr.resistanceOhms;
    probeTemp.textContent = fixed(sf.heaterTempC, 0);
    if (sample(buf, Date.now(), th.tempC, th.setpointC)) render(svg, buf);
  });
  return unsub;
}
