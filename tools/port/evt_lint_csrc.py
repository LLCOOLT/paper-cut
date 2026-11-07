#!/usr/bin/env python3
"""The C side of evt_lint.py: what every script function (USER_FUNC) reads.

Scans the C sources (src/, rels/, port/) for function definitions. For each
USER_FUNC it follows the reads of its argument words (args[n], *args++,
event->args[n], ...), the C type each read lands in (pointer, word, int,
float), whether it is an output (evtSetValue on it) and where the value goes:
a message tag (msgSearch), an NPC name (evtNpcNameToPtr), a map joint, a hit
object, a map name, an item id, ... Those sinks are followed through plain
helper functions (a parameter passed on to another function's parameter), so
evt_msg_print -> _evt_msg_print -> msgWindow_Entry makes its second argument
a message tag.

This is a text scan, not a compiler: it is deliberately simple and errs on
the side of "unknown" (no check) rather than a wrong check.
"""

import os
import warnings
import re

warnings.filterwarnings("ignore", category=SyntaxWarning)

# (callee, argument position) -> kind of name/value it takes
BASE_SINKS = {
    ("msgSearch", 0): "msg",
    ("msgWindow_Entry", 0): "msg",
    ("msgWindow_Add", 0): "msg",
    ("npcNameToPtr", 0): "npc",
    ("evtNpcNameToPtr", 1): "npc",
    ("npcEntry", 0): "npc_def",
    ("mobjNameToPtr", 0): "mobj",
    ("mobjEntry", 0): "mobj_def",
    ("hitNameToPtr", 0): "hit",
    ("mapDataPtr", 0): "map",
    ("seqSetSeq", 1): "map",
    ("seqSetSeq", 2): "bero",
    ("itemEntry", 4): "item",
    ("pouchGetItem", 0): "item",
    ("pouchRemoveItem", 0): "item",
    ("pouchCheckItem", 0): "item",
    ("evtEntry", 0): "script",
    ("evtEntryType", 0): "script",
    ("evtChildEntry", 1): "script",
    ("evtBrotherEntry", 1): "script",
}
# name prefixes whose first argument is a map joint / hit object name
PREFIX_SINKS = [
    (re.compile(r"^map(Obj|Grp)[A-Z]\w*$"), 0, "model"),
    (re.compile(r"^hit(Obj|Grp)[A-Z]\w*$"), 0, "hit"),
]
# callee names never followed (they take names for other reasons or are generic)
NO_FOLLOW = {"strcmp", "strcpy", "strcat", "sprintf", "strncpy", "strlen", "memcpy", "memset", "OSReport",
             "OSPanic", "printf", "snprintf", "strncmp", "sscanf", "evtGetValue", "evtSetValue", "evtGetFloat",
             "evtSetFloat", "portTrace", "__assert", "assert"}

INT_TYPES = {"s8", "s16", "s32", "u8", "u16", "u32", "int", "BOOL", "short", "char", "unsigned", "signed", "long",
             "bool", "s64", "u64"}
WORD_TYPES = {"EvtWord", "intptr_t", "uintptr_t", "size_t", "ptrdiff_t"}
FLOAT_TYPES = {"f32", "f64", "float", "double"}

FUNC_RE = re.compile(
    r"(?:^|[;}\n])\s*((?:static\s+|inline\s+|extern\s+)*)"
    r"(?:USER_FUNC\s*\(\s*(?:PORT_HOOKABLE\s*\(\s*)?(\w+)\s*\)?\s*\)"
    r"|[A-Za-z_][\w\s\*]*?[\s\*]\(?\s*(?:PORT_HOOKABLE\s*\(\s*)?(\w+)\s*\)?\s*\(([^;{}()]*(?:\([^()]*\)[^;{}()]*)*)\))"
    r"\s*\{")


