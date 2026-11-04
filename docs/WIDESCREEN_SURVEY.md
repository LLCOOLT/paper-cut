# Widescreen survey (16:9 and 21:9), US Aurora build

Survey of 2026-11-04 on feature/0.3.0, Windows, US disc. Nothing in the game or port was changed by the survey;
the fixes that followed (branch 040-widefix) are in "Fixes" at the end, and each problem below says what became of it.
The pictures named below are kept in
`C:\Users\LT\AppData\Local\Temp\claude\C--Users-LT-Repos-ttyd-port\de5a5c05-96a0-4b83-8f42-73464ef9e212\scratchpad\wsurvey\evidence\`
(every raw capture is under `...\wsurvey\runs\<tag>_<43|169|219>\shots\`, and the per-picture numbers are in `...\wsurvey\analysis.txt`).

## How it was checked

- Build: a copy of `build-pc-us-aurora` (exe, DLLs, `mods`) run from the scratchpad. Every run used `TTYD_FAST=1`, a fresh `TTYD_USER_DIR` with `skip_intro=1` and `fullscreen=0`, `TTYD_NO_LAUNCHER=1`, `SDL_WINDOW_ACTIVATE_WHEN_SHOWN=0`, a 120 s timeout and `game_slot.sh` with 2 slots.
- Aspects: `widescreen=0 aspect_ratio=1` (4:3), `widescreen=1 aspect_ratio=2` (16:9, 853x480 capture), `widescreen=1 aspect_ratio=4` (21:9, 1120x480 capture). `wide_hud=1` was added on a few 21:9 runs.
- Same frame in each aspect: the runs are deterministic, so the 4:3 and wide captures of one script are the same frame. The 4:3 picture matches the central 608 columns of the wide one (lowest difference at exactly 608), which is what the comparisons use.
- Field rooms (`TTYD_MAP` warp at the first entrance, two captures at 30 and 200 frames), 49 rooms, at least two per area: aaa_00; gor_00, 01, 02, 03, 04, 10; tik_00, 03; hei_00, 01; nok_00, 01; gon_00, 04, 11; win_00, 01; mri_00, 01; tou_00, 02, 03; usu_00, 01; gra_00; jin_00, 04; muj_00, 01; dou_00, 03; rsh_00_a, 02_a; hom_00; eki_00, 01; pik_00, 02; bom_00, 01; moo_00, 01; aji_00, 01; las_00, 09; kpa_00; jon_00. All at 4:3 and 16:9; gor_00, gor_01, hei_00, nok_00, mri_01, tou_02, rsh_02_a, aji_01 also at 21:9.
- Battles at 4:3, 16:9 and 21:9: Hooktail (gon #20), Magnus von Grapple 2.0 (mri #20), Macho Grubba (tou #23), a Petal Meadows battle (hei #1), a Palace of Shadow battle (las #1, 21:9 included); the jin battle ran but found no enemy.
- 2D and transitions: the title and the memory-card / file-creation screens (no warp), the Start menu's tabs in Rogueport (gor_01), dialogue with letterbox bars (hei_01 at story 20, also 21:9 with `wide_hud=1`), map-entry transitions (aaa_00, tik_01 through `dokan_1`, gor_01; every 3-6 frames from the first frame), a walk through a loading zone (gor_01 to gor_02, captures every 6 frames) and walks in nok_00 and hei_00.
- A small PIL script (`analyze.py`) measured, for each wide capture, the share of black and of one flat colour in the parts outside the central 4:3 area, and the difference of the central area against the 4:3 capture. Every flagged picture and most of the others were looked at by eye.

## Problems found, most visible first

### 1. The Start menu's backdrop is squeezed into the 4:3 area (high)

Rooms: any room; seen in gor_01. With the Start menu open, the field behind it is the whole wide frame squeezed into the central 608 columns: 1.40x narrower at 16:9 and 1.84x at 21:9 (the squeezed frame matches the capture with a mean difference of about 3, the unsqueezed one about 37). Beside it the sides show the sky backdrop with stretched clouds, not the field.
Evidence: `startmenu_gor01_43.png`, `startmenu_gor01_169.png`, `startmenu_gor01_219.png`.
Cause: `winRootBgCaptureDisp` (`src/window/win_root.c`) copies the full EFB, then turns the 3D camera and map drawing off; `winRootBgDrawDisp` draws the copy on `CAMERA_2D` as a quad from `-fbWidth/2` to `fbWidth/2`, which the widescreen x scale shrinks to the 4:3 area.
Fix: draw that quad's x from `WIDE_X(-w / 2)` to `WIDE_X(w / 2)` (include/wide_x.h). The copy already holds the wide picture, so it then lands where it was.
Status: fixed as proposed (Fixes, 1).

### 2. Every map-entry page-flip shows the live room at the edges (high)

Rooms: every warp entrance tried (aaa_00, tik_01, gor_01 at its first frames). The game's room-entry transition unfolds the new room on a turning page over black. At 16:9 the black backing ends about 24 pixels from each edge; at 21:9 about 158 pixels, so strips of the room are visible at both sides while the page turns. The black band is about 805 pixels wide at both aspects, so it is drawn in a camera that the widescreen scale shrinks. The page itself keeps its 4:3 shape and shows the whole wide frame squeezed into it (the tik_01 page shows the green pipe that is outside the 4:3 view).
Evidence: `pageflip_aaa00_169.png`, `pageflip_aaa00_219.png`, `pageflip_tik01_43.png`, `pageflip_tik01_169.png`, `pageflip_tik01_219.png`.
Cause, from the code (not traced to the exact draw): the page-flip types (`FADE_PAGE_FLIP_*`, `fadedrv.c` around line 1100) draw `disp_maku` / the paper pose on `CAMERA_3D_IMAGE`, which `portWidescreenCamera` scales like the 3D camera. The black around the page then covers a fixed 4:3-based width.
Fix: draw a full-width black quad (`WIDE_X` edges, or on the unscaled `CAMERA_FADE`) behind the page for these fade types. To keep the page's contents unsqueezed, either copy only the central 4:3 part of the EFB for the page texture, or widen the page model's x by the edge scale. A trace of which camera draws the black (`TTYD_TRACE` or a print in `disp_maku`) would settle the exact place.
Status: fixed (Fixes, 2). The black is part of the page model itself, so widening the model by the edge scale fixed both the black and the squeeze.

### 3. The title and the file screens are pillarboxed (high, first thing a player sees)

The title picture (logo, characters, hills) and the white flash before it stay 4:3 with black bars at both sides; only the red curtains reach past them, and at 21:9 the curtains end too, with black past them. The memory-card / file screens (red curtain backdrop) are 4:3 with black bars at both sides at 16:9 and 21:9. The file-select list itself was not reached (see "Not checked").
Evidence: `title_169.png`, `title_219.png`, `title_flash_219.png`, `fileselect_169.png`, `fileselect_219.png`.
Fix: for the backdrops, a full-width draw (`WIDE_X` x edges, or the unscaled background camera) of a texture that wraps or is extended; for the title picture, side art is needed (a mirrored or extended copy of its sky and hills, or a blurred copy as `side_bars` does for bars). The title is drawn from `src/sequence/seq_title.c` (`disp1`/`disp2` on `CAMERA_3D` / `CAMERA_3D_EFFECTB`); the file screens from `src/sequence/seq_load.c` (not read in detail).
Status: fixed (Fixes, 3). The memory-card screens' curtain turned out to be the closed 3D stage curtain from `fadedrv.c`, not `seq_load.c`.

### 4. Background pictures are stretched, not widened (medium, every outdoor room)

Rooms: every room with a backdrop picture (hei_00, nok_00, aaa_00, tou_00, gor_00 clouds, hom_00 sunset, bom_00 snow sky and others). `CAMERA_BACKGROUND` is left unscaled on purpose, so the 608-unit picture is stretched to the frame: clouds are 1.40x wider at 16:9 and 1.84x at 21:9, while the 3D in front keeps its shape. In the centre-area comparison this is the largest difference in outdoor rooms (clouds moved outward).
Evidence: `bgstretch_hei00_43_169_219.png` (the same sky at 4:3, 16:9, 21:9), `bgstretch_diffs.jpg` (4:3, wide centre, difference; the sky lights up, the 3D does not).
Cause: `bgDisp` in `src/driver/bgdrv.c` draws a 0..608 quad with u 0..1, and `portWidescreenCamera` exempts `CAMERA_BACKGROUND` from the x scale.
Fix: scale the background camera like the others and draw the quad from `WIDE_X` edges with u widened by the same factor around the scroll offset. The backdrop already scrolls horizontally with the camera's yaw (four screen widths a turn), so its texture repeats sideways and the extra width is real art, not a stretch.
Status: fixed (Fixes, 4). The camera stays unscaled and only the u range widens, which gives the same result.

### 5. Indoor rooms and some sets show black void past their walls (medium; known, now measured)

At 16:9 the parts outside 4:3 are mostly or wholly black in these rooms (share of black pixels in that side in brackets):
- gor_10 (both sides 1.00; dark already at 4:3), eki_01 (right 1.00), aji_01 (left 0.98, 0.92 at 21:9 with stray stars), las_00 (left 0.93), gon_00 (left 0.71), kpa_00 (left 0.58 and a corner at the right), tik_00 (left 0.56, the dark inside of a foreground wall), dou_00 (left 0.50), rsh_00_a (right 0.33), eki_00 (bottom left: a floor edge and void under it), mri_01 at 21:9 (left 0.62), rsh_02_a at 21:9 (right 0.47).
- kpa_00 (Bowser's castle set) is the worst open set: the flat lava-and-sky backdrop and the rock cut-outs end with black past both sides.
- Outdoor sets show their own ends at 21:9 rather than black: nok_00 (the path ends in mid-air and the hill backdrop ends), gor_01 (sky past the last building). The black under floors in usu_01 and gra_00 is the game's own art and is there at 4:3 too.

Evidence: `edge_eki01_169.png`, `edge_aji01_169.png`, `edge_aji01_219.png`, `edge_las00_169.png`, `edge_gon00_169.png`, `edge_kpa00_169.png`, `edge_tik00_169.png`, `edge_dou00_169.png`, `edge_rsh00_169.png`, `edge_rsh02_219.png`, `edge_mri01_219.png`, `edge_eki00_169.png`, `edge_nok00_219.png`, `edge_gor01_219.png`.
Fix: the geometry is not there, so the port can only hide it: per-room (or per-camera-position) limits on the visible width, for example a list of rooms that use `aspect_max` 16:9 or pillarbox with the blurred `side_bars`, or a per-room clamp of the widened frame to the room's bounding box projected on screen (`camdrv.c` already projects the map bounding box for its near/far planes, around line 330). Changing aspect per scene was rejected before for being inconsistent (docs/PC_PORT.md); bars that fade in only where a room ends would be the gentler version.
Status: limited per room, with bars past the limit (Fixes, 5). The limit can be switched off.

### 6. Battles at 21:9 show the theatre's walls (medium at 21:9, small at 16:9)

At 16:9 the proscenium (red side curtains and wooden frame) reaches the edges in every battle tried; only a few pixels of grey floor show at the bottom corners. At 21:9 about 150 pixels each side show the theatre's grey side walls, a wall clock at the left and a dark floor, and the audience seats stop at the proscenium's width. The closed curtain at battle start is wide enough, but the walls show under it at 21:9. No audience members were present in the battles tried, so no audience popping was seen.
Evidence: `battle_hooktail_169.png`, `battle_hooktail_219.png`, `battle_grubba_219.png`, `battle_curtain_219.png`.
Fix: either cap battles at 16:9 (the stage is built for it) with bars past that, or widen the stage set: scale the theatre's side curtains and audience rows outward. The stage darkening quads in `BattleStageDispDark` (`src/battle/battle_stage.c`) are 1280 units wide at z 105; they were not seen to fall short, but they would be the next thing to check at 21:9 and 32:9 during a darkened move.
Status: battles capped at 16:9, with bars past it (Fixes, 6).

### 7. Smaller items

- Speech bubbles and message windows stay in the central 4:3 area at 16:9 and 21:9, with tails pointing at the speaker; letterbox bars and the area-name bars span the full width (`camdrv.c` uses `WIDE_X`). Evidence: `bubble_letterbox_219.png`. A speaker standing outside the 4:3 area at 21:9 was not tried, so whether the tail then points the wrong way is not known.
- HUD with `wide_hud=1` at 21:9: HP/FP at the left edge, star points and coins at the right edge, as intended. Evidence: `hud_widehud_219.png`. Without it the HUD stays centred.
- The black fade of a loading-zone map change covers the full width at 21:9. Evidence: `fade_mapchange_219.png`.
- Glitzville's in-world screen (`screen_capture` in `src/battle/battle_stage.c`) copies 608x480 of the EFB, which in widescreen holds the squeezed wide frame. It was not seen on screen.

## Culling (things vanishing at the sides)

No case of an object present at 4:3 and missing at the same place at 16:9 was found. In the centre-area comparisons the differences near the 4:3 borders came from the stretched backdrop (item 4) and from animation, not from missing objects. NPCs and map objects outside the 4:3 view are drawn at 16:9 and 21:9 (Rogueport's rat and posters, Petalburg's NPCs at the far right at 21:9 while walking, Glitzville's Toads, the snow particles in Fahr Outpost). This matches the code: `npcdrv.c`, `mapdrv.c` (`MAP_CLIP_IN`) and `mobjdrv.c` widen their tests with `WIDE_X`. No other projection-space culling test was found in `src/` with a search for `MTX44MultVec` and `fabs(...x) >` tests.

## Not checked

- The file-select list itself (the run stopped on the memory card's file creation), chapter intro cards, Peach and Bowser interludes, the Game Over screen, the ending.
- Iris and spot fades that close on Mario (pipes and doors entered by playing): no run triggered one. From the code they are drawn by `disp_texture` (`fadedrv.c`) with a clamped hole texture on a quad of 304 units, on `CAMERA_FADE` (unscaled) or `CAMERA_3D`; on the unscaled camera the hole would be stretched into an ellipse and placed off Mario when he is not at the screen's centre. This needs a capture.
- Battles with an audience, special moves and stage darkening, the Art Attack / Sweet Treat style full-screen effects (`sac_scissor.c` copies 608x480 of the EFB), Bowser and Peach segments.
- 16:10, 32:9 and window-shaped aspects, `aspect_max` and `side_bars`; JP and EU builds; Mac, iPhone and Android.
- Moving the camera with the free camera; mirror mode with widescreen.

## Fixes (branch 040-widefix, 2026-11-04)

All of them change drawing only and apply only with a frame wider than 4:3. The scratchpad below is
`C:\Users\LT\AppData\Local\Temp\claude\C--Users-LT-Repos-ttyd-port\de5a5c05-96a0-4b83-8f42-73464ef9e212\scratchpad\`.
Before-and-after sheets are in `...\widefix\pairs\`. Each sheet stacks the pictures as before, after, before, after, and each
picture is labelled with its source file. "Before" is the survey's run (`...\wsurvey\runs\<tag>_<aspect>\`) or a run of the
survey's binary (`..._b`), and "after" is this branch (`...\widefix\runs\<tag>_<aspect>_a\`), with the same input script and
frame. A capture holds the game's frame only, without the window's side bars, so a frame limited by item 5 or 6 is narrower
than the window. The scripts are in `...\widefix\` (`run.sh`, `sweep.py`, plans `plan*.txt`, `tools\`).

1. **Start menu backdrop** (`src/window/win_root.c`, `winRootBgDrawDisp`): the captured frame is drawn from `WIDE_X(-w/2)`
   to `WIDE_X(w/2)`, so the field behind the menu is unsqueezed and lines up with the field. Sheet `1_startmenu.png`
   (gor_01 at 16:9, and at 21:9, which gor_01 now limits to 1.9).
2. **Room-entry page flip** (`src/driver/imgdrv.c`, `imgDisp`): the black around the turning page belongs to the page model,
   so a flat page that holds a copy of the whole frame is widened by the edge scale. This applies to an image with no virtual
   point, a full 608x480 capture that is not an offscreen rectangle, on a scaled camera. The black then reaches the edges and
   the page's picture keeps its shape, landing exactly on the room. The paper clouds (`evt_cloud.c`) use the same kind of
   capture and are widened too; they were not seen. Sheet `2_pageflip.png` (aaa_00 at 16:9, tik_01 at 21:9).
3. **Title and file screens**:
   - The title picture is 4:3 art with nothing past it. It ends sharply, without repeating, so the sides behind the curtains
     continue its outer pixel columns outward (clamped texture), dimming toward the frame's edges (`titleWideSides`,
     `src/sequence/seq_title.c`). This soft extension of its sky, hills and grass was chosen over a mirror, which would
     duplicate Bowser and Flurrie, and over black.
   - The white flash before the title and the other flat colour fades on the 3D or 2D cameras reach the frame's edges
     (`disp_texture`, and the white-out in `disp_dummy_black`, `src/driver/fadedrv.c`). The iris and spot fades are not
     changed, so their holes keep their shape.
   - The memory-card screens' closed red curtain is the 3D stage curtain on the image camera (`disp_maku`, `fadedrv.c`). It
     is widened by the edge scale, like the curtains on the unscaled fade cameras, so its pleats are a little wider: 1.4x at
     16:9, 1.84x at 21:9. Curtains placed in the world and the four-page flip are not changed.
   - The file-select list's backdrop repeats sideways. It now spans the frame, with its edge strips at the frame's edges and
     the repeats in step with the 4:3 ones (`loadDrawBgWide`, `src/sequence/seq_load.c`).
   - Sheets `3_title.png` (16:9 and 21:9, and the white flash) and `3_filescreens.png` (memory card at 16:9 and 21:9, the
     file list at 21:9).
   - The curtains of the title (`FADE_CURTAIN_SIDES_A2_TOP`, unscaled fade camera) still end about 60 pixels from the edges
     at 21:9. The dimmed extension shows past them.
4. **Background pictures** (`src/driver/bgdrv.c`, `bgDisp`): a backdrop whose texture repeats (`GX_REPEAT` in s) keeps its
   quad full width on the unscaled background camera, and its u range widens around the middle by the edge scale. The clouds
   keep their 4:3 shape and the sides show more of the picture. A clamped backdrop still stretches as before. Sheet
   `4_background.png` (hei_00 at 4:3, then before and after at 16:9 and 21:9).
5. **Rooms whose sets end** (`port/src/aurora/widescreen_aurora.c`, `kRoomLimits`): a table of maps and the widest aspect
   each shows, measured at the survey's entrance views (the widest frame with no column of void):

   | Map | Widest aspect |
   |---|---|
   | eki_01, aji_01, las_00 | 4:3 |
   | gon_00, kpa_00 | 1.40 |
   | dou_00 | 1.47 |
   | rsh_00_a | 1.60 |
   | mri_01 | 1.87 |
   | nok_00, gor_01 | 1.90 |
   | rsh_02_a | 2.00 |

   A wider frame stops at that aspect (`VILockAspectRatio`, as `aspect_max` does), and the rest is the existing side bars,
   blurred with `side_bars`. The setting `aspect_scene_limits` (Display, "Bars where a scene ends") is on by default and can
   be switched off. Rooms not in the list are not changed. tik_00 (the dark inside of its own foreground wall), gor_10 (dark
   at 4:3 too) and eki_00 (a floor edge) were left out as the game's own art. Sheets `5_rooms_a.png` (eki_01 16:9, aji_01
   21:9, kpa_00 16:9) and `5_rooms_b.png` (mri_01, nok_00, gor_01 at 21:9).
6. **Battles at 21:9**: the same limit with 16:9 in any battle (`gp->inBattle`). The proscenium fills 16:9, and past it are
   bars instead of the theatre's side walls. Sheet `6_battle.png` (Hooktail and Macho Grubba at 21:9). While the battle's
   curtain rises, the side walls still show at 16:9, as they did before.

Headless: `GXGetTexObjWrapS`/`T` and `GXInitTexObjWrapMode` were added to `port/src/headless/gx/gx_texture_pc.c`; the
headless build (`build-pc-us`) links.

### How the fixes were checked

- **No change at 4:3**: the survey's binary and this branch were run with the same scripts at 4:3: hei_00 idle, the aaa_00
  page flip, the Hooktail battle, and the title through the memory card to the file list. Every capture is identical pixel for
  pixel (mean difference 0.000; `...\widefix\tools\ba43.py`).
- **Same game at every aspect**: `TTYD_FINGERPRINT` was compared, one line per frame, between 4:3, 16:9 and 21:9 runs with the
  same input. The runs were a walk through a loading zone (gor_01 to gor_02), the Hooktail battle, the Start menu in gor_01,
  the tik_01 page flip and eki_01 (a limited room). Every frame's hash is identical (`...\widefix\runs\fp*_<aspect>_a\fp.txt`).
  The last line of one 4:3 run is a frame that the other run's timeout cut off.
- **Builds**: Windows clang US Aurora (`build-pc-us-aurora`) and headless (`build-pc-us`).

### Not fixed, and why

- **The limits are per room, not per camera position.** A listed room is limited even where its camera shows no void (for
  example eki_01, aji_01 and las_00 are 4:3 throughout). A room's other camera positions could show void that its entrance
  view did not, so the table may need more entries as rooms are played. A limit that follows the camera (the map's bounding
  box projected each frame) was not tried: the frame would change shape while walking.
- When a room or battle limits the frame, the HUD (with `wide_hud`) moves to the new edges.
- **Not seen on screen**:
  - Sweet Treat / Art Attack style effects (`sac_scissor.c`, an offscreen-rectangle capture, deliberately left out of
    item 2).
  - The paper clouds.
  - Iris and spot fades on the 3D camera.
  - Glitzville's in-world screen.
  - Speech-bubble tails for a speaker outside 4:3.
- Battles were not widened (the side curtains and audience rows); they are limited instead.
