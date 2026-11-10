"""The game's fonts against the port's text drawn with them: no letter a font lacks.

The US and EU releases' font (f/papermarioset_US.bfn, _EU.bfn: the same map) and the JP release's
(f/papermarioset_JPN.bfn, gen_lang.py's JA_FONT) have no '<' or '>' (a message tag's marks) and the US/EU
one lacks a few Latin-1 letters too: a letter a font lacks is drawn as nothing, as "Settings > Updates"
was in a refusal's note ("Settings  Updates"). The port's text is made the font's on its way in
(tr.cpp's latin1, jaFont and asciiFold; gen_lang.py's game_bytes and ja_font_bytes): a stray '<' '>' as
« » (US/EU) or ← → (JP), the lacking letters as the look-alikes of gen_lang.py's LATIN_LACKS. This checks:

- every translation's game-font text (lang_tables.inc's third column, as gen_lang.py makes it): each
  letter outside the message tags is in its release's font;
- every string literal in the C sources that draw with the game's font (port/src/aurora, port/src/common)
  and every literal given to portTrGame anywhere: once folded as the English is, no letter the US/EU
  font can't show (a character past Latin-1, as › → “ ” ✓, would make the whole text fall back to its
  UTF-8 bytes).

  python tools/port/font_chars.py      (exit 1 and the findings; testpass.py lint runs it too)
"""
import glob
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_lang  # noqa: E402

# the US/EU font's MAP1 codes in 0x20..0xFF (read from the discs' papermarioset_US.bfn and _EU.bfn: the same)
US_LACKS = {0x3C, 0x3E, 0x7F, 0xA0, 0xA2, 0xA3, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAC, 0xAD, 0xAE, 0xAF, 0xB0, 0xB1,
            0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xC3, 0xC5, 0xC6, 0xD5, 0xDD, 0xE3, 0xE5, 0xE6, 0xF0, 0xF5, 0xF7,
            0xF8, 0xFD, 0xFE, 0xFF} | set(range(0x80, 0xA0))
US_FONT = {c for c in range(0x20, 0x100) if c not in US_LACKS} | {0x0A}

LITERAL = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
TR_GAME = re.compile(r'\bportTrGame(?:NoPlayAs)?\(\s*((?:"(?:[^"\\\n]|\\.)*"\s*)+)\)')
COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
INCLUDE = re.compile(r"^\s*#\s*include[^\n]*$", re.M)


def unescape(s):
    """a C literal's text (UTF-8 source) as Python text; None when it isn't text (bytes past UTF-8)"""
    out = bytearray()
    i = 0
    raw = s.encode("utf-8")
    while i < len(raw):
        c = raw[i]
        if c == 0x5C and i + 1 < len(raw):
            n = raw[i + 1]
            simple = {ord("n"): 10, ord("t"): 9, ord("r"): 13, ord("0"): 0, ord("\\"): 0x5C, ord('"'): 0x22,
                      ord("'"): 0x27, ord("a"): 7}
            if n == ord("x"):
                m = re.match(rb"[0-9A-Fa-f]{1,2}", raw[i + 2:])
                out.append(int(m.group(0), 16) if m else 0)
                i += 2 + (len(m.group(0)) if m else 0)
                continue
            if ord("0") <= n <= ord("7"):
                m = re.match(rb"[0-7]{1,3}", raw[i + 1:])
                out.append(int(m.group(0), 8) & 0xFF)
                i += 1 + len(m.group(0))
                continue
            out.append(simple.get(n, n))
            i += 2
            continue
        out.append(c)
        i += 1
    try:
        return out.decode("utf-8")
    except UnicodeDecodeError:
        return None  # (bytes in the game's encoding already: Shift-JIS, Latin-1)


def lacking_us(text):
    """the letters of text (UTF-8, as the English is) the US/EU font can't show once folded (tr.cpp's latin1)"""
    folded = gen_lang.fold_marks(text, False)
    bad = set()
    for c in gen_lang.TAG.sub("", folded):
        f = gen_lang.LATIN_LACKS.get(c, gen_lang.LATIN_FOLD.get(c, c))
        for d in f:
            if ord(d) > 0xFF or ord(d) not in US_FONT:
                bad.add(c)
    return bad


def table_problems():
    out = []
    for code in gen_lang.LANGS:
        pairs = gen_lang.read(code)
        game_text = gen_lang.read("ja_game") if code == "ja" else {}
        for en, tr in pairs.items():
            if code == "ja":
                g = gen_lang.ja_font_bytes(game_text.get(en, tr))
                if g is not None:
                    shown = gen_lang.TAG.sub("", gen_lang.fold_marks(g.decode("cp932"), True))
                    bad = {c for c in shown if c not in gen_lang.JA_FONT}
                    if bad:
                        out.append(f"ja: {en[:60]!r}: the JP font has no {''.join(sorted(bad))}")
                continue
            g = gen_lang.game_bytes(code, tr)
            if g is None:
                continue  # (the menus' only: the game's font shows the English)
            shown = gen_lang.TAG.sub("", g.decode("latin-1"))
            bad = {c for c in shown if ord(c) not in US_FONT}
            if bad:
                out.append(f"{code}: {en[:60]!r}: the US/EU font has no {ascii(''.join(sorted(bad)))}")
    return out


def source_problems():
    out = []
    files = []
    for d in ("port/src/aurora", "port/src/common"):
        for e in ("*.c", "*.inc"):
            files += glob.glob(os.path.join(REPO, d, e))
    drawn = set(files)
    for d in ("port/src", "port/settings"):
        files += glob.glob(os.path.join(REPO, d, "**", "*.c"), recursive=True)
        files += glob.glob(os.path.join(REPO, d, "**", "*.cpp"), recursive=True)
    for path in sorted(set(files)):
        text = open(path, encoding="utf-8", errors="replace").read()
        code = INCLUDE.sub("", COMMENT.sub(lambda m: re.sub(r"[^\n]", " ", m.group(0)), text))
        if path in drawn:
            literals = [(m.start(), m.group(1)) for m in LITERAL.finditer(code)]
        else:
            literals = []
            for m in TR_GAME.finditer(code):
                literals += [(m.start(), x) for x in LITERAL.findall(m.group(1))]
        for at, lit in literals:
            s = unescape(lit)
            if s is None or all(ord(c) < 0x80 and c not in "<>" for c in s):
                continue
            # (control codes are line ends, data files' and logs'; '<' '>' given to portTrGame are folded,
            # elsewhere they're a file format's or a log's far more often than drawn text's)
            if any(0x3000 <= ord(c) <= 0x9FFF or 0xFF00 <= ord(c) <= 0xFFEF for c in s):
                # Japanese: the JP release's text (its font's letters)
                shown = gen_lang.TAG.sub("", gen_lang.fold_marks(s, True))
                bad = {c for c in shown if c not in gen_lang.JA_FONT and ord(c) >= 0x80}
            else:
                bad = {c for c in lacking_us(s) if ord(c) >= 0x80}
            if bad:
                line = code.count("\n", 0, at) + 1
                out.append(f"{os.path.relpath(path, REPO)}:{line}: the game's font has no "
                           f"{ascii(''.join(sorted(bad)))} in {s[:60]!r}")
    return out


def problems():
    found = table_problems() + source_problems()
    if not found:
        return ""
    return "; ".join(found[:12]) + (f" (and {len(found) - 12} more)" if len(found) > 12 else "")


def main():
    found = table_problems() + source_problems()
    for f in found:
        print(ascii(f)[1:-1])
    print(f"font_chars: {len(found)} problem(s)" if found else "font_chars: ok")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
