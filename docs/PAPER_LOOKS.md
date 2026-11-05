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
shadow shaped like their own paper cut-out instead of the round shadow under
them. The shadow lies where the round one was (on the floor below, also while
jumping) and falls away from the nearest lamp the room has above them, so it
stretches as they walk under it; in rooms without one, the light comes from
above and to the left of the camera, like a paper theatre's footlights set to
one side, so the shadow lies back and to the right of the character as a
shape of its own rather than hiding behind them. Where the game
gives a character no shadow, there is none.

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

General → **Pop-up launcher** (on by default). When the launcher opens, its stage builds like a pop-up book: the floor rises,
the curtains part, the title unfolds upright and the cards rise onto the
stage, and Mario and his partners spring up along its front edge. Any key,
click, touch or button skips it.

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
