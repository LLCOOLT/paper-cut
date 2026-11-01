#!/usr/bin/env python3
"""Put the port's game-file changes back onto the decomp's new game code.

The port's game code was decomp 85ac972be plus PC changes (HEAD of main). The
integration branch holds the decomp's new code as is. For every file the port
changed, each hunk of `85ac972be -> main` is placed onto the new file only when
that is unambiguous; everything else is listed for a manual merge:

- insertion (the port only added lines): the code around it, ignoring
  comments and whitespace, is found exactly once in the new file;
- replacement: the replaced lines, ignoring comments and whitespace, are found
  exactly once and unchanged; comment lines of the new file inside that range
  are kept (before the port's lines);
- otherwise: "merge" (found, but the decomp changed those lines too: names,
  constants) or "unplaced" (not found), with the closest location if any.

Writes the files in place and port/integration/ledger.tsv (one row per hunk).
usage: reapply_port.py [--base 85ac972be] [--port main] [--dry-run] [FILE...]
"""
import argparse
import csv
import difflib
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
CONST = re.compile(r"\b(?:0x[0-9A-Fa-f]+|\d+)[uUlLfF]*\b|\b[A-Z][A-Z0-9_]*[A-Z0-9]\b|\bk[A-Z]\w*\b")
COND = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b")


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True).stdout


def show(rev, path):
    p = subprocess.run(["git", "show", f"{rev}:{path}"], cwd=ROOT, capture_output=True)
    return p.stdout.decode("utf-8", "surrogateescape").split("\n") if p.returncode == 0 else None


def exact(line):
    s = re.sub(r"//.*$", "", line)
    s = re.sub(r"/\*.*?\*/", "", s)
    return re.sub(r"\s+", "", s)


def shape(line):
    s = exact(line)
    s = re.sub(r"\bmp?([A-Z])", lambda m: m.group(1).lower(), s)
    s = CONST.sub("K", s)
    return re.sub(r"\(?K([|+]K)+\)?", "K", s)


def code_lines(lines, key):
    """(normalized code lines, their line numbers); block comments skipped."""
    out, idx, in_comment = [], [], False
    for i, line in enumerate(lines):
        s = line
        if in_comment:
            if "*/" not in s:
                continue
            s = s.split("*/", 1)[1]
            in_comment = False
        if "/*" in s and "*/" not in s.split("/*", 1)[1]:
            s = s.split("/*", 1)[0]
            in_comment = True
        k = key(s)
        if k:
            out.append(k)
            idx.append(i)
    return out, idx


def find(seq, sub):
    n = len(sub)
    if not n:
        return []
    first = sub[0]
    return [i for i in range(len(seq) - n + 1) if seq[i] == first and seq[i:i + n] == sub]


def is_comment_only(line):
    s = line.strip()
    return s.startswith(("//", "/*", "*", "*/")) and not exact(line)


def locate(base, i1, i2, new_exact, new_shape):
    """Return (status, start, end) in normalized-code coordinates of the new file."""
    removed = [x for x in (exact(l) for l in base[i1:i2]) if x]
    if removed:
        hits = find(new_exact, removed)
        if len(hits) == 1:
            return "placed", hits[0], hits[0] + len(removed)
        shaped = [shape(l) for l in base[i1:i2] if exact(l)]
        hits = find(new_shape, shaped)
        if len(hits) == 1:
            return "merge", hits[0], hits[0] + len(shaped)
        before = [x for x in (shape(l) for l in base[max(0, i1 - 5):i1]) if x][-1:]
        after = [x for x in (shape(l) for l in base[i2:i2 + 5]) if x][:1]
        hits = find(new_shape, before + shaped + after)
        if len(hits) == 1:
            s = hits[0] + len(before)
            return "merge", s, s + len(shaped)
        return "unplaced", None, None
    before = [x for x in (exact(l) for l in base[max(0, i1 - 8):i1]) if x][-2:]
    after = [x for x in (exact(l) for l in base[i2:i2 + 8]) if x][:2]
    for cand, off in ((before + after, len(before)), (before, len(before)), (after, 0)):
        if cand:
            hits = find(new_exact, cand)
            if len(hits) == 1:
                return "placed", hits[0] + off, hits[0] + off
    # the port's lines are new, so a place found by shape (names, constants) is safe too
    sb = [shape(l) for l in base[max(0, i1 - 8):i1] if exact(l)][-2:]
    sa = [shape(l) for l in base[i2:i2 + 8] if exact(l)][:2]
    for cand, off in ((sb + sa, len(sb)), (sb, len(sb)), (sa, 0)):
        if cand:
            hits = find(new_shape, cand)
            if len(hits) == 1:
                return "placed-shape", hits[0] + off, hits[0] + off
    return "unplaced", None, None


