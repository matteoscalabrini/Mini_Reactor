import { el, fixed, hhmmss, clear } from "../core/ui.js";
import { parseCsv } from "../core/csv.js";
import { render } from "../core/chart.js";
import { listRuns, runCsvUrl } from "../core/api.js";
import { parseRunList, latestRunId, runFileName } from "../core/runs.js";
import { visibleRows } from "../core/runstate.js";

export function mount(root) {
  const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
  svg.setAttribute("width", "100%"); svg.setAttribute("height", "170");
  svg.setAttribute("preserveAspectRatio", "none");
  const picker = el("select", { class: "rb-picker", onchange: () => { fullLoad = false; load(); } });
  const dl = el("a", { class: "btn", href: "#", download: "run.csv" }, "⬇ Download CSV");
  // Tail-by-default: long runs (weeks) produce multi-MB CSVs — load a recent
  // window and only pull the whole file on explicit request.
  const fullBtn = el("button", { class: "ghost", hidden: "",
    onclick: () => { fullLoad = true; load(); } }, "Load full");
  const refresh = el("button", { class: "ghost",
    onclick: async () => { await refreshRuns(); load(); } }, "Refresh");
  const status = el("span", { class: "muted" }, "—");
  const tableWrap = el("div", { class: "tscroll" });
  const TAIL_ROWS = 1000;
  let fullLoad = false; // current selection: fetch entire CSV instead of the tail
  let expanded = false;
  let runsCache = [];   // last-fetched runs, for resolving the download filename

  root.append(
    el("div", { class: "card" },
      el("h3", {}, "HISTORY", el("span", { class: "rb-tools" }, picker, dl, fullBtn, refresh)),
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
    runsCache = runs;
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
      status.textContent = "No runs"; clear(tableWrap); fullBtn.hidden = true;
      while (svg.firstChild) svg.removeChild(svg.firstChild);
      dl.removeAttribute("href"); return;
    }
    const run = runsCache.find((r) => r.id === id);
    dl.href = runCsvUrl(id); dl.download = runFileName(run ? run.label : "", id);
    refresh.disabled = true; refresh.textContent = "…"; status.textContent = "loading…";
    try {
      const res = await fetch(runCsvUrl(id) + (fullLoad ? "" : `?tail=${TAIL_ROWS}`)).catch(() => null);
      const truncated = !!(res && res.ok && res.headers.get("X-Tail-Truncated") === "1");
      const text = res && res.ok ? await res.text() : "";
      const { header, rows } = parseCsv(text);
      fullBtn.hidden = !truncated;
      if (!rows.length) {
        status.textContent = "No log data"; clear(tableWrap);
        while (svg.firstChild) svg.removeChild(svg.firstChild); return;
      }
      const iL = idx(header, "liquid_c"), iS = idx(header, "setpoint_c"), iF = idx(header, "fault");
      // Fault rows are zero-filled in the CSV (probe NaN → 0.00 + fault=1) —
      // keep them out of the trend or a probe glitch plots as a dive to 0 °C.
      const valid = rows.filter((r) => r[iF] !== "1" && !Number.isNaN(+r[iL]) && !Number.isNaN(+r[iS]));
      const ds = downsample(valid);
      render(svg, { t: ds.map((r) => +r[iL]), s: ds.map((r) => +r[iS]) });
      status.textContent = truncated ? `last ${rows.length} rows — full file via ⬇` : `${rows.length} rows`;
      expanded = false;
      renderTable(header, rows);
    } finally {
      refresh.disabled = false; refresh.textContent = "Refresh";
    }
  }

  (async () => { await refreshRuns(); load(); })();
  return null;
}
