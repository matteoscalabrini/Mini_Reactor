#!/usr/bin/env python3
"""Mock backend for the Mini-Reactor /api/v1 contract — develop the future UI in a
browser with no hardware. Serves data/ and fakes the firmware's v1 REST + WebSocket
API: nested telemetry, rpm-based disc drive, NTC safety probe, first-order liquid
thermal model.

Run:  /tmp/pio-venv/bin/python tools/mock_server.py   (needs aiohttp)
Open: http://localhost:8000
"""
import asyncio, json, time, random, pathlib
from aiohttp import web

DATA = pathlib.Path(__file__).resolve().parent.parent / "data"
AMBIENT = 22.0


def clamp_rpm(rpm):
    # Mirror the firmware's Reactor::setRpm clamp: 0 = stop, else [0.5, 30].
    if rpm <= 0:
        return 0.0
    if rpm < 0.5:
        return 0.5
    if rpm > 30:
        return 30.0
    return rpm


state = {
    "running": False, "targetC": 36.0, "rpm": 8.0, "rpmSetpoint": 8.0,
    "durationMin": 0, "tempC": AMBIENT, "heaterPct": 0.0, "startMs": 0.0,
    "fault": False, "heaterProbeFault": False, "heaterTempC": 24.0, "safetyTripped": False,
    "currentMa": 600, "microsteps": 16, "reverse": False, "enabled": True,
    "load": 280, "loadBias": 0.0,
    "drvOt": False, "drvOtpw": False, "drvStall": False,
    "drvOpenLoadA": False, "drvOpenLoadB": False, "drvShortA": False, "drvShortB": False,
    "ssid": "LAB-NET-5G", "connected": True, "ap": False, "ip": "192.168.1.42",
    "kp": 0.08, "ki": 0.0015, "kd": 0.4, "pidMode": "auto",
    "atActive": False, "atProgress": 0, "atResult": None, "atStartMs": 0.0,
    "calMethod": "beta", "calibrated": False, "calPoints": [], "ntcAdc": 1820, "ntcR": 9120.0,
}
clients = set()
_alarm_since = {}


def status():
    now = time.monotonic()
    elapsed = int(now - state["startMs"]) if state["running"] else 0
    remaining = None
    if state["running"] and state["durationMin"] > 0:
        remaining = max(0, state["durationMin"] * 60 - elapsed)
    temp = None if state["fault"] else round(state["tempC"], 2)
    err = None if temp is None else round(state["targetC"] - state["tempC"], 2)
    heaterC = None if state["heaterProbeFault"] else round(state["heaterTempC"], 1)
    rpm = state["rpm"] if state["running"] else 0.0
    active = []
    if state["fault"]:
        active.append(("sensor_fault", "warn"))
    if state["heaterProbeFault"]:
        active.append(("heater_probe_fault", "warn"))
    if state["safetyTripped"]:
        active.append(("safety_tripped", "critical"))
    if state["drvOt"]:
        active.append(("driver_ot", "critical"))
    elif state["drvOtpw"]:
        active.append(("driver_otpw", "warn"))
    if state["drvStall"]:
        active.append(("driver_stall", "warn"))
    if state["drvOpenLoadA"] or state["drvOpenLoadB"]:
        active.append(("driver_open_load", "warn"))
    now = int(time.monotonic())
    codes = {c for c, _ in active}
    for c in list(_alarm_since):
        if c not in codes:
            del _alarm_since[c]
    alarms = []
    for code, sev in active:
        _alarm_since.setdefault(code, now)
        alarms.append({"code": code, "severity": sev, "since": _alarm_since[code]})
    return {
        "apiVersion": "1.0", "uptimeSec": int(now),
        "system": {"firmware": "1.0.0-mock", "freeHeap": 142000,
                   "vbus": "12V", "sdMounted": True},
        "thermal": {
            "tempC": temp, "setpointC": state["targetC"], "errorC": err,
            "heaterPct": round(state["heaterPct"], 1), "fault": state["fault"],
            "safety": {"tripped": state["safetyTripped"],
                       "heaterTempC": heaterC,
                       "heaterMaxC": 80.0, "processMaxC": 55.0,
                       "probe": {"adcRaw": state["ntcAdc"],
                                 "resistanceOhms": None if state["heaterProbeFault"]
                                                   else round(state["ntcR"]),
                                 "calibrated": state["calibrated"],
                                 "method": state["calMethod"]}},
            "pid": {
                "kp": state["kp"], "ki": state["ki"], "kd": state["kd"],
                "p": 0.0, "i": 0.0, "d": 0.0,
                "out": round(state["heaterPct"] / 100.0, 3),
                "mode": "autotune" if state["atActive"] else state["pidMode"],
                "autotune": {"active": state["atActive"],
                             "progress": state["atProgress"],
                             "result": state["atResult"]},
            },
        },
        "disc": {
            "running": state["running"], "rpm": rpm,
            "rpmSetpoint": state["rpmSetpoint"],
            "direction": "ccw" if state["reverse"] else "cw",
            "currentMa": state["currentMa"], "microsteps": state["microsteps"],
            "enabled": state["enabled"],
            "load": state["load"] if state["running"] else None,
            "driver": {"version": "0x21", "connected": True, "flags": {
                "otpw": state["drvOtpw"], "ot": state["drvOt"], "stall": state["drvStall"],
                "openLoadA": state["drvOpenLoadA"], "openLoadB": state["drvOpenLoadB"],
                "shortA": state["drvShortA"], "shortB": state["drvShortB"]}},
        },
        "run": {"active": state["running"], "elapsedSec": elapsed,
                "remainingSec": remaining, "durationMin": state["durationMin"]},
        "wifi": {"mode": "ap" if state["ap"] else "sta",
                 "connected": state["connected"], "ssid": state["ssid"],
                 "ip": state["ip"], "rssi": -54 if state["connected"] else None},
        "storage": {"sdMounted": True, "logBytes": None, "logging": True},
        "alarms": alarms,
    }


