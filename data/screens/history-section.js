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
