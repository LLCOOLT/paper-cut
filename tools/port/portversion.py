"""Game version selection shared by the port tools.

The PC build is per version (CMake -DTTYD_VERSION): JP and US differ in struct
layouts and in which sources they build. Tools take --version (default:
$TTYD_VERSION, else G8MJ01) and read the version's source list from
port/sources_<version>.cmake (tools/port/gen_sources.py)."""
import os, re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
VERSIONS = ["G8MJ01", "G8ME01", "G8MP01"]  # index = BUILD_VERSION, as in configure.py

def default():
    return os.environ.get("TTYD_VERSION", VERSIONS[0])

def defines(version):
    """The version flags configure.py passes to the compiler."""
    out = [f"-DBUILD_VERSION={VERSIONS.index(version)}", f"-DVERSION_{version}",
           f"-Ibuild/{version}/include"]
    if version == "G8MP01":
        out.append("-DVERSION_G8ME01")  # PAL is built from the US code, as in configure.py
    return out

def version_args(version):
    """Only the -D flags of defines(): what decides the game's layouts."""
    return [d for d in defines(version) if d.startswith("-D")]

# the short names of the build folders and executables (build-pc-<short>-<backend>, ttyd-<short>)
SHORT = {"G8MJ01": "jp", "G8ME01": "us", "G8MP01": "eu"}

def version_switch(bodies):
    """One generated C text from each version's (VERSIONS -> text): shared when they
    agree, else an #if chain on the version macros (PAL also defines VERSION_G8ME01,
    so it is tested first; a body it shares with US goes under VERSION_G8ME01)."""
    jp, us, eu = (bodies[v] for v in VERSIONS)
    if jp == us == eu:
        return jp
    if eu == us:
        return f"#ifdef VERSION_G8ME01\n{us}#else\n{jp}#endif\n"
    if us == jp:
        return f"#ifdef VERSION_G8MP01\n{eu}#else\n{jp}#endif\n"
    return f"#if defined(VERSION_G8MP01)\n{eu}#elif defined(VERSION_G8ME01)\n{us}#else\n{jp}#endif\n"

def add_argument(ap):
    ap.add_argument("--version", choices=VERSIONS, default=default(),
                    help="game version (default: $TTYD_VERSION or G8MJ01)")

def sources(version, filt=None):
    """Game and REL sources the version builds, relative to ROOT."""
    text = open(os.path.join(ROOT, "port", f"sources_{version}.cmake"), encoding="utf-8").read()
    out = re.findall(r"\$\{CMAKE_SOURCE_DIR\}/(\S+)", text)
    return sorted(p for p in out if not filt or filt in p)
