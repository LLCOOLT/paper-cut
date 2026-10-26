#!/usr/bin/env python3
"""Turn a GameCube mod's own copies of the game's headers into shims over
the decomp's (docs/MODDING.md, "Porting a GameCube mod"). Run after
mod_fields.py has renamed the mod's field accesses.

  mod_headers.py --mod DIR --headers 'include/ttyd/**/*.h' --headers 'include/gc/*.h'
                 --lst include/lst/ttyd.us.lst [--version G8ME01] [--alias Mod=Decomp ...]
                 [--evt-header include/evt_cmd.h]

In each header, in place:
- a struct the decomp also has (same name, or an --alias) loses its
  definition: the decomp's is used (`using Mod = Decomp;` when renamed);
- its extern "C" declarations go: the decomp's prototypes are used. A
  function the decomp names differently (matched by GameCube address: the
  mod's symbol list against config/<version>/symbols.txt) gets
  `#define ModName DecompName`. A variable becomes a reference bound by name
  at load (ttyd_mod/vars.h: Windows can't import data into a DLL). A function
  the decomp doesn't have keeps its prototype. ttyd-tools' EVT_DECLARE*
  macros are read the same way;
- ttyd_mod/game_all.h and ttyd_mod/vars.h are included.
In every header and source of the mod, static_asserts on sizes go (the PC
layouts differ); in --evt-header (ttyd-tools' script macros), script words
become the PC game's EvtWord; with --widen-pointer-casts, pointer arithmetic
done in uint32_t is done in uintptr_t (a raw GameCube offset stays one:
mod_fields.py's report and the port's docs list what to check by hand); with
--gc-lowmem, a fixed address in the GameCube's low memory is one in the
port's stand-in for it (TTYD_GC_ADDR); with --gc-data, a fixed address of the
game's data is the same field on the PC (TTYD_GC_DATA, gc_offsets.py).
Everything else (the mod's enums, constants, helpers) stays.
"""
import argparse
import glob
import os
import re
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mod_fields
import gc_offsets

ROOT = mod_fields.ROOT


REL_SECTIONS = {1: ".text", 2: ".ctors", 3: ".dtors", 4: ".rodata", 5: ".data", 6: ".bss"}


def splits(path, base):
    """{section: [(start, end, source path)]} from a decomp splits.txt; base:
    the folder its paths are relative to (src/, rels/<rel>/src/)."""
    out, cur = {}, None
    for ln in open(path, encoding="utf-8"):
        m = re.match(r"^(\S+\.c):\s*$", ln)
        if m:
            cur = f"{base}/{m.group(1)}"
            continue
        m = re.match(r"^\s+(\.\w+)\s+start:0x([0-9A-Fa-f]+) end:0x([0-9A-Fa-f]+)", ln)
        if m and cur:
            out.setdefault(m.group(1), []).append((int(m.group(2), 16), int(m.group(3), 16), cur))
    return out


def decomp_symbols(version):
    """(GameCube address -> decomp name, address -> a variable's source
    file): the DOL's by address, a REL's by (rel, section, offset)."""
    by_addr, local_file = {}, {}
    for path in glob.glob(os.path.join(ROOT, "config", version, "**", "symbols.txt"), recursive=True):
        rel = os.path.basename(os.path.dirname(path))
        rel = None if rel == version else rel
        sp = os.path.join(os.path.dirname(path), "splits.txt")
        ranges = splits(sp, "src" if rel is None else f"rels/{rel}/src") if os.path.exists(sp) else {}
        for ln in open(path, encoding="utf-8"):
            m = re.match(r"^(\S+) = (\.\w+):0x([0-9A-Fa-f]+);", ln)
            if not m:
                continue
            addr = int(m.group(3), 16)
            key = addr if rel is None else (rel, m.group(2), addr)
            by_addr.setdefault(key, m.group(1))
            if "type:function" in ln:
                CODE_KEYS.add(key)
            # (a REL's symbol the decomp keeps static may be global in the
            # REL: its file, either way)
            if "type:object" in ln or "type:function" in ln:
                for start, end, src in ranges.get(m.group(2), []):
                    if start <= addr < end:
                        local_file.setdefault(key, src)
                        break
    return by_addr, local_file


def rel_module_ids(rel_dir):
    """A REL's module id (its header's first word) -> its name, from the
    disc's files/rel (the mod's symbol list names RELs by id)."""
    out = {}
    for path in glob.glob(os.path.join(rel_dir, "*.rel")):
        with open(path, "rb") as f:
            out[int.from_bytes(f.read(4), "big")] = os.path.splitext(os.path.basename(path))[0]
    return out


def mod_symbols(path, rel_ids):
    """The mod's symbol list (ttyd-tools' .lst): name -> address, or for a
    REL's symbol (MODULE,SECTION,OFFSET) -> (rel, section, offset)."""
    out = {}
    for ln in open(path, encoding="utf-8", errors="replace"):
        ln = ln.strip()
        m = re.match(r"^([0-9A-Fa-f]{8}):(\w+)", ln)
        if m:
            out[m.group(2)] = int(m.group(1), 16)
            continue
        # MODULE,SECTION,OFFSET in hex, with or without 0x (lists differ)
        m = re.match(r"^(?:0x)?([0-9A-Fa-f]+),(\d+),(?:0x)?([0-9A-Fa-f]+):(\w+)", ln)
        if m:
            MOD_LISTED.add(m.group(4))  # (in a REL of the mod's own too)
        if m and int(m.group(1), 16) in rel_ids:
            out[m.group(4)] = (rel_ids[int(m.group(1), 16)], REL_SECTIONS.get(int(m.group(2))), int(m.group(3), 16))
    return out


def read_exports(path):
    """The names a game executable's module-definition file exports."""
    out = set()
    for ln in open(path, encoding="utf-8"):
        parts = ln.split()
        if parts and parts[0] != "EXPORTS":
            out.add(parts[0])
    return out


ENUM_DECL = re.compile(r"EnumDecl 0x[0-9a-f]+ .*? (?:(class|struct) )?(\w+)(?: '[^']*'(?::'[^']*')?)?$")
ENUM_CONST = re.compile(r"EnumConstantDecl 0x[0-9a-f]+ .*? (\w+) '[^']*'(?::'[^']*')?$")
TYPEDEF = re.compile(r"TypedefDecl 0x[0-9a-f]+ .*? (\w+) '([^']*)'(?::'([^']*)')?$")


