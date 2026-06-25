import * as store from "../core/store.js";
import { el, fixed, hhmmss, bar } from "../core/ui.js";
import { makeBuffer, sample, render } from "../core/chart.js";
import * as historySection from "./history-section.js";

export function mount(root) {
  const buf = makeBuffer(150, 2000);
  const heroVal = el("span", {}, "—"), heroPill = el("span", { class: "pill off" }, "MONITOR");
  const setp = el("div", { class: "v" }, "—"), errv = el("div", { class: "v" }, "—");
  const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
  svg.setAttribute("width", "100%"); svg.setAttribute("height", "170"); svg.setAttribute("preserveAspectRatio", "none");

  // Signature: deviation strip. Full scale ±3 °C, in-spec band ±0.5 °C, so the
  // band sits at 50%±8.33% of the scale width.
  const DEV_FS = 3, DEV_TOL = 0.5;
  const devTick = el("i", { class: "dev-tick", style: "left:50%" });
  const dev = el("div", { class: "dev" },
    el("div", { class: "dev-scale" },
      el("span", { class: "dev-band", style: "left:41.67%;width:16.66%" }),
      el("span", { class: "dev-zero" }), devTick),
    el("div", { class: "dev-meta" },
      el("span", {}, "−" + DEV_FS + " °C"), el("span", { class: "c" }, "SETPOINT"), el("span", {}, "+" + DEV_FS + " °C")));

  const runPill = el("span", { class: "pill off" }, "IDLE");
  const elapsed = el("div", { class: "v" }, "—"), remain = el("div", { class: "v" }, "—"), dur = el("div", { class: "v" }, "—");
  const progBar = bar(0), progPct = el("span", { class: "n" }, "—");
  const rpmN = el("span", { class: "n" }, "—");
  const rpmBar = bar(0), heatN = el("span", { class: "n" }, "—"), heatBar = bar(0);
  const probeState = el("span", { class: "pill off" }, "—"), probeMethod = el("div", { class: "v" }, "—"),
        probeRes = el("div", { class: "v" }, "—"), probeTemp = el("div", { class: "v" }, "—");

  root.append(
    el("div", { class: "grid" },
      el("div", { class: "card monitor-main" },
        el("h3", {}, "BATH TEMPERATURE", heroPill),
        el("div", { class: "hero" },
          el("div", { class: "reading" }, heroVal, el("span", { class: "u" }, "°C")),
          el("div", { class: "meta" },
            el("div", {}, el("div", { class: "lbl" }, "SETPOINT"), setp),
            el("div", {}, el("div", { class: "lbl" }, "ERROR"), errv))),
        dev,
        el("div", { class: "chart" }, svg,
          el("div", { class: "legend" }, el("span", {}, el("i", {}), "Measured"),
            el("span", {}, el("i", { class: "sp" }), "Setpoint"),
            el("span", { style: "margin-left:auto" }, "last 5 min")))),
      el("div", { class: "side" },
        el("div", { class: "card" }, el("h3", {}, "RUN STATE", runPill),
          el("div", { class: "stat3" },
            el("div", {}, el("div", { class: "lbl" }, "ELAPSED"), elapsed),
            el("div", {}, el("div", { class: "lbl" }, "REMAINING"), remain),
            el("div", {}, el("div", { class: "lbl" }, "DURATION"), dur)),
          el("div", { class: "kv" }, el("span", { class: "lbl" }, "PROGRESS"), progPct),
          progBar),
        el("div", { class: "card" }, el("h3", {}, "AGITATOR"),
          el("div", { class: "kv" }, el("span", { class: "lbl" }, "SPEED"), el("span", {}, rpmN, el("span", { class: "u", style: "color:var(--accent)" }, " rpm"))),
          rpmBar),
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
    // Deviation strip: measured minus setpoint, mapped onto the ±DEV_FS scale.
    // In-spec = accent; out of tolerance either side = bad (out).
    const d2 = (th.tempC != null && th.setpointC != null) ? th.tempC - th.setpointC : null;
    devTick.style.left = (d2 == null ? 50 : Math.max(2, Math.min(98, 50 + (d2 / DEV_FS) * 50))) + "%";
    devTick.className = "dev-tick" + (d2 != null && Math.abs(d2) > DEV_TOL ? " out" : "");
    runPill.className = "pill " + (run.active ? "on" : "off"); runPill.textContent = run.active ? "RUNNING" : "IDLE";
    elapsed.textContent = hhmmss(run.elapsedSec || 0);
    remain.textContent = run.remainingSec == null ? (run.active ? "∞" : "—") : hhmmss(run.remainingSec);
    dur.textContent = run.durationMin ? run.durationMin + "m" : "∞";
    const durSec = (run.durationMin || 0) * 60;
    const pct = durSec > 0 ? Math.max(0, Math.min(100, (run.elapsedSec || 0) / durSec * 100)) : 0;
    progBar.firstChild.style.width = pct + "%";
    progPct.textContent = durSec > 0 ? Math.round(pct) + "%" : (run.active ? "∞" : "—");
    rpmN.textContent = fixed(disc.rpm, 1);
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

  const histRoot = el("div", { class: "history-wrap" });
  root.append(histRoot);
  const histTeardown = historySection.mount(histRoot);

  return () => { unsub(); if (histTeardown) histTeardown(); };
}
