// Feature availability from /api/v1/status. A missing features map or key means
// enabled — back-compat with firmware built before the toggle scaffold. No DOM.
export function featureEnabled(status, key) {
  const f = status && status.features;
  if (!f || f[key] == null) return true;
  return !!f[key];
}
