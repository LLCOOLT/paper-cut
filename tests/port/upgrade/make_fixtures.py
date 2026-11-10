"""Writes the upgrade test's settings fixtures (tests/port/upgrade/<release>/settings.txt).

Each release's settings.txt as that release writes it: every setting it knows,
one key=value line each (0.1.0-0.3.0 write them all), with about a third of
them changed from their defaults, the same way for the same key in every
release, so a value set in one release can be followed into the next. The 0.4.0
and 0.4.1 files are written as those releases write them: the fixed fields, then
only the feature settings that differ from their defaults.

The key lists (keys_<release>.txt, "key default min max") are the releases'
settings tables, read from their sources (settings_aurora.c's kFields and every
portSettingsDeclare); 0.4.1's is 0.4.0's plus restart_handoff (the only setting
it added). keys_current.txt is the release being built: the upgrade test reads
every older file against it. Make it from the game's own table (the testpass
scenario settings_table writes settings_dump.txt):
    python tests/port/upgrade/make_fixtures.py --keys settings_dump.txt
Run with any Python 3:
    python tests/port/upgrade/make_fixtures.py
"""
import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RELEASES = ["0.1.0", "0.2.0", "0.3.0", "0.4.0", "0.4.1"]


def keys_from_dump(dump):
    """keys_current.txt from $TTYD_SETTINGS_DUMP's lines (settings_extra.c): every declared
    setting ("extra key value min max def 1") and the fixed fields ("field key value min max",
    their default the value the fresh run read: the test defaults are none of them but
    fullscreen and skip_intro, taken from the last release's list)"""
    last = {k: (d, lo, hi) for k, d, lo, hi in keys(RELEASES[-1])}
    out = {}
    for line in open(dump, encoding="utf-8"):
        f = line.rstrip("\n").split("\t")
        if f[0] == "extra" and len(f) >= 7 and f[6] == "1":
            out[f[1]] = (int(f[5]), int(f[3]), int(f[4]))
        elif f[0] == "field" and len(f) >= 5:
            d = last[f[1]][0] if f[1] in last else int(f[2])
            out[f[1]] = (d, int(f[3]), int(f[4]))
    # a setting declared only on first use or on phones (partner skins, the touch controls) isn't in a
    # fresh run's table: the last release's entry stands for it (the frame rate: a field written as text)
    for k, v in last.items():
        out.setdefault(k, v)
    with open(os.path.join(HERE, "keys_current.txt"), "w", newline="\n") as o:
        o.write("".join(f"{k} {d} {lo} {hi}\n" for k, (d, lo, hi) in sorted(out.items())))
    print("keys_current.txt:", len(out), "settings")


def keys(release):
    out = []
    with open(os.path.join(HERE, f"keys_{release}.txt")) as f:
        for line in f:
            k, d, lo, hi = line.split()
            # (widescreen's -1, "not in the file yet", is never written: the default found then is)
            out.append((k, max(int(d), int(lo)), int(lo), int(hi)))
    return out


def changed(key, d, lo, hi, every):
    """the key's changed value (or None: left at its default), the same in every release"""
    h = int(hashlib.sha1(key.encode()).hexdigest()[:8], 16)
    if key == "framerate":
        return "120"
    if h % every != 0 or lo == hi:
        return None
    # a value other than the default, inside the range that every release accepts
    v = hi if d != hi else lo
    if hi - lo > 4:
        v = lo + (hi - lo) * 3 // 4
        if v == d:
            v = lo
    return str(v)


def write(release, lines):
    d = os.path.join(HERE, release)
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "settings.txt"), "w", newline="\n") as f:
        f.write("".join(f"{k}={v}\n" for k, v in lines))


def main():
    fixed = [l.split()[0] for l in open(os.path.join(HERE, "fixed_0.4.0.txt"))]
    for r in RELEASES:
        lines = []
        for k, d, lo, hi in keys(r):
            v = changed(k, d, lo, hi, 3)
            if r in ("0.4.0", "0.4.1"):
                v = changed(k, d, lo, hi, 11)
                if v is None and k in fixed:
                    v = "60" if k == "framerate" else str(d)
                if v is not None:
                    lines.append((k, v))
            else:
                lines.append((k, v if v is not None else ("60" if k == "framerate" else str(d))))
        # a key a later release added, and one no release ever had (another app's, a typo):
        # kept and written back as they are
        lines.append(("zz_from_a_later_release", "7"))
        write(r, lines)
        print(r, len(lines), "lines")


if __name__ == "__main__":
    if len(sys.argv) > 2 and sys.argv[1] == "--keys":
        keys_from_dump(sys.argv[2])
    else:
        main()
