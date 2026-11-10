"""The message tags the port's C code asks for, against what answers them, per build and per release.

msgSearch (src/driver/msgdrv.c) answers a tag from the loaded message files or, on PC, from Paper Cut's own
handlers (portPauseSettingsMsg, portJournalHintMsg, portOpenMsg, ...); a tag nothing answers is the red
"No messages" box and a "missing message" line in the log. evt_lint.py checks the tags of event scripts
(on the US Aurora build); this checks the C code's own, which evt_lint never sees, on both backends:

- Paper Cut's tags ("pc_..."): every one the C code names, in code a build compiles (src/, port/src/common
  and that backend's own sources, #if blocks resolved for it), is answered in that build: named again
  somewhere it compiles (a handler's strcmp or table) or under a prefix it compares (strncmp). A tag only
  Aurora answers but both builds ask for (the pause menu's tab help on headless: "pc_menu_kiroku") is one.
- the game's tags Paper Cut's code names (port/: any literal that is a tag in some language's files, a
  lookup's or a table's): in a message file of every language of every release (US, JP, EU's five),
  when the discs are in orig/ (read once, then cached in the temp folder).

  python tools/port/msg_tags.py      (exit 1 and the findings; testpass.py lint runs it too)
"""
import os
import re
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HERE = os.path.dirname(os.path.abspath(__file__))

COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
PC_TAG = re.compile(r'"(pc_\w+)"')
PREFIX = re.compile(r'strncmp\(\s*[\w>.\-]+\s*,\s*"(pc_\w+)"\s*,\s*\d+\s*\)')
ANSWER = re.compile(r'strcmp\(\s*[\w>.\-]+\s*,\s*"(pc_\w+)"\s*\)')  # (a handler's, in the game's files too: msgdrv.c)
SETTING = re.compile(r'\b\w*[Ss]etting\w*\(\s*$')  # (a setting's key, not a message: settingOn("pc_badges"))
# tags that are no message asked for on the build named
NOT_ASKED = {
    ("pc_pm64_rowf", "headless"): "Rowf's NPC name (pm64_field_pc.c's tattles): he is only ever there on Aurora "
                                  "(pm64_badges_aurora.c)",
}
LITERAL = re.compile(r'"([A-Za-z0-9_]+)"')

# what each build defines (CMakeLists.txt); VERSION as the US release's (both regions' code paths are
# alike for these tags)
BUILDS = {
    "headless": {"TARGET_PC", "PORT_MODS", "TTYD_BACKEND_HEADLESS"},
    "aurora": {"TARGET_PC", "PORT_MODS", "TTYD_BACKEND_AURORA"},
}
VERSIONS = {"VERSION_JP": 1, "VERSION_US": 2, "VERSION_EU": 3, "VERSION": 2}


def strip_comments(text):
    # (comments as blank lines, so the preprocessor's lines stay where they were)
    return COMMENT.sub(lambda m: "\n" * m.group(0).count("\n"), text)


def eval_if(expr, defs):
    e = re.sub(r"\bdefined\s*\(\s*(\w+)\s*\)", lambda m: " 1 " if m.group(1) in defs else " 0 ", expr)
    e = re.sub(r"\bdefined\s+(\w+)", lambda m: " 1 " if m.group(1) in defs else " 0 ", e)
    e = e.replace("&&", " and ").replace("||", " or ")
    e = re.sub(r"!(?!=)", " not ", e)

    def ident(m):
        n = m.group(0)
        if n in ("and", "or", "not"):
            return n
        if n in VERSIONS:
            return str(VERSIONS[n])
        return "1" if n in defs else "0"
    e = re.sub(r"\b[A-Za-z_]\w*\b", ident, e)
    e = re.sub(r"\b(0x[0-9A-Fa-f]+|\d+)[uUlL]*\b", r"\1", e)
    try:
        return bool(eval(e, {"__builtins__": {}}))
    except Exception:
        return True  # (an expression this can't read: both ways, as if it were there)


def live_text(text, defs):
    """The text with the lines a build doesn't compile blanked (#if/#ifdef/#ifndef/#elif/#else/#endif, and the
    #defines met on the way)."""
    defs = set(defs)
    out = []
    stack = []  # (this branch on, some branch taken, the parent on)
    on = True
    lines = strip_comments(text).split("\n")
    i = 0
    while i < len(lines):
        line = lines[i]
        full = line
        while full.endswith("\\") and i + 1 < len(lines):
            i += 1
            full = full[:-1] + " " + lines[i]
            out.append("")
        m = re.match(r"\s*#\s*(\w+)\s*(.*)", full)
        if m:
            d, rest = m.group(1), m.group(2).strip()
            if d in ("if", "ifdef", "ifndef"):
                c = (rest.split()[0] in defs) if d == "ifdef" and rest else \
                    (rest.split()[0] not in defs) if d == "ifndef" and rest else eval_if(rest, defs)
                stack.append((on, c and on))
                on = on and c
            elif d == "elif" and stack:
                parent, taken = stack[-1]
                c = parent and not taken and eval_if(rest, defs)
                stack[-1] = (parent, taken or c)
                on = c
            elif d == "else" and stack:
                parent, taken = stack[-1]
                on = parent and not taken
                stack[-1] = (parent, True)
            elif d == "endif" and stack:
                on = stack.pop()[0]
                if stack:
                    pass
            elif d == "define" and on and rest:
                defs.add(re.match(r"\w+", rest).group(0))
            elif d == "undef" and on and rest:
                defs.discard(rest.split()[0])
            out.append("")
        else:
            out.append(line if on else "")
        i += 1
    return "\n".join(out)