def balanced(lines):
    depth = 0
    for l in lines:
        m = COND.match(l)
        if not m:
            continue
        k = m.group(1)
        if k.startswith("if"):
            depth += 1
        elif k == "endif":
            depth -= 1
            if depth < 0:
                return False
    return depth == 0


def process(path, base_rev, port_rev, rows, dry):
    base, port = show(base_rev, path), show(port_rev, path)
    full = os.path.join(ROOT, path)
    if port is None:
        rows.append([path, "-", "-", "file", "obsolete", "removed by the port"])
        return
    if base is None:
        rows.append([path, "-", "-", "file", "placed", "added by the port (kept as on main)"])
        return
    if not os.path.exists(full):
        rows.append([path, "-", "-", "file", "unplaced", "file removed upstream"])
        return
    with open(full, encoding="utf-8", errors="surrogateescape", newline="") as f:
        text = f.read()
    crlf = "\r\n" in text
    new = text.replace("\r\n", "\n").split("\n")
    ne, ne_idx = code_lines(new, exact)
    ns, ns_idx = code_lines(new, shape)
    edits = []  # (first line, last line exclusive, replacement lines)
    file_rows = []
    for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(None, base, port, autojunk=False).get_opcodes():
        if tag == "equal":
            continue
        status, s, e = locate(base, i1, i2, ne, ns)
        kind = "insert" if i1 == i2 or not any(exact(l) for l in base[i1:i2]) else "replace"
        where = ""
        if status == "placed-shape":
            # an insertion anchored on code the decomp renamed or named constants in
            status, idx = "placed", ns_idx
        else:
            idx = ne_idx
        if status == "placed":
            if kind == "insert":
                if s < len(idx):
                    at = idx[s]
                    # before a doc comment attached to the next code line
                    while at > 0 and is_comment_only(new[at - 1]):
                        at -= 1
                    if s > 0 and at <= idx[s - 1]:
                        at = idx[s - 1] + 1
                else:
                    at = len(new)
                edits.append((at, at, port[j1:j2]))
                where = f"new:{at + 1}"
            else:
                first, last = ne_idx[s], ne_idx[e - 1] + 1
                kept = [l for l in new[first:last] if is_comment_only(l) or not l.strip()]
                kept = [l for l in kept if l.strip()]
                edits.append((first, last, kept + port[j1:j2]))
                where = f"new:{first + 1}-{last}"
        elif status == "merge":
            idx = ns_idx
            at = idx[s] if s < len(idx) else len(new)
            where = f"near new:{at + 1}"
        file_rows.append([path, f"{i1 + 1}-{i2}", f"{j1 + 1}-{j2}", kind, status, where])
    # apply from the bottom; an edit overlapping one below it becomes a merge
    edits.sort(key=lambda x: (x[0], x[1]))
    out = list(new)
    last_start = None
    applied = 0
    for first, last, repl in reversed(edits):
        if last_start is not None and last > last_start:
            for r in file_rows:
                if r[5] in (f"new:{first + 1}-{last}", f"new:{first + 1}") and r[4] == "placed":
                    r[4], r[5] = "merge", "overlaps another hunk; " + r[5]
            continue
        out[first:last] = repl
        last_start = first
        applied += 1
    if not balanced(out) and balanced(new):
        note = "#if/#endif unbalanced after placement: a conditional's other half did not place"
        for r in file_rows:
            if r[4] == "placed":
                r[5] = (r[5] + "; " if r[5] else "") + "check: " + note
    rows.extend(file_rows)
    if not dry:
        data = "\n".join(out)
        if crlf:
            data = data.replace("\n", "\r\n")
        with open(full, "w", encoding="utf-8", errors="surrogateescape", newline="") as f:
            f.write(data)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", default="85ac972be")
    ap.add_argument("--port", default="main")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("files", nargs="*")
    a = ap.parse_args()
    files = a.files or git("diff", "--name-only", a.base, a.port, "--", "src", "rels", "include", "libs",
                           "config").decode().split()
    rows = []
    for i, f in enumerate(files):
        process(f, a.base, a.port, rows, a.dry_run)
    os.makedirs(os.path.join(ROOT, "port", "integration"), exist_ok=True)
    with open(os.path.join(ROOT, "port", "integration", "ledger.tsv"), "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, delimiter="\t", lineterminator="\n")
        w.writerow(["id", "file", "base_lines", "port_lines", "kind", "status", "where", "resolution"])
        for n, r in enumerate(rows, 1):
            w.writerow([n] + r + [""])
    from collections import Counter
    print(len(rows), "hunks:", dict(Counter(r[4] for r in rows)))
    print(len(files), "files;", len({r[0] for r in rows if r[4] != "placed"}), "with hunks left to merge")


if __name__ == "__main__":
    main()
