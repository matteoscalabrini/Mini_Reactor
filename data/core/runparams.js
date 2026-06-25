// Shared run parameters (target / rpm / duration), persisted so the global
// Start button works from any page. Storage is injectable for host tests.
const DEFAULTS = { targetC: 36, rpm: 8, durationMin: 0, name: "" };
const KEY = "reactor.runparams";

function memoryStorage() {
  const m = {};
  return { getItem: (k) => (k in m ? m[k] : null), setItem: (k, v) => { m[k] = v; } };
}

export function createRunParams(storage) {
  const store = storage
    || (typeof globalThis !== "undefined" && globalThis.localStorage) || memoryStorage();
  const subs = new Set();
  let state = { ...DEFAULTS };
  try {
    const raw = store.getItem(KEY);
    if (raw) state = { ...DEFAULTS, ...JSON.parse(raw) };
  } catch (_) { /* keep defaults */ }

  const get = () => ({ ...state });
  function set(patch) {
    state = { ...state, ...patch };
    try { store.setItem(KEY, JSON.stringify(state)); } catch (_) { /* ignore */ }
    for (const fn of subs) fn(get());
  }
  function subscribe(fn) { subs.add(fn); fn(get()); return () => subs.delete(fn); }
  return { get, set, subscribe };
}

export const runParams = createRunParams();
