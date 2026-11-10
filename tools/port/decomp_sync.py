#!/usr/bin/env python3
"""Bring the decomp's new work (LLCOOLT/ttyd) into Paper Cut's game code.

The game code (src/, rels/, include/, libs/, config/, configure.py and the
decomp's tools) is the decomp's, plus the port's changes (docs/PC_PORT.md,
"The game code and the decomp"). port/decomp_base.txt names the decomp commit
it's on. A sync applies the decomp's changes since then, base..target, to the
port's files with a three-way merge, so the port's own changes stay where the
decomp didn't touch the same lines; where both did, the file is left with
conflict markers for a person to settle, the decomp's code
winning.

  decomp_sync.py --check          new decomp commits? (exit 0 none, 10 some)
  decomp_sync.py [--to REF]       sync on a new branch decomp-sync/<target>
                 [--report FILE]  and write what happened as Markdown
                 [--no-branch]    on the current branch instead
  decomp_sync.py --continue       after settling conflicts (git add): commit
                                  and finish
  decomp_sync.py --publish        (with a sync or --continue) also push the
                                  branch and open the pull request (gh, if
                                  installed; else the link to open it)

The whole job in one command, from a clean checkout of main:

  python3 tools/port/decomp_sync.py --publish

After the merge: port/sources_<version>.cmake regenerated (gen_sources.py, new
or removed files), the version-macro check (tools/check_versions.py), and the
base moved. Exit: 0 synced and committed, 2 conflicts (left uncommitted, listed
in the report), 1 an error, 3 synced but a check failed. docs/DECOMP_SYNC.md
has the steps.
"""
import argparse, os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DECOMP_URL = "https://github.com/LLCOOLT/ttyd"
BASE_FILE = os.path.join("port", "decomp_base.txt")
# the decomp's files the port takes...
PATHS = ["src", "rels", "include", "libs", "config", "configure.py", "tools"]
# ...except what it leaves out: the demo discs (not built here), the decomp's
# own docs, CI and README, and its SDK submodule (the port has the files)
EXCLUDE = [
    ":(exclude,glob)config/*_DEMO*/**",
    ":(exclude,glob)orig/**",
    ":(exclude)libs/dolsdk2004",
    ":(exclude)tools/README.md",
    ":(exclude)tools/port",
]


def git(*args, check=True, capture=True, input=None):
    r = subprocess.run(["git", *args], cwd=ROOT, text=True, input=input,
                       stdout=subprocess.PIPE if capture else None, stderr=subprocess.PIPE if capture else None)
    if check and r.returncode != 0:
        raise SystemExit(f"git {' '.join(args)} failed:\n{r.stderr}")
    return r


def fetch(ref):
    if git("remote", "get-url", "decomp", check=False).returncode != 0:
        git("remote", "add", "decomp", DECOMP_URL)
    for attempt in range(4):  # flaky networks: 2, 4, 8 s
        if git("fetch", "--quiet", "decomp", ref, check=False).returncode == 0:
            return git("rev-parse", "FETCH_HEAD").stdout.strip()
        import time
        time.sleep(2 ** (attempt + 1))
    raise SystemExit(f"could not fetch {ref} from {DECOMP_URL}")


def base():
    return open(os.path.join(ROOT, BASE_FILE)).read().strip()


