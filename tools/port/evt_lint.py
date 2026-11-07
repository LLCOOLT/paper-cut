#!/usr/bin/env python3
"""evt_lint: a static checker for every event script of the game and of Paper Cut.

The game's logic runs as event scripts (evt bytecode, include/evt/evt_cmd.h).
A broken script or a broken name in one shows up at runtime as a crash, a hang,
a red "No messages" box or a warp into nothing. This finds them without playing.

How it reads the scripts: each translation unit with scripts is compiled by
clang's front end to LLVM IR (the US Aurora build's own flags, from
compile_commands.json; nothing is written but a small cache), so every script
comes out as the exact words the game runs: macros expanded, enums and FLOAT()
evaluated, #ifdefs resolved, every pointer a named reference to a function,
string or table. That is more complete than parsing the macros (no evaluator to
get wrong) and more readable than a built object (names, not relocations).
The functions the scripts call are read from the C sources (evt_lint_csrc.py):
how many argument words each reads, as what C type, and where the value goes.

Checks, per script:
  flow      opcodes valid, IF/ELSE/END_IF, SWITCH/CASE/END_SWITCH, LOOP/END_LOOP,
            inline/sibling blocks balanced, BREAK/CONTINUE inside their block,
            GOTO targets present (and within the 16-label table), END_SCRIPT
            before the array ends, nesting within the engine's 8 levels
  vars      LW/LF/GW/GF/LSW/LSWF/GSW/GSWF indices in range; GSW bytes compared
            or set with values a byte can't hold
  call      USER_FUNC argument count against the words the function reads;
            a pointer passed where the function reads an s32 (cut to 32 bits
            on PC); a number where it reads a pointer; outputs that are not
            variables; calls to functions with no C definition found
  refs      map names (MapData list), bero/entry names of the target map,
            NPC names entered in the map, mobj names, map joints/hit objects
            (the map's m/<map>/d file on the disc), item ids, message tags in
            every language's message files (US, JP, EU's five) or Paper Cut's own

Each finding is "faithful" (the original game's script, as shipped) or "port"
(Paper Cut's own script or a line inside #ifdef TARGET_PC / PORT_MODS).

    evt_lint.py [--build DIR] [-j N] [--report local-docs/EVT_LINT.md]
    evt_lint.py --check              compare with tests/port/evt_lint_baseline.txt (exit 1 on new findings)
    evt_lint.py --update-baseline    record the current findings as the baseline
Without the disc images (orig/<id>/*.iso) the message and map-joint checks are
skipped, and --check compares only the others.
"""

import argparse
import collections
import concurrent.futures as cf
import json
import os
import pickle
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import evt_lint_csrc as csrc  # noqa: E402
import evt_lint_ir as ir  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(HERE))
BASELINE = os.path.join(ROOT, "tests", "port", "evt_lint_baseline.txt")
CACHE_VERSION = 3
DISC_CACHE = 2

# ---------------------------------------------------------------- opcodes
OPS = {}
for _i, _n in enumerate("""NEXT END_SCRIPT END_EVENT LABEL GOTO DO WHILE DO_BREAK DO_CONTINUE WAIT_FRAME WAIT_MSEC HALT
IF_STR_EQUAL IF_STR_NOT_EQUAL IF_STR_LESS IF_STR_GREATER IF_STR_LESS_EQUAL IF_STR_GREATER_EQUAL IFF_EQUAL
IFF_NOT_EQUAL IFF_LESS IFF_GREATER IFF_LESS_EQUAL IFF_GREATER_EQUAL IF_EQUAL IF_NOT_EQUAL IF_LESS IF_GREATER
IF_LESS_EQUAL IF_GREATER_EQUAL IF_FLAG IF_NOT_FLAG ELSE END_IF SWITCH SWITCHI CASE_EQUAL CASE_NOT_EQUAL CASE_LESS
CASE_GREATER CASE_LESS_EQUAL CASE_GREATER_EQUAL CASE_ETC CASE_OR CASE_AND CASE_FLAG CASE_END CASE_BETWEEN
SWITCH_BREAK END_SWITCH SET SETI SETF ADD SUB MUL DIV MOD ADDF SUBF MULF DIVF SET_READ READ READ2 READ3 READ4
READ_N SET_READF READF READF2 READF3 READF4 READF_N SET_USER_WORK SET_USER_FLAG ALLOC_USER_WORK AND ANDI OR ORI
SET_FRAME_FROM_MSEC SET_MSEC_FROM_FRAME SET_RAM SET_RAMF GET_RAM GET_RAMF SETR SETRF GETR GETRF USER_FUNC RUN_EVENT
RUN_EVENT_ID RUN_CHILD_EVENT DELETE_EVENT RESTART_EVENT SET_PRI SET_SPD SET_TYPE STOP_ALL START_ALL STOP_OTHER
START_OTHER STOP_ID START_ID CHECK_EVENT INLINE_EVENT INLINE_EVENT_ID END_INLINE BROTHER_EVENT BROTHER_EVENT_ID
END_BROTHER DEBUG_PUT_MSG DEBUG_MSG_CLEAR DEBUG_PUT_REG DEBUG_NAME DEBUG_REM DEBUG_BP""".split()):
    OPS[_n] = _i
OPNAME = {v: k for k, v in OPS.items()}
MAX_OP = max(OPS.values())
# the argument count each opcode's macro gives it (None: any)
OP_ARGS = {"END_SCRIPT": 0, "END_EVENT": 0, "LABEL": 1, "GOTO": 1, "DO": 1, "WHILE": 0, "DO_BREAK": 0,
           "DO_CONTINUE": 0, "WAIT_FRAME": 1, "WAIT_MSEC": 1, "HALT": 1, "ELSE": 0, "END_IF": 0, "SWITCH": 1,
           "SWITCHI": 1, "CASE_ETC": 0, "CASE_END": 0, "CASE_BETWEEN": 2, "SWITCH_BREAK": 0, "END_SWITCH": 0,
           "RUN_EVENT": 1, "RUN_EVENT_ID": 2, "RUN_CHILD_EVENT": 1, "INLINE_EVENT": 0, "INLINE_EVENT_ID": 1,
           "END_INLINE": 0, "BROTHER_EVENT": 0, "BROTHER_EVENT_ID": 1, "END_BROTHER": 0}
for _n in OPS:
    if _n.startswith("IF"):
        OP_ARGS[_n] = 2
    elif _n.startswith("CASE_") and _n not in OP_ARGS:
        OP_ARGS[_n] = 1
    elif _n in ("SET", "SETI", "SETF", "ADD", "SUB", "MUL", "DIV", "MOD", "ADDF", "SUBF", "MULF", "DIVF", "AND",
                "ANDI", "OR", "ORI", "SETR", "SETRF", "GETR", "GETRF", "SET_RAM", "SET_RAMF", "GET_RAM",
                "GET_RAMF", "SET_FRAME_FROM_MSEC", "SET_MSEC_FROM_FRAME", "CHECK_EVENT", "ALLOC_USER_WORK",
                "READ_N", "READF_N", "READ2", "READF2"):
        OP_ARGS[_n] = 2
    elif _n in ("READ3", "READF3"):
        OP_ARGS[_n] = 3
    elif _n in ("READ4", "READF4"):
        OP_ARGS[_n] = 4
    elif _n in ("SET_READ", "SET_READF", "READ", "READF", "SET_USER_WORK", "SET_USER_FLAG", "DELETE_EVENT",
                "RESTART_EVENT", "SET_PRI", "SET_SPD", "SET_TYPE", "STOP_ALL", "START_ALL", "STOP_OTHER",
                "START_OTHER", "STOP_ID", "START_ID", "DEBUG_PUT_MSG", "DEBUG_PUT_REG", "DEBUG_NAME", "DEBUG_REM"):
        OP_ARGS[_n] = 1
    elif _n in ("DEBUG_MSG_CLEAR", "DEBUG_BP", "NEXT"):
        OP_ARGS[_n] = 0
