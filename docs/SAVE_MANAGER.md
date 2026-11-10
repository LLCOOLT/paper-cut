# Save manager

The Saves tab brings saves in from other GameCube tools, sends them out in their formats, and backs up every save at once. Code: `port/settings/features/save_manager.cpp`. Moving saves between two devices running Paper Cut is docs/SAVE_TRANSFER.md; the library of every save file is docs/SAVE_FILES.md.

## Import saves

**Import a save...** picks a file, or a file is dropped on the window (on the launcher, or over the game, which opens the Saves tab). Read by its contents, not its name:

| File | From |
| --- | --- |
| `.gci` | Dolphin's GCI folders (`01-G8ME-mariost_save_file.gci`), Paper Cut's own cards |
| `.gcs` | GameShark / MaxDrive exports (a 0x110-byte header before the save) |
| `.sav` | Action Replay exports (a 0x80-byte header; the entry's 16-bit fields byte-swapped, put back as Dolphin does) |
| `.raw`, `.mcp` | a whole memory card: Dolphin's Memory Card mode, Nintendont, a card dumped with GCMM |

From a whole card, every save of this game is taken (the Japanese, US and European games keep separate files on one card); other games' files are left. Each save found is listed with its release; the ticked ones go into their release's card. A card that already has a save asks once more, and the save there goes to the dated backups first (Restore a backup undoes it). The running game's card is written once the game isn't writing it, and the game starts again to read it; on a phone or tablet that card is imported from the launcher.

A save's release isn't changed on import: a European save goes into the European card. Copy a save to another release converts it.

## Export saves

- **Export .gci** for each card that has a save: the file for Dolphin's GCI folder (or a GameCube, with GCMM).
- **Export a memory card image (.raw)...**: one card holding every release's save, for Dolphin's Memory Card mode or Nintendont. The card is the smallest that fits (4 Mbit, 59 blocks), formatted for slot A, its encoding Shift JIS when it holds only the Japanese save (a Japanese console's card), Western otherwise.

On a computer the system's save dialog asks where; on iOS the system's export picker does (Files, iCloud Drive or another app's folder); on Android the files go in `<user>/Exports`. A save holding Paper Cut's own badges or dishes needs A copy for Dolphin or a GameCube first (docs/SAVE_FILES.md); the export is the card as it is.

## Back up everything

**Back up all saves now** copies every release's card and the save-file library into `<user>/Backups/All <date>/`. Each one listed can be restored: its cards over the cards (each save there backed up first, as above) and its library files added back to the library. The dated backups the game keeps on each save (Backups kept) go on as before.
