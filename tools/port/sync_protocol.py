"""Online play's protocol: a layout change of the messages never ships under the same PORT_SYNC_PROTOCOL.

Two games play together online when they have the same disc and the same protocol (port/include/port/
statesync.h's PORT_SYNC_PROTOCOL), whatever their builds. So a build that changes what crosses must bump
it, or a game a commit older takes its messages as something else. This reads every message's size
(the WIRE_SIZE lines of port/src/common and port/include/port) and the message types (statesync.h's
SS_MSG_ and SS_R_ values), and compares their digest with the record, tests/port/sync_protocol.txt:

- the same protocol, another layout: fails (bump PORT_SYNC_PROTOCOL, then --update);
- a newer protocol than the record: fails until --update writes the record again;
- the same protocol and layout: passes.

A change of meaning with no change of size (a field used another way, a rule every game must apply
alike) is not seen here: that bump is the author's.

  python tools/port/sync_protocol.py            (exit 1 and why; testpass.py lint runs it too)
  python tools/port/sync_protocol.py --update   (writes the record for the protocol as it is)
"""
import glob
import hashlib
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HEADER = os.path.join(REPO, "port", "include", "port", "statesync.h")
RECORD = os.path.join(REPO, "tests", "port", "sync_protocol.txt")
WIRE = re.compile(r"^\s*WIRE_SIZE\(\s*(\w+)\s*,\s*(\w+)\s*\)\s*;", re.M)
TYPE = re.compile(r"^\s*(SS_(?:MSG|R)_\w+)\s*=\s*(\w+)", re.M)
PROTOCOL = re.compile(r"^\s*#\s*define\s+PORT_SYNC_PROTOCOL\s+(\d+)", re.M)


def sources():
    files = []
    for d, exts in (("port/src/common", ("*.c", "*.inc")), ("port/include/port", ("*.h",))):
        for e in exts:
            files += glob.glob(os.path.join(REPO, d, e))
    return sorted(files)


def current():
    """(protocol, layout digest, the layout's lines)"""
    text = open(HEADER, encoding="utf-8").read()
    m = PROTOCOL.search(text)
    if m is None:
        return None, None, []
    lines = []
    for path in sources():
        src = open(path, encoding="utf-8", errors="replace").read()
        name = os.path.basename(path)
        lines += ["%s %s %s" % (name, t, int(n, 0)) for t, n in WIRE.findall(src)]
    lines += ["type %s %s" % (k, int(v, 0)) for k, v in TYPE.findall(text)]
    lines.sort()
    digest = hashlib.sha256("\n".join(lines).encode()).hexdigest()[:16]
    return int(m.group(1)), digest, lines


def recorded():
    want = {}
    if os.path.exists(RECORD):
        for line in open(RECORD, encoding="utf-8"):
            parts = line.split()
            if len(parts) == 2 and not line.startswith("#"):
                want[parts[0]] = parts[1]
    return want


def problems():
    proto, digest, lines = current()
    if proto is None:
        return "port/include/port/statesync.h has no PORT_SYNC_PROTOCOL"
    if not lines:
        return "no WIRE_SIZE lines found (the check reads nothing)"
    want = recorded()
    if "protocol" not in want or "layout" not in want:
        return "no record (tests/port/sync_protocol.txt): run tools/port/sync_protocol.py --update"
    if proto < int(want["protocol"]):
        return "PORT_SYNC_PROTOCOL %d is below the record's %s" % (proto, want["protocol"])
    if proto == int(want["protocol"]) and digest != want["layout"]:
        return ("the online messages' layout changed (WIRE_SIZE or the message types) under the same "
                "PORT_SYNC_PROTOCOL %d: bump it (port/include/port/statesync.h), then run "
                "tools/port/sync_protocol.py --update" % proto)
    if proto != int(want["protocol"]) or digest != want["layout"]:
        return ("PORT_SYNC_PROTOCOL is %d, the record's %s: run tools/port/sync_protocol.py --update"
                % (proto, want["protocol"]))
    return ""


def main():
    if "--update" in sys.argv[1:]:
        proto, digest, lines = current()
        if proto is None or not lines:
            sys.exit("sync_protocol: nothing to record")
        with open(RECORD, "w", encoding="utf-8", newline="\n") as f:
            f.write("# tools/port/sync_protocol.py's record: the online protocol and its messages' layout digest\n")
            f.write("protocol %d\nlayout %s\n" % (proto, digest))
        print("sync_protocol: recorded protocol %d, layout %s (%d lines)" % (proto, digest, len(lines)))
        return 0
    bad = problems()
    print("sync_protocol: " + (bad or "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
