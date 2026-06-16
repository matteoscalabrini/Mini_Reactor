import assert from "node:assert/strict";
import { makeBuffer, sample, bounds, points } from "../../data/core/chart.js";

const b = makeBuffer(3, 1000);
assert.equal(sample(b, 0, 10, 12), true, "first sample taken");
assert.equal(sample(b, 500, 11, 12), false, "throttled within period");
assert.equal(sample(b, 1000, 20, 12), true, "sample after period");
assert.equal(sample(b, 2000, NaN, 12), false, "NaN skipped");
assert.equal(sample(b, 3000, 30, 12), true);
assert.equal(sample(b, 4000, 40, 12), true);
assert.deepEqual(b.t, [20, 30, 40], "ring buffer caps at maxPts");

const bb = bounds(b);
assert.ok(bb.lo < 12 && bb.hi > 40, "bounds padded around data");
const pts = points([0, 10], { lo: 0, hi: 10 }, 100, 50);
assert.deepEqual(pts[0], [0, 50], "first point bottom-left");
assert.deepEqual(pts[1], [100, 0], "last point top-right");
console.log("chart.test OK");
