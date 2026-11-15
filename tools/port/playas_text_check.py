#!/usr/bin/env python3
"""Play as's text, checked: every message of every disc's languages (US English; JP Japanese; the European
disc's English, German, French, Spanish and Italian) and Paper Cut's own texts (port/assets/lang), swapped for
Luigi, Peach and Bowser by the game's own code (luigi_pc.c, $TTYD_PLAY_AS_TEXT_DUMP), then searched for what the
swap left wrong (docs/PLAY_AS.md, Text: the rules). Each finding is a class, a pick, a text and where:

  name       Mario's name left where it is the player's, or the pick's left where it was the NPC's (by count:
             the output names Mario as often as the input named the pick, and the pick as often as it named Mario;
             titles that keep the name, "Paper Mario", "Mario Bros.", マリオストーリー, "Luigi's Mansion", aside)
  pronoun    with Peach: a sentence that names her (where it named Mario) still says he/him/his, er/ihn/sein, il,
             él, lui, 彼 after her name
  title      with Peach: a man's title or word just before her name ("Mr.", "Herr", "monsieur", "señor",
             "Signor", "Caro", "ce", "quel", ...) or くん after it
  agreement  with Peach: her name, a verb "to be" or "to become" and a man's adjective or participle ("Peach est
             devenu", "Peach è arrivato", "Peach es atacado")
  nickname   with Peach or Bowser: a nickname for Mario's mustache ("Mr. Mustache", "Captain 'Stache",
             "Mister Moustache", "Capitan Baffo", "Mister Schnauzer", "bigotudo", ヒゲ), or "plumber"
  brother    with Luigi: Mario (the NPC Luigi, named Mario) calling the player "big brother", or the player called
             Luigi's "little brother"

The allowlist (tools/port/playas_text_allow.txt) holds the findings that are meant: one per line,
"class pick source key  # why" (pick luigi|peach|bowser|* and source US|JP|EU-en|EU-de|EU-fr|EU-es|EU-it|PC-xx,
each a comma list; source and key file-name patterns, * and ?; key the message's file/tag or Paper Cut's English).
An allowed line that no longer finds anything is reported too (stale), so the list stays exact.

  playas_text_check.py tables [--check]    port/assets/play_as_text.txt into port/src/common/play_as_text_tables.inc
  playas_text_check.py gen FILE            write every text to FILE (the game's dump input)
  playas_text_check.py check FILE          FILE.out (the game's swaps) checked; exit 1 on a finding
  playas_text_check.py run EXE [--keep]    gen, the headless EXE with $TTYD_PLAY_AS_TEXT_DUMP, check
  playas_text_check.py report FILE         counts per class, pick and source (no exit status)
    --class C --pick P --source S --show N   (check/report: only those; show N findings each)

The testpass pair playas_text_dump (the game swaps) and playas_text_verdict (its user folder checked here, from
user_gen: a finding fails it) runs it on the US headless build; the discs are read from orig/ (all three: a
missing one is a failure, so the check never passes on less than every language).
"""
import argparse
import fnmatch
import os
import pickle
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
ALLOW = os.path.join(HERE, "playas_text_allow.txt")
PICKS = ["luigi", "peach", "bowser"]
NAME = {"luigi": "Luigi", "peach": "Peach", "bowser": "Bowser"}
JP_NAME = {"mario": "マリオ", "luigi": "ルイージ", "peach": "ピーチ", "bowser": "クッパ"}
# (source, disc, folder, language)
DISCS = [("US", "G8ME01", "msg/US", "en"), ("JP", "G8MJ01", "msg", "ja"), ("EU-en", "G8MP01", "msg/US", "en"),
         ("EU-de", "G8MP01", "msg/DE", "de"), ("EU-fr", "G8MP01", "msg/FR", "fr"), ("EU-es", "G8MP01", "msg/SP", "es"),
         ("EU-it", "G8MP01", "msg/IT", "it")]
