#!/usr/bin/env python3
"""Generate the mods' hook dispatchers (docs/MODDING.md, "Code mods").

Input: port/mods/hooks.spec, one game function per line ('#' comments).
Each must be a global function defined in a game source (src/), not
variadic. Its definition is named PORT_HOOKABLE(name) (include/port_hook.h:
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

Signatures come from clang's AST of the defining file, compiled with the
flags the Aurora builds use (read from build-pc-<jp|us>-aurora/build.ninja,
so configure those first). --apply makes the edits in the game sources
(PORT_HOOKABLE at the definition, the two includes); without it a missing
edit is an error. --check: fail if a generated file is stale.
"""
import argparse
import os
import re
import shlex
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import portversion

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SPEC = os.path.join(ROOT, "port", "mods", "hooks.spec")
INC_DIR = os.path.join(ROOT, "port", "include", "port", "hooks")
LIST_H = os.path.join(ROOT, "port", "include", "port", "hook_list.h")
MOD_H = os.path.join(ROOT, "port", "include", "ttyd_mod", "hooks.h")
SHORT = {"G8MJ01": "jp", "G8ME01": "us"}


class Error(Exception):
    pass


def read_spec():
    names = []
    for lineno, raw in enumerate(open(SPEC, encoding="utf-8"), 1):
        line = raw.split("#", 1)[0].strip()
        if line:
            if not re.match(r"^\w+$", line):
                raise Error(f"hooks.spec:{lineno}: not a function name: {line}")
            names.append(line)
    return names


def def_pattern(name):
    """A definition line of name (as written, or already PORT_HOOKABLE)."""
    n = re.escape(name)
    return re.compile(rf"^(?:USER_FUNC\(\s*(?:PORT_HOOKABLE\({n}\)|{n})\s*\)\s*\{{?\s*|[A-Za-z_][\w \t\*]*?[\s\*](?:PORT_HOOKABLE\({n}\)|{n})\s*\([^;]*)$")


def find_definition(name, sources):
    pat = def_pattern(name)
    found = []
    for src in sources:
        if not src.startswith("src/") or not src.endswith(".c"):
            continue
        lines = open(os.path.join(ROOT, src), encoding="utf-8", errors="replace").read().split("\n")
        for i, ln in enumerate(lines):
            if pat.match(ln.rstrip("\r")) and not ln.lstrip().startswith("static"):
                # the body follows: '{' at the end of this line or alone on a later one
                rest = "\n".join(lines[i:i + 8])
                if re.search(r"\)\s*\{", rest) and ";" not in rest.split("{", 1)[0]:
                    found.append((src, i))
    if len(found) != 1:
        raise Error(f"{name}: {'no' if not found else len(found)} global definition(s) in the game's sources")
    return found[0]


def stem(src):
    return re.sub(r"\W", "_", src[:-2])


def apply_edits(name, src, lineno):
    path = os.path.join(ROOT, src)
    raw = open(path, "rb").read()
    crlf = b"\r\n" in raw
    text = raw.decode("utf-8").replace("\r\n", "\n")
    lines = text.split("\n")
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
    data = text.replace("\n", "\r\n") if crlf else text
    if data.encode("utf-8") != raw:
        open(path, "wb").write(data.encode("utf-8"))
        print(f"edited {src}")


def check_edits(name, src, lineno):
    text = open(os.path.join(ROOT, src), encoding="utf-8", errors="replace").read()
    line = text.split("\n")[lineno]
    if f"PORT_HOOKABLE({name})" not in line or '#include "port_hook.h"' not in text or \
            f"port/hooks/{stem(src)}.inc" not in text or f"port/hooks/{stem(src)}.h" not in text:
        raise Error(f"{name}: {src} lacks its PORT_HOOKABLE edits (run tools/port/gen_hooks.py --apply)")


def compile_flags(version, src):
    """The Aurora build's flags for src, from its build.ninja."""
    ninja = os.path.join(ROOT, f"build-pc-{SHORT[version]}-aurora", "build.ninja")
    if not os.path.exists(ninja):
        raise Error(f"{os.path.relpath(ninja, ROOT)} missing: configure the {version} Aurora build first")
    text = open(ninja, encoding="utf-8", errors="replace").read()
    # (files with Japanese text are compiled from a Shift-JIS copy, sjis/<src>)
    m = re.search(r"^build CMakeFiles/ttyd_dol\.dir/(?:sjis/)?" + re.escape(src) + r"\.(?:obj|o):.*\n((?:  .*\n)+)",
                  text, re.M)
    if not m:
        raise Error(f"{src} is not compiled by the {version} Aurora build")
    flags = []
    for ln in m.group(1).splitlines():
        key, _, val = ln.strip().partition(" = ")
        if key in ("DEFINES", "FLAGS", "INCLUDES"):
            flags += shlex.split(val, posix=True)
    # keep what affects parsing; drop debug info and codegen options
    return [f for f in flags if not f.startswith(("-g", "-O")) and f not in ("-Xclang", "-gcodeview")
            and not f.startswith("--dependent-lib")]


# the location may be "<scratch space>:N:M" (a name made by ## in a macro)
FUNC = re.compile(r"FunctionDecl 0x[0-9a-f]+ .*? (\w+) '([^']*)'")
PARM = re.compile(r"ParmVarDecl 0x[0-9a-f]+ <.*?> (?:col|line):[\d:]+ (?:used |referenced )?(?:(\w+) )?'([^']*)'")