def strip_comments(text):
    """Comments out, strings and line structure kept."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            if j < 0:
                j = n
            i = j
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            if j < 0:
                j = n - 2
            out.append(re.sub(r"[^\n]", " ", text[i:j + 2]))
            i = j + 2
        elif c == '"' or c == "'":
            j = i + 1
            while j < n and text[j] != c:
                if text[j] == "\\":
                    j += 1
                j += 1
            out.append(text[i:j + 1])
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


# the US PC Aurora build's configuration, for #if lines
PP_DEFINES = {"TARGET_PC": 1, "PORT_MODS": 1, "TTYD_BACKEND_AURORA": 1, "NONMATCHING": 1, "VERSION_G8ME01": 1,
              "VERSION_DEMO_US": 0, "VERSION_DEMO_JP": 1, "VERSION_JP": 2, "VERSION_US": 3, "VERSION_DEMO_US2": 4,
              "VERSION_DEMO_EU": 5, "VERSION_EU": 6, "VERSION": 3, "PORT_RELS_STATIC": 1, "__cplusplus": 0,
              "_WIN32": 1, "NDEBUG": 1, "PORT_ONLINE": 1}


def pp_eval(cond):
    """Value of an #if condition under PP_DEFINES (unknown names are 0, as in C); None if unreadable."""
    c = re.sub(r"\bdefined\s*\(\s*(\w+)\s*\)|\bdefined\s+(\w+)",
               lambda m: "1" if (m.group(1) or m.group(2)) in PP_DEFINES else "0", cond)
    c = re.sub(r"\b([A-Za-z_]\w*)\b", lambda m: str(PP_DEFINES.get(m.group(1), 0)), c)
    c = c.replace("&&", " and ").replace("||", " or ")
    c = re.sub(r"!(?!=)", " not ", c)
    c = re.sub(r"(\d+)[uUlL]+\b", r"\1", c)
    try:
        return bool(eval(c, {"__builtins__": {}}, {}))
    except Exception:
        return None


def drop_preproc(text):
    """Preprocessor lines blanked, and the lines of #if branches the US PC build doesn't compile.
    A condition it can't read keeps its first branch."""
    lines = text.split("\n")
    out = []
    cont = False
    stack = []  # [this branch live, some branch taken, parent live]
    live = True
    for ln in lines:
        s = ln.lstrip()
        if cont:
            cont = ln.rstrip().endswith("\\")
            out.append("")
            continue
        if s.startswith("#"):
            cont = ln.rstrip().endswith("\\")
            m = re.match(r"#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)", s)
            if m:
                d, cond = m.group(1), m.group(2).split("//")[0].split("/*")[0].strip()
                if d in ("if", "ifdef", "ifndef"):
                    if d == "ifdef":
                        v = cond.split()[0] in PP_DEFINES if cond else False
                    elif d == "ifndef":
                        v = cond.split()[0] not in PP_DEFINES if cond else True
                    else:
                        v = pp_eval(cond)
                        if v is None:
                            v = True
                    stack.append([v, v, live])
                    live = live and v
                elif d == "elif" and stack:
                    top = stack[-1]
                    v = False if top[1] else pp_eval(cond)
                    if v is None:
                        v = False
                    top[0] = v
                    top[1] = top[1] or v
                    live = top[2] and v
                elif d == "else" and stack:
                    top = stack[-1]
                    top[0] = not top[1]
                    top[1] = True
                    live = top[2] and top[0]
                elif d == "endif" and stack:
                    live = stack.pop()[2]
            out.append("")
            continue
        out.append(ln if live else "")
    return "\n".join(out)


def match_brace(text, i):
    """Index of the } matching the { at i (strings skipped)."""
    depth = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            j = i + 1
            while j < n and text[j] != c:
                if text[j] == "\\":
                    j += 1
                j += 1
            i = j
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return n - 1


def split_top(s, sep=","):
    parts, depth, cur = [], 0, []
    i = 0
    while i < len(s):
        c = s[i]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif c == '"':
            j = i + 1
            while j < len(s) and s[j] != '"':
                if s[j] == "\\":
                    j += 1
                j += 1
            cur.append(s[i:j + 1])
            i = j + 1
            continue
        if c == sep and depth == 0:
            parts.append("".join(cur))
            cur = []
        else:
            cur.append(c)
        i += 1
    if cur or parts:
        parts.append("".join(cur))
    return parts


CAST_RE = re.compile(r"^\(\s*(?:const\s+)?(?:struct\s+)?[A-Za-z_]\w*(?:\s+const)?\s*\**\s*\)\s*")


