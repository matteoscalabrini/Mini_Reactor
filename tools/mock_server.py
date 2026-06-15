#!/usr/bin/env python3
"""Mock backend for the Mini-Reactor web UI — lets you develop the dashboard in a
browser with no hardware. Serves data/ and fakes the firmware's REST + WebSocket
API, simulating a heated rotating-disk reactor (first-order thermal model).

Run:  /tmp/pio-venv/bin/python tools/mock_server.py
Open: http://localhost:8000
"""
import asyncio, json, time, random, pathlib
from aiohttp import web, WSMsgType

DATA = pathlib.Path(__file__).resolve().parent.parent / "data"
AMBIENT = 22.0

state = {
    "running": False, "targetC": 30.0, "motorPct": 40.0, "durationMin": 0,
    "tempC": AMBIENT, "heaterC": AMBIENT, "heaterPct": 0.0, "startMs": 0,
    "fault": False, "safety": False,
    "ssid": "", "connected": True, "ap": False, "ip": "192.168.1.42",
}
clients = set()
log_rows = ["t_ms,running,temp_c,setpoint_c,heater_pct,motor_pct,fault"]


def status():
    now = time.monotonic()
    elapsed = int(now - state["startMs"]) if state["running"] else 0
    remaining = 0
    if state["running"] and state["durationMin"] > 0:
        remaining = max(0, state["durationMin"] * 60 - elapsed)
    return {
        "reactor": {
            "running": state["running"],
            "tempC": None if state["fault"] else round(state["tempC"], 2),
            "heaterC": round(state["heaterC"], 1),
            "setpointC": state["targetC"],
            "heaterPct": round(state["heaterPct"], 1),
            "motorPct": state["motorPct"] if state["running"] else 0,
            "fault": state["fault"], "safety": state["safety"],
            "elapsedSec": elapsed, "remainingSec": remaining,
            "durationMin": state["durationMin"],
        },
        "wifi": {"connected": state["connected"], "ap": state["ap"], "ip": state["ip"]},
        "vbus": "12V", "sdMounted": True, "uptimeSec": int(now),
    }


async def simulate():
    """First-order thermal sim + simple P heater, pushed to all WS clients."""
    dt = 0.25
    while True:
        s = state
        if s["running"]:
            err = s["targetC"] - s["tempC"]
            s["heaterPct"] = max(0.0, min(100.0, 12.0 * err)) if not s["fault"] else 0.0
            duty = s["heaterPct"] / 100.0
            s["tempC"] += (0.6 * duty - 0.02 * (s["tempC"] - AMBIENT)) * dt
        else:
            s["heaterPct"] = 0.0
            s["tempC"] += (-0.02 * (s["tempC"] - AMBIENT)) * dt
        s["tempC"] += random.uniform(-0.04, 0.04)
        # Heater-mounted NTC: runs hotter than the bath, tracks duty.
        target_heat = s["tempC"] + (s["heaterPct"] / 100.0) * 42.0
        s["heaterC"] += (target_heat - s["heaterC"]) * 0.25 + random.uniform(-0.1, 0.1)
        s["safety"] = s["heaterC"] >= 80.0
        if s["safety"]:
            s["heaterPct"] = 0.0
        # auto-stop on duration
        if s["running"] and s["durationMin"] > 0:
            if time.monotonic() - s["startMs"] >= s["durationMin"] * 60:
                s["running"] = False
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
        state.update(running=True, targetC=float(b.get("targetC", 30)),
                     motorPct=float(b.get("motorPercent", 40)),
                     durationMin=int(b.get("durationMin", 0)),
                     startMs=time.monotonic())
    elif b.get("action") == "stop":
        state["running"] = False
    return web.json_response({"ok": True})


async def api_setpoint(req):
    b = await req.json()
    if "targetC" in b:
        state["targetC"] = float(b["targetC"])
    if "motorPercent" in b:
        state["motorPct"] = float(b["motorPercent"])
    return web.json_response({"ok": True})


async def api_scan(req):
    nets = [{"ssid": n, "rssi": r, "secure": sec} for n, r, sec in [
        ("LAB-NET-5G", -42, True), ("fermentation_floor", -58, True),
        ("ESP-GUEST", -67, False), ("BUILDING-IOT", -74, True)]]
    return web.json_response({"scanning": False, "networks": nets})


async def api_connect(req):
    b = await req.json()
    state.update(ssid=b.get("ssid", ""), connected=True, ap=False)
    return web.json_response({"ok": True})


async def api_forget(req):
    state.update(connected=False, ap=True, ip="192.168.4.1")
    return web.json_response({"ok": True})


async def api_log(req):
    return web.Response(text="\n".join(log_rows) + "\n", content_type="text/csv")


async def api_log_clear(req):
    del log_rows[1:]
    return web.json_response({"ok": True})


def main():
    app = web.Application()
    app.router.add_get("/ws", ws_handler)
    app.router.add_get("/api/status", api_status)
    app.router.add_post("/api/run", api_run)
    app.router.add_post("/api/setpoint", api_setpoint)
    app.router.add_get("/api/wifi/scan", api_scan)
    app.router.add_post("/api/wifi/connect", api_connect)
    app.router.add_post("/api/wifi/forget", api_forget)
    app.router.add_get("/api/log", api_log)
    app.router.add_post("/api/log/clear", api_log_clear)
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
