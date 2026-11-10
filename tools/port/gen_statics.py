#!/usr/bin/env python3
"""List the game's file-scope statics: port/statics_<version>.txt.

GameCube mods reach the game's file statics by address (a work pointer `wp`,
a REL's script, a function they call) as freely as its globals. A PC mod links by name, and a
static has none outside its file, so the build compiles each game source
that has statics through a wrapper that appends one exported pointer per
static (CMakeLists.txt, ttyd_static_exports):

    void* const port_static__src_manager_cardmgr__wp = (void*)&wp;

which tools/port/mod_headers.py names for a ported mod's declaration of it
(and ttyd_mod/vars.h's ttydGameVar follows). This lists them, per source, as
clang sees the PC builds (compile_commands.json of a configured Aurora build
and a configured headless build of the same version):

    gen_statics.py --build build-pc-us-aurora --build build-pc-us-headless [--check]

Both backends compile through the one list, so it holds only the statics both
see: a static under the Aurora backend's #if (fontmgr's portFontStringIcon)
has no wrapper on headless to take its address, and one only headless defines
none on Aurora. A hookable function's body (name__body, mods' hooks: Aurora
only) is kept as Aurora sees it; CMakeLists.txt skips those without mods. (A build needs only its configure and the pre-compile
generated sources: the sjis/ copies and generated/ headers.)

The list is committed (statics change only with the game's sources), so a
configure doesn't need clang's AST of 1,000 files.
"""
import argparse
import concurrent.futures
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LOC = re.compile(r"<([A-Za-z]:[^:<>]+|/[^:<>]+):\d+:\d+")
VAR = re.compile(r"^\|-(?:VarDecl|FunctionDecl) 0x[0-9a-f]+ (?:prev 0x[0-9a-f]+ )?<(\S+?)[,>].*? (?:used |referenced )*(\w+) '[^']*'(?::'[^']*')? .*?\bstatic\b")
FUNC = re.compile(r"^[|`]-FunctionDecl 0x[0-9a-f]+ .*? (?:used |referenced )*(\w+) '")
BODY = re.compile(r"^[| ] [|`]-CompoundStmt ")


def game_source(path, build):
    """The game source (src/... or rels/...) a compile command builds, or None:
    a Shift-JIS copy or a statics wrapper in the build folder stands for its
    original."""
    p = os.path.normpath(path)
    for gen in ("sjis", "statics"):  # (the statics' own wrapper, once built)
        d = os.path.normpath(os.path.join(build, gen))
        if os.path.normcase(p).startswith(os.path.normcase(d) + os.sep):
            p = os.path.join(ROOT, os.path.relpath(p, d))
    rel = os.path.relpath(p, ROOT).replace(os.sep, "/")
    # spelled as git has it: a case-insensitive disk can keep a file's old
    # case (unit_..._mkii.c for git's _mkII.c), and the name of each static's
    # export is made from this path
    rel = tracked_spelling().get(rel.lower(), rel)
    return rel if rel.startswith(("src/", "rels/")) and rel.endswith(".c") else None


_tracked = None


def tracked_spelling():
    """{lowercased path: the path as git tracks it} for the game's sources."""
    global _tracked
    if _tracked is None:
        r = subprocess.run(["git", "ls-files", "src", "rels"], cwd=ROOT, capture_output=True, text=True)
        _tracked = {p.lower(): p for p in r.stdout.splitlines()} if r.returncode == 0 else {}
    return _tracked


