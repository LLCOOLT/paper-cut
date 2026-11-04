# A second screen for Paper Cut: ideas

For dual-screen handhelds like the AYN Thor (Android, a main screen and a small touch screen below). The same companion can also run on a PC's second monitor, or on a phone or tablet beside the TV.

## What it could show

**1. The Start menu, live (the main one).** You get the pause menu's information without pausing:
- the party: Mario's and each partner's HP and FP, the level and Star Points;
- the badges worn and the BP left;
- the items: tapping one in the field uses it, the same as choosing it in the menu;
- the coins, the Star Pieces and the Shine Sprites;
- the Crystal Stars found so far.

Tapping a partner switches to them (the quick swap).

**2. The map.** Where Mario is, the "where next" hint from the Journal tab, and each room's Star Pieces and Shine Sprites (found / left), from the tracker that already exists.

**3. In battle.**
- Every enemy's Tattle entry: HP, attack, defence, and what it does. Hidden until Goombella has tattled that kind, as in the game.
- The turn order and the Star Power gauge.
- For the move being used, the action command it needs.

**4. Controls on the touch screen.** The on-screen buttons we already have for phones, moved off the picture, plus quick buttons:
- partner swap and the bag;
- save state, fast-forward and screenshot;
- the Start menu's settings.

**5. The Journal while playing.** Story progress, the Trouble Center's open requests, the recipes found and the Tattle Log, all to browse without pausing.

**6. A message log.** The last lines of dialogue, to re-read anything that went by too fast. With text-to-speech later, a button to hear them again.

**7. For runners and streamers.** The speedrun timer and splits, the input display and the memory watch on the second screen, keeping the main picture clean.

**8. Co-op.** Player 2's side: the partner they play, its field ability and its HP.

## Suggested first version

Start with **1 (the Start menu, live)**, **2 (the map)** and **3 (battle)**. They're the ones that make a second screen worth having, and the information is all in the game's memory already.

Then **4 (controls)**, which frees the main screen on the Thor entirely.

## How it would work

**Finding the second screen.** On Android, the system reports the extra display (DisplayManager). The companion opens on it when it's there, and its own setting can turn it off.

**Drawing it.** The companion is a separate, simple UI fed with the game's state a few times a second. It doesn't draw through the game's renderer, so it costs nothing on the main screen.
- On Android it's a native view on the second display (the Presentation API).
- On a PC it's a second window, which can go on another monitor.
- For phones and tablets, an optional local web page from the game (a QR code to join) could carry the same UI.

**Taps and online co-op.** Anything tapped (using an item, swapping partners) becomes input the game reads like a button press. So it stays safe for online co-op: the other player's game sees the same input.

**Languages.** All of it is in the six menu languages, with the game's own names for items and badges taken from the disc's text.
