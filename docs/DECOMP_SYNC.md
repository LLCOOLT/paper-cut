# Decomp sync

Paper Cut's game code is the decompilation's ([LLCOOLT/ttyd](https://github.com/LLCOOLT/ttyd)) plus the port's changes. `port/decomp_base.txt` names the decomp commit it's on. `tools/port/decomp_sync.py` brings in the decomp's newer work, and a weekly routine runs it.

## Running it

From a clean checkout of `main`, one command does the whole job:

```bash
python3 tools/port/decomp_sync.py --publish
```

If the decomp has nothing new, it says so and stops. Otherwise it:
1. Fetches the decomp.
2. Applies the decomp's changes since the base to the port's copies of its files with a three-way merge. The paths are `src/`, `rels/`, `include/`, `libs/`, `config/`, `configure.py` and `tools/`. It leaves out the demo discs, the decomp's own docs and CI, and `tools/port`.
3. Commits the merge as `Game code: the decomp's <base>..<target>` on a new branch, `decomp-sync/<target>`.
4. Regenerates `port/sources_<version>.cmake`, runs `tools/check_versions.py`, and moves the base (`port/decomp_base.txt`).
5. Pushes the branch and opens a pull request against `main` with the report as its description. This uses `gh` if it's installed and signed in. Otherwise it prints the link to open the pull request yourself.

Other ways to run it:
- `--check`: only says whether there's anything new (exit 10: yes).
- Without `--publish`: does everything but push. Read the report and push when you like.
- `--report FILE`: writes the report there too.

**Conflicts:** where the decomp and the port changed the same lines, it stops before committing, leaves conflict markers in those files and lists them. The rule is the one in `docs/PC_PORT.md`: the decomp's code wins. Put the port's change back on top, inside `#ifdef TARGET_PC`, or drop it if the decomp made it unnecessary. Then `git add` the files and finish:

```bash
python3 tools/port/decomp_sync.py --continue --publish
```

**Before merging:** `tools/port/local_ci.py` has to pass on the self-hosted runners: the GameCube match, the audits, and every map with golden audio. CI runs it when the pull request opens.
