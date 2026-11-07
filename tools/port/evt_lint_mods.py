#!/usr/bin/env python3
"""Mods' text scripts (<mod>/evt/*.evt, docs/MODDING.md "Scripts") for evt_lint.py.

Assembles each script as port/mods/evt_asm.cpp does (the same command table,
port/mods/evt_table_gen.h, and value syntax: LW(), GSW(), FLOAT(), STRING(),
PTR(), constants, arithmetic) into the words the game would run, then runs the
checker's own script checks on them. A mod's messages (messages/*.txt,
"[tag]" sections) count as message tags for its scripts; a mod's map
(mod.json "maps") counts as a map, reached from its init script.
"""

import json
import os
import re

import evt_lint_ir as ir

VAR_BASE = {"LW": 30000000, "GW": 50000000, "LF": 70000000, "GF": 90000000, "LSWF": 110000000,
            "GSWF": 130000000, "LSW": 150000000, "GSW": 170000000, "UW": 190000000, "UF": 210000000}


def load_table(root):
    p = os.path.join(root, "port", "mods", "evt_table_gen.h")
    t = open(p, encoding="utf-8").read()
    cmds = {m.group(1): (int(m.group(2)), int(m.group(3)))
            for m in re.finditer(r'\{"(\w+)", (\d+), (-?\d+)\}', t)}
    consts = {m.group(1): int(m.group(2)) for m in re.finditer(r'\{"(\w+)", (-?\d+)LL\}', t)}
    return cmds, consts


TOK = re.compile(r'\s+|//[^\n]*|/\*.*?\*/|"(?:[^"\\]|\\.)*"|\d+\.\d*(?:[eE][-+]?\d+)?f?|0[xX][0-9A-Fa-f]+|\d+|'
                 r'[A-Za-z_]\w*|<<|>>|[-+*/%|&^~(),]', re.S)


def tokens(text):
    out = []
    pos = 0
    while pos < len(text):
        m = TOK.match(text, pos)
        if not m:
            raise ValueError("bad character %r" % text[pos])
        s = m.group(0)
        pos = m.end()
        if s.isspace() or s.startswith("//") or s.startswith("/*"):
            continue
        out.append(s)
    return out


class Asm:
    PREC = {"|": 1, "^": 2, "&": 3, "<<": 4, ">>": 4, "+": 5, "-": 5, "*": 6, "/": 6, "%": 6}

    def __init__(self, cmds, consts, strings):
        self.cmds, self.consts, self.strings = cmds, consts, strings

    def value(self, toks):
        """One argument's tokens -> int or Ref."""
        self.t, self.i = toks, 0
        v = self.expr(0)
        if isinstance(v, float):
            v = int(v)
        return v

    def expr(self, minp):
        a = self.unary()
        while self.i < len(self.t) and self.t[self.i] in self.PREC and self.PREC[self.t[self.i]] > minp:
            op = self.t[self.i]
            self.i += 1
            b = self.expr(self.PREC[op])
            if isinstance(a, ir.Ref) or isinstance(b, ir.Ref):
                continue
            if op in "+-*/%" and (isinstance(a, float) or isinstance(b, float)):
                a = {"+": a + b, "-": a - b, "*": a * b, "/": a / b if b else 0.0, "%": a % b if b else 0.0}[op]
                continue
            a, b = int(a), int(b)
            a = {"+": a + b, "-": a - b, "*": a * b, "/": int(a / b) if b else 0, "%": a % b if b else 0,
                 "|": a | b, "&": a & b, "^": a ^ b, "<<": a << b, ">>": a >> b}[op]
        return a

    def unary(self):
        s = self.t[self.i]
        self.i += 1
        if s == "-":
            v = self.unary()
            return -v if not isinstance(v, ir.Ref) else v
        if s == "~":
            return ~int(self.unary())
        if s == "(":
            v = self.expr(0)
            self.i += 1
            return v
        if s.startswith('"'):
            body = s[1:-1].encode("utf-8")
            name = "modstr.%d" % len(self.strings)
            self.strings[name] = body + b"\0"
            return ir.Ref(name)
        if s[0].isdigit():
            if "." in s or s.endswith("f"):
                return float(s.rstrip("f"))
            return int(s, 0)
        if self.i < len(self.t) and self.t[self.i] == "(":
            self.i += 1
            v = self.expr(0)
            self.i += 1
            if s == "FLOAT":
                return int(float(v) * 1024.0) - 230000000
            if s in ("PTR", "STRING"):
                return v
            if s in VAR_BASE:
                return int(v) - VAR_BASE[s]
            raise ValueError("no function %s()" % s)
        if s in self.consts:
            return self.consts[s]
        return ir.Ref(s)  # a function, script or table by its name in the game

    def script(self, toks, start):
        """(words, index after EVT_END()) of the script whose body starts at toks[start]."""
        words = []
        i = start
        while i < len(toks):
            c = toks[i]
            i += 1
            if c in ("EVT_END", "EVT_END_PAD", "EVT_END_PAD4"):
                if i < len(toks) and toks[i] == "(":
                    i += 2
                words.append(1)
                words += [0] * {"EVT_END": 0, "EVT_END_PAD": 1, "EVT_END_PAD4": 4}[c]
                return words, i
            if c not in self.cmds:
                raise ValueError("unknown command %s" % c)
            op, nargs = self.cmds[c]
            args = []
            if i < len(toks) and toks[i] == "(":
                depth = 0
                cur = []
                j = i
                while j < len(toks):
                    t = toks[j]
                    if t == "(":
                        depth += 1
                        if depth == 1:
                            j += 1
                            continue
                    elif t == ")":
                        depth -= 1
                        if depth == 0:
                            break
                    if t == "," and depth == 1:
                        args.append(cur)
                        cur = []
                    else:
                        cur.append(t)
                    j += 1
                if cur:
                    args.append(cur)
                i = j + 1
            vals = [self.value(a) for a in args]
            words.append((len(vals) << 16) | op)
            words += vals
        raise ValueError("a script without EVT_END()")