# argument positions read raw (not a variable reference) per opcode
RAW_ARGS = {"SETI": {1}, "ANDI": {1}, "ORI": {1}, "LABEL": {0}, "DO": set(), "SWITCHI": {0},
            "DEBUG_PUT_MSG": {0}, "DEBUG_NAME": {0}, "DEBUG_REM": {0}, "ALLOC_USER_WORK": {0}}

# variable encodings (evt_cmd.h): name, base, max (inclusive upper end of the encoded range), count
VARS = [  # checked from the most negative up, as evtGetValue does
    ("ADDR", None, -270000000, None),
    ("PTR", None, -250000000, None),
    ("FLOAT", None, -220000000, None),
    ("UF", 210000000, -200000000, None),
    ("UW", 190000000, -180000000, None),
    ("GSW", 170000000, -160000000, 0x800),
    ("LSW", 150000000, -140000000, 0x400),
    ("GSWF", 130000000, -120000000, 0x100 * 32),
    ("LSWF", 110000000, -100000000, 0x10 * 32),
    ("GF", 90000000, -80000000, 96),
    ("LF", 70000000, -60000000, 96),
    ("GW", 50000000, -40000000, 32),
    ("LW", 30000000, -20000000, 16),
]
EVT_NULLPTR = -250000000


def decode_var(v):
    """(kind, index) for a word, or None for a plain number."""
    if not isinstance(v, int):
        return None
    v = ((v + (1 << 63)) % (1 << 64)) - (1 << 63)
    if v <= -270000000:
        return None  # an address or a large negative literal
    for name, base, mx, cnt in VARS:
        if v <= mx:
            if base is None:
                return (name, None)
            return (name, v + base)
    return None


def s32(v):
    return ((v + (1 << 31)) % (1 << 32)) - (1 << 31)


# ---------------------------------------------------------------- findings
SEVERITY = {
    "flow-bad-opcode": "error", "flow-off-end": "error", "flow-unbalanced": "error", "flow-goto-missing": "error",
    "flow-labels-over-16": "error", "flow-nesting": "error", "flow-break-outside": "error",
    "flow-arg-count": "error",
    "var-range": "error", "var-gsw-byte": "warn",
    "call-unknown-func": "info", "call-too-few-args": "error", "call-maybe-too-few-args": "warn",
    "call-extra-args": "info", "call-ptr-into-s32": "error", "call-int-into-ptr": "warn",
    "call-output-not-var": "warn", "call-not-a-func": "error",
    "ref-map": "error", "ref-bero": "error", "ref-npc": "warn", "ref-mobj": "warn", "ref-model": "warn",
    "ref-hit": "warn", "ref-item": "error", "ref-script": "error",
    "msg-missing-all": "error", "msg-port-direct-text": "warn", "rando-target": "error", "msg-missing-some": "error", "msg-port-missing": "error",
}
NEEDS_DISC = {"ref-model", "ref-hit", "ref-npc", "msg-missing-all", "msg-missing-some"}


class Finding:
    __slots__ = ("cat", "path", "script", "detail", "where", "origin", "subject", "maps")

    def __init__(self, cat, path, script, detail, where="", subject=None, maps=()):
        self.cat, self.path, self.script, self.detail, self.where = cat, path, script, detail, where
        self.origin = "faithful"
        self.subject = subject
        self.maps = tuple(sorted(maps))

    def key(self):
        return "%s\t%s\t%s\t%s" % (self.cat, self.path, self.script, self.detail)


# ---------------------------------------------------------------- IR collection
def tu_list(build):
    with open(os.path.join(build, "compile_commands.json"), encoding="utf-8") as f:
        cc = json.load(f)
    out = []
    seen = set()
    for e in cc:
        src = e["file"].replace("\\", "/")
        b = build.replace("\\", "/")
        orig = src
        for pre in (b + "/statics/", b + "/sjis/"):
            if src.lower().startswith(pre.lower()):
                orig = ROOT.replace("\\", "/") + "/" + src[len(pre):]
        if not orig.endswith((".c", ".cpp")) or not os.path.exists(orig):
            continue
        rel = os.path.relpath(orig, ROOT).replace("\\", "/")
        if rel in seen or not rel.startswith(("src/", "rels/", "port/")):
            continue
        try:
            with open(orig, encoding="utf-8", errors="replace") as f:
                text = f.read()
        except OSError:
            continue
        if not re.search(r"EVT_BEGIN|BeroEntry|relSetEvtAddr|relSetBtlAddr|NpcSetupInfo|EvtWord\s*\*", text):
            continue
        seen.add(rel)
        out.append((rel, orig, e))
    return out


def ir_command(e, orig):
    cmd = e["command"]
    cmd = re.sub(r" -o \S+ -c \S+", " ", cmd)
    cmd = cmd.replace(" -g -Xclang -gcodeview", " ").replace(" -g ", " ")
    return cmd + " -S -emit-llvm -g0 -Xclang -disable-llvm-passes -o - \"%s\"" % orig


def compact(mod):
    """What evt_lint keeps of a module: globals' decoded values, functions' references."""
    g = {}
    for name, (ty, val, linkage) in mod.globals.items():
        local = "internal" in linkage or "private" in linkage
        p = ir.Parser([], mod.types)
        rty = p.resolve(ty)
        if isinstance(val, ir.Str):
            g[name] = ("str", bytes(val), local)
        elif rty[0] == "arr" and p.resolve(rty[2]) in (("i", 64),):
            g[name] = ("words", ir.flat_words(mod, ty, val), local)
        elif rty[0] == "arr" and p.resolve(rty[2])[0] == "st":
            g[name] = ("structs", ir.array_of_structs(mod, ty, val), local)
        elif rty[0] == "st":
            g[name] = ("struct", ir.struct_fields(mod, ty, val), local)
        elif rty[0] == "arr" and p.resolve(rty[2]) == ("ptr",):
            g[name] = ("ptrs", ir.flat_words(mod, ty, val), local)
        else:
            refs = ir.all_refs(val, set())
            if refs:
                g[name] = ("other", sorted(refs), local)
    fr = {k: sorted(v) for k, v in mod.func_refs.items()}
    return {"globals": g, "funcs": fr}


def build_one(args):
    rel, orig, e, cache = args
    cpath = os.path.join(cache, rel.replace("/", "__") + ".pkl")
    stamp = (CACHE_VERSION, os.path.getmtime(orig), e["command"])
    if os.path.exists(cpath):
        try:
            with open(cpath, "rb") as f:
                d = pickle.load(f)
            if d.get("stamp") == stamp:
                return rel, d, None
        except Exception:
            pass
    cmd = ir_command(e, orig)
    r = subprocess.run(cmd, cwd=e["directory"], capture_output=True, shell=False)
    if r.returncode != 0:
        return rel, None, r.stderr.decode("utf-8", "replace")[-1500:]
    mod = ir.parse_module(r.stdout.decode("latin-1"))
    d = compact(mod)
    d["stamp"] = stamp
    with open(cpath, "wb") as f:
        pickle.dump(d, f, protocol=4)
    return rel, d, None


def collect_ir(build, jobs, quiet=False):
    cache = os.path.join(build, "evt_lint")
    os.makedirs(cache, exist_ok=True)
    tus = tu_list(build)
    out = {}
    errs = {}
    t0 = time.time()
    with cf.ThreadPoolExecutor(max_workers=jobs) as ex:
        futs = [ex.submit(build_one, (rel, orig, e, cache)) for rel, orig, e in tus]
        for i, fu in enumerate(cf.as_completed(futs)):
            rel, d, err = fu.result()
            if d is None:
                errs[rel] = err
            else:
                out[rel] = d
            if not quiet and (i + 1) % 100 == 0:
                print("  %d/%d units (%.0fs)" % (i + 1, len(tus), time.time() - t0), file=sys.stderr)
    return dict(sorted(out.items())), errs  # (sorted: the same globals win each run)


# ---------------------------------------------------------------- source facts
PC_COND = re.compile(r"\b(TARGET_PC|PORT_MODS|TTYD_BACKEND_\w+|PORT_\w+)\b")


