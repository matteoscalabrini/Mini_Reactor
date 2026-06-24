// Pure helpers for the saved-run list. No DOM.
export function parseRunList(json) {
  const runs = (json && json.runs) || [];
  return runs.map((r) => ({
    id: r.id,
    label: r.label || `Run ${r.id}`,
    startedSec: r.startedSec == null ? null : r.startedSec,
    durationSec: r.durationSec == null ? null : r.durationSec,
    bytes: r.bytes || 0,
    current: !!r.current,
  }));
}

// Default picker selection: the current run if any, else the highest id.
export function latestRunId(runs) {
  if (!runs || !runs.length) return null;
  const cur = runs.find((r) => r.current);
  if (cur) return cur.id;
  return runs.reduce((m, r) => (r.id > m ? r.id : m), runs[0].id);
}