UI = [("PC-en", "en", None), ("PC-de", "de", "de"), ("PC-fr", "fr", "fr"), ("PC-es", "es", "es"),
      ("PC-it", "it", "it"), ("PC-ja", "ja", "ja"), ("PC-ja_game", "ja", "ja_game")]
CACHE_STAMP = 1


# --- the texts ---

def disc_messages():
    """{source: [(file/tag, raw bytes)]} from orig/<version>/*.iso, cached in the temp folder"""
    sys.path.insert(0, HERE)
    import gcdisc
    isos = {}
    for _, v, _, _ in DISCS:
        d = os.path.join(REPO, "orig", v)
        for n in (os.listdir(d) if os.path.isdir(d) else []):
            if n.lower().endswith((".iso", ".gcm")):
                isos[v] = os.path.join(d, n)
    missing = sorted({v for _, v, _, _ in DISCS} - set(isos))
    if missing:
        sys.exit("playas_text_check: no disc image of %s in orig/ (every language is checked)" % ", ".join(missing))
    cache = os.path.join(tempfile.gettempdir(), "ttyd_playas_text.pkl")
    stamp = (CACHE_STAMP, sorted((v, os.path.getsize(p), int(os.path.getmtime(p))) for v, p in isos.items()))
    try:
        with open(cache, "rb") as f:
            d = pickle.load(f)
        if d["stamp"] == stamp:
            return d["msgs"]
    except Exception:
        pass
    discs = {v: gcdisc.Disc(p) for v, p in isos.items()}
    msgs = {}
    for src, v, folder, _ in DISCS:
        out = []
        for path in sorted(k for k in discs[v].files if k.startswith(folder + "/")):
            rest = path[len(folder) + 1:]
            if "/" in rest or not rest.endswith(".txt"):
                continue
            parts = discs[v].read(path).split(b"\0")
            for k in range(0, len(parts) - 1, 2):
                if parts[k]:
                    out.append((rest[:-4] + "/" + parts[k].decode("cp932", "replace"), parts[k + 1]))
        msgs[src] = out
    with open(cache, "wb") as f:
        pickle.dump({"stamp": stamp, "msgs": msgs}, f, protocol=4)
    return msgs


def ui_texts():
    """{source: [(english, translation)]}: Paper Cut's own (port/assets/lang, as gen_lang.py reads them)"""
    sys.path.insert(0, HERE)
    import gen_lang
    out = {}
    english = set()
    for src, lang, code in UI:
        if code is None:
            continue
        pairs = gen_lang.read(code)
        out[src] = sorted(pairs.items())
        english |= set(pairs)
    out["PC-en"] = [(e, e) for e in sorted(english)]
    return out


def records():
    """[(kind, lang, source, key, raw bytes)] in a fixed order"""
    rec = []
    for src, items in disc_messages().items():
        lang = next(l for s, _, _, l in DISCS if s == src)
        rec += [("g", lang, src, key, raw) for key, raw in items]
    ui = ui_texts()
    for src, lang, _ in UI:
        rec += [("u", lang, src, en, tr.encode("utf-8")) for en, tr in ui[src]]
    return rec


def esc(b):
    return b.replace(b"\\", b"\\\\").replace(b"\n", b"\\n").replace(b"\t", b"\\t").replace(b"\r", b"\\r")


def unesc(b):
    out, i = bytearray(), 0
    while i < len(b):
        c = b[i]
        if c == 0x5C and i + 1 < len(b):
            n = b[i + 1]
            out.append({0x6E: 0x0A, 0x74: 0x09, 0x72: 0x0D}.get(n, n))
            i += 2
        else:
            out.append(c)
            i += 1
    return bytes(out)


def gen(path):
    rec = records()
    with open(path, "wb") as f:
        for kind, lang, src, key, raw in rec:
            # (the kind's first letter is the game's, g or u; its source after it, which the game doesn't read)
            f.write(b"%s:%s\t%s\t%s\t%s\n" % (kind.encode(), src.encode(), lang.encode(), esc(key.encode("utf-8")),
                                                esc(raw)))
    return len(rec)


