#!/usr/bin/env python3
"""A stand-in Archipelago server (Archipelago's network protocol, docs/network protocol.md in
ArchipelagoMW/Archipelago; Python's standard library only) for the built-in client's tests
(docs/ARCHIPELAGO.md): plain ws://, one room, one slot of "Paper Mario: The Thousand-Year Door"
and a second player ("Luigi", another game) whose finds the slot receives.

  ap_standin_server.py LOG [--port 38291] [--slot Mario] [--password PW]
                       [--items ID,ID,...]            items the slot has received (found by Luigi)
                       [--missing LOC,LOC,...]        the slot's locations not checked yet
                       [--checked LOC,...]            and checked
                       [--placements LOC:ITEM:PLAYER,...]  what LocationScouts finds (others: Luigi's item 1)
                       [--slot-data JSON] [--seconds N] [--spawn]
                       [--deathlink-after S]          a DeathLink from Luigi S seconds after the slot connects

It answers RoomInfo, GetDataPackage (a small package of the ids above), Connect (Connected, then
ReceivedItems), Sync, LocationScouts (LocationInfo), LocationChecks (PrintJSON), ConnectUpdate,
StatusUpdate, Bounce (Bounced to every client) and Say: "!hint NAME" answers with a Hint about the
first of the slot's places still missing (PrintJSON "Hint"), "!getitem ID" sends the slot that item
(ReceivedItems), "!release" says the slot's items were released. Every command a client sends is written
to LOG, one JSON object per line (a test reads its LocationChecks there). --spawn starts the
server on its own, returns once it listens (a scenario's user_gen: LOG is the file it makes),
and the server ends after --seconds (default 600).
"""
import argparse
import asyncio
import base64
import hashlib
import json
import os
import socket
import struct
import subprocess
import sys
import time

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
GAME = "Paper Mario: The Thousand-Year Door"
OTHER = "Another Game"


def ids(text):
    return [int(x) for x in text.split(",") if x.strip()]


async def read_message(reader):
    """The next text message (a client's frames are masked); None once closed."""
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
    if n < 126:
        head = bytes([0x81, n])
    elif n < 65536:
        head = bytes([0x81, 126]) + struct.pack(">H", n)
    else:
        head = bytes([0x81, 127]) + struct.pack(">Q", n)
    return head + data