def strip_casts(e):
    e = e.strip()
    while True:
        m = CAST_RE.match(e)
        if m and len(e) > m.end():
            e = e[m.end():].strip()
            continue
        if e.startswith("(") and e.endswith(")") and match_paren(e, 0) == len(e) - 1:
            e = e[1:-1].strip()
            continue
        return e


def cast_kind(e):
    """'ptr' / 'int' / 'word' / 'float' for an explicit outer cast, else None."""
    m = CAST_RE.match(e.strip())
    if not m:
        return None
    c = m.group(0)
    if "*" in c:
        return "ptr"
    w = re.findall(r"\w+", c)
    if any(x in WORD_TYPES for x in w):
        return "word"
    if any(x in FLOAT_TYPES for x in w):
        return "float"
    if any(x in INT_TYPES for x in w):
        return "int"
    return None


def match_paren(s, i):
    depth = 0
    for j in range(i, len(s)):
        if s[j] == "(":
            depth += 1
        elif s[j] == ")":
            depth -= 1
            if depth == 0:
                return j
    return -1


CALL_RE = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
KEYWORDS = {"if", "for", "while", "switch", "return", "sizeof", "do", "else", "case", "USER_FUNC", "PORT_HOOKABLE",
            "defined"}


def calls_in(text):
    """(callee, [arg exprs], start) for each call in text."""
    out = []
    for m in CALL_RE.finditer(text):
        name = m.group(1)
        if name in KEYWORDS:
            continue
        o = m.end() - 1
        c = match_paren(text, o)
        if c < 0:
            continue
        out.append((name, split_top(text[o + 1:c]), m.start(), c))
    return out


class Func:
    def __init__(self, name, path, line, params, body, user, body_off):
        self.name, self.path, self.line = name, path, line
        self.params = params  # [(name, type kind)]
        self.body = body
        self.user = user
        self.body_off = body_off
        self.reads = []  # USER_FUNC: [Read]
        self.variadic = False
        self.edges = []  # (param index, callee, callee arg pos)
        self.param_kinds = [set() for _ in params]


class Read:
    __slots__ = ("idx", "mode", "ctype", "sinks", "cond", "var")

    def __init__(self, idx):
        self.idx = idx
        self.mode = "raw"  # value / float / out / raw
        self.ctype = None  # ptr / int / word / float
        self.sinks = set()  # (callee, pos)
        self.cond = False
        self.var = None


DECL_START = re.compile(r"^\s*(?:const\s+|static\s+|volatile\s+|unsigned\s+|signed\s+|struct\s+)*([A-Za-z_]\w*)\b")


def var_types(body):
    """var name -> 'ptr'/'int'/'word'/'float'/'other' from the declarations in body."""
    types = {}
    for stmt in re.split(r"[;{}]", body):
        s = stmt.strip()
        m = DECL_START.match(s)
        if not m:
            continue
        base = m.group(1)
        if base in KEYWORDS or base in ("return", "goto", "break", "continue"):
            continue
        rest = s[m.end():]
        # a declaration: base then declarators; a call or assignment is not
        if not re.match(r"^\s*(?:const\s+)?\**\s*[A-Za-z_]\w*\s*(?:\[[^\]]*\]\s*)*(?:=|,|$)", rest):
            continue
        if base in INT_TYPES:
            k = "int"
        elif base in WORD_TYPES:
            k = "word"
        elif base in FLOAT_TYPES:
            k = "float"
        else:
            k = TYPEDEFS.get(base, "other")
        for d in split_top(rest):
            dm = re.match(r"^\s*(?:const\s+)?(\**)\s*([A-Za-z_]\w*)\s*(\[)?", d)
            if not dm:
                continue
            if dm.group(1) or dm.group(3):
                types[dm.group(2)] = "ptr"
            elif base == "void":
                continue
            else:
                types.setdefault(dm.group(2), k)
    return types


