import { setStatus, setOnline } from "./store.js";

let backoff = 1000;
export function start() {
  const ws = new WebSocket(`ws://${location.host}/ws`);
  ws.onopen = () => { setOnline(true); backoff = 1000; };
  ws.onclose = () => { setOnline(false); setTimeout(start, backoff); backoff = Math.min(backoff * 2, 10000); };
  ws.onmessage = (e) => { try { setStatus(JSON.parse(e.data)); } catch (_) {} };
}
