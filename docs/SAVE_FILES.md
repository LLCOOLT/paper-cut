# Save files

The memory card has four files, as on the GameCube. Paper Cut keeps every save beyond those four in a library of its own, and can give each file its own settings. The card stays the GameCube's `.gci`, so it still moves to and from Dolphin. Code: `port/settings/features/save_files.cpp`, `port/src/common/cardsave_pc.c`, `port/src/common/save_info_pc.c`.

## The library (Saves > Save files)

With **Keep every save file** on (the default), each save the game writes into one of the card's files is also copied to the library:

    <user>/Save files/<JAP|USA|EUR>/<id>/save.bin    the file's slot, as on the card (0x4000 bytes)
                                         info.txt    name=<the name you gave it>
                                         thumb.bmp   the picture as shown when it saved

The list shows each file's picture, name (the one typed at New Game, or one you give it with **Rename**), chapter, Mario's level, play time, who played it (Mario or Luigi), when and on which map it was saved, and which of the card's files holds it now.

- **Play in that file** puts a library file into the card file chosen in **Play in** (File 1 to 4). Whatever save was there is added to the library first, and the card is backed up as for any change (Saves > Restore a backup). While the game runs, the card is written once the game isn't writing it and the game starts again, as Restore does; on a phone or tablet, from the launcher.
- **Add the card's saves to the library** copies in the card's saves the library doesn't have yet (a card from Dolphin, say).
- **Delete** (pressed twice) removes a file from the library only; a save that is also on the card stays there.

The file's id is what ties a card file to its library entry: a file loaded and saved again updates its own entry; the file select's Copy makes a new one. It is kept in the card file itself (below), so it goes with the card.

## Settings per save file

With **Settings per save file** on (the default), these are saved with each file at each save and set again when that file is loaded:

- the difficulty and its custom enemy HP and attack, the action-command assists, Retry on Game Over;
- the Enhancements' settings and the Remake page's Remake balance, Bag size, Skip scenes, Coins up to 9,999, Use healing items when the bag is full and Level-ups heal every partner, but not Play as (it stays the player's);
- the challenge rules and the Remix rules.

Display, sound, controls and everything else stay the same for every file. A file saved before this, or saved on a GameCube or in Dolphin, has none: loading it leaves the settings as they are. A save put in place without the card (quick resume, the save warps) keeps the settings as they are, too.

## A copy for Dolphin or a GameCube (Saves)

Paper Cut's own items (the badge set's, the badge maker's, Zess T.'s new dishes) have ids past the end of the GameCube game's item table, so that game can't read a save holding one. **Make the copy** writes each card that has a save of this game to

    <user>/For Dolphin/<JAP|USA|EUR>/01-G8Mx-mariost_save_file.gci

with those items sold, in every slot that verifies: each for the coins a shop pays for it (Mario's coins stop at 999), held, stored or worn; a worn one is taken off, and Mario's HP, FP and BP and the partners' HP are worked out again as the game does when a badge comes off. The checksums are made again, so the GameCube game and Dolphin read the copy as an ordinary save. The card here is left as it is. Send saves to another device offers the same copy as a second link on its page, for a card that has such items (docs/SAVE_TRANSFER.md). Code: `portSaveDolphinSafe` in `port/src/common/save_convert_pc.c`.

## Paper Cut's block in the card file

The id, the character and the file's settings are a few lines of text in each slot's last part, which the game neither fills nor checksums (offset 0x3800 of the 0x4000-byte slot, before the slot's version, size and checksums):

    pcx1
    id=<16 hex digits>
    char=mario
    s.difficulty=1
    ...

The GameCube and Dolphin ignore it, and a save they write has none. Because it is part of the card's bytes, it goes wherever the card goes: online co-op's setup sends the host's card to the guest, and both games load the file, with its settings, in the same frame; the save transfer and the backups carry it too.
