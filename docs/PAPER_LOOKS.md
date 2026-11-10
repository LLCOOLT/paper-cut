# Paper looks

Options that make the paper look more like paper. All of them change only
what is drawn: the game plays exactly the same, saves and replays are the
same, and online co-op stays in step with them on or off on either side (each
player's are their own). They are on the settings page's **Graphics** tab
(the pop-up launcher and the launcher's look are on **General**), off by
default except the pop-up launcher.

## Sharp text

The game's font is one bitmap sheet of letters about 24 pixels high, so at
high resolutions text is that sheet magnified and soft. With **Sharp text** on,
the sheet is rebuilt when the font loads as a *distance field*: each texel
holds how far it is from the edge of a letter (worked out from the game's own
letters, interpolated four times finer). Each frame the edge is found again at
the size the text is drawn, one screen pixel wide, so letters stay sharp at
any resolution and any text size. The letters are the game's own shapes.

When a texture pack replaces the font sheet, sharp text steps aside for that
font by itself and the pack's letters are drawn as the pack made them; it
looks again whenever the packs change (turned on or off, reloaded, or a
map's own set), and the setting stays as you left it.

## Smooth paper edges

The paper characters and many props are cut out of their textures by an alpha
test: a texel is either drawn or not, which leaves stair-stepped edges, very
visible at 4K. With **Smooth paper edges** on, those cut-out edges get a smooth
edge one pixel wide instead:

- with **Anti-aliasing (4x MSAA)** on, as the share of each pixel's samples
  the cut-out covers (alpha to coverage), for the cut-outs drawn solid;
- and, for cut-outs that are blended (most characters), faded into their own
  transparency, which works without MSAA too.

Other drawing is unchanged.

## Paper shadows

Characters (Mario, his partners, the people and enemies in the field) cast a
soft shadow shaped like their own paper cut-out instead of the round shadow
under them. The cut-out is laid down on the floor where the round shadow was,
in front of their feet, as if lit from above and behind: a paper cut-out faces
the camera, so a shadow falling away from it would hide behind the character
and show only as a sliver at its edge. The lamps the room has around them turn
it to the left or right, each counted by how brightly it lights them
(spotlights only inside their beam), so it swings smoothly as they walk from
one lamp to the next; where no lamp reaches it leans a little to the right.
The shadow is drawn a few times, each a little turned, lengthened or nudged,
so its edge is soft, and it fades and softens toward its far end. In a jump
it stays on the ground under them, a little smaller and fainter the higher
they are. Where the game gives a character no shadow, there is none.

**High** makes the shadow longer, darker and sharper, as a strong lamp's is,
and adds a short dark contact shadow where the feet meet the floor.

## Paper physics

**Paper physics** (Subtle or Lively) makes the cut-outs bend like paper: they
lean back into fast moves and sway upright again when they stop, flutter on
quick turns, and squash and spring back on hard landings. It is a small change
to how each character is drawn; where they are, what they touch and what the
game knows about them stay the same.

## Picture filters

Graphics → **Picture filter** (Off by default) lays a filter over the whole
picture. Besides CRT, Paper grain and Sharpen, two are paper ones:

- **Cut-paper outline:** an ink line and a pale cut edge round each shape, as
  pieces of paper laid on each other.
- **Folding light:** the picture on a folded sheet, with a slow light moving
  across its panels.

Photo mode has the same two among its own filters ([PC_PORT.md](PC_PORT.md#running)).

## Pop-up launcher

General → **Pop-up launcher** (on by default). When the launcher opens, its stage builds like a pop-up book: the curtains
part and gather at their tie-backs, the stage boards fold up from the front
edge, the clouds pop up in two rows, the moon and stars drop on their strings,
the footlights come up, the title unfolds upright and the cards rise onto the
stage, and Mario and his partners spring up along its front edge. Any key,
click, touch or button skips it.

The stage itself is a paper theatre cut from layers of paper, drawn by Paper
Cut (no game art): a painted night sky with cut-out clouds, a paper moon and
stars hanging on strings, stage boards with footlights and a spotlight, pleated
red curtains tied back with gold tasselled ropes, and a valance of swags with
a gold fringe. The curtains, the hanging stars and the tassels sway gently;
with Windows' *Animation effects* turned off (Settings → Accessibility →
Visual effects) the stage keeps still.

Mario's and the partners' faces are the game's own icons. They are made from
your disc the first time the game starts on it (with the rest of "Preparing
graphics") and kept in your user folder, so they appear from the next time the
launcher opens; nothing of the game's is part of Paper Cut itself.

## The launcher's look

The launcher is dressed like the game's own menus. Its pages sit in paper
windows on the stage; the tabs along the top are the Start menu's, in its
colours (Play is Mario's red, Online Party's pink, Settings Gear's orange,
About Journal's blue); a white glove points at whatever is chosen, gliding
from item to item and nudging back and forth as the game's does; the buttons
are torn paper labels; and changing tabs turns the page. Each disc in the list
shows its banner (the picture a GameCube's menu shows for it, read from the
disc) with a tag for its region (JP, US, EU). Soft paper sounds go with the
glove, a choice and a page turn (**Launcher sounds** on **General**). It all
works the same with a mouse, a keyboard (arrows and Enter; Ctrl+Page Up and
Page Down change tabs), a controller (the shoulder buttons change tabs) and
touch.

**Launcher look** on **General** picks how those pieces are drawn:

- **Paper Cut's** (the default): drawn by Paper Cut from shapes and its own
  type, nothing of the game's, so the launcher looks the same before any disc
  is added and after.
- **The game's (trial)**: the same pieces with the game's own art: the Start
  menu's pointing glove, its woven paper window, its torn strip and label.
  They are made from your disc (an art recipe, like the pop-up's cast) as soon
  as it is in the launcher's list, and kept in your user folder. Until a disc
  is added the launcher keeps Paper Cut's look.

## Bloom

Graphics > **Bloom** (Off, Soft, Strong, Dreamy; off by default) adds a glow around the bright parts of the scene: lamps, sparkles, the sky. It is drawn with the GameCube's own means, after the 3D scene and before the HUD, so the HUD and the text stay sharp: the frame so far is copied at the game's own half size (whatever the internal resolution), then added back over itself as a soft disc of taps (the middle and rings around it, close enough together that they blend into one glow, weighed by distance), each keeping only what is brighter than the preset's threshold. The copy is of the whole frame as it is, so it is drawn back in screen space: never squeezed by widescreen or flipped by mirror mode as the cameras' draws are (once, it was, and the glow showed as a see-through copy of every bright thing pulled towards the middle; `bloom_centred_*` scenarios check the glow is centred on what glows). Soft keeps only the brightest parts and a small radius; Dreamy takes in more of the picture and spreads it wider. It changes only the look, each player's own. Code: `port/src/aurora/bloom_aurora.c`.

## Ray-traced lighting

Graphics > **Ray-traced lighting** (Off, On, High; off by default; Windows and Linux desktops) traces light through the 3D scene as it is on screen, after the scene and before the HUD. Each pixel sends rays over its surface's hemisphere and marches them against the scene's depth: where they run into something close, the surface is shaded, so corners, folds and the ground under the paper characters darken softly. One more ray goes toward the room's main light (the lamps the paper shadows use), giving contact shadows. High sends more rays and adds one bounce of light: the colour a ray hits tints the surface it left, so a red wall warms the floor beside it. Graphics cards have no ray tracing through WebGPU, so the rays are traced on screen: what is off screen or hidden behind something casts nothing. Where the device can't do it the option isn't shown. It changes only the look. Code: `port/settings/features/rt_lighting.cpp`, `port/src/aurora/rt_lighting_aurora.c`.
