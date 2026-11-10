# Brothers mode

Multiplayer → **Brothers mode** (setting `brothers_mode`, off by default; also the first row of the game's
Multiplayer page, at the title and in the Start menu; its
Multiplayer settings... row opens the rest of Settings → Multiplayer there):
Luigi joins Mario as a second hero, not a skin. Player 2 plays him with the
second controller (Start on another controller joins, as for co-op); with nobody on
the second controller he follows Mario by himself, and player 1 plays his turns
in battle. Saves are not changed: nothing about Luigi goes in a save file.

While Brothers mode is on, Settings > Enhancements > Characters → **Play as** is off, whoever is picked (both brothers are themselves),
and the second controller plays Luigi instead of the partner (co-op's partner
controls are player 1's then).

## In the field

- The stick walks Luigi, **A** jumps (a little higher than Mario; a block he
  hits from below is hit as Mario's head would hit it), **B** swings his hammer
  (blocks and switches in front of him, as Mario's hammer works them; an enemy
  it comes down on is a first strike, as with Mario's hammer).
- **A** in front of someone Mario could talk to starts the talk, as Mario's A
  would, while Mario is near (the scene is Mario's: they turn to him).
- Left alone for a few seconds, he follows Mario's steps again, hopping where
  Mario jumped. If he falls, gets stuck or is left far behind, he is back at
  Mario's side.
- He waits out of sight while Mario is in a pipe, in a paper form (plane, boat,
  tube, sideways), riding Yoshi, hiding in Vivian's shadow or 8-bit, and comes
  back beside Mario afterwards; after every door, pipe or room change he is
  beside Mario.
- The camera keeps both brothers in view: with Multiplayer → Camera on Fit
  everyone it frames them (and players 3 and 4) as co-op does
  ([PLAYERS.md](PLAYERS.md)), and Luigi walked to the picture's edge is held
  there; on Follow Mario it moves toward the point between them (at most
  half-way to Luigi).
- "2P" shows over Luigi while a second controller is there.
- His HP and FP show under the game's HUD, with his face, sliding in and out
  with it.

Luigi uses the playable Luigi's model ([PLAYABLE_LUIGI.md](PLAYABLE_LUIGI.md):
his own animations and his hammer, made from your disc at start); without it,
the disc's Luigi model (stand, walk, run, jump: no hammer).

## In battle

- Luigi stands beside Mario, the partner a step further back.
- He has his own turn in the player phase, with Mario's command menu: Jump,
  Hammer, items, Star Powers, Tactics. The brothers share the items, the badges,
  the boots and hammer and the Star Power.
- He has his own HP and FP, as high as Mario's. They are kept from battle to
  battle; in the field, whatever heals Mario heals him too, and he is full
  whenever Mario is (a Heart Block, the inn, a level-up). A brother who is down
  after a won battle gets up with 1 HP. Restarting the game (or loading a file)
  has him full again.
- His guards and Superguards are player 2's.
- A brother who is down stays down for the rest of the battle (a Life Shroom
  gets either one up, as it does Mario) and skips his turns; the battle is lost
  when both brothers are down.
- While Luigi acts, the battle's "Mario" is Luigi: his moves, their FP and
  Star Powers are his.
- His HP and FP show under the game's, with his face.

## Bros. moves

With Duo moves on (Multiplayer → **Duo moves**, [DUO_MOVES.md](DUO_MOVES.md)), Bros. Bounce,
Bros. Hammer and Bros. Drop are in either brother's Jump or Hammer list, and
the other brother's move this turn goes with it. With player 2 there each
brother's half is his own player's (both on A); without, player 1 plays both
(A Mario, B Luigi). The duo moves with the partner are there too, the
partner's half on the second controller.

## With three or four players

Players 3 and 4 ([PLAYERS.md](PLAYERS.md)) play alongside the brothers: player 3 the
follower or a companion, player 4 a companion. In battle the partner's turns
are player 1's, or player 3's or 4's when it's their companion's partner;
Luigi's are always player 2's.

## Online

Playing together online, the guest in seat 2 is Luigi: their controller walks
him in the host's game, which every game shows, and they play his turns and
guards in shared battles ([STATE_SYNC.md](STATE_SYNC.md#playing-together)).