def port_lines(text):
    """Set of 1-based line numbers inside a port-only #if region."""
    stack = []  # each: port-active for this branch
    out = set()
    for i, ln in enumerate(text.split("\n"), 1):
        s = ln.strip()
        m = re.match(r"#\s*(ifdef|ifndef|if|elif|else|endif)\b(.*)", s)
        if m:
            d, cond = m.group(1), m.group(2)
            if d in ("ifdef", "if"):
                pos = bool(PC_COND.search(cond)) and not re.search(r"!\s*defined\s*\(?\s*(TARGET_PC|PORT_MODS)|"
                                                                   r"!\s*(TARGET_PC|PORT_MODS)", cond)
                neg = bool(PC_COND.search(cond)) and not pos
                stack.append([pos, neg])
            elif d == "ifndef":
                neg = bool(PC_COND.search(cond))
                stack.append([False, neg])
            elif d == "elif":
                if stack:
                    stack[-1] = [bool(PC_COND.search(cond)) and "!" not in cond, False]
            elif d == "else":
                if stack:
                    pos, neg = stack[-1]
                    stack[-1] = [neg, pos]
            elif d == "endif":
                if stack:
                    stack.pop()
            if any(p for p, _ in stack):
                out.add(i)
            continue
        if any(p for p, _ in stack):
            out.add(i)
    return out


class SourceFacts:
    def __init__(self):
        self.text = {}
        self.port = {}
        self.script_lines = {}  # (path, name) -> (begin, end)
        self.script_names = set()

    def load(self, rel):
        if rel in self.text:
            return
        p = os.path.join(ROOT, rel)
        try:
            with open(p, encoding="utf-8", errors="replace") as f:
                t = f.read()
        except OSError:
            t = ""
        self.text[rel] = t
        self.port[rel] = port_lines(t)
        lines = t.split("\n")
        cur = None
        for i, ln in enumerate(lines, 1):
            m = re.search(r"\bEVT_BEGIN\(\s*(\w+)\s*\)", ln)
            if m:
                cur = (m.group(1), i)
                self.script_names.add(m.group(1))
            elif cur and re.search(r"\bEVT_END(_PAD\d?)?\s*\(", ln):
                self.script_lines[(rel, cur[0])] = (cur[1], i)
                cur = None

    def origin(self, rel, script, subject):
        if rel.startswith(("port/", "examples/")):
            return "port"
        self.load(rel)
        span = self.script_lines.get((rel, script))
        pl = self.port.get(rel, set())
        if span is None:
            # a table or a script from an included file
            return "faithful"
        b, e = span
        if b in pl:
            return "port"
        if subject:
            lines = self.text[rel].split("\n")
            for i in range(b, e + 1):
                if i in pl and subject in lines[i - 1]:
                    return "port"
        return "faithful"


# ---------------------------------------------------------------- disc data
class DiscData:
    LANGS = [("US", "G8ME01", "msg/US"), ("JP", "G8MJ01", "msg"), ("EU-en", "G8MP01", "msg/US"),
             ("EU-de", "G8MP01", "msg/DE"), ("EU-fr", "G8MP01", "msg/FR"), ("EU-es", "G8MP01", "msg/SP"),
             ("EU-it", "G8MP01", "msg/IT")]

    def __init__(self, cache):
        self.ok = False
        self.msg = {}  # lang -> file stem -> set(tags)
        self.joints = {}  # map -> set(names)
        self.npcs = {}  # map -> NPC names of its setup file (m/<map>/s)
        self.cache = cache

    def load(self):
        cpath = os.path.join(self.cache, "disc.pkl")
        isos = {}
        for v in ("G8ME01", "G8MJ01", "G8MP01"):
            d = os.path.join(ROOT, "orig", v)
            if os.path.isdir(d):
                for n in os.listdir(d):
                    if n.lower().endswith((".iso", ".gcm")):
                        isos[v] = os.path.join(d, n)
        if not isos:
            return False
        stamp = (DISC_CACHE, sorted((v, os.path.getsize(p)) for v, p in isos.items()))
        if os.path.exists(cpath):
            try:
                with open(cpath, "rb") as f:
                    d = pickle.load(f)
                if d["stamp"] == stamp:
                    self.msg, self.joints, self.npcs, self.ok = d["msg"], d["joints"], d["npcs"], True
                    return True
            except Exception:
                pass
        import gcdisc
        discs = {v: gcdisc.Disc(p) for v, p in isos.items()}
        for lang, v, d in self.LANGS:
            if v not in discs:
                continue
            files = {}
            for path, size in sorted((k, v2[1]) for k, v2 in discs[v].files.items() if k.startswith(d + "/")):
                rest = path[len(d) + 1:]
                if "/" in rest or not rest.endswith(".txt"):
                    continue
                data = discs[v].read(path)
                parts = data.split(b"\0")
                tags = set()
                for k in range(0, len(parts) - 1, 2):
                    if parts[k]:
                        tags.add(parts[k].decode("latin-1"))
                files[rest[:-4]] = tags
            self.msg[lang] = files
        disc = discs.get("G8ME01") or next(iter(discs.values()))
        for path, size in sorted((k, v2[1]) for k, v2 in disc.files.items() if k.startswith("m/")):
            parts = path.split("/")
            if len(parts) == 3 and parts[2] == "d":
                try:
                    self.joints[parts[1]] = map_joint_names(disc.read(path))
                except Exception:
                    self.joints[parts[1]] = None
            if len(parts) == 3 and parts[2] == "s":
                xml = disc.read(path).decode("cp932", "replace")
                self.npcs[parts[1]] = set(re.findall(r'<name>\s*"([^"]*)"', xml))
        for path, size in sorted((k, v2[1]) for k, v2 in disc.files.items() if k.startswith("b/")):
            parts = path.split("/")
            if len(parts) == 3 and parts[2] == "d":
                try:
                    self.joints["b/" + parts[1]] = map_joint_names(disc.read(path))
                except Exception:
                    pass
        with open(cpath, "wb") as f:
            pickle.dump({"stamp": stamp, "msg": self.msg, "joints": self.joints, "npcs": self.npcs}, f, protocol=4)
        self.ok = True
        return True


def map_joint_names(b):
    import struct as st
    u32 = lambda o: st.unpack_from(">I", b, o)[0]  # noqa: E731
    data_size, rel_count, chunk_count, extern_count = u32(4), u32(8), u32(12), u32(16)
    data = 0x20
    chunks = data + data_size + rel_count * 4
    strings = chunks + chunk_count * 8 + extern_count * 8

    def cstr(o):
        e = b.index(b"\0", o)
        return b[o:e].decode("latin-1")
    info = None
    for k in range(chunk_count):
        off, so = u32(chunks + k * 8), u32(chunks + k * 8 + 4)
        if cstr(strings + so) == "information":
            info = data + off
    names = set()
    if info is None:
        return names
    seen = set()

    def walk(j):
        while j and j not in seen and data + j < len(b):
            seen.add(j)
            jo = data + j
            names.add(cstr(data + u32(jo)))
            walk(u32(jo + 0xC))
            j = u32(jo + 0x10)
    walk(u32(info + 4))
    return names


# ---------------------------------------------------------------- the linker view
def text_of(b):
    """A script string as text: the sources are UTF-8 (Japanese NPC names); anything else as bytes."""
    try:
        return b.decode("utf-8")
    except UnicodeDecodeError:
        return b.decode("latin-1")


class World:
    def __init__(self, units):
        self.units = units
        self.globals = {}  # unit-independent name -> (unit, kind, value)
        self.local = {}  # (unit, name) -> (kind, value)
        self.func_def = {}  # function name -> unit
        for rel, d in units.items():
            for name, (kind, val, local) in d["globals"].items():
                self.local[(rel, name)] = (kind, val)
                if not local or name.startswith("??_C@"):
                    if name not in self.globals or kind != "other":
                        self.globals[name] = (rel, kind, val)
            for fn in d["funcs"]:
                self.func_def.setdefault(fn, rel)

    def get(self, rel, name):
        """(unit, kind, value) of name as seen from unit rel."""
        v = self.local.get((rel, name))
        if v is not None:
            return (rel, v[0], v[1])
        g = self.globals.get(name)
        return g

    def string(self, rel, ref):
        if not isinstance(ref, ir.Ref):
            return None
        g = self.get(rel, ref.name)
        if g and g[1] == "str":
            b = g[2][ref.off:]
            e = b.find(b"\0")
            return text_of(b if e < 0 else b[:e])
        return None

    def is_func(self, name):
        return name in self.func_def or (name not in self.globals and not name.startswith("??_C@")
                                         and not any(name == n for (_, n) in ()))


