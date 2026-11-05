# The action-command dojo and timing assist

Practise any of Mario's or the partners' action commands, guards, Superguards and Stylish moves, and see each press told in frames. Timing assist then widens only the kinds of command you struggle with.

## The dojo (Modes tab)

**Action-command dojo** in the Modes tab starts a practice battle on the Glitz Pit's ring, run by the game's own battle code. Start it in the field with a file loaded. It's offline only: not during online play or a replay.

| Choice | |
|---|---|
| Dummy | One of the Glitz Pit's own fighters, from the Goombas to the Dark Craw. Its attacks are its own, so pick the one whose attack you want to guard. |
| First partner out | The partner who starts the fight. You can still switch partners in the fight. |
| Every move | Every partner at Ultra Rank, Ultra Boots and Hammer, every Jump and Hammer badge worn, and all Star Powers, lent for the dojo. Without it, Mario is as he is in the file. |
| The dummy attacks | **Every turn**, **When you choose Defend** (Mario's or the partner's Tactics > Defend: the dummy attacks that turn), or **Never**. You can change it while the dojo runs. |

- **In the fight:** Mario's and the partner's HP, Mario's FP and the Star Power fill up at the start of every turn's command. The dummy has 99 HP that come back after every hit, gives no Star Points or coins, and never falls. If it does fall after all (a one-hit KO, a Bob-omb's blast), it comes back in a new fight.
- **Leaving:** Run Away (the dummy always lets you), or **Leave the dojo** in the Modes tab between fights. Mario goes back to the room he came from, and his pouch is put back as it was: HP, FP, items, badges, partners. Nothing done in the dojo stays.
- **Autosave** is off while the dojo runs.

## The window over the battle

After each judgement, a window of the game's own (its font and frame) at the bottom of the screen says how it went:

- the kind of command: the battle menu's Jump, Hammer or Special, a partner's name, or Guard, Superguard, Stylish;
- the verdict: **Perfect!**, **3 frames early**, **2 frames late**, **No press**, **Too many presses**, or, for commands that have no single moment to press (gauges, mashing, aiming), **Success** or **Missed**;
- a bar of frames: the window in colour (an attack's green, a guard's blue with its last Superguard frames in gold), the frames around it in grey, and the press marked in red with its button;
- the frame of the window it was in ("frame 3 of 8"), and how this kind went since the dojo started ("7 of 9", and the average press, "1.5 frames early on average").

While a guard or a timed press is coming that the game's own timing can see ahead (an enemy's jump onto Mario or the partner, Mario's or a partner's jump), and while a Stylish move's check runs, a second bar above shows the window with the frames counting towards it: the **Stylish trainer**.

How each one is judged, the way the game judges it:

| Kind | Window |
|---|---|
| Jumps, Headbonk (timed A) | A within the last N frames before the hit (N by Simplifier and Unsimplifier badges; it narrows with each Power Bounce). Two presses in 20 frames fail. |
| Hammer | Let go of the stick within a few frames after the last, big light comes on. Early is before it lights; late, after the window or held until the end. |
| Guard | A within the last 10-5 frames before the hit; Superguard, B within the last 5-1. Two presses in 15 frames fail. |
| Stylish | A within the move's own window of frames. A Stylish move not tried isn't counted. |

**Action command timing in every battle** (Accessibility tab) shows the same window in every battle, not only the dojo. It only reads and draws, so it's each player's own and changes nothing online.

## Results

The dojo keeps your results by kind of command: Jump, Hammer, Special moves, each partner, Guard, Superguard and Stylish. For each, the tries, how many were in time, and the average press: how many frames early or late the presses were, a press in time counting as 0. They're kept on this device in `dojo.txt` in the user folder, not in the save file. The Modes tab shows them since you last entered the dojo and in all; **Reset the results** clears them. Only presses in the dojo count.

## Timing assist by command type (Accessibility tab)

**Action commands: attacks** and **Action commands: guards** have a fourth choice, **By command type**, beside Off, Wider windows and Automatic. With it, **Timing assist by command type** chooses each kind's assist: Off, A little wider, Wider or Much wider.

- **Attacks** (Jump, Hammer, Special moves, each partner, Stylish): each step is one Simplifier badge's worth, so Much wider is what Wider windows gives every attack. Stylish moves: a frame more on each side of the window a step.
- **Guards:** a third more of the Guard window a step (Much wider: twice as long, as Wider windows); a frame more of the Superguard window a step.
- **Use the dojo's suggestions** sets each kind from your results (at least 5 tries): 85 % in time or better, Off; 65 %, A little wider; 45 %, Wider; below that, Much wider. It also sets attacks and guards to By command type. Kinds with too few tries stay as they are.

Timing assist reads only its settings, never the results directly, so the results stay on your device while the assist is a setting like any other, recorded in replays. Attacks and guards set to By command type go with each save file when settings are kept per file, as the other assist choices do; each kind's level is yours across files. **Online**, the host's settings are what play, timing assist included (each kind's level is sent with the others); the dojo itself is offline only.