DECLARED_SCAN = set()  # functions the last scanned AST declares (top level)
DECLARED = set()       # functions the decomp's headers declare
SCRIPTS = set()        # the decomp's scripts (EvtWord arrays), by name
CODE_KEYS = set()      # the decomp's functions, by GameCube address (or REL, section, offset)
MOD_LISTED = set()     # every name the mod's symbol list has
ENUM_TYPES = set()     # enum type names (the decomp's and the mod's)


def scan_ast(text):
    """{enum name: (scoped, [(constant, value)])}, {typedef name: underlying},
    {every enum constant, named enum or not: its enum's name or None}."""
    enums, typedefs, consts = {}, {}, {}
    cur = None
    pending = None  # a constant whose value is yet to come (ConstantExpr below it)
    last_anon = None
    for ln in text.splitlines():
        body = ln.lstrip("|`- ")
        # newer clang appends attributes ("external-linkage") after the type
        body = re.sub(r"( [a-z]+(?:-[a-z]+)+)+$", "", body)
        if re.match(r"EnumDecl 0x[0-9a-f]+ <[^>]*> \S+$", body):
            # typedef enum { ... } Name: unnamed here, named by the typedef after it
            cur_name, cur = None, (False, [])
            last_anon = cur
            pending = None
            continue
        m = ENUM_DECL.match(body)
        if m:
            # a forward declaration has no constants: only a definition registers
            cur_name, cur = m.group(2), (bool(m.group(1)), [])
            pending = None
            continue
        m = ENUM_CONST.match(body)
        if m and cur is not None:
            if cur_name is not None and enums.get(cur_name) is not cur:
                enums[cur_name] = cur
            values = cur[1]
            values.append([m.group(1), values[-1][1] + 1 if values else 0])
            consts.setdefault(m.group(1), cur_name)
            pending = values[-1]
            continue
        m = re.match(r"value: Int (-?\d+)", body)
        if m and pending is not None:
            pending[1] = int(m.group(1))
            pending = None
            continue
        m = re.match(r"FunctionDecl 0x[0-9a-f]+ .*? (\w+) '[^']*'", body)
        if m and not ln.startswith(("| ", "  ")):
            DECLARED_SCAN.add(m.group(1))
        m = TYPEDEF.match(body)
        if m:
            typedefs.setdefault(m.group(1), m.group(3) or m.group(2))
            if last_anon is not None and m.group(2) == f"enum {m.group(1)}" and m.group(1) not in enums:
                enums[m.group(1)] = last_anon
                for c, _ in last_anon[1]:
                    consts[c] = m.group(1)
            last_anon = None
        if re.match(r"^[|`]-(?!EnumConstantDecl)", ln):
            cur = cur if body.startswith("EnumConstantDecl") else None
    return enums, typedefs, consts


def decomp_ast(version, tmp):
    mod_fields.gb.VERSION_ARGS[:] = mod_fields.portversion.defines(version)[:2]
    probe = os.path.join(tmp, "decomp_ast.c")
    with open(probe, "w") as f:
        for p in sorted(glob.glob(os.path.join(ROOT, "include", "**", "*.h"), recursive=True)):
            f.write(f'#include "{os.path.relpath(p, os.path.join(ROOT, "include")).replace(os.sep, "/")}"\n')
        f.write('#include "dolphin/gx.h"\n#include "musyx/musyx.h"\n')
    args = [*mod_fields.gb.NATIVE_ARGS, "-Ilibs/musyx/include", *mod_fields.gb.VERSION_ARGS]
    r = mod_fields.run([mod_fields.CLANG, "-fsyntax-only", "-w", *args, "-ferror-limit=0",
                        "-Xclang", "-ast-dump", probe], ROOT)
    out = scan_ast(r.stdout)
    # function-like names the headers define as macros (PSVECAdd -> C_VECAdd,
    # sincosf -> ttyd_sincosf): as good as declared
    m = mod_fields.run([mod_fields.CLANG, "-E", "-dM", "-w", *args, probe], ROOT)
    DECLARED_SCAN.update(re.findall(r"^#define (\w+)", m.stdout, re.M))
    return out


def game_all_functions(tmp):
    """The functions a C++ mod sees declared (ttyd_mod/game_all.h, as the US
    Aurora build compiles it): the game's, the SDK's, MusyX's."""
    import gen_game_all
    probe = os.path.join(tmp, "game_all.cpp")
    with open(probe, "w") as f:
        f.write('#include "ttyd_mod/game_all.h"\n')
    r = mod_fields.run([os.environ.get("CLANGXX", "clang++"), "-x", "c++", "-std=gnu++20", "-fsyntax-only", "-w",
                        *gen_game_all.flags(), "-Xclang", "-ast-dump", "-fno-color-diagnostics", probe], ROOT)
    out = set(re.findall(r"^[|` ]*[|`]-FunctionDecl 0x[0-9a-f]+ .*? (\w+) '[^']*'", r.stdout, re.M))
    # and the names that build defines as macros (Aurora's PSMTX44Copy ->
    # C_MTX44Copy), as good as declared
    m = mod_fields.run([os.environ.get("CLANGXX", "clang++"), "-x", "c++", "-std=gnu++20", "-E", "-dM", "-w",
                        *gen_game_all.flags(), probe], ROOT)
    return out | set(re.findall(r"^#define (\w+)", m.stdout, re.M))


def mod_ast(mod, includes, defines, tmp, headers):
    probe = os.path.join(tmp, "mod_ast.cpp")
    with open(probe, "w") as f:
        for h in headers:
            f.write(f'#include "{h}"\n')
    r = mod_fields.run([mod_fields.CLANG, "-x", "c++", "-std=gnu++23", "-fsyntax-only", "-w",
                        *[f"-I{os.path.join(mod, i)}" for i in includes], *[f"-D{d}" for d in defines],
                        *mod_fields.host_includes(), "-Xclang", "-ast-dump", probe], mod)
    return scan_ast(r.stdout)


