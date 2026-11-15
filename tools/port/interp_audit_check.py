#!/usr/bin/env python3
"""The interpolation audit's net check over game logs: every draw that left the game changed once a frame drawn
between the game's own (Graphics > Frame rate above 60, "display", stereo 3D's first eye) was over, against an
allowlist of the caches the game's next frame computes again.

  interp_audit_check.py LOG|DIR ... [--allow FILE] [--all]

A game run with TTYD_INTERP_AUDIT=1 (Windows, the Aurora build) and a higher frame rate prints
"[interp-audit] net writer F (file:line) <- caller <- caller" and "[interp-audit] net global NAME+0xOFFSET"
lines (port/src/aurora/interp_audit.c); a DIR is searched for log.txt and *.log files (a testpass run's folder:
local-docs/testpass/<run>/). Each site is keyed by the first game function in its chain (memmove, MTX and GX helpers
skipped) and its file, without the line, or by the global's name; a key the allowlist (default
tests/port/interp_audit_allow.txt: one key a line, "*" for any run of characters, # comments) doesn't match fails,
named with the first log it was in. "net check incomplete" fails too, and so does no net line at all in any log
(the audit was off: nothing was checked). --all lists every key seen, allowed or not (to review the allowlist).

--update-scenarios writes the allowlist into the interp_audit scenarios (tests/port/testpass/interp_audit_*.json) as
two log_must_not patterns (any net writer or global it doesn't allow), so a testpass run of them fails on its own;
run it after each change of the allowlist.

A failure is a draw callback that changes the game's state: a timer or counter stepped in the draw (it runs twice as
fast at 120 Hz), a one-shot taken, a block allocated or freed, a sound started. The fix: the draw returns early in
portInterpInBetween(), or keeps what it steps with portInterpKeep (docs/FRAME_INTERPOLATION.md).
"""
import argparse
import fnmatch
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
ALLOW = os.path.join(REPO, "tests", "port", "interp_audit_allow.txt")
# helpers a write is put down to the caller of (the draw that used them)
HELPER = re.compile(r"^(memmove|memcpy|memset|_?_?mem\w*|PSMTX\w*|PSVEC\w*|MTX\w*|C_MTX\w*|GX\w*|qqsort|str\w*|"
                    r"\w*printf\w*|0[0-9a-f]+)$")
NET = re.compile(r"^\[interp-audit\] net (writer|global) (.*)$")
HELPER_RX = r"(?:mem\w*|PSMTX\w*|PSVEC\w*|MTX\w*|C_MTX\w*|GX\w*|qqsort|str\w*|\w*printf\w*|0[0-9a-f]+)"


def key_of(kind, rest):
    if kind == "global":
        return "global " + re.sub(r"\+0x[0-9a-f]+$", "", rest.strip())
    for part in rest.split(" <- "):
        part = part.strip()
        m = re.match(r"^(\S+?)(?:__body)?(?:\+0x[0-9a-f]+)?(?: \(([^:)]+)(?::\d+)?\))?$", part)
        if not m:
            continue
        fn, src = m.group(1), m.group(2)
        if HELPER.match(fn):
            continue
        return f"writer {fn}" + (f" ({src})" if src else "")
    return "writer " + rest.split(" <- ")[0].strip()


def logs_in(paths):
    for p in paths:
        if os.path.isdir(p):
            for root, _, files in os.walk(p):
                for f in files:
                    if f == "log.txt" or f.endswith(".log"):
                        yield os.path.join(root, f)
        else:
            yield p


def load_allow(path):
    pats = []
    for line in open(path, encoding="utf-8"):
        line = line.split("#", 1)[0].strip()
        if line:
            pats.append(line)
    return pats


def glob_rx(g):
    rx = fnmatch.translate(g)  # (?s:...)\Z: the glob as the allowlist's matcher reads it
    return rx[:-2] if rx.endswith((r"\Z", r"\z")) else rx


def scenario_patterns(allow):
    """the allowlist as two log_must_not patterns: a net writer whose first game function, and a net global whose
    name, the allowlist doesn't match"""
    writers, globs = [], []
    for p in allow:
        kind, _, rest = p.partition(" ")
        if kind == "writer":
            writers.append(glob_rx(rest.split(" (")[0].split("(")[0].strip()))
        elif kind == "global":
            globs.append(glob_rx(rest.strip()))
    frame = r"(?:__body)?(?:\+0x[0-9a-f]+)?(?: \([^)]*\))?"
    w = (r"^\[interp-audit\] net writer (?:" + HELPER_RX + frame + " <- )*+(?!(?:" + "|".join(writers) + ")" +
         r"(?:__body)?(?:\+0x[0-9a-f]+)?(?: \(| <- |$))")
    g = r"^\[interp-audit\] net global (?!(?:" + "|".join(globs) + r")$)"
    return [w, g]


def update_scenarios(allow):
    import glob
    import json
    pats = scenario_patterns(allow)
    for p in pats:
        re.compile(p)
    n = 0
    for path in sorted(glob.glob(os.path.join(REPO, "tests", "port", "testpass", "interp_audit_*.json"))):
        s = json.load(open(path, encoding="utf-8"))
        keep = [x for x in s.get("log_must_not", []) if not (x.startswith("^\\[interp-audit\\] net writer (?:") or
                                                             x.startswith("^\\[interp-audit\\] net global (?!"))]
        s["log_must_not"] = keep + pats
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            json.dump(s, f, indent=1, ensure_ascii=False)
            f.write("\n")
        n += 1
    print(f"interp_audit_check: the allowlist written into {n} scenarios")
    return 0 if n else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="*")
    ap.add_argument("--allow", default=ALLOW)
    ap.add_argument("--all", action="store_true", help="list every key seen")
    ap.add_argument("--update-scenarios", action="store_true", help="the allowlist into the interp_audit scenarios")
    a = ap.parse_args()
    allow = load_allow(a.allow)
    if a.update_scenarios:
        return update_scenarios(allow)
    if not a.paths:
        ap.error("logs or folders to check")
    seen = {}
    incomplete = []
    logs = 0
    for log in logs_in(a.paths):
        logs += 1
        try:
            text = open(log, encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        for line in text.splitlines():
            m = NET.match(line)
            if m:
                seen.setdefault(key_of(m.group(1), m.group(2)), (log, line))
            elif line.startswith("[interp-audit] net check incomplete"):
                incomplete.append(log)
    bad = {k: v for k, v in seen.items() if not any(fnmatch.fnmatchcase(k, p) for p in allow)}
    if a.all:
        for k in sorted(seen):
            print(("  " if k not in bad else "! ") + k)
    for k in sorted(bad):
        print(f"FAIL {k}\n     {bad[k][1]}\n     in {bad[k][0]}")
    for log in incomplete:
        print(f"FAIL net check incomplete in {log}")
    if not seen:
        print(f"FAIL no net line in {logs} logs: was TTYD_INTERP_AUDIT=1 set, at a frame rate above 60?")
    print(f"interp_audit_check: {len(seen)} sites in {logs} logs, {len(bad)} not allowed")
    return 1 if bad or incomplete or not seen else 0


if __name__ == "__main__":
    sys.exit(main())
