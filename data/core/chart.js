// Trend ring buffer (pure) + SVG render. Sampling is decoupled from telemetry rate.
export function makeBuffer(maxPts = 150, periodMs = 2000) { return { maxPts, periodMs, t: [], s: [], last: -1e15 }; }

export function sample(buf, now, tempC, setpointC) {
  if (tempC === null || tempC === undefined || Number.isNaN(tempC)) return false;
  if (now - buf.last < buf.periodMs) return false;
  buf.last = now;
  buf.t.push(tempC); buf.s.push(setpointC);
  if (buf.t.length > buf.maxPts) { buf.t.shift(); buf.s.shift(); }
  return true;
}
export function bounds(buf) {
  let lo = Infinity, hi = -Infinity;
  for (const arr of [buf.t, buf.s]) for (const v of arr) { if (v < lo) lo = v; if (v > hi) hi = v; }
  if (!isFinite(lo)) return null;
  const pad = Math.max(0.5, (hi - lo) * 0.15); lo -= pad; hi += pad;
  if (hi - lo < 2) { const m = (hi + lo) / 2; lo = m - 1; hi = m + 1; }
  return { lo, hi };
}
export function points(arr, b, W, H) {
  const n = arr.length;
  return arr.map((v, i) => [(n <= 1 ? 0 : i / (n - 1)) * W, H - ((v - b.lo) / (b.hi - b.lo)) * H]);
}
const NS = "http://www.w3.org/2000/svg";
// Resolve colours from the live CSS tokens so the measured trace is *exactly*
// the accent colour (and the reference/grid track the rest of the theme).
function cssVar(name, fallback) {
  if (typeof getComputedStyle === "undefined" || typeof document === "undefined") return fallback;
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim() || fallback;
}
function rgba(hex, a) {
  const m = /^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(hex);
  return m ? `rgba(${parseInt(m[1], 16)},${parseInt(m[2], 16)},${parseInt(m[3], 16)},${a})` : hex;
}
function path(d) { const p = document.createElementNS(NS, "path"); p.setAttribute("d", d); return p; }
export function render(svg, buf) {
  const MEASURED = cssVar("--accent", "#00b4d8"), REFERENCE = cssVar("--ref", "#6e8595"),
        GRID = cssVar("--rule-2", "#e6ecef"), FILL = rgba(MEASURED, 0.13);
  const gridLine = (d) => { const p = path(d); p.setAttribute("fill", "none"); p.setAttribute("stroke", GRID); p.setAttribute("stroke-width", "1"); return p; };
  const W = 600, H = 170; svg.setAttribute("viewBox", `0 0 ${W} ${H}`);
  while (svg.firstChild) svg.removeChild(svg.firstChild);
  // Graph-paper grid: 6 columns × 4 rows of hairlines, always drawn so an empty
  // plot still reads as a measurement field.
  for (let i = 1; i < 6; i++) { const x = (i / 6) * W; svg.append(gridLine(`M${x.toFixed(1)},0 L${x.toFixed(1)},${H}`)); }
  for (let i = 1; i < 4; i++) { const y = (i / 4) * H; svg.append(gridLine(`M0,${y.toFixed(1)} L${W},${y.toFixed(1)}`)); }
  const b = bounds(buf); if (!b) return;
  const tp = points(buf.t, b, W, H), sp = points(buf.s, b, W, H);
  const line = (pts) => pts.map((p, i) => `${i ? "L" : "M"}${p[0].toFixed(1)},${p[1].toFixed(1)}`).join(" ");
  if (sp.length) { const e = path(line(sp)); e.setAttribute("fill", "none"); e.setAttribute("stroke", REFERENCE); e.setAttribute("stroke-width", "1.75"); e.setAttribute("stroke-dasharray", "6 5"); svg.append(e); }
  if (tp.length) {
    const area = path(`${line(tp)} L${W},${H} L0,${H} Z`); area.setAttribute("fill", FILL); svg.append(area);
    const e = path(line(tp)); e.setAttribute("fill", "none"); e.setAttribute("stroke", MEASURED); e.setAttribute("stroke-width", "2.5"); e.setAttribute("stroke-linejoin", "round"); e.setAttribute("stroke-linecap", "round"); svg.append(e);
  }
}
