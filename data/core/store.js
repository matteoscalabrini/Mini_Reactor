// Reactive telemetry + connection state. No DOM.
const subs = new Set();
const connSubs = new Set();
let state = null;
let online = false;

export function setStatus(s) { state = s; for (const fn of subs) fn(state); }
export function getStatus() { return state; }
export function subscribe(fn) { subs.add(fn); if (state) fn(state); return () => subs.delete(fn); }

export function setOnline(v) { online = v; for (const fn of connSubs) fn(v); }
export function isOnline() { return online; }
export function subscribeConn(fn) { connSubs.add(fn); fn(online); return () => connSubs.delete(fn); }