def match_brace(text, start):
    """Index just past the brace block opening at text[start] == '{'."""
    depth = 0
    body_is_function = False
    i = start
    while i < len(text):
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i + 1
        elif c == "/" and text.startswith("//", i):
            i = text.find("\n", i)
            if i < 0:
                return len(text)
            continue
        elif c == "/" and text.startswith("/*", i):
            i = text.find("*/", i) + 1
        elif c in "\"'":
            j = i + 1
            while j < len(text) and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            i = j
        i += 1
    return len(text)


DECL = re.compile(r"^\s*(?:extern\s+)?(?P<type>[\w:<>\s\*&]+?)\s*(?P<ptr>[\*&\s]*)\b(?P<name>\w+)\s*(?P<arr>\[[^\]]*\])?\s*;")
FUNC = re.compile(r"\b(?P<name>\w+)\s*\([^;{]*\)\s*(?:noexcept)?\s*;")


def split_statements(body):
    """A block's statements (each on one line) and preprocessor lines, in
    order."""
    items, cur = [], ""
    # ttyd-tools' EVT_DEFINE_USER_FUNC(x) carries its own semicolon
    body = re.sub(r"\b(EVT_\w+\s*\([^()]*\))(?!\s*;)", r"\1;", body)
    depth = 0
    body_is_function = False
    for ln in body.splitlines():
        if ln.strip().startswith("#") and not cur.strip():
            items.append(ln.strip())
            continue
        # a statement ends at a ';' outside braces, or at the '}' closing a
        # function's body (an inline definition stays whole; a struct's or an
        # initializer's braces end at their ';')
        for ch in ln + " ":
            if ch == "{" and depth == 0:
                body_is_function = re.search(r"\)\s*(?:const|noexcept)?\s*$", cur) is not None
            cur += ch
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0 and body_is_function:
                    items.append(" ".join(cur.split()))
                    cur = ""
            elif ch == ";" and depth == 0:
                stmt = " ".join(cur.split())
                if stmt != ";":
                    items.append(stmt)
                cur = ""
    if cur.strip():
        items.append(" ".join(cur.split()))
    return items


# ttyd-tools' declaration macros (most GameCube mods use them)
EVT_MACRO = re.compile(r"^EVT_(DECLARE_USER_FUNC|DEFINE_USER_FUNC|DECLARE)\s*\(\s*(\w+)\s*(?:,\s*([^)]*))?\)\s*;?$")


def var_ref(name, dec, typ, arr):
    """A game variable as a reference bound by name at load (ttyd_mod/vars.h);
    the define renames the mod's uses (a parameter of the name with them)."""
    out = [f"#define {name} ttyd_var_{name}"]
    if arr and arr.strip("[] "):
        out.append(f'extern "C++" inline {typ} (&ttyd_var_{name}){arr} = ttydGameVar<{typ}{arr}>("{dec}");')
    elif arr:
        out.append(f'extern "C++" inline {typ}* const ttyd_var_{name} = &ttydGameVar<{typ}>("{dec}");')
    else:
        out.append(f'extern "C++" inline {typ}& ttyd_var_{name} = ttydGameVar<{typ}>("{dec}");')
    return out


class Names:
    """A mod's name for a game symbol -> (the decomp's name, the name the
    game's executable exports it under: a REL's symbols carry its prefix,
    rel_<rel>__name)."""

    def __init__(self, mod_syms, decomp, exports):
        self.mod_syms, (self.dec_by_addr, self.local_file), self.exports = mod_syms, decomp, exports

    def lookup(self, name):
        key = self.mod_syms.get(name)
        dec = (self.dec_by_addr.get(key) if key is not None else None) or name
        rel = key[0] if isinstance(key, tuple) else None
        link = dec
        if self.exports:
            def rel_too(n):
                return [n, f"rel_{rel}__{n}"] if rel else [n]
            cands = []
            src = self.local_file.get(key) if key is not None else None
            if src:
                # a file's static: the pointer to it the build exports
                # (CMakeLists.txt, ttyd_static_exports), before any global of
                # the same name elsewhere
                cands += rel_too(f"port_static__{re.sub(r'[^A-Za-z0-9_]', '_', src[:-2])}__{dec}")
            cands += rel_too(dec) + rel_too(name)
            link = next((c for c in cands if c in self.exports), None)
            if link is None and key is None:
                # not in the mod's list (a REL of its own held it: TTYDAP's
                # custom.rel of battle units): a REL's copy of the name
                link = self.rel_global(name)
            link = link or dec
        return dec, link

    def listed(self, name):
        """In the mod's symbol list: the game's (or a REL of the mod's own
        the port doesn't have), not a name one of its sources defines."""
        return name in self.mod_syms or name in MOD_LISTED

    def rel_global(self, name):
        if not hasattr(self, "_rel_globals"):
            self._rel_globals = {}
            for e in sorted(self.exports):
                m = re.match(r"rel_[a-z0-9]+__(?!port_static__)(\w+)$", e)
                if m:
                    self._rel_globals.setdefault(m.group(1), e)
        return self._rel_globals.get(name)