def statics(job):
    entry, src = job
    # the command line as the build runs it (its quoting is the shell's)
    cmd = entry["command"]
    cmd = re.sub(r" -o \S+| -c(?= )| -MD(?= )| -MF \S+| -MT \S+", "", cmd)
    cmd += " -fsyntax-only -w -Xclang -ast-dump -fno-color-diagnostics"
    r = subprocess.run(cmd, cwd=entry["directory"], capture_output=True, text=True, errors="replace",
                       shell=os.name != "nt")
    # (a Shift-JIS copy's #line names the original)
    main = os.path.normcase(os.path.normpath(os.path.join(ROOT, src)))
    # a static function counts only where this version defines it: the decomp
    # declares some for every version and defines them in one version's block.
    # Any top-level FunctionDecl of that name with a CompoundStmt body defines
    # it (a later definition without 'static', a mods dispatcher in an .inc).
    cur, names, funcs, bodies, pending = "", [], set(), set(), None
    for ln in r.stdout.splitlines():
        if ln.startswith(("|-", "`-")):
            fd = FUNC.match(ln)
            pending = fd.group(1) if fd else None
        elif pending and BODY.match(ln):
            bodies.add(pending)
            pending = None
        m = VAR.match(ln)
        if m:
            first = LOC.match("<" + m.group(1))
            if first:
                cur = first.group(1)
            if os.path.normcase(os.path.normpath(os.path.join(entry["directory"], cur))) == main:
                names.append(m.group(2))
                if ln[2:].startswith("FunctionDecl"):
                    funcs.add(m.group(2))
        for loc in LOC.finditer(ln):
            cur = loc.group(1)
    return sorted(set(n for n in names if n not in funcs or n in bodies))


def configured(build):
    """(version, backend, compile commands) of a configured build."""
    db = json.load(open(os.path.join(build, "compile_commands.json"), encoding="utf-8"))
    # the build's version (PAL defines VERSION_G8ME01 too: its own macro decides)
    flags = next(set(re.findall(r"-DVERSION_(G8M[EJP]01)\b", e.get("command", ""))) for e in db
                 if "-DVERSION_" in e.get("command", ""))
    version = "G8MP01" if "G8MP01" in flags else flags.pop()
    # (from the configure: a headless build has a test target with the Aurora define, vec_flush_test)
    with open(os.path.join(build, "CMakeCache.txt"), encoding="utf-8", errors="replace") as f:
        backend = next((ln.split("=", 1)[1].strip() for ln in f if ln.startswith("TTYD_BACKEND:")), "aurora")
    return version, backend, db


def scan(build, db):
    """{game source: its statics} as one configured build sees them."""
    todo = {}
    for e in db:
        src = game_source(os.path.join(e["directory"], e["file"]), build)
        if src and src not in todo:
            todo[src] = e
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 8) as ex:
        return dict(zip(todo, ex.map(statics, [(e, src) for src, e in todo.items()])))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", action="append",
                    help="a configured build, one per backend (default build-pc-us-aurora, build-pc-us-headless)")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    builds = [os.path.join(ROOT, b) for b in a.build or ["build-pc-us-aurora", "build-pc-us-headless"]]
    confs = [configured(b) for b in builds]
    versions = sorted({v for v, _, _ in confs})
    backends = sorted({b for _, b, _ in confs})
    if len(versions) != 1:
        sys.exit(f"gen_statics: the builds are of different versions ({', '.join(versions)})")
    if backends != ["aurora", "headless"]:
        sys.exit(f"gen_statics: needs an Aurora and a headless build of the version, not only {backends[0]}:"
                 " both compile through the one list")
    version = versions[0]
    # a source's statics: the ones every build that compiles it sees; and a hookable
    # function's body (name__body) as Aurora sees it: only mods' hooks make one, and
    # CMakeLists.txt skips those on a build without them
    seen = {}
    for build, (_, backend, db) in zip(builds, confs):
        seen[backend] = scan(build, db)
    found = {}
    for src in sorted(set(seen["aurora"]) | set(seen["headless"])):
        au, hl = seen["aurora"].get(src), seen["headless"].get(src)
        both = set(au) & set(hl) if au is not None and hl is not None else set(au if au is not None else hl)
        found[src] = sorted(both | {n for n in au or () if n.endswith("__body")})
    lines = [f"# GENERATED by tools/port/gen_statics.py ({version}). Do not edit.",
             "# Each game source's file-scope statics, exported for mods (CMakeLists.txt)."]
    lines += [f"{src} {' '.join(names)}" for src, names in sorted(found.items()) if names]
    text = "\n".join(lines) + "\n"
    out = os.path.join(ROOT, "port", f"statics_{version}.txt")
    old = open(out, encoding="utf-8").read() if os.path.exists(out) else None
    count = sum(len(n) for n in found.values())
    if a.check:
        print(f"{'ok   ' if old == text else 'STALE'} {os.path.relpath(out, ROOT)} ({count} statics)")
        return 0 if old == text else 1
    if old != text:
        with open(out, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    print(f"{os.path.relpath(out, ROOT)}: {count} statics in {len([n for n in found.values() if n])} files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
