// Drive the firmware's ASYNC WiFi scan protocol:
//   GET /wifi/scan queues a scan and returns the current cache. The cache is initially
//   idle {scanning:false, networks:[]}, then becomes {scanning:true,...} once the scan
//   starts, then {scanning:false, networks:[...]} when it finishes (~2-4s).
// We must NOT accept the initial idle response (empty array is truthy) — wait until the
// scan has actually run. `scanFn` returns api.wifiScan()'s shape: {body:{scanning,networks}}.
export async function pollScan(scanFn, sleep, tries = 20, periodMs = 500) {
  await scanFn();                       // trigger the scan; ignore the idle response
  let started = false;
  for (let i = 0; i < tries; i++) {
    await sleep(periodMs);
    const b = (await scanFn()).body || {};
    if (b.scanning) { started = true; continue; }            // scan in progress
    if (started || (b.networks && b.networks.length)) {      // finished (ran), or has results
      return b.networks || [];
    }
  }
  return [];                            // timed out with no result
}
