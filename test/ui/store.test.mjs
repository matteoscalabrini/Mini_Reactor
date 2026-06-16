import assert from "node:assert/strict";
import * as store from "../../data/core/store.js";

let seen = null, conn = null;
const off = store.subscribe((s) => { seen = s; });
store.subscribeConn((v) => { conn = v; });

store.setStatus({ uptimeSec: 5 });
assert.equal(seen.uptimeSec, 5, "subscriber receives status");
assert.equal(store.getStatus().uptimeSec, 5, "getStatus returns latest");

off();
store.setStatus({ uptimeSec: 9 });
assert.equal(seen.uptimeSec, 5, "unsubscribe stops updates");

store.setOnline(true);
assert.equal(conn, true, "conn subscriber notified");
assert.equal(store.isOnline(), true);
console.log("store.test OK");
