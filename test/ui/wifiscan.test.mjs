import assert from "node:assert/strict";
import { pollScan } from "../../data/core/wifiscan.js";

const noSleep = () => Promise.resolve();
// scanFn that returns successive responses (clamped at the last), shaped like api.wifiScan().
const seq = (responses) => { let i = 0; return async () => ({ body: responses[Math.min(i++, responses.length - 1)] }); };

const A = [{ ssid: "A", rssi: -40, secure: true }, { ssid: "B", rssi: -60, secure: false }];

// Device async protocol: the trigger + first polls report the IDLE/empty cache, then
// scanning:true, then results. Must NOT break on the initial idle {scanning:false, networks:[]}.
let nets = await pollScan(seq([
  { scanning: false, networks: [] },   // trigger response (idle) — ignored
  { scanning: true, networks: [] },    // scan running
  { scanning: true, networks: [] },
  { scanning: false, networks: A },    // done
]), noSleep, 10, 0);
assert.deepEqual(nets, A, "waits through idle -> scanning -> done");

// Mock-style: results available immediately (scanning:false with networks).
nets = await pollScan(seq([
  { scanning: false, networks: A },
  { scanning: false, networks: A },
]), noSleep, 10, 0);
assert.deepEqual(nets, A, "returns immediate results");

// A real scan that finds nothing -> [] (only after it actually ran).
nets = await pollScan(seq([
  { scanning: false, networks: [] },   // trigger
  { scanning: true, networks: [] },    // running
  { scanning: false, networks: [] },   // done, none found
]), noSleep, 10, 0);
assert.deepEqual(nets, [], "empty result after a real scan");

console.log("wifiscan.test OK");
