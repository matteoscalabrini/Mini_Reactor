import assert from "node:assert/strict";
import { featureEnabled } from "../../data/core/features.js";

assert.equal(featureEnabled({}, "sdLogging"), true);                 // no features map -> enabled
assert.equal(featureEnabled(null, "autotune"), true);               // no status -> enabled
assert.equal(featureEnabled({ features: {} }, "sdLogging"), true);  // key absent -> enabled
assert.equal(featureEnabled({ features: { sdLogging: false } }, "sdLogging"), false);
assert.equal(featureEnabled({ features: { sdLogging: true } }, "sdLogging"), true);
assert.equal(featureEnabled({ features: { oledUi: false } }, "oledUi"), false);

console.log("features.test OK");