# ---------------------------------------------------------------- script decode
class Op:
    __slots__ = ("pos", "op", "args")

    def __init__(self, pos, op, args):
        self.pos, self.op, self.args = pos, op, args


def decode(words):
    """(ops, problems) for a script's words."""
    ops = []
    probs = []
    pos = 0
    n = len(words)
    ended = False
    while pos < n:
        w = words[pos]
        if not isinstance(w, int):
            probs.append(("flow-bad-opcode", "word %d is a pointer (%s) where an opcode belongs" % (pos, w.name)))
            return ops, probs, False
        w &= 0xFFFFFFFFFFFFFFFF
        op = w & 0xFFFF
        cnt = (w >> 16) & 0xFFFF
        if op > MAX_OP or (w >> 32):
            probs.append(("flow-bad-opcode", "word %d = %#x is no opcode" % (pos, w)))
            return ops, probs, False
        name = OPNAME[op]
        if pos + 1 + cnt > n:
            probs.append(("flow-off-end", "%s at word %d wants %d arguments past the end" % (name, pos, cnt)))
            return ops, probs, False
        ops.append(Op(pos, name, words[pos + 1:pos + 1 + cnt]))
        pos += 1 + cnt
        if name == "END_SCRIPT":
            ended = True
            break
    if not ended:
        probs.append(("flow-off-end", "no END_SCRIPT: the script runs into whatever follows it"))
    return ops, probs, ended


def looks_like_script(words):
    if not words or not isinstance(words[0], int):
        return False
    ops, probs, ended = decode(words)
    return ended and not probs


