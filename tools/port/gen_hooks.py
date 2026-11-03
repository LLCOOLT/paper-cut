#!/usr/bin/env python3
"""Generate the mods' hook dispatchers (docs/MODDING.md, "Code mods").

Input: port/mods/hooks.spec:
  name           a game function, hand-picked: it must be hookable (a function
                 defined in one game source, src/, not variadic) or this fails
  @files GLOB    every function a mod can name defined in these sources: not
                 static or inline, declared in a game header a C++ mod can
                 include (ttyd_mod/game_all.h), not variadic, with the same
                 signature in every version; the rest are left out quietly
  -name          a function the @files sets leave out (hot, or unsafe to hook)
  @skip GLOB     sources the @files sets leave out
'#' starts a comment.

Slots: port/mods/hook_slots.txt gives each function its slot number, once and
for good: new functions are added at its end, and a function that leaves the
list keeps its number, unused (a mod built against an older list still
reaches the same functions, and a hook on a gone one is refused). The file is
written by this script (--apply, or a plain run); keep it in the repository.

Each hookable definition is named PORT_HOOKABLE(name) (include/port_hook.h:
name__body with mods); the file includes port_hook.h and, at its end, its
generated dispatchers:

  port/include/port/hooks/<file>.inc   per source file: for each function,
                                       name(...) calling the slot's pre hooks,
                                       then the mod's replacement or the
                                       body, then the post hooks; and
                                       portHookInit_<file>() recording the bodies
  port/include/port/hook_list.h        PORT_HOOK(index, name) and
                                       PORT_HOOK_FILE(file) lists (the host)
  port/include/ttyd_mod/hooks.h        for mods: TTYD_HOOK_<name> slot numbers,
                                       and each slot's function, pre and post
                                       types

Signatures come from clang's AST of the defining file (one dump per file and
version), compiled with the flags the Aurora builds use (read from
build-pc-<jp|us|eu>-aurora/build.ninja, so configure those first). --apply
makes the edits in the game sources (PORT_HOOKABLE at the definition, the two
includes); without it a missing edit is an error. --check: fail if a
generated file is stale. --report: what the @files sets left out, and why.

Measuring the cost (docs/MODDING.md, "Hook slots"): a build with
-DPORT_HOOK_PROFILE=1 counts each slot's calls; $TTYD_HOOK_PROFILE=<file>
writes the counts per frame at exit (port/mods/mod_api.cpp). Functions called
often each frame go in the '-name' list.
"""
import argparse
import concurrent.futures
import fnmatch
import glob
import os
import re
import shlex
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import portversion

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SPEC = os.path.join(ROOT, "port", "mods", "hooks.spec")
SLOTS = os.path.join(ROOT, "port", "mods", "hook_slots.txt")
GAME_ALL = os.path.join(ROOT, "port", "include", "ttyd_mod", "game_all.h")
INC_DIR = os.path.join(ROOT, "port", "include", "port", "hooks")
LIST_H = os.path.join(ROOT, "port", "include", "port", "hook_list.h")
MOD_H = os.path.join(ROOT, "port", "include", "ttyd_mod", "hooks.h")
SHORT = portversion.SHORT


class Error(Exception):
    pass


class Spec:
    def __init__(self):
        self.names = []     # hand-picked, in order
        self.files = []     # @files globs
        self.skips = []     # @skip globs
        self.excluded = set()  # -name