def signature(version, name, src):
    """The definition's return and parameter types as written. Parsed without
    PORT_MODS: the function as the game has it, no dispatcher, no rename."""
    clang = os.environ.get("CLANG", "clang")
    flags = [f for f in compile_flags(version, src) if f != "-DPORT_MODS=1"]
    cmd = [clang, "-fsyntax-only", "-w", *flags,
           "-Xclang", "-ast-dump", "-Xclang", f"-ast-dump-filter={name}", os.path.join(ROOT, src)]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, errors="replace")
    if r.returncode != 0:
        sys.stderr.write(r.stderr[-3000:])
        raise Error(f"{name}: clang failed on {src}")
    ret, params, inside, found = None, [], False, False
    for ln in r.stdout.splitlines():
        m = FUNC.search(ln)
        if m and ln.startswith("FunctionDecl"):
            inside = m.group(1) == name
            if inside:
                t = m.group(2)
                if "..." in t:
                    raise Error(f"{name}: variadic functions can't be hooked")
                depth = 0
                for i, ch in enumerate(t):
                    if ch == "(" and depth == 0 and i > 0:
                        ret = t[:i].strip()
                        break
                    depth += ch == "("
                    depth -= ch == ")"
                params = []
            continue
        if inside:
            mp = PARM.search(ln)
            if mp:
                params.append(mp.group(2))
            elif "CompoundStmt" in ln:
                found = True  # the definition (not a prototype)
                break
    if not found:
        raise Error(f"{name}: its definition was not found in clang's AST of {src}")
    if "(" in ret:
        raise Error(f"{name}: functions returning function pointers can't be hooked")
    return ret, params


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
    o.append(f"{ret} {name}({decl}) {{")
    o.append(f"    PortHookSlot* s = &portHookSlots[{index}];")
    if not void:
        o.append(f"    {declare(ret, 'r')};")
    o.append("    int i, skip = 0;")
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


def generate(version, names, apply):
    sources = portversion.sources(version)
    entries = []  # (index, name, src, ret, params)
    for i, name in enumerate(names):
        src, lineno = find_definition(name, sources)
        if apply:
            apply_edits(name, src, lineno)
            src, lineno = find_definition(name, sources)  # the include moved it down
        check_edits(name, src, lineno)
        ret, params = signature(version, name, src)
        entries.append((i, name, src, ret, params))
    return entries


def outputs(entries):
    files = {}
    by_file = {}
    for e in entries:
        by_file.setdefault(e[2], []).append(e)
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
            p.append(f"{ret} {name}({', '.join(params) or 'void'});")
        files[os.path.join(INC_DIR, stem(src) + ".h")] = "\n".join(p) + "\n"
    lst = [head.rstrip("\n"), "/* The hook slots, for the host (port/mods/mod_api.cpp). */",
           f"#define PORT_HOOK_COUNT {len(entries)}",
           "#define PORT_HOOK_LIST \\"]
    lst += [f"    PORT_HOOK({i}, {n}) \\" for i, n, *_ in entries]
    lst += ["", "#define PORT_HOOK_FILES \\"]
    lst += [f"    PORT_HOOK_FILE({stem(s)}) \\" for s in sorted(by_file)]
    lst.append("")
    files[LIST_H] = "\n".join(lst) + "\n"
    m = [head.rstrip("\n"),
         "/* Hook slots for mods (ttyd_mod.h's hook_pre/hook_post/hook_replace).",
         " * For a slot NAME:",
         " *   TtydFn_NAME    the function: a replacement has this type, as does",
         " *                  api->original(TTYD_HOOK_NAME), the game's own",
         " *   TtydPre_NAME   int pre(Ret* result, T0* a0, ...): may change the",
         " *                  arguments; nonzero skips the function (then *result",
         " *                  is what it returns)",
         " *   TtydPost_NAME  void post(Ret* result, T0 a0, ...): may change the result",
         " * Void functions have no result parameter. */",
         "#ifndef TTYD_MOD_HOOKS_H", "#define TTYD_MOD_HOOKS_H", "",
         "enum TtydHookSlot {"]
    m += [f"    TTYD_HOOK_{n} = {i}," for i, n, *_ in entries]
    m += [f"    TTYD_HOOK_COUNT = {len(entries)}", "};", ""]
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


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--apply", action="store_true", help="make the PORT_HOOKABLE edits in the game sources")
    ap.add_argument("--check", action="store_true", help="fail if a generated file is stale")
    a = ap.parse_args()
    try:
        names = read_spec()
        per = {v: generate(v, names, a.apply) for v in portversion.VERSIONS}
    except Error as e:
        print(f"gen_hooks: {e}", file=sys.stderr)
        return 2
    sigs = {v: [(n, s, r, p) for _, n, s, r, p in es] for v, es in per.items()}
    if sigs["G8MJ01"] != sigs["G8ME01"]:
        diff = [x[0] for x, y in zip(sigs["G8MJ01"], sigs["G8ME01"]) if x != y]
        print(f"gen_hooks: signatures differ between versions: {', '.join(diff)}", file=sys.stderr)
        return 2
    files = outputs(per["G8ME01"])
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
        print(f"ok    {len(files)} hook files")
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(main())