def param_list(sig):
    out = []
    if sig.strip() in ("", "void"):
        return out
    for p in split_top(sig):
        p = p.strip()
        if p == "...":
            out.append(("...", None))
            continue
        m = re.match(r"^(.*?)([A-Za-z_]\w*)\s*(\[[^\]]*\])?$", p)
        if not m:
            out.append(("?", None))
            continue
        t = m.group(1)
        if "*" in t or m.group(3):
            k = "ptr"
        else:
            w = re.findall(r"\w+", t)
            k = "word" if any(x in WORD_TYPES for x in w) else "float" if any(x in FLOAT_TYPES for x in w) \
                else "int" if any(x in INT_TYPES for x in w) else "other"
        out.append((m.group(2), k))
    return out


def block_spans(body):
    """[(start, end, kind)] of each {...} block: kind loop / cond / first (if (isFirstCall)) / plain."""
    spans = []
    for m in re.finditer(r"\{", body):
        i = m.start()
        j = match_brace(body, i)
        pre = body[max(0, i - 200):i].rstrip()
        kind = "plain"
        if pre.endswith(")"):
            # find the opening paren of the trailing (...)
            depth = 0
            k = len(pre) - 1
            while k >= 0:
                if pre[k] == ")":
                    depth += 1
                elif pre[k] == "(":
                    depth -= 1
                    if depth == 0:
                        break
                k -= 1
            head = pre[:k].rstrip()
            cond = pre[k:]
            if re.search(r"\b(for|while)$", head):
                kind = "loop"
            elif re.search(r"\bif$", head):
                kind = "first" if re.fullmatch(r"\(\s*isFirstCall\s*\)", cond.strip()) else "cond"
            elif re.search(r"\bswitch$", head):
                kind = "cond"
        elif re.search(r"\b(else|do)$", pre):
            kind = "loop" if pre.endswith("do") else "cond"
        elif re.search(r"(case\s+[^:]+|default)\s*:$", pre):
            kind = "cond"
        spans.append((i, j, kind))
    return spans


def in_kind(spans, pos, kind):
    return any(a < pos < b and k == kind for a, b, k in spans)


def statement_at(body, pos):
    a = max(body.rfind(";", 0, pos), body.rfind("{", 0, pos), body.rfind("}", 0, pos)) + 1
    b = body.find(";", pos)
    if b < 0:
        b = len(body)
    return a, b