def decode(kind, lang, raw):
    return raw.decode("utf-8", "replace") if kind == "u" else raw.decode("cp932" if lang == "ja" else "latin-1",
                                                                         "replace")


# --- the checks ---

TAG = re.compile(r"<[^<>\n]*>")
L = r"A-Za-z\u00C0-\u00D6\u00D8-\u00F6\u00F8-\u00FF"  # (a Latin letter, as luigi_pc.c's isLetter)


def plain(kind, text):
    """the text without the game's commands (<wait 250>, <k>: a sentence's end where they turn a page)"""
    if kind != "g":
        return text
    return TAG.sub(lambda m: " . " if m.group(0) in ("<p>", "<k>") else " ", text)


# titles and other words that keep a name (luigi_pc.c's nameAt)
TITLES = re.compile(r"(?:Paper|PAPER)[ \n]+(?:Mario|MARIO)|(?:Mario|MARIO)[ \n]+(?:Bro|BRO|SERIES)|"
                    r"ペーパーマリオ|マリオ(?:ブラ|スト|シリ|ネッ)|(?:Luigi|LUIGI)(?:'s|'S|s|S)?[ \n]+(?:Mansion|MANSION)|"
                    r"ルイージマンション|スピーチ|Peachy[ \n]+Peach|Peach[ \n]+Tart|Peach-Torte")


def untitled(text, titles=None):
    """text without the titles that keep a name: those it has (titles None), or those of the input (a swap's
    output: "Paper Luigi" swapped is "Paper Mario", which is no title of the game's there)"""
    if titles is None:
        return TITLES.sub(" ", text)
    for t in titles:
        text = text.replace(t, " ", 1)
    return text


def names(text, who, titles=None):
    """how often text names who (mario or a pick) as a name, titles aside: Mario's at a word's start (the end of
    a word may run on: "Marioink", "Mariole" count, so a pun the swap misses shows); the pick's as a word (or
    with German's genitive s: "Peachy", the fruit's, is no name)"""
    t = untitled(text, titles)
    if who == "mario":
        latin, jp = r"(?<![%s])(?:Mario|MARIO)" % L, JP_NAME["mario"]
    else:
        n, last = NAME[who], NAME[who][-1]
        latin = r"(?<![%s])(?:%s%s*|%s%s*)(?:s|S)?(?![%s])" % (L, n, last, n.upper(), last.upper(), L)
        jp = JP_NAME[who]
    return len(re.findall(latin, t)) + t.count(jp)


SENT = re.compile(r"(?<=[.!?。！？])|\n\n")
HE = {"en": r"\b(?:he|him|his|himself|He|Him|His|Himself|HE|HIM|HIS)\b",
      "de": r"\b(?:er|ihn|ihm|seine|seinen|seinem|seiner|seines|Er|Ihn|Ihm|Seine|Seinen|Seinem|Seiner)\b(?!-)|"
            r"\b[Ss]ein(?= [A-ZÄÖÜ])",
      "fr": r"\b(?:il|Il|lui-même)\b(?! (?:y |faut|fallait|faudra))",
      "es": r"(?<![%s])(?:él|Él)(?![%s])" % (L, L),
      "it": r"\b(?:lui|Lui)\b", "ja": r"(?<![ァ-ヶー])カレ(?![ーら])"}
