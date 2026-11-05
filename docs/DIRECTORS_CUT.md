# Director's Cut

Modes > **Director's Cut** (off by default) puts back content the game's code still has but never uses. Each restored thing has a page in the **Director's Cut journal** (Modes, on the settings page and in the Start menu). Code: `port/src/aurora/directors_cut_aurora.c`, `port/settings/features/directors_cut.cpp`.

## What comes back

- **Seven cut badges.** The battle code has their moves in full, but nothing in the game gives them, and the item table lists them hidden with no name: Mega Jump, Mega Smash, Mega Quake, Super Charge, Super Charge P, Triple Dip (on its own; normally only two Double Dips give it) and Lucky Start P. Dazzle sells them for Star Pieces. Their names and texts were never finished, so Paper Cut gives them its own. They sort after the other badges.
- **Boggly Woods' other enemy groups.** The area's data has a second set of enemy groups for its field enemies, Clefts with Pale Piranhas among others, chosen by a flag nothing ever sets. With Director's Cut on, the field enemies draw from that set; the flag itself isn't written to the save.

Turning it off hides the badges from Dazzle again (badges already bought stay yours) and puts Boggly Woods back as it was.

## Online and replays

The setting is one of the host's settings online already sends, and everything above follows from it and the game's own state (the badge stock, the enemies' random choice through the game's own random numbers), so both games of a session play the same.

## Not (yet) restored

Other unused content exists in the decomp but needs more than a switch: test enemies only in the European release's test module, rooms whose maps may not be on the disc, enemy groups inside other areas' modules, a level-up confirmation box with no text on the disc, and two badges with no effect at all.
