#!/usr/bin/env python3
"""
heapmon.py — network-side heap monitor for the Mini-Reactor.

Polls GET /api/v1/status and appends one CSV row per sample. Designed to run
unattended until the reactor's network stack dies (the ~30min-2h death), so the
tail of the CSV is the evidence: leak (freeHeap slope down), fragmentation
(freeHeap flat, largestBlock collapsing), or event death (all flat, then gone).

The system.* diagnostic fields (minFreeHeap/largestBlock/freeDma/minFreeDma)
exist only in the instrumented firmware (working-tree build) — older firmware
rows just leave those columns blank.

Usage:
    python3 tools/heapmon.py <device-ip> [--period 10] [--out heapmon.csv]

Stdlib only. Ctrl-C to stop; the CSV survives.
"""

import argparse
import base64
import csv
import datetime
import json
import os
import socket
import subprocess
import sys
import threading
import time
import urllib.request


def ping(host: str) -> bool:
    """One ICMP echo, 2s deadline. Distinguishes HTTP-dead from IP-dead."""
    try:
        r = subprocess.run(
            ["ping", "-c", "1", "-t", "2", host],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        return r.returncode == 0
    except Exception:
        return False


def fetch_status(host: str, timeout: float = 5.0) -> dict:
    with urllib.request.urlopen(f"http://{host}/api/v1/status", timeout=timeout) as r:
        return json.load(r)


FIELDS = [
    "time", "state", "uptimeSec", "freeHeap", "minFreeHeap", "largestBlock",
    "freeDma", "minFreeDma", "rssi", "wifiMode", "espnowBound", "runActive",
    "pingOk", "wsFrames",
]


class WsHolder:
    """Holds one browser-like WebSocket connection to ws://host/ws and drains it.

    Without at least one connected WS client, WebInterface::update() skips
    ws_->textAll() entirely — the prime-suspect code path would never run.
    Counts received frames (~4 Hz expected) and reconnects on drop, like the
    SPA's exponential-backoff reconnect.
    """

    def __init__(self, host: str):
        self.host = host
        self.frames = 0          # total frames received (monotonic)
        self.connected = False
        threading.Thread(target=self._run, daemon=True).start()

    def _handshake(self) -> socket.socket:
        s = socket.create_connection((self.host, 80), timeout=5)
        key = base64.b64encode(os.urandom(16)).decode()
        s.sendall(
            (f"GET /ws HTTP/1.1\r\nHost: {self.host}\r\nUpgrade: websocket\r\n"
             f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n"
             f"Sec-WebSocket-Version: 13\r\n\r\n").encode())
        hdr = b""
        while b"\r\n\r\n" not in hdr:
            chunk = s.recv(1024)
            if not chunk:
                raise ConnectionError("closed during WS handshake")
            hdr += chunk
        if b" 101 " not in hdr.split(b"\r\n", 1)[0]:
            raise ConnectionError(f"WS upgrade refused: {hdr[:64]!r}")
        s.settimeout(15)
        return s

    def _run(self):
        backoff = 1.0
        while True:
            try:
                s = self._handshake()
                self.connected = True
                backoff = 1.0
                buf = b""
                while True:
                    chunk = s.recv(4096)   # drain + count frame headers, discard payload
                    if not chunk:
                        raise ConnectionError("WS closed by device")
                    buf += chunk
                    while len(buf) >= 2:
                        ln = buf[1] & 0x7F
                        off = 2 + (2 if ln == 126 else 8 if ln == 127 else 0)
                        if len(buf) < off:
                            break
                        if ln == 126:
                            ln = int.from_bytes(buf[2:4], "big")
                        elif ln == 127:
                            ln = int.from_bytes(buf[2:10], "big")
                        if len(buf) < off + ln:
                            break
                        self.frames += 1
                        buf = buf[off + ln:]
            except Exception as e:
                if self.connected:
                    print(f"[heapmon] ws dropped: {e}")
                self.connected = False
                time.sleep(backoff)
                backoff = min(backoff * 2, 10.0)


def main() -> int:
    ap = argparse.ArgumentParser(description="Mini-Reactor heap monitor")
    ap.add_argument("host", help="device IP (no mDNS on the reactor)")
    ap.add_argument("--period", type=float, default=10.0, help="seconds between samples")
    ap.add_argument("--out", default=None, help="CSV path (default heapmon-<stamp>.csv)")
    ap.add_argument("--ws", type=int, default=1,
                    help="number of held WS clients (0 = don't exercise the WS push path)")
    args = ap.parse_args()

    holders = [WsHolder(args.host) for _ in range(args.ws)]

    out = args.out or f"heapmon-{datetime.datetime.now():%Y%m%d-%H%M%S}.csv"
    f = open(out, "a", newline="")
    w = csv.DictWriter(f, fieldnames=FIELDS)
    if f.tell() == 0:
        w.writeheader()

    print(f"[heapmon] polling http://{args.host}/api/v1/status every {args.period:g}s -> {out}")
    prev_heap, prev_t, dead_since = None, None, None

    while True:
        now = datetime.datetime.now().isoformat(timespec="seconds")
        row = {k: "" for k in FIELDS}
        row["time"] = now
        try:
            s = fetch_status(args.host)
            sysd = s.get("system", {})
            row.update(
                state="ok",
                uptimeSec=s.get("uptimeSec", ""),
                freeHeap=sysd.get("freeHeap", ""),
                minFreeHeap=sysd.get("minFreeHeap", ""),
                largestBlock=sysd.get("largestBlock", ""),
                freeDma=sysd.get("freeDma", ""),
                minFreeDma=sysd.get("minFreeDma", ""),
                rssi=s.get("wifi", {}).get("rssi", ""),
                wifiMode=s.get("wifi", {}).get("mode", ""),
                espnowBound=s.get("espnow", {}).get("bound", ""),
                runActive=s.get("run", {}).get("active", ""),
                pingOk=1,
                wsFrames=sum(h.frames for h in holders),
            )
            heap = sysd.get("freeHeap")
            slope = ""
            if prev_heap is not None and heap is not None:
                dt_min = (time.time() - prev_t) / 60.0
                if dt_min > 0:
                    slope = f"  d={((heap - prev_heap) / dt_min):+.0f} B/min"
            prev_heap, prev_t = heap, time.time()
            if dead_since:
                print(f"[heapmon] {now} RECOVERED after {time.time() - dead_since:.0f}s offline")
                dead_since = None
            print(f"[heapmon] {now} up={row['uptimeSec']}s free={row['freeHeap']} "
                  f"largest={row['largestBlock']} dmaMin={row['minFreeDma']}{slope}")
        except Exception as e:
            alive = ping(args.host)
            row.update(state="http_dead" if alive else "ip_dead", pingOk=1 if alive else 0)
            if dead_since is None:
                dead_since = time.time()
                print(f"[heapmon] {now} *** DEVICE UNREACHABLE ({type(e).__name__}) "
                      f"ping={'OK — HTTP layer dead' if alive else 'DEAD — IP stack gone'} ***")
                print("[heapmon] leave this running (it will log recovery); "
                      "now check: 1) is 'MiniReactor-Setup' SSID broadcasting?  "
                      "2) UniFi client list: still associated?")
        w.writerow(row)
        f.flush()
        time.sleep(args.period)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\n[heapmon] stopped")
        sys.exit(0)