def read_spec():
    spec = Spec()
    for lineno, raw in enumerate(open(SPEC, encoding="utf-8"), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        if line.startswith("@files "):
            spec.files.append(line[7:].strip())
        elif line.startswith("@skip "):
            spec.skips.append(line[6:].strip())
        elif re.match(r"^-\w+$", line):
            spec.excluded.add(line[1:])
        elif re.match(r"^\w+$", line):
            spec.names.append(line)
        else:
            raise Error(f"hooks.spec:{lineno}: not a function name or directive: {line}")
    return spec


def read_slots():
    """name -> slot number (port/mods/hook_slots.txt)."""
    slots = {}
    if os.path.exists(SLOTS):
        for raw in open(SLOTS, encoding="utf-8"):
            line = raw.split("#", 1)[0].strip()
            if line:
                index, name = line.split()
                slots[name] = int(index)
    return slots


def slots_text(slots):
    lines = ["# Each hookable function's slot number (tools/port/gen_hooks.py): fixed once given,",
             "# so mods built against an older list keep working. New ones go at the end; a",
             "# function that leaves port/mods/hooks.spec keeps its line (the slot stays unused).",
             "# Written by the generator."]
    lines += [f"{i} {n}" for n, i in sorted(slots.items(), key=lambda x: x[1])]
    return "\n".join(lines) + "\n"


def def_pattern(name):
    """A definition line of name (as written, or already PORT_HOOKABLE)."""
    n = re.escape(name)
    return re.compile(rf"^(?:USER_FUNC\(\s*(?:PORT_HOOKABLE\({n}\)|{n})\s*\)\s*\{{?\s*|[A-Za-z_][\w \t\*]*?[\s\*](?:PORT_HOOKABLE\({n}\)|{n})\s*\([^;]*)$")


_LINES = {}


def source_lines(src):
    if src not in _LINES:
        _LINES[src] = open(os.path.join(ROOT, src), encoding="utf-8", errors="replace").read().split("\n")
    return _LINES[src]


def find_in(name, src):
    """The definition lines of name in src (several: one per version's #if)."""
    pat = def_pattern(name)
    lines = source_lines(src)
    found = []
    for i, ln in enumerate(lines):
        if pat.match(ln.rstrip("\r")):
            # the body follows: '{' at the end of this line or alone on a later one
            rest = "\n".join(lines[i:i + 8])
            if re.search(r"\)\s*\{", rest) and ";" not in rest.split("{", 1)[0]:
                found.append((src, i))
    return found


def find_definition(name, sources):
    found = []
    for src in sources:
        if src.startswith("src/") and src.endswith(".c"):
            found += find_in(name, src)
    # several in one file: the versions' own definitions (#if VERSION ...),
    # each version compiles one; all of them are edited. A static function (the
    # decomp makes file-local functions static) is hookable too; its prototype
    # and dispatcher are then static as well (STATIC).
    if not found or len({src for src, _ in found}) != 1:
        raise Error(f"{name}: {len(found) or 'no'} definition(s) in the game's sources")
    return found


STATIC = set()  # hookable functions whose definition is static
UNDECLARED = set()  # hookable functions without a prototype before their definition


def is_static(src, lineno):
    return source_lines(src)[lineno].lstrip().startswith("static")


def stem(src):
    return re.sub(r"\W", "_", src[:-2])


def apply_edits(names, src):
    """PORT_HOOKABLE at each of names' definitions in src, and the includes."""
    path = os.path.join(ROOT, src)
    raw = open(path, "rb").read()
    crlf = b"\r\n" in raw
    text = raw.decode("utf-8").replace("\r\n", "\n")
    lines = text.split("\n")
    _LINES[src] = lines
    for name in names:
        for _, lineno in find_in(name, src):
            ln = lines[lineno]
            if "PORT_HOOKABLE(" + name + ")" not in ln:
                if ln.startswith("USER_FUNC("):
                    ln = re.sub(rf"USER_FUNC\(\s*{re.escape(name)}\s*\)", f"USER_FUNC(PORT_HOOKABLE({name}))", ln, count=1)
                else:
                    ln = re.sub(rf"([\s\*]){re.escape(name)}(\s*\()", rf"\1PORT_HOOKABLE({name})\2", ln, count=1)
                lines[lineno] = ln
    text = "\n".join(lines)
    if '#include "port_hook.h"' not in text:
        # before the first #include outside any #if (the GameCube build needs it too)
        depth, at, lines = 0, None, text.split("\n")
        for i, l in enumerate(lines):
            s = l.strip()
            if s.startswith("#if"):
                depth += 1
            elif s.startswith("#endif"):
                depth -= 1
            elif s.startswith("#include") and depth == 0:
                at = i
                break
        lines.insert(at, '#include "port_hook.h"')
        text = "\n".join(lines)
    tail = f'#ifdef PORT_MODS\n#include "port/hooks/{stem(src)}.inc"\n#endif\n'
    if f"port/hooks/{stem(src)}.inc" not in text:
        text = text + ("" if text.endswith("\n") else "\n") + "\n" + tail
    if f"port/hooks/{stem(src)}.h" not in text:
        # after the last #include outside any #if: the prototypes, for calls
        # made before a definition the file never declared (now a dispatcher)
        depth, at, lines = 0, None, text.split("\n")
        for i, l in enumerate(lines):
            s = l.strip()
            if s.startswith("#if"):
                depth += 1
            elif s.startswith("#endif"):
                depth -= 1
                if depth == 0 and at is not None and at == i - 1:
                    at = i
            elif s.startswith("#include") and depth == 0 and "port/hooks/" not in s:
                at = i
        lines[at + 1:at + 1] = ["#ifdef PORT_MODS", f'#include "port/hooks/{stem(src)}.h"', "#endif"]
        text = "\n".join(lines)
    _LINES[src] = text.split("\n")
    data = text.replace("\n", "\r\n") if crlf else text
    if data.encode("utf-8") != raw:
        open(path, "wb").write(data.encode("utf-8"))
        print(f"edited {src}")


def unapply_edits(names, files):
    """PORT_HOOKABLE taken off the functions no longer listed, and a file's
    hook includes off when none of its functions is."""
    for path in glob.glob(os.path.join(ROOT, "src", "**", "*.c"), recursive=True):
        src = os.path.relpath(path, ROOT).replace(os.sep, "/")
        raw = open(path, "rb").read()
        if b"PORT_HOOKABLE(" not in raw:
            continue
        crlf = b"\r\n" in raw
        text = raw.decode("utf-8").replace("\r\n", "\n")
        text = re.sub(r"PORT_HOOKABLE\((\w+)\)", lambda m: m.group(0) if m.group(1) in names else m.group(1), text)
        if src not in files:
            text = text.replace(f'\n#ifdef PORT_MODS\n#include "port/hooks/{stem(src)}.h"\n#endif', "")
            text = text.replace(f'\n\n#ifdef PORT_MODS\n#include "port/hooks/{stem(src)}.inc"\n#endif\n', "\n")
        data = text.replace("\n", "\r\n") if crlf else text
        if data.encode("utf-8") != raw:
            open(path, "wb").write(data.encode("utf-8"))
            _LINES.pop(src, None)
            print(f"edited {src} (functions left the list)")


def check_edits(name, src, lineno):
    lines = source_lines(src)
    text = "\n".join(lines)
    if f"PORT_HOOKABLE({name})" not in lines[lineno] or '#include "port_hook.h"' not in text or \
            f"port/hooks/{stem(src)}.inc" not in text or f"port/hooks/{stem(src)}.h" not in text:
        raise Error(f"{name}: {src} lacks its PORT_HOOKABLE edits (run tools/port/gen_hooks.py --apply)")


_NINJA = {}


def compile_flags(version, src):
    """The Aurora build's flags for src, from its build.ninja."""
    if version not in _NINJA:
        ninja = os.path.join(ROOT, f"build-pc-{SHORT[version]}-aurora", "build.ninja")
        if not os.path.exists(ninja):
            raise Error(f"{os.path.relpath(ninja, ROOT)} missing: configure the {version} Aurora build first")
        text = open(ninja, encoding="utf-8", errors="replace").read()
        # (files with Japanese text are compiled from a Shift-JIS copy, sjis/<src>;
        # files with statics through a wrapper, statics/<src>)
        table = {}
        for m in re.finditer(r"^build CMakeFiles/ttyd_dol\.dir/(?:sjis/|statics/)?((?:src|rels)/\S+?\.c)\.(?:obj|o):.*\n((?:  .*\n)+)",
                             text, re.M):
            table[m.group(1)] = m.group(2)
        _NINJA[version] = table
    body = _NINJA[version].get(src)
    if body is None:
        raise Error(f"{src} is not compiled by the {version} Aurora build")
    flags = []
    for ln in body.splitlines():
        key, _, val = ln.strip().partition(" = ")
        if key in ("DEFINES", "FLAGS", "INCLUDES"):
            flags += shlex.split(val, posix=True)
    # keep what affects parsing; drop debug info and codegen options
    return [f for f in flags if not f.startswith(("-g", "-O")) and f not in ("-Xclang", "-gcodeview")
            and not f.startswith("--dependent-lib")]


# the location may be "<scratch space>:N:M" (a name made by ## in a macro)
FUNC = re.compile(r"FunctionDecl 0x[0-9a-f]+ .*? (\w+) '([^']*)'(?:\s*:\s*'[^']*')?(.*)$")
PARM = re.compile(r"ParmVarDecl 0x[0-9a-f]+ <.*?> (?:col|line):[\d:]+ (?:used |referenced )?(?:(\w+) )?'([^']*)'")


def file_definitions(version, src):
    """Every function defined in src's translation unit: name -> (type, params,
    flags, declared), flags the words after the type (static, inline, extern),
    declared: a prototype comes before the definition. Parsed
    without PORT_MODS: the functions as the game has them, no dispatchers.
    $GEN_HOOKS_CACHE=<dir>: kept there by the source's time (while editing
    hooks.spec; the headers' changes aren't seen)."""
    cache = os.environ.get("GEN_HOOKS_CACHE")
    if cache:
        import pickle
        key = os.path.join(cache, f"{version}_{stem(src)}_{int(os.path.getmtime(os.path.join(ROOT, src)))}.pickle")
        if os.path.exists(key):
            with open(key, "rb") as f:
                return pickle.load(f)
        defs = file_definitions_uncached(version, src)
        os.makedirs(cache, exist_ok=True)
        with open(key, "wb") as f:
            pickle.dump(defs, f)
        return defs
    return file_definitions_uncached(version, src)


def file_definitions_uncached(version, src):
    clang = os.environ.get("CLANG", "clang")
    flags = [f for f in compile_flags(version, src) if f != "-DPORT_MODS=1"]
    cmd = [clang, "-fsyntax-only", "-w", *flags, "-Xclang", "-ast-dump", os.path.join(ROOT, src)]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True)
    if r.returncode != 0:
        sys.stderr.write(r.stderr.decode("utf-8", "replace")[-3000:])
        raise Error(f"clang failed on {src} ({version})")
    defs = {}
    declared = set()  # the prototypes so far
    cur = None  # [name, type, flags, params, defined]

    def end(cur):
        if cur is not None:
            if cur[4]:
                defs[cur[0]] = (cur[1], cur[3], cur[2], cur[0] in declared)
            declared.add(cur[0])

    for ln in r.stdout.decode("utf-8", "replace").split("\n"):
        head = ln[:2]
        if head in ("|-", "`-"):
            end(cur)
            cur = None
            if ln[2:14] == "FunctionDecl":
                m = FUNC.search(ln)
                if m:
                    cur = [m.group(1), m.group(2), m.group(3).split(), [], False]
        elif cur is not None and ln[:2] in ("| ", "  ") and ln[2:4] in ("|-", "`-"):
            if ln[4:15] == "ParmVarDecl":
                mp = PARM.search(ln)
                if mp:
                    cur[3].append(mp.group(2))
            elif ln[4:16] == "CompoundStmt":
                cur[4] = True
    end(cur)
    return defs


