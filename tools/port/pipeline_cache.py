#!/usr/bin/env python3
"""Merge Aurora pipeline caches into the bundled seed, initial_pipeline_cache.db.

Aurora records every GX pipeline it builds (its configuration: the GameCube
render state, the same on every platform and GPU) in pipeline_cache.db in its
cache folder, and at launch builds the pipelines of the bundled seed
(initial_pipeline_cache.db in the app's resources) before the game needs them.
Without a seed, a model drawn for the first time on a slow GPU (an iPhone) is
skipped until its pipeline is ready: cutscene characters stay invisible while
their shadows draw, and a faster machine hitches the first time it walks past
a paper note or talks to someone.

  merge OUT DB_OR_DIR...   every pipeline_cache.db found (recursively) into
                           OUT; a pipeline seen in several keeps the earliest
                           first_frame_used (Aurora builds the seed in that
                           order). The seed comes from runs on the user's own
                           disc (map_sweep --shots keeps each run's cache in
                           <shots>/_cache, the test pass in
                           local-docs/testpass/.cache): it stays out of the
                           repository. --current keeps only the configuration
                           versions this checkout's Aurora reads (others are
                           never built).
  info DB                  pipelines per shader type and config version
  check DB [--min N]       DB is a seed this checkout's Aurora uses (its
                           schema, and at least N GX pipelines, default 4000,
                           at the GX configuration version Aurora writes:
                           extern/aurora with port/patches/aurora); exit 1
                           with the reason otherwise
  versions                 the schema and configuration versions Aurora writes

The build bundles data/initial_pipeline_cache.db when it exists
(CMakeLists.txt; the APKs through port/android/app/build.gradle).
tools/port/release.py refreshes it from the test pass's caches and requires it
(docs/RELEASING.md). By hand: map_sweep.py the Aurora build over every map and
battle of both versions with --shots, then
  pipeline_cache.py merge --current data/initial_pipeline_cache.db data/initial_pipeline_cache.db <shots dirs>
"""
import glob
import os
import re
import shutil
import sqlite3
import sys
from collections import Counter

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SEED = os.path.join(ROOT, "data", "initial_pipeline_cache.db")
TYPES = {0: "clear", 1: "GX", 2: "RmlUi"}
GX = 1
MIN_GX_PIPELINES = 4000

# (type or "schema", file in extern/aurora, the constant's name)
_CONSTANTS = [
    ("schema", "lib/gfx/pipeline_cache.cpp", "PipelineCacheSchema"),
    (0, "lib/gfx/clear.hpp", "ClearPipelineConfigVersion"),
    (1, "lib/gx/pipeline.hpp", "GXPipelineConfigVersion"),
    (2, "lib/rmlui/pipeline.hpp", "RmlPipelineConfigVersion"),
]


def aurora_versions(root=ROOT):
    """{"schema": N, type: version} as the build's Aurora writes them: the constants in extern/aurora, as the last
    of port/patches/aurora that changes each leaves it (the build applies them; the tree may or may not have them
    yet). Exits with the reason when extern/aurora isn't there."""
    aurora = os.path.join(root, "extern", "aurora")
    patches = sorted(glob.glob(os.path.join(root, "port", "patches", "aurora", "*.patch")))
    out = {}
    for key, rel, name in _CONSTANTS:
        path = os.path.join(aurora, *rel.split("/"))
        if not os.path.isfile(path):
            sys.exit(f"pipeline_cache: {path} is missing (check out extern/aurora): Aurora's {name} is unknown")
        pattern = re.compile(r"constexpr\s+(?:u?int(?:32_t)?|int)\s+" + name + r"\s*=\s*(\d+)\s*;")
        m = pattern.search(open(path, encoding="utf-8", errors="replace").read())
        if not m:
            sys.exit(f"pipeline_cache: {name} not found in {path}")
        value = int(m.group(1))
        for p in patches:
            for line in open(p, encoding="utf-8", errors="replace"):
                if line.startswith("+") and not line.startswith("+++"):
                    pm = pattern.search(line)
                    if pm:
                        value = int(pm.group(1))
        out[key] = value
    return out


def find_dbs(paths):
    for p in paths:
        if os.path.isfile(p):
            yield p
        else:
            for root, _, files in os.walk(p):
                if "pipeline_cache.db" in files:
                    yield os.path.join(root, "pipeline_cache.db")


def rows(path):
    # A run stopped without Aurora's teardown (map_sweep's _Exit) leaves its
    # data in the write-ahead log: read a copy, which replays it.
    if os.path.exists(path + "-wal"):
        import tempfile
        tmp = tempfile.mkdtemp(prefix="pipeline-cache-")
        try:
            for suffix in ("", "-wal", "-shm"):
                if os.path.exists(path + suffix):
                    shutil.copyfile(path + suffix, os.path.join(tmp, "c.db" + suffix))
            return rows_of(os.path.join(tmp, "c.db"), readonly=False)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
    return rows_of(path, readonly=True)


def rows_of(path, readonly):
    try:
        con = sqlite3.connect(f"file:{path}?mode=ro", uri=True) if readonly else sqlite3.connect(path)
        out = list(con.execute("SELECT type, hash, config_version, config_size, config, first_frame_used "
                               "FROM pipeline_cache"))
        schema = [r[0] for r in con.execute("SELECT value FROM aurora_schema")]
        con.close()
        return schema, out
    except sqlite3.Error:
        return None, []  # a run that never wrote its cache (stopped early)


