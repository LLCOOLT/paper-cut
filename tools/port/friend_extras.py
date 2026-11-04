"""What Luigi's friends' models get besides animations (docs/LUIGI_ANIMS.md,
"Soot-black Blooey"): parts added to a disc model and textures made at startup
from the disc's, so nothing of the disc's is in the repository.

c_lp_gesso (Blooey) in chapter 6, as the plan has him, scorched black:
  - the soot drawing: the disc's Blooey (texture 0) darkened to soot with
    glowing ember spots, made by the port at startup (luigi_pack.SOOT), as his
    plane's texture bind 2 (bind 1 is the disc's own brown burnt one)
  - a smoke wisp: a card of our own drawing above his head, `pSmoke` (his
    plane's child, so it rides his every move), hidden
    unless a pose shows it (the SOOT_* poses do, curling up)

EXTRAS[model] = (records(base) -> (records, patches), texture ops(base TPL))
"""
import luigi_pack as LP
import paper_model as PM

# ember spots on the soot drawing (128x144 texels): (x, y, radius)
SOOT_SPOTS = [(42, 30, 7), (86, 44, 5), (60, 92, 6), (28, 116, 4), (98, 122, 5), (74, 18, 4)]
SMOKE_RECT = (-3.0, 15.0, 3.0, 25.0)  # 48x80 texels above his head
SMOKE_PIVOT = (0.0, 15.0)


def draw_smoke():
    """a wisp of grey smoke curling up: soft puffs, smaller as it rises, a pale outline"""
    c = PM.Canvas(48, 80)
    puffs = [(24, 70, 9.0), (20, 58, 8.0), (27, 47, 7.0), (21, 36, 6.0), (26, 26, 5.0), (22, 17, 4.0), (25, 9, 3.0)]
    f = PM.sd_union(*[PM.sd_circle(x, y, r) for x, y, r in puffs])
    c.shape(f, "#6e6a6c", outline="#3a3638", width=2.0, light="#9a9698", light_off=(-2.0, -2.0))
    return c.rows()


# c_lp_gesso- has two textures (Blooey, burnt Blooey); the made ones follow
TEX_SOOT, TEX_SMOKE = 2, 3


def _gesso_records(base):
    soot = LP.bind_records(TEX_SOOT)  # the plane's bind 2 (its binds 0 and 1 are the table's last)
    smoke, patches = LP.plane_records(base, "pSmoke", "pPlane1", SMOKE_RECT, 0.2, SMOKE_PIVOT, TEX_SMOKE, pre=soot)
    return LP.add_records(soot, smoke), patches


def _gesso_textures(base_tpl):
    return [(LP.SOOT, 0, SOOT_SPOTS), (LP.OWN, draw_smoke())]


EXTRAS = {"c_lp_gesso": (_gesso_records, _gesso_textures)}
