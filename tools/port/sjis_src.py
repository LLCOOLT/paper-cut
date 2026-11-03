#!/usr/bin/env python3
"""Shift-JIS string literals for the PC build.

The decomp's sources are UTF-8; the matching build passes them through
sjiswrap, so MWCC sees Japanese text in string and character literals as
Shift-JIS bytes, which is what the game's tables and the disc data use.
clang keeps UTF-8, so for the PC build each source that has non-ASCII
literals is compiled from a copy where those characters are written as
octal escapes of their Shift-JIS (cp932) bytes. Comments and code are left
alone; a #line directive keeps diagnostics and debug info on the original.

usage: sjis_src.py SRC OUT          convert one file
       sjis_src.py --list FILE...   print the files that need converting
"""
import os
import sys


def needs(text):
    return any(ord(c) > 127 for c in text)


def ascii_comment(s):
    return "".join(ch if ord(ch) < 128 else "?" for ch in s)


def convert(text, path):
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(ascii_comment(text[i:j]))
            i = j
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(ascii_comment(text[i:j]))
            i = j
        elif c in "\"'":
            j = i + 1
            buf = [c]
            while j < n and text[j] != c and text[j] != "\n":
                ch = text[j]
                if ch == "\\" and j + 1 < n:
                    buf.append(text[j:j + 2])
                    j += 2
                    continue
                if ord(ch) > 127:
                    try:
                        data = ch.encode("cp932")
                    except UnicodeEncodeError:
                        sys.exit(f"{path}: {ch!r} has no Shift-JIS encoding")
                    buf.append("".join(f"\\{b:03o}" for b in data))
                else:
                    buf.append(ch)
                j += 1
            if j < n and text[j] == c:
                buf.append(c)
                j += 1
            out.append("".join(buf))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def main():
    if sys.argv[1] == "--list":
        paths = sys.argv[2:]
        if len(paths) == 1 and paths[0].startswith("@"):  # response file (Windows command-line limits)
            paths = open(paths[0][1:], encoding="utf-8").read().split(";")
        for p in paths:
            p = p.strip()
            if not p:
                continue
            if needs(open(p, encoding="utf-8").read()):
                print(p)
        return
    src, dst = sys.argv[1], sys.argv[2]
    text = open(src, encoding="utf-8").read()
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    body = f'#line 1 "{os.path.abspath(src).replace(os.sep, "/")}"\n' + convert(text, src)
    if os.path.exists(dst) and open(dst, encoding="ascii", errors="replace").read() == body:
        return
    with open(dst, "w", encoding="ascii", errors="strict", newline="\n") as f:
        f.write(body)


if __name__ == "__main__":
    main()
