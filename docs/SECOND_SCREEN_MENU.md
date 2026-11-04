# The second screen's Start menu

The second screen (Android with a second built-in display, such as the AYN
Thor; on the desktop only for tests) shows the game's own pause menu, drawn by
the game's own code, live and navigable while the game plays on the main
screen.

## How it works

One frame of the second screen's menu runs once per game frame, after the
game's frame is out and the next one has begun (`VIWaitForRetrace`, end, in
`port/src/aurora/vi_aurora.c` -> `portCompanionMenuFrame`,
`port/src/aurora/companion_menu_aurora.c`), when:

- a presenter shows the second screen (`portCompanionShowing`), the
  readback works (no MSAA), and `$TTYD_COMPANION_MENU` isn't `0`;
- the game is in the field (`SEQ_GAME`, no map change), Mario himself
  (`characterId 0`), and the game's own Start menu is closed (its
  `wp->menuState == 0`) and has been for 2 frames. Otherwise the second
  screen keeps its last picture.

The frame (`enter` / `leave`) leaves the game exactly as it found it:

| What | How |
|---|---|
| The game's data and bss | the save states' sections (`portSnapSection`, about 1.3 MB + 0.5 MB) copied before, put back after |
| The Start menu's own statics (`src/window/win_*.c`) | in a block of their own (`port/include/port/companion_section.h`, `PORT_SECTION_MENU`): swapped for the second screen's copy (which starts as the image's data, copied by a constructor before `main`) |
| What the decoders know (`anim_pc.c`, `tpl_pc.c`, `gx_arrays_pc.c`, `PORT_SECTION_KEEP`) | kept through the copy back: the second screen's own files stay decoded and known (`PORT_GX_BLOB_REGISTRY` raised to 128 for room) |
| Allocations (`__memAlloc`, `_mapAlloc`, `smartAlloc` and frees) | host memory (`port/src/common/companion_menu_pc.c`), type 3 smart blocks let go at the end of its frame |
| Files (`fileAlloc`, `fileAsync`, `fileFree`) | read at once from the disc with Aurora's `DVDOpen`/`DVDReadPrio` (not the game's DVD manager), decoded, kept for the run |
| Effects (`effEntry`, `effDelete`; the Journal's Crystal Stars) | entries of its own, never in `effMain`; the menu draws them itself |
| Animation poses (Mario, the partners) | an `AnimationWork` of its own (`portAnimWorkInit`, `portAnimSwapWork`, `animdrv.c`) |
| Window manager (the item target windows) | a `WinMgrWork` of its own (`winmgr.c`), run by `winMgrMain` in its frame |
| The draw list | its own `pDispWork`/`pSortWork` (put back with the bss) |
| `winGetPtr()` | its own `WindowWork` (`portWinSetPtr`, in its copy of the statics) |
| Sound | `pmario_sound.c`: SFX/BGM/ENV on and off, flags, do nothing |
| The pad (`keyGet*`) | `system.c`: its own input |
| The mouse (`portMenuPoint`) | its own taps (`menu_tap_pc.c`) |
| Random numbers | `portRandState` saved and put back |
| The audio thread | held (`salPcLock`) through the frame |
| Writes the audit found in the heaps | the pouch (its getters cache the boots' and hammer's levels, which battles read) and the message windows' `wakuTexObj` (GXLoadTexObj notes the texture map): copied before, put back after (`keep`) |

`$TTYD_COMPANION_AUDIT=1` (Windows, `interp_audit.c`): the game's heaps are
read-only during each of its frames (each writing instruction listed with two
callers) and the executable's writable sections compared before and after
(changed globals listed). Expected entries: Aurora's own state, this
feature's own statics, the decoders' (`sLive*`, `sBlobs`, `sRanges`), and
`keptBack`'s memmove (putting the kept heap objects back).

What the menu would change is left out or becomes a tap of the second
screen (`port/include/port/companion.h`), which the game acts on as input:

- an item chosen for Mario or a partner: `PORT_COMPANION_TAP_ITEM_ON` (slot,
  partner id), used by `portCompanionFieldItem` at the point the Start menu
  could open (win_main.c): Mario and the partner out as the existing tap; any
  other partner at once, as the item list does (`companionItemOn`). In its
  frame, `win_item.c` state 300 always closes on a target (the second
  screen then sends the tap when `winRootMain` returns -3).
- a partner switch: `PORT_COMPANION_TAP_PARTNER` when it returns -2 with a
  switch asked.
- left out (and said in `win_*.c` under `gPortCompanionMenu`): equipping and
  unequipping badges (A in the badge lists does nothing), sorting (Z is never
  sent), reading mail marks nothing read (`pouchOpenMail`, `swSet`
  skipped), the Mailbox SP isn't added to the pouch, the Ragged Diary,
  the Settings tab (it changes the player's settings; not among its tabs),
  B on the tab row (the second screen's menu never closes).
- After a -2/-3 close the menu opens again at the same tab.

Drawing: `draw()` queues `winRootDisp(CAMERA_2D, &sWork)` and runs the
window manager into its own list, sorts it, draws a plain background
(a quad, CAMERA_2D's view) and each camera with entries as `camDraw` does
(no widescreen: the picture is 4:3), then `GXCopyTex` 640x480 RGBA8 with
clear (the frame buffer as the frame began) and
`AuroraReadbackCaptureCopies`; `collect()` picks it up a frame or two later
and untiles it into the picture (`portCompanionMenuPicture`).

Presenting: the desktop window (`port/settings/features/companion.cpp`,
only with `$TTYD_COMPANION_WINDOW=1`: no setting, note or translation) shows
the picture (streaming texture, 4:3 letterboxed) with buttons under it (L,
the arrows, R; B and A). A tap on the picture is the game's own mouse
handling in the menu (`portMenuPoint`): a tab or row tapped is selected, a
tap on the selected one is A. Without a picture yet (or no readback) it
shows the old panels.

Android: see "Android" below.

Tests: input script commands `companion tap NX NY`, `companion button
a|b|up|down|left|right|l|r`, `companion shot NAME` (the next picture as
NAME.png in `$TTYD_SHOT_DIR`); `$TTYD_COMPANION_TAB=<index>` the tab it opens
at. Pictures lag the input by many frames under `TTYD_FAST` (the GPU behind
the unpaced game): wait ~150 frames before a shot.

## Settings

- Windows, macOS, Linux, iOS: no setting, no window (only
  `$TTYD_COMPANION_WINDOW=1` for tests). The old Display > Second screen
  setting and its desktop note are gone.
- Android: `second_screen` (on by default), shown on the settings page and
  the Start menu's Settings tab only while the device reports a second
  display of its own (`portSettingsSetShown`, `portCompanionDualScreen`).

## Android

`TtydCompanion.java`: a `Presentation` on the second display. "Dual-screen
device" means a display other than the default one, in
`DISPLAY_CATEGORY_PRESENTATION`, that is built in: `Display.getType()`
(hidden API, read by reflection) is `TYPE_INTERNAL` (1). Where reflection
fails: not `FLAG_PRIVATE` and not named like HDMI, USB, Wi-Fi, wireless,
cast, overlay or virtual. An external monitor or a cast screen therefore
gets nothing, and the setting stays hidden (`setDualScreen` ->
`portCompanionDualScreen` -> `portSettingsSetShown("second_screen")`).
**Untried on a real device** (no AYN Thor here): only built.

The page shows the menu's picture (`menuPicture`, ARGB into a Bitmap,
refreshed every 33 ms while it shows) with the same buttons as the desktop;
taps on the picture go to `menuTap`, buttons to `menuButton`. Until the
first picture, the old panels.

## Left out, and why

- Battles, Bowser and Peach: the menu is the field's and Mario's; the
  second screen keeps its last picture.
- Equipping badges, sorting, the Ragged Diary, reading mail as read,
  Settings: they change the game (or the player's settings); the second
  screen's menu only shows them.
- The background is a plain gradient (the game's capture of the field isn't
  taken for the second screen).
