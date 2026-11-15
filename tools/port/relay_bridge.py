#!/usr/bin/env python3
"""The online tests' way onto the real relay: headless games (which have no WebSocket or sealing) speak the
local stand-in's frames to this bridge ($TTYD_SYNC_TEST_RELAY=127.0.0.1:<PORT>, sync_test_relay.py's
protocol), and it carries each connection to the real relay as the game's own WebSocket would
(port/relay/src/index.js: wss://<relay>/room/<HALF>?role=...). What the relay sees is a test: every room's half
is rewritten to TEST + 4 random characters (never a player's room), the query says &test=netmatrix, and the
connections say User-Agent papercut-netmatrix. Nothing is sealed (the headless build's stand-in mode), which the
relay never looks at.

It counts what the relay is billed for on the Workers Free plan (docs/ONLINE_RELAY.md: 100,000 requests a day;
each connection a request, messages into the relay 20 to 1) and stops at a hard budget: every connection cut
and every new one refused ("budget"), so a run can never use more than it was given.

  relay_bridge.py PORT [--url wss://papercut-relay.llcoolt.workers.dev] [--budget 2000] [--log FILE]
                  [--counts FILE] [--seconds N] [--net SPEC] [--net-role ROLE=SPEC ...] [--seed N]

--counts FILE: the counts as JSON at the end and every 5 seconds (testpass.py reads it into the result).
--net, --net-role, --seed: as sync_test_relay.py's (tools/port/netshape.py), on the frames to the games.
"""
import argparse
import asyncio
import base64
import collections
import json
import math
import os
import secrets
import ssl
import struct
import sys
import time
from urllib.parse import parse_qs, urlencode, urlparse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import netshape  # noqa: E402

DEFAULT_URL = "wss://papercut-relay.llcoolt.workers.dev"
UA = "papercut-netmatrix (Paper Cut online tests)"
PER_CONNECTION = 4  # requests a connection costs at most: the Worker, the room, the room list's report in and out
PER_MESSAGES = 20  # messages into the relay per request
LOG = sys.stdout
A = None
HALVES = {}  # the game's room half -> the test half on the relay
COUNTS = collections.Counter()
CUT = []  # the budget reached: [why]
LIVE = set()


def log(*a):
    print("[bridge] %.2f" % time.time(), *a, file=LOG, flush=True)


def requests_used():
    return COUNTS["connections"] * PER_CONNECTION + COUNTS["lists"] * 2 + \
        math.ceil(COUNTS["messages in"] / PER_MESSAGES)


def write_counts():
    if A.counts:
        d = dict(COUNTS, requests=requests_used(), budget=A.budget, cut=bool(CUT), url=A.url)
        with open(A.counts, "w", encoding="utf-8") as f:
            json.dump(d, f, indent=1)


def over_budget():
    if not CUT and requests_used() >= A.budget:
        CUT.append("budget")
        log("BUDGET reached: %d of %d requests; every connection cut" % (requests_used(), A.budget))
        for w in list(LIVE):
            try:
                w.transport.abort()
            except Exception:
                pass
    return bool(CUT)


async def frame_in(r):
    head = await r.readexactly(5)
    n, kind = struct.unpack("<IB", head)
    return kind, (await r.readexactly(n - 1) if n > 1 else b"")


def frame(kind, data):
    return struct.pack("<IB", len(data) + 1, kind) + data


def ws_frame(opcode, data):
    """a client's frame (masked, as RFC 6455 asks)"""
    head = bytes([0x80 | opcode])
    n = len(data)
    if n < 126:
        head += bytes([0x80 | n])
    elif n < 65536:
        head += bytes([0x80 | 126]) + struct.pack(">H", n)
    else:
        head += bytes([0x80 | 127]) + struct.pack(">Q", n)
    mask = secrets.token_bytes(4)
    body = bytes(b ^ mask[i & 3] for i, b in enumerate(data))
    return head + mask + body


