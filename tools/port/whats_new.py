#!/usr/bin/env python3
"""The in-game "what's new" page's text (port/include/port/whats_new.h), made
at build time from the release notes (docs/release_notes/<version>.md).

  whats_new.py NOTES_DIR BUILD_VERSION OUT.c

The notes are those of BUILD_VERSION (TTYD_PORT_VERSION) when there are
some, otherwise the newest (a dev build shows what the next release will
say so far). Of them, only "What's new" (or "What's in it") and "Fixed":
each *subheading* a heading, each bullet one compact line, its bold name and
the first part of its first sentence (no Downloads table, no known issues,
no links). Each line is kept three ways: UTF-8 (the settings page), Latin-1
(the US and EU releases' font) and plain ASCII (the JP release's font, whose
'<', '>', '\\' and '`' are a tag's marks).
"""
import os
import re
import sys
import unicodedata

KEEP = ("what's new", "what's in it", "fixed")
MAX_ITEM = 110  # characters of a compact line, about two lines of the window

# Latin-1 has these; the rest of the typography is folded
FOLD = {"‘": "'", "’": "'", "“": '"', "”": '"', "–": "-", "—": "-",
        "…": "...", "→": "->", "←": "<-", " ": " ", "−": "-", "•": "-"}


def version_key(name):
    return tuple(int(n) for n in re.findall(r"\d+", name))


def pick(notes_dir, build_version):
    own = os.path.join(notes_dir, build_version + ".md")
    if os.path.exists(own):
        return build_version, own
    names = [f[:-3] for f in os.listdir(notes_dir) if re.fullmatch(r"\d+(\.\d+)*\.md", f)]
    if not names:
        return build_version, None
    newest = max(names, key=version_key)
    return newest, os.path.join(notes_dir, newest + ".md")


def plain(text):
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)  # [text](link)
    text = text.replace("\\|", "|").replace("**", "").replace("`", "")
    text = re.sub(r"(?<![\w*])\*([^*]+)\*(?![\w*])", r"\1", text)  # *emphasis*
    return re.sub(r"\s+", " ", text).strip()


def compact(bullet):
    """A bullet's first part: up to the end of its first sentence or clause
    (not inside brackets), at most MAX_ITEM characters."""
    text = plain(bullet)
    depth, cut = 0, len(text)
    for i, c in enumerate(text):
        if c in "([":
            depth += 1
        elif c in ")]":
            depth = max(0, depth - 1)
        elif depth == 0 and i >= 24 and (c == ";" or (c == "." and (i + 1 == len(text) or text[i + 1] == " "))):
            cut = i + (1 if c == "." else 0)
            break
    text = text[:cut].rstrip(" ,;:")
    if len(text) > MAX_ITEM:
        text = text[:MAX_ITEM - 3]
        # at the last clause's end when it leaves most of the line ("..., two players on one
        # screen...", not "..., in the Glitz..."); else at a word
        clause = max(text.rfind(", "), text.rfind(": "), text.rfind(" ("), text.rfind(" - "))
        cut = clause if clause >= MAX_ITEM * 0.55 else text.rfind(" ")
        text = text[:cut].rstrip(" ,;:(-") + "..."
    return text


def lines(path):
    out = []  # (kind, text): 0 a section, 1 a subheading, 2 an item
    keep = False
    for raw in open(path, encoding="utf-8"):
        line = raw.rstrip("\n")
        top = re.fullmatch(r"\*\*(.+?)\*\*:?\s*", line)
        if top:
            keep = top.group(1).rstrip(":").strip().lower() in KEEP
            if keep:
                out.append((0, plain(top.group(1)).rstrip(":")))
            continue
        if line.startswith("**") and not line.startswith("- "):
            keep = False  # a bold paragraph ("**You need your own copy...**"): another section
            continue
        if not keep:
            continue
        sub = re.fullmatch(r"\*([^*].*?)\*\s*", line)
        if sub:
            out.append((1, plain(sub.group(1))))
        elif line.startswith("- "):
            out.append((2, compact(line[2:])))
    return out


def latin1(text):
    text = "".join(FOLD.get(c, c) for c in text)
    out = []
    for c in text:
        if ord(c) < 256:
            out.append(c)
        else:
            base = unicodedata.normalize("NFKD", c).encode("ascii", "ignore").decode()
            out.append(base or "?")
    return "".join(out).replace("<", "(").replace(">", ")")


def ascii_only(text):
    text = latin1(text).replace("×", "x").replace("\\", "/").replace("`", "'")
    out = []
    for c in text:
        if ord(c) < 128:
            out.append(c)
        else:
            base = unicodedata.normalize("NFKD", c).encode("ascii", "ignore").decode()
            out.append(base or "?")
    return "".join(out)


def c_bytes(data):
    out = '"'
    for b in data:
        if b in (0x22, 0x5C):
            out += "\\" + chr(b)
        elif 0x20 <= b < 0x7F and chr(b) != "?":  # (no trigraphs)
            out += chr(b)
        else:
            out += f'\\{b:03o}'
    return out + '"'


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    notes_dir, build_version, out_path = sys.argv[1:]
    version, path = pick(notes_dir, build_version)
    items = lines(path) if path else []
    src = ["/* GENERATED from docs/release_notes by tools/port/whats_new.py. Do not edit. */",
           '#include "port/whats_new.h"', "",
           f"const char portWhatsNewVersion[] = {c_bytes(version.encode())};",
           f"const char portWhatsNewBuild[] = {c_bytes(build_version.encode())};",
           "const PortWhatsNewLine portWhatsNewLines[] = {"]
    for kind, text in items:
        src.append(f"    {{{kind}, {c_bytes(text.encode('utf-8'))}, {c_bytes(latin1(text).encode('latin-1'))}, "
                   f"{c_bytes(ascii_only(text).encode('ascii'))}}},")
    src += ["    {0, 0, 0, 0},", "};", f"const int portWhatsNewCount = {len(items)};", ""]
    text = "\n".join(src)
    if not os.path.exists(out_path) or open(out_path, encoding="utf-8").read() != text:
        os.makedirs(os.path.dirname(out_path), exist_ok=True)
        open(out_path, "w", encoding="utf-8", newline="\n").write(text)


if __name__ == "__main__":
    main()
