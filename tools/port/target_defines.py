"""Build definitions per target: a macro a library's sources test that the build never defines for it.

CMake's target_compile_definitions(<target> PRIVATE ...) reaches only that target. Paper Cut's C++ side
libraries (ttyd_settings, ttyd_mods, ttyd_touch, ...) are targets of their own, so a macro defined PRIVATE
on ttyd is never seen by them: second_player.cpp tested PORT_TOUCH_OVERLAY, which only ttyd had, and
always compiled its "no touch controls" stub on phones.

This reads CMakeLists.txt (and the cmake files it includes) without configuring: every target's
definitions in any configuration (target_compile_definitions, per-source COMPILE_DEFINITIONS, the lists
they expand), and every library's sources it can name (paths under the source tree, file(GLOB) lists).
For each library source, every macro its #if/#ifdef/#elif lines test that the build defines somewhere
(for any target) must be defined for that library too, or for another target that builds the same
source (build_id.cpp's PORT_BUILD_HEADLESS tells its two builds apart), or by the source itself.

  python tools/port/target_defines.py      (exit 1 and the findings; testpass.py lint runs it too)
"""
import glob
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CMAKE_FILES = ("CMakeLists.txt", "cmake/web.cmake", "port/ios/ios.cmake")
SOURCE_EXT = (".c", ".cc", ".cpp", ".cxx", ".m", ".mm")
# set for every target by the toolchain or build type, or only consulted by system headers
IGNORED = {"NDEBUG", "_CRT_SECURE_NO_WARNINGS", "_USE_MATH_DEFINES", "UNICODE", "_UNICODE"}
# targets whose sources are the game's or tests (built with the game's definitions on purpose, or apart)
LIBRARY_KINDS = {"OBJECT", "STATIC", "SHARED", "MODULE"}
CONDITIONAL = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|elifdef|elifndef)\b(.*)$")
DEFINE = re.compile(r"^\s*#\s*define\s+([A-Za-z_]\w*)")
IDENT = re.compile(r"[A-Za-z_]\w*")