def split_type(name, t):
    """'int (s32, void *)' -> 'int'; Error for what can't be hooked."""
    if "..." in t:
        raise Error(f"{name}: variadic functions can't be hooked")
    depth = 0
    ret = None
    for i, ch in enumerate(t):
        if ch == "(" and depth == 0 and i > 0:
            ret = t[:i].strip()
            break
        depth += ch == "("
        depth -= ch == ")"
    if ret is None or "(" in ret:
        raise Error(f"{name}: functions returning function pointers can't be hooked")
    return ret


def declare(ctype, name):
    """'T *' + name, also for function pointers ('void (*)(int)')."""
    if "(*)" in ctype:
        return ctype.replace("(*)", f"(*{name})", 1)
    return f"{ctype} {name}"


def dispatcher(index, name, ret, params):
    void = ret == "void"
    ps = [f"p{i}" for i in range(len(params))]
    decl = ", ".join(declare(t, p) for t, p in zip(params, ps)) or "void"
    args = ", ".join(ps)
    tps = ", ".join(f"__typeof__({p})" for p in ps)
    tpp = ", ".join(f"__typeof__({p})*" for p in ps)
    addr = ", ".join(f"&{p}" for p in ps)
    res_t = "" if void else f"__typeof__(r)*"
    pre_t = ", ".join(x for x in (res_t, tpp) if x) or "void"
    post_t = ", ".join(x for x in (res_t, tps) if x) or "void"
    pre_a = ", ".join(x for x in ("" if void else "&r", addr) if x)
    post_a = ", ".join(x for x in ("" if void else "&r", args) if x)
    o = []
    o.append(f"{'static ' if name in STATIC else ''}{ret} {name}({decl}) {{")
    o.append(f"    PortHookSlot* s = &portHookSlots[{index}];")
    if not void:
        o.append(f"    {declare(ret, 'r')};")
    o.append("    int i, skip = 0;")
    o.append("    PORT_HOOK_COUNT_CALL(s);")
    o.append("    if (!s->active) {")
    o.append(f"        {'' if void else 'return '}{name}__body({args});")
    if void:
        o.append("        return;")
    o.append("    }")
    if not void:
        o.append("    __builtin_memset(&r, 0, sizeof(r));")
    o.append("    for (i = 0; i < s->preCount; i++) {")
    o.append(f"        skip |= ((int (*)({pre_t}))s->pre[i])({pre_a});")
    o.append("    }")
    o.append("    if (!skip) {")
    call = f"(s->replace != NULL ? (({ret} (*)({tps or 'void'}))s->replace)({args}) : {name}__body({args}))"
    if void:
        o.append(f"        if (s->replace != NULL) {{ ((void (*)({tps or 'void'}))s->replace)({args}); }} else {{ {name}__body({args}); }}")
    else:
        o.append(f"        r = {call};")
    o.append("    }")
    o.append("    for (i = 0; i < s->postCount; i++) {")
    o.append(f"        ((void (*)({post_t}))s->post[i])({post_a});")
    o.append("    }")
    if not void:
        o.append("    return r;")
    o.append("}")
    return "\n".join(o)