def merge(out, inputs, keep=None):
    """keep: {type: config version}; rows of other types or versions are left out (None: all kept)"""
    dbs = list(find_dbs(inputs))
    if not dbs:
        sys.exit("pipeline_cache: no pipeline_cache.db found")
    merged = {}
    schema = None
    empty = 0
    dropped = 0
    for db in dbs:
        s, rs = rows(db)
        if s is None:
            empty += 1
            continue
        if keep is not None and s != [keep["schema"]]:
            print(f"pipeline_cache: skipping {db}: schema {s}, Aurora's is {keep['schema']}", file=sys.stderr)
            continue
        if schema is None:
            schema = s
        elif s != schema:
            print(f"pipeline_cache: skipping {db}: schema {s}, expected {schema}", file=sys.stderr)
            continue
        for t, h, ver, size, cfg, first in rs:
            if keep is not None and keep.get(t) != ver:
                dropped += 1
                continue
            key = (t, h, ver)
            if key not in merged or first < merged[key][5]:
                merged[key] = (t, h, ver, size, cfg, first)
    if schema is None:
        sys.exit("pipeline_cache: none of the caches could be read")
    tmp = out + ".tmp"
    if os.path.exists(tmp):
        os.remove(tmp)
    con = sqlite3.connect(tmp)
    con.executescript("""
        CREATE TABLE aurora_schema(value INTEGER);
        CREATE TABLE pipeline_cache (
          type INTEGER NOT NULL,
          hash INTEGER NOT NULL,
          config_version INTEGER NOT NULL,
          config_size INTEGER NOT NULL,
          config BLOB NOT NULL,
          first_frame_used INTEGER NOT NULL,
          PRIMARY KEY (type, hash)
        );
        CREATE INDEX pipeline_cache_load_order_idx ON pipeline_cache(type, config_version, first_frame_used);
    """)
    con.executemany("INSERT INTO aurora_schema VALUES (?)", [(v,) for v in schema])
    con.executemany("INSERT OR REPLACE INTO pipeline_cache VALUES (?, ?, ?, ?, ?, ?)",
                    sorted(merged.values(), key=lambda r: (r[0], r[5])))
    con.commit()
    con.execute("VACUUM")
    con.close()
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    os.replace(tmp, out)
    print(f"pipeline_cache: {len(merged)} pipelines from {len(dbs) - empty} caches -> {out}"
          + (f" ({empty} without a cache table skipped)" if empty else "")
          + (f" ({dropped} rows of other configuration versions left out)" if dropped else ""))
    return len(merged)


def counts(path):
    """(schema, Counter of (type, config version))"""
    s, rs = rows(path)
    return s, Counter((r[0], r[2]) for r in rs)


def info(path):
    s, c = counts(path)
    print(f"{path}: schema {s}, {sum(c.values())} pipelines")
    print("  by type/version:", dict(c))


def check(path, minimum=MIN_GX_PIPELINES, versions=None):
    """None when path is a seed the build's Aurora uses (its schema, at least `minimum` GX pipelines at Aurora's GX
    configuration version), else why not. Prints what it found."""
    versions = versions or aurora_versions()
    if not os.path.isfile(path):
        return f"{path} is not there"
    s, c = counts(path)
    if s is None:
        return f"{path} is not a pipeline cache (no aurora_schema or pipeline_cache table)"
    if s != [versions["schema"]]:
        return f"{path} has schema {s}; this Aurora reads schema {versions['schema']}"
    gx = c.get((GX, versions[GX]), 0)
    other = sum(n for (t, v), n in c.items() if t == GX and v != versions[GX])
    print(f"pipeline seed {path}: {gx} GX pipelines at configuration version {versions[GX]}"
          + (f" ({other} at other versions, never built)" if other else "")
          + f", {sum(c.values())} in all")
    if gx < minimum:
        why = (f"{path} has {gx} GX pipelines at Aurora's configuration version {versions[GX]}, fewer than "
               f"{minimum}")
        if other:
            why += f" ({other} at other versions: made by an older Aurora, which this one never builds)"
        return why
    return None


def main():
    args = sys.argv[1:]
    if len(args) >= 3 and args[0] == "merge":
        current = "--current" in args
        rest = [a for a in args[1:] if a != "--current"]
        if len(rest) < 2:
            print(__doc__)
            return 2
        merge(rest[0], rest[1:], aurora_versions() if current else None)
    elif len(args) == 2 and args[0] == "info":
        info(args[1])
    elif len(args) in (2, 4) and args[0] == "check":
        minimum = MIN_GX_PIPELINES
        if len(args) == 4:
            if args[2] != "--min":
                print(__doc__)
                return 2
            minimum = int(args[3])
        why = check(args[1], minimum)
        if why:
            print(f"pipeline_cache: {why}", file=sys.stderr)
            return 1
    elif args == ["versions"]:
        v = aurora_versions()
        print(f"schema {v['schema']}; " + ", ".join(f"{TYPES[t]} (type {t}) version {v[t]}" for t in (0, 1, 2)))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
