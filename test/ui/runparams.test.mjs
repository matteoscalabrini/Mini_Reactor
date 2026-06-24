import assert from "node:assert/strict";
import { createRunParams } from "../../data/core/runparams.js";

function shim() {
  const m = {};
  return { getItem: (k) => (k in m ? m[k] : null), setItem: (k, v) => { m[k] = v; }, _m: m };
}

// defaults when storage empty
let s = shim();
let rp = createRunParams(s);
assert.deepEqual(rp.get(), { targetC: 36, rpm: 8, durationMin: 0 });

// set merges + persists
rp.set({ targetC: 42 });
assert.equal(rp.get().targetC, 42);
assert.equal(rp.get().rpm, 8);
assert.ok(s._m["reactor.runparams"].includes("42"));

// a fresh instance reads persisted values
let rp2 = createRunParams(s);
assert.equal(rp2.get().targetC, 42);

// subscribe fires immediately + on change, unsub stops it
let seen = [];
const unsub = rp2.subscribe((v) => seen.push(v.targetC));
assert.equal(seen[0], 42);
rp2.set({ targetC: 30 });
assert.equal(seen[1], 30);
unsub();
rp2.set({ targetC: 99 });
assert.equal(seen.length, 2);

console.log("runparams.test OK");
