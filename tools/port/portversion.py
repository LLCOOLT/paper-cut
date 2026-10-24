"""Game version selection shared by the port tools.

The PC build is per version (CMake -DTTYD_VERSION): JP and US differ in struct
layouts and in which sources they build. Tools take --version (default:
$TTYD_VERSION, else G8MJ01) and read the version's source list from
port/sources_<version>.cmake (tools/port/gen_sources.py)."""
import os, re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
VERSIONS = ["G8MJ01", "G8ME01"]  # index = BUILD_VERSION, as in configure.py

def default():
    return os.environ.get("TTYD_VERSION", VERSIONS[0])

def defines(version):
    """The version flags configure.py passes to the compiler."""
    return [f"-DBUILD_VERSION={VERSIONS.index(version)}", f"-DVERSION_{version}",
            f"-Ibuild/{version}/include"]

def add_argument(ap):
    ap.add_argument("--version", choices=VERSIONS, default=default(),
                    help="game version (default: $TTYD_VERSION or G8MJ01)")

def sources(version, filt=None):
    """Game and REL sources the version builds, relative to ROOT."""
    text = open(os.path.join(ROOT, "port", f"sources_{version}.cmake"), encoding="utf-8").read()
    out = re.findall(r"\$\{CMAKE_SOURCE_DIR\}/(\S+)", text)
    return sorted(p for p in out if not filt or filt in p)
