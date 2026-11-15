#!/usr/bin/env python3
"""Writes a damaged pipeline cache database (Aurora's pipeline_cache.db: its tables, then pages overwritten in
the middle, as a full disk or a crash mid-write leaves one: "database disk image is malformed") for the test pass's
gfx_pipeline_cache_damaged scenario (a testpass user_gen script: its first argument is where to write it).

usage: corrupt_pipeline_cache.py DEST
"""
import os
import sqlite3
import sys


def main():
    dest = sys.argv[1]
    if os.path.exists(dest):
        os.remove(dest)
    db = sqlite3.connect(dest)
    db.executescript("""
        CREATE TABLE aurora_schema(value INTEGER);
        INSERT INTO aurora_schema VALUES (0);
        CREATE TABLE pipeline_cache (type INTEGER NOT NULL, hash INTEGER NOT NULL, config_version INTEGER NOT NULL,
          config_size INTEGER NOT NULL, config BLOB NOT NULL, first_frame_used INTEGER NOT NULL,
          PRIMARY KEY (type, hash));
    """)
    db.executemany("INSERT INTO pipeline_cache VALUES (?, ?, ?, ?, ?, ?)",
                   [(1, i, 1, 256, bytes([i % 251]) * 256, i) for i in range(2000)])
    db.commit()
    db.close()
    size = os.path.getsize(dest)
    page = 4096
    with open(dest, "r+b") as f:
        # (the header and the first pages, the schema's, kept: it opens as a database and fails inside)
        for at in range(page * 3, size - page, page * 7):
            f.seek(at)
            f.write(b"\xa5" * page)
    print(f"{dest}: {size} bytes, pages from 3 overwritten")


if __name__ == "__main__":
    main()
