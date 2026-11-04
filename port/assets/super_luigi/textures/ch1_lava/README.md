# Chapter 1's lava textures

Rumblebump Volcano is Keelhaul Key (`muj_*`) turned volcanic. These replace
Keelhaul Key's terrain textures, named as Dolphin names them (a texture pack,
docs/MODDING.md), so they apply wherever those textures are drawn: the story
code turns the set on only for the chapter's maps.

Our own drawings, made by `tools/port/lava_textures.py` (run it to remake
them; the list of names and what each becomes is at its top):

| Becomes | Replaces (muj_*) |
|---|---|
| volcanic rock: dark basalt slabs, lit from the top left, glowing cracks | the cliff and wall rock, the earth cliff, banded earth |
| cinders: loose cinders, embers | the gravel |
| ash: burnt ground, cooled crust, glowing seams, sparks | the jungle floor, mossy ground, sand |
| lava: hot cells under a dark crust, bubbles | the shallow water, the sea's layers, wave crests |
| sky: smoky red over a lava sea | the sea and sky backdrop |

Checked in game on muj_00, muj_02 and muj_05 with the set in the user
folder's `textures/`: the rock, cliffs and ground read as volcanic. The sea
(muj_00) doesn't turn orange: its material tints its layers blue-green and
draws a ring layer of its own, so it reads as a dark crust with teal rings.
Making it molten needs the map's water colour changed (the story code), not
only textures. The plants keep their green (cut-outs, not replaced).