# a man's title or word before a name, and after it in Japanese
TITLE_BEFORE = {
    "en": r"(?:Mr\.|Mister|Master|Sir|sir|Mr|Monsieur|mister|master)",
    "de": r"(?:Herr|Herrn|Lieber|lieber|Liebster|liebster|diesen|Diesen|Mister|"
          r"kleiner|großer)",
    "fr": r"(?:monsieur|Monsieur|M\.|Cher|cher|Très cher|ce|Ce|cet|Cet|le|Le|du|au|un|Mister|mon|Mon)",
    "es": r"(?:señor|Señor|Sr\.|Don|don|Querido|querido|Queridísimo|ese|Ese|este|Este|el|El|del|al|un|"
          r"Mister|amigo|Amigo)",
    "it": r"(?:Signor|signor|Signore|signore|Sig\.|Caro|caro|Carissimo|carissimo|quel|Quel|questo|Questo|il|Il|"
          r"del|al|dal|nel|sul|un|Mister|amico|Amico)",
}
# (くん is no man's here: Frankly and Flavio say クリスチーヌくん too)
TITLE_AFTER_JA = r"(?:おにいさん|にいさん|兄さん|あんちゃん|のアニキ|アニキ|ぼうや|坊や|おじさん|オヤジ|おやじ)"
BE = {"fr": r"(?:est|était|sera|serait|soit|semble|devient|reste|est devenu|a été|s'est)",
      "es": r"(?:es|está|era|estaba|será|sea|esté|parece|queda|ha sido|fue)",
      "it": r"(?:è|era|sarà|sia|sembra|diventa|resta|rimane|si è|è stato|fu|è diventato|si è fatto)"}
MASC = {"fr": r"[%s'-]*(?:é|u|is|it|eux|if|ien|ier|el|eur|ant)" % L, "es": r"[%s]*(?:o|os|ón|or)" % L,
        "it": r"[%s]*(?:o|i|tore)" % L}
NICK = {
    "en": r"(?:Mustache|'Stache|Stache|MUSTACHE|mustached|Mustached|plumber|Plumber|PLUMBER|plumbers)",
    "de": r"(?:Schnauzer|Schnauzbart|Schnurrbart|Schnurrbärtige[rn]?|Klempner)",
    "fr": r"(?:Moustache|moustachu|Moustachu|moustachue|plombier|Plombier)",
    "es": r"(?:Bigote|bigotudo|Bigotudo|bigotes|fontanero|Fontanero)",
    "it": r"(?:Baffo|baffone|Baffone|baffuto|Baffuto|baffetto|idraulico|Idraulico)",
    "ja": r"(?:ヒゲ(?!キ)|ひげ(?!き)|髭|配管工)",
}
BROTHER = {
    "en": r"(?:[Bb]ig [Bb]ro(?:ther)?|[Bb]ig [Bb]ruh|[Oo]lder brother)",
    "de": r"(?:[Gg]roßer Bruder|[Bb]rüderchen|[Gg]roßer Bruderherz)",
    "fr": r"(?:[Gg]rand frère|[Ff]rangin|[Ff]rérot)",
    "es": r"(?:[Hh]ermano mayor|[Hh]ermanito|[Hh]ermanazo)",
    "it": r"(?:[Ff]ratellone|[Ff]ratello maggiore)",
    "ja": r"(?:にいさん|兄さん|アニキ|おにいちゃん|兄貴)",
}


# Luigi's own lines to Mario (his talks, his calls, his books) and the brothers' scenes
LUIGI_TALK = re.compile(r"gor_luigi|luigi_book|stg3_tel_|irai_27_|aaa_00/(?:pro|ep)_")


_PHRASES = None


def mario_phrases(pick, lang, key):
    """the tables' phrases for pick in lang (that message's too) that name Mario another way: "Mariole", "Marty"""
    global _PHRASES
    if _PHRASES is None:
        _PHRASES = [r for r in read_tables() if r[0] == "phrase" and r[4] != r[5]]
    tag = key.split("/", 1)[-1]
    out = []
    for kind, picks, l, t, frm, to, n in _PHRASES:
        if pick in picks and l == lang and (t is None or tag == t or (t.endswith("*") and tag.startswith(t[:-1]))):
            if re.match(r"(?:Mario|MARIO|マリ)", frm) and not re.match(r"(?:%s|%s)(?![%s])" % (NAME[pick], JP_NAME[pick], L), to):
                out.append(frm)
    return sorted(out, key=len, reverse=True)


