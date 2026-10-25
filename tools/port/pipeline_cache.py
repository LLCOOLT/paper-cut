#!/usr/bin/env python3
"""Merge Aurora pipeline caches into the bundled seed, initial_pipeline_cache.db.

Aurora records every GX pipeline it builds (its configuration: the GameCube
render state, the same on every platform and GPU) in pipeline_cache.db in its
cache folder, and at launch builds the pipelines of the bundled seed
(initial_pipeline_cache.db in the app's resources) before the game needs them.
Without a seed, a model drawn for the first time on a slow GPU (an iPhone) is
skipped until its pipeline is ready: cutscene characters stay invisible while
their shadows draw.

  merge OUT DB_OR_DIR...   every pipeline_cache.db found (recursively) into
                           OUT; a pipeline seen in several keeps the earliest
                           first_frame_used (Aurora builds the seed in that
                           order). The seed comes from runs on the user's own
                           disc (map_sweep --shots keeps each run's cache in
                           <shots>/_cache): it stays out of the repository.
  info DB                  pipelines per shader type and config version

The build bundles data/initial_pipeline_cache.db when it exists
(CMakeLists.txt). Typical: map_sweep.py the Aurora build over every map and
battle of both versions with --shots, then
  pipeline_cache.py merge data/initial_pipeline_cache.db <shots dirs>
"""
import os
import shutil
import sqlite3
import sys
from collections import Counter


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


def merge(out, inputs):
    dbs = list(find_dbs(inputs))
    if not dbs:
        sys.exit("pipeline_cache: no pipeline_cache.db found")
    merged = {}
    schema = None
    empty = 0
    for db in dbs:
        s, rs = rows(db)
        if s is None:
            empty += 1
            continue
        if schema is None:
            schema = s
        elif s != schema:
            print(f"pipeline_cache: skipping {db}: schema {s}, expected {schema}", file=sys.stderr)
            continue
        for t, h, ver, size, cfg, first in rs:
            key = (t, h, ver)
            if key not in merged or first < merged[key][5]:
                merged[key] = (t, h, ver, size, cfg, first)
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
    shutil.move(tmp, out)
    print(f"pipeline_cache: {len(merged)} pipelines from {len(dbs) - empty} caches -> {out}"
          + (f" ({empty} without a cache table skipped)" if empty else ""))


def info(path):
    s, rs = rows(path)
    print(f"{path}: schema {s}, {len(rs)} pipelines")
    print("  by type/version:", dict(Counter((r[0], r[2]) for r in rs)))


def main():
    if len(sys.argv) >= 4 and sys.argv[1] == "merge":
        merge(sys.argv[2], sys.argv[3:])
    elif len(sys.argv) == 3 and sys.argv[1] == "info":
        info(sys.argv[2])
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