def analyze_user(f):
    body = f.body
    ev = f.params[0][0] if f.params else "event"
    ev_re = re.escape(ev)
    aliases = set(re.findall(r"\b([A-Za-z_]\w*)\s*=\s*" + ev_re + r"\s*->\s*args\b(?!\s*\[)", body))
    for _ in range(3):  # copies of the pointer (intArg = args; floatArg = args): each its own cursor
        for a in list(aliases):
            aliases |= set(re.findall(r"\b([A-Za-z_]\w*)\s*=\s*" + re.escape(a) + r"\s*;", body))
    spans = block_spans(body)
    reads = []
    pats = [(re.compile(ev_re + r"\s*->\s*args\s*\[\s*(\d+)\s*\]"), "evidx"),
            (re.compile(r"\*\s*" + ev_re + r"\s*->\s*args\b(?!\s*\[|\s*\+)"), "evzero"),
            (re.compile(ev_re + r"\s*->\s*args\s*\[\s*[^\]\d\s][^\]]*\]"), "varidx")]
    for a in aliases:
        ae = re.escape(a)
        pats += [(re.compile(r"\*\s*\(?\s*\b" + ae + r"\s*\+\+\s*\)?"), "postinc:" + a),
                 (re.compile(r"\b" + ae + r"\s*\[\s*(\d+)\s*\]"), "idx:" + a),
                 (re.compile(r"\b" + ae + r"\s*\[\s*[^\]\d\s][^\]]*\]"), "varidx"),
                 (re.compile(r"(?<![\*\w])\b" + ae + r"\s*\+\+"), "inc:" + a),
                 (re.compile(r"\b" + ae + r"\s*\+=\s*(\d+)"), "add:" + a),
                 (re.compile(r"\*\s*\b" + ae + r"\b(?!\s*\+\+|\s*\[|\s*=[^=])"), "deref:" + a),
                 (re.compile(r"\b" + ae + r"\s*\+\s*[A-Za-z_]"), "varidx"),
                 (re.compile(r"[,(]\s*" + ae + r"\s*[,)]"), "escape:" + a)]
    events = []
    for rx, kind in pats:
        for m in rx.finditer(body):
            events.append((m.start(), m.end(), kind, m))
    # if/else pairs: an argument pointer advanced in both branches moves once
    pairs = 0
    for (a1, b1, k1) in spans:
        for (a2, b2, k2) in spans:
            if a2 > b1 and re.fullmatch(r"\s*else\s*", body[b1 + 1:a2]):
                events += [(a1, a1, "bif:%d" % pairs, None), (b1, b1, "eif:%d" % pairs, None),
                           (a2, a2, "belse:%d" % pairs, None), (b2, b2, "eelse:%d" % pairs, None)]
                pairs += 1
    events.sort(key=lambda e: (e[0], -e[1]))
    cursor = {a: 0 for a in aliases}
    saved = {}
    taken = []
    for st, en, kind, m in events:
        if m is None:
            k, _, pid = kind.partition(":")
            if k in ("bif", "eif"):
                saved[(k, pid)] = dict(cursor)
            elif k == "belse" and ("bif", pid) in saved:
                cursor = dict(saved[("bif", pid)])
            elif k == "eelse" and ("eif", pid) in saved:
                cursor = {x: max(cursor.get(x, 0), saved[("eif", pid)].get(x, 0)) for x in cursor}
            continue
        if any(s <= st < e for s, e in taken):
            continue
        k, _, a = kind.partition(":")
        idx = None
        if k == "evidx":
            idx = int(m.group(1))
        elif k == "evzero":
            idx = 0
        elif k == "varidx":
            f.variadic = True
        elif k == "postinc":
            idx = cursor[a]
            cursor[a] += 1
            if in_kind(spans, st, "loop"):
                f.variadic = True
        elif k == "idx":
            idx = cursor[a] + int(m.group(1))
        elif k == "inc":
            cursor[a] += 1
            if in_kind(spans, st, "loop"):
                f.variadic = True
        elif k == "add":
            cursor[a] += int(m.group(1))
        elif k == "deref":
            idx = cursor[a]
        elif k == "escape":
            # the args pointer handed to another function: what that reads is unknown
            stmt = body[statement_at(body, st)[0]:en]
            if not re.search(r"=\s*" + re.escape(a) + r"\s*[,)]", stmt):
                f.variadic = True
        taken.append((st, en))
        if idx is None:
            continue
        r = Read(idx)
        r.cond = in_kind(spans, st, "cond") or in_kind(spans, st, "loop")
        classify_read(f, body, st, en, r, ev_re)
        reads.append(r)
    f.reads = reads
    if re.search(r"\b" + ev_re + r"\s*->\s*args\b\s*[,)]", body) and not aliases:
        f.variadic = True


