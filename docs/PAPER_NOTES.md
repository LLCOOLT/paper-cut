# Paper notes

Notes left on the ground for other players: a set phrase and a word ("Hidden
a Star Piece?", "Be wary of a trap"), never free text. Others read them where
they lie and rate them up or down; the best of each room are kept. They are
kept on the online relay ([ONLINE_RELAY.md](ONLINE_RELAY.md)), so they need it, and they are
shared by every release and language: a note is two numbers and a place, and
each game shows it in its own language.

## In the game

- **Seeing them**: a room's notes lie on the ground as small folded sheets
  (gold-edged once rated +3 or more, blue for your own). Stand by one and its
  text shows over it, with its rating. A room shows at most its 8 best.
- **Leaving one**: the Start menu's Online tab → **Paper notes...**. Pick a **Phrase**
  and a **Word** with left and right (the note reads out at the top), then
  **Leave this note here**: it is left where Mario stands. Three of yours per
  room at most, ten a day in an area.
- **Rating**: the same page lists the room's notes, nearest first; right
  rates one up, left down, once each (not your own). A note rated down to -3
  is gone; a room keeps 30, the lowest-rated older ones going first.
- **Refresh** asks the relay again; otherwise an area's notes (all the maps
  that start with the same three letters, `gor_` Rogueport, `tik_` its
  underground...) are fetched when you enter it and kept for 10 minutes.

The setting is Multiplayer → **Paper notes** (on by default). Off, nothing is
fetched or shown. The notes are also off during online co-op and while a
replay records or plays: they are only drawn and never touch the game, but
this way a session's games, and a replay and its playback, show exactly the
same.

What this device left and rated is kept in `<user>/notes.txt`, so its own
notes show as yours and a rating isn't asked twice.

## On the relay

`port/relay/src/index.js`, the `Notes` Durable Object, one per area:

    GET  /notes?area=<AAA>   notes <N>
                             <ID> <MAP> <X> <Y> <Z> <PHRASE> <WORD> <SCORE>   (each map's best 8)
    POST /notes?area=<AAA>   add <MAP> <X> <Y> <Z> <PHRASE> <WORD>   -> ok <ID>
                             rate <MAP> <ID> <1|-1>                   -> ok <SCORE>

A refusal is a status other than 200 with `<why>: <words>` (`mine`, `today`,
`own`, `rated`, `gone`, `bad`). The relay keeps no address: only a hash of it
(with the optional `HASH_SALT` secret), for the limits and one vote each.
The phrase and word numbers are `port/settings/features/notes.cpp`'s lists,
which only ever grow at the end; a game that doesn't know a number skips the
note.

On the Workers Free plan: a fetch is one request per area entered (every 10
minutes at most), leaving or rating one each; nothing is polled.