def kept_phrases(pick, lang):
    """the tables' phrases for pick in lang kept as they are (from = to)"""
    return sorted({r[4] for r in read_tables() if r[0] == "phrase" and r[4] == r[5] and pick in r[1] and r[2] == lang},
                  key=len, reverse=True)


def sentences(text):
    return [s for s in SENT.split(text)]


def find(kind, lang, src, key, text, outs):
    """[(class, pick, snippet)] for one text"""
    found = []
    if kind == "u" and all(o == text for o in outs):
        return found  # (Paper Cut's own that keep Mario's name: portPlayAsKeepsMario; or name no one)
    p_in = plain(kind, text)
    for pick, out in zip(PICKS, outs):
        p_out = plain(kind, out)
        # names: the output names Mario as often as the input named the pick, the pick as often as Mario
        titles = TITLES.findall(p_in)
        # (a phrase of the tables that is a name of Mario's, "Mariole", "Marioink", made the pick's: none of his)
        p_named = p_in
        for frm in mario_phrases(pick, lang, key):
            p_named = p_named.replace(frm, " ")
        m_in, k_in = names(p_named, "mario"), names(p_in, pick)
        m_out, k_out = names(p_out, "mario", titles), names(p_out, pick, titles)
        if m_out != k_in or k_out != m_in:
            m = re.search(r"(?<![%s])(?:Mario|MARIO)[%s]*|マリオ.?" % (L, L), untitled(p_out, titles))
            found.append(("name", pick, "Mario x%d (want %d), %s x%d (want %d)%s" % (
                m_out, k_in, NAME[pick], k_out, m_in, (": " + ctx(p_out, m)) if m else "")))
        pick_name = JP_NAME[pick] if lang == "ja" else NAME[pick]
        if pick == "peach" and lang in HE:
            si, so = sentences(p_in), sentences(p_out)
            if len(si) == len(so):
                for a, b in zip(si, so):
                    if names(a, "mario") == 0:
                        continue
                    at = b.find(pick_name)
                    m = re.search(HE[lang], b[at:]) if at >= 0 else None
                    if m:
                        found.append(("pronoun", pick, ctx(b, None, at, at + m.end())))
        if pick == "peach":
            if lang in TITLE_BEFORE:
                for m in re.finditer(r"(?<![%s])%s[ \n]+%s(?![%s])" % (L, TITLE_BEFORE[lang], pick_name, L), p_out):
                    found.append(("title", pick, ctx(p_out, m)))
            if lang == "ja":
                for m in re.finditer(pick_name + TITLE_AFTER_JA, p_out):
                    found.append(("title", pick, ctx(p_out, m)))
            if lang in BE:
                for m in re.finditer(r"%s[ \n]+%s[ \n]+(?:(?:un|une|le|el|il|uno|mi|notre|nostro|vero|tout|todo|"
                                     r"proprio|ormai|davvero|très|muy|molto|bien|assez|si|più|plus|más)[ \n]+)*"
                                     r"%s(?![%s])" % (pick_name, BE[lang], MASC[lang], L), p_out):
                    found.append(("agreement", pick, ctx(p_out, m)))
        if pick in ("peach", "bowser") and lang in NICK:
            kept = p_out  # (what the tables keep as it is on purpose: Keelhaul Key's rocks, the shop's Mastro Baffo)
            for frm in kept_phrases(pick, lang):
                kept = re.sub(re.escape(frm).replace(r"\ ", r"\s+"), " ", kept)
            for m in re.finditer(r"(?<![%s])%s(?![%s])" % (L, NICK[lang], L) if lang != "ja" else NICK[lang], kept):
                found.append(("nickname", pick, ctx(p_out, m)))
        if pick == "luigi" and lang in BROTHER and (names(p_in, "luigi") or LUIGI_TALK.search(key)):
            for m in re.finditer(BROTHER[lang], p_out):
                found.append(("brother", pick, ctx(p_out, m)))
    return found


