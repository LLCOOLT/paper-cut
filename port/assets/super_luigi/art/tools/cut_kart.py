"""Big Green 01's cards cut from the style model's own kart (`python cutout.py cut_kart`, then `python kart_cards.py`).

The design: the style model's seed 31 (designs/kart/concept_kart_d_seed31.png), redrawn by Qwen-Image-Edit as a side view
facing left with no driver (side_qwen_31_s2.png) and given a light LoRA pass (side_restyled_d40_s2.png). Qwen would not
split a vehicle into laid-out pieces (it handed the kart back whole), so each hidden part was asked for on its own, in
place: the body shell with the wheels, seat, wheel and exhaust taken off and drawn whole where they covered it
(parts_qwen_shell.png), and the seat alone (parts_qwen_seat.png). Every sheet is 1024 square and in register with the
design. (Asked for a wreck, Qwen drew a new kart in a new view; the wrecks are kart_cards.py's, from these cards.)

The cards, back to front (sl_kart.py's CARDS): the far wheels; the engine (the rear cowl over the motor, its pipe and the
orange silencer); the seat; Luigi; the nose (the front fender and the chassis rail his legs lie along); the body (the side
pod and the tail, in front of his hips); the plate; the near wheels; the steering wheel on its column.
The model's (0, 0) is on the ground midway between the axles; 28 sheet pixels a unit (34 units nose to tail).
"""
SRC = "designs/kart/side_restyled_d40_s2.png"
SHELL = "designs/kart/parts_qwen_shell.png"
SEAT = "designs/kart/parts_qwen_seat.png"
OUT = "kart/qwen"
MODEL = "c_lp_kart"
PX = 3.5  # 28 sheet pixels a unit
ORIGIN = (551, 761)
LINE = "#12280f"

FRONT, BACK = (272.5, 652.0), (830.3, 652.8)  # the hubs; the rail hides each tyre's top (kart_cards.py fills it in)


def _ellipse(c, rx, ry, n=48):
    import math
    return [(c[0] + rx * math.cos(2 * math.pi * i / n), c[1] + ry * math.sin(2 * math.pi * i / n)) for i in range(n)]


# the nose: the front fender, its flap and the rail back to the side pod
NOSE = [(30, 560), (90, 505), (180, 400), (300, 318), (318, 318), (342, 390), (300, 425), (220, 495), (196, 522),
        (408, 530), (436, 556), (436, 700), (196, 700), (30, 690)]
# the body: the side pod up to its rim, the green tail over the cowl's back, down to the floor
BODY = [(420, 620), (430, 600), (468, 560), (540, 520), (640, 500), (700, 482), (790, 452), (860, 466), (940, 505),
        (992, 570), (992, 640), (940, 696), (420, 700)]
# the engine: the rear cowl behind the seat, the pipe out of it and the orange silencer
ENGINE = [(405, 600), (412, 540), (440, 470), (500, 430), (600, 408), (700, 412), (770, 430), (790, 395), (850, 380),
          (950, 395), (956, 470), (900, 505), (840, 505), (790, 470), (800, 470), (860, 530), (860, 620)]
SEAT_POLY = [(668, 318), (700, 314), (734, 330), (730, 362), (704, 430), (684, 516), (668, 556), (660, 566), (452, 566),
             (444, 540), (468, 508), (520, 494), (588, 486), (640, 360)]
STEER = [(330, 300), (360, 284), (400, 300), (460, 390), (452, 436), (420, 440), (396, 420), (380, 400), (330, 470),
         (262, 556), (226, 548), (232, 528), (300, 440), (352, 372), (328, 330)]

CARDS = {
    "kart_nose": dict(src=SHELL, poly=NOSE, pivot=(240, 640), desc="the front fender and the rail; in front of Luigi's legs"),
    "kart_body": dict(src=SHELL, poly=BODY, pivot=ORIGIN, desc="the side pod and the tail, in front of Luigi's hips. Pivot = the kart's origin"),
    "kart_engine": dict(poly=ENGINE, pivot=(700, 500), air=0.3, desc="the rear cowl, the pipe and the orange silencer"),
    "kart_seat": dict(src=SEAT, poly=SEAT_POLY, pivot=(590, 560), desc="the seat, behind Luigi"),
    "steering_wheel": dict(poly=STEER, pivot=(244, 548), desc="the steering wheel on its column; pivot at the column's foot"),
    "wheel_near_front": dict(poly=_ellipse(FRONT, 99, 99), pivot=FRONT, air=0.25, desc="the near front wheel; rz rolls it"),
    "wheel_near_back": dict(poly=_ellipse(BACK, 108, 108), pivot=BACK, air=0.25, desc="the near back wheel; rz rolls it"),
}
ANCHORS = {"hips": (560, 505), "pipe": (952, 432), "front_axle": FRONT, "back_axle": BACK}