class Room:
    def __init__(self, a):
        self.a = a
        self.items = ids(a.items)
        self.missing = ids(a.missing)
        self.checked = ids(a.checked)
        self.placements = {}
        for p in a.placements.split(","):
            if p.strip():
                loc, item, player = (int(x) for x in p.split(":"))
                self.placements[loc] = (item, player)
        self.slot_data = json.loads(a.slot_data)
        self.clients = set()
        self.log = open(a.log, "a", encoding="utf-8")

    def write_log(self, cmd):
        self.log.write(json.dumps(cmd) + "\n")
        self.log.flush()

    def network_item(self, item, location, player):
        return {"item": item, "location": location, "player": player, "flags": 1, "class": "NetworkItem"}

    def data_package(self):
        ttyd_items = {f"Item {i}": i for i in self.items + [v[0] for v in self.placements.values() if v[1] == 1]}
        ttyd_locs = {f"Location {l}": l for l in self.missing + self.checked}
        return {"games": {GAME: {"item_name_to_id": ttyd_items, "location_name_to_id": ttyd_locs, "checksum": "standin"},
                          OTHER: {"item_name_to_id": {"Luigi's Thing": 1}, "location_name_to_id": {"Luigi's Place": 1},
                                  "checksum": "standin"}}}

    def received(self, index=0):
        return {"cmd": "ReceivedItems", "index": index,
                "items": [self.network_item(i, 1, 2) for i in self.items[index:]]}

    def hint(self):
        """A hint about the first of the slot's places still missing (its placement, else Luigi's item 1)."""
        loc = self.missing[0] if self.missing else (self.checked[0] if self.checked else 0)
        item, player = self.placements.get(loc, (1, 2))
        return {"cmd": "PrintJSON", "type": "Hint", "receiving": player, "found": loc in self.checked,
                "item": self.network_item(item, loc, 1),
                "data": [{"type": "player_id", "text": str(player)}, {"text": "'s "},
                         {"type": "item_id", "text": str(item), "player": player}, {"text": " is at "},
                         {"type": "location_id", "text": str(loc), "player": 1}, {"text": " in "},
                         {"type": "player_id", "text": "1"}, {"text": "'s World."}]}

    def say(self, text):
        if text.startswith("!hint"):
            return [self.hint(), {"cmd": "RoomUpdate", "hint_points": 0}]
        if text.startswith("!getitem"):
            arg = text[len("!getitem"):].strip()
            if not arg.isdigit():
                return [{"cmd": "PrintJSON", "type": "CommandResult", "data": [{"text": f"No item named {arg}."}]}]
            self.items.append(int(arg))
            index = len(self.items) - 1
            return [{"cmd": "ReceivedItems", "index": index, "items": [self.network_item(int(arg), -1, 1)]},
                    {"cmd": "PrintJSON", "type": "ItemCheat", "receiving": 1,
                     "item": self.network_item(int(arg), -1, 1),
                     "data": [{"text": f"Cheat console: sending item {arg} to {self.a.slot}."}]}]
        if text.startswith("!release"):
            return [{"cmd": "PrintJSON", "type": "Release", "data": [{"text": f"{self.a.slot} has released all remaining items."}]}]
        return [{"cmd": "PrintJSON", "type": "Chat", "slot": 1, "message": text,
                 "data": [{"text": f"{self.a.slot}: {text}"}]}]

    def answer(self, cmd):
        name = cmd.get("cmd")
        if name == "Say":
            return self.say(cmd.get("text", ""))
        if name == "GetDataPackage":
            return [{"cmd": "DataPackage", "data": self.data_package()}]
        if name == "Connect":
            if cmd.get("name") != self.a.slot:
                return [{"cmd": "ConnectionRefused", "errors": ["InvalidSlot"]}]
            if self.a.password and cmd.get("password") != self.a.password:
                return [{"cmd": "ConnectionRefused", "errors": ["InvalidPassword"]}]
            if cmd.get("game") != GAME:
                return [{"cmd": "ConnectionRefused", "errors": ["InvalidGame"]}]
            players = [{"team": 0, "slot": 1, "alias": self.a.slot, "name": self.a.slot, "class": "NetworkPlayer"},
                       {"team": 0, "slot": 2, "alias": "Luigi", "name": "Luigi", "class": "NetworkPlayer"}]
            slot_info = {"1": {"name": self.a.slot, "game": GAME, "type": 1, "group_members": [], "class": "NetworkSlot"},
                         "2": {"name": "Luigi", "game": OTHER, "type": 1, "group_members": [], "class": "NetworkSlot"}}
            connected = {"cmd": "Connected", "team": 0, "slot": 1, "players": players, "missing_locations": self.missing,
                         "checked_locations": self.checked, "slot_info": slot_info, "hint_points": 0}
            if cmd.get("slot_data"):
                connected["slot_data"] = self.slot_data
            connected["hint_points"] = 10
            return [connected, self.received(0),
                    {"cmd": "PrintJSON", "type": "Join", "data": [{"text": f"{self.a.slot} has joined."}]}]
        if name == "Sync":
            return [self.received(0)]
        if name == "LocationScouts":
            out = []
            for loc in cmd.get("locations", []):
                item, player = self.placements.get(loc, (1, 2))
                out.append(self.network_item(item, loc, player))
            return [{"cmd": "LocationInfo", "locations": out}]
        if name == "LocationChecks":
            replies = []
            for loc in cmd.get("locations", []):
                if loc in self.missing:
                    self.missing.remove(loc)
                    self.checked.append(loc)
                item, player = self.placements.get(loc, (1, 2))
                replies.append({"cmd": "PrintJSON", "type": "ItemSend", "receiving": player,
                                "item": self.network_item(item, loc, 1),
                                "data": [{"type": "player_id", "text": "1"}, {"text": " found "},
                                         {"type": "item_id", "text": str(item), "player": player},
                                         {"text": " ("}, {"type": "location_id", "text": str(loc), "player": 1},
                                         {"text": ")"}]})
            return replies
        if name == "StatusUpdate" and cmd.get("status") == 30:
            return [{"cmd": "PrintJSON", "type": "Goal", "data": [{"text": f"{self.a.slot} reached the goal."}]}]
        return []


