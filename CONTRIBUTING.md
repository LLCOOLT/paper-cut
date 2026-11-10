# Contributing to Paper Cut

Thanks for wanting to help! Paper Cut is a PC port of Paper Mario: The Thousand-Year Door built on the
[matching decompilation](https://github.com/LLCOOLT/ttyd). Bug reports, ideas, translations, fixes and features
are all welcome.

## Before you start

- **Bugs and crashes:** open an issue with the [templates](https://github.com/LLCOOLT/paper-cut/issues/new/choose).
  A crash report from Settings > General > Crash reports helps most.
- **New features:** open an idea issue first, so we can agree on the shape before you spend time on it.
- **Small fixes** (typos, docs, an obvious bug): a pull request is fine without an issue.

## The hard rules

These protect the project. A pull request that breaks one can't be merged, however good it is.

1. **No Nintendo assets in the repo.** No textures, models, sounds, music, text dumps or other files copied
   from a disc or ROM, in any form (raw, converted, re-encoded or "cleaned up"). New art is made from the
   player's own disc at runtime, through recipes (see [docs/CHARACTERS.md](docs/CHARACTERS.md)); only the
   recipe and code are committed.
2. **No downloads of game assets.** Paper Cut never fetches texture packs, mods or text packs on its own.
   Players add those themselves.
3. **Paper Mario 64:** the player's own ROM is read on their device only. No N64 code, ROM bytes or data
   extracted from it go into the repo; plain facts (a move's FP cost, a name) typed in by hand are fine.
4. **Licences:** Paper Cut is MIT. Don't copy code from GPL projects (including other TTYD mods and
   randomizers) or from anything without a compatible licence. Clean-room reimplementations from the decomp
   and public documentation are fine. Third-party code goes in [docs/THIRD_PARTY.md](docs/THIRD_PARTY.md).
5. **The original game stays the original.** With every new setting at its default, the game must play
   exactly as before: the test pass's fingerprints must not change. New behaviour goes behind a setting.

## Building and testing

- Build: see [docs/PC_PORT.md](docs/PC_PORT.md). You need your own disc image for each version you test.
- Code in `src/` and `rels/` is the game's decompiled code: port changes there go inside `#ifdef TARGET_PC`,
  so the GameCube build still matches.
- Paper Cut's own code lives in `port/`.
- Tests: [docs/TESTING.md](docs/TESTING.md). Run the scenarios your change touches with `tools/port/testpass.py`,
  and add a scenario under `tests/port/testpass/` for anything new. Runs can be headless or offscreen, so no
  window opens.
- Check US, JP and EU builds when your change touches game code or text.

## Text and translations

- Paper Cut's own text lives in `port/assets/lang/<language>/`, in English and five translations
  (de, es, fr, it, ja). After editing, run `tools/port/gen_lang.py` and commit `port/settings/lang_tables.inc`.
- Use the game's own terms in each language (item, badge and character names as that release of the game says
  them).
- Text must fit its box in every language; the test pass's text-fit crawls check this.

## Pull requests

- A clear title, and unrelated changes in separate pull requests when you can (it makes review quicker).
- Say what you tested (versions, platforms, scenarios) and add screenshots for anything visible.
- Reference the issue it fixes ("Fixes #123").
- Keep commits tidy; we may squash on merge.
- By contributing, you agree your work is released under the project's [MIT licence](LICENSE).

## Be kind

Be respectful and patient with everyone here. Harassment or personal attacks aren't tolerated.
