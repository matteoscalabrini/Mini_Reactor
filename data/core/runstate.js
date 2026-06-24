// Pure derivation for the run-control bar + history table. No DOM.
export function deriveRunbar(status) {
  const run = (status && status.run) || {};
  const safety = ((status && status.thermal) || {}).safety || {};
  const pause = run.pause || {};
  const active = !!run.active;
  const motor = !!pause.motor;
  const heater = !!pause.heater;
  const paused = active && (motor || heater);
  const mode = !active ? "idle" : paused ? "paused" : "running";
  const held = !paused ? null : (motor && heater) ? "all" : motor ? "motor" : "heater";
  const probeFault = !!(safety.probe && safety.probe.resistanceOhms == null);
  const startBlocked = probeFault || !!safety.tripped;
  return { mode, held, startBlocked };
}

// Rows are newest-first; show the first `cap` unless expanded.
export function visibleRows(rows, expanded, cap = 10) {
  if (!Array.isArray(rows)) return [];
  return expanded ? rows.slice() : rows.slice(0, cap);
}
