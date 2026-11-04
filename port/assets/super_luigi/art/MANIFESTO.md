# The art manifesto: drawing as Paper Mario: The Thousand-Year Door draws

This is how to make a new card that belongs in the game. It comes from measuring every character
texture on the US disc (6,263 images in 308 models, 4,469 large enough to measure) and from studying
the cards zoomed in. It holds numbers and words only. Nothing from the disc is in the repository.

**What it can and can't do.** The numbers catch art that is wrong. They can't prove art is right: our
old drawn art passes most of them once it's been through the game's compression, and it still looked
wrong. Sections 3 to 7 are what the numbers can't reach, and the eye decides those. See section 9.

**Where the numbers came from.** Bounds are the 2.5th to 97.5th percentile of the 234 training models.
A quarter of the models were held out by the CRC of their names. Exception: `c_kuribo_n`, a held-out
model, was also looked at while writing the words.

## 1. The card

| Rule | Value | Measured |
|---|---|---|
| Sides | Multiples of 8 | 99.97% of images |
| Format | CMPR | 6,240 of 6,263; 20 RGB5A3, 3 IA8 |
| Alpha | 1-bit | Median partial alpha 0.00; cloth may have a little |
| Grain | At most 4 colours in any 4×4 block | All training models: this is CMPR's signature |
| Typical size | Median 56×56; large parts 112×104 | 90% of large parts are 72 to 224 wide |
| Fill of the rectangle | Large parts 35% to 69% | Cards are cropped close |
| Texel density | 8 texels a unit; giant bosses 4 | From `c_luigi` and `c_gonbaba` |

Always finish through a CMPR round trip. The grain is part of the look, and no drawn noise replaces it.

## 2. The line

| Rule | Value |
|---|---|
| Width | Median run 4 texels; a character's median falls in 3 to 6 |
| By kind | Soft, small: 3 (Peach, Kammy). Sturdy: 4 (Luigi, Hooktail). Heavy: 5 to 6 (Crump, Bulkhead) |
| Sides | Even: the median of (R+B) minus (L+T) is 0. A character falls in ±3 |
| Value | Dark: median 0.19, most from 0.13 to 0.22, up to 0.46 |
| Colour | One per character: median spread of 5 in RGB across its large cards |
| Hue | Not pure black, usually. Warm sepia, wine and brown are most common (the 0 to 60° hues hold 534 of 1,102) |
| Exceptions | Near-black for villains and metal: Crump `#101418`, Bristle `#080829`, Shadow Queen `#100C10` |

The line is inked by hand, and this is what the numbers miss:
- **Its width swells and thins** on a curve. A card's line varies by about 66% of its mean width around its contour.
- **It's thickest where forms meet,** under a chin, at a boot's sole, in a join.
- **Corners are soft.** Even Bristle's spikes and the wrench's jaws have rounded tips.
- **It never reads as a stroke applied by software.** An even, mechanical width is the first thing that looks wrong.

Interior lines are few (median 2% of the inside texels) and they mean something: a skirt's fold, a
brim, a mouth. They start at the outline or another line and taper to nothing: see Peach's skirt folds.
No seams, grooves or decorative stripes.

## 3. Shape

- **Round first.** Shapes are soft, bean-like and a little lopsided: Goomba's head, Bulkhead's foot, Toad's cap. A large part's outline is close to a blob (compactness median 1.13, where a circle is 1.0).
- **No ruled lines.** Even the Shadow Queen's robe, the straightest cloth in the game, curves along its long side, and it's still one of the few cards with a truly straight edge.
- **Asymmetry.** Nothing is mirrored exactly within a card, and a pair of cards differs.
- **Big, simple masses.** Read the silhouette at thumbnail size. Detail lives in a few places, usually the face.
- **Parts are separate cards,** overlapping at the joins, so that they read as paper when they turn.

## 4. Colour