def sources(build):
    roots = ["src", "rels", os.path.join("port", "src", "common"), os.path.join("port", "mods"),
             os.path.join("port", "archipelago")]
    roots.append(os.path.join("port", "src", "headless") if build == "headless" else os.path.join("port", "src", "aurora"))
    if build == "aurora":
        roots.append(os.path.join("port", "settings"))
    files = []
    for r in roots:
        for dp, dn, fn in os.walk(os.path.join(REPO, r)):
            for n in fn:
                if n.endswith((".c", ".cpp", ".h", ".inc")):
                    files.append(os.path.join(dp, n))
    if build == "aurora":  # (Aurora's build takes headless's profile_pc.c too: CMakeLists.txt)
        files.append(os.path.join(REPO, "port", "src", "headless", "platform", "profile_pc.c"))
    return files


def pc_tag_problems():
    """Paper Cut's tags each build asks for and answers: the game's code (src/, rels/) only ever asks, so a tag
    it names wants Paper Cut's code of that build (port/) to name it too (a handler's strcmp, a table, a
    portModsSetMessage) or a prefix it compares; a tag Paper Cut's shared code names once, which only the
    other backend's own code names again, is that backend's answer the build lacks."""
    seen = {}  # build -> tag -> {"game": [(rel, line)], "port": [(rel, line)]}
    prefixes = {}
    for build, defs in BUILDS.items():
        tags, pre = {}, set()
        for path in sources(build):
            try:
                text = open(path, encoding="utf-8", errors="replace").read()
            except OSError:
                continue
            if "pc_" not in text:
                continue
            live = live_text(text, defs)
            rel = os.path.relpath(path, REPO).replace("\\", "/")
            side = "port" if rel.startswith("port/") else "game"
            for m in PC_TAG.finditer(live):
                if SETTING.search(live[max(0, m.start() - 48):m.start()]):
                    continue
                tags.setdefault(m.group(1), {"game": [], "port": []})[side].append(
                    (rel, live.count("\n", 0, m.start()) + 1))
            pre.update(m.group(1) for m in PREFIX.finditer(live))
            pre.update(m.group(1) for m in ANSWER.finditer(live))  # (a whole tag: its own prefix)
        seen[build], prefixes[build] = tags, pre
    bad = []
    for build in BUILDS:
        other = [b for b in BUILDS if b != build][0]
        for tag, at in sorted(seen[build].items()):
            if any(tag.startswith(p) for p in prefixes[build]) or (tag, build) in NOT_ASKED:
                continue
            mine = {r for r, _ in at["port"]}
            theirs = {r for r, _ in seen[other].get(tag, {"port": []})["port"]} - mine
            if at["game"] and not at["port"]:
                rel, line = at["game"][0]
            elif len(at["port"]) == 1 and theirs and not at["game"]:
                rel, line = at["port"][0]
            else:
                continue
            bad.append("%s:%d: '%s' is asked for on %s and nothing there answers it%s" % (
                rel, line, tag, build, " (only %s does: %s)" % (other, ", ".join(sorted(theirs))) if theirs else ""))
    return bad


def game_tag_uses(known):
    """The game's tags Paper Cut's code names (port/: a lookup's literal, a table's entry): every literal there
    that is a message tag in some language's files."""
    uses = {}
    for path in sorted(set(sources("aurora") + sources("headless"))):
        rel = os.path.relpath(path, REPO).replace("\\", "/")
        if not rel.startswith("port/"):
            continue
        live = strip_comments(open(path, encoding="utf-8", errors="replace").read())
        for m in LITERAL.finditer(live):
            if m.group(1) in known:
                uses.setdefault(m.group(1), set()).add((rel, live.count("\n", 0, m.start()) + 1))
    return uses


def game_tag_problems():
    sys.path.insert(0, HERE)
    try:
        import evt_lint
    except Exception:
        return []
    disc = evt_lint.DiscData(os.path.join(tempfile.gettempdir(), "ttyd_msg_tags"))
    os.makedirs(disc.cache, exist_ok=True)
    if not disc.load():
        return []  # (no discs: nothing to check against)
    by_lang = {lang: set().union(*files.values()) for lang, files in disc.msg.items()}
    bad = []
    for tag, where in sorted(game_tag_uses(set().union(*by_lang.values())).items()):
        lacking = [lang for lang, tags in sorted(by_lang.items()) if tag not in tags]
        if lacking:
            rel, line = sorted(where)[0]
            bad.append("%s:%d: the game's '%s' is in no message file of %s" % (rel, line, tag, ", ".join(lacking)))
    return bad


def problems():
    return pc_tag_problems() + game_tag_problems()


if __name__ == "__main__":
    p = problems()
    for x in p:
        print(x)
    print("%d finding(s)" % len(p))
    sys.exit(1 if p else 0)
