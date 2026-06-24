// REST transport-out. Every command acks async; callers observe results via telemetry.
async function req(method, path, body) {
  try {
    const r = await fetch(path, {
      method,
      headers: body ? { "Content-Type": "application/json" } : {},
      body: body ? JSON.stringify(body) : undefined,
    });
    const j = await r.json().catch(() => ({}));
    return { ok: r.ok && j.ok !== false, status: r.status, body: j };
  } catch (_) { return { ok: false, status: 0, body: {} }; }
}
export const get = (p) => req("GET", p);
export const post = (p, b) => req("POST", p, b || {});

export const runStart = (targetC, rpm, durationMin) => post("/api/v1/run", { action: "start", targetC, rpm, durationMin });
export const runPause = (target) => post("/api/v1/run", { action: "pause", target });
export const runResume = () => post("/api/v1/run", { action: "resume" });
export const runStop = (data) => post("/api/v1/run", { action: "stop", data });
export const listRuns = () => get("/api/v1/runs");
export const runCsvUrl = (id) => `/api/v1/runs/${id}`;
export const deleteRun = (id) => post(`/api/v1/runs/${id}/delete`);
export const setpoint = (o) => post("/api/v1/setpoint", o);
export const disc = (o) => post("/api/v1/disc", o);
export const pidGains = (kp, ki, kd) => post("/api/v1/pid", { kp, ki, kd });
export const pidMode = (mode) => post("/api/v1/pid", { mode });
export const autotune = (action) => post("/api/v1/pid/autotune", { action });
export const getCalibration = () => get("/api/v1/calibration");
export const calPoint = (referenceC) => post("/api/v1/calibration/point", { referenceC });
export const calCompute = () => post("/api/v1/calibration/compute");
export const calReset = () => post("/api/v1/calibration/reset");
export const wifiScan = () => get("/api/v1/wifi/scan");
export const wifiConnect = (ssid, password) => post("/api/v1/wifi/connect", { ssid, password });
export const wifiForget = () => post("/api/v1/wifi/forget");
export const logClear = () => post("/api/v1/log/clear");