def mod_headers():
    """The game headers a C++ mod can include (ttyd_mod/game_all.h), as included."""
    return re.findall(r'^#include "(?!ttyd_mod/)([^"]+)"', open(GAME_ALL, encoding="utf-8").read(), re.M)


def header_prototypes(headers):
    """name -> the first of headers declaring it."""
    out = {}
    for rel in headers:
        path = os.path.join(ROOT, "include", rel)
        if not os.path.exists(path):
            continue
        text = open(path, encoding="utf-8", errors="replace").read()
        for m in re.finditer(r"^[^#/\n][^;{}()\n]*?\b(\w+)\s*\([^;{}]*\)\s*;", text, re.M):
            out.setdefault(m.group(1), rel)
    return out


def matches(src, globs):
    return any(fnmatch.fnmatch(src, g) for g in globs)


def run_parallel(jobs):
    """{key: (fn, args)} -> {key: result}, clang dumps in parallel."""
    out = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
        futs = {ex.submit(fn, *args): key for key, (fn, args) in jobs.items()}
        for f in concurrent.futures.as_completed(futs):
            out[futs[f]] = f.result()
    return out


def collect(spec, apply, report):
    """The hookable functions: [(name, src, ret, params)], hand-picked first,
    each the same in every version."""
    versions = portversion.VERSIONS
    sources = {v: set(portversion.sources(v)) for v in versions}
    common = set.intersection(*sources.values())
    # the hand-picked: their files
    picked = {}
    for name in spec.names:
        found = find_definition(name, sorted(common))
        picked[name] = found[0][0]
    # the @files sets: the sources to read
    auto_files = sorted(s for s in common if s.startswith("src/") and s.endswith(".c")
                        and matches(s, spec.files) and not matches(s, spec.skips))
    files = sorted(set(picked.values()) | set(auto_files))
    dumps = run_parallel({(v, f): (file_definitions, (v, f)) for v in versions for f in files})
    protos = header_prototypes(mod_headers())

    entries, left_out = [], []
    seen = set()

    def same_everywhere(name, src):
        sigs = []
        for v in versions:
            d = dumps[(v, src)].get(name)
            if d is None:
                return None, f"not defined in {src} in {v}"
            sigs.append(d)
        if any(s[:2] != sigs[0][:2] for s in sigs):
            return None, "its signature differs between versions"
        return sigs[0], None

    for name in spec.names:
        src = picked[name]
        sig, why = same_everywhere(name, src)
        if sig is None:
            raise Error(f"{name}: {why}")
        if "static" in sig[2]:
            STATIC.add(name)
        entries.append((name, src, split_type(name, sig[0]), sig[1]))
        seen.add(name)
    for src in auto_files:
        defs = dumps[(versions[0], src)]
        # file-local (static, or a header's inline) functions are not in a mod's reach
        names = [n for n, d in defs.items() if "static" not in d[2] and "inline" not in d[2] and n not in seen]
        lines = {n: find_in(n, src) for n in names}
        for name in sorted(names, key=lambda n: (not lines[n], lines[n][:1], n)):
            ftype, params, words, _ = defs[name]
            why = None
            if name in spec.excluded:
                why = "excluded in hooks.spec"
            elif name not in protos:
                why = "no prototype in a header a mod can include"
            elif "..." in ftype:
                why = "variadic"
            elif any("unnamed" in t or "anonymous" in t for t in [ftype] + params):
                why = "an unnamed type in its signature"
            elif not lines[name]:
                why = "its definition's first line isn't 'type name(' (edit it by hand to hook it)"
            elif any(source_lines(src)[n].lstrip().startswith("static") for _, n in lines[name]):
                why = "static in one version"
            elif re.search(rf"^\s*#\s*define\s+{name}\b", "\n".join(source_lines(src)), re.M):
                why = "a macro of its name in its file (the dispatcher would be renamed)"
            else:
                sig, why = same_everywhere(name, src)
            if why is None:
                try:
                    ret = split_type(name, sig[0])
                except Error as e:
                    why = str(e).split(": ", 1)[1]
            if why is not None:
                left_out.append((src, name, why))
                continue
            entries.append((name, src, ret, sig[1]))
            seen.add(name)
    # a prototype for the dispatcher only where none comes before the
    # definition (port/hooks/<file>.h: the types may be the file's own, later)
    for name, src, _, _ in entries:
        if not all(dumps[(v, src)][name][3] for v in versions):
            UNDECLARED.add(name)
    if report:
        for src, name, why in left_out:
            print(f"left out  {src}  {name}: {why}")
        print(f"{len(entries)} hookable ({len(spec.names)} hand-picked), {len(left_out)} left out")
    by_file = {}
    for name, src, _, _ in entries:
        by_file.setdefault(src, []).append(name)
    if apply:
        unapply_edits({name for name, *_ in entries}, set(by_file))
    for src, names in sorted(by_file.items()):
        if apply:
            apply_edits(names, src)
        for name in names:
            for s, lineno in find_in(name, src):
                check_edits(name, s, lineno)
    return entries


