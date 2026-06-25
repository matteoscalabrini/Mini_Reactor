import * as store from "./core/store.js";
import { start as startWs } from "./core/ws.js";
import { get } from "./core/api.js";
import { el, clear, hhmmss } from "./core/ui.js";
import * as monitor from "./screens/monitor.js";
import * as settings from "./screens/settings.js";
import { mount as mountRunbar } from "./core/runbar.js";

const screens = { monitor, settings };

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
function fstat(k, v) {
  return el("div", { class: "fstat" }, el("span", { class: "k" }, k), el("span", { class: "v" }, v));
}
// Connection lamp lives in the masthead; everything else moves to the footer.
function setConn() {
  const c = document.getElementById("conn");
  if (!c) return;
  const on = store.isOnline();
  c.className = "lamp " + (on ? "on" : "off");
  c.title = on ? "Link up" : "Link down";
}
function renderShell(d) {
  setConn();
  const footer = document.getElementById("footer");
  clear(footer);
  const w = d.wifi || {};
  const link = w.connected ? (w.ip || "online") : (w.mode === "ap" ? "AP" : "offline");
  footer.append(
    fstat("LINK", link),
    fstat("VBUS", (d.system && d.system.vbus) || "—"),
    fstat("SD", d.system && d.system.sdMounted ? "OK" : "—"),
    fstat("UPTIME", hhmmss(d.uptimeSec || 0)),
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
store.subscribeConn(() => { setConn(); const d = store.getStatus(); if (d) renderShell(d); });
mountRunbar(document.getElementById("runbar"));
route();
get("/api/v1/status").then((r) => { if (r.body && r.body.apiVersion) store.setStatus(r.body); });
startWs();
