#!/usr/bin/env python3
"""Put the browser builds of each version together as one static site.

Each web build writes the page and its own ttyd-<us|jp|eu>.js
and .wasm into <build>/web. The site is one page with every version beside
it; the page loads the one that matches the player's disc.

usage: make_site.py OUT BUILD_DIR [BUILD_DIR ...]
"""
import shutil
import sys
from pathlib import Path


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    for build in sys.argv[2:]:
        web = Path(build) / "web"
        if not web.is_dir():
            print(f"{web}: no web build there", file=sys.stderr)
            return 1
        for f in web.iterdir():
            if f.is_file():
                shutil.copy2(f, out / f.name)
                print(f"{f} -> {out / f.name}")
    # GitHub Pages: serve files as they are (no Jekyll)
    (out / ".nojekyll").write_text("")
    return 0


if __name__ == "__main__":
    sys.exit(main())
