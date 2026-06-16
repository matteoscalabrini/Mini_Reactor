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
function path(d) { const p = document.createElementNS(NS, "path"); p.setAttribute("d", d); return p; }
export function render(svg, buf) {
  const W = 600, H = 170; svg.setAttribute("viewBox", `0 0 ${W} ${H}`);
  while (svg.firstChild) svg.removeChild(svg.firstChild);
  const b = bounds(buf); if (!b) return;
  const tp = points(buf.t, b, W, H), sp = points(buf.s, b, W, H);
  const line = (pts) => pts.map((p, i) => `${i ? "L" : "M"}${p[0].toFixed(1)},${p[1].toFixed(1)}`).join(" ");
  if (sp.length) { const e = path(line(sp)); e.setAttribute("fill", "none"); e.setAttribute("stroke", "#9fb1bc"); e.setAttribute("stroke-width", "2"); e.setAttribute("stroke-dasharray", "6 5"); svg.append(e); }
  if (tp.length) {
    const area = path(`${line(tp)} L${W},${H} L0,${H} Z`); area.setAttribute("fill", "rgba(28,156,216,.16)"); svg.append(area);
    const e = path(line(tp)); e.setAttribute("fill", "none"); e.setAttribute("stroke", "#1c9cd8"); e.setAttribute("stroke-width", "2.5"); e.setAttribute("stroke-linejoin", "round"); svg.append(e);
  }
}