def classify_read(f, body, st, en, r, ev_re):
    a, b = statement_at(body, st)
    stmt = body[a:b]
    rel = st - a
    before = stmt[:rel]
    types = f._types
    # evtGetValue(event, READ) / evtGetFloat / evtSetValue(event, READ, ...)
    m = re.search(r"\b(evtGetValue|evtGetFloat|evtGetNumber|evtSetValue|evtSetFloat)\s*\(\s*" + ev_re + r"\s*,\s*$",
                  before)
    if m and m.group(1).startswith("evtGet") and before[:m.start()].strip() in ("", "(void)") and \
            re.fullmatch(r"\s*\)\s*", body[en:b]):
        r.mode = "discard"  # read and thrown away: harmless past the end
        return
    if m:
        fn = m.group(1)
        if fn in ("evtSetValue", "evtSetFloat"):
            r.mode = "out"
            return
        r.mode = "float" if fn == "evtGetFloat" else "value"
        call_start = a + m.start()
        expr_start = call_start
        # the cast in front of the call, the variable it is assigned to
        pre = body[a:call_start]
        cm = re.search(r"\(\s*(?:const\s+)?(?:struct\s+)?[A-Za-z_]\w*(?:\s+const)?\s*\**\s*\)\s*$", pre)
        if cm:
            r.ctype = cast_kind(cm.group(0))
            expr_start = a + cm.start()
        vm = re.search(r"([A-Za-z_]\w*)\s*(\[[^\]]*\])?\s*=\s*$", body[a:expr_start])
        if vm and not re.search(r"[=!<>]=\s*$", body[a:expr_start]):
            r.var = vm.group(1)
            vt = types.get(r.var) if not vm.group(2) else None  # (an array element: its type is unknown here)
            if r.ctype is None or r.ctype == "word":
                r.ctype = vt or r.ctype
        if r.mode == "float":
            r.ctype = "float"
        # a direct use as a call argument: f(..., (cast)evtGetValue(event, READ), ...)
        whole = body[a:b]
        for name, args, cs, ce in calls_in(whole):
            if name in ("evtGetValue", "evtGetFloat", "evtGetNumber"):
                continue
            for pos, ex in enumerate(args):
                exs = strip_casts(ex)
                if re.match(r"^evtGet(Value|Float|Number)\s*\(", exs) and (a + cs) < st < (a + ce):
                    inner = exs[exs.index("(") + 1:]
                    if body[st:en].replace(" ", "") in inner.replace(" ", ""):
                        r.sinks.add((name, pos))
                        if r.ctype is None:
                            r.ctype = cast_kind(ex.strip())
        if r.var:
            add_var_sinks(f, body, b, r.var, r.sinks)
        return
    # raw: var = READ;
    vm = re.search(r"([A-Za-z_]\w*)\s*=\s*(\([^()]*\)\s*)?$", before)
    if vm and body[en:b].strip() == "":
        r.var = vm.group(1)
        rest = body[b:]
        vv = re.escape(r.var)
        if re.search(r"\bevtSet(Value|Float)\s*\(\s*" + ev_re + r"\s*,\s*" + vv + r"\b", rest):
            r.mode = "out"
        elif re.search(r"\bevtGet(Value|Float|Number)\s*\(\s*" + ev_re + r"\s*,\s*" + vv + r"\b", rest):
            gm = re.search(r"\bevtGet(Value|Float|Number)\s*\(\s*" + ev_re + r"\s*,\s*" + vv + r"\b", rest)
            r.mode = "float" if gm.group(1) == "Float" else "value"
        else:
            r.mode = "raw"
            r.ctype = f._types.get(r.var)
        return
    r.mode = "raw"


def add_var_sinks(f, body, frm, var, sinks):
    vv = r"\b" + re.escape(var) + r"\b"
    rest = body[frm:]
    for name, args, cs, ce in calls_in(rest):
        if name in NO_FOLLOW:
            continue
        for pos, ex in enumerate(args):
            if re.fullmatch(r"\s*" + vv + r"\s*", strip_casts(ex)):
                sinks.add((name, pos))
    if re.search(r"\bitemDataTable\s*\[\s*" + re.escape(var) + r"\s*\]", rest):
        sinks.add(("itemDataTable", 0))


MACRO_DEF = re.compile(r"^[ \t]*#[ \t]*define[ \t]+(\w+)\(([^)]*)\)((?:[^\n]*\\\n)*[^\n]*)", re.M)


def expand_user_macros(t):
    """Script functions written by a function-like macro (SETTER(evt_pouch_add_coin, ...),
    FLAGONOFF(disp, ...)): each invocation replaced by its expansion, on its own line."""
    macros = {}
    for m in MACRO_DEF.finditer(t):
        body = m.group(3).replace("\\\n", " ")
        if "USER_FUNC" in body:
            macros[m.group(1)] = ([p.strip() for p in m.group(2).split(",") if p.strip()], body)
    if not macros:
        return t
    rx = re.compile(r"^([ \t]*)(" + "|".join(map(re.escape, macros)) + r")\s*\(", re.M)
    out = []
    pos = 0
    for m in rx.finditer(t):
        if m.start() < pos:
            continue
        o = m.end() - 1
        c = match_paren(t, o)
        if c < 0:
            continue
        params, body = macros[m.group(2)]
        args = [a.strip() for a in split_top(t[o + 1:c])]
        exp = body
        for p, a in zip(params, args):
            exp = re.sub(r"\s*##\s*" + re.escape(p) + r"\b", a, exp)
            exp = re.sub(r"\b" + re.escape(p) + r"\s*##\s*", a, exp)
            exp = re.sub(r"\b" + re.escape(p) + r"\b", a, exp)
        exp = re.sub(r"\s*##\s*", "", exp)
        nl = t.count("\n", m.start(), c + 1)
        out.append(t[pos:m.start()])
        out.append(m.group(1) + exp + "\n" * nl)
        pos = c + 1
    out.append(t[pos:])
    return "".join(out)