def convert_statement(s, names, notes, namespaced=False):
    """One declaration of an extern "C" block: what replaces it. In a
    namespace, a declaration that goes leaves `using ::name;`, so the mod's
    ns::name still finds the game's."""
    use = (lambda n: [f"using ::{n};"]) if namespaced else (lambda n: [])
    em = EVT_MACRO.match(s)
    if em:
        kind, name, count = em.groups()
        dec, link = names.lookup(name)
        if kind == "DECLARE" or dec in SCRIPTS:
            # (a script the mod declared as a user function, as it only ran it)
            # a game script: data, bound by name at load
            notes.append(f"evt {name}")
            return var_ref(name, link, "EvtWord", "[]")
        if "port_static__" in link:
            # a file's static user function: through the pointer the build
            # exports for it (a script takes the value, bound at load)
            notes.append(f"static user function {name}")
            return [f"EVT_DECLARE_USER_FUNC(ttyd_proto_{name}, {count});",
                    f"constexpr int32_t {name}_parameter_count = ({count});",
                    f'extern "C++" inline decltype(&ttyd_proto_{name}) const ttyd_fn_{name} = '
                    f'reinterpret_cast<decltype(&ttyd_proto_{name})>(ttydGameStatic("{link}"));',
                    f"#define {name} ttyd_fn_{name}"]
        if dec not in DECLARED:
            # the mod's own, or the game's without a decomp header (under
            # the name the game exports it by)
            return ([f"#define {name} {link}"] if link != name else []) + [s]
        out = [f"decltype({dec}) {link};"] if link != dec else []
        out += [f"#define {name} {link}"] if link != name else []
        out += use(link) if link == dec else []
        if kind == "DECLARE_USER_FUNC":
            # the declaration is the decomp's; USER_FUNC's count check stays
            out.append(f"constexpr int32_t {name}_parameter_count = ({count});")
        notes.append(f"evt {name}")
        return out
    fm = FUNC.search(s)
    # a prototype, not a function pointer variable (whose first parenthesis
    # holds *name); a parameter may still be a function pointer
    if fm and not re.match(r"^[^(]*\(\s*\*", s) and not s.startswith("typedef"):
        name = fm.group("name")
        dec, link = names.lookup(name)
        if dec in DECLARED:
            # the decomp's prototype; under its exported name if that differs
            out = [f"decltype({dec}) {link};"] if link != dec else use(link)
            return out + ([f"#define {name} {link} /* the game's name */"] if link != name else [])
        if "port_static__" in link:
            # a file's static function: through the pointer the build exports
            # for it, typed by the mod's prototype (under a scratch name)
            proto = re.sub(rf"\b{name}(\s*\()", rf"ttyd_proto_{name}\1", s, count=1)
            notes.append(f"static function {name}")
            # a function of the mod's name that calls through it (ns::name
            # works, as a macro wouldn't)
            return [proto,
                    f'extern "C++" inline decltype(&ttyd_proto_{name}) const ttyd_fn_{name} = '
                    f'reinterpret_cast<decltype(&ttyd_proto_{name})>(ttydGameStatic("{link}"));',
                    f'extern "C++" template <typename... A> inline decltype(auto) {name}(A&&... a) '
                    f'{{ return ttyd_fn_{name}(static_cast<A&&>(a)...); }}']
        if names.exports is not None and not names.listed(name) and link not in names.exports:
            return [s]  # the mod's own function (one of its sources defines it)
        # no decomp header declares it (the game declares it in its .c, or
        # doesn't have it): the mod's prototype, under the game's name
        proto = re.sub(rf"\b{name}(\s*\()", rf"{link}\1", s, count=1)
        # enum parameters as plain integers: the game's C takes ints, and
        # C++ won't turn one into an enum by itself
        if ENUM_TYPES:
            proto = re.sub(r"(?:\b\w+::)*\b(" + "|".join(map(re.escape, ENUM_TYPES)) + r")\b(?=\s*\w|\s*[,)])",
                           "int32_t", proto)
        notes.append(f"kept prototype {name}")
        return [proto] + ([f"#define {name} {link} /* the game's name */"] if link != name else [])
    dm = DECL.match(s)
    if dm and s.startswith("extern"):
        name = dm.group("name")
        dec, link = names.lookup(name)
        if names.exports is not None and not names.listed(name) and link not in names.exports:
            return [s]  # the mod's own variable (one of its sources defines it)
        typ = re.sub(r"\bextern\b", "", (dm.group("type") + " " + dm.group("ptr")).strip()).strip()
        if dec in SCRIPTS:
            # a game script, which the mod declared in 32-bit words: the PC's
            # script words (EvtWord), so an index into it is a command's
            typ = re.sub(r"\bu?int32_t\b", "EvtWord", typ)
        return var_ref(name, link, typ, dm.group("arr"))
    return [s]


def convert_declarations(text, names, notes):
    """A mod file's extern "C" declarations (not its copies of the game's
    headers): blocks of prototypes and variables only (a block with function
    bodies is the mod's own code), and single extern "C" lines."""
    spaces = [(m.start(), match_brace(text, m.end() - 1)) for m in re.finditer(r"\bnamespace\s+[\w:]+\s*\{", text)]
    pieces, pos = [], 0
    for m in re.finditer(r'extern\s+"C"\s*\{', text):
        if m.start() < pos:
            continue
        end = match_brace(text, m.end() - 1)
        body = re.sub(r"//[^\n]*|/\*.*?\*/", "", text[m.end():end - 1], flags=re.S)
        if "{" in body:
            continue
        inside = any(a < m.start() < b for a, b in spaces)
        out = []
        for item in split_statements(body):
            out += [item] if item.startswith("#") else convert_statement(item, names, notes, inside)
        pieces.append(text[pos:m.start()])
        pieces.append('extern "C"\n{\n' + "".join(f"    {o}\n" for o in out) + "}"
                      if any(not o.startswith("#") for o in out) else "".join(f"{o}\n" for o in out))
        pos = end
    pieces.append(text[pos:])
    text = "".join(pieces)

    def line(m):
        inside = any(a < m.start() < b for a, b in spaces)
        out = convert_statement(m.group(2).strip(), names, notes, inside)
        if out == [m.group(2).strip()]:
            return m.group(0)
        return m.group(1) + 'extern "C"\n{\n' + "".join(f"    {o}\n" for o in out) + "}" if out else ""
    return re.sub(r'^([ \t]*)extern\s+"C"\s+(?!\{)([^;{}\n]+;)', line, text, flags=re.M)


