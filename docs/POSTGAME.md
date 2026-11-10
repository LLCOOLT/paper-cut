# After the story: maps, trophies, the Paper Tower

Things to do once you know the way around, and the record of what you've done.

## Area maps

Settings → Enhancements → Field → **Area map** (while a game runs) draws a map of the area Mario is
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
is first played with this version. Each time the game saves, the map as it is
then is kept too (`file<N>.saved.txt`), and loading the file reads that one:
rooms seen after the last save (then a Game Over or a reset) don't count. A new
game started in the slot starts a new map.

### Fast travel (0.5.0)

Settings → Enhancements → **Fast travel** (off by default; off, the game is
exactly the original). In the Start menu's Journal, on the **Map**, pick a
place and press A (or click or tap a place already picked): its area opens
with the rooms Mario has been in on this file, then a room's ways in he has
walked through. Pick one and the trip is made once the menu is closed. The
towns are also under **Travel to** (the same page, and the Start menu's
Settings tab), on the same rules.

- Only rooms and ways in from this file's map (as of its last save), and only
  trips Mario could make on foot now: never out of a stretch the story keeps
  him in (the Excess Express, Glitzville in chapter 3, Keelhaul Key until it
  can be sailed to any time, the Moon and the X-Naut Fortress, the timed
  escapes and the like), never into rooms the story has closed, never while
  someone follows him, and only as Mario. A place with no room to go to is a
  dark dot.
- A trip waits for any scene or battle to end; if the story changed meanwhile
  so that it no longer fits, it is dropped.
- Not in replays. Online, only while every player playing has it on.

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
  (Bonetail once he's beaten in the Pit), standing round the pedestal in story
  order: up the left side, along the step before the Door and down the right,
  each in a place of its own;
- a plaque for each achievement, gold once earned: stand on the pedestal in
  the middle of the room to read them; under them, once the file has won a
  raid ([RAID.md](RAID.md)), the raid plaque with the raids won and the
  rotation's bosses beaten.

The room is the Thousand-Year Door room's ground (its files) with a set-up of
Paper Cut's own: none of that room's story scenes run, and the Door itself
stays shut. Its two side ways lead where the real room's do, into Rogueport's
underground, so you walk out like from any other room. Visiting changes
nothing in the save; it isn't offered during a replay. Online, visit it from
the Start menu's Settings tab (Modes → Trophy room), which every game of the
session runs together, so everyone goes in the same frame; the room is the
host's file's (its stars and bosses), and the plaques over the picture are each
player's own achievements.

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
own, deeper the higher you are: the Pit's 20s on floor 5, its 30s on 10, 40s
on 15, 60s on 20, 70s on 25 and its 80s (Arantulas, Dark Bristles, Piranha
Plants, Spunias) on floor 30, the last. Winning one gives a few coins. Walk
back out the way you came in to start a floor again (but in a few rooms Mario comes in through a gate that stays shut behind him).

The floor, the time and the floor's gates show in the top right corner. *Leave
the tower* takes Mario back to the room he started from; so does finishing.
A Game Over ends the climb as it would anywhere.

**Seed.** Modes > *Paper Tower seed* picks the rooms, their order, the gates and
the battles: 0 (the default) is the day's climb, the same for everyone that
day (by the UTC date; online, the host's, and in a replay the recording's); any other number is that climb every time, to share.

**Skip this floor** moves on when a floor won't let Mario through (a room whose
ground needs something the tower doesn't set up); the climb then counts as
helped: it sets no best and earns no achievements.

The best floor and the best time to the top (unhelped climbs only) are kept in
`<user folder>/paper_tower.txt`, each player's own. The tower moves Mario and
gives coins, so it isn't offered during a replay; online it starts (and is left
or skipped) from the Start menu's row only, which every game of the session
runs together, from the host's seed or day.

## Daily shop specials