def commits(a, b):
    out = git("log", "--format=%h %s", f"{a}..{b}").stdout.strip()
    return [l for l in out.splitlines() if l]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--to", default="main", help="the decomp ref to sync to (default main)")
    ap.add_argument("--report", help="write a Markdown report here")
    ap.add_argument("--no-branch", action="store_true")
    ap.add_argument("--continue", dest="cont", action="store_true")
    ap.add_argument("--publish", action="store_true", help="push the branch and open the pull request")
    a = ap.parse_args()
    state = os.path.join(ROOT, ".git", "decomp_sync_target")

    if a.cont:
        if not os.path.exists(state):
            raise SystemExit("no sync to continue")
        start, target = open(state).read().split()
        if git("diff", "--name-only", "--diff-filter=U").stdout.strip():
            raise SystemExit("conflicts left: settle them and git add them first")
        os.remove(state)
        return finish(a, start, target, commits(start, target), [f"# Decomp sync: {start[:7]}..{target[:7]} (continued)", ""])

    start = base()
    target = fetch(a.to)
    # the base itself, for the three-way merge's blobs
    git("fetch", "--quiet", "decomp", start, check=False)
    new = commits(start, target)
    if a.check:
        print(f"{len(new)} new decomp commit(s) since {start[:7]}" + ("" if not new else ":"))
        for c in new:
            print("  " + c)
        return 10 if new else 0
    if not new:
        print("up to date with the decomp")
        return 0
    if git("status", "--porcelain", "--untracked-files=no").stdout.strip():
        raise SystemExit("the working tree has changes: commit or stash them first")

    if not a.no_branch:
        git("checkout", "-B", f"decomp-sync/{target[:7]}")
    diff = git("diff", "--binary", "--find-renames", start, target, "--", *PATHS, *EXCLUDE).stdout
    changed = git("diff", "--name-status", "--find-renames", start, target, "--", *PATHS, *EXCLUDE).stdout.strip()
    report = [f"# Decomp sync: {start[:7]}..{target[:7]}", "", f"{len(new)} decomp commit(s):", ""]
    report += [f"- {c}" for c in new] + ["", f"{len(changed.splitlines())} file(s) changed in the port's paths.", ""]

    conflicts = []
    if diff.strip():
        r = git("apply", "--3way", "--index", "--whitespace=nowarn", check=False, input=diff)
        unmerged = git("diff", "--name-only", "--diff-filter=U").stdout.split()
        conflicts = sorted(set(unmerged))
        if r.returncode != 0 and not conflicts:
            report += ["## Failed", "", "```", r.stderr.strip(), "```"]
            write(a.report, report)
            print(r.stderr, file=sys.stderr)
            return 1

    if conflicts:
        with open(state, "w") as f:
            f.write(f"{start} {target}\n")
        report += ["## Conflicts", "",
                   "The decomp and the port both changed these. Settle each (the decomp's code wins; put the "
                   "port's change back on top, inside `#ifdef TARGET_PC`, or drop it when the decomp made it "
                   "unnecessary), then `git add` them and run `tools/port/decomp_sync.py --continue`:", ""]
        report += [f"- `{f}`" for f in conflicts]
        write(a.report, report)
        print(f"{len(conflicts)} conflict(s); see the report")
        return 2

    return finish(a, start, target, new, report)


def finish(a, start, target, new, report):
    git("commit", "--quiet", "-m",
        f"Game code: the decomp's {start[:7]}..{target[:7]}\n\n" + "\n".join(new))

    # follow-ups: the source lists, the checks, the base
    checks = []
    for cmd in (["python3", "tools/port/gen_sources.py"], ["python3", "tools/check_versions.py"]):
        r = subprocess.run(cmd, cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        checks.append((" ".join(cmd), r.returncode, r.stdout[-3000:]))
    with open(os.path.join(ROOT, BASE_FILE), "w") as f:
        f.write(target + "\n")
    git("add", "-A", "port")
    git("commit", "--quiet", "-m", f"Game code: source lists and the decomp base after {target[:7]}", check=False)

    report += ["## Checks", ""]
    for name, code, out in checks:
        report += [f"- `{name}`: {'passed' if code == 0 else f'FAILED ({code})'}"]
        if code != 0:
            report += ["", "```", out.strip(), "```", ""]
    report += ["", "Still to run where the disc images are: `tools/port/local_ci.py` (the GameCube match, the "
               "audits, every map with golden audio)."]
    if a.report is None:
        a.report = os.path.join(ROOT, ".git", "decomp-sync-report.md")
    write(a.report, report)
    ok = all(c == 0 for _, c, _ in checks)
    print("synced; " + ("all checks passed" if ok else "a check failed: see the report"))
    if a.publish:
        publish(start, target, a.report, ok)
    return 0 if ok else 3


def publish(start, target, report_path, ok):
    """Pushes the sync's branch and opens a pull request against main (gh), or prints the link to open one."""
    branch = git("rev-parse", "--abbrev-ref", "HEAD").stdout.strip()
    for attempt in range(4):  # flaky networks: 2, 4, 8 s
        if git("push", "-u", "origin", branch, check=False).returncode == 0:
            break
        import time
        time.sleep(2 ** (attempt + 1))
    else:
        raise SystemExit(f"could not push {branch}")
    title = f"Decomp sync: {start[:7]}..{target[:7]}"
    body = open(report_path).read() + (
        "\n\nBefore merging: `tools/port/local_ci.py` on the self-hosted runners (the GameCube match, the audits, "
        "every map with golden audio)." + ("" if ok else "\n\n**A check failed here: see above.**"))
    import shutil
    if shutil.which("gh"):
        r = subprocess.run(["gh", "pr", "create", "--base", "main", "--head", branch, "--title", title, "--body", body],
                           cwd=ROOT, text=True, capture_output=True)
        if r.returncode == 0:
            print("pull request: " + r.stdout.strip())
            return
        print("gh couldn't open the pull request:\n" + r.stderr.strip(), file=sys.stderr)
    url = git("remote", "get-url", "origin").stdout.strip().removesuffix(".git")
    url = url.replace("git@github.com:", "https://github.com/")
    print(f"pushed {branch}; open the pull request: {url}/compare/main...{branch}?expand=1")
    print(f"(its description: {report_path})")


def write(path, lines):
    text = "\n".join(lines) + "\n"
    if path:
        with open(path, "w") as f:
            f.write(text)
    else:
        print(text)


if __name__ == "__main__":
    sys.exit(main())