async def ws_read(r):
    """one whole message: (opcode, data); continuation frames joined"""
    parts, op = [], None
    while True:
        b0, b1 = await r.readexactly(2)
        n = b1 & 0x7F
        if n == 126:
            n = struct.unpack(">H", await r.readexactly(2))[0]
        elif n == 127:
            n = struct.unpack(">Q", await r.readexactly(8))[0]
        mask = await r.readexactly(4) if b1 & 0x80 else None
        data = await r.readexactly(n)
        if mask:
            data = bytes(b ^ mask[i & 3] for i, b in enumerate(data))
        opcode = b0 & 0x0F
        if opcode >= 8:  # (control frames come between a message's pieces)
            return opcode, data
        if opcode != 0:
            op = opcode
        parts.append(data)
        if b0 & 0x80:
            return op, b"".join(parts)


async def https_open(path):
    """(reader, writer, status, headers) after a GET with a WebSocket upgrade (or without: path starting '!')"""
    u = urlparse(A.url)
    host = u.hostname
    port = u.port or (443 if u.scheme == "wss" else 80)
    ctx = ssl.create_default_context() if u.scheme == "wss" else None
    r, w = await asyncio.wait_for(asyncio.open_connection(host, port, ssl=ctx, server_hostname=host if ctx else None),
                                  15)
    upgrade = not path.startswith("!")
    path = path.lstrip("!")
    lines = [f"GET {path} HTTP/1.1", f"Host: {host}", f"User-Agent: {UA}"]
    if upgrade:
        lines += ["Upgrade: websocket", "Connection: Upgrade", "Sec-WebSocket-Version: 13",
                  "Sec-WebSocket-Key: " + base64.b64encode(secrets.token_bytes(16)).decode()]
    else:
        lines += ["Connection: close"]
    w.write(("\r\n".join(lines) + "\r\n\r\n").encode())
    await w.drain()
    status_line = (await asyncio.wait_for(r.readline(), 15)).decode(errors="replace")
    status = int(status_line.split()[1]) if len(status_line.split()) > 1 else 0
    headers = {}
    while True:
        ln = (await asyncio.wait_for(r.readline(), 15)).decode(errors="replace").strip()
        if not ln:
            break
        k, _, v = ln.partition(":")
        headers[k.strip().lower()] = v.strip()
    return r, w, status, headers


async def body_of(r, headers):
    try:
        if "content-length" in headers:
            return await asyncio.wait_for(r.readexactly(int(headers["content-length"])), 10)
        if headers.get("transfer-encoding", "").lower() == "chunked":
            out = b""
            while True:
                n = int((await r.readline()).strip() or b"0", 16)
                if n == 0:
                    return out
                out += await r.readexactly(n)
                await r.readline()
        return await asyncio.wait_for(r.read(4096), 10)
    except Exception:
        return b""


class Down:
    """frames to the game, through the --net shaping when there is one"""

    def __init__(self, w, role):
        self.w = w
        spec = dict(x.split("=", 1) for x in A.net_role).get(role, A.net)
        self.sh = netshape.Shaper(spec, A.seed, role) if spec else None
        self.q = collections.deque()
        self.ev = asyncio.Event()
        self.task = asyncio.ensure_future(self.pump()) if self.sh else None

    def put(self, data):
        if self.sh is None:
            self.w.write(data)
            return
        self.q.append((self.sh.delay(time.monotonic() * 1000, len(data)), data))
        self.ev.set()

    async def pump(self):
        while True:
            while not self.q:
                self.ev.clear()
                await self.ev.wait()
            t, data = self.q[0]
            d = t - time.monotonic() * 1000
            if d > 0:
                await asyncio.sleep(d / 1000)
                continue
            self.q.popleft()
            if self.w.is_closing():
                return
            self.w.write(data)
            try:
                await self.w.drain()
            except ConnectionError:
                return


async def client(gr, gw):
    LIVE.add(gw)
    try:
        await client_in(gr, gw)
    except Exception as e:
        log("connection ended:", type(e).__name__, e)
    finally:
        LIVE.discard(gw)
        gw.close()
        write_counts()