With Modes > **Daily shop specials** on, five shops (Rogueport's, Petalburg's,
Glitzville's, Twilight Town's and Fahr Outpost's) each put one rare item on a
shelf at 30% off: an Ultra Shroom, a Life Shroom, a Jammin' Jelly, a Shooting
Star, a Repel Cape and the like. Each day brings different ones, the same for
everyone that day (by the UTC date; online, the host's, and in a replay the recording's). The special takes one shelf's place for
the day; that shelf's own item is back the next day, or with the setting off.
Walking into one of those shops shows the day's special for a few seconds, and
Modes > *Today's specials* lists them all.

The day's seed is `port/include/port/date_seed.h`, shared with the Paper
Tower's day climb. The specials change what a shop sells, so they're off in
replays; online they follow the host's setting and the host's day, so every
game of the session sells the same.

## More achievements

With Achievements on (the Achievements tab), 35 more come with the new
places and modes: the 31 below, and four with the raid boss
([RAID.md](RAID.md#rewards)). Each has the usual pop-up when earned and a
plaque in the trophy room:

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
| Better Together, *Rock Bottom, Together* | the Pit's 25th and 100th floors in co-op |
| Boss Rush, Crowd Favorite, Rematch | winning a boss rush; 10 rounds in one endless Glitz Pit run; winning a boss refight |
| *Once More, With Feeling* | finishing a chapter again in chapter select |
| One Life, Remixed, Green Thunder | finishing chapter 1 on a hardcore file; a chapter with Remix: enemies on; a chapter as Luigi |
| Say Cheese, Photo Album | 1 and 25 photo mode pictures |
| *Two Plumbers, Two Places* | playing online co-op |

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

## More after the story (0.5.0)

Each of these is off until its setting is on (Modes tab, unless noted) or it
is started; the ones that change play are locked by Console-accurate and don't
run online or in a replay. What a file has done is kept in the save itself.

- **Whacka in the Pit.** Once Bonetail has fallen, Whacka waits on the 100th
  floor for a fight: his own disc model with Paper Cut's own moves (a butting
  rush, a floor-shaking quake every third turn, and a snack of his own bumps
  when he's hurt). Each win drops a Whacka Bump; after the first, the Whacka
  on Keelhaul Key never runs out of bumps.
- **Pit Mover.** At the Pit's entrance, once the Pit has been cleared:
  straight down to the 50th floor's rest room.
- **Exhibition matches.** Any Glitz Pit team ranked below Mario (all of them
  once he's champion), with no match conditions, Wings of Night and the
  Destructors included. Back where you started after it, won or lost.
- **The returning champion and Nighttail.** After the credits: the Golden
  Talon (Rawk Hawk's model gilded, with two Red Chomps in his colours, made
  with the Enemy maker's tools) in the Glitz Pit, and Nighttail (Gloomtail's
  model in night's colours, with Bonetail's sleepy breath) in the Palace.
- **Gold Medal.** Every Tattle, badge and recipe: a plaque in the trophy
  room and a gold look for Mario and Luigi in the Wardrobe's looks.
- **Gallery.** On the Start menu (Settings, Modes, Gallery): the disc's enemy
  and boss models turning round, chapter by chapter, named in the game's own
  text. A chapter opens once its story is done and half the Star Pieces of its
  areas are found. Your own pictures stay in the Journal's Photos book.
- **Second-fight Tattles**, a **Boo quiz** of 15 questions in Creepy
  Steeple's hall, **Newsletter Trouble** from the plaza after chapter 7,
  **crane game prizes** in the X-Naut Fortress after the story (a new batch
  each visit), and a Pianta who **sells Pianta tokens** (50 for 150 coins, or
  250 and 500 at once).
- **From the Japanese release** (Gameplay tab): the Thunder Rage and Thunder
  Bolt recipe for a Trial Stew, the bad ending in which Mario stays at the
  Shadow Queen's side, and Hooktail's fear of frogs (Attack FX R's croak).
  Zess T.'s own new dishes gain a Hottest Dog (a Hot Dog and a Fire Flower).
- **Audio tab:** talk blips (a voice for each kind of speaker, from the
  game's own message sounds) and the usual battle music chosen area by area
  from the disc's battle tracks.

Five more achievements come with them: Bump in the Road, Talon Tamer, Lights
Out, Ghost Scholar and Extra! Extra!, plus the Gold Medal's own.