def commands(text):
    """[(name, [args])] of a CMake file, quotes and comments understood (arguments unexpanded)."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c == "#":
            if text.startswith("#[[", i) or text.startswith("#[=", i):
                end = text.find("]]", i)
                i = n if end < 0 else end + 2
            else:
                end = text.find("\n", i)
                i = n if end < 0 else end + 1
            continue
        m = re.compile(r"[A-Za-z_]\w*\s*\(").match(text, i)
        if not m:
            i += 1
            continue
        name = text[i:m.end()].split("(")[0].strip().lower()
        i = m.end()
        args, word, depth, quoted = [], "", 1, False
        while i < n and depth > 0:
            c = text[i]
            if quoted:
                if c == "\\" and i + 1 < n:
                    word += text[i:i + 2]
                    i += 2
                    continue
                if c == '"':
                    quoted = False
                    args.append(word)
                    word = ""
                else:
                    word += c
            elif c == '"':
                quoted = True
            elif c == "#":
                end = text.find("\n", i)
                i = n if end < 0 else end
                continue
            elif c == "(":
                depth += 1
                word += c
            elif c == ")":
                depth -= 1
                if depth > 0:
                    word += c
            elif c.isspace():
                if word:
                    args.append(word)
                word = ""
            else:
                word += c
            i += 1
        if word:
            args.append(word)
        out.append((name, args))
    return out


class Model:
    def __init__(self):
        self.vars = {"CMAKE_SOURCE_DIR": [REPO], "CMAKE_CURRENT_SOURCE_DIR": [REPO], "PROJECT_SOURCE_DIR": [REPO],
                     "TTYD_VERSION": ["G8MJ01", "G8ME01", "G8MP01"]}  # (every release's macros)
        self.defs = {}      # target: {macro}
        self.srcdefs = {}   # source path: {macro}
        self.sources = {}   # target: [path]
        self.kind = {}      # target: OBJECT, STATIC, ... or EXECUTABLE

    def expand(self, word, depth=0):
        """a word with its ${VAR}s: every value (a list variable gives one word per item); [] if unknown"""
        m = re.search(r"\$\{([^${}]*)\}", word)
        if m is None:
            return [word]
        if depth > 8:
            return []
        values = self.vars.get(m.group(1))
        if not values:
            return []
        out = []
        for v in values:
            out += self.expand(word[:m.start()] + v + word[m.end():], depth + 1)
        return out[:4096]

    def words(self, args):
        out = []
        for a in args:
            if a.startswith("$<"):
                continue  # (a generator expression: configure-time only)
            out += self.expand(a)
        return out

    def add_var(self, name, values):
        have = self.vars.setdefault(name, [])
        have += [v for v in values if v not in have]  # (every branch's values: any configuration)

    def run(self, cmds):
        for name, args in cmds:
            getattr(self, "cmd_" + name, lambda a: None)(args)

    def cmd_set(self, args):
        if not args:
            return
        vals = args[1:]
        for stop in ("CACHE", "PARENT_SCOPE"):
            if stop in vals:
                vals = vals[:vals.index(stop)]
        self.add_var(args[0], self.words(vals))

    def cmd_list(self, args):
        if len(args) >= 2 and args[0] in ("APPEND", "PREPEND", "INSERT"):
            vals = args[3:] if args[0] == "INSERT" else args[2:]
            self.add_var(args[1], self.words(vals))

    def cmd_file(self, args):
        if len(args) < 3 or args[0] not in ("GLOB", "GLOB_RECURSE") or "RELATIVE" in args:
            return
        found = []
        for pat in self.words([a for a in args[2:] if a not in ("CONFIGURE_DEPENDS", "LIST_DIRECTORIES")]):
            found += sorted(glob.glob(pat, recursive=args[0] == "GLOB_RECURSE"))
        self.add_var(args[1], [p.replace("\\", "/") for p in found])

    def cmd_foreach(self, args):
        if not args:
            return
        rest = args[1:]
        if rest[:1] == ["RANGE"]:
            return
        if rest[:2] == ["IN", "LISTS"]:
            vals = [v for lst in rest[2:] for v in self.vars.get(lst, [])]
        elif rest[:2] == ["IN", "ITEMS"]:
            vals = self.words(rest[2:])
        else:
            vals = self.words(rest)
        self.add_var(args[0], vals)

    def _target(self, args, kind):
        names = self.expand(args[0]) if args else []
        rest = args[1:]
        if rest[:1] and rest[0] in LIBRARY_KINDS:
            kind = rest[0] if kind is None else kind
            rest = rest[1:]
        rest = [a for a in rest if a not in ("EXCLUDE_FROM_ALL", "WIN32", "MACOSX_BUNDLE", "IMPORTED", "ALIAS",
                                              "GLOBAL", "INTERFACE")]
        for t in names:
            self.kind.setdefault(t, kind or "STATIC")
            self.sources.setdefault(t, []).extend(self.words(rest))

    def cmd_add_library(self, args):
        self._target(args, None)

    def cmd_add_executable(self, args):
        self._target(args, "EXECUTABLE")

    def cmd_target_sources(self, args):
        for t in self.expand(args[0]) if args else []:
            self.sources.setdefault(t, []).extend(
                self.words([a for a in args[1:] if a not in ("PRIVATE", "PUBLIC", "INTERFACE")]))

    @staticmethod
    def macro(word):
        name = word.split("=", 1)[0].strip()
        if name.startswith("-D"):
            name = name[2:]
        return name if IDENT.fullmatch(name or "-") else None

    def cmd_target_compile_definitions(self, args):
        for t in self.expand(args[0]) if args else []:
            mine = self.defs.setdefault(t, set())
            for w in self.words([a for a in args[1:] if a not in ("PRIVATE", "PUBLIC", "INTERFACE")]):
                if self.macro(w):
                    mine.add(self.macro(w))

    def _source_defs(self, files, defs):
        for f in files:
            mine = self.srcdefs.setdefault(os.path.normcase(os.path.abspath(f)), set())
            for w in defs:
                if self.macro(w):
                    mine.add(self.macro(w))

    def cmd_set_property(self, args):
        if not args or args[0] != "SOURCE" or "PROPERTY" not in args:
            return
        at = args.index("PROPERTY")
        if args[at + 1:at + 2] != ["COMPILE_DEFINITIONS"]:
            return
        files = args[1:at]
        if "TARGET_DIRECTORY" in files:
            files = files[:files.index("TARGET_DIRECTORY")]
        files = [f for f in files if f not in ("APPEND", "APPEND_STRING")]
        self._source_defs(self.words(files), self.words(args[at + 2:]))

    def cmd_set_source_files_properties(self, args):
        if "PROPERTIES" not in args:
            return
        at = args.index("PROPERTIES")
        files = args[:at]
        if "TARGET_DIRECTORY" in files:
            files = files[:files.index("TARGET_DIRECTORY")]
        props = args[at + 1:]
        for i in range(0, len(props) - 1, 2):
            if props[i] == "COMPILE_DEFINITIONS":
                self._source_defs(self.words(files), self.words(props[i + 1].split(";")))


def tested(path):
    """{macro: first line} a source's conditionals test, and {macro} it #defines itself"""
    try:
        with open(path, encoding="utf-8", errors="replace") as fh:
            lines = fh.read().splitlines()
    except OSError:
        return {}, set()
    seen, own, i = {}, set(), 0
    while i < len(lines):
        line, at = lines[i], i + 1
        while line.endswith("\\") and i + 1 < len(lines):
            i += 1
            line = line[:-1] + " " + lines[i]
        i += 1
        d = DEFINE.match(line)
        if d:
            own.add(d.group(1))
            continue
        m = CONDITIONAL.match(line)
        if not m:
            continue
        expr = re.sub(r"/\*.*?\*/|//.*$", "", m.group(2))
        expr = re.sub(r"'(?:\\.|[^'])*'|\"(?:\\.|[^\"])*\"", "", expr)
        for name in IDENT.findall(expr):
            if name != "defined":
                seen.setdefault(name, at)
    return seen, own


