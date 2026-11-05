# After the story: maps, trophies, the Paper Tower

Things to do once you know the way around, and the record of what you've done.

## Area maps

Settings > **Area map** (while a game runs) draws a map of the area Mario is
in, or any other area you pick, made from the game's own room data: every
room the game has in that area, and the ways between them.

- A room you've been in is a paper card with its number (`gor_01` is "01");
  one you haven't is a grey "?". Mario's room has a red edge and a red dot.
- A way you've walked through (either way) is drawn in ink; one you haven't
  taken yet is dashed. A way out of the area ends in an arrow with the area
  it leads to.
- The gold diamonds and suns on a card are the Star Pieces and Shine Sprites
  still to find in that room (the same table as the Enhancements' tracker).
  Hover over a card for the counts and the exits not taken.

A room's place on the map comes from its exits: each one's name says which way
Mario walks through it (north, south-west...), so the room it leads to is
drawn on that side. Pipes and doors have no direction and take the nearest
free place, so the map is a guide to how rooms connect, not a drawing to
scale.

What a file has seen is kept beside the memory cards, not in the save:
`<user folder>/AreaMaps/<game id>/file<N>.txt`. It counts from when the file
is first played with this version; a new game started in the same slot under
another name starts a new map.

On a device with a second screen, the second screen's **Map** page shows the
area's map under "This room".

The tables come from the decomp's rooms (`tools/port/gen_area_maps.py`, which
writes `port/src/common/area_maps_gen.c`); run it again after the rooms
change.

## The trophy room

Modes > **Trophy room** > *Visit the trophy room* (or the same row on the
Start menu's Settings tab, under Modes) takes Mario to a room under Rogueport
that fills as the file plays:

- the Crystal Stars the file has, turning on the pedestals they stood on when
  the Thousand-Year Door opened;
- the bosses it has beaten, as small paper cut-outs of their own battle models
  (Bonetail once he's beaten in the Pit);
- a plaque for each achievement, gold once earned: stand on the pedestal in
  the middle of the room to read them.

The room is the Thousand-Year Door room's ground (its files) with a set-up of
Paper Cut's own: none of that room's story scenes run, and the Door itself
stays shut. Its two side ways lead where the real room's do, into Rogueport's
underground, so you walk out like from any other room. Visiting changes
nothing in the save; it isn't offered during online co-op or a replay.

## The Paper Tower

Once a file has finished the game, Modes > **The Paper Tower** > *Climb the
Paper Tower* (or the Start menu's row) starts a climb of 30 floors, each one
of the game's own corridor rooms (Petal Meadows to the Palace of Shadow) with
only its ground: no people, enemies or story. Mario comes in by one road and
leaves by the other, which leads to the next floor. Between the two stand the
floor's gates:

| Gate | Looks like | To get through |
|---|---|---|
| Paper-mode gap | paper slats with thin gaps | Paper mode: slip through sideways |
| Tube-mode gap | a low paper wall with a gap at the bottom | Tube mode: roll under it |
| Wind wall | streaks of wind blowing back | Paper mode: side-on, the wind can't push you |

The first floors have one gate, then two, then three, and from the tenth the
kinds are mixed. Every fifth floor is a battle, one of the Pit of 100 Trials'
own, deeper the higher you are; winning one gives a few coins. The 30th floor's
battle is the last. Walk back out the way you came in to start a floor again.

The floor, the time and the floor's gates show in the top right corner. *Leave
the tower* takes Mario back to the room he started from; so does finishing.
A Game Over ends the climb as it would anywhere.

**Seed.** Modes > *Paper Tower seed* picks the rooms, their order, the gates and
the battles: 0 (the default) is the day's climb, the same for everyone that
day (by the UTC date); any other number is that climb every time, to share.

**Skip this floor** moves on when a floor won't let Mario through (a room whose
ground needs something the tower doesn't set up); the climb then counts as
helped: it sets no best and earns no achievements.

The best floor and the best time to the top (unhelped climbs only) are kept in
`<user folder>/paper_tower.txt`. The tower moves Mario and gives coins, so it
isn't offered during online co-op or a replay.

## Daily shop specials

With Modes > **Daily shop specials** on, five shops (Rogueport's, Petalburg's,
Glitzville's, Twilight Town's and Fahr Outpost's) each put one rare item on a
shelf at 30% off: an Ultra Shroom, a Life Shroom, a Jammin' Jelly, a Shooting
Star, a Repel Cape and the like. Each day brings different ones, the same for
everyone that day (by the UTC date). The special takes one shelf's place for
the day; that shelf's own item is back the next day, or with the setting off.
Walking into one of those shops shows the day's special for a few seconds, and
Modes > *Today's specials* lists them all.

The day's seed is `port/include/port/date_seed.h`, shared with the Paper
Tower's day climb. The specials change what a shop sells, so they're off
during online co-op and replays.

## More achievements

With Achievements on (the Achievements tab), 31 more come with the new
places and modes, each with the usual pop-up when earned, and each a plaque in
the trophy room:

| Achievement | For |
|---|---|
| Know Your Way Around, Down Under | every room of Rogueport, and of its underground, on the area maps |
| Explorer, Cartographer | 100 rooms, and every room, on the area maps |
| Hall of Fame, A Full Shelf | visiting the trophy room; visiting it with every Crystal Star and every boss in it |
| Ten Floors Up, Thin Air, Top of the Tower, Paper Airplane | the Paper Tower's 10th and 20th floors, the top, the top in under 30 minutes (no skipped floors) |
| Bargain Hunter, Regular Customer | buying 1 and 10 daily shop specials |
| Brotherly Rivalry, Old Rivals | finishing 1 and 10 versus battles |
| Shuffled, Stars Anywhere, Seed Master | playing an open-world randomizer file; holding 3, and all 7, Crystal Stars on one |
| Racing a Ghost, Faster Than Your Shadow | racing a ghost; being 10 seconds ahead of one in a room |
| Better Together, Rock Bottom, Together | the Pit's 25th and 100th floors in co-op |
| Boss Rush, Crowd Favorite, Rematch | winning a boss rush; 10 rounds in one endless Glitz Pit run; winning a boss refight |
| Once More, With Feeling | finishing a chapter again in chapter select |
| One Life, Remixed, Green Thunder | finishing chapter 1 on a hardcore file; a chapter with Remix: enemies on; a chapter as Luigi |
| Say Cheese, Photo Album | 1 and 25 photo mode pictures |
| Two Plumbers, Two Places | playing online co-op |

As before they're kept per save file beside the memory cards
(`<user folder>/Achievements/<game id>/file<N>.txt`), now with the modes'
counts as `c <counter> <value>` lines, and none is given or counted after the
Practice tab has changed the game, until a file is loaded again.

**Adding one** (for a mode of your own, say): a row in the list in
`port/src/common/achievements_pc.c` (`{id, name, note, K_COUNTER, goal,
"counter"}`; the id is never renamed), and a call where the mode does the
thing: `portAchievementCounterAdd("counter", 1)` for a count, or
`portAchievementCounterMax("counter", value)` for a best
(`port/include/port/achievements.h`). The 0.4.0 modes are watched from
`port/src/aurora/achievements_modes_aurora.c` through what each mode already
tells its page, so their own files didn't change.
