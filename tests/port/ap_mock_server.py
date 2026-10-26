#!/usr/bin/env python3
"""A stand-in Archipelago server for the port's tests (Python's standard
library only): plain ws://, one slot.

  ap_mock_server.py --port 38281 --slot Mario --items 77772112,77772112 --log server.jsonl

On a connection it sends RoomInfo; to Connect, Connected (slot 1, the slot
data given) and ReceivedItems (the items given, as found by player 2, "Luigi");
to GetDataPackage, an empty package. Every command a client sends is written
to --log, one JSON object per line (a test reads its LocationChecks there).
It serves until killed, or --seconds.
"""
import argparse
import asyncio
import base64
import hashlib
import json
import struct

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


async def read_frame(reader):
    """The next text message (a client's frames are masked)."""
    message = b""
    while True:
        h = await reader.readexactly(2)
        op, n = h[0] & 0x0F, h[1] & 0x7F
        if n == 126:
            n = struct.unpack(">H", await reader.readexactly(2))[0]
        elif n == 127:
            n = struct.unpack(">Q", await reader.readexactly(8))[0]
        mask = await reader.readexactly(4) if h[1] & 0x80 else b"\0\0\0\0"
        data = bytes(b ^ mask[i & 3] for i, b in enumerate(await reader.readexactly(n)))
        if op == 0x8:
            return None
        if op in (0x9, 0xA):
            continue
        message += data
        if h[0] & 0x80:
            return message.decode("utf-8")


def frame(text):
    data = text.encode("utf-8")
    n = len(data)
    head = bytes([0x81]) + (bytes([n]) if n < 126 else bytes([126]) + struct.pack(">H", n) if n < 65536
                            else bytes([127]) + struct.pack(">Q", n))
    return head + data


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=38281)
    ap.add_argument("--slot", default="Mario")
    ap.add_argument("--game", default="Paper Mario: The Thousand-Year Door")
    ap.add_argument("--items", default="", help="item ids the slot has received, comma-separated")
    ap.add_argument("--slot-data", default='{"goal": 1, "remote_items": 0, "death_link": 0}')
    ap.add_argument("--log", required=True)
    ap.add_argument("--seconds", type=float, default=0)
    a = ap.parse_args()
    items = [int(x) for x in a.items.split(",") if x]
    log = open(a.log, "w", encoding="utf-8")

    async def client(reader, writer):
        request = b""
        while b"\r\n\r\n" not in request:
            request += await reader.read(1024)
        key = [ln.split(b":", 1)[1].strip() for ln in request.split(b"\r\n") if ln.lower().startswith(b"sec-websocket-key")][0]
        accept = base64.b64encode(hashlib.sha1(key + GUID.encode()).digest()).decode()
        writer.write(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                      f"Sec-WebSocket-Accept: {accept}\r\n\r\n").encode())

        def send(*cmds):
            writer.write(frame(json.dumps(list(cmds))))

        send({"cmd": "RoomInfo", "version": {"major": 0, "minor": 6, "build": 2, "class": "Version"},
              "tags": [], "password": False, "games": [a.game], "seed_name": "test", "time": 0})
        await writer.drain()
        while True:
            try:
                text = await read_frame(reader)
            except (asyncio.IncompleteReadError, ConnectionError):
                break  # the game closed the connection (or exited)
            if text is None:
                break
            for cmd in json.loads(text):
                log.write(json.dumps(cmd) + "\n")
                log.flush()
                if cmd.get("cmd") == "GetDataPackage":
                    send({"cmd": "DataPackage", "data": {"games": {}}})
                elif cmd.get("cmd") == "Connect":
                    if cmd.get("name") != a.slot or cmd.get("game") != a.game:
                        send({"cmd": "ConnectionRefused", "errors": ["InvalidSlot"]})
                        continue
                    send({"cmd": "Connected", "team": 0, "slot": 1,
                          "players": [{"team": 0, "slot": 1, "alias": a.slot, "name": a.slot},
                                      {"team": 0, "slot": 2, "alias": "Luigi", "name": "Luigi"}],
                          "missing_locations": [], "checked_locations": [],
                          "slot_data": json.loads(a.slot_data),
                          "slot_info": {"1": {"name": a.slot, "game": a.game, "type": 1},
                                        "2": {"name": "Luigi", "game": "Other", "type": 1}}},
                         {"cmd": "ReceivedItems", "index": 0,
                          "items": [{"item": i, "location": 1000 + n, "player": 2, "flags": 1}
                                    for n, i in enumerate(items)]})
            await writer.drain()
        writer.close()

    async def serve():
        server = await asyncio.start_server(client, "127.0.0.1", a.port)
        print(f"ap_mock_server: ws://127.0.0.1:{a.port}", flush=True)
        async with server:
            if a.seconds:
                await asyncio.sleep(a.seconds)
            else:
                await server.serve_forever()

    asyncio.run(serve())


if __name__ == "__main__":
    main()
