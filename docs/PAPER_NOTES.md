# Paper notes

Notes left on the ground for other players: a set phrase and a word ("Hidden
a Star Piece?", "Be wary of a trap"), never free text. Others read them where
they lie and rate them up or down; the best of each room are kept. They are
kept on the online relay ([ONLINE_RELAY.md](ONLINE_RELAY.md)), so they need it, and they are
shared by every release and language: a note is two numbers and a place, and
each game shows it in its own language.

## In the game

- **Seeing them**: a room's notes lie on the ground as small written sheets,
  bobbing gently with a glint now and then (gold once rated +3 or more, blue
  for your own); nothing floats over them (a "!" over Mario is the game's
  "press A here"). Stand by one and its text shows over it in a speech
  balloon, with its rating and the button that opens it ("LB: Read"); a
  note right behind whoever you play (Bowser is broad) slides out to their
  side. A room shows at most its 8 best.
- **Opening one**: at a note, press the notes' button: **LB** on an Xbox
  controller, **L1** on a PlayStation one, **L** on a Switch one, the **L**
  trigger on a GameCube controller (the game's L, which Mario doesn't use in
  the field, works on any controller), **C** on the keyboard; on a touch
  screen tap the note (the mouse can click it too). The balloon shows the
  button of the controller in hand. It is the binding **Read a paper note**
  (Controls > Bindings). The balloon then offers **Rate up**, **Rate down**
  and **Close** (Close alone for one you rated; **Take back** and Close for
  your own): A or a tap chooses, B or the notes' button again closes. **A is never a note's**: by a
  note it jumps, talks, reads a sign or opens a door as anywhere else.
- **Leaving one**: the Start menu's Multiplayer tab → **Paper notes...**. Pick a **Phrase**
  and a **Word** with left and right (the note reads out at the top), then
  **Leave this note here**: it is left where Mario stands. Three of yours per
  room at most, ten a day in an area.
- **The phrases and words**: 18 phrases, from "Try ..." and "Be wary of ..."
  to "Check behind ..." and "Hooray for ..."; three take no word ("Thank
  you!", "I did it!", "Don't give up!"), and with one of those the **Word**
  row is greyed and passed over. 72 words: things to find and use (a Star
  Piece, a Save Block, the Super Hammer, the Ultra Boots, a Superguard, an
  action command...), places and moments (a dead end, the way up, this room,
  the music...), the partners, Mario, Luigi, Peach and Bowser, and the bosses
  (Hooktail, Doopliss, the Shadow Sirens, Magnus von Grapple, the Shadow
  Queen...). The partners and most bosses are named as the game's own text
  names them, in its language and release (Lohgard on a German disc,
  ゴンババ on the Japanese one); the rest are Paper Cut's text, translated
  with the game's names. With a Paper Mario 64 ROM linked
  ([ROM_LINK.md](ROM_LINK.md)) the words also offer its partners
  (Goombario to Lakilester); a note naming one shows his name to anyone.
- **Rating**: the same page lists the room's notes, nearest first; right
  rates one up, left down, once each (not your own). A note rated down to -3
  is gone; a room keeps 30, the lowest-rated older ones going first.
- **Taking one back**: your own note's balloon offers **Take back**, and on
  the page choosing one of yours twice (A, or a tap: the first asks, the
  second takes it) does the same. It is gone for everyone, frees its place in
  your three for the room, and `notes.txt` forgets it. A note is its
  device's, not its network's (see **This device's code** below): it is
  taken back from any network, and another device on the same one (a
  household, a school, a phone carrier's shared address) can't take it.
  Another device's note is answered "Only the device that left it can take
  it back." A note left before 0.5.1 belongs to the address it was left from,
  as it did then: it is taken back from there, and from another network the
  answer is "It can only be taken back from where it was left." (it stays
  yours, for when you're back). A relay from before 0.5.1 answers "This
  relay can't take notes back yet."
- **Refresh** asks the relay again; otherwise an area's notes (all the maps
  that start with the same three letters, `gor_` Rogueport, `tik_` its
  underground...) are fetched when you enter it and kept for 30 minutes, the
  last 8 areas' together, so walking back and forth asks nothing again.

The setting is Multiplayer → **Paper notes** (on by default). Off, nothing is
fetched or shown.

- **Online co-op**: the notes show, each game drawing the room's notes for
  itself. They are only drawn (their text's random numbers put back), never
  read or touched by the game, so the players' games stay the same whether
  or not one of them shows notes. Leaving and rating wait until you're out
  of the room: the Multiplayer tab shows **Paper notes...** off a session
  only, so a room's Start menus are the same in every game, and no note opens
  (the balloon shows no button).
- **Replays**: off while one records or plays, so a replay's playback shows
  exactly what its recording did.

What this device left and rated is kept in `<user>/notes.txt`, so its own
notes show as yours and a rating isn't asked twice; a note taken back (or
found gone when taken) leaves it.

**This device's code**: the first time a note is left or taken back, the
game makes a random code (128 bits from the system's random generator) and
keeps it in `<user>/notes_code.txt` (a portable copy's user folder too). It
goes to the relay with each note left and each take, inside the request
(never in an address); the relay keeps only a salted hash of it as the note's
author, never the code. The code is never written to the log, and Report a
problem takes it out of everything in its zip. Moving the user folder to
another computer (both files) keeps your notes yours; deleting
`notes_code.txt` makes a new code, and the notes left with the old one can
no longer be taken back (they stay for others until rated away or crowded
out).

## Tests

`TTYD_NOTES_TEST="<dx> <dy> <dz> <phrase> <word> <score>[ m|o];..."` puts notes
in the first room the field shows, round Mario's place there, with no relay
(none: another device's; `m`: this device's own, by its code; `o`: in this
device's `notes.txt` but left before codes from another address; `a`: left
before codes from this address; `s`: in `notes.txt` but left by another
device on this address). Leaving, rating and taking back are then answered
by the game itself with the relay's rules (three of a device's a room, six of
an address's, one rating each, gone at -3, taken back by the device that
left it, or from its address before codes), logged as `[notes] test: ...`;
Refresh logs the room's count (`[notes] test: refresh: <N> notes in <area>,
<M> yours`). `TTYD_NOTES_TEST_OLD_RELAY=1` answers a take as a relay from
before 0.5.1; `TTYD_NOTES_TEST_MOVE=1` changes the address after each note
left (`[notes] test: the address changes ...`). `TTYD_NOTES_PAGE=1` opens the Paper
notes page as the Start menu's Multiplayer tab is chosen. The scenarios
`paper_notes_world`, `paper_notes_read`, `paper_notes_page`,
`paper_notes_new_words` and the `paper_notes_take_*` ones (`tests/port/testpass/`) use them. With
`TTYD_TEXT_FIT` the page, as it opens, measures every phrase with every
word as the balloon draws it, and the fit log names any that has to shrink
much (`paper_notes_fit_<language>`, one per language).

## On the relay

`port/relay/src/index.js`, the `Notes` Durable Object, one per area:

    GET  /notes?area=<AAA>   notes <N>
                             <ID> <MAP> <X> <Y> <Z> <PHRASE> <WORD> <SCORE>   (each map's best 8)
    POST /notes?area=<AAA>   add <MAP> <X> <Y> <Z> <PHRASE> <WORD>   -> ok <ID>
                             rate <MAP> <ID> <1|-1>                   -> ok <SCORE>
                             take <MAP> <ID>                          -> ok taken   (0.5.1)

A refusal is a status other than 200 with `<why>: <words>` (`mine`, `today`,
`own`, `rated`, `gone`, `bad`).

From 0.5.1 the game adds this device's code (32 to 64 lowercase hex digits)
as a last word to `add` and `take`:

    add <MAP> <X> <Y> <Z> <PHRASE> <WORD> <CODE>   -> ok <ID>
    take <MAP> <ID> <CODE>                         -> ok taken

Such a note's author is the code's hash (with `HASH_SALT`), the hash of the
address it came from kept beside it. A take by another code, or a take with no
code, is `own` (403, "only the device that left a note ..."). The limits stay
on the address, since a code can be made anew: ten notes a day in an area,
one rating each, no rating of a note left from your own address, and at most
six notes a room from one address whatever its codes; the three a room are
counted by code. A note from before 0.5.1 (or from an older game) has its
address as author: a take from that address, with or without a code, takes
it, from any other is `own` (403, "only who left a note ..."). A relay from
before 0.5.1 answers a coded `add` or `take` `bad: add or rate` (400): the
game then leaves the note without its code (the address's, as before), and
reads a take's as "can't take notes back yet". The relay still answers 0.4.x
and 0.5.0 games exactly as before (`port/relay/test/run_compat.sh`, which also
checks taking back and the codes). The relay keeps no address and no code:
only a hash of each (with the optional `HASH_SALT` secret).
The phrase and word numbers are `port/settings/features/notes.cpp`'s lists,
which only ever grow at the end; a game that doesn't know a number skips the
note. `tools/port/notes_lists.txt` is the lists as released, and
`testpass.py lint` fails unless notes.cpp's lists start with it (nothing
reordered, renamed or removed). The relay takes any phrase up to 255 and any
word up to 1023, so new entries need no change there; a phrase with no word
is left with word 0.

On the Workers Free plan: a fetch is one request per area entered (every 30
minutes at most), leaving or rating one each; nothing is polled.