def outputs(entries, slots):
    """entries: [(index, name, src, ret, params)] by index."""
    files = {}
    by_file = {}
    for e in entries:
        by_file.setdefault(e[2], []).append(e)
    count = max(slots.values()) + 1 if slots else 0
    head = "/* GENERATED by tools/port/gen_hooks.py from port/mods/hooks.spec. Do not edit. */\n"
    for src, es in sorted(by_file.items()):
        o = [head.rstrip("\n"), f"/* The hookable functions of {src}: included at its end (PORT_MODS). */",
             '#include "port/hook_slot.h"', ""]
        for index, name, _, ret, params in es:
            o.append(dispatcher(index, name, ret, params))
            o.append("")
        o.append(f"void portHookInit_{stem(src)}(void) {{")
        for index, name, _, ret, params in es:
            o.append(f"    portHookSlots[{index}].body = (void*){name}__body;")
        o.append("}")
        files[os.path.join(INC_DIR, stem(src) + ".inc")] = "\n".join(o) + "\n"
        p = [head.rstrip("\n"), f"/* The hookable functions of {src}: their prototypes, included after its",
             " * #includes (PORT_MODS), for calls made before their definitions. */"]
        for index, name, _, ret, params in es:
            if name in UNDECLARED:
                p.append(f"{'static ' if name in STATIC else ''}{ret} {name}({', '.join(params) or 'void'});")
        files[os.path.join(INC_DIR, stem(src) + ".h")] = "\n".join(p) + "\n"
    lst = [head.rstrip("\n"), "/* The hook slots, for the host (port/mods/mod_api.cpp); a number missing",
           " * from the list is a function no longer hookable (port/mods/hook_slots.txt). */",
           f"#define PORT_HOOK_COUNT {count}",
           "#define PORT_HOOK_LIST \\"]
    lst += [f"    PORT_HOOK({i}, {n}) \\" for i, n, *_ in entries]
    lst += ["", "#define PORT_HOOK_FILES \\"]
    lst += [f"    PORT_HOOK_FILE({stem(s)}) \\" for s in sorted(by_file)]
    lst.append("")
    files[LIST_H] = "\n".join(lst) + "\n"
    live = {n for _, n, *_ in entries}
    names = [""] * count
    for n, i in slots.items():
        names[i] = n if n in live else ""
    m = [head.rstrip("\n"),
         "/* Hook slots for mods (ttyd_mod.h's hook_pre/hook_post/hook_replace).",
         " * For a slot NAME:",
         " *   TtydFn_NAME    the function: a replacement has this type, as does",
         " *                  api->original(TTYD_HOOK_NAME), the game's own",
         " *   TtydPre_NAME   int pre(Ret* result, T0* a0, ...): may change the",
         " *                  arguments; nonzero skips the function (then *result",
         " *                  is what it returns)",
         " *   TtydPost_NAME  void post(Ret* result, T0 a0, ...): may change the result",
         " * Void functions have no result parameter. The numbers never change",
         " * (port/mods/hook_slots.txt); one no longer listed is a function that",
         " * left the list, and a hook on it is refused. */",
         "#ifndef TTYD_MOD_HOOKS_H", "#define TTYD_MOD_HOOKS_H", "",
         "/* The headers declaring the hooked functions (their slots' types). */"]
    m += [f'#include "{h}"' for h in declaring_headers([n for _, n, *_ in entries])]
    m += ["", "enum TtydHookSlot {"]
    m += [f"    TTYD_HOOK_{n} = {i}," for i, n, *_ in entries]
    m += [f"    TTYD_HOOK_COUNT = {count}", "};", ""]
    m += ["/* The slots' function names, by slot (ttyd_mod/compat.h finds a slot by name). */",
          "#define TTYD_HOOK_NAMES { \\"]
    m += [f'    "{n}", \\' for n in names]
    m += ["}", ""]
    for i, n, src, ret, params in entries:
        void = ret == "void"
        res = [] if void else [ret + " *"]
        m.append(f"/* {src} */")
        m.append(f"typedef {ret} (*TtydFn_{n})({', '.join(params) or 'void'});")
        pre = res + [t + " *" if "(*)" not in t else t.replace("(*)", "(**)") for t in params]
        m.append(f"typedef int (*TtydPre_{n})({', '.join(pre) or 'void'});")
        m.append(f"typedef void (*TtydPost_{n})({', '.join(res + params) or 'void'});")
    m += ["", "#endif"]
    files[MOD_H] = "\n".join(m) + "\n"
    return files