def scan_file(path, text, funcs):
    raw = strip_comments(text)
    # #define wait win_main_wait: the compiled name is the second
    renames = dict(re.findall(r"^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)[ \t]+([A-Za-z_]\w*)[ \t]*$", raw, re.M))
    t = drop_preproc(expand_user_macros(raw))
    i = 0
    depth_ok = top_level_mask(t)
    for m in FUNC_RE.finditer(t):
        o = m.end() - 1
        if not depth_ok(o):
            continue
        user_name = m.group(2)
        name = user_name or m.group(3)
        if name in KEYWORDS or name is None:
            continue
        sig = "EventEntry* event, BOOL isFirstCall" if user_name else m.group(4)
        params = param_list(sig)
        user = bool(user_name) or (len(params) == 2 and re.search(r"EventEntry\s*\*", split_top(sig)[0] or "")
                                   and re.search(r"\bBOOL\b|\bint\b|\bs32\b", split_top(sig)[1]))
        c = match_brace(t, o)
        body = t[o:c + 1]
        line = t.count("\n", 0, o) + 1
        f = Func(name, path, line, params, body, bool(user), o)
        f._types = var_types(body)
        for (pn, pk) in params:
            f._types.setdefault(pn, pk)
        if f.user:
            analyze_user(f)
        # parameter -> callee edges
        for pi, (pn, pk) in enumerate(params):
            if pn in ("?", "...") or (f.user and pi == 0):
                continue
            s = set()
            add_var_sinks(f, body, 0, pn, s)
            for callee, pos in s:
                f.edges.append((pi, callee, pos))
        funcs.setdefault(name, []).append(f)
        if renames.get(name, name) != name:
            funcs.setdefault(renames[name], []).append(f)


def top_level_mask(t):
    """o -> True when the { at o is at brace depth 0."""
    depth = [0] * (len(t) + 1)
    d = 0
    i, n = 0, len(t)
    marks = []
    while i < n:
        c = t[i]
        if c == '"' or c == "'":
            j = i + 1
            while j < n and t[j] != c:
                if t[j] == "\\":
                    j += 1
                j += 1
            i = j + 1
            continue
        if c == "{":
            marks.append((i, d))
            d += 1
        elif c == "}":
            d -= 1
        i += 1
    top = {i for i, dd in marks if dd == 0}
    return lambda o: o in top


def sink_kind(callee, pos):
    k = BASE_SINKS.get((callee, pos))
    if k:
        return k
    for rx, p, kind in PREFIX_SINKS:
        if p == pos and rx.match(callee):
            return kind
    return None


def resolve(funcs):
    """Fixpoint: each function parameter's kinds through helper calls."""
    changed = True
    rounds = 0
    while changed and rounds < 12:
        changed = False
        rounds += 1
        for name, fl in funcs.items():
            for f in fl:
                for pi, callee, pos in f.edges:
                    ks = set()
                    k = sink_kind(callee, pos)
                    if k:
                        ks.add(k)
                    for g in funcs.get(callee, ()):
                        if pos < len(g.param_kinds) and not (g.user and pos == 0):
                            ks |= g.param_kinds[pos]
                    if not ks <= f.param_kinds[pi]:
                        f.param_kinds[pi] |= ks
                        changed = True


def read_kinds(funcs, r):
    ks = set()
    for callee, pos in r.sinks:
        k = sink_kind(callee, pos)
        if k:
            ks.add(k)
        elif callee == "itemDataTable":
            ks.add("item")
        for g in funcs.get(callee, ()):
            if pos < len(g.param_kinds) and not g.user:
                ks |= g.param_kinds[pos]
    return ks


