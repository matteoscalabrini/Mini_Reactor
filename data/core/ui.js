// Pure formatters (host-tested) + DOM helpers (browser-only; not called at import).
export const hhmmss = (s) => {
  s = Math.max(0, s | 0);
  const h = (s / 3600) | 0, m = ((s % 3600) / 60) | 0, x = s % 60;
  return [h, m, x].map((n) => String(n).padStart(2, "0")).join(":");
};
export const fixed = (v, d = 1) =>
  (v === null || v === undefined || Number.isNaN(v)) ? "—" : Number(v).toFixed(d);
export const dash = (v) => (v === null || v === undefined) ? "—" : v;

export function el(tag, attrs = {}, ...kids) {
  const e = document.createElement(tag);
  for (const [k, val] of Object.entries(attrs)) {
    if (val === null || val === undefined) continue;
    if (k === "class") e.className = val;
    else if (k === "html") e.innerHTML = val;
    else if (k.startsWith("on") && typeof val === "function") e.addEventListener(k.slice(2), val);
    else e.setAttribute(k, val);
  }
  for (const kid of kids.flat()) {
    if (kid === null || kid === undefined || kid === false) continue;
    e.append(kid.nodeType ? kid : document.createTextNode(String(kid)));
  }
  return e;
}
export const clear = (node) => { while (node.firstChild) node.removeChild(node.firstChild); };
export const pill = (text, kind) => el("span", { class: `pill ${kind}` }, text);
export function bar(pct) {
  const p = Math.max(0, Math.min(100, pct || 0));
  return el("div", { class: "bar" }, el("i", { style: `width:${p}%` }));
}
export function toast(msg, kind = "err") {
  const t = el("div", { class: `toast ${kind}` }, msg);
  document.body.append(t);
  setTimeout(() => t.remove(), 2600);
}

// Briefly confirm a command was accepted, inline next to its control. Reuses a
// single note per target so repeat clicks don't stack. ok=false shows a red ✕.
export function flashApplied(target, ok = true) {
  if (!target) return;
  let note = target._appliedNote;
  if (!note || !note.isConnected) {
    note = el("span", { class: "applied-note" });
    target._appliedNote = note;
    target.after(note);
  }
  clearTimeout(note._t);
  note.textContent = ok ? "✓ applied" : "✕ failed";
  note.className = `applied-note ${ok ? "ok" : "err"} show`;
  note._t = setTimeout(() => note.classList.remove("show"), 1800);
}
