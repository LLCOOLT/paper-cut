#!/usr/bin/env python3
"""Paper Mario 64 swaps (port/src/common/pm64_enemy_pc.c's kEnemies, port/src/common/pm64_foes_pc.c): every TTYD
enemy kind and the PM64 sprite it is drawn as, checked like for like, and every NPC of the story's maps checked
for being drawn as one at all.

  table   each kEnemies entry resolved to its PM64 NPC sprite (the archive's index, the PM64 decomp's names) and
          palette: it fails on a sprite past the archive, a palette or animation past the sprite's, or a sprite
          of another character than its TTYD host (a Koopa drawn as a Goomba). The stand-ins that fight as
          another kind (portPm64EnemyStandIn: Monty Mole as the Big Bandit, Shy Guy as the Glitz Pit Goomba)
          are allowed only as the C code's stand-ins, which the story never uses.
  npcs    every NpcSetupInfo of the story's areas (rels/<area>/src): its fight (battleInfoId into the area's
          btlsetup table), the PM64 enemy its field sprite became before the townsfolk fix (any NPC whose fight
          had a cousin, 2 in 5 with "Mixed", all with "All") and after it (field enemies only, NPC_FLAG_ENEMY).
          A townsfolk NPC (battleInfoId 0, no dead event) holds the area's btlsetup table's first fight, one it
          never starts: on Keelhaul Key that is three Embers, so its Toads and the Whacka were blue flames.

usage: pm64_swap_check.py [table|npcs|all] [--md OUT.md]
Exit status 1 when a check fails. Standard library only.
"""
import argparse
import glob
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
ENEMY_C = os.path.join(ROOT, "port", "src", "common", "pm64_enemy_pc.c")
FOES_C = os.path.join(ROOT, "port", "src", "common", "pm64_foes_pc.c")
UNIT_H = os.path.join(ROOT, "include", "battle", "battle_unit.h")

# The TTYD kind each host is, as PM64's sprite and palette (the like-for-like look). A stand-in's host keeps its
# own kind here (a Big Bandit is a Bandit), so the stand-in fails unless the C code marks it one.
HOST_KIND = {
    "UNIT_GOOMBA": ("Goomba", "Default"),
    "UNIT_SPIKY_GOOMBA": ("SpikedGoomba", "Default"),
    "UNIT_PARAGOOMBA": ("Paragoomba", "Default"),
    "UNIT_HYPER_GOOMBA": ("Goomba", "Hyper"),
    "UNIT_HYPER_PARAGOOMBA": ("Paragoomba", "Hyper"),
    "UNIT_GLOOMBA": ("Goomba", "Dark"),
    "UNIT_PARAGLOOMBA": ("Paragoomba", "Dark"),
    "UNIT_SPIKY_GLOOMBA": ("SpikedGoomba", "Dark"),
    "UNIT_GOOMBA_GLITZVILLE": ("Goomba", None),
    "UNIT_KOOPA_TROOPA": ("KoopaTroopa", "Default"),
    "UNIT_PARATROOPA": ("ParaTroopa", "Default"),
    "UNIT_DARK_KOOPA": ("KoopaTroopa", "Dark"),
    "UNIT_DARK_PARATROOPA": ("ParaTroopa", "Dark"),
    "UNIT_FUZZY": ("Fuzzy", "Default"),
    "UNIT_GREEN_FUZZY": ("Fuzzy", "Forest"),
    "UNIT_FLOWER_FUZZY": ("Fuzzy", "Jungle"),
    "UNIT_BOB_OMB": ("Bobomb", "Default"),
    "UNIT_BULLET_BILL": ("BulletBill", "Default"),
    "UNIT_BILL_BLASTER": ("BillBlaster", "Default"),
    "UNIT_BANDIT": ("Bandit", "Default"),
    "UNIT_BIG_BANDIT": ("Bandit", None),
    "UNIT_CLEFT": ("Cleft", "Default"),
    "UNIT_HYPER_CLEFT": ("Cleft", "Hyper"),
    "UNIT_POKEY": ("Pokey", "Default"),
    "UNIT_POISON_POKEY": ("Pokey", "Mummy"),
    "UNIT_BUZZY_BEETLE": ("BuzzyBeetle", "Default"),
    "UNIT_SPIKE_TOP": ("SpikeTop", "Default"),
    "UNIT_RED_SPIKY_BUZZY": ("BonyBeetle", "Default"),  # PM64's spiked buzzy (no plain Spiky Buzzy there)
    "UNIT_SWOOPER": ("Swooper", "Default"),
    "UNIT_RED_CHOMP": ("StoneChomp", "Default"),
    "UNIT_PUTRID_PIRANHA": ("LargePiranha", "Putrid"),
    "UNIT_FROST_PIRANHA": ("LargePiranha", "Frost"),
    "UNIT_LAVA_BUBBLE": ("LavaBubble", "Default"),
    "UNIT_EMBER": ("LavaBubble", "Blue"),
    "UNIT_LAKITU": ("Lakitu", "Default"),
    "UNIT_SPINY": ("Spiny", "Default"),
    "UNIT_CRAZEE_DAYZEE": ("Dayzee", "Default"),
    "UNIT_AMAZY_DAYZEE": ("Dayzee", "Amazy"),
    "UNIT_RUFF_PUFF": ("RuffPuff", "Default"),
    "UNIT_DRY_BONES": ("DryBones", "Default"),
    "UNIT_MAGIKOOPA": ("Magikoopa", "Default"),
    "UNIT_KOOPATROL": ("Koopatrol", "Default"),
    "UNIT_HAMMER_BRO": ("HammerBros", "Default"),
}

