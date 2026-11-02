# 0.2 verification pass

Every feature the 0.2 milestones reported as "built but not tried", tried in the game (US; JP and EU where
the text or layout is the version's own). Runs: `TTYD_FAST=1`, a fresh user folder each, sample saves by
`TTYD_LOAD_SAVE` or a card file in slot A, `TTYD_MAP` warps, scripted input (`TTYD_INPUT`: `hover`, `tap`,
`back`, `wheel`, `select`, `give`, `flag`), EFB captures (`shot`), and window captures (PrintWindow) for
what Aurora draws over the picture (ImGui overlays, the settings page, the picture filter). Play as Luigi
is checked elsewhere.

| Milestone | Item | Result | Notes |
|---|---|---|---|
| M4b | File select: the four files | pass | hover moves the cursor to a file; a click on file 1 loads it |
| M4b | File select: copy / delete | pass | Copy Data and Erase Data by click; hover picks a file in their modes; right click backs out |
| M4b | File select: options | pass | Sound Settings by click; hover steps to Surround; right click closes it |
| M4b | Pause menu: tabs | pass | hover steps to the tab, click opens it (US and JP) |
| M4b | Pause menu: Items list | pass | the two list buttons by hover; items two to a row by hover; wheel moves a row (US and JP) |
| M4b | Pause menu: Badges list | pass | All / Equipped buttons; badge rows by hover; wheel -3 moves three rows (US and JP) |
| M4b | Pause menu: Party and Mario move lists | pass | partner moves (X) and Mario's Specials rows by hover; wheel in Specials |
| M4b | Battle: ring and sub-lists | pass | hover turns the ring to Items, a click on the front bubble opens it, rows by hover, wheel down one |
| M4b | Right click as back | pass | file select, pause menu levels, battle item list back to the ring |
| M4b | Scroll wheel in lists | pass | pause Items/Badges/Specials, battle item list |
| M3b | Save Block: store an item | pass | Save / Store / Take out / Leave menu; Honey Syrup stored; Leave returns to the field. The menu's own four lines and its three notices are English in JP and in the EU's other languages (the item lists are the game's) |
| M3b | Save Block: take an item out | pass | the stored item listed and taken back; "Nothing is stored." after (US, JP, EU French) |
| M3b | Badge loadouts | untried (harness) | the Loadouts section shows four empty rows with Wear disabled in play; Store worn / Wear need a real mouse click (posted window messages don't reach ImGui) |
| M3b | Walk boost | untried (harness) | it reads the keyboard / SDL gamepad directly; scripted input drives only the GameCube pad |
| M3b | Star Piece / Shine Sprite tracker | pass | "Star Pieces 1/5" in the top right on gor_01 after one of its flags is set |
| M3b | Faster field text | pass | the Save Block tutorial prints about three times as much text in the same frames |
| M3b | Shop sell list "xN" | fixed | US marks items and badges (x3, x2). JP: the mark touched the name (no winFontSetWidth there); a long name is now narrowed to leave it room |
| M3b | Jukebox play list | fixed | the track heard on the map is listed (BGM_STG2_MRI1); "1 tracks heard" now reads "1 track". Playing a track needs a real mouse click: untried |
| M2 | HUD size in battle menus | fixed | at 150 % a sub-menu's list, scaled about the screen's left edge, ran off the right edge, and its help window off the left; the list now scales about its own middle kept on screen and the help window about its own; the mouse's hit boxes follow the scaled ring and list (they used the unscaled positions) |
| M2 | Sharpen filter | pass | window captures with and without: edges visibly crisper (the filter is in the present pass, not in `TTYD_SHOT` captures) |
| M2 | Texture detail (LOD bias) | untried (harness) | no change at any value on gor_01 and muj_00; 16x anisotropic filtering doesn't change them either, so no mipmapped texture was sampled there to show it |
| M5b | Boss rush | fixed | Lord Crump (boss 1) and Bowser and Kammy (step 15) start with the loadout; the timer box over the picture was a fixed 252 wide and cut a long boss name and the time off at the right edge: now as wide as its text |
| M5b | Endless Glitz Pit | fixed | a round starts with a Pit team; its condition showed the game's red "No messages" (looked up before the Pit map's messages were loaded): now looked up on the Pit map ("Defeat your opponent in five turns or less!") |
| M5b | Mod manager list | pass | three example mods listed with authors, what they change, switches and order arrows |