def declaring_headers(names):
    """The game headers (include/, as included) whose prototypes declare
    names; one header for each, the first in path order."""
    headers = sorted(glob.glob(os.path.join(ROOT, "include", "**", "*.h"), recursive=True))
    index = {}
    for h in headers:
        text = open(h, encoding="utf-8", errors="replace").read()
        rel = os.path.relpath(h, os.path.join(ROOT, "include")).replace(os.sep, "/")
        for m in re.finditer(r"^[^#/\n][^;{}()\n]*?\b(\w+)\s*\([^;{}]*\)\s*;", text, re.M):
            index.setdefault(m.group(1), rel)
    out = []
    for n in names:
        rel = index.get(n)
        if rel is not None and rel not in out:
            out.append(rel)
    return sorted(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--apply", action="store_true", help="make the PORT_HOOKABLE edits in the game sources")
    ap.add_argument("--check", action="store_true", help="fail if a generated file is stale")
    ap.add_argument("--report", action="store_true", help="list what the @files sets left out, and why")
    a = ap.parse_args()
    try:
        spec = read_spec()
        found = collect(spec, a.apply, a.report)
    except Error as e:
        print(f"gen_hooks: {e}", file=sys.stderr)
        return 2
    slots = read_slots()
    new_slots = dict(slots)
    for name, *_ in found:
        if name not in new_slots:
            new_slots[name] = max(new_slots.values(), default=-1) + 1
    entries = sorted(((new_slots[n], n, s, r, p) for n, s, r, p in found))
    files = outputs(entries, new_slots)
    files[SLOTS] = slots_text(new_slots)
    stale = 0
    os.makedirs(INC_DIR, exist_ok=True)
    os.makedirs(os.path.dirname(MOD_H), exist_ok=True)
    keep = set(files)
    for path, text in sorted(files.items()):
        old = open(path, encoding="utf-8").read() if os.path.exists(path) else None
        rel = os.path.relpath(path, ROOT)
        if a.check:
            if old != text:
                print(f"STALE {rel}")
                stale += 1
        elif old != text:
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
            print(f"wrote {rel}")
    for f in os.listdir(INC_DIR):
        p = os.path.join(INC_DIR, f)
        if p not in keep:
            if a.check:
                print(f"STALE {os.path.relpath(p, ROOT)} (no longer generated)")
                stale += 1
            else:
                os.remove(p)
                print(f"removed {os.path.relpath(p, ROOT)}")
    if a.check and not stale:
        print(f"ok    {len(files)} hook files, {len(entries)} hook slots")
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(main())