def problems():
    model = Model()
    for rel in CMAKE_FILES:
        path = os.path.join(REPO, rel)
        if os.path.isfile(path):
            with open(path, encoding="utf-8", errors="replace") as fh:
                model.run(commands(fh.read()))
    known = set().union(*model.defs.values(), *model.srcdefs.values()) - IGNORED
    builders = {}  # source: [targets building it]
    for t, srcs in model.sources.items():
        for s in srcs:
            builders.setdefault(os.path.normcase(os.path.abspath(s)), []).append(t)
    bad = []
    for t in sorted(model.sources):
        if model.kind.get(t) not in LIBRARY_KINDS or t in ("ttyd", "ttyd_dol", "musyx_pc") or t.startswith("rel_"):
            continue  # (the game's own code and the executables: the definitions' reference, not checked)
        for s in sorted(set(model.sources[t])):
            if not s.endswith(SOURCE_EXT) or not os.path.isfile(s):
                continue
            key = os.path.normcase(os.path.abspath(s))
            have = set(model.srcdefs.get(key, set()))
            for other in builders.get(key, [t]):
                have |= model.defs.get(other, set())
            seen, own = tested(s)
            for name, line in sorted(seen.items(), key=lambda kv: kv[1]):
                if name in known and name not in have and name not in own:
                    bad.append(f"{os.path.relpath(s, REPO).replace(os.sep, '/')}:{line}: tests {name}, which the build never defines "
                               f"for {t} (a PRIVATE definition of another target: give {t} it too)")
    return bad


def main():
    bad = problems()
    for b in bad:
        print(b)
    print(f"target_defines: {len(bad)} problems")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
