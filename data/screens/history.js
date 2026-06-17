import { el, fixed, hhmmss, clear } from "../core/ui.js";
import { parseCsv } from "../core/csv.js";
import { render } from "../core/chart.js";

export function mount(root) {
  const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
  svg.setAttribute("width", "100%"); svg.setAttribute("height", "170"); svg.setAttribute("preserveAspectRatio", "none");
  const tableWrap = el("div", {});
  const status = el("span", { class: "muted" }, "—");
  const refresh = el("button", { class: "ghost", onclick: () => load() }, "Refresh");

  root.append(
    el("div", { class: "card" }, el("h3", {}, "HISTORY", refresh),
      el("div", { class: "chart" }, svg,
        el("div", { class: "legend" }, el("span", {}, el("i", {}), "Liquid °C"),
          el("span", {}, el("i", { class: "sp" }), "Setpoint °C"),
          el("span", { style: "margin-left:auto" }, status)))),
    el("div", { class: "card" }, el("h3", {}, "RECENT ROWS"), tableWrap));

  const idx = (header, name) => header.indexOf(name);
  const downsample = (arr, max = 200) => { const stride = Math.ceil(arr.length / max) || 1; return arr.filter((_, i) => i % stride === 0); };

  async function load() {
    refresh.disabled = true; refresh.textContent = "…"; status.textContent = "loading…";
    const res = await fetch("/api/v1/log").catch(() => null);
    refresh.disabled = false; refresh.textContent = "Refresh";
    const text = res && res.ok ? await res.text() : "";
    const { header, rows } = parseCsv(text);
    if (!rows.length) { status.textContent = "No log data"; while (svg.firstChild) svg.removeChild(svg.firstChild); clear(tableWrap); return; }
    const iT = idx(header, "t_ms"), iL = idx(header, "liquid_c"), iS = idx(header, "setpoint_c"),
          iH = idx(header, "heater_c"), iD = idx(header, "heater_pct"), iR = idx(header, "rpm"),
          iLoad = idx(header, "load"), iF = idx(header, "fault"), iSaf = idx(header, "safety");
    const liquid = downsample(rows.map((r) => +r[iL]).filter((v) => !Number.isNaN(v)));
    const setp = downsample(rows.map((r) => +r[iS]).filter((v) => !Number.isNaN(v)));
    render(svg, { t: liquid, s: setp });
    status.textContent = `${rows.length} rows`;

    clear(tableWrap);
    const head = el("tr", {}, ["TIME", "LIQUID", "HEATER", "SETPT", "DUTY%", "RPM", "LOAD", "FLAG"].map((h) => el("th", {}, h)));
    const body = rows.slice(-50).reverse().map((r) => el("tr", {},
      el("td", {}, hhmmss(Math.round((+r[iT] || 0) / 1000))),
      el("td", {}, fixed(+r[iL], 1)), el("td", {}, fixed(+r[iH], 1)), el("td", {}, fixed(+r[iS], 1)),
      el("td", {}, fixed(+r[iD], 0)), el("td", {}, fixed(+r[iR], 1)), el("td", {}, r[iLoad] || "—"),
      el("td", {}, (r[iF] === "1" ? "FAULT" : r[iSaf] === "1" ? "TRIP" : "ok"))));
    tableWrap.append(el("table", { class: "htable" }, el("thead", {}, head), el("tbody", {}, body)));
  }

  load();
  return null;
}