- **Few colours do the work.** A large part's inside needs a median of 9 five-bit colours for 90% of its area; 90% of parts need 28 or fewer. A character's median stays under 50.
- **Saturated or neutral, little between.** Median saturation of large parts is 0.49, but the spread is wide (0.10 to 0.77 between quartiles): whites and greys sit beside strong hues.
- **Bright but not blown.** The median value of the inside is 0.84, and the 95th percentile is 0.91. White is usually a warm or cool off-white.
- **A palette per character,** not per game: pick it from the character's source, or by mapping tone bands so the values stay.

## 5. Shading

Earlier notes said "one soft ramp, lit from the top left". The disc doesn't support that as a rule.
Only 45% of large parts fall toward the lower right, with a median magnitude of 45 luminance per
100 texels. There are three real manners, and a character uses one or two:

1. **Flat with one hard second tone:** Peach's skirt, Hooktail's body and belly, Toad's spots. 2 tones, no blur.
2. **Airbrushed:** a soft gradient, darkening toward a rim: Goomba's head, Kammy's robe, Crump's belly, Bulkhead's body. A broad ramp, never banded.
3. **Glossy volume:** airbrush plus one hard-edged sheen or spot: Toad's cap rim, Bulkhead's dot, Papa Bob-omb's ball, Koops-like shells. Matte things get no sheen.

Choose by material: cloth and skin flat or airbrushed; metal, shells and bombs glossy. Don't mix manners within one material.
Detail stays low: the median high-frequency value on large parts is 12, with a character's median under 32.5.

## 6. Faces

Eyes, mouths and noses are whole cards, swapped for expressions. They carry most of a character's charm.
**Use the game's face cards, recoloured, wherever a face is a normal one.** Draw a new face only if the
character can't have a normal one, and then match the nearest real one's construction: big simple eye
shapes, a heavy line, a single highlight.

## 7. Character

These can't be measured, and they matter most.

- **It should read as a Paper Mario character in silhouette alone, and as a new one.** Recolouring an existing character gives the first without the second.
- **It's a paper cutout that knows it's paper:** a clean cut edge, no fake 3D lighting across the whole figure, no photo texture.
- **Designs are slightly silly,** with big heads and hands, and one big idea each. Avoid fussy costume detail.
- **Two or three parts at most carry the design:** Crump's cape and belly, Kammy's hat and wand.

## 8. Rig and motion

Measured from 307 models' animations. Times are frames at 60 a second.

| Pose | Median frames | Median keys | Quartile range of frames |
|---|---|---|---|
| Stand `S_1` | 60 | 10 | 40 to 130 |
| Walk `W_1` | 40 | 10 | 38 to 40 |
| Run `R_1` | 20 | 10 | 20 |
| Talk `T_1` | 14 | 5 | 12 to 14 |
| Attack `A_1` | 12 | 5 | 6 to 20 |
| Hurt `D_1` | 10 | 3 | 8 to 12 |
| Guard `G_1` | 10 | 4 | 8 to 12 |

- A model has a median of 19 groups (6 to 35) and 12 animations (4 to 37). Bosses go far past this.
- Median 12.7 keys a second, with 10% to 90% between 7.4 and 21.9.
- **Walk at 40 and run at 20 are close to fixed.** Keep to them: they're part of how the whole cast moves.
- **Attacks and hurts are snappy.** Few keys, few frames.
- **Talks are fast loops,** about 14 frames.
- **Don't squash and stretch the body.** The game keeps it rare (blinks, an odd impact). This was measured on a sample of characters in `docs/super_luigi/ART_PLAN.md`, not across the whole disc.

## 9. How a card is accepted

Three separate checks, and all three must pass.

1. **The numbers:** `art/tools/check_manifesto.py`. A character must meet the hard rules (alpha, CMPR grain) and may miss one soft rule. On the 73 held-out game models, 71 pass. It's only a floor: passing it says little.
2. **Beside the real thing:** in the game, in its real scene and light, next to the real character closest to it, at the same scale. The test: would someone who knows the game pick out the new one because it looks wrong? Check by eye against sections 2 to 7, item by item.
3. **The user's verdict.** They decide if it's the game's art. A piece isn't accepted until they've seen it.
