# Releasing 0.3.0 and 0.4.0: squashing, the README, the merge

Where things stand (2026-11-04):

| Branch | On | Commits | Changes |
|---|---|---|---|
| `main` (0.2.0) | — | — | `e0fbccc35`, two commits past both branches' base `6456cd02a` |
| `public-0.3.0` | `main` | 1 (`0.3.0: Super Luigi`) | a squash from earlier; `super-luigi-int` has moved on since (182 files differ) |
| `super-luigi-int` (0.3.0) | `6456cd02a` | 583 | 1,248 files |
| `feature/0.4.0` | `6456cd02a` | 61 (plus merges) | 242 files |

Only 16 files are changed by both, besides the translation files:
`CMakeLists.txt`, `README.md`, `docs/MODDING.md`, `features/modes.cpp`,
`launcher_ui.cpp`, `main_aurora.c`, `resume_aurora.c`, `gameplus_pc.c`,
`gx_arrays_pc.c`, `input_pc.c`, `warp_pc.c`, `battle_seq_command.c`,
`animdrv.c`, `icondrv.c`, `msgdrv.c` and `tools/port/ttyd_anim.py`.

## Order

1. **0.3.0 first.** Refresh `public-0.3.0` from `super-luigi-int`'s final
   state: one commit, `0.3.0: Super Luigi`, on `main`, as 0.2.0 was. Tag
   `v0.3.0`.
2. **0.4.0 on top of 0.3.0.** Put 0.4.0's groups (below) on the new `main`.
   The 16 shared files are resolved once, while the first group that touches
   each is built. The translation tables are regenerated (`gen_lang.py`), never
   merged by hand.
3. **Check the combination.** Build JP, US, EU, headless and the APK. Run the
   same-input fingerprint, an online pair, and a Super Luigi chapter start
   from the inn with co-op on and off. Also check a Super Luigi run online:
   its own code must use only the routed maths (the `hypotf` and
   fused multiply-add class, `port_prefix.h`).
4. **The README and the notes of both** (below), then tag `v0.4.0`.

Decided (2026-11-04): two releases, Super Luigi first. 0.3.0 is released
from the Super Luigi work before 0.4.0's groups go on top of it.

## 0.4.0's commits, grouped

Nine commits, each by its final files, in this order, each built (headless
at least) before the next. A shared file (`settings.h`, the settings list,
`lang_tables.inc`, `CMakeLists.txt`) lands with the first group that needs
it, at its final state.

| # | Commit | From |
|---|---|---|
| 1 | **Settings where you play**: the Start menu's Settings tab, Restart now, six languages (per-feature files), bigger text, Where next?, the first-start notice, the saves' tools while playing | `6618d6820` `e4c35059d` `77bfb938b` `bbdb23763` `a68e2d95c` `e10b613e3` `d768fd595` `9ef4db77d` |
| 2 | **Controls**: controllers (mapping list, players 1–4, remapping, names), matching button prompts, the Steam Deck's first start | `98faf9572` `ff6a26866` part of `e5032b7de` and `72c402fef` |
| 3 | **Co-op**: player 2 as the partner in battle and in the field, joining, 1P and 2P tags | `7667578e9` `05957ba17` `64c453fae` `e5032b7de` `476bb7269` |
| 4 | **Online co-op and the same maths everywhere**: lockstep, the Online tab, fdlibm, unfused arithmetic, the cross-machine fixes and test switches | `b031c00d4` `6789230f1` `fbdb519ac` `c95dc4719` `1418ec7db` `6164c74bf` `b8afec6ab` `a49a0a48f` `d240782af` `5f30392b7` |
| 5 | **The second screen**: the game's own Start menu on a dual-screen device | `c2cdaeb3b` `54254e22d` `48c9b1c0e` `ac09c10a6` `206306187` |
| 6 | **Looks and sound**: the audio mixer, HDR, texture upscaling, free camera, mirror mode, cosmetics, music shuffle and your own music (with the decoders) | `8604d2ce8` `0997e3e8a` `3b8133ceb` `0a0b1d971` `e8cbb1670` `a87b08cea` `7188795fc` |
| 7 | **Play**: autosave, challenges, boss refights, the built-in randomizer | `998393f04` `8e8019d09` `c68e1812a` `b0f09a6ce` `7c300dd13` |
| 8 | **Platforms and tools**: the AppImage and Flatpak, save sync through a folder, character export, decomp sync | `72c402fef` `b66a1b02e` `67017f52c` `b590d8a73` `721a8958b` `5e4d3fffa` `3036a07e6` `e9cc92314` |
| 9 | **The README, the release notes, the roadmap and the pictures** | `bf7e4400d` `ff75ffd6a` `7d73871f3` `86089c25b` `cb64e11de` `35ede6e04` `afcfe265f` `080903e8e` `3abaffd28` |

Dropped: the mod browser (`646ab562b`) with its removal (`8fc9eea2a`), which
together change nothing. The branch's merge commits go too.

How: one scratch branch per attempt, made with `git checkout feature/0.4.0
-- <group's files>` on top of the previous group. At the end it is compared
with `git diff feature/0.4.0`, which must be empty (except for the 0.3.0
side). `feature/0.4.0` is kept as it is until the release is out.

## The README for 0.4.0

Already in: the new features, the screenshots and GIFs, co-op and online,
the second screen (dual-screen Android only).

Still to do:
- **The second screen's picture**: a Thor mock-up of the real Start menu
  (Party and Gear) in place of `shot-second-screen.jpg`, which shows the old
  panels.
- **Playing online, step by step**: host and join, port 47700, forwarding a
  port or Tailscale/ZeroTier, and that both need the same Paper Cut version
  and disc release.
- **Status**: online tested between a Mac and a PC; Linux's AppImage built,
  the Flatpak once it builds; Android still not run on a device.
- **With 0.3.0**: one images block at the top (Luigi, quick-swap,
  widescreen, the launcher, then co-op and online). The Super Luigi section
  stays where 0.3.0 has it, and Features stays after it.
- **Release notes**: 0.4.0's say Super Luigi came in 0.3.0 and is still
  there; 0.3.0's stay as they are.
