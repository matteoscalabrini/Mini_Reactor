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

// Download filename for a run CSV: the session name when set, else run_<id>.csv.
// "Run <id>" is the firmware's label for an unnamed run, so it's treated as
// unnamed. The name becomes the browser's saved filename, so keep only a safe
// whitelist (letters/digits/space/._-), collapse runs to "_", and cap the length.
export function runFileName(label, id) {
  const auto = !label || /^Run \d+$/.test(label);
  const safe = (auto ? `run_${id}` : label)
    .replace(/[^a-zA-Z0-9 ._-]+/g, " ")
    .trim()
    .replace(/\s+/g, "_")
    .slice(0, 48);
  return `${safe || `run_${id}`}.csv`;
}