# ---------------------------------------------------------------- the checker
class Linter:
    def __init__(self, world, funcs_summary, facts, disc, consts, port_tags):
        self.w = world
        self.fs = funcs_summary
        self.facts = facts
        self.disc = disc
        self.consts = consts
        self.port_tags = port_tags
        self.findings = []
        self.scripts = {}  # (unit, name) -> words
        self.maps_of = collections.defaultdict(set)  # (unit, name) -> maps reaching it
        self.map_names = set()
        self.map_scripts = collections.defaultdict(set)
        self.battle_areas = collections.defaultdict(set)
        self.stats = collections.Counter()

    def add(self, cat, unit, script, detail, where="", subject=None, maps=()):
        f = Finding(cat, unit, script, detail, where, subject, maps)
        f.origin = self.facts.origin(unit, script, subject)
        self.findings.append(f)

    # -- which globals are scripts
    def find_scripts(self):
        for (rel, name), (kind, val) in self.w.local.items():
            if kind != "words":
                continue
            if name in self.facts.script_names or looks_like_script(val):
                if name in self.facts.script_names or rel.startswith(("rels/", "src/", "port/")):
                    if looks_like_script(val) or name in self.facts.script_names:
                        self.scripts[(rel, name)] = val

    def resolve(self, rel, ref):
        """(unit, name) of a referenced global, as the linker would pick it."""
        if (rel, ref.name) in self.w.local:
            return (rel, ref.name)
        g = self.w.globals.get(ref.name)
        if g:
            return (g[0], ref.name)
        return None

    # -- map ownership: from each map's init script, every script and table reachable
    def walk_maps(self):
        for rel, text in list(self.facts.text.items()):
            pass
        for rel in self.w.units:
            self.facts.load(rel)
            t = self.facts.text[rel]
            for m in re.finditer(r'relSetEvtAddr\(\s*"(\w+)"\s*,\s*(\w+)\s*\)', t):
                mp, sym = m.group(1), m.group(2)
                node = self.resolve(rel, ir.Ref(sym))
                if node:
                    self.reach(node, mp)
            for m in re.finditer(r'relSetBtlAddr\(\s*"(\w+)"\s*,\s*(\w+)\s*,\s*(\w+)\s*\)', t):
                for sym in (m.group(2), m.group(3)):
                    node = self.resolve(rel, ir.Ref(sym))
                    if node:
                        self.reach(node, "btl:" + m.group(1))
        # the map list
        for (rel, name), (kind, val) in self.w.local.items():
            if rel == "src/data/mapdata.c" and kind == "struct" and val and isinstance(val[0], ir.Ref):
                s = self.w.string(rel, val[0])
                if s:
                    self.map_names.add(s)

    def reach(self, start, mp):
        stack = [start]
        while stack:
            node = stack.pop()
            if mp in self.maps_of[node]:
                continue
            self.maps_of[node].add(mp)
            self.map_scripts[mp].add(node)
            rel, name = node
            g = self.w.local.get(node)
            refs = set()
            if g is not None:
                ir.all_refs(g[1], refs)
            elif name in self.w.units.get(rel, {}).get("funcs", {}):
                if rel.startswith("rels/") or rel.startswith("port/"):
                    refs = set(self.w.units[rel]["funcs"][name])
            for r in refs:
                if r.startswith("??_C@"):
                    continue
                nn = self.resolve(rel, ir.Ref(r))
                if nn is None:
                    fu = self.w.func_def.get(r)
                    if fu and (fu.startswith("rels/") or fu.startswith("port/")):
                        nn = (fu, r)
                if nn is not None and mp not in self.maps_of[nn]:
                    stack.append(nn)

    # -- per-map name sets
    def collect_map_names(self):
        self.npcs = collections.defaultdict(set)
        self.mobjs = collections.defaultdict(set)
        self.beros = collections.defaultdict(set)
        self.str_compares = collections.defaultdict(set)
        self.bero_tables = []  # (unit, name, rows)
        for rel in self.w.units:
            t = self.facts.text.get(rel, "")
            for m in re.finditer(r"\bBeroEntry\s+(\w+)\s*\[", t):
                g = self.w.local.get((rel, m.group(1)))
                if g and g[0] == "structs":
                    self.bero_tables.append((rel, m.group(1), g[1]))
        for rel, name, rows in self.bero_tables:
            for mp in self.maps_of.get((rel, name), ()):
                for row in rows:
                    if row and len(row) >= 13:
                        s = self.w.string(rel, row[0])
                        if s:
                            self.beros[mp].add(s)
        for (rel, name), words in self.scripts.items():
            ops, _, _ = decode(words)
            mps = self.maps_of.get((rel, name), ())
            for op, lw_strings, lw_ptr in self.track(rel, ops):
                if op.op.startswith("IF_STR") or op.op == "CASE_EQUAL" or op.op == "SET":
                    for a in op.args:
                        s = self.w.string(rel, a)
                        if s:
                            for mp in mps:
                                self.str_compares[mp].add(s)
                if op.op == "USER_FUNC" and op.args and isinstance(op.args[0], ir.Ref):
                    info = csrc.pick(self.fs.get(op.args[0].name), rel)
                    if not info:
                        continue
                    for i, a in enumerate(op.args[1:]):
                        ai = info["args"].get(i)
                        if not ai:
                            continue
                        for s in self.cands(rel, a, lw_strings):
                            if "npc_def" in ai["kinds"]:
                                for mp in mps:
                                    self.npcs[mp].add(s)
                            if "mobj_def" in ai["kinds"]:
                                for mp in mps:
                                    self.mobjs[mp].add(s)

    def cands(self, rel, a, lw_strings):
        """The strings an argument word can hold: its own, or those last SET into its variable."""
        s = self.w.string(rel, a)
        if s is not None:
            return [s]
        if decode_var(a) is not None and a in lw_strings:
            return list(dict.fromkeys(lw_strings[a]))
        return []

    def track(self, rel, ops):
        """Each op with the strings and pointers its variables were last SET to (straight-line, every
        branch's SET kept until the variable is used)."""
        lw_strings = collections.defaultdict(list)
        lw_ptr = {}
        for op in ops:
            yield op, lw_strings, lw_ptr
            if op.op in ("SET", "SETI") and len(op.args) == 2 and decode_var(op.args[0]):
                tgt = op.args[0]
                s = self.w.string(rel, op.args[1])
                if s is not None:
                    lw_strings[tgt].append(s)
                elif op.args[1] in lw_strings and decode_var(op.args[1]):
                    lw_strings[tgt] = list(lw_strings[op.args[1]])
                else:
                    lw_strings.pop(tgt, None)
                lw_ptr[tgt] = isinstance(op.args[1], ir.Ref) or bool(lw_ptr.get(op.args[1]))
            elif op.op == "USER_FUNC":
                for a in op.args[1:]:
                    if decode_var(a):
                        lw_strings.pop(a, None)
                        lw_ptr.pop(a, None)  # (it may be an output: what it holds now is unknown)
            elif op.args and decode_var(op.args[0]) and not (
                    op.op.startswith("IF") or op.op.startswith("CASE") or op.op.startswith("SWITCH")):
                lw_strings.pop(op.args[0], None)
                lw_ptr.pop(op.args[0], None)
                for a in op.args[1:]:
                    if op.op.startswith("READ") or op.op.startswith("GET"):
                        lw_strings.pop(a, None)
                        lw_ptr.pop(a, None)

    # -- the checks
    def run(self):
        for (rel, name), words in sorted(self.scripts.items()):
            self.check_script(rel, name, words)
        self.check_bero_tables()

    def check_bero_tables(self):
        for rel, name, rows in self.bero_tables:
            for i, row in enumerate(rows):
                if not row or len(row) < 13:
                    continue
                mp = self.w.string(rel, row[11])
                be = self.w.string(rel, row[12])
                if mp:
                    self.check_map_bero(rel, name, "row %d" % i, mp, be)
                for fi in (8, 10):
                    r = row[fi] if fi < len(row) else None
                    if isinstance(r, ir.Ref) and self.resolve(rel, r) is None and r.name not in self.w.func_def:
                        self.add("ref-script", rel, name, "row %d event %s not defined" % (i, r.name), "", r.name)

    def check_map_bero(self, rel, script, where, mp, be):
        if mp not in self.map_names:
            self.add("ref-map", rel, script, "map '%s' is not in the map list" % mp, where, '"%s"' % mp)
            return
        if be:
            known = self.beros.get(mp, set()) | self.str_compares.get(mp, set())
            if mp in self.map_scripts and be not in known:
                self.add("ref-bero", rel, script, "entry '%s' not in %s's bero tables or entry checks" % (be, mp),
                         where, '"%s"' % be)

    def check_script(self, rel, name, words):
        self.stats["scripts"] += 1
        self.stats["scripts-" + ("port" if rel.startswith("port/") else rel.split("/")[0])] += 1
        ops, probs, ended = decode(words)
        for cat, d in probs:
            self.add(cat, rel, name, d)
        self.stats["ops"] += len(ops)
        mps = self.maps_of.get((rel, name), set())
        # control flow
        stack = []
        labels = []
        gotos = []
        inline_starts = []
        for i, op in enumerate(ops):
            o = op.op
            want = OP_ARGS.get(o)
            if want is not None and len(op.args) != want and o != "USER_FUNC":
                self.add("flow-arg-count", rel, name, "%s with %d arguments (wants %d)" % (o, len(op.args), want),
                         "word %d" % op.pos)
            if o.startswith("IF"):
                stack.append(["if", False, op])
            elif o == "ELSE":
                if not stack or stack[-1][0] != "if":
                    self.add("flow-unbalanced", rel, name, "ELSE outside an IF", "word %d" % op.pos)
                elif stack[-1][1]:
                    self.add("flow-unbalanced", rel, name, "second ELSE in one IF", "word %d" % op.pos)
                else:
                    stack[-1][1] = True
            elif o == "END_IF":
                if not stack or stack[-1][0] != "if":
                    self.add("flow-unbalanced", rel, name, "END_IF without its IF (inside %s)" %
                             (stack[-1][0] if stack else "nothing"), "word %d" % op.pos)
                    self.pop_to(stack, "if")
                else:
                    stack.pop()
            elif o in ("SWITCH", "SWITCHI"):
                stack.append(["switch", False, op])
                if sum(1 for s in stack if s[0] == "switch") > 8:
                    self.add("flow-nesting", rel, name, "SWITCH nested deeper than 8", "word %d" % op.pos)
            elif o.startswith("CASE_"):
                if not stack or stack[-1][0] != "switch":
                    self.add("flow-unbalanced", rel, name, "%s outside a SWITCH" % o, "word %d" % op.pos)
            elif o == "SWITCH_BREAK":
                if not any(s[0] == "switch" for s in stack):
                    self.add("flow-break-outside", rel, name, "BREAK_SWITCH outside a SWITCH", "word %d" % op.pos)
            elif o == "END_SWITCH":
                if not stack or stack[-1][0] != "switch":
                    self.add("flow-unbalanced", rel, name, "END_SWITCH without its SWITCH", "word %d" % op.pos)
                    self.pop_to(stack, "switch")
                else:
                    stack.pop()
            elif o == "DO":
                stack.append(["loop", False, op])
                if sum(1 for s in stack if s[0] == "loop") > 8:
                    self.add("flow-nesting", rel, name, "LOOP nested deeper than 8", "word %d" % op.pos)
            elif o in ("DO_BREAK", "DO_CONTINUE"):
                if not any(s[0] == "loop" for s in stack):
                    self.add("flow-break-outside", rel, name, "%s outside a LOOP" % o, "word %d" % op.pos)
            elif o == "WHILE":
                if not stack or stack[-1][0] != "loop":
                    self.add("flow-unbalanced", rel, name, "END_LOOP without its LOOP", "word %d" % op.pos)
                    self.pop_to(stack, "loop")
                else:
                    stack.pop()
            elif o in ("INLINE_EVENT", "INLINE_EVENT_ID", "BROTHER_EVENT", "BROTHER_EVENT_ID"):
                stack.append(["inline" if o.startswith("INLINE") else "brother", False, op])
                inline_starts.append(i)
            elif o in ("END_INLINE", "END_BROTHER"):
                k = "inline" if o == "END_INLINE" else "brother"
                if not stack or stack[-1][0] != k:
                    self.add("flow-unbalanced", rel, name, "%s with %s open" % (o, stack[-1][0] if stack else
                                                                                "nothing"), "word %d" % op.pos)
                    self.pop_to(stack, k)
                else:
                    stack.pop()
            elif o == "LABEL":
                labels.append((i, op.args[0] if op.args else None))
            elif o == "GOTO":
                gotos.append((i, op.args[0] if op.args else None, [s[0] for s in stack]))
        if ended and stack:
            self.add("flow-unbalanced", rel, name, "END_SCRIPT with %s still open (from word %d)" %
                     (stack[-1][0], stack[-1][2].pos), "")
        if len(labels) > 16:
            self.add("flow-labels-over-16", rel, name, "%d labels; the jump table holds 16" % len(labels))
        for gi, lab, ctx in gotos:
            if not isinstance(lab, int):
                continue
            if decode_var(lab) is not None:
                continue
            ids = [l for (_, l) in labels]
            if lab not in ids:
                self.add("flow-goto-missing", rel, name, "GOTO %d: no LABEL %d" % (lab, lab),
                         "word %d" % ops[gi].pos)
            elif len(labels) > 16:
                first16 = [l for (_, l) in labels[:16]]
                if lab not in first16:
                    self.add("flow-goto-missing", rel, name, "GOTO %d: LABEL %d is past the 16-label table" %
                             (lab, lab), "word %d" % ops[gi].pos)
            if ("inline" in ctx or "brother" in ctx) and lab in ids:
                # an inline block's own jump table starts at the block: labels before it are not seen
                start = max(si for si in inline_starts if si < gi) if any(si < gi for si in inline_starts) else 0
                if not any(li > start and l == lab for li, l in labels):
                    self.add("flow-goto-missing", rel, name, "GOTO %d inside an inline/sibling block: LABEL %d "
                             "is before the block, outside its jump table" % (lab, lab), "word %d" % ops[gi].pos)
        # variables, calls, references
        for op, lw_strings, lw_ptr in self.track(rel, ops):
            self.check_vars(rel, name, op)
            if op.op in ("RUN_EVENT", "RUN_EVENT_ID", "RUN_CHILD_EVENT", "RESTART_EVENT") and op.args:
                a = op.args[0]
                if isinstance(a, ir.Ref):
                    node = self.resolve(rel, a)
                    if node is None:
                        self.add("ref-script", rel, name, "%s of %s: not defined in any unit" % (op.op, a.name),
                                 "word %d" % op.pos, a.name)
                    elif node not in self.scripts:
                        self.add("ref-script", rel, name, "%s of %s: not a script" % (op.op, a.name),
                                 "word %d" % op.pos, a.name)
                elif isinstance(a, int) and decode_var(a) is None and a != 0:
                    self.add("ref-script", rel, name, "%s of the number %d" % (op.op, a), "word %d" % op.pos)
            if op.op == "USER_FUNC":
                self.check_call(rel, name, op, mps, lw_strings, lw_ptr)

    def pop_to(self, stack, kind):
        while stack and stack[-1][0] != kind:
            stack.pop()
        if stack:
            stack.pop()

    def check_vars(self, rel, name, op):
        raw = RAW_ARGS.get(op.op, set())
        if op.op == "GOTO":
            raw = {0}
        for i, a in enumerate(op.args):
            if op.op == "USER_FUNC" and i == 0:
                continue
            if i in raw:
                continue
            dv = decode_var(a)
            if not dv or dv[1] is None:
                continue
            kind, idx = dv
            cnt = dict((v[0], v[3]) for v in VARS)[kind]
            if cnt is not None and not (0 <= idx < cnt):
                self.add("var-range", rel, name, "%s %s(%d): the index is outside 0..%d" %
                         (op.op if op.op != "USER_FUNC" else self.fname(op), kind, idx, cnt - 1),
                         "word %d" % op.pos)
        # GSW byte values
        if op.op in ("SET", "IF_EQUAL", "IF_NOT_EQUAL", "IF_LESS", "IF_GREATER", "IF_LESS_EQUAL",
                     "IF_GREATER_EQUAL", "ADD") and len(op.args) == 2:
            dv = decode_var(op.args[0])
            v = op.args[1]
            if dv and dv[0] == "GSW" and dv[1] != 0 and isinstance(v, int) and decode_var(v) is None:
                if not (0 <= v <= 255) and not (op.op == "ADD" and -255 <= v <= 255):
                    self.add("var-gsw-byte", rel, name, "%s GSW(%d) with %d: GSW(1..) hold one byte (0..255)" %
                             (op.op, dv[1], v), "word %d" % op.pos)

    def fname(self, op):
        a = op.args[0] if op.args else None
        return a.name if isinstance(a, ir.Ref) else "?"

    def check_call(self, rel, name, op, mps, lw_strings, lw_ptr):
        if not op.args or not isinstance(op.args[0], ir.Ref):
            self.add("call-not-a-func", rel, name, "USER_FUNC with %r as its function" %
                     (op.args[0] if op.args else None), "word %d" % op.pos)
            return
        fn = op.args[0].name
        self.stats["calls"] += 1
        where = "word %d" % op.pos
        if (rel, fn) in self.w.local and self.w.local[(rel, fn)][0] != "other" or \
                (fn in self.w.globals and fn not in self.w.func_def):
            self.add("call-not-a-func", rel, name, "USER_FUNC of data %s" % fn, where, fn)
            return
        info = csrc.pick(self.fs.get(fn), rel)
        given = len(op.args) - 1
        if info is None:
            self.add("call-unknown-func", rel, name, "%s: no C definition found to check" % fn, where, fn)
            return
        if info["min"] > given:
            self.add("call-too-few-args", rel, name, "%s(%d args): reads %d (%s:%d)" %
                     (fn, given, info["min"], info["path"], info["line"]), where, fn)
        elif info["max_used"] > given and not info["variadic"]:
            self.add("call-maybe-too-few-args", rel, name, "%s(%d args): may read %d (%s:%d)" %
                     (fn, given, info["max_used"], info["path"], info["line"]), where, fn)
        elif given > info["max"] and not info["variadic"] and info["max"] > 0:
            self.add("call-extra-args", rel, name, "%s(%d args): reads %d" % (fn, given, info["max"]), where, fn)
        for i, a in enumerate(op.args[1:]):
            ai = info["args"].get(i)
            if not ai:
                continue
            modes, ctypes, kinds = ai["modes"], ai["ctypes"], ai["kinds"]
            isvar = decode_var(a) is not None
            isptr = isinstance(a, ir.Ref) or (isvar and lw_ptr.get(a))
            if "out" in modes and not (modes - {"out", "raw"}) and not isvar and not (isinstance(a, int) and
                                                                                      a == 0):
                self.add("call-output-not-var", rel, name, "%s arg %d is an output; got %s" %
                         (fn, i, self.show(rel, a)), where, fn)
            if isptr and ctypes == {"int"} and modes & {"value"} and not kinds:
                self.add("call-ptr-into-s32", rel, name, "%s arg %d: a pointer (%s) read into an s32" %
                         (fn, i, self.show(rel, a)), where, fn)
            if isinstance(a, int) and not isvar and a not in (0, -1, EVT_NULLPTR, 1) and ctypes == {"ptr"} \
                    and modes == {"value"}:
                self.add("call-int-into-ptr", rel, name, "%s arg %d: the number %d read as a pointer" %
                         (fn, i, s32(a)), where, fn)
            cands = self.cands(rel, a, lw_strings)
            if "msg" in kinds and fn.startswith("evt_msg") and op.args[1:] and isinstance(op.args[1], int) \
                    and decode_var(op.args[1]) is None and op.args[1] & 1 and i > 0:
                # MSG_PRINT_DIRECT: the text itself, not a tag
                if self.facts.origin(rel, name, None) == "port" or rel.startswith(("port/", "examples/")) or \
                        any(self.facts.origin(rel, name, '"%s' % c[:12]) == "port" for c in cands):
                    for c in cands:
                        self.add("msg-port-direct-text", rel, name, "%s shows text with no tag (one language "
                                 "only): '%s'" % (fn, c.split("\n")[0][:40]), where, None)
                continue
            if "map" in kinds and fn == "evt_seq_set_seq" and op.args[1:2] != [self.consts.get("SEQ_MAP_CHANGE", 2)]:
                continue
            if "item" in kinds and isinstance(a, int) and not isvar:
                v = s32(a)
                if v not in (0, -1) and not (1 <= v < self.consts.get("BADGE_MAX", 339)):
                    self.add("ref-item", rel, name, "%s arg %d: item id %d out of range" % (fn, i, v), where, fn)
            for s in cands:
                self.check_name(rel, name, fn, i, s, kinds, mps, where, given, op)

    def show(self, rel, a):
        s = self.w.string(rel, a)
        if s is not None:
            return '"%s"' % s
        if isinstance(a, ir.Ref):
            return a.name
        dv = decode_var(a)
        if dv:
            return "%s(%s)" % dv
        return str(s32(a)) if isinstance(a, int) else repr(a)

    def check_name(self, rel, script, fn, i, s, kinds, mps, where, given, op):
        subj = '"%s"' % s
        fmaps = [m for m in mps if not m.startswith("btl:")]
        if "msg" in kinds:
            self.check_msg(rel, script, fn, s, mps, where)
        if "map" in kinds and s:
            if s not in self.map_names:
                self.add("ref-map", rel, script, "%s: map '%s' is not in the map list" % (fn, s), where, subj)
            elif "bero" not in kinds:
                # the next argument is the entry name in evt_bero_mapchange and its kin
                info = csrc.pick(self.fs.get(fn), rel)
                if info and i + 1 < given and "bero" in info["args"].get(i + 1, {}).get("kinds", ()):
                    be = self.w.string(rel, op.args[i + 2])
                    if be:
                        self.check_map_bero(rel, script, where, s, be)
        if "npc" in kinds and fmaps and s:
            special = {"me", "party", "(^x^)party", "extparty", "mario", "master", "slave_0", "slave_1",
                       "slave_2", "slave_3", ""}
            known = set()
            for mp in fmaps:
                known |= self.npcs.get(mp, set())
                known |= self.disc.npcs.get(mp, set())
            if not self.disc.ok:
                known = None
            if known is not None and s not in special and s not in known and not re.search(r"_s\d$", s):
                self.add("ref-npc", rel, script, "%s: NPC '%s' is never entered in %s" % (fn, s, "/".join(
                    sorted(fmaps))), where, subj, fmaps)
        if "mobj" in kinds and fmaps and s:
            known = set()
            for mp in fmaps:
                known |= self.mobjs.get(mp, set())
            if s not in known and s != "me":
                self.add("ref-mobj", rel, script, "%s: mobj '%s' is never made in %s" % (fn, s, "/".join(
                    sorted(fmaps))), where, subj, fmaps)
        if ("model" in kinds or "hit" in kinds) and fmaps and s and self.disc.ok:
            known = set()
            have = False
            for mp in fmaps:
                j = self.disc.joints.get(mp)
                if j is not None:
                    known |= j
                    have = True
            if have and s not in known:
                self.add("ref-model" if "model" in kinds else "ref-hit", rel, script,
                         "%s: '%s' is no joint of %s's map file" % (fn, s, "/".join(sorted(fmaps))), where,
                         subj, fmaps)

    def check_msg(self, rel, script, fn, tag, mps, where):
        if not tag:
            return
        self.stats["msg-refs"] += 1
        if tag.startswith("pc_") or any(tag.startswith(p) for p in self.port_tags[1]):
            if tag not in self.port_tags[0] and not any(tag.startswith(p) for p in self.port_tags[1]):
                self.add("msg-port-missing", rel, script, "%s: Paper Cut tag '%s' handled nowhere" % (fn, tag),
                         where, '"%s"' % tag)
            return
        if not self.disc.ok:
            return
        files = set()
        for mp in mps:
            if mp.startswith("btl:"):
                area = mp[4:]
                files |= {f for f in self.disc.msg.get("US", {}) if f.startswith(area + "_")}
            else:
                files.add(mp[:6])
        if not mps:
            stem = os.path.splitext(os.path.basename(rel))[0]
            if re.fullmatch(r"[a-z]{3}_\d\d", stem):
                files.add(stem)
            else:
                area = rel.split("/")[1] if rel.startswith("rels/") else None
                if area:
                    files |= {f for f in self.disc.msg.get("US", {}) if f.startswith(area + "_")}
        m = re.match(r"lecture_friends_(\d\d)", tag)
        if m:
            # msgdrv.c portMsgOtherFile: a partner's joining text comes from its partner's own map file
            n = int(m.group(1))
            files.add(next(f for last, f in ((3, "gor_01"), (6, "hei_01"), (8, "win_04"), (11, "tou_10"),
                                             (14, "usu_00"), (16, "muj_05"), (99, "gor_01")) if n <= last))
        missing = []
        for lang, _, _ in DiscData.LANGS:
            tbl = self.disc.msg.get(lang)
            if tbl is None:
                continue
            if tag in tbl.get("global", ()):
                continue
            if any(tag in tbl.get(f, ()) for f in files):
                continue
            missing.append(lang)
        if not missing:
            return
        langs = [l for l, _, _ in DiscData.LANGS if l in self.disc.msg]
        scope = "/".join(sorted(mps)) if mps else "(no map reaches it)"
        if len(missing) == len(langs):
            elsewhere = sorted(f for f, tags in self.disc.msg.get("US", {}).items() if tag in tags)
            note = "; only in %s.txt" % "/".join(elsewhere[:3]) if elsewhere else "; in no file at all"
            self.add("msg-missing-all", rel, script, "%s: tag '%s' not loaded with %s%s" % (fn, tag, scope, note),
                     where, '"%s"' % tag, mps)
        else:
            self.add("msg-missing-some", rel, script, "%s: tag '%s' missing in %s (%s)" %
                     (fn, tag, ",".join(missing), scope), where, '"%s"' % tag, mps)


