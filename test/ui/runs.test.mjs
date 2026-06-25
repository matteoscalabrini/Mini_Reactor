import assert from "node:assert/strict";
import { parseRunList, latestRunId, runFileName } from "../../data/core/runs.js";

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

// runFileName: named -> name.csv; unnamed ("Run N" / empty) -> run_<id>.csv
assert.equal(runFileName("jimbo", 1), "jimbo.csv");
assert.equal(runFileName("Run 5", 5), "run_5.csv");          // firmware unnamed label
assert.equal(runFileName("", 3), "run_3.csv");               // missing label
assert.equal(runFileName(null, 9), "run_9.csv");
assert.equal(runFileName("Ethanol distillation", 2), "Ethanol_distillation.csv");
assert.equal(runFileName("a/b:c*?", 4), "a_b_c.csv");        // FAT-illegal chars dropped
assert.equal(runFileName("   ", 6), "run_6.csv");            // whitespace-only -> fallback
assert.equal(runFileName("x".repeat(80), 7).length, 52);     // 48 cap + ".csv"

console.log("runs.test OK");
