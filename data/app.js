import * as store from "./core/store.js";
import { start as startWs } from "./core/ws.js";
import { get } from "./core/api.js";
import { el, clear, hhmmss } from "./core/ui.js";
import * as monitor from "./screens/monitor.js";
import * as run from "./screens/run.js";

const screens = {
  monitor,
  run,
  settings: { mount: (root) => { root.append(el("div", { class: "card" }, "Settings — wiring pending.")); } },
};

let teardown = null;
function route() {
  const name = location.hash.replace("#", "") || "monitor";
  const tab = screens[name] ? name : "monitor";
  document.querySelectorAll(".tab").forEach((a) => a.classList.toggle("on", a.dataset.tab === tab));
  const root = document.getElementById("screen");
  if (typeof teardown === "function") teardown();
  root.innerHTML = "";
  teardown = screens[tab].mount(root) || null;
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
    chip("UP", hhmmss(d.uptimeSec || 0)),
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

window.addEventListener("hashchange", route);
store.subscribe(renderShell);
store.subscribeConn(() => { const d = store.getStatus(); if (d) renderShell(d); });
route();
get("/api/v1/status").then((r) => { if (r.body && r.body.apiVersion) store.setStatus(r.body); });
startWs();