async def client_in(gr, gw):
    kind, data = await frame_in(gr)
    line = data.decode(errors="replace")
    if kind != 1 or not line.startswith("OPEN "):
        return
    u = urlparse(line[5:])
    if over_budget():
        gw.write(frame(2, b"budget: this test run's relay budget is used up"))
        await gw.drain()
        return
    if u.path == "/rooms":
        COUNTS["lists"] += 1
        r, w, status, headers = await https_open("!" + line[5:] + "&test=netmatrix")
        body = await body_of(r, headers)
        w.close()
        gw.write(frame(3, body))
        await gw.drain()
        try:
            await frame_in(gr)
        except Exception:
            pass
        return
    half = u.path.split("/")[-1]
    test_half = HALVES.setdefault(half, "TEST" + "".join(secrets.choice("23456789ABCDEFGHJKMNPQRSTUVWXYZ")
                                                         for _ in range(4)))
    q = parse_qs(u.query)
    q.pop("list", None)  # (a test room is never listed for players to see)
    q["test"] = ["netmatrix"]
    role = q.get("role", ["?"])[0]
    path = f"/room/{test_half}?" + urlencode({k: v[0] for k, v in q.items()})
    COUNTS["connections"] += 1
    r, w, status, headers = await https_open(path)
    if status != 101:
        why = (await body_of(r, headers)).decode(errors="replace").strip() or f"HTTP {status}"
        log(half, "->", test_half, role, "refused:", status, why)
        w.close()
        gw.write(frame(2, why.encode()))
        await gw.drain()
        return
    log(half, "->", test_half, role, "in")
    down = Down(gw, role)
    down.put(frame(1, b"open"))
    LIVE.add(w)

    async def up():
        # the game's frames into the relay: counted (the budget), as WebSocket messages
        while True:
            k, d = await frame_in(gr)
            if over_budget():
                return
            COUNTS["messages in"] += 1
            COUNTS[f"{role} in"] += 1
            w.write(ws_frame(2 if k == 0 else 1, d))
            await w.drain()

    async def down_loop():
        while True:
            op, d = await ws_read(r)
            if op == 8:  # close
                log(half, role, "closed by the relay", d[2:].decode(errors="replace") if len(d) > 2 else "")
                return
            if op == 9:
                w.write(ws_frame(10, d))
                continue
            if op in (1, 2):
                COUNTS["messages out"] += 1
                down.put(frame(1 if op == 1 else 0, d))

    tasks = [asyncio.ensure_future(up()), asyncio.ensure_future(down_loop())]
    try:
        await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
    finally:
        for t in tasks:
            t.cancel()
        if down.task:
            down.task.cancel()
        LIVE.discard(w)
        try:
            w.write(ws_frame(8, struct.pack(">H", 1000)))
            w.close()
        except Exception:
            pass
        log(half, role, "gone; requests so far", requests_used())


async def ticker():
    while True:
        await asyncio.sleep(5)
        write_counts()
        log("counts", " ".join(f"{k}={v}" for k, v in sorted(COUNTS.items())), "requests", requests_used(),
            "of", A.budget)


async def main():
    global A, LOG
    ap = argparse.ArgumentParser()
    ap.add_argument("port", type=int)
    ap.add_argument("--url", default=os.environ.get("PAPERCUT_TEST_RELAY_URL", DEFAULT_URL))
    ap.add_argument("--budget", type=int, default=2000)
    ap.add_argument("--log")
    ap.add_argument("--counts")
    ap.add_argument("--seconds", type=int, default=600)
    ap.add_argument("--net", default="")
    ap.add_argument("--net-role", nargs="*", default=[])
    ap.add_argument("--seed", type=int, default=1)
    A = ap.parse_args()
    netshape.parse(A.net)
    for x in A.net_role:
        netshape.parse(x.split("=", 1)[1])
    if A.log:
        LOG = open(A.log, "w", encoding="utf-8")
    server = await asyncio.start_server(client, "127.0.0.1", A.port)
    log("listening", A.port, "->", A.url, "budget", A.budget, "requests")
    asyncio.ensure_future(ticker())
    await asyncio.sleep(A.seconds)
    log("time is up; requests used", requests_used())
    write_counts()
    server.close()


if __name__ == "__main__":
    asyncio.run(main())