def mod_messages(moddir):
    tags = set()
    for dp, dn, fn in os.walk(os.path.join(moddir, "messages")):
        for n in fn:
            if n.endswith(".txt"):
                text = open(os.path.join(dp, n), encoding="utf-8", errors="replace").read()
                for m in re.finditer(r"^\[([^\]\s]+)\]", text, re.M):
                    tags.add(m.group(1))
    return tags


def lint_mods(lint, root):
    try:
        cmds, consts = load_table(root)
    except OSError:
        return
    for base in (os.path.join(root, "examples", "mods"), os.path.join(root, "port", "mods", "bundled")):
        if not os.path.isdir(base):
            continue
        for mod in sorted(os.listdir(base)):
            moddir = os.path.join(base, mod)
            evtdir = os.path.join(moddir, "evt")
            if not os.path.isdir(evtdir):
                continue
            rel_base = os.path.relpath(moddir, root).replace("\\", "/")
            try:
                meta = json.load(open(os.path.join(moddir, "mod.json"), encoding="utf-8"))
            except (OSError, ValueError):
                meta = {}
            strings = {}
            scripts = {}
            for n in sorted(os.listdir(evtdir)):
                if not n.endswith(".evt"):
                    continue
                rel = rel_base + "/evt/" + n
                text = open(os.path.join(evtdir, n), encoding="utf-8", errors="replace").read()
                asm = Asm(cmds, consts, strings)
                try:
                    toks = tokens(text)
                    i = 0
                    while i < len(toks):
                        if toks[i] == "EVT_BEGIN":
                            name = toks[i + 2]
                            words, i = asm.script(toks, i + 4)
                            scripts[(rel, name)] = words
                        else:
                            i += 1
                except (ValueError, IndexError) as e:
                    lint.add("flow-bad-opcode", rel, "(file)", "does not assemble: %s" % e)
            if not scripts:
                continue
            # the mod's names become globals of its own unit for the checker
            for (rel, name), words in scripts.items():
                for sn, b in strings.items():
                    lint.w.local[(rel, sn)] = ("str", b)
                lint.w.local[(rel, name)] = ("words", words)
                lint.w.globals.setdefault(name, (rel, "words", words))
                lint.scripts[(rel, name)] = words
                lint.stats["mod-scripts"] += 1
            tags = mod_messages(moddir)
            for mp, spec in (meta.get("maps") or {}).items():
                lint.map_names.add(mp)
                init = spec.get("init") if isinstance(spec, dict) else None
                for (rel, name) in scripts:
                    if name == init:
                        lint.reach((rel, name), mp)
                files = spec.get("files") if isinstance(spec, dict) else None
                if files and lint.disc.ok and files in lint.disc.joints:
                    lint.disc.joints[mp] = lint.disc.joints[files]  # its map file is another map's
            orig_check = lint.check_msg

            def check_msg(rel, script, fn, tag, mps, where, _tags=tags, _orig=orig_check):
                if tag not in _tags:
                    _orig(rel, script, fn, tag, mps, where)
            lint.check_msg = check_msg
            lint.collect_map_names()
            for (rel, name), words in sorted(scripts.items()):
                lint.check_script(rel, name, words)
            lint.check_msg = orig_check