# ---------------------------------------------------------------- port text, constants
def port_tag_sets():
    lits, prefixes = set(), set()
    for d in ("port/src", "port/archipelago", "port/mods"):
        for dp, dn, fn in os.walk(os.path.join(ROOT, d)):
            for n in fn:
                if not n.endswith((".c", ".cpp", ".h")):
                    continue
                try:
                    t = open(os.path.join(dp, n), encoding="utf-8", errors="replace").read()
                except OSError:
                    continue
                for m in re.finditer(r'"(pc_\w+)"', t):
                    lits.add(m.group(1))
                for m in re.finditer(r'strncmp\(\s*\w+\s*,\s*"(\w+)"\s*,\s*\d+\s*\)', t):
                    if m.group(1).startswith("pc_"):
                        prefixes.add(m.group(1))
    return lits, prefixes


def evt_consts():
    out = {}
    p = os.path.join(ROOT, "port", "mods", "evt_table_gen.h")
    try:
        for m in re.finditer(r'\{"(\w+)", (-?\d+)LL\}', open(p, encoding="utf-8").read()):
            out[m.group(1)] = int(m.group(2))
    except OSError:
        pass
    return out


def check_rando_targets(lint):
    """The randomizer's script patches (port/archipelago/ttyd_ap_data.h, TTYD_AP_SCRIPT): each writes an
    item id into one word of a game script, at the PC offset gc_offsets.py translated from the apworld's
    GameCube offset. As TTYD_AP_VERIFY does at runtime: the PC word must hold what the disc's REL holds at
    the GameCube offset (else the patch lands on another word: an opcode, a function, a flag)."""
    import struct
    p = os.path.join(ROOT, "port", "archipelago", "ttyd_ap_data.h")
    if not os.path.exists(p):
        return
    text = open(p, encoding="utf-8").read()
    rel = "port/archipelago/ttyd_ap_data.h"
    tstart = text.find("kTtydApTargets")
    locs = {}
    lt = text[text.find("kTtydApLocations"):]
    for m in re.finditer(r'\{(\d+), "([^"]*)", \d+, \d+, \d+, (\d+), (\d+), (\d+), (-?\d+)\}', lt):
        for k in range(int(m.group(4)), int(m.group(4)) + int(m.group(5))):
            locs[k] = m.group(2)
    by_file = collections.defaultdict(list)
    for (unit, name) in lint.scripts:
        by_file[(unit.split("/")[1] if unit.startswith("rels/") else "", os.path.splitext(
            os.path.basename(unit))[0], name)].append(unit)
    rels = {}
    tgt_re = re.compile(r'\{"(\w+)::(\w+)", "(\w*)", (0x[0-9A-Fa-f]+|\d+), (TTYD_AP_\w+), \d+, (\d+), '
                        r'(0x[0-9A-Fa-f]+|\d+)\}')
    for k, m in enumerate(tgt_re.finditer(text, tstart)):
        if m.group(5) != "TTYD_AP_SCRIPT":
            continue
        f, name, area, off, price, gco = (m.group(1), m.group(2), m.group(3), int(m.group(4), 0),
                                          int(m.group(6)), int(m.group(7), 0))
        lint.stats["rando-targets"] += 1
        units = by_file.get((area, f, name)) or [u for (a2, f2, n2), us in by_file.items() if f2 == f and n2 == name
                                                 for u in us]
        what = "%s::%s+0x%X" % (f, name, off)
        loc = locs.get(k, "?")
        if units:
            words = lint.scripts[(units[0], name)]
        else:
            # an EvtWord table read by a script (jon's treasure_tbl), not a script itself
            units = [u for (u, n), (kind, v) in lint.w.local.items() if n == name and kind == "words" and
                     os.path.splitext(os.path.basename(u))[0] == f]
            if not units:
                lint.add("rando-target", rel, what, "%s: no script or table %s in %s" % (loc, name, f))
                continue
            words = lint.w.local[(units[0], name)][1]
        idx, rem = divmod(off, 8)
        if rem or idx >= len(words):
            lint.add("rando-target", rel, what, "%s: offset outside the script (%d words)" % (loc, len(words)))
            continue
        ops, _, _ = decode(words)
        where_ = "word %d" % idx
        for op in ops:
            if op.pos == idx:
                where_ = "the %s opcode" % op.op
                break
            if op.pos < idx <= op.pos + len(op.args):
                ai = idx - op.pos - 1
                where_ = "%s %s" % (lint.fname(op) if op.op == "USER_FUNC" else op.op,
                                    "argument %d" % (ai - 1) if op.op == "USER_FUNC" else "word %d" % ai)
                if op.op == "USER_FUNC" and ai == 0:
                    where_ = "%s's function pointer" % lint.fname(op)
                break
        have = words[idx]
        if area not in rels:
            rp = os.path.join(ROOT, "orig", "G8ME01", "files", "rel", area + ".rel")
            rels[area] = open(rp, "rb").read() if os.path.exists(rp) else None
        gc = rels[area]
        if gc is None or gco + 4 > len(gc):
            continue
        want = struct.unpack_from(">i", gc, gco)[0]
        if isinstance(have, int) and s32(have) == want:
            continue
        near = [j for j in range(max(0, idx - 24), min(len(words), idx + 25))
                if isinstance(words[j], int) and s32(words[j]) == want]
        hint = (" (the disc's word, %d, is at +0x%X)" % (want, min(near, key=lambda j: abs(j - idx)) * 8)) \
            if near else " (the disc has %d there)" % want
        lint.add("rando-target", rel, what, "%s: writes over %s = %s%s" % (loc, where_, lint.show(units[0], have),
                                                                         hint))


