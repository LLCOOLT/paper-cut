# Releasing 0.3.0

Decided (2026-11-04): this branch's work is 0.3.0, and it goes to `main`
first. Super Luigi waits until it has been played through, then comes in a
later release on top of this one.

Where things stand:

| Branch | On | Commits |
|---|---|---|
| `main` (0.2.0) | — | `e0fbccc35`, two commits past this branch's base `6456cd02a` |
| `feature/0.3.0` | `6456cd02a` | five milestone commits, then one per feature still landing |
| `game-fixes` | `main` | fixes to the base game found while building Super Luigi (the ARAM hang, the map-animation clock, the Glitz Pit's REL epilog) |

The milestones (squashed on 2026-11-04 with an identical tree; the old
history is kept locally as `feature/0.4.0-before-squash`):

1. Settings, controls and play
2. Co-op you can join, online co-op over TCP, the camera and looks, the
   second screen
3. Online co-op between a Mac and a PC; the second screen's Start menu
4. Online rooms, sealed end to end, through the relay; the controller's
   light, presets, photo mode, surround, text to speech, recovery
5. Widescreen fixes, Discord status, colour-blind filters, sound captions,
   language packs

## Steps

1. **The last features.** Each feature branch is cherry-picked on top as
   one commit (`git cherry-pick $(git merge-base <branch>
   feature/0.4.0-before-squash)..<branch>`). The translation tables are
   regenerated (`gen_lang.py`), never merged by hand.
2. **game-fixes.** Cherry-picked once it's built and checked.
3. **The clean-up pass** over everything added, then the in-game "what's
   new" page and the translations.
4. **Onto main.** Each follow-up commit folded into its milestone, then
   rebased onto `main` (`e0fbccc35`'s two 0.2.0 fixes come in).
5. **Check.** Build JP, US, EU, headless and the APK; the same-input
   fingerprint; an online pair, directly and through the relay.
6. **The README and the release notes** (`docs/release_notes/0.3.0.md`),
   then tag `v0.3.0`.

## The README

Already in: the new features, the screenshots and GIFs, co-op, playing
online step by step, the second screen (dual-screen Android only), the
status rows.

Still to do: the second screen's picture (a Thor mock-up of the real Start
menu in place of `shot-second-screen.jpg`); the status once Linux, the Mac
and Android have been tried.