def convert_source_externs(text, names, mod_scripts, notes):
    """A mod source's own declarations of the game's variables, scripts and
    code (`extern T x;` lines, which convert_declarations' extern "C" blocks
    don't cover): each bound by name, under a name of the file's
    (ttyd_src_x; the file's unqualified uses after it are renamed, not a
    call of a function of the name nor a namespace's x). A script declared
    as 32-bit words is the PC's script words; a function's instructions (to
    patch) are words of nowhere (ttyd_mod/vars.h's TTYD_GC_CODE). A cast of
    a script to words is to script words."""
    scripts = SCRIPTS | mod_scripts
    bound = set()

    def decl(m):
        ind, name, arr = m.group("ind"), m.group("name"), re.sub(r"\s", "", m.group("arr"))
        typ = " ".join(m.group("type").split())
        dec, link = names.lookup(name)
        if not names.listed(name) and not (names.exports and link in names.exports):
            # the mod's own (another of its files defines it); its scripts
            # are the PC's script words
            if name in mod_scripts and re.fullmatch(r"(?:const )?u?int32_t", typ):
                return f"{ind}extern {typ.replace('uint32_t', 'EvtWord').replace('int32_t', 'EvtWord')} {name}{arr};"
            return m.group(0)
        if name in bound:
            return ""  # declared twice
        bound.add(name)
        notes.append(f"source extern {name}")
        if names.mod_syms.get(name) in CODE_KEYS:
            return f"{ind}static int32_t* const ttyd_src_{name} = TTYD_GC_CODE(int32_t*); /* {name}'s code */"
        if dec in SCRIPTS:
            typ = re.sub(r"\bu?int32_t\b", "EvtWord", typ)
        if arr == "":
            return f'{ind}static {typ}& ttyd_src_{name} = ttydGameVar<{typ}>("{link}");'
        if arr == "[]":
            return f'{ind}static {typ}* const ttyd_src_{name} = &ttydGameVar<{typ}>("{link}");'
        return f'{ind}static {typ} (&ttyd_src_{name}){arr} = ttydGameVar<{typ}{arr}>("{link}");'
    decl_re = re.compile(r'^(?P<ind>[ \t]*)extern\s+(?:"C"\s+)?(?P<type>(?:const\s+)?[\w:]+(?:\s*\*)*)\s*'
                         r'(?<=[\s*])(?P<name>\w+)\s*(?P<arr>(?:\[[^\]\n]*\]\s*)*);', re.M)
    text = decl_re.sub(decl, text)
    # a user function's arguments the mod built itself (evt->args = args, a
    # saved evt->args put back): script words, as the entry's args are
    built = set(re.findall(r"->\s*(?:args|evtArguments)\s*=\s*&?\s*(\w+)\s*;", text))
    built |= set(re.findall(r"(\w+)\s*=\s*[\w.\[\]]+->\s*(?:args|evtArguments)\s*;", text))
    if built:
        text = re.sub(r"\bu?int32_t(\s*\*?\s*(?:const\s+)?(?:" + "|".join(sorted(built)) + r")\b(?=\s*[\[=;]))",
                      r"EvtWord\1", text)
    # a game function cast to its instruction words, to patch them: the same
    def code_cast(m):
        key = names.mod_syms.get(m.group(2))
        return f"TTYD_GC_CODE({m.group(1)})" if key in CODE_KEYS else m.group(0)
    text = re.sub(r"\(\s*((?:const\s+)?u?int32_t\s*\*)\s*\)\s*&?\s*(?:\w+::)*(\w+)\b(?!\s*[(\[.])", code_cast, text)
    for name in bound:
        at = text.index(f"ttyd_src_{name}")
        at = text.index("\n", at) if "\n" in text[at:] else len(text)
        text = text[:at] + re.sub(rf"(?<![\w:.>\"]){name}\b(?!\s*\()", f"ttyd_src_{name}", text[at:])
    return re.sub(r"\b(const_cast|reinterpret_cast)<\s*(const\s+)?u?int32_t\s*\*\s*>(\(\s*(?:[\w:]+::)?(\w+)\s*\))",
                  lambda m: f"{m.group(1)}<{m.group(2) or ''}EvtWord *>{m.group(3)}" if m.group(4) in scripts
                  else m.group(0), text)