# animation slots of kEnemies and the PM64 animation names each may be
ANIM_SLOTS = (("idle", ("Idle", "Still")), ("walk", ("Walk", "Run", "Fly", "Idle", "Hop")), ("hurt", ("Hurt",)))


def read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


def unit_enum():
    """UNIT_* name -> value, from battle_unit.h's BattleUnitType (each '// 0xNN')."""
    out = {}
    for m in re.finditer(r"^\s*(UNIT_\w+)\s*(?:=\s*(\w+))?\s*,\s*//\s*(0x[0-9A-Fa-f]+)", read(UNIT_H), re.M):
        out[m.group(1)] = int(m.group(3), 16)
    return out


def enemies():
    rows = []
    for m in re.finditer(r'\{"([^"]+)", (0x\w+), (0x\w+), (\d+), (\d+), (\d+), (\d+), (\d+), (UNIT_\w+),', read(ENEMY_C)):
        rows.append({"english": m.group(1), "sprite": int(m.group(4)), "palette": int(m.group(5)),
                     "idle": int(m.group(6)), "walk": int(m.group(7)), "hurt": int(m.group(8)), "host": m.group(9)})
    return rows


def stand_in_hosts():
    m = re.search(r"int portPm64EnemyStandIn\(int i\) \{(.*?)\n\}", read(ENEMY_C), re.S)
    return set(re.findall(r"UNIT_\w+", m.group(1))) if m else set()


def check_table(rows):
    fails, out = [], []
    stand = stand_in_hosts()
    for e in rows:
        sp, pal = e["sprite"], e["palette"]
        note, ok = "", True
        if sp < 0 or sp >= len(NAMES):
            fails.append("%s: sprite %d is outside the archive (0..%d)" % (e["english"], sp, len(NAMES) - 1))
            out.append((e, "?", "?", {}, "OUTSIDE THE ARCHIVE"))
            continue
        name = NAMES[sp]
        if name not in SPRITES:
            fails.append("%s: sprite %d (%s) has no names here: add it to SPRITES" % (e["english"], sp, name))
            out.append((e, name, "?", {}, "UNKNOWN SPRITE"))
            continue
        pals, anims = SPRITES[name]
        pname = pals[pal] if 0 <= pal < len(pals) else None
        if pname is None:
            fails.append("%s: palette %d past %s's %d" % (e["english"], pal, name, len(pals)))
            ok = False
        an = {}
        for slot, want in ANIM_SLOTS:
            i = e[slot]
            if not 0 <= i < len(anims):
                fails.append("%s: %s animation %d past %s's %d" % (e["english"], slot, i, name, len(anims)))
                ok = False
                an[slot] = "?"
                continue
            an[slot] = anims[i]
            if not any(anims[i].startswith(w) for w in want):
                fails.append("%s: %s animation %d is %s's %s" % (e["english"], slot, i, name, anims[i]))
                ok = False
        kind = HOST_KIND.get(e["host"])
        if kind is None:
            fails.append("%s: host %s has no kind in HOST_KIND" % (e["english"], e["host"]))
            ok = False
        elif kind[0] != name or (kind[1] is not None and kind[1] != pname):
            if e["host"] in stand:
                note = "stand-in (Pit, Master Quest; never the story)"
            else:
                fails.append("%s: drawn as %s/%s, but its host %s is a %s/%s" %
                             (e["english"], name, pname, e["host"], kind[0], kind[1]))
                ok = False
        elif e["host"] in stand:
            fails.append("%s: %s marked a stand-in but is like for like" % (e["english"], e["host"]))
            ok = False
        out.append((e, name, pname, an, note if ok else "MISMATCH"))
    return out, fails


# --- the story's NPCs ---

def story_areas():
    return dict((a, int(c)) for a, c in re.findall(r'\{"(\w{3})", (\d)\}', read(FOES_C)))


def area_battles(area):
    """btlsetup table rows -> the first unit's kind of each fight it may pick, and all units' kinds."""
    src = "\n".join(read(p) for p in sorted(glob.glob(os.path.join(ROOT, "rels", area, "src", "**", "*.c"),
                                                     recursive=True)))
    kinds = {}
    for p in glob.glob(os.path.join(ROOT, "rels", "*", "src", "unit", "*.c")) + \
            glob.glob(os.path.join(ROOT, "src", "**", "unit_*.c"), recursive=True):
        for m in re.finditer(r"BattleUnitKind (unit_\w+) = \{\s*(\d+),", read(p)):
            kinds.setdefault(m.group(1), int(m.group(2)))
    parties = {}
    for m in re.finditer(r"BattleUnitSetup (btlparty_\w+)\[\] = \{(.*?)\n\};", src, re.S):
        parties[m.group(1)] = re.findall(r"&(unit_\w+), // unitKindParams\s*\n\s*(\d+), // alliance", m.group(2))
    groups = {}
    for m in re.finditer(r"BattleGroupSetup (btlgrp_\w+) = \{\s*(\d+), // numEnemies\s*(\w+),", src):
        groups[m.group(1)] = parties.get(m.group(3), [])
    sels = {}
    for m in re.finditer(r"NpcBattleSelect (btlgrpsel_\w+)\[\] = \{(.*?)\n\};", src, re.S):
        sels[m.group(1)] = re.findall(r"\{\d+, &(btlgrp_\w+)", m.group(2))
    m = re.search(r"NpcSetupBattleInfo btlsetup_\w+_tbl\[\] = \{(.*?)\n\};", src, re.S)
    table = []
    if m:
        for row in re.split(r"\n    \},?", m.group(1)):
            s = re.search(r"(\w+|NULL), // select", row)
            if s is None:
                continue
            fights = []
            for g in sels.get(s.group(1), []):
                fights.append([(u, kinds.get(u), int(al)) for u, al in groups.get(g, [])])
            table.append(fights)
    return table


