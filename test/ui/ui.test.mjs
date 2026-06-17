import assert from "node:assert/strict";
import { hhmmss, fixed, dash } from "../../data/core/ui.js";

assert.equal(hhmmss(3661), "01:01:01");
assert.equal(hhmmss(-5), "00:00:00");
assert.equal(fixed(30.42, 1), "30.4");
assert.equal(fixed(30.96, 1), "31.0");
assert.equal(fixed(null), "—");
assert.equal(fixed(NaN), "—");
assert.equal(dash(null), "—");
assert.equal(dash(0), 0);
console.log("ui.test OK");