async def simulate():
    dt = 0.25
    while True:
        s = state
        if s["running"]:
            err = s["targetC"] - s["tempC"]
            s["heaterPct"] = max(0.0, min(100.0, 12.0 * err))
            duty = s["heaterPct"] / 100.0
            s["tempC"] += (0.6 * duty - 0.02 * (s["tempC"] - AMBIENT)) * dt
            s["heaterTempC"] = s["tempC"] + 18.0 * duty  # heater runs hotter than bath
            s["loadBias"] = min(120.0, s["loadBias"] + 0.05)   # biofilm slowly loads the disc
            s["load"] = int(max(0, 380 - s["loadBias"] + random.uniform(-8, 8)))
            if s["durationMin"] > 0 and \
               time.monotonic() - s["startMs"] >= s["durationMin"] * 60:
                s["running"] = False
        else:
            s["heaterPct"] = 0.0
            s["tempC"] += (-0.02 * (s["tempC"] - AMBIENT)) * dt
            s["heaterTempC"] += (-0.05 * (s["heaterTempC"] - AMBIENT)) * dt
            s["load"] = 0
            s["loadBias"] = 0.0
        if s["atActive"]:
            s["atProgress"] = min(100, s["atProgress"] + 5)
            if s["atProgress"] >= 100:
                s["atActive"] = False
                s["atResult"] = "ok"
                s["kp"], s["ki"], s["kd"] = 0.12, 0.0021, 0.55  # "tuned" gains
                s["pidMode"] = "auto"
        s["tempC"] += random.uniform(-0.04, 0.04)
        payload = json.dumps(status())
        for ws in list(clients):
            try:
                await ws.send_str(payload)
            except Exception:
                clients.discard(ws)
        await asyncio.sleep(dt)


async def ws_handler(req):
    ws = web.WebSocketResponse()
    await ws.prepare(req)
    clients.add(ws)
    await ws.send_str(json.dumps(status()))
    try:
        async for _ in ws:
            pass
    finally:
        clients.discard(ws)
    return ws


async def api_status(req):
    return web.json_response(status())


async def api_run(req):
    b = await req.json()
    if b.get("action") == "start":
        rpm = float(b.get("rpm", 8))
        targetC = float(b.get("targetC", 36))
        if rpm < 0 or rpm > 30:
            return web.json_response(
                {"ok": False, "error": {"code": "out_of_range",
                                        "message": "rpm must be 0..30"}}, status=400)
        if targetC < 0 or targetC > 55:
            return web.json_response(
                {"ok": False, "error": {"code": "out_of_range",
                                        "message": "targetC must be 0..55"}}, status=400)
        rpm = clamp_rpm(rpm)
        if state["heaterProbeFault"]:
            # Mirror firmware pre-flight: heater NTC faulted -> start refused, run stays
            # idle. /run still acks (async queue); refusal observed via GET /status.
            return web.json_response({"ok": True})
        state.update(running=True, targetC=targetC,
                     rpm=rpm, rpmSetpoint=rpm,
                     durationMin=int(b.get("durationMin", 0)),
                     startMs=time.monotonic())
        return web.json_response({"ok": True})
    if b.get("action") == "stop":
        state["running"] = False
        return web.json_response({"ok": True})
    return web.json_response(
        {"ok": False, "error": {"code": "invalid_request",
                                "message": "action must be start|stop"}}, status=400)


async def api_debug_probe_fault(req):
    # Mock-only: simulate a disconnected/faulted heater NTC for UI/integration testing.
    b = await req.json()
    state["heaterProbeFault"] = bool(b.get("fault", False))
    return web.json_response({"ok": True, "heaterProbeFault": state["heaterProbeFault"]})


async def api_setpoint(req):
    b = await req.json()
    if "targetC" in b:
        targetC = float(b["targetC"])
        if targetC < 0 or targetC > 55:
            return web.json_response(
                {"ok": False, "error": {"code": "out_of_range",
                                        "message": "targetC must be 0..55"}}, status=400)
        state["targetC"] = targetC
    if "rpm" in b:
        state["rpm"] = state["rpmSetpoint"] = clamp_rpm(float(b["rpm"]))
    return web.json_response({"ok": True})


