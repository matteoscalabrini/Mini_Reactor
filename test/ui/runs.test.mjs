import assert from "node:assert/strict";
import { parseRunList, latestRunId } from "../../data/core/runs.js";

assert.deepEqual(parseRunList({}), []);
assert.deepEqual(parseRunList(null), []);
assert.equal(latestRunId([]), null);
assert.equal(latestRunId(null), null);

const list = parseRunList({ runs: [
  { id: 3, startedSec: 10, durationSec: 60, bytes: 100 },
  { id: 5, label: "Run 5", current: true },
] });
assert.equal(list.length, 2);
assert.equal(list[0].label, "Run 3");      // label fallback
assert.equal(list[0].current, false);
assert.equal(list[1].current, true);

assert.equal(latestRunId(list), 5);        // current wins

const noCur = parseRunList({ runs: [{ id: 2 }, { id: 7 }, { id: 4 }] });
assert.equal(latestRunId(noCur), 7);       // else max id

console.log("runs.test OK");