async def serve(a):
    room = Room(a)

    async def client(reader, writer):
        head = b""
        while b"\r\n\r\n" not in head:
            chunk = await reader.read(1024)
            if not chunk:
                return
            head += chunk
        key = next((l.split(b":", 1)[1].strip() for l in head.split(b"\r\n") if l.lower().startswith(b"sec-websocket-key")), b"")
        accept = base64.b64encode(hashlib.sha1(key + GUID.encode()).digest())
        writer.write(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                     b"Sec-WebSocket-Accept: " + accept + b"\r\n\r\n")
        room.clients.add(writer)

        def send(cmds):
            if cmds:
                writer.write(frame(json.dumps(cmds)))

        send([{"cmd": "RoomInfo", "version": {"major": 0, "minor": 6, "build": 4, "class": "Version"},
               "generator_version": {"major": 0, "minor": 6, "build": 4, "class": "Version"},
               "tags": ["AP"], "password": bool(a.password),
               "permissions": {"release": 2, "collect": 2, "remaining": 2}, "hint_cost": 10,
               "location_check_points": 1, "games": [GAME, OTHER],
               "datapackage_checksums": {GAME: "standin", OTHER: "standin"}, "seed_name": "standin",
               "time": time.time()}])
        try:
            while True:
                text = await read_message(reader)
                if text is None:
                    break
                for cmd in json.loads(text):
                    room.write_log(cmd)
                    if cmd.get("cmd") == "Connect" and a.deathlink_after > 0:
                        async def death_later():
                            await asyncio.sleep(a.deathlink_after)
                            death = {"cmd": "Bounced", "tags": ["DeathLink"],
                                     "data": {"time": time.time(), "source": "Luigi", "cause": "Luigi fell."}}
                            if writer in room.clients:
                                writer.write(frame(json.dumps([death])))
                                room.write_log({"cmd": "standin:DeathLink", "source": "Luigi"})
                        asyncio.ensure_future(death_later())
                    if cmd.get("cmd") == "Bounce":
                        bounced = dict(cmd, cmd="Bounced")
                        for w in list(room.clients):
                            w.write(frame(json.dumps([bounced])))
                        continue
                    send(room.answer(cmd))
                await writer.drain()
        except (asyncio.IncompleteReadError, ConnectionError):
            pass
        finally:
            room.clients.discard(writer)
            writer.close()

    server = await asyncio.start_server(client, "127.0.0.1", a.port)
    async with server:
        await asyncio.sleep(a.seconds)


def listening(port):
    with socket.socket() as s:
        s.settimeout(0.5)
        return s.connect_ex(("127.0.0.1", port)) == 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log")
    ap.add_argument("--port", type=int, default=38291)
    ap.add_argument("--slot", default="Mario")
    ap.add_argument("--password", default="")
    ap.add_argument("--items", default="")
    ap.add_argument("--missing", default="")
    ap.add_argument("--checked", default="")
    ap.add_argument("--placements", default="")
    ap.add_argument("--slot-data", default='{"goal": 1, "remote_items": 0, "death_link": 0}')
    ap.add_argument("--seconds", type=float, default=600)
    ap.add_argument("--deathlink-after", type=float, default=0)
    ap.add_argument("--spawn", action="store_true")
    a = ap.parse_args()
    if a.spawn:
        args = [sys.executable, os.path.abspath(__file__)] + [x for x in sys.argv[1:] if x != "--spawn"]
        flags = {}
        if os.name == "nt":
            flags["creationflags"] = subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP
        else:
            flags["start_new_session"] = True
        subprocess.Popen(args, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, **flags)
        for _ in range(100):
            if listening(a.port):
                return 0
            time.sleep(0.1)
        print(f"the stand-in server didn't start on port {a.port}", file=sys.stderr)
        return 1
    open(a.log, "a").close()
    asyncio.run(serve(a))
    return 0


if __name__ == "__main__":
    sys.exit(main())