def summarize(funcs):
    """name -> {'min': required arg count, 'max': args read, 'variadic', 'args': {i: info}, 'path', 'line'}."""
    resolve(funcs)
    out = {}
    for name, fl in funcs.items():
        for f in fl:
            if not f.user:
                continue
            args = {}
            req = 0
            mx = mxu = 0
            for r in f.reads:
                if r.mode == "discard":
                    mx = max(mx, r.idx + 1)  # read (so not an extra argument), but harmless if missing
                    continue
                a = args.setdefault(r.idx, {"modes": set(), "ctypes": set(), "kinds": set(), "cond": True})
                a["modes"].add(r.mode)
                if r.ctype:
                    a["ctypes"].add(r.ctype)
                a["kinds"] |= read_kinds(funcs, r)
                a["cond"] = a["cond"] and r.cond
                mx = max(mx, r.idx + 1)
                mxu = max(mxu, r.idx + 1)
                if not r.cond:
                    req = max(req, r.idx + 1)
            info = {"min": req, "max": mx, "max_used": mxu, "variadic": f.variadic, "args": args, "path": f.path, "line": f.line}
            # the same name can be defined more than once (a static helper in two rels): keep them all
            out.setdefault(name, []).append(info)
    return out


def pick(infos, unit):
    """The definition a script in unit calls: its own file's, its rel's, then the shared one."""
    if not infos:
        return None
    for i in infos:
        if i["path"] == unit:
            return i
    if unit.startswith("rels/"):
        area = "/".join(unit.split("/")[:2]) + "/"
        for i in infos:
            if i["path"].startswith(area) and not i["path"].endswith((".h", ".inc")):
                return i
        for i in infos:
            if i["path"].startswith(area):
                return i
    for i in infos:
        if not i["path"].startswith("rels/"):
            return i
    return infos[0]


TYPEDEFS = {}  # typedef name -> 'int' / 'word' / 'float' / 'ptr' / 'other'


def load_typedefs(root):
    """Scalar typedefs and enums of the headers: an enum or s32 alias is an int, a pointer alias a pointer."""
    for d in ("include", "port/include", "src", "rels"):
        for dp, dn, fn in os.walk(os.path.join(root, d)):
            for n in fn:
                if not n.endswith((".h", ".c")):
                    continue
                try:
                    t = strip_comments(open(os.path.join(dp, n), encoding="utf-8", errors="replace").read())
                except OSError:
                    continue
                if "typedef" not in t:
                    continue
                for m in re.finditer(r"\btypedef\s+enum\b[^{;]*\{[^}]*\}\s*(\w+)\s*;", t):
                    TYPEDEFS[m.group(1)] = "int"
                for m in re.finditer(r"\btypedef\s+((?:const\s+|unsigned\s+|signed\s+|struct\s+)*\w+)\s*(\**)\s*(\w+)\s*;", t):
                    base, star, name = m.group(1).split()[-1], m.group(2), m.group(3)
                    if star:
                        TYPEDEFS[name] = "ptr"
                    elif base in INT_TYPES:
                        TYPEDEFS.setdefault(name, "int")
                    elif base in WORD_TYPES:
                        TYPEDEFS[name] = "word"
                    elif base in FLOAT_TYPES:
                        TYPEDEFS[name] = "float"
                    elif base in TYPEDEFS:
                        TYPEDEFS.setdefault(name, TYPEDEFS[base])
                for m in re.finditer(r"\btypedef\s+\w[\w\s]*\(\s*\*\s*(\w+)\s*\)", t):
                    TYPEDEFS[m.group(1)] = "ptr"  # a function pointer type


def scan_tree(root, dirs=("src", "rels", "port")):
    load_typedefs(root)
    funcs = {}
    for d in dirs:
        for dp, dn, fn in os.walk(os.path.join(root, d)):
            dn[:] = [x for x in dn if x not in ("patches", "relay", "android", "ios", "apple", "build")]
            for n in fn:
                if n.endswith((".c", ".cpp", ".h", ".inc")):
                    p = os.path.join(dp, n)
                    try:
                        with open(p, encoding="utf-8", errors="replace") as fh:
                            text = fh.read()
                    except OSError:
                        continue
                    if "(" not in text:
                        continue
                    scan_file(os.path.relpath(p, root).replace("\\", "/"), text, funcs)
    return funcs