# ---------------------------------------------------------------- report
def write_report(path, lint, errs, elapsed, disc_ok):
    f = lint.findings
    by = collections.Counter((x.cat, x.origin) for x in f)
    cats = sorted({x.cat for x in f}, key=lambda c: (["error", "warn", "info"].index(SEVERITY.get(c, "info")), c))
    L = []
    L.append("# evt_lint report")
    L.append("")
    L.append("Generated by `tools/port/evt_lint.py` (%.0f s). %d scripts (%d ops, %d calls) in %d units; "
             "%d message references. Disc data: %s." % (elapsed, lint.stats["scripts"], lint.stats["ops"],
                                                       lint.stats["calls"], len(lint.w.units),
                                                       lint.stats["msg-refs"], "yes" if disc_ok else "no"))
    L.append("%d mod scripts, %d randomizer script targets checked." % (lint.stats["mod-scripts"],
                                                                      lint.stats["rando-targets"]))
    notes = os.path.join(os.path.dirname(path), "EVT_LINT_NOTES.md")
    if os.path.exists(notes):  # the reviewer's reading of the findings, kept beside the report
        L.append("")
        L.append(open(notes, encoding="utf-8").read().rstrip())
        L.append("")
        L.append("# Every finding")
    if errs:
        L.append("")
        L.append("Units that failed to compile to IR (not checked): " + ", ".join(sorted(errs)))
    L.append("")
    L.append("| category | severity | faithful | port |")
    L.append("|---|---|---|---|")
    for c in cats:
        L.append("| %s | %s | %d | %d |" % (c, SEVERITY.get(c, "info"), by[(c, "faithful")], by[(c, "port")]))
    L.append("| **total** | | %d | %d |" % (sum(v for (c, o), v in by.items() if o == "faithful"),
                                            sum(v for (c, o), v in by.items() if o == "port")))
    for origin in ("port", "faithful"):
        L.append("")
        L.append("## %s findings" % ("Port-side" if origin == "port" else "Faithful (as the original game ships)"))
        for c in cats:
            items = [x for x in f if x.cat == c and x.origin == origin]
            if not items:
                continue
            L.append("")
            L.append("### %s (%s, %d)" % (c, SEVERITY.get(c, "info"), len(items)))
            L.append("")
            for x in sorted(items, key=lambda x: (x.path, x.script, x.where)):
                L.append("- `%s` %s %s: %s" % (x.path, x.script, ("(" + x.where + ")") if x.where else "",
                                                x.detail))
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(L) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", default=os.path.join(ROOT, "build-pc-us-aurora"))
    ap.add_argument("-j", "--jobs", type=int, default=4)
    ap.add_argument("--report", default=os.path.join(ROOT, "local-docs", "EVT_LINT.md"))
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--update-baseline", action="store_true")
    ap.add_argument("--no-disc", action="store_true")
    ap.add_argument("--json", help="write every finding as JSON here")
    a = ap.parse_args()
    t0 = time.time()
    if not os.path.exists(os.path.join(a.build, "compile_commands.json")):
        print("evt_lint: no compile_commands.json in %s (configure the US Aurora build first)" % a.build)
        return 2
    print("evt_lint: compiling scripts to IR...", file=sys.stderr)
    units, errs = collect_ir(a.build, a.jobs)
    print("evt_lint: reading the C sources...", file=sys.stderr)
    funcs = csrc.scan_tree(ROOT)
    summary = csrc.summarize(funcs)
    facts = SourceFacts()
    for rel in units:
        facts.load(rel)
    for dp, dn, fn in os.walk(os.path.join(ROOT, "rels")):
        for n in fn:
            if n.endswith((".inc", ".h")):
                facts.load(os.path.relpath(os.path.join(dp, n), ROOT).replace("\\", "/"))
    disc = DiscData(os.path.join(a.build, "evt_lint"))
    if not a.no_disc:
        print("evt_lint: reading message files and maps from the discs...", file=sys.stderr)
        disc.load()
    world = World(units)
    lint = Linter(world, summary, facts, disc, evt_consts(), port_tag_sets())
    lint.find_scripts()
    lint.walk_maps()
    lint.collect_map_names()
    lint.run()
    import evt_lint_mods
    evt_lint_mods.lint_mods(lint, ROOT)
    check_rando_targets(lint)
    elapsed = time.time() - t0
    os.makedirs(os.path.dirname(a.report), exist_ok=True)
    write_report(a.report, lint, errs, elapsed, disc.ok)
    if a.json:
        with open(a.json, "w", encoding="utf-8") as fh:
            json.dump([{"cat": x.cat, "origin": x.origin, "path": x.path, "script": x.script, "detail": x.detail,
                        "where": x.where, "maps": x.maps} for x in lint.findings], fh, indent=1)
    keys = collections.Counter(x.key() for x in lint.findings)
    print("evt_lint: %d scripts, %d findings (%d port-side); report %s" % (
        lint.stats["scripts"], len(lint.findings), sum(1 for x in lint.findings if x.origin == "port"),
        os.path.relpath(a.report, ROOT)))
    if a.update_baseline:
        if not disc.ok:
            print("evt_lint: --update-baseline needs the discs (orig/<id>/*.iso)")
            return 2
        with open(BASELINE, "w", encoding="utf-8", newline="\n") as fh:
            fh.write("# evt_lint baseline: known findings (tools/port/evt_lint.py --update-baseline)\n")
            for k in sorted(keys.elements()):
                fh.write(k + "\n")
        print("evt_lint: baseline written (%d)" % sum(keys.values()))
    if a.check:
        base = collections.Counter()
        if os.path.exists(BASELINE):
            for ln in open(BASELINE, encoding="utf-8"):
                ln = ln.rstrip("\n")
                if ln and not ln.startswith("#"):
                    base[ln] += 1
        if not disc.ok:
            base = collections.Counter({k: v for k, v in base.items() if k.split("\t")[0] not in NEEDS_DISC})
        new = keys - base
        gone = base - keys
        for k in sorted(new.elements()):
            print("NEW  " + k.replace("\t", "  "))
        if gone:
            print("evt_lint: %d baseline findings are gone (run --update-baseline)" % sum(gone.values()))
        if errs:
            for k, v in sorted(errs.items()):
                print("UNIT FAILED %s: %s" % (k, v.strip().splitlines()[-1] if v.strip() else "?"))
        if new or errs:
            print("evt_lint: %d new findings" % sum(new.values()))
            return 1
        print("evt_lint: no new findings")
    return 0


if __name__ == "__main__":
    sys.exit(main())