def convert(text, decomp_records, aliases, names, notes, enum_repl, enum_ns=None):
    # 0. the mod's enums the decomp also has: the decomp's (their constants
    # were rewritten in every file, see main); enum_repl says what stands in
    for name, repl in enum_repl.items():
        for m in list(re.finditer(rf"^\s*enum\s+(?:class\s+)?{name}\b[^;{{]*\{{", text, re.M))[::-1]:
            end = match_brace(text, m.end() - 1)
            tail = re.match(r"\s*;", text[end:])
            spaces = [(n.start(), match_brace(text, n.end() - 1)) for n in re.finditer(r"\bnamespace\s+[\w:]+\s*\{", text)]
            inside = any(a < m.start() < b for a, b in spaces)
            here = enum_ns[name] if enum_ns and name in enum_ns and inside else repl
            text = text[:m.start()] + "\n" + here + text[end + (tail.end() if tail else 0):]
            notes.append(f"enum {name}")
        text = re.sub(rf"^\s*enum\s+(?:class\s+)?{name}\b[^;{{]*;\s*$", "", text, flags=re.M)

    # 0b. the mod's other scoped enums: plain enums inside a struct, so
    # X::kName still works and a value passes to the game's int parameters
    # (an enum class doesn't convert by itself)
    for m in list(re.finditer(r"^(?P<ind>[ \t]*)enum\s+class\s+(?P<name>\w+)\s*(?P<base>:\s*[\w:]+)?\s*\{", text, re.M))[::-1]:
        end = match_brace(text, m.end() - 1)
        tail = re.match(r"\s*;", text[end:])
        name, base = m.group("name"), m.group("base") or ""
        body = text[m.end() - 1:end]
        text = (text[:m.start()] + f"{m.group('ind')}struct {name}_scope {{ enum type {base} {body}; }};\n"
                f"{m.group('ind')}using {name} = {name}_scope::type;" + text[end + (tail.end() if tail else 0):])
        notes.append(f"unscoped {name}")
    text = re.sub(r"^\s*enum\s+class\s+\w+\s*(:\s*[\w:]+)?\s*;\s*$", "", text, flags=re.M)

    # 1. extern "C" blocks: variables -> references bound at load, functions
    # -> the decomp's; preprocessor lines stay where they are
    pos = 0
    pieces = []
    namespaces = [(m.start(), match_brace(text, m.end() - 1))
                  for m in re.finditer(r"\bnamespace\s+[\w:]+\s*\{", text)]
    for m in re.finditer(r'extern\s+"C"\s*\{', text):
        if m.start() < pos:
            continue
        end = match_brace(text, m.end() - 1)
        body = re.sub(r"//[^\n]*|/\*.*?\*/", "", text[m.end():end - 1], flags=re.S)
        inside = any(a < m.start() < b for a, b in namespaces)
        out = []
        for item in split_statements(body):
            if item.startswith("#"):
                out.append(item)
            else:
                out += convert_statement(item, names, notes, inside)
        pieces.append(text[pos:m.start()])
        if any(not o.startswith("#") for o in out):
            pieces.append('extern "C"\n{\n' + "".join(f"    {o}\n" for o in out) + "}")
        else:
            pieces.append("".join(f"{o}\n" for o in out))
        pos = end
    pieces.append(text[pos:])
    text = "".join(pieces)

    # 1b. the mod's own inline copies of the game's or the SDK's functions
    # (GXPosition3f32 writing the GameCube's FIFO register): the real ones
    pieces, pos = [], 0
    for m in re.finditer(r"^[ \t]*(?:INLINE_FUNC|static\s+inline|inline)\s+[\w\s\*&:<>,]*?\b(\w+)\s*\([^;{)]*\)\s*\{",
                         text, re.M):
        if m.start() < pos or m.group(1) not in DECLARED:
            continue
        end = match_brace(text, m.end() - 1)
        pieces += [text[pos:m.start()], f"/* {m.group(1)}: the game's */\n"]
        pos = end
        notes.append(f"inline {m.group(1)}")
    text = "".join(pieces) + text[pos:]

    # 2. the game's structs: the decomp's definition
    def struct_sub(m):
        name = m.group("name")
        dec = aliases.get(name, name)
        if dec not in decomp_records:
            return None
        return dec
    result = []
    pos = 0
    namespaces = [(m.start(), match_brace(text, m.end() - 1))
                  for m in re.finditer(r"\bnamespace\s+[\w:]+\s*\{", text)]
    replaced = set()
    for m in re.finditer(r"^[ \t]*(?P<td>typedef\s+)?struct\s+(?P<name>\w+)\s*\{", text, re.M):
        if m.start() < pos:
            continue
        dec = struct_sub(m)
        if dec is None:
            continue
        end = match_brace(text, m.end() - 1)
        tail = re.match(r"\s*(?:__attribute__\s*\(\(.*?\)\)\s*)?\w*\s*;", text[end:])
        end += tail.end() if tail else 0
        result.append(text[pos:m.start()])
        name = m.group("name")
        # in a namespace, the name stays there (ns::Name still names it)
        if any(a < m.start() < b for a, b in namespaces) or dec != name:
            result.append(f"using {name} = ::{dec}; /* the decomp's */")
        else:
            result.append(f"/* {name}: the decomp's */")
        pos = end
        replaced.add(name)
        notes.append(f"struct {name} -> {dec}")
    result.append(text[pos:])
    text = "".join(result)
    # forward declarations of the replaced structs (and the aliased ones)
    # would declare other types of the same names
    for name in replaced | {n for n, d in aliases.items() if d in decomp_records}:
        text = re.sub(rf"^\s*struct\s+{name}\s*;\s*$", "", text, flags=re.M)


    # 4. the decomp's headers first, and the definitions after them
    m = re.search(r"^#define \w+_H\w*\s*$", text, re.M)
    at = m.end() if m else 0
    head = '\n#include "ttyd_mod/game_all.h"\n#include "ttyd_mod/vars.h"\n'
    return text[:at] + head + text[at:]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mod", required=True)
    ap.add_argument("--headers", action="append", required=True)
    ap.add_argument("--lst", required=True)
    ap.add_argument("--version", default="G8ME01")
    ap.add_argument("--alias", action="append", default=[])
    ap.add_argument("--include", action="append", default=None)
    ap.add_argument("--define", action="append", default=[])
    ap.add_argument("--sources", default="source/**/*.cpp")
    ap.add_argument("--rel-dir", help="the disc's files/rel (REL module ids for the mod's symbol list)")
    ap.add_argument("--exports", help="the game's export list (build/generated/ttyd_exports.def): the names "
                                      "its symbols link by (a REL's carry a prefix)")
    ap.add_argument("--gc-data", action="store_true",
                    help="fixed GameCube addresses of the game's data as the PC's (TTYD_GC_DATA; needs --disc)")
    ap.add_argument("--disc", default=None, help="the extracted disc, default orig/<version> (for --gc-data)")
    ap.add_argument("--gc-lowmem", action="store_true",
                    help="fixed GameCube low-memory addresses (0x800000F8-0x80005000) as TTYD_GC_ADDR")
    ap.add_argument("--widen-pointer-casts", action="store_true",
                    help="pointer arithmetic in uint32_t (reinterpret_cast<uint32_t>(ptr)) in uintptr_t")
    ap.add_argument("--evt-header", action="append", default=[],
                    help="the mod's copy of ttyd-tools' evt_cmd.h (script words become EvtWord)")
    a = ap.parse_args()
    mod = os.path.abspath(a.mod)
    evt_headers = {os.path.normcase(os.path.abspath(os.path.join(mod, h))) for h in a.evt_header}
    disc = a.disc or os.path.join(ROOT, "orig", a.version)
    gc_data_cache = {}
    aliases = dict(x.split("=", 1) for x in a.alias)
    # (normalized: a glob keeps the pattern's separators, and a file must be one path)
    files = sorted({os.path.normpath(p) for pat in a.headers for p in glob.glob(os.path.join(mod, pat), recursive=True)})
    with tempfile.TemporaryDirectory() as tmp:
        records = mod_fields.pc_records(a.version, tmp)
        DECLARED_SCAN.clear()
        dec_enums, dec_typedefs, dec_consts_all = decomp_ast(a.version, tmp)
        DECLARED.update(DECLARED_SCAN)
        DECLARED.update(game_all_functions(tmp))
        for src in (glob.glob(os.path.join(ROOT, "src", "**", "*.c"), recursive=True)
                    + glob.glob(os.path.join(ROOT, "rels", "**", "*.c"), recursive=True)):
            text = open(src, encoding="utf-8", errors="replace").read()
            SCRIPTS.update(re.findall(r"\bEVT_(?:BEGIN|DEF)\(\s*(\w+)\s*\)", text))
            SCRIPTS.update(re.findall(r"\bEvtWord\s+\(?(\w+)\)?\s*\[", text))
        mod_enums, _, _ = mod_ast(mod, a.include or ["include"], a.define, tmp, files)
    rel_ids = rel_module_ids(a.rel_dir) if a.rel_dir else {}
    names = Names(mod_symbols(os.path.join(mod, a.lst), rel_ids), decomp_symbols(a.version),
                  read_exports(a.exports) if a.exports else None)
    # records: the decomp's struct names as the PC build's headers have them
    # (a mod builds against those, not the GameCube SDK's)

    # scoped enums of the mod the decomp has too: each constant to the
    # decomp's constant of the same value, or a cast
    enum_map = {}
    enum_repl = {}
    plain = {}  # unscoped constants named differently: mod name -> decomp's
    unscoped_names = set()  # the mod's unscoped enums that became another type
    enum_ns = {}            # what stands in for an enum inside a namespace
    for name, (scoped, consts) in mod_enums.items():
        target = aliases.get(name, name)
        tag = target if target in dec_enums else re.sub(r"^enum\s+", "", dec_typedefs.get(target, ""))
        if tag not in dec_enums:
            continue
        by_value = {}
        dec_consts = set()
        for c, v in dec_enums[tag][1]:
            by_value.setdefault(v, c)
            dec_consts.add(c)
        enum_map[name] = {c: by_value.get(v, f"(({tag}){v})") for c, v in consts}
        enum_repl[name] = (f"/* enum {name}: the decomp's */" if tag == name
                           else f"using {name} = {tag}; /* the decomp's */")
        # in a namespace: the type and the decomp's constants there too
        enum_ns[name] = "\n".join([f"using {name} = ::{tag}; /* the decomp's */"]
                                  + [f"using ::{c};" for c in sorted(dec_consts)])
        if not scoped:
            # used bare: rename only the ones the decomp names differently
            plain.update({c: t for c, t in enum_map[name].items() if c not in dec_consts})
    # unscoped enums of the mod whose constants the decomp already has under
    # another enum's name (or a nameless one): the mod's name becomes that
    # type, and only the constants the decomp lacks stay
    for name, (scoped, consts) in mod_enums.items():
        if name in enum_map or scoped or not any(c in dec_consts_all for c, _ in consts):
            continue
        owner = next((dec_consts_all[c] for c, _ in consts if dec_consts_all.get(c)), None)
        lines = [f"using {name} = {owner}; /* the decomp's */" if owner else f"typedef int {name}; /* the decomp's constants */"]
        lines += [f"static constexpr int {c} = {v};" for c, v in consts if c not in dec_consts_all]
        enum_repl[name] = "\n".join(lines)
        enum_ns[name] = "\n".join([lines[0].replace(f"= {owner};", f"= ::{owner};") if owner else lines[0]]
                                  + [f"using ::{c};" for c, _ in consts if c in dec_consts_all] + lines[1:])
        unscoped_names.add(name)
    ENUM_TYPES.update(dec_enums, mod_enums, {t for t, u in dec_typedefs.items() if u.startswith('enum ')})
    sources = sorted(os.path.normpath(p) for p in glob.glob(os.path.join(mod, a.sources), recursive=True))
    every_header = {os.path.normpath(p) for inc in (a.include or ["include"])
                    for p in glob.glob(os.path.join(mod, inc, "**", "*.h"), recursive=True)}
    # the game's structs the mod's copies of its headers define (the
    # decomp's replace them): their forward declarations, wherever they are
    game_structs = set()
    for path in files:
        game_structs |= set(re.findall(r"\bstruct\s+(\w+)\s*\{", open(path, encoding="utf-8", errors="replace").read()))
    # and the ones it only declares, under a name the conversion maps (--alias)
    for path in set(files) | set(sources) | every_header:
        game_structs |= {n for n in re.findall(r"\bstruct\s+(\w+)\s*;", open(path, encoding="utf-8", errors="replace").read())
                         if n in aliases}
    game_structs = {n for n in game_structs if aliases.get(n, n) in records}
    record_names_re = re.compile(rb"\bstruct\s+(" + "|".join(sorted(map(re.escape, game_structs))).encode()
                                 + rb")\s*;") if game_structs else None
    notes_all = []
    # the mod's own scripts (PC script words, as its EVT_BEGIN defines them)
    mod_scripts = set()
    # the mod's namespaces named as a game type (its namespace BattleUnitType
    # of constants, where the decomp has enum BattleUnitType): renamed, as a
    # using-directive would make the name ambiguous
    ns_clash = set()
    for path in set(sources) | every_header:
        text = open(path, encoding="utf-8", errors="replace").read()
        mod_scripts |= set(re.findall(r"\bEVT_BEGIN\w*\(\s*(\w+)", text))
        ns_clash |= {n for n in re.findall(r"\bnamespace\s+(\w+)\s*\{", text)
                     if n in records or n in dec_enums or n in dec_typedefs}
    ns_names = "|".join(sorted(ns_clash)).encode()
    ns_clash_re = re.compile(rb"(\bnamespace\s+)(" + ns_names + rb")\b|(?<![.>])\b(" + ns_names
                             + rb")(?=\s*::)") if ns_clash else None
    for path in sorted(set(files) | set(sources) | every_header):
        raw = open(path, "rb").read()
        # GameCube size checks (a PC layout differs: pointers are 8 bytes)
        new = re.sub(rb"^[ \t]*static_assert\s*\(\s*sizeof\b.*?\);[^\n]*$", b"", raw, flags=re.M | re.S)
        # a forward declaration of a struct the decomp has (in a namespace, in
        # any file of the mod): the decomp's, not another type of that name
        # the mod's own declarations of the game's functions and variables,
        # outside its copies of the game's headers (an extern "C" block of
        # prototypes in a source, one extern "C" line): as in those headers
        if path not in files:
            new = convert_declarations(new.decode("utf-8", errors="surrogateescape"), names,
                                       notes_all).encode("utf-8", errors="surrogateescape")
            new = convert_source_externs(new.decode("utf-8", errors="surrogateescape"), names, mod_scripts,
                                      notes_all).encode("utf-8", errors="surrogateescape")
        if ns_clash_re is not None:
            new = ns_clash_re.sub(lambda m: (m.group(1) + m.group(2) if m.group(2) else m.group(3)) + b"_ns", new)
        if record_names_re is not None:
            new = record_names_re.sub(
                lambda m: f"using {m.group(1).decode()} = ::{aliases.get(m.group(1).decode(), m.group(1).decode())};".encode(),
                new)
        if a.widen_pointer_casts:
            # a pointer held in a 32-bit integer (the GameCube's width) for
            # arithmetic: the PC's width (uintptr_t), and the variable too
            new = re.sub(rb"\b(?:const\s+)?uint32_t(\s+\w+\s*=\s*reinterpret_cast<)uint32_t>",
                         lambda m: m.group(0).replace(b"uint32_t", b"uintptr_t"), new)
            new = re.sub(rb"reinterpret_cast<uint32_t>\(", b"reinterpret_cast<uintptr_t>(", new)
            # a pointer to bytes from what the mod declared a void* (the game's
            # function returned one): the decomp's typed pointer needs a reinterpret
            new = re.sub(rb"static_cast<((?:const\s+)?(?:u?int8_t|char|unsigned char)\s*\*)>\(", rb"reinterpret_cast<\1>(", new)
            # a word's bytes passed as an argument (a 0xRRGGBBAA color for a
            # GXColor): in the GameCube's byte order (ttyd_mod/loose.h)
            new = re.sub(rb"^(?![^\n]*\breturn\b)([^\n]*?)reinterpret_cast<(?:const\s+)?uint8_t\s*\*>\(&([^()\n]+)\)",
                         rb"\1ttydBytesBE(\2)", new, flags=re.M)
        if a.gc_lowmem:
            # fixed GameCube low-memory addresses (a patcher's settings, a
            # mailbox): the port's stand-in for that memory (ttyd_mod/vars.h)
            def lowmem(m):
                v = int(m.group(1), 16)
                return b"TTYD_GC_ADDR(" + m.group(1) + b")" if 0x800000F8 <= v < 0x80005000 else m.group(0)
            new = re.sub(rb"\b(?:constexpr\s+)?(?:const\s+)?u?int(?:32|ptr)_t(\s+\w+\s*=\s*)(0x8000[0-4][0-9A-Fa-f]{3})\b",
                         lambda m: b"const uintptr_t" + m.group(1) + m.group(2)
                         if 0x800000F8 <= int(m.group(2), 16) < 0x80005000 else m.group(0), new)
            new = re.sub(rb"(?<!\w)(?<!TTYD_GC_ADDR\()(0x8000[0-4][0-9A-Fa-f]{3})\b", lowmem, new)
        if a.gc_data:
            # fixed GameCube addresses of the game's data (0x803DB890): the
            # same field on the PC (tools/port/gc_offsets.py)
            def data(m):
                v = int(m.group(1), 16)
                if not 0x80005000 <= v < 0x81800000:
                    return m.group(0)
                t = gc_data_cache.get(v)
                if t is None:
                    try:
                        t = gc_offsets.translate_address(a.version, disc, v)
                    except gc_offsets.Error:
                        t = {}
                    gc_data_cache[v] = t
                if t.get("kind") not in ("data", "script"):
                    return m.group(0)
                name, via = t["export"]
                return f'TTYD_GC_DATA("{name}", 0x{t["pc_offset"]:X}, {"true" if via else "false"})'.encode()
            new = re.sub(rb"\b(?:constexpr\s+)?(?:const\s+)?u?int(?:32|ptr)_t(\s+\w+\s*=\s*)(0x8[01][0-9A-Fa-f]{6})\b",
                         lambda m: b"const uintptr_t" + m.group(1) + data(re.match(rb"(.*)", m.group(2)))
                         if data(re.match(rb"(.*)", m.group(2))) != m.group(2) else m.group(0), new)
            new = re.sub(rb"(?<![\w\"])(0x8[01][0-9A-Fa-f]{6})\b", data, new)
        if os.path.normcase(os.path.abspath(path)) in evt_headers:
            # ttyd-tools' script macros: the PC game's script words (EvtWord,
            # which holds a pointer) in place of int32_t
            new = re.sub(rb"reinterpret_cast<int32_t>", b"(EvtWord)", new)
            new = re.sub(rb"\bint32_t\b", b"EvtWord", new)
            if b"ttyd_mod/game_all.h" not in new:
                new = re.sub(rb"(#define \w+_H\w*[ \t]*\r?\n)", rb'\1#include "ttyd_mod/game_all.h"\n', new, count=1)
        for name, consts in enum_map.items():
            new = re.sub(rb"\b" + name.encode() + rb"::(\w+)\b",
                         lambda m, consts=consts: consts.get(m.group(1).decode(), m.group(0).decode()).encode(), new)
        if plain:
            new = re.sub(rb"\b(" + "|".join(map(re.escape, plain)).encode() + rb")\b",
                         lambda m: plain[m.group(1).decode()].encode(), new)
        if unscoped_names:
            # Enum::kName of an enum that is now another type (or an int):
            # the constant is a plain name
            new = re.sub(rb"\b(?:" + "|".join(map(re.escape, unscoped_names)).encode() + rb")::(\w+)\b", rb"\1", new)
        # `struct Mod` where Mod is now an alias of the decomp's struct
        struct_aliases = [n for n, d in aliases.items() if d in records and n != d]
        if struct_aliases:
            new = re.sub(rb"\bstruct\s+(" + "|".join(map(re.escape, struct_aliases)).encode() + rb")\b(?!\s*[{;:])",
                         rb"\1", new)
        if new != raw:
            open(path, "wb").write(new)
    print(f"enums mapped onto the decomp's: {len(enum_map)} ({', '.join(sorted(enum_map)[:12])}...)")
    for path in files:
        raw = open(path, "rb").read().decode("utf-8", errors="replace")
        notes = []
        new = convert(raw, records, aliases, names, notes, enum_repl, enum_ns)
        if new != raw:
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write(new)
        print(f"{os.path.relpath(path, mod)}: {len([n for n in notes if n.startswith('struct')])} structs, "
              f"{len([n for n in notes if n.startswith('kept')])} kept prototypes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
