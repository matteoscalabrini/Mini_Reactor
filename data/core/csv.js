// Minimal CSV parser for the reactor log. Numeric coercion happens at the call site.
export function parseCsv(text) {
  const lines = String(text).split(/\r?\n/).filter((l) => l.trim().length);
  if (!lines.length) return { header: [], rows: [] };
  return { header: lines[0].split(","), rows: lines.slice(1).map((l) => l.split(",")) };
}
