# Battle extras

Paper Cut adds a few things to the game's battles, all on the settings page (F1,
or the Start menu's Settings tab). Each is off until you turn
it on (or start it), so the game plays as it was made unless you ask.

## Battle grades

Enhancements → **Battle grades**. Each battle you win is graded D, C,
B, A or S, shown for a moment at the top of the picture. The grade starts at
70 points and goes:

- up 5 for each Stylish move (up to 25) and 10 for each Superguard (up to 30);
- down 3 for each point of damage Mario and the partner took (up to 60), and
  up 10 if they took none;
- down 10 for each turn past the battle's par: one turn, and one more for
  every two enemies.

100 or more is an S, 85 an A, 70 a B, 50 a C. An S pays 2 coins more plus one
per enemy, an A one more. The count of each grade is kept in
`battle_grades.txt` in the user folder.

## The Paper Cut badge set

Enhancements → **Paper Cut badge set**. Twelve new badges, each folded
from two or three of the game's own: Glass Cannon (Power Plus twice and Double
Pain), Paper Armor, Second Wind, Charmed Life, Leech Fold, Showstopper, Rush
Hour, Charge Kit, Stomp Kit, Hammer Kit, Treasure Nose and Paper Shield P. A
badge does exactly what the badges it's made of do, for their BP together,
one less for each badge past the first.

Once the prologue is over, a Toad merchant by Rogueport's plaza sells them.

The GameCube game can't read Paper Cut's badges or dishes. To take a file
holding one to Dolphin or a GameCube, use Saves > **A copy for Dolphin or a
GameCube** (or the "for Dolphin" link of Send saves to another device): the
copy has them sold, and your own card keeps them (docs/SAVE_FILES.md).

## Zess T.'s new dishes

Enhancements → **Zess T.'s new dishes**. Sixteen new dishes, cooked
from pairs of ingredients that made a Mistake before. They're there to be
found by trying; they raise attack or defence for a few turns, restore HP and
FP over several turns, make you hard to hit or invisible, let you move twice,
charge your next attack or hit back at enemies. They aren't in the Recipe Log.

## Badge maker

Enhancements → **Badge maker**. Design up to eight badges of your
own: up to four of the game's badges in one, a name and the badge it looks
like. Its BP cost is set the same way as the badge set's, and the merchant
sells it for 20 coins and 15 more per BP. Share one as a code
(`PCBADGE1:...`): Copy the code, and Add from the code on the other side.

Online, the host's badge maker is the one that counts; the badges there are
called Custom Badge 1 to 8.

## Battle puzzles

Modes → **Battle puzzles** (with a file loaded), or the Start menu's Settings
tab (Modes → Battle puzzles: today's, or any one picked with left and right).
Online they start from the Start menu only, which every game of the session
runs together (today's by the host's day). Fifty
battles from the game, each with a fixed set-up (Mario's level, HP, FP,
partner, badges and items) and a goal, like a chess puzzle: "win in 1 turn",
sometimes without items, without being hurt, without jumping or without the
hammer. They grow harder from Goombas to the Shadow Queen. Mario comes back
as he was, where he was, whether the puzzle was solved or not; losing one
isn't a Game Over.

Today's puzzle is one of the harder half, picked by the date (the same for
everyone). Puzzles solved and the fewest turns are kept in `puzzles.txt`.

## Enemy maker

Challenges → **Enemy maker**. Make up to eight versions of the
game's enemies: more or less HP, attack and defence, a size and a colour, a
move borrowed from another enemy's attack (burn, freeze, poison, electrify,
sleep, dizzy, confuse, shrink, soften, immobilize or drain), extra coins and
your own name and Tattle. Share them as codes (`PCFOE1:...`).

With Challenges → **Use my enemies** on, every enemy of a kind you changed is your version,
in the story, challenges and battle puzzles. Online the host's list counts:
it goes with the host's setup, and every game of the session (the host's too)
plays from it for the rest of the run, whatever either side edits meanwhile; a
guest's own `enemies.txt` isn't touched.

For modders and modes: the code format is described in
`port/include/port/enemy_variant.h`, and a mode can arm variants for a battle
from its own data.
