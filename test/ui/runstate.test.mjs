import assert from "node:assert/strict";
import { deriveRunbar, visibleRows } from "../../data/core/runstate.js";

assert.deepEqual(deriveRunbar({ run: { active: false } }),
  { mode: "idle", held: null, startBlocked: false });
assert.deepEqual(deriveRunbar({ run: { active: true } }),
  { mode: "running", held: null, startBlocked: false });
assert.deepEqual(deriveRunbar({ run: { active: true, pause: { motor: true } } }),
  { mode: "paused", held: "motor", startBlocked: false });
assert.equal(deriveRunbar({ run: { active: true, pause: { heater: true } } }).held, "heater");
assert.equal(deriveRunbar({ run: { active: true, pause: { motor: true, heater: true } } }).held, "all");
assert.equal(deriveRunbar({ run: { active: false },
  thermal: { safety: { probe: { resistanceOhms: null } } } }).startBlocked, true);
assert.equal(deriveRunbar({ run: { active: false },
  thermal: { safety: { tripped: true } } }).startBlocked, true);
assert.equal(deriveRunbar(undefined).mode, "idle");

const rows = Array.from({ length: 25 }, (_, i) => i);
assert.equal(visibleRows(rows, false).length, 10);
assert.equal(visibleRows(rows, true).length, 25);
assert.deepEqual(visibleRows(rows, false), [0, 1, 2, 3, 4, 5, 6, 7, 8, 9]);
assert.deepEqual(visibleRows(null, false), []);

console.log("runstate.test OK");