def story_npcs(area):
    out = []
    for p in sorted(glob.glob(os.path.join(ROOT, "rels", area, "src", area + "_*.c"))):
        mapname = os.path.splitext(os.path.basename(p))[0]
        for m in re.finditer(r"NpcSetupInfo (\w+)\[\] = \{(.*?)\n\};", read(p), re.S):
            for blk in re.split(r"\n    \},", m.group(2)):
                name = re.search(r'"([^"]*)", // name', blk)
                if name is None:
                    continue
                flags = re.search(r"\n\s*(.*?), // flags", blk)
                dead = re.search(r"\n\s*(\w+), (?:[\w, ]*)// deadEvent", blk)
                bid = re.search(r"(-?\d+), // (?:searchRange\.\.)?battleInfoId", blk)
                out.append({"map": mapname, "list": m.group(1), "name": name.group(1),
                            "enemy": flags is not None and "NPC_FLAG_ENEMY" in flags.group(1),
                            "dead": dead is not None and dead.group(1) != "NULL",
                            "battle": int(bid.group(1)) if bid else -1})
    return out


def check_npcs(rows):
    enum = unit_enum()
    by_value = dict((v, k) for k, v in enum.items())
    stand = stand_in_hosts()
    cousin = {}
    for e in rows:
        if e["host"] not in stand:
            cousin.setdefault(e["host"], e)
    result, fails = [], []
    for area, chapter in sorted(story_areas().items(), key=lambda x: (x[1], x[0])):
        table = area_battles(area)
        for npc in story_npcs(area):
            b = npc["battle"]
            fights = table[b] if 0 <= b < len(table) else []
            looks = set()
            for f in fights:
                if not f or any(al != 1 for _, _, al in f):
                    continue
                if not any(by_value.get(k) in cousin for _, k, _ in f):
                    continue
                lead = cousin.get(by_value.get(f[0][1]))
                looks.add(lead["english"] if lead else "-")
            if not looks:
                continue
            townsfolk = b == 0 and not npc["enemy"] and not npc["dead"]
            before = ", ".join(sorted(looks))
            after = "itself" if townsfolk else before + " (field enemy, if its init makes it one)"
            result.append((chapter, npc, before, after, townsfolk))
    # the field look only for a field enemy: picked() must test NPC_FLAG_ENEMY, else every townsfolk NPC above
    # wanders as its area's first fight's enemy again
    m = re.search(r"static int picked\(NpcEntry\* npc\) \{(.*?)\n\}", read(FOES_C), re.S)
    if m is None or "NPC_FLAG_ENEMY" not in m.group(1):
        fails.append("pm64_foes_pc.c picked() no longer requires NPC_FLAG_ENEMY: %d townsfolk NPCs would be drawn "
                     "as PM64 enemies" % sum(1 for r in result if r[4]))
    return result, fails


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("what", nargs="?", default="all", choices=("table", "npcs", "all"))
    ap.add_argument("--md", help="write the lists as a markdown file")
    a = ap.parse_args()
    rows = enemies()
    md, fails = [], []
    if not rows:
        print("pm64_swap_check: no kEnemies rows parsed from %s" % ENEMY_C)
        return 1
    if a.what in ("table", "all"):
        table, f = check_table(rows)
        fails += f
        md += ["## The enemy table (TTYD kind -> PM64 sprite)", "",
               "| PM64 enemy | TTYD host | sprite | palette | idle / walk / hurt | check |",
               "| --- | --- | --- | --- | --- | --- |"]
        for e, name, pname, an, note in table:
            md.append("| %s | %s | %d %s | %d %s | %s | %s |" % (
                e["english"], e["host"], e["sprite"], name, e["palette"], pname,
                " / ".join(an.get(s, "?") for s, _ in ANIM_SLOTS), note or "ok"))
        print("table: %d entries, %d sprites in the archive, %d failures" % (len(table), len(NAMES), len(f)))
    if a.what in ("npcs", "all"):
        npcs, f = check_npcs(rows)
        fails += f
        md += ["", "## Story NPCs whose fight has a PM64 cousin", "",
               "Before the fix every one of these wandered as the PM64 enemy shown (\"All\"; about 2 in 5 with "
               "\"Mixed\"). After it a townsfolk NPC (battleInfoId 0, no dead event: the area's first, unused "
               "fight) is always itself; field enemies keep their cousin.", "",
               "| chapter | map | NPC | battleInfoId | before | after |", "| --- | --- | --- | --- | --- | --- |"]
        for chapter, npc, before, after, townsfolk in npcs:
            md.append("| %d | %s | %s (%s) | %d | %s | %s |" % (chapter, npc["map"], npc["name"], npc["list"],
                                                             npc["battle"], before, after))
        print("npcs: %d story NPCs with a cousin fight, %d of them townsfolk (fixed: drawn as themselves)" %
              (len(npcs), sum(1 for n in npcs if n[4])))
    for f in fails:
        print("FAIL: " + f)
    if a.md:
        with open(a.md, "w", encoding="utf-8") as fo:
            fo.write("\n".join(md) + "\n")
    print("pm64_swap_check: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


# --- PM64's NPC sprite archive (the decomp's npc_sprite_names.yaml): index -> name, and the palettes and
# animations of the sprites kEnemies uses ---
NAMES = (
    "WorldGoombario", "WorldKooper", "WorldBombette", "WorldParakarry", "WorldBow", "WorldWatt", "WorldSushie", "WorldLakilester",
    "BattleGoombario", "BattleKooper", "BattleBombette", "BattleParakarry", "BattleBow", "BattleWatt", "BattleSushie", "BattleLakilester",
    "KooperWithoutShell", "WorldEldstar", "WorldMamar", "WorldSkolar", "WorldMuskular", "WorldMisstar", "WorldKlevar", "WorldKalmar",
    "BattleEldstar", "BattleMamar", "BattleSkolar", "BattleMuskular", "BattleMisstar", "BattleKlevar", "BattleKalmar", "Twink",
    "JrTroopa", "SpikedJrTroopa", "SpikedParaJrTroopa", "MageJrTroopa", "ParaJrTroopa", "Goomba", "SpikedGoomba", "Paragoomba",
    "KoopaTroopa", "ParaTroopa", "Fuzzy", "Bobomb", "BulletBill", "BillBlaster", "MontyMole", "Cleft",
    "Pokey", "Bandit", "BuzzyBeetle", "Swooper", "StoneChomp", "LargePiranha", "SmallPiranha", "Sentinel",
    "WorldClubba", "BattleClubba", "ShyGuy", "GrooveGuy", "SkyGuy", "PyroGuy", "SpyGuy", "MediGuy",
    "Fuzzipede", "SpearGuy", "HeartPlant", "HurtPlant", "MBush", "LavaBubble", "KentCKoopa", "Dayzee",
    "Lakitu", "Spiny", "Bzzap", "TuffPuff", "SpikeTop", "Duplighost", "AlbinoDino", "Blooper",
    "BabyBlooper", "Gulpit", "DryBones", "ThrownBone", "BonyBeetle", "Magikoopa", "FlyingMagikoopa", "WorldKoopatrol",
    "Koopatrol", "HammerBros", "BushBasic", "BushBlocky", "BushDry", "BushLeafy", "BushMatted", "WorldKammy",
    "BattleKammy", "GoombaBros", "GoombaKing", "SpikyGoomnut", "DarkToad", "KoopaBros", "Buzzar", "Tutankoopa",
    "ChainChomp", "WorldTubba", "BattleTubba", "TubbasHeart", "BigLanternGhost", "ShySquadGuy", "MarshalGuy", "StiltGuy",
    "StiltGuyUnfold", "ShyStackGuy", "ShyStackUnfold", "ShyStackDamage", "ShyStackRock", "GeneralGuy", "GeneralGuyBomb", "TankGuy",
    "LavaPiranha", "PetitPiranha", "LavaBud", "HuffNPuff", "RuffPuff", "Monstar", "CrystalKing", "WorldBowser",
    "BattleBowser", "Luigi", "Toad", "Toadette", "VannaT", "ToadKid", "ToadGuard", "HarryT",
    "ToadMinister", "Postmaster", "ConductorToad", "TrainToad", "Fishmael", "Musician", "Koopa", "KoopaWithoutShell",
    "WorldBobomb", "Whacka", "Dryite", "Mouser", "Boo", "Yoshi", "YoshiKid", "Raven",
    "Bubulb", "Penguin", "ShiverToad", "AltBandit", "Goompa", "Goombaria", "Gooma", "Goompapa",
    "Goomama", "TheMaster", "Chan", "Lee", "Merlon", "ChetRippo", "Rowf", "MinhT",
    "RussT", "TayceT", "FiceT", "Bartender", "Chanterelle", "RipCheato", "ChuckQuizmo", "Merluvlee",
    "Merlar", "Merlow", "StarMan", "KoloradoWife", "KoopaKoot", "Kolorado", "BattleKolorado", "Archeologist",
    "Nomadimouse", "WorldMerlee", "BattleMerlee", "DisguisedMoustafa", "Moustafa", "Oaklie", "Bootler", "Yakkey",
    "GourmetGuy", "VillageLeader", "LeadersFriend", "RaphaelRaven", "Tolielup", "GateFlower", "Petunia", "Posie",
    "Lily", "Rosie", "Sun", "Lakilulu", "Ninji", "MayorPenguin", "MayorPenguinWife", "PenguinPatrol",
    "Herringway", "Merle", "StarRod", "Fire", "Coin", "ParadePeach", "ParadeKoopas", "ParadeBurntBowser",
    "ParadeLuigi", "ParadePartner", "ParadeYoshi", "ParadeKolorado", "ParadeChick", "ParadeIceShow", "ParadeToad", "ParadeTwirler",
    "ParadeDrummer", "ParadeBanner", "ParadeHorn", "ParadeTubba", "ParadeWizard", "ParadeMario", "ParadeShyGuy", "ParadeTwink",
    "Leaf",
)
SPRITES = {
    "Goomba": (('Default', 'Red', 'Hyper', 'Frost', 'Dark', 'Poisoned', 'Red_Poisoned', 'Hyper_Poisoned', 'Frost_Poisoned', 'Dark_Poisoned', 'Dizzy', 'Red_Dizzy', 'Hyper_Dizzy', 'Frost_Dizzy', 'Dark_Dizzy', 'Shocked', 'Red_Shocked', 'Hyper_Shocked', 'Frost_Shocked', 'Dark_Shocked', 'Burnt', 'Mushroom', 'Toadstool'),
        ('Still', 'Idle', 'Walk', 'Run', 'Midair', 'Hurt', 'Dead', 'Dizzy', 'Sleep', 'BurnHurt', 'BurnStill', 'Tense', 'FakeToadstool', 'FakeMushroom', 'Electrocute')),
    "SpikedGoomba": (('Default', 'Dark', 'Poisoned', 'Dark_Poisoned', 'Dizzy', 'Dark_Dizzy', 'Shocked', 'Dark_Shocked', 'Burnt'),
        ('Still', 'Idle', 'Walk', 'Run', 'Midair', 'HurtStill', 'Hurt', 'BurnEyesClosed', 'BurnEyesOpen', 'Dizzy', 'Sleep', 'Laugh', 'Electrocute')),
    "Paragoomba": (('Default', 'Red', 'Hyper', 'Dark', 'Poisoned', 'Red_Poisoned', 'Hyper_Poisoned', 'Dark_Poisoned', 'Dizzy', 'Red_Dizzy', 'Hyper_Dizzy', 'Dark_Dizzy', 'Shocked', 'Red_Shocked', 'Hyper_Shocked', 'Dark_Shocked', 'Burnt'),
        ('Still', 'Idle', 'Miss', 'Walk', 'Run', 'Dive', 'Hurt', 'HurtStill', 'WingsStill', 'LWingStill', 'RWingStill', 'Sleep', 'Dizzy', 'BurnHurt', 'BurnStill', 'Electrocute')),
    "KoopaTroopa": (('Dark', 'Default', 'Frost', 'Dark_Poisoned', 'Poisoned', 'Frost_Poisoned', 'Dark_Dizzy', 'Dizzy', 'Frost_Dizzy', 'Dark_Shocked', 'Shocked', 'Frost_Shocked', 'Burnt'),
        ('Still', 'Idle', 'Scramble', 'Walk', 'Run', 'ShellStill', 'ShellSpin', 'ShellEnter', 'ShellExit', 'Salute', 'HurtStill', 'Hurt', 'BurnHurt', 'BurnStill', 'Floating', 'ToppleStruggle', 'ToppleStill', 'Sleep', 'ToppleSleep', 'Stunned', 'ToppleStunned', 'BeginPanic1', 'BeginPanic2', 'Panic')),
    "ParaTroopa": (('Dark', 'Default', 'Dark_Poisoned', 'Poisoned', 'Dark_Dizzy', 'Dizzy', 'Dark_Shocked', 'Shocked', 'Burnt'),
        ('Still', 'Idle', 'Walk', 'Run', 'Windup', 'Recoil', 'HurtStill', 'Hurt', 'BurnHurt', 'BurnStill', 'ShellIdle', 'ShellEnter', 'ShellDive', 'ShellHit', 'ShellExit', 'WingsStill', 'WingsBurn', 'UnusedIdle', 'Sleep', 'Stunned')),
    "Fuzzy": (('Default', 'Forest', 'Blue', 'Jungle', 'Poisoned', 'Forest_Poisoned', 'Blue_Poisoned', 'Jungle_Poisoned', 'Dizzy', 'Forest_Dizzy', 'Blue_Dizzy', 'Jungle_Dizzy', 'Shocked', 'Forest_Shocked', 'Blue_Shocked', 'Jungle_Shocked', 'Burnt'),
        ('Still', 'Idle', 'Walk', 'Run', 'Leech', 'Jump', 'Ambush', 'Bite', 'Hurt', 'Confused', 'HurtLoop', 'Talk', 'Shout', 'Divide', 'Sleep', 'Stunned', 'Burn', 'HurtStill', 'BurnStill', 'HurtShock', 'HurtShockLoop')),
    "Bobomb": (('Default', 'Red', 'Burst', 'Fuse', 'Poisoned', 'Red_Poisoned', 'Burst_Poisoned', 'Fuse_Poisoned', 'Dizzy', 'Red_Dizzy', 'Burst_Dizzy', 'Fuse_Dizzy', 'Shocked', 'Red_Shocked', 'Burst_Shocked', 'Fuse_Shocked', 'Burnt'),
        ('Still', 'StillLit', 'Idle', 'IdleLit', 'Walk', 'WalkLit', 'Run', 'RunLit', 'Cower', 'Panic', 'KO', 'AngryIdle', 'AngryChase', 'AngryKO', 'Hurt', 'HurtLit', 'BurnHurt', 'BurnStill', 'BurnHurtLit', 'BurnStillLit', 'Buildup', 'Boom', 'FaceFwd', 'WaveFwd', 'Sleep', 'Wake', 'SleepLit', 'WakeLit', 'Dizzy', 'DizzyLit')),
    "BulletBill": (('Default', 'Gold', 'Poisoned', 'Gold_Poisoned', 'Dizzy', 'Gold_Dizzy', 'Shocked', 'Gold_Shocked', 'Burnt'),
        ('Still', 'Idle', 'TenseCopy', 'Tense', 'Fire', 'Hurt', 'Recover', 'BurnHurt')),
    "BillBlaster": (('Default', 'Gold', 'Poisoned', 'Gold_Poisoned', 'Dizzy', 'Gold_Dizzy', 'Shocked', 'Gold_Shocked', 'Burnt', 'Gold_Burnt'),
        ('Still', 'Idle', 'Fire', 'Hurt', 'Collapse', 'Burned', 'Buildup', 'Release')),
    "MontyMole": (('Default', 'Dark', 'Poisoned', 'Dark_Poisoned', 'Dizzy', 'Dark_Dizzy', 'Shocked', 'Dark_Shocked', 'Burnt'),
        ('Still', 'Idle', 'Walk', 'Run', 'ThrowAttack', 'Hurt', 'HurtJump', 'HurtMidair', 'HurtDropDown', 'Panic', 'BurnHurt', 'BurnStill', 'BurnHurtJump', 'Smoke', 'PopOutStill', 'Rock', 'Emerge', 'Burrow', 'Hole', 'WallHole', 'Sleep', 'Clap', 'WorldEmerge', 'WorldBurrow', 'WorldThrowAttack', 'Stand', 'WorldGetRock', 'WorldThrowRock')),
    "Cleft": (('Default', 'Hyper', 'Poisoned', 'Hyper_Poisoned', 'Dizzy', 'Hyper_Dizzy', 'Shocked', 'Hyper_Shocked', 'Blend1', 'Blend2', 'Blend3', 'Hyper_Blend1', 'Hyper_Blend2', 'Hyper_Blend3', 'Rock'),
        ('Still', 'ToppleStill', 'Idle', 'ToppleKick', 'Party', 'ToppleIdle', 'Walk', 'Run', 'Hurt', 'ToppleHurt', 'ToppleStillAlt', 'ToppleHurtAlt', 'Sleep', 'ToppleSleep', 'Dizzy', 'ToppleDizzy', 'Crouch', 'Charge', 'RockWobble', 'Reveal', 'Hiding', 'Disguise', 'CamoOn', 'CamoOff', 'Laugh', 'HyperCamoOn', 'HyperCamoOff')),
    "Pokey": (('Default', 'Mummy', 'Frost', 'Poisoned', 'Mummy_Poisoned', 'Frost_Poisoned', 'Dizzy', 'Mummy_Dizzy', 'Frost_Dizzy', 'Shocked', 'Mummy_Shocked', 'Frost_Shocked', 'Burnt'),
        ('Still4', 'Still3', 'Still2', 'Still1', 'Idle4', 'Idle3', 'Idle2', 'Idle1', 'Run4', 'Run3', 'Run2', 'Run1', 'Hurt4', 'Hurt3', 'Hurt2', 'Hurt1', 'Windup3', 'Throw3', 'Windup2', 'Throw2', 'Windup1', 'Throw1', 'Projectile', 'Emerge', 'BurnHurt4', 'BurnStill4', 'BurnHurt3', 'BurnStill3', 'BurnHurt2', 'BurnStill2', 'BurnHurt1', 'BurnStill1', 'BurnedProjectile', 'Dizzy4', 'Dizzy3', 'Dizzy2', 'Dizzy1', 'Sleep4', 'Sleep3', 'Sleep2', 'Sleep1')),
    "Bandit": (('Default', 'Poisoned', 'Dizzy', 'Shocked', 'Burnt', 'Coin'),
        ('Still', 'Idle', 'IdleHolding', 'StillUnused', 'StillHolding', 'Walk', 'WalkHolding', 'Run', 'RunHolding', 'Hurt', 'BurnHurt', 'BurnStill', 'Poof', 'Tackle', 'LookUp', 'GotCoin', 'Sleep', 'Dizzy', 'Laugh', 'Relief', 'Coin')),
    "BuzzyBeetle": (('Default', 'Poisoned', 'Dizzy', 'Shocked', 'Burnt'),
        ('Still', 'Idle', 'ToppleIdle', 'Walk', 'Run', 'ShellSpin', 'ShellStill', 'Hurt', 'FallDown', 'Sleep', 'Dizzy', 'BurnHurt', 'ShellEnter', 'ShellExit', 'CeilingStill', 'CeilingIdle', 'CeilingWalk', 'CeilingRun', 'CeilingShell', 'CeilingHurt', 'CeilingSleep', 'CeilingDizzy', 'CeilingShellEnter', 'CeilingShellExit', 'CeilingBurnHurt', 'ToppleHurt', 'ToppleStill', 'ToppleSleep', 'ToppleStruggle', 'ToppleBurnHurt')),
    "Swooper": (('Default', 'Gray', 'Poisoned', 'Gray_Poisoned', 'Dizzy', 'Gray_Dizzy', 'Shocked', 'Gray_Shocked', 'Burnt'),
        ('StillRoost', 'Still', 'Idle', 'IdleCentered', 'WorldDead', 'StillAlt', 'Fly', 'SleepRoost', 'DizzyRoost', 'BurnStillRoost', 'BurnHurtRoost', 'StillHang', 'IdleHang', 'DetachedHang', 'HurtHang', 'SleepHang', 'DizzyHang', 'BurnHurtHang', 'BurnStillHang', 'Sleep', 'Dizzy', 'Hurt', 'HurtStill', 'BurnHurt', 'BurnStill', 'HurtStillHang', 'HurtUpsideDown', 'IdleRoost')),
    "StoneChomp": (('Default', 'Poisoned', 'Dizzy', 'Shocked', 'Burnt'),
        ('Still', 'Idle', 'Chain', 'QuickBite', 'Bite', 'SlowBite', 'Hurt', 'BurnHurt', 'BurnStill', 'BurnChain', 'Sleep', 'Dizzy')),
    "LargePiranha": (('Putrid', 'Frost', 'Putrid_Poisoned', 'Frost_Poisoned', 'Putrid_Dizzy', 'Frost_Dizzy', 'Putrid_Shocked', 'Frost_Shocked', 'Burnt'),
        ('Still', 'Idle', 'Walk', 'Run', 'Talk', 'Windup', 'Bite', 'PostBite', 'BadBreathStart', 'BadBreathLoop', 'BadBreathEnd', 'FrostBreathStart', 'FrostBreathLoop', 'FrostBreathEnd', 'Hurt', 'Dizzy', 'BurnHurt', 'BurnStill', 'Retract', 'Hidden', 'Extend', 'CeilingStill', 'CeilingIdle', 'Emerge', 'Burrow')),
    "ShyGuy": (('Red', 'Blue', 'Green', 'Pink', 'Yellow', 'Black', 'Red_Poisoned', 'Blue_Poisoned', 'Green_Poisoned', 'Pink_Poisoned', 'Yellow_Poisoned', 'Black_Poisoned', 'Red_Dizzy', 'Blue_Dizzy', 'Green_Dizzy', 'Pink_Dizzy', 'Yellow_Dizzy', 'Black_Dizzy', 'Red_Shocked', 'Blue_Shocked', 'Green_Shocked', 'Pink_Shocked', 'Yellow_Shocked', 'Black_Shocked', 'Burnt'),
        ('Still', 'Idle', 'Walk', 'Run', 'Dash', 'Tackle', 'ArmsUp', 'Bow', 'Neutral', 'Jump', 'FallingKick', 'NeutralAlt', 'Hurt', 'BurnHurt', 'BurnStill', 'Smoke', 'Crashed', 'Excited', 'Slouch', 'Dizzy', 'Scared', 'Hiding')),
    "LavaBubble": (('Default', 'Blue', 'Glow', 'BlueGlow', 'Poisoned', 'Blue_Poisoned', 'Glow_Poisoned', 'BlueGlow_Poisoned', 'Dizzy', 'Blue_Dizzy', 'Glow_Dizzy', 'BlueGlow_Dizzy', 'Shocked', 'Blue_Shocked', 'Glow_Shocked', 'BlueGlow_Shocked', 'Burnt'),
        ('Still', 'Idle', 'FlySlow', 'FlyFast', 'Confused', 'Inhale', 'Exhale', 'Hurt', 'BurnHurt', 'BurnStill', 'Dizzy', 'Sleep')),
    "Dayzee": (('Default', 'Amazy', 'Poisoned', 'Amazy_Poisoned', 'Dizzy', 'Amazy_Dizzy', 'Shocked', 'Amazy_Shocked', 'Burnt'),
        ('Still', 'Idle', 'Walk', 'Run', 'Dash', 'Panic', 'SingSlow', 'SingFast', 'Hurt', 'BurnHurt', 'BurnStill', 'Dizzy', 'Sleep', 'Shocked', 'Skip1', 'Skip2')),
    "Lakitu": (('Default', 'Spiny', 'Poisoned', 'Spiny_Poisoned', 'Dizzy', 'Spiny_Dizzy', 'Shocked', 'Spiny_Shocked', 'Burnt'),
        ('Still', 'Idle', 'FlySlow', 'FlyFast', 'Horror', 'BurnHurt', 'BurnStill', 'Hurt', 'Sleep', 'Dizzy', 'SmallCloudVanish', 'SpinyLargeStill', 'SpinyLargeSpin', 'SpinyMediumStill', 'SpinyMediumSpin', 'SpinySmallStill', 'SpinySmallSpin', 'SpinyHalfOutSpin', 'FindSpiny', 'LiftSpiny', 'GetSpiny', 'ThrowSpiny', 'Talk')),
    "Spiny": (('Default', 'Poisoned', 'Dizzy', 'Shocked', 'Burnt'),
        ('Still', 'Idle', 'IdleTopple', 'Walk', 'Run', 'Shell', 'ShellEnter', 'ShellExit', 'ShellSpin', 'Hurt', 'HurtTopple', 'BurnHurt', 'BurnStill', 'BurnHurtTopple', 'BurnStillTopple', 'Fall', 'Sleep', 'SleepTopple', 'Dizzy', 'DizzyTopple', 'Struggle', 'StillTopple', 'FlipOver', 'FlipUp', 'Ball', 'CurlUp', 'Uncurl', 'StillCeiling', 'IdleCeiling', 'WalkCeiling', 'RunCeiling', 'ShellCeiling', 'ShellEnterCeiling', 'ShellExitCeiling', 'HurtCeiling', 'BurnHurtCeiling', 'BurnStillCeiling', 'SleepCeiling', 'DizzyCeiling')),
    "SpikeTop": (('Default', 'Poisoned', 'Dizzy', 'Shocked', 'Burnt'),
        ('Still', 'StillCeiling', 'StillTopple', 'Idle', 'IdleCeiling', 'IdleTopple', 'Walk', 'WalkCeiling', 'Run', 'RunCeiling', 'ShellSpin', 'ShellEnter', 'ShellExit', 'ShellEnterCeiling', 'ShellExitCeiling', 'Shell', 'ShellCeiling', 'FlipOver', 'FlipUp', 'Hurt', 'HurtCeiling', 'HurtTopple', 'BurnHurt', 'BurnStill', 'BurnHurtTopple', 'BurnHurtCeiling', 'BurnStillCeiling', 'Sleep', 'SleepCeiling', 'SleepTopple', 'Dizzy', 'DizzyCeiling', 'DizzyTopple')),
    "DryBones": (('Default', 'Poisoned', 'Dizzy', 'Shocked', 'Burnt'),
        ('Still', 'BonePile', 'Idle', 'Walk', 'Run', 'AimBone', 'ThrowBone', 'Hurt', 'Collapse', 'Struggle', 'Reform', 'BurnHurt', 'BurnStill', 'Dizzy', 'Sleep', 'Bone')),
    "BonyBeetle": (('Default', 'Poisoned', 'Dizzy', 'Shocked', 'Burnt'),
        ('Still', 'StillSpiky', 'StillTopple', 'StillSpikyTopple', 'Idle', 'IdleSpiky', 'IdleTopple', 'IdleSpikyTopple', 'FlipOver', 'FlipOverSpiky', 'FlipUp', 'FlipUpSpiky', 'Walk', 'WalkSpiky', 'Run', 'RunSpiky', 'ShellEnter', 'ShellEnterSpiky', 'ShellExit', 'ShellExitSpiky', 'ShellStill', 'ShellStillSpiky', 'ShellSpin', 'ShellSpinSpiky', 'Hurt', 'HurtSpiky', 'HurtToppled', 'HurtToppledSpiky', 'Struggle', 'StruggleSpiky', 'BurnHurt', 'BurnStill', 'BurnHurtSpiky', 'BurnStillSpiky', 'BurnHurtToppled', 'BurnStillToppled', 'BurnHurtToppledSpiky', 'BurnStillToppledSpiky', 'Dizzy', 'DizzySpiky', 'DizzyToppled', 'DizzyToppledSpiky', 'Sleep', 'SleepSpiky', 'SleepToppled', 'SleepToppledSpiky', 'ExtendSpikes', 'RetractSpikes')),
    "Magikoopa": (('Default', 'Red', 'Yellow', 'White', 'Green', 'Gray', 'Poisoned', 'Red_Poisoned', 'Yellow_Poisoned', 'White_Poisoned', 'Green_Poisoned', 'Gray_Poisoned', 'Dizzy', 'Red_Dizzy', 'Yellow_Dizzy', 'White_Dizzy', 'Green_Dizzy', 'Gray_Dizzy', 'Shocked', 'Red_Shocked', 'Yellow_Shocked', 'White_Shocked', 'Green_Shocked', 'Gray_Shocked', 'Burnt'),
        ('Still', 'Idle', 'Shout', 'CastSpell', 'Hurt', 'BurnHurt', 'BurnStill', 'Dizzy', 'Sleep')),
    "Koopatrol": (('Default', 'Poisoned', 'Dizzy', 'Shocked', 'Burnt'),
        ('Still', 'Idle', 'FaceFront', 'Walk', 'Run', 'Shell', 'ShellSpin', 'ShellEnter', 'ShellExit', 'LeanSide', 'HurtStill', 'Hurt', 'BurnHurt', 'BurnStill', 'ShellUnused', 'ToppleStruggle', 'ToppleIdle', 'Sleep', 'ToppleSleep', 'Dizzy', 'ToppleDizzy', 'Lift', 'Panic')),
    "HammerBros": (('Default', 'Poisoned', 'Dizzy', 'Shocked', 'Burnt'),
        ('Still', 'StillDisarmed', 'Idle', 'IdleDisarmed', 'Walk', 'CarrySlow', 'WalkDisarmed', 'Run', 'CarryFast', 'RunDisarmed', 'Talk', 'TalkDisarmed', 'Throw', 'Hammer', 'Hurt', 'BurnHurt', 'BurnStill', 'Sleep', 'Dizzy', 'Lift', 'Toss', 'ShellEnter', 'Shell', 'ShellExit', 'WorldAim', 'WorldThrow', 'GotItem')),
    "RuffPuff": (('Default', 'Poisoned', 'Dizzy', 'Shocked', 'Burnt'),
        ('Still', 'Idle', 'Angry', 'Walk', 'Run', 'Grin', 'Hurt', 'BurnHurt', 'BurnStill', 'Sleep', 'Blush', 'Dizzy', 'Talk', 'AngryTalk')),
}


if __name__ == "__main__":
    sys.exit(main())