def ctx(text, m, a=None, b=None):
    if m is not None:
        a, b = m.start(), m.end()
    s = text[max(0, a - 40):b + 30]
    return re.sub(r"\s+", " ", s).strip()


# --- the allowlist ---

def load_allow():
    rules = []
    try:
        lines = open(ALLOW, encoding="utf-8").read().splitlines()
    except OSError:
        return rules
    for n, line in enumerate(lines, 1):
        body = line.split("#", 1)[0].strip()
        if not body:
            continue
        parts = body.split(None, 3)
        if len(parts) < 4:
            sys.exit("playas_text_allow.txt:%d: want 'class pick source key  # why'" % n)
        rules.append({"cls": parts[0], "pick": parts[1], "src": parts[2], "key": parts[3], "line": n, "used": 0})
    return rules


def allowed(rules, cls, pick, src, key):
    for r in rules:
        if r["cls"] not in ("*", cls) or not any(p in ("*", pick) for p in r["pick"].split(",")):
            continue
        if not any(fnmatch.fnmatchcase(src, s) for s in r["src"].split(",")):
            continue
        if r["key"] == key or fnmatch.fnmatchcase(key, r["key"]):
            r["used"] += 1
            return True
    return False


def results(path, flt):
    """[(class, pick, source, key, snippet, allowed)] for the dump at path (and its .out)"""
    ins = open(path, "rb").read().split(b"\n")
    outs = open(path + ".out", "rb").read().split(b"\n")
    ins = [l for l in ins if l]
    outs = outs[:len(ins)]
    if len(outs) != len(ins) or (outs and not outs[-1] and ins):
        sys.exit("playas_text_check: %s.out has %d lines for %d texts (the game's dump stopped?)" % (
            path, len([o for o in outs if o]), len(ins)))
    rules = load_allow()
    found = []
    for line, oline in zip(ins, outs):
        kind, lang, key, raw = [unesc(x) for x in line.split(b"\t", 3)]
        kind, src = kind.decode().split(":", 1)
        lang, key = lang.decode(), key.decode("utf-8", "replace")
        text = decode(kind, lang, raw)
        o = [decode(kind, lang, unesc(x)) for x in oline.split(b"\t")]
        if len(o) != 3:
            sys.exit("playas_text_check: a line of %s.out without three swaps" % path)
        for cls, pick, snip in find(kind, lang, src, key, text, o):
            if flt.cls and cls != flt.cls or flt.pick and pick != flt.pick or flt.source and src != flt.source:
                continue
            found.append((cls, pick, src, key, snip, allowed(rules, cls, pick, src, key)))
    stale = [r for r in rules if not r["used"]] if not (flt.cls or flt.pick or flt.source) else []
    return found, stale, len(ins)


def summary(found):
    rows = {}
    for cls, pick, src, key, snip, ok in found:
        r = rows.setdefault((cls, src), {"luigi": [0, 0], "peach": [0, 0], "bowser": [0, 0]})
        r[pick][1 if ok else 0] += 1
    print("%-10s %-11s %18s %18s %18s" % ("class", "source", "luigi (allowed)", "peach (allowed)", "bowser (allowed)"))
    for (cls, src), r in sorted(rows.items()):
        print("%-10s %-11s %18s %18s %18s" % (cls, src, *["%d (%d)" % tuple(r[p]) for p in PICKS]))


# --- the tables (port/assets/play_as_text.txt -> port/src/common/play_as_text_tables.inc) ---

TABLES_SRC = os.path.join(REPO, "port", "assets", "play_as_text.txt")
TABLES_OUT = os.path.join(REPO, "port", "src", "common", "play_as_text_tables.inc")
LANG_C = {"en": "LANG_EN", "de": "LANG_DE", "fr": "LANG_FR", "es": "LANG_ES", "it": "LANG_IT", "ja": "LANG_JA"}
PICK_C = {"luigi": "PK_L", "peach": "PK_P", "bowser": "PK_B"}


