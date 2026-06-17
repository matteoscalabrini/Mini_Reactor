import assert from "node:assert/strict";
import { parseCsv } from "../../data/core/csv.js";

let r = parseCsv("a,b,c\n1,2,3\n4,5,6\n");
assert.deepEqual(r.header, ["a", "b", "c"]);
assert.equal(r.rows.length, 2);
assert.deepEqual(r.rows[1], ["4", "5", "6"]);

// blank and trailing lines are ignored
r = parseCsv("h1,h2\n\n10,20\n\n");
assert.equal(r.rows.length, 1);
assert.deepEqual(r.rows[0], ["10", "20"]);

// empty input -> empty
assert.deepEqual(parseCsv("").rows, []);
assert.deepEqual(parseCsv("").header, []);
console.log("csv.test OK");