async def api_disc(req):
    b = await req.json()
    if "rpm" in b:
        state["rpm"] = state["rpmSetpoint"] = clamp_rpm(float(b["rpm"]))
    if "currentMa" in b:
        state["currentMa"] = int(b["currentMa"])
    if "microsteps" in b:
        state["microsteps"] = int(b["microsteps"])
    if "direction" in b:
        state["reverse"] = (b["direction"] == "ccw")
    if "enabled" in b:
        state["enabled"] = bool(b["enabled"])
    return web.json_response({"ok": True})


async def api_scan(req):
    nets = [{"ssid": n, "rssi": r, "secure": sec} for n, r, sec in [
        ("LAB-NET-5G", -42, True), ("fermentation_floor", -58, True),
        ("ESP-GUEST", -67, False), ("BUILDING-IOT", -74, True)]]
    return web.json_response({"scanning": False, "networks": nets})


async def api_connect(req):
    b = await req.json()
    if not b.get("ssid"):
        return web.json_response(
            {"ok": False, "error": {"code": "wifi_ssid_required",
                                    "message": "ssid is required"}}, status=400)
    state.update(ssid=b["ssid"], connected=True, ap=False)
    return web.json_response({"ok": True})


async def api_forget(req):
    state.update(connected=False, ap=True, ip="192.168.4.1")
    return web.json_response({"ok": True})


async def api_log(req):
    header = "t_ms,running,liquid_c,heater_c,setpoint_c,heater_pct,rpm,load,fault,safety"
    return web.Response(text=header + "\n", content_type="text/csv")


async def api_log_clear(req):
    return web.json_response({"ok": True})


async def api_pid(req):
    b = await req.json()
    if "kp" in b and "ki" in b and "kd" in b:
        state["kp"], state["ki"], state["kd"] = float(b["kp"]), float(b["ki"]), float(b["kd"])
    if "mode" in b:
        state["pidMode"] = "manual" if b["mode"] == "manual" else "auto"
    return web.json_response({"ok": True})


async def api_autotune(req):
    b = await req.json()
    action = b.get("action")
    if action == "start":
        state.update(atActive=True, atProgress=0, atResult=None, atStartMs=time.monotonic())
        return web.json_response({"ok": True})
    if action == "cancel":
        state.update(atActive=False, atResult=None)
        return web.json_response({"ok": True})
    return web.json_response(
        {"ok": False, "error": {"code": "invalid_request",
                                "message": "action must be start|cancel"}}, status=400)


async def api_calibration(req):
    return web.json_response({"method": state["calMethod"], "calibrated": state["calibrated"],
                              "points": state["calPoints"]})


async def api_cal_point(req):
    b = await req.json()
    if "referenceC" not in b:
        return web.json_response(
            {"ok": False, "error": {"code": "invalid_request",
                                    "message": "referenceC required"}}, status=400)
    state["calPoints"].append({"referenceC": float(b["referenceC"]),
                               "resistanceOhms": round(state["ntcR"])})
    return web.json_response({"ok": True})


async def api_cal_compute(req):
    # Mirrors the firmware's async ack: always {"ok":true}; 0 points → no change
    # (calibrated stays False), observed via GET /calibration.
    n = len(state["calPoints"])
    if n >= 1:
        state["calMethod"] = "offset" if n == 1 else ("beta" if n == 2 else "steinhart")
        state["calibrated"] = True
    return web.json_response({"ok": True})


async def api_cal_reset(req):
    state.update(calMethod="beta", calibrated=False, calPoints=[])
    return web.json_response({"ok": True})


def main():
    app = web.Application()
    app.router.add_get("/ws", ws_handler)
    app.router.add_get("/api/v1/status", api_status)
    app.router.add_post("/api/v1/run", api_run)
    app.router.add_post("/api/v1/debug/probe-fault", api_debug_probe_fault)
    app.router.add_post("/api/v1/setpoint", api_setpoint)
    app.router.add_post("/api/v1/disc", api_disc)
    app.router.add_get("/api/v1/wifi/scan", api_scan)
    app.router.add_post("/api/v1/wifi/connect", api_connect)
    app.router.add_post("/api/v1/wifi/forget", api_forget)
    app.router.add_get("/api/v1/log", api_log)
    app.router.add_post("/api/v1/log/clear", api_log_clear)
    app.router.add_post("/api/v1/pid", api_pid)
    app.router.add_post("/api/v1/pid/autotune", api_autotune)
    app.router.add_get("/api/v1/calibration", api_calibration)
    app.router.add_post("/api/v1/calibration/point", api_cal_point)
    app.router.add_post("/api/v1/calibration/compute", api_cal_compute)
    app.router.add_post("/api/v1/calibration/reset", api_cal_reset)
    app.router.add_get("/", lambda r: web.FileResponse(DATA / "index.html"))
    app.router.add_static("/", DATA)

    async def on_start(a):
        a["sim"] = asyncio.create_task(simulate())

    async def on_stop(a):
        a["sim"].cancel()

    app.on_startup.append(on_start)
    app.on_cleanup.append(on_stop)
    web.run_app(app, host="127.0.0.1", port=8000)


if __name__ == "__main__":
    main()
