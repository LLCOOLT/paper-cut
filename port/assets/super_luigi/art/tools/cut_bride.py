"""Luigi the bride's veil and gown cut from the model's own pieces (`python cutout.py cut_bride`).

The design: the style model's bridal pieces laid out flat (designs/bride/design_bride_3.png: a lace bodice with its
long lace sleeve, a lace-trimmed gown, a garland of pink roses, a little gown). Here: the long lace strip is the veil (turned to fall
behind him, +x, hanging, and turned back as blown), the top of the rose garland is the band before his cap, the little gown's skirt is
the skirt pinned at his waist (standing, and swung back). The same pieces stay as the model drew them; each is sized to
him by its own `scale`. sl_veil.py hangs them on c_luigi_pc. Points are in the sheet's pixels (1024 square).
"""
SRC = "designs/bride/design_bride_3.png"
OUT = "veil/qwen"
MODEL = "c_luigi_bride"
PX = 4.0  # 32 sheet pixels a unit, before each card's scale
ORIGIN = (512, 1000)
LINE = "#2a2026"
SPECKS = 0.05  # bits of the neighbouring pieces at a cut

# the sleeve's own edge on the bodice side (its line kept, the bodice's white beyond it left on the sheet)
VEIL = [(100, 140), (194, 140), (193, 262), (180, 300), (172, 340), (168, 400), (160, 440), (157, 480), (155, 600),
        (153, 645), (110, 645), (35, 540), (60, 400), (100, 250)]
VEIL_TOP = (120, 160)
BAND = [(470, 105), (995, 105), (995, 300), (880, 300), (800, 345), (650, 360), (560, 330), (470, 300)]
SKIRT = [(625, 560), (800, 560), (890, 880), (885, 915), (530, 915), (540, 860)]
WAIST = (712, 575)

CARDS = {
    "veil_side": dict(poly=VEIL, pivot=VEIL_TOP, scale=1.2, flip=True, desc="the lace veil, hanging from his cap"),
    "veil_side_flutter": dict(poly=VEIL, pivot=VEIL_TOP, scale=1.2, rotate=-24.0, flip=True, desc="the veil blown back"),
    "veil_back": dict(poly=VEIL, pivot=VEIL_TOP, scale=1.2, desc="the veil seen from behind"),
    "veil_band": dict(poly=BAND, pivot=(730, 330), scale=0.62, desc="the band of pink roses before his cap"),
    "gown_side": dict(poly=SKIRT, pivot=WAIST, scale=0.88, desc="the skirt pinned at his waist, the lace at its hem"),
    "gown_side_swish": dict(poly=SKIRT, pivot=WAIST, scale=0.88, rotate=-8.0, desc="the skirt swung back"),
}
BINDS = [["veil_side", "veil_side_flutter"], ["gown_side", "gown_side_swish"]]