def c_str(b):
    """bytes as a C string literal (anything but printable ASCII escaped, split so a hex escape ends)"""
    out, hexed = [], False
    for c in b:
        if 0x20 <= c < 0x7F and c not in (0x22, 0x5C):
            if hexed and chr(c) in "0123456789abcdefABCDEF":
                out.append('" "')
            out.append(chr(c))
            hexed = False
        else:
            out.append("\\x%02X" % c)
            hexed = True
    return '"' + "".join(out) + '"'


_TABLES = None


def read_tables():
    global _TABLES
    if _TABLES is None:
        _TABLES = read_tables_now()
    return _TABLES


def read_tables_now():
    rows = []
    for n, line in enumerate(open(TABLES_SRC, encoding="utf-8").read().splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        parts = [p.strip() for p in line.split("|")]
        if len(parts) != 6 or parts[0] not in ("before", "chain", "after", "phrase"):
            sys.exit("play_as_text.txt:%d: want 'before|chain|after|phrase | picks | lang | tag | from | to'" % n)
        kind, picks, lang, tag, frm, to = parts
        if lang not in LANG_C or not frm or any(p not in PICK_C for p in picks.split(",")):
            sys.exit("play_as_text.txt:%d: unknown pick or language, or no text" % n)
        if kind in ("before", "chain", "after") and picks != "peach":
            sys.exit("play_as_text.txt:%d: before, chain and after are Peach's" % n)
        rows.append((kind, picks.split(","), lang, tag or None, frm, to, n))
    return rows


def key_of(frm, lang):
    if lang == "ja":
        return frm
    runs = re.findall(r"[!-~]+", frm)
    best = max(runs, key=len) if runs else ""
    return best if len(best) >= 3 else ""


def tables_text():
    rows = read_tables()
    out = ["/* Made by tools/port/playas_text_check.py tables from port/assets/play_as_text.txt: don't edit. */", ""]
    words = [r for r in rows if r[0] in ("before", "chain")]
    words.sort(key=lambda r: (r[0] == "chain", -len(r[4])))
    out.append("static const Word kBefore[] = {")
    for kind, picks, lang, tag, frm, to, n in words:
        out.append("    {%s, %s, %s, %d}," % (LANG_C[lang], c_str(frm.encode()), c_str(to.encode()), kind == "chain"))
    out.append("};")
    for name, kind in (("kAfter", "after"), ("kPhrases", "phrase")):
        sel = sorted([r for r in rows if r[0] == kind], key=lambda r: -len(r[4]))
        out.append("static const Phrase %s[] = {" % name)
        for _, picks, lang, tag, frm, to, n in sel:
            if kind == "after" and lang != "ja":
                frm, to = " " + frm, " " + to
            if lang == "ja":  # (a full-width space is any space or line break too, the text's own kept)
                frm, to = frm.replace("　", " "), to.replace("　", " ")
            sj = lambda t: c_str(t.encode("cp932")) if lang == "ja" else "NULL"  # noqa: E731
            key = key_of(frm.strip(), lang)
            out.append("    {%s, %s, %s, %s, %s, %s, %s, %s, %s}," % (
                " | ".join(PICK_C[p] for p in picks), LANG_C[lang], c_str(tag.encode()) if tag else "NULL",
                c_str(frm.encode()), c_str(to.encode()), sj(frm), sj(to), c_str(key.encode()),
                c_str(key.encode("cp932")) if lang == "ja" else "NULL"))
        if not sel:
            out.append("    {0, LANG_OTHER, NULL, \"\", \"\", \"\", \"\", \"\", NULL},")
        out.append("};")
    return "\n".join(out) + "\n"


def tables(check):
    text = tables_text()
    try:
        old = open(TABLES_OUT, encoding="utf-8").read()
    except OSError:
        old = None
    if check:
        return old == text
    if old != text:
        open(TABLES_OUT, "w", encoding="utf-8", newline="\n").write(text)
    return True


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == "tables":
        check = "--check" in sys.argv
        ok = tables(check)
        print("play_as_text_tables.inc %s" % ("up to date" if ok else "out of date: run playas_text_check.py tables"))
        return 0 if ok else 1
    if len(sys.argv) == 3 and sys.argv[2] in ("gen", "verdict"):
        # (testpass.py's user_gen: "script DEST args": the input into DEST, or DEST the verdict on the dump
        # beside it, which playas_text_dump left in the user folder)
        dest = sys.argv[1]
        if sys.argv[2] == "gen":
            print("%d texts" % gen(dest))
            return 0
        sys.argv = [sys.argv[0], "check", os.path.join(os.path.dirname(dest), "playas_text_in.txt"),
                    "--verdict", dest, "--show", "5"]
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["gen", "check", "run", "report"])
    ap.add_argument("path")
    ap.add_argument("--class", dest="cls")
    ap.add_argument("--pick")
    ap.add_argument("--source")
    ap.add_argument("--show", type=int, default=20)
    ap.add_argument("--all", action="store_true", help="show the allowed findings too")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verdict", help="check: also write the verdict to this file")
    a = ap.parse_args()
    if a.cmd == "gen":
        print("%d texts" % gen(a.path))
        return 0
    path = a.path
    if a.cmd == "run":
        d = tempfile.mkdtemp(prefix="playas_text_")
        path = os.path.join(d, "playas_text_in.txt")
        gen(path)
        user = os.path.join(d, "user")
        os.makedirs(user)
        open(os.path.join(user, "settings.txt"), "w").write("skip_intro=1\nfullscreen=0\n")
        iso = next(os.path.join(REPO, "orig", "G8ME01", n) for n in os.listdir(os.path.join(REPO, "orig", "G8ME01"))
                   if n.lower().endswith(".iso"))
        env = {k: v for k, v in os.environ.items() if not k.startswith("TTYD_")}
        env.update(TTYD_FAST="1", TTYD_USER_DIR=user, TTYD_NO_LAUNCHER="1", TTYD_PLAY_AS_TEXT_DUMP=path)
        r = subprocess.run([os.path.abspath(a.path), iso], env=env, cwd=os.path.dirname(os.path.abspath(a.path)), capture_output=True,
                           text=True, errors="replace", timeout=600)
        if not os.path.exists(path + ".out"):
            sys.exit("playas_text_check: the game made no swaps (exit %d): %s" % (r.returncode, r.stdout[-600:]))
    found, stale, n = results(path, a)
    if a.cmd == "run" and not a.keep:
        import shutil
        shutil.rmtree(os.path.dirname(path), ignore_errors=True)
    bad = [f for f in found if not f[5]]
    if not tables(True):  # (the game's tables made from an older play_as_text.txt: what was checked isn't it)
        print("play_as_text_tables.inc is out of date: run playas_text_check.py tables")
        bad.append(("tables", "*", "*", "play_as_text_tables.inc", "out of date", False))
    if a.cmd == "report" or a.cmd in ("check", "run"):
        summary(found)
        shown = {}
        for cls, pick, src, key, snip, ok in found:
            if ok and not a.all:
                continue
            k = (cls, pick)
            shown[k] = shown.get(k, 0) + 1
            if shown[k] <= a.show:
                print("%s%-9s %-6s %-6s %s: %s" % ("  (allowed) " if ok else "", cls, pick, src, key, snip))
        for r in stale:
            print("playas_text_allow.txt:%d: no longer found: %s %s %s %s" % (r["line"], r["cls"], r["pick"], r["src"],
                                                                            r["key"]))
    verdict = "%d findings, %d allowed, %d stale allowlist lines, in %d texts x 3 picks\n" % (
        len(bad), len(found) - len(bad), len(stale), n)
    print(verdict, end="")
    if a.verdict:
        open(a.verdict, "w", encoding="utf-8").write(verdict)
    if a.cmd == "report":
        return 0
    return 1 if bad or stale else 0


if __name__ == "__main__":
    sys.exit(main())
