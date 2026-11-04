/* Super Luigi, chapter 6: "Hatesong Tower" (docs/super_luigi/ch6.md). The
 * climb of Creepy Steeple's rooms (jin_*), from the Howling Cliff (gra_06) to
 * the Crown Loft, and the ending of the whole story at the brothers' house
 * (aaa_00). Each room is a mod map on borrowed geometry with an init of ours
 * (noSetup: none of the borrowed map's NPCs or field enemies).
 *
 * The friends' field abilities: each puzzle is a framework point of its
 * friend's ability (evt_sl_point), so his own field move solves it. Until a
 * friend's file is built (portSlFriendX returning NULL: the chapters 2-5
 * branches add Jerry, Torque, Hayzee and Screamy), walking up to the puzzle
 * has him pop out and do it, so the chapter plays whole either way.
 *
 * The bosses, the Wailer's tweaks, the formations and Future Sight are in
 * superluigi_ch06_boss_aurora.c; the text in superluigi_ch06_text_aurora.c. */
#include "port/superluigi.h"
#include "port/superluigi_evt.h"
#include "port/mod_host.h"
#include "port/hook_list.h"
#include "battle/battle_party.h"
#include "battle/battle_unit.h"
#include "data/item_data.h"
#include "data/party_data.h"
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "driver/npcdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_hit.h"
#include "evt/evt_window.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mario.h"
#include "mario/mario_party.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "system.h"
#include "data_tables.h"
#include <dolphin/gx.h>
#include <dolphin/pad.h>
#include <dolphin/mtx.h>

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* the game's Japanese names (Shift-JIS: tools/port/sjis_src.py doesn't touch port sources) */
#define SJ6_BOO "\x83" "\x65" "\x83" "\x8C" "\x83" "\x54"
#define SJ6_SWOOPER "\x83" "\x6F" "\x83" "\x54" "\x83" "\x6F" "\x83" "\x54"
#define SJ6_SWOOPER_AI "\x83" "\x6F" "\x83" "\x54" "\x83" "\x6F" "\x83" "\x54" "\x81" "\x69" "\x93" "\x56" "\x88" "\xE4" "\x81" "\x6A"
#define SJ6_BUZZY "\x83" "\x81" "\x83" "\x62" "\x83" "\x67"
#define SJ6_SPIKY "\x83" "\x67" "\x83" "\x51" "\x83" "\x81" "\x83" "\x62" "\x83" "\x67"
#define SJ6_SPIKY_AI "\x93" "\x56" "\x88" "\xE4" "\x83" "\x67" "\x83" "\x51" "\x83" "\x81" "\x83" "\x62" "\x83" "\x67"
#define SJ6_PARROT "\x83" "\x49" "\x83" "\x45" "\x83" "\x80"

/* chapter 6's flags: the framework's bank (SL_GSWF, 0-99) */
#define SLF(n) SL_GSWF(6, n)
enum {
    F_ARRIVED,
    F_AMBUSH,
    F_SWITCH_W,   /* jin_61's switch */
    F_SWITCH_E,   /* jin_62's */
    F_RUST,       /* jin_61's gate freed */
    F_BOOS,       /* jin_62's Boos danced away */
    F_WAILER,     /* the Wailer beaten */
    F_WALL,       /* jin_65's wall blown */
    F_TORCH_1,
    F_TORCH_2,
    F_TORCH_3,
    F_PARROT_0,   /* the parrot's four lines heard */
    F_PARROT_1,
    F_PARROT_2,
    F_PARROT_3,
    F_KEY,        /* the Crown Key */
    F_FLOOR,      /* jin_68's floor broken */
    F_FLOAT,      /* floated over the shaft */
    F_WINCH,      /* jin_69's winch freed */
    F_GOO,        /* swum the goo: Blooey burnt */
    F_UNLOCK,     /* the Crown Door */
    F_READY,      /* "everybody ready?" */
    F_VAULT,      /* the Vault's hidden door seen */
    F_KING,       /* the King's scene played */
    F_KING_DOWN,
    F_CREPE_DOWN,
    F_DONE,
    F_CHOIR,      /* jin_66: all the torches lit */
    F_FELL,       /* jin_68's floor: fell through, to jin_69 */
    /* pickups */
    P_GRA_PIECE = 40,
    P_61_JELLY,
    P_61_HPPLUS,
    P_62_COIN1,
    P_62_COIN2,
    P_62_FIREDRIVE,
    P_63_PIECE,
    P_63_TILE,
    P_64_POWER,
    P_64_ULTRA,
    P_64_PIECE,
    P_64_SHINE,
    P_65_LASTSTAND,
    P_66_SHINE,
    P_66_PIECE,
    P_67_MEGARUSH,
    P_67_SYRUP,
    P_67_PIECE,
    P_68_SHROOM,
    P_68_SHINE,
    P_69_TORNADO,
    P_70_COIN,
    P_70_LIFE,
    P_70_PIECE,
};

USER_FUNC(evt_sl6_boss_arm);  /* superluigi_ch06_boss_aurora.c: 1 Wailer, 2 King, 3 Crepe, 0 off */
USER_FUNC(evt_sl6_boss_used); /* out: 1 if Hayzee sang in the Wailer's fight */
USER_FUNC(evt_sl_lava_zone);  /* superluigi_statue_aurora.c */
USER_FUNC(evt_luigi_story_pose); /* luigi_pc.c */
static USER_FUNC(sl6_boos_seen); /* the Boos shown for a while (below, by boosFrame) */
s32 psndSFXOn(const char* name);
void psndSFX_pit(s32 id, s16 pitch);
int portSlRiding(void);       /* superluigi_blooey_aurora.c */

/* ======================================================================
 * Helpers in C
 * ====================================================================== */

/* in: a friend (PORT_SL_FRIEND_*); out: 1 if he's the partner out now, and his hint's tag
 * (who can do it, and how: switch to him, then his ability) */
static USER_FUNC(sl6_friend_out) {
    static const char* const kHint[] = {NULL, "sl_c6_hint_blooey", "sl_c6_hint_jerry", "sl_c6_hint_torque",
                                        "sl_c6_hint_hayzee", "sl_c6_hint_screamy"};
    const int id = (int)evtGetValue(event, event->args[0]);
    const PortSlFriend* f = portSlGetFriend(id);
    PartyEntry* party = partyGetPtr(marioGetPartyId());
    (void)isFirstCall;
    evtSetValue(event, event->args[1], f != NULL && party != NULL && party->currentMemberId == f->partyId);
    evtSetValue(event, event->args[2], (EvtWord)(AddrWord)(id > 0 && id <= 5 ? kHint[id] : "sl_c6_hint_friend"));
    return EVT_RETURN_DONE;
}

/* in: a tag, a value: to the log (a scene's progress, tests) */
static USER_FUNC(sl6_log) {
    (void)isFirstCall;
    printf("[sl6] %s %d\n", (const char*)evtGetValue(event, event->args[0]), (int)evtGetValue(event, event->args[1]));
    fflush(stdout);
    return EVT_RETURN_DONE;
}

/* in: 1: the partner out steps away for a scene (remembered); 0: he's back; 2: he stays away */
static USER_FUNC(sl6_party_stash) {
    static int sStashed;
    PartyEntry* p;
    const int on = (int)evtGetValue(event, event->args[0]);
    (void)isFirstCall;
    if (on == 2) {
        sStashed = 0;
    } else if (on != 0) {
        p = partyGetPtr(marioGetPartyId());
        sStashed = p != NULL ? p->currentMemberId : 0;
        if (p != NULL) {
            marioPartyKill();
        }
    } else if (sStashed != 0) {
        marioPartyEntry((PartyId)sStashed);
        sStashed = 0;
    }
    return EVT_RETURN_DONE;
}

/* in: 1: the tag's stills (sl_st0..2) freeze where their poses are; 0: they move again */
static USER_FUNC(sl6_still_hold) {
    static const char* const kNames[] = {"sl_st0", "sl_st1", "sl_st2"};
    const int on = (int)evtGetValue(event, event->args[0]);
    int i;
    (void)isFirstCall;
    for (i = 0; i < 3; i++) {
        NpcEntry* npc = npcNameToPtr_NoAssert(kNames[i]);
        if (npc != NULL) {
            animPoseSetLocalTimeRate(npc->poseId, on ? 0.0f : 1.0f);
        }
    }
    return EVT_RETURN_DONE;
}

/* in: a flag (its word); out: whether it's set */
static USER_FUNC(sl6_flag) {
    /* the flag's own word (SET_RAW_VALUE(LW, SLF(...))), read here; 0: none */
    const EvtWord w = evtGetValue(event, event->args[0]);
    (void)isFirstCall;
    evtSetValue(event, event->args[1], w != 0 ? (evtGetValue(event, w) ? 1 : 0) : 0);
    return EVT_RETURN_DONE;
}

/* the friends at their ranks coming in (ch6.md: Blooey and Jerry Super) */
static USER_FUNC(sl6_ranks) {
    PouchData* p = pouchGetPtr();
    (void)event;
    (void)isFirstCall;
    if (portSuperLuigiMode() == PORT_SL_INN) {
        const PortSlFriend* f;
        if ((f = portSlGetFriend(PORT_SL_FRIEND_BLOOEY)) != NULL && p->partyData[f->partyId].attackLevel < 1) {
            p->partyData[f->partyId].attackLevel = p->partyData[f->partyId].techLevel = 1;
        }
        if ((f = portSlGetFriend(PORT_SL_FRIEND_JERRY)) != NULL && p->partyData[f->partyId].attackLevel < 1) {
            p->partyData[f->partyId].attackLevel = p->partyData[f->partyId].techLevel = 1;
        }
    }
    return EVT_RETURN_DONE;
}

/* in: x, z, radius: blocks until Luigi stands within it and can move */
static USER_FUNC(sl6_wait_near) {
    EvtWord* a = event->args;
    MarioWork* m = marioGetPtr();
    f32 dx = m->position.x - (f32)evtGetValue(event, a[0]);
    f32 dz = m->position.z - (f32)evtGetValue(event, a[1]);
    f32 r = (f32)evtGetValue(event, a[2]);
    (void)isFirstCall;
    if (gp->inBattle || !marioChkKey() || dx * dx + dz * dz > r * r) {
        return EVT_RETURN_BLOCK;
    }
    return EVT_RETURN_DONE;
}

/* in: a height: blocks until Luigi is above it */
static USER_FUNC(sl6_wait_above) {
    (void)isFirstCall;
    return marioGetPtr()->position.y >= (f32)evtGetValue(event, event->args[0]) ? EVT_RETURN_DONE : EVT_RETURN_BLOCK;
}

/* Blooey scorched black (docs/LUIGI_ANIMS.md, "Soot-black Blooey"): while
 * on, every pose c_lp_gesso plays (field, battle, his own move scripts) is
 * its SOOT_ twin, through animdrv.c's portAnimAlias, and his faces are the
 * sooty ones. 1 on, 0 back as he was. */
static int sSoot;

/* animdrv.c: a pose name as played (the framework's weak one returns it as is) */
const char* portAnimAlias(const char* file, const char* name) {
    static char sName[8][40];
    static int sNext;
    char* out;
    if (!sSoot || portSuperLuigiChapter() != 6 || file == NULL || (AddrWord)name < 0x10000 || strcmp(file, "c_lp_gesso") != 0 ||
        strncmp(name, "SOOT_", 5) == 0 || strncmp(name, "BURN_", 5) == 0) {
        return name;
    }
    out = sName[sNext++ & 7];
    snprintf(out, sizeof(sName[0]), "SOOT_%s", name);
    return out;
}

static void sootSet(int on) {
    if (on == sSoot) {
        return;
    }
    sSoot = on;
    portSlFriendIcons(PORT_SL_FRIEND_BLOOEY, on ? "art/icons/blooey/blooey_soot_menu.png" : NULL,
                      on ? "art/icons/blooey/blooey_soot_hud.png" : NULL);
    printf("[superluigi] chapter 6: Blooey %s\n", on ? "burnt" : "clean");
    fflush(stdout);
}

/* in: 1 burnt, 0 clean */
static USER_FUNC(sl6_soot) {
    (void)isFirstCall;
    sootSet((int)evtGetValue(event, event->args[0]));
    return EVT_RETURN_DONE;
}

/* out: 1 if a partner is out */
static USER_FUNC(sl6_party_out) {
    (void)isFirstCall;
    evtSetValue(event, event->args[0], partyGetPtr(marioGetPartyId()) != NULL);
    return EVT_RETURN_DONE;
}

/* places measured in the borrowed rooms ($TTYD_MAPOBJ_DUMP's object centres, the test runs' "where") */
#define SHAFT_NEAR_X 700 /* jin_11: the stairs' top, before the breaking floor; the floor (845, the room's end) */
#define SHAFT_X 845
#define SHAFT_Y 115
#define SHAFT_Z 5
#define TORCH_Z 30 /* jin_07: the dark torches (honou02-04, on taimatsu02-04; honou01 at x -134 burns) */
#define TORCH1_X (-41)
#define TORCH2_X 45
#define TORCH3_X 131
#define TORCH_MID_X 45
#define VAULT_X (-440) /* jin_00: the Vault's way in (n_bero_1), an opening in the back wall, ground floor */
#define VAULT_Z (-230)
#define HALL_SPRING_X (-250) /* jin_00: the spring up to the gallery, on the ground at the back */
#define HALL_SPRING_Z (-165)
#define HALL_UP_X (-220) /* where it lands: on the gallery's back walk, clear of its pillar and the wall */
#define HALL_UP_Z (-205)
#define WALL_X (-110) /* jin_06: the peel-away wall (mekure), cracked here, in the back wall */
#define WALL_Z (-85)
#define MOAT_X0 (-30) /* jin_09: the goo moat, between where the way in's walk ends (x 90) and the spring */
#define MOAT_X1 66
#define MOAT_NEAR_X 95
#define MOAT_FAR_X (-65)
#define SPRING_X (-100) /* the spring up to the hall's landing, moved off the moat */
#define WINCH_X 300 /* jin_10: the winch, on the slope down from the door's alcove, east of the gate (164) */
#define WINCH_Z 5
#define FALL_X 420 /* where Luigi lands through jin_11's floor: the slope, east of the gate */
#define FALL_Z 0

/* --- the King's goo: c_sl_goo_pit tiles lying flat on the floor, churning --- */

/* in: x0, z0, x1, z1, y: the rectangle covered with goo (NPCs sl_goo<n>, gone with the room) */
static USER_FUNC(sl6_goo) {
    /* a tile every 96 units: the pit is 48 model units across, which an NPC draws about 1.52
     * world units each (measured against Luigi: at scale 2 the hall's pit spread 25 units past
     * its rectangle each side, past where its lava zone burns), so each tile is scaled to fill
     * exactly its 96 */
    static const f32 kTile = 96.0f;
    static const f32 kDrawn = 48.0f * 1.52f;
    EvtWord* a = event->args;
    const f32 x0 = (f32)evtGetValue(event, a[0]), z0 = (f32)evtGetValue(event, a[1]);
    const f32 x1 = (f32)evtGetValue(event, a[2]), z1 = (f32)evtGetValue(event, a[3]);
    const f32 y = (f32)evtGetValue(event, a[4]);
    f32 x, z;
    int n = 0;
    (void)isFirstCall;
    if (!animGroupBaseAsync("c_sl_goo_pit", 0, NULL)) {
        return EVT_RETURN_BLOCK;
    }
    /* whole tiles inside the rectangle (give it sides of 96s) */
    for (z = z0 + kTile / 2; z <= z1 - kTile / 2 + 1.0f; z += kTile) {
        for (x = x0 + kTile / 2; x <= x1 - kTile / 2 + 1.0f; x += kTile) {
            char name[16];
            NpcEntry* npc;
            snprintf(name, sizeof(name), "sl_goo%d", n++);
            if (npcNameToPtr_NoAssert(name) != NULL) {
                continue;
            }
            npcEntry(name, "c_sl_goo_pit");
            npc = npcNameToPtr(name);
            npc->position.x = x;
            npc->position.y = y;
            npc->position.z = z;
            npc->scale.x = npc->scale.y = npc->scale.z = kTile / kDrawn;
            npc->flags |= NPC_FLAG_NO_BILLBOARD | NPC_FLAG_INTANGIBLE | NPC_FLAG_NO_SHADOW | NPC_FLAG_NO_MARIO_SIDE_HIT |
                          NPC_FLAG_NO_FIELD_ATTACK;
            strcpy(npc->currentAnim, "S_1");
            animPoseSetAnim(npc->poseId, npc->currentAnim, 1);
        }
    }
    return EVT_RETURN_DONE;
}

/* --- Hayzee's Echo: a phrase sung back on the buttons --- */

static const u32 kPhrases[3][6] = {
    {PAD_BUTTON_A, PAD_BUTTON_B, PAD_BUTTON_A, PAD_BUTTON_Y, 0},
    {PAD_BUTTON_X, PAD_BUTTON_X, PAD_BUTTON_B, PAD_BUTTON_A, 0},
    {PAD_BUTTON_Y, PAD_BUTTON_A, PAD_BUTTON_X, PAD_BUTTON_B, PAD_BUTTON_B, 0},
};

/* each button's note, as Hayzee's Echo pitches them (superluigi_hayzee_aurora.c) */
static s16 notePitch(u32 button) {
    return button == PAD_BUTTON_B ? 300 : button == PAD_BUTTON_X ? 700 : button == PAD_BUTTON_Y ? 1000 : 0;
}

/* in: the phrase (0-2): the lit torch hums it, a note a beat (blocks while it plays) */
static USER_FUNC(sl6_echo_hum) {
    static int t;
    const int phrase = (int)evtGetValue(event, event->args[0]);
    if (isFirstCall) {
        t = 0;
    }
    if (t % 30 == 0 && kPhrases[phrase][t / 30] != 0) {
        psndSFX_pit(psndSFXOn("SFX_STAR_POINT_GET1"), notePitch(kPhrases[phrase][t / 30]));
    }
    t++;
    return kPhrases[phrase][t / 30] == 0 && t % 30 >= 10 ? EVT_RETURN_DONE : EVT_RETURN_BLOCK;
}

/* in: the phrase (0-2); out: 1 sung right, 0 a wrong note (blocks while it's sung) */
static USER_FUNC(sl6_echo) {
    static int sAt, sWait;
    static const char* const kTags[3] = {"sl_c6_echo_p0", "sl_c6_echo_p1", "sl_c6_echo_p2"};
    const int phrase = (int)evtGetValue(event, event->args[0]);
    u32 trg;
    if (isFirstCall) {
        sAt = 0;
        sWait = 20;
    }
    portSlPrompt(kTags[phrase], 200);
    if (sWait > 0) {
        sWait--;
        return EVT_RETURN_BLOCK;
    }
    trg = keyGetButtonTrg(0) & (PAD_BUTTON_A | PAD_BUTTON_B | PAD_BUTTON_X | PAD_BUTTON_Y);
    if (trg == 0) {
        return EVT_RETURN_BLOCK;
    }
    if (trg != kPhrases[phrase][sAt]) {
        portSlPrompt(NULL, 0);
        psndSFXOn("SFX_AC_FAIL1");
        evtSetValue(event, event->args[1], 0);
        return EVT_RETURN_DONE;
    }
    psndSFX_pit(psndSFXOn("SFX_AUDIENCE_PANSY_SINGING1"), notePitch(trg));
    sWait = 8;
    if (kPhrases[phrase][++sAt] == 0) {
        portSlPrompt(NULL, 0);
        evtSetValue(event, event->args[1], 1);
        return EVT_RETURN_DONE;
    }
    return EVT_RETURN_BLOCK;
}

/* ======================================================================
 * Shared scenes
 * ====================================================================== */

#define SAY(tag, who) CALL(evt_msg_print, 0, STRING(tag), 0, STRING(who))
#define NARRATE(tag) CALL(evt_msg_print, 0, STRING(tag), 0, 0)

/* door beros' swings (the borrowed rooms' own, by door object) */
#define DOOR(fn, obj)                                               \
    static EVT_BEGIN(fn##_c)                                        \
        MULTIPLY_FLOAT(LW(0), FLOAT(0.44433594f))                   \
        CALL(evt_mapobj_rotate, 0, STRING(obj), 0, LW(0), 0)        \
        RETURN                                                      \
    EVT_END()                                                       \
    static EVT_BEGIN(fn##_o)                                        \
        MULTIPLY_FLOAT(LW(0), FLOAT(-0.44433594f))                  \
        CALL(evt_mapobj_rotate, 0, STRING(obj), 0, LW(0), 0)        \
        RETURN                                                      \
    EVT_END()
#define DOOR_ENTRY(hit, fn, map, bero) \
    {hit, 1, 2, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, map, bero, 5, 5, fn##_c, fn##_o}
#define ROAD_ENTRY(hit, map, bero) \
    {hit, 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, map, bero, 1, 1, 0, 0}

#define FRIENDS_LEFT 7777

/* All five friends as NPCs beside Luigi for a scene (sl_bl Blooey, sl_je Jerry, sl_tq Torque,
 * sl_hz Hayzee, sl_sc Screamy), whoever the partner is: the partner steps out of the way for it
 * (sl6_party_stash) and back after (friends_out; friends_gone leaves him out, at the goodbyes) */
static EVT_BEGIN(friends_in)
    CALL(sl6_party_stash, 1)
    CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
    CALL(evt_npc_entry, STRING("sl_bl"), STRING("c_lp_gesso"))
    CALL(evt_npc_entry, STRING("sl_je"), STRING("c_lp_bom"))
    CALL(evt_npc_entry, STRING("sl_tq"), STRING("c_lp_met"))
    CALL(evt_npc_entry, STRING("sl_hz"), STRING("c_lp_pansy"))
    CALL(evt_npc_entry, STRING("sl_sc"), STRING("c_lp_moa"))
    /* in a line on the room's middle side of Luigi (toward x 0), facing him, a step behind;
     * LW(14) FRIENDS_LEFT: on his left whatever (the goodbyes: the King and the princess are
     * on his right) */
    SET_VALUE(LW(4), -40)
    SET_VALUE(LW(5), 90)
    IF_INT_LESS(LW(0), 0)
        IF_INT_NOT_EQUAL(LW(14), FRIENDS_LEFT)
            SET_VALUE(LW(4), 40)
            SET_VALUE(LW(5), 270)
        END_IF
    END_IF
    ADD(LW(2), -15)
    SET_VALUE(LW(3), LW(0))
    ADD(LW(3), LW(4))
    CALL(evt_npc_set_position, STRING("sl_bl"), LW(3), LW(1), LW(2))
    ADD(LW(3), LW(4))
    CALL(evt_npc_set_position, STRING("sl_je"), LW(3), LW(1), LW(2))
    ADD(LW(3), LW(4))
    CALL(evt_npc_set_position, STRING("sl_tq"), LW(3), LW(1), LW(2))
    ADD(LW(3), LW(4))
    CALL(evt_npc_set_position, STRING("sl_hz"), LW(3), LW(1), LW(2))
    ADD(LW(3), LW(4))
    ADD(LW(1), 20)
    CALL(evt_npc_set_position, STRING("sl_sc"), LW(3), LW(1), LW(2))
    CALL(evt_npc_set_ry, STRING("sl_bl"), LW(5))
    CALL(evt_npc_set_ry, STRING("sl_je"), LW(5))
    CALL(evt_npc_set_ry, STRING("sl_tq"), LW(5))
    CALL(evt_npc_set_ry, STRING("sl_hz"), LW(5))
    CALL(evt_npc_set_ry, STRING("sl_sc"), LW(5))
    CALL(evt_npc_set_anim, STRING("sl_bl"), STRING("S_1"))
    CALL(evt_npc_set_anim, STRING("sl_je"), STRING("S_1"))
    CALL(evt_npc_set_anim, STRING("sl_tq"), STRING("S_1"))
    CALL(evt_npc_set_anim, STRING("sl_hz"), STRING("S_1"))
    CALL(evt_npc_set_anim, STRING("sl_sc"), STRING("S_1"))
    RETURN
EVT_END()

static EVT_BEGIN(friends_gone)
    CALL(sl6_party_stash, 2)
    CALL(evt_npc_delete, STRING("sl_bl"))
    CALL(evt_npc_delete, STRING("sl_je"))
    CALL(evt_npc_delete, STRING("sl_tq"))
    CALL(evt_npc_delete, STRING("sl_hz"))
    CALL(evt_npc_delete, STRING("sl_sc"))
    RETURN
EVT_END()

static EVT_BEGIN(friends_out)
    CALL(evt_npc_delete, STRING("sl_bl"))
    CALL(evt_npc_delete, STRING("sl_je"))
    CALL(evt_npc_delete, STRING("sl_tq"))
    CALL(evt_npc_delete, STRING("sl_hz"))
    CALL(evt_npc_delete, STRING("sl_sc"))
    CALL(sl6_party_stash, 0)
    RETURN
EVT_END()

/* The puzzle's watch: in LW(10) x, LW(11) z, LW(12) radius, LW(13) the
 * friend, LW(14) the flag it sets, LW(15) Luigi's look at it (a tag, or 0).
 * The friend's own field ability solves it (its point runs the room's
 * script); walking up, Luigi says what he sees the first time, and, while
 * that friend isn't the one out, who could do it. LW(7): a height it's under (0: any). */
static EVT_BEGIN(puzzle_watch)
    SET_VALUE(LW(9), 0)
    LOOP(0)
        CALL(sl6_flag, LW(14), LW(0))
        IF_INT_EQUAL(LW(0), 1)
            BREAK_LOOP
        END_IF
        /* near it (LW(7): if not 0, and below that height: a room of two floors) */
        LOOP(0)
            CALL(sl6_wait_near, LW(10), LW(11), LW(12))
            IF_INT_EQUAL(LW(7), 0)
                BREAK_LOOP
            END_IF
            CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
            IF_INT_LESS_OR_EQUAL(LW(1), LW(7))
                BREAK_LOOP
            END_IF
            WAIT_FRAMES(30)
        END_LOOP
        CALL(sl6_flag, LW(14), LW(0))
        IF_INT_EQUAL(LW(0), 1)
            BREAK_LOOP
        END_IF
        CALL(sl6_friend_out, LW(13), LW(5), LW(6))
        IF_INT_EQUAL(LW(9), 0)
            IF_INT_NOT_EQUAL(LW(15), 0)
                CALL(evt_mario_key_onoff, 0)
                CALL(evt_msg_print, 0, LW(15), 0, STRING("mario"))
                CALL(evt_mario_key_onoff, 1)
            END_IF
            SET_VALUE(LW(9), 1)
        END_IF
        IF_INT_EQUAL(LW(5), 0)
            CALL(evt_mario_key_onoff, 0)
            CALL(evt_msg_print, 0, LW(6), 0, STRING("mario"))
            CALL(evt_mario_key_onoff, 1)
        END_IF
        /* once: wait to be well away before it can hint again */
        LOOP(0)
            WAIT_FRAMES(30)
            CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
            CALL(evt_sub_get_dist, LW(0), LW(2), LW(10), LW(11), LW(3))
            SET_VALUE(LW(4), LW(12))
            MULTIPLY(LW(4), 2)
            IF_INT_GREATER(LW(3), LW(4))
                BREAK_LOOP
            END_IF
        END_LOOP
    END_LOOP
    RETURN
EVT_END()

/* gives an item from where Luigi stands (LW(0) the item) */
static EVT_BEGIN(give_item)
    CALL(evt_mario_get_pos, 0, LW(1), LW(2), LW(3))
    ADD(LW(2), 30)
    CALL(evt_item_entry, STRING("_sl6_give"), LW(0), LW(1), LW(2), LW(3), ITEM_ENTRY_FIXED, -1, 0)
    CALL(evt_item_flag_on, STRING("_sl6_give"), ITEM_FLAG_NO_TOUCH)
    CALL(evt_item_get_item, STRING("_sl6_give"))
    RETURN
EVT_END()

/* a chest's prize: LW(0) the item, LW(1) the chest's mobj name, LW(2) 1 a badge */
static EVT_BEGIN(chest_give)
    CALL(evt_mario_key_onoff, 0)
    SET_VALUE(LW(5), LW(0))
    SET_VALUE(LW(6), LW(2))
    CALL(evt_mobj_get_position, LW(1), LW(0), LW(2), LW(3))
    CALL(evt_item_entry, STRING("_sl6_box"), LW(5), LW(0), LW(2), LW(3), ITEM_ENTRY_FIXED, -1, 0)
    IF_INT_EQUAL(LW(6), 1)
        CALL(evt_item_flag_on, STRING("_sl6_box"), ITEM_FLAG_BADGE_RETURN | ITEM_FLAG_NO_TOUCH)
    ELSE
        CALL(evt_item_flag_on, STRING("_sl6_box"), ITEM_FLAG_NO_TOUCH)
    END_IF
    CALL(evt_mobj_wait_animation_end, LW(1))
    CALL(evt_item_get_item, STRING("_sl6_box"))
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

#define CHEST(fn, mobj, item, badge)               \
    static EVT_BEGIN(fn)                           \
        SET_VALUE(LW(0), item)                     \
        SET_VALUE(LW(1), STRING(mobj))             \
        SET_VALUE(LW(2), badge)                    \
        CALL_CHILD(chest_give)                     \
        RETURN                                     \
    EVT_END()

/* Screamy's Look in a room with Boos: they can't fade from him (boosFrame) */
static EVT_BEGIN(boos_look)
    CALL(sl6_boos_seen)
    CALL(evt_sl_prompt, STRING("sl_c6_boos_seen"), 2000)
    RETURN
EVT_END()

/* every jin room's sound and the burnt Blooey from jin_70 on */
static EVT_BEGIN(jin_common)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG4_JIN1"))
    IF_INT_EQUAL(SLF(F_WAILER), 0)
        CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG4_JIN1"))
    ELSE
        CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG4_JIN2"))
    END_IF
    CALL(evt_snd_set_rev_mode, 2)
    CALL(sl6_soot, SLF(F_GOO))
    RETURN
EVT_END()

/* ======================================================================
 * gra_60, the Howling Cliff (gra_06)
 * ====================================================================== */

static EVT_BEGIN(gra_door_c)
    MULTIPLY_FLOAT(LW(0), FLOAT(0.38867188f))
    CALL(evt_mapobj_rotate, 0, STRING("door_L"), 0, LW(0), 0)
    MULTIPLY_FLOAT(LW(0), FLOAT(-1.0f))
    CALL(evt_mapobj_rotate, 0, STRING("door_R"), 0, LW(0), 0)
    RETURN
EVT_END()
static EVT_BEGIN(gra_door_o)
    MULTIPLY_FLOAT(LW(0), FLOAT(-0.38867188f))
    CALL(evt_mapobj_rotate, 0, STRING("door_L"), 0, LW(0), 0)
    MULTIPLY_FLOAT(LW(0), FLOAT(-1.0f))
    CALL(evt_mapobj_rotate, 0, STRING("door_R"), 0, LW(0), 0)
    RETURN
EVT_END()

/* the path south-west (Mario's road from gra_05) is the cliff they climbed: its sign, and a turn
 * back, never a walk off into a room that isn't there */
static EVT_BEGIN(gra_60_cliff_sign)
    CALL(evt_mario_key_onoff, 0)
    NARRATE("sl_c6_cliff_sign")
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(gra_60_cliff_back)
    CALL(evt_party_stop, 0)
    NARRATE("sl_c6_cliff_sign")
    SAY("sl_c6_cliff_back", "mario")
    CALL(evt_mario_mov_pos2, -260, 300, FLOAT(90.0f))
    CALL(evt_mario_set_dir, 90, 0, 0)
    CALL(evt_party_run, 0)
    SET_VALUE(LW(0), 1) /* no exit */
    RETURN
EVT_END()

static BeroEntry gra_60_bero[] = {
    {"n_bero", 1, 6, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, 120, NULL, -1, NULL, "jin_60", "w_bero", 5, 5, gra_door_c, gra_door_o},
    {"sw_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, gra_60_cliff_back, NULL, NULL, 1, 1, 0, 0},
    {0},
};

static EVT_BEGIN(gra_60_arrive)
    /* the card's curtain rises on the cliff, which goes black: the wind, the narrator, then the
     * cliff again (the card's own fade-in comes after the room's init, so wait for it first) */
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_seq_wait, 2)
    CALL(evt_fade_end_wait)
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG4_GRA1"))
    CALL(evt_fade_entry, FADE_OUT_BLACK_3D, 600, 0, 0, 0)
    CALL(evt_fade_end_wait)
    WAIT_MS(900)
    NARRATE("sl_c6_arrive_00")
    CALL(evt_mario_set_pos, -320, 0, 330)
    CALL(evt_fade_entry, FADE_IN_BLACK_3D, 1200, 0, 0, 0)
    CALL(evt_fade_end_wait)
    CALL(sl6_ranks)
    CALL(sl6_party_out, LW(0))
    IF_INT_EQUAL(LW(0), 0)
        CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
        SUBTRACT(LW(0), 40)
        CALL(evt_mario_set_party_pos, 0, 1, LW(0), LW(1), LW(2))
    END_IF
    CALL(evt_mario_mov_pos2, -220, 290, FLOAT(80.0f))
    CALL(evt_mario_set_dir, 90, 0, 0)
    CALL_CHILD(friends_in)
    SAY("sl_c6_arrive_01", "mario")
    SAY("sl_c6_arrive_02", "sl_tq")
    SAY("sl_c6_arrive_03", "sl_je")
    SAY("sl_c6_arrive_04", "sl_sc")
    SAY("sl_c6_arrive_05", "sl_hz")
    SAY("sl_c6_arrive_06", "sl_bl")
    /* the wind drops */
    CALL(evt_snd_envoff, PSND_FADE_OUT)
    WAIT_MS(1500)
    SAY("sl_c6_arrive_07", "mario")
    SAY("sl_c6_arrive_08", "mario")
    SAY("sl_c6_arrive_09", "sl_hz")
    SAY("sl_c6_arrive_10", "sl_je")
    SAY("sl_c6_arrive_11", "sl_bl")
    SAY("sl_c6_arrive_12", "sl_sc")
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG4_GRA1"))
    CALL_CHILD(friends_out)
    CALL(evt_sl_title_card)
    SET_VALUE(SLF(F_ARRIVED), 1)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* 13. Goodbyes, at dawn (from the loft) */
static EVT_BEGIN(gra_60_goodbye)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_STG2_TAIJU1"))
    CALL(evt_snd_envoff, PSND_FADE_OUT)
    /* dawn: the night cliff warmed and lifted (a map look adds light; the map blend could only
     * darken it, an orange over the night's blue gone brown), the sky's own backdrop unchanged */
    CALL(evt_sl_map_look, 0, 0xFFE8D8, 0x3A2412)
    CALL(evt_seq_wait, 2)
    CALL(evt_mario_set_pos, -100, 0, 280)
    CALL(evt_npc_entry, STRING("sl_eclair"), STRING("c_sl_eclair"))
    CALL(evt_npc_set_position, STRING("sl_eclair"), -25, 0, 262)
    CALL(evt_npc_set_ry, STRING("sl_eclair"), 270)
    CALL(evt_npc_entry, STRING("sl_marron"), STRING("c_sl_marron"))
    CALL(evt_npc_set_position, STRING("sl_marron"), 12, 0, 245)
    CALL(evt_npc_set_ry, STRING("sl_marron"), 270)
    CALL(evt_npc_set_anim, STRING("sl_marron"), STRING("S_1"))
    CALL(evt_mario_set_dir, 90, 0, 0)
    WAIT_MS(500)
    SET_VALUE(LW(14), FRIENDS_LEFT)
    CALL_CHILD(friends_in)
    SAY("sl_c6_goodbye_blooey", "sl_bl")
    SAY("sl_c6_goodbye_jerry", "sl_je")
    SAY("sl_c6_goodbye_torque", "sl_tq")
    SAY("sl_c6_goodbye_hayzee", "sl_hz")
    SAY("sl_c6_goodbye_screamy", "sl_sc")
    /* they go their own ways */
    CALL(evt_npc_set_anim, STRING("sl_je"), STRING("W_1"))
    CALL(evt_npc_set_anim, STRING("sl_tq"), STRING("W_1"))
    CALL(evt_npc_set_anim, STRING("sl_hz"), STRING("W_1"))
    CALL(evt_npc_set_anim, STRING("sl_sc"), STRING("W_1"))
    CALL(evt_npc_set_anim, STRING("sl_bl"), STRING("W_1"))
    CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
    CALL(evt_mario_set_dir, 270, 0, 0)
    SAY("sl_c6_goodbye_luigi", "mario")
    CALL(evt_npc_set_ry, STRING("sl_je"), 270)
    CALL(evt_npc_set_ry, STRING("sl_tq"), 270)
    CALL(evt_npc_set_ry, STRING("sl_hz"), 270)
    CALL(evt_npc_set_ry, STRING("sl_sc"), 270)
    CALL(evt_npc_set_ry, STRING("sl_bl"), 270)
    BEGIN_INLINE_EVENT
        CALL(evt_npc_move_position, STRING("sl_bl"), -480, 460, 2200, 0, 4)
    END_INLINE_EVENT
    BEGIN_INLINE_EVENT
        CALL(evt_npc_move_position, STRING("sl_je"), -500, 420, 2400, 0, 4)
    END_INLINE_EVENT
    BEGIN_INLINE_EVENT
        CALL(evt_npc_move_position, STRING("sl_tq"), -520, 380, 2600, 0, 4)
    END_INLINE_EVENT
    BEGIN_INLINE_EVENT
        CALL(evt_npc_move_position, STRING("sl_hz"), -540, 440, 2800, 0, 4)
    END_INLINE_EVENT
    CALL(evt_npc_set_anim, STRING("sl_marron"), STRING("WV_1"))
    CALL(evt_sl_npc_fly, STRING("sl_sc"), -560, 220, 300, 2600)
    CALL(evt_npc_set_anim, STRING("sl_marron"), STRING("S_1"))
    WAIT_MS(500)
    CALL_CHILD(friends_gone)
    CALL(evt_mario_set_dir, 90, 0, 0)
    SAY("sl_c6_goodbye_eclair", "sl_eclair")
    SAY("sl_c6_goodbye_luigi2", "mario")
    /* (the map change makes the fade itself: one of our own first flashes the room back) */
    CALL(evt_fade_set_mapchange_type, 0, FADE_OUT_BLACK, 1500, FADE_IN_BLACK, 300)
    CALL(evt_fade_set_mapchange_type, 1, FADE_OUT_BLACK, 1500, FADE_IN_BLACK, 300)
    CALL(evt_bero_mapchange, STRING("aaa_60"), 0)
    RETURN
EVT_END()

static EVT_BEGIN(gra_60_init)
    CALL(evt_snd_bgmoff, PSND_FADE_OUT)
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG4_GRA1"))
    SET_VALUE(LW(0), PTR(gra_60_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL(sl6_soot, SLF(F_GOO))
    /* the iron gate stands open, as it does after Mario's visit */
    CALL(evt_hit_bind_mapobj, STRING("A_mon_L"), STRING("mon_L"))
    CALL(evt_hit_bind_mapobj, STRING("A_mon_R"), STRING("mon_R"))
    CALL(evt_mapobj_rotate, 1, STRING("mon_L"), 0, 90, 0)
    CALL(evt_mapobj_rotate, 1, STRING("mon_R"), 0, -90, 0)
    CALL(evt_hit_bind_update, STRING("A_mon_L"))
    CALL(evt_hit_bind_update, STRING("A_mon_R"))
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("kusari"), MAPOBJ_FLAG_HIDE)
    CALL(evt_hitobj_onoff, STRING("a_saku01"), 1, 0)
    CALL(evt_hitobj_onoff, STRING("A_Mon"), 1, 0)
    CALL(evt_hitobj_onoff, STRING("ana"), 1, 0)
    CALL(evt_mobj_save_blk, STRING("mobj_save"), -125, 60, 235, 0, 0)
    CALL(evt_mobj_recovery_blk, STRING("mobj_recover"), 0, -270, 60, 270, 0, 0)
    /* the cliff's sign at the head of the way down (sw_bero) */
    CALL(evt_mobj_signboard, STRING("sl_cliff_sign"), -405, 0, 425, PTR(gra_60_cliff_sign), LSWF(0))
    /* the hidden block over the dead tree by the cliff edge: a Star Piece */
    CALL(evt_mobj_brick, STRING("mobj_hidden1"), 67, 90, -13, ITEM_STAR_PIECE, 11, 0, SLF(P_GRA_PIECE))
    IF_INT_EQUAL(SLF(F_CREPE_DOWN), 1)
        SPAWN_EVENT(gra_60_goodbye)
    ELSE
        IF_INT_EQUAL(SLF(F_ARRIVED), 0)
            SPAWN_EVENT(gra_60_arrive)
        END_IF
    END_IF
    RETURN
EVT_END()

/* ======================================================================
 * jin_60, the Dark Hall (jin_00)
 * ====================================================================== */

static EVT_BEGIN(hall_w_c)
    MULTIPLY_FLOAT(LW(0), FLOAT(0.38867188f))
    CALL(evt_mapobj_rotate, 0, STRING("door01L"), 0, LW(0), 0)
    MULTIPLY_FLOAT(LW(0), FLOAT(-1.0f))
    CALL(evt_mapobj_rotate, 0, STRING("door01R"), 0, LW(0), 0)
    RETURN
EVT_END()
static EVT_BEGIN(hall_w_o)
    MULTIPLY_FLOAT(LW(0), FLOAT(-0.38867188f))
    CALL(evt_mapobj_rotate, 0, STRING("door01L"), 0, LW(0), 0)
    MULTIPLY_FLOAT(LW(0), FLOAT(-1.0f))
    CALL(evt_mapobj_rotate, 0, STRING("door01R"), 0, LW(0), 0)
    RETURN
EVT_END()
DOOR(hall_n3, "door03")
DOOR(hall_s, "door07")
DOOR(hall_n2, "door02")
DOOR(hall_e1, "door04")
DOOR(hall_e2, "door05")
DOOR(hall_e3, "door06")

static BeroEntry jin_60_bero[] = {
    {"w_bero", 1, 6, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, 120, NULL, -1, NULL, "gra_60", "n_bero", 5, 5, hall_w_c, hall_w_o},
    DOOR_ENTRY("n_bero_3", hall_n3, "jin_61", "s_bero_1"),
    DOOR_ENTRY("s_bero", hall_s, "jin_62", "n_bero_1"),
    DOOR_ENTRY("e_bero_1", hall_e1, "jin_63", "w_bero1"),
    DOOR_ENTRY("e_bero_2", hall_e2, NULL, NULL),
    DOOR_ENTRY("e_bero_3", hall_e3, "jin_70", "e_bero"),
    DOOR_ENTRY("n_bero_2", hall_n2, "jin_71", "s_bero"),
    ROAD_ENTRY("n_bero_1", "jin_64", "s_bero"),
    {"ana", 0xF00, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, NULL, NULL, 4, 4, 0, 0},
    {0},
};

static s32 kKeyList[] = {ITEM_STEEPLE_KEY_0x16, -1};

static EVT_BEGIN(crown_lock_check)
    CALL(evt_pouch_check_item, ITEM_STEEPLE_KEY_0x16, LW(0))
    IF_INT_GREATER(LW(0), 0)
        CALL(evt_win_item_select, 0, PTR(kKeyList), LW(0), 0)
        IF_INT_EQUAL(LW(0), -1)
            CALL(evt_mobj_exec_cancel, STRING("me"))
        END_IF
    ELSE
        NARRATE("sl_c6_locked")
    END_IF
    RETURN
EVT_END()

static EVT_BEGIN(crown_unlock)
    SET_VALUE(SLF(F_UNLOCK), 1)
    SET_VALUE(LW(0), STRING("n_bero_2"))
    CALL_CHILD(bero_case_switch_on)
    RETURN
EVT_END()

/* 2. The ambush */
static EVT_BEGIN(jin_60_ambush)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_snd_bgmoff, PSND_FADE_OUT)
    CALL(evt_seq_wait, 2)
    CALL(evt_snd_sfxon, STRING("SFX_DOOR_SHUT_CASTLE1"), 0)
    CALL(evt_cam_shake, 4, FLOAT(0.0f), FLOAT(1.5f), 300)
    WAIT_MS(600)
    CALL(evt_mario_mov_pos2, -520, 0, FLOAT(60.0f))
    SAY("sl_c6_ambush_00", "mario")
    /* the camera back off Luigi: he and the dark ahead of him, where the King steps out */
    CALL(evt_cam3d_evt_set, -440, 150, 520, -440, 55, -40, 900, 11)
    /* something breathes in the dark: two small eyes high up */
    CALL(evt_snd_sfxon, STRING("SFX_BOSS_GNB_BREATH_CHANGE1"), 0)
    CALL(evt_eff64, STRING("sl_eye1"), STRING("akari_charge_n64"), 0, -320, 160, -60, FLOAT(0.4f), 60, 0, 0, 0, 0, 0, 0)
    CALL(evt_eff64, STRING("sl_eye2"), STRING("akari_charge_n64"), 0, -290, 160, -60, FLOAT(0.4f), 60, 0, 0, 0, 0, 0, 0)
    WAIT_MS(1200)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_DANGER4"))
    CALL(evt_npc_entry, STRING("sl_king"), STRING("c_sl_chestnut_king"))
    CALL(evt_npc_set_position, STRING("sl_king"), -250, 0, -60)
    CALL(evt_npc_set_ry, STRING("sl_king"), 270)
    CALL(evt_npc_set_anim, STRING("sl_king"), STRING("W_1"))
    CALL(evt_npc_move_position, STRING("sl_king"), -400, -20, 1800, 0, 4)
    CALL(evt_npc_set_anim, STRING("sl_king"), STRING("S_1"))
    CALL(evt_snd_sfxon, STRING("SFX_BTL_GNB_STEP1"), 0)
    CALL(evt_cam_shake, 4, FLOAT(0.0f), FLOAT(2.0f), 500)
    /* a drop of goo hits the floor, and hisses */
    CALL(evt_eff, STRING(""), STRING("kemuri_test"), 13, -420, 0, -10, FLOAT(1.0f), 0, 0, 0, 0, 0, 0, 0)
    CALL(evt_snd_sfxon, STRING("SFX_BTL_DAMAGE_FIRE1"), 0)
    SAY("sl_c6_ambush_01", "sl_king")
    SAY("sl_c6_ambush_02", "mario")
    SAY("sl_c6_ambush_03", "sl_king")
    /* he roars; the goo eats the middle of the hall (the camera back to take it in) */
    CALL(evt_cam3d_evt_set, -300, 230, 640, -300, 40, -30, 0, 11)
    CALL(evt_npc_set_anim, STRING("sl_king"), STRING("R_2"))
    CALL(evt_snd_sfxon, STRING("SFX_BTL_GNB_ROAR1"), 0)
    CALL(evt_cam_shake, 4, FLOAT(0.0f), FLOAT(4.0f), 1200)
    CALL(sl6_goo, -240, -144, 240, 144, 1)
    CALL(evt_sl_lava_zone, -236, -140, 236, 140, 20, -400, 0, 0)
    LOOP(5)
        CALL(evt_sub_random, 400, LW(0))
        SUBTRACT(LW(0), 200)
        CALL(evt_sub_random, 160, LW(2))
        SUBTRACT(LW(2), 80)
        CALL(evt_eff, STRING(""), STRING("kemuri_test"), 7, LW(0), 0, LW(2), FLOAT(2.0f), 0, 0, 0, 0, 0, 0, 0)
        WAIT_MS(150)
    END_LOOP
    WAIT_MS(600)
    CALL(evt_npc_set_anim, STRING("sl_king"), STRING("W_1"))
    CALL(evt_npc_reverse_ry, STRING("sl_king"))
    CALL(evt_sl_npc_fly, STRING("sl_king"), 200, 400, -200, 2000)
    CALL(evt_npc_delete, STRING("sl_king"))
    CALL(evt_eff_delete, STRING("sl_eye1"))
    CALL(evt_eff_delete, STRING("sl_eye2"))
    CALL(evt_cam3d_evt_off, 700, 11)
    CALL_CHILD(friends_in)
    SAY("sl_c6_ambush_04", "sl_je")
    SAY("sl_c6_ambush_05", "sl_sc")
    SAY("sl_c6_ambush_06", "mario")
    CALL_CHILD(friends_out)
    SET_VALUE(SLF(F_AMBUSH), 1)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG4_JIN1"))
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* The way up: the hall's doors on, the stair door's and the Crown Door's, are on the gallery
 * (y 200) round it, which the ground floor doesn't reach (Mario's own way up was a spring in
 * another room). Both chapels' switches raise a spring out of the floor, the steeple's own. */
static EVT_BEGIN(jin_60_spring)
    CALL(evt_mario_jump_jumpstand, 0, 0, 0, HALL_UP_X, 270, HALL_UP_Z, HALL_UP_X, 270, HALL_UP_Z, 1200, 1)
    RETURN
EVT_END()


/* 5. Before the Crown Door: the last check */
static EVT_BEGIN(jin_60_ready)
    CALL(evt_mario_key_onoff, 0)
    CALL_CHILD(friends_in)
    SAY("sl_c6_crown_door_00", "mario")
    SAY("sl_c6_crown_door_01", "sl_sc")
    SAY("sl_c6_crown_door_02", "sl_je")
    SAY("sl_c6_crown_door_03", "sl_tq")
    SAY("sl_c6_crown_door_04", "sl_hz")
    SAY("sl_c6_crown_door_05", "sl_bl")
    CALL_CHILD(friends_out)
    SET_VALUE(SLF(F_READY), 1)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(jin_60_ready_watch)
    CALL(sl6_wait_above, 150)
    CALL(sl6_wait_near, -470, -230, 120)
    CALL_CHILD(jin_60_ready)
    RETURN
EVT_END()

/* Screamy's Look: the Vault's door, hidden in the wall */
static EVT_BEGIN(jin_60_look)
    CALL(evt_mario_key_onoff, 0)
    SAY("sl_c6_look_64", "party")
    CALL(evt_snd_sfxon, STRING("SFX_STAR_POINT_GET1"), 0)
    SET_VALUE(SLF(F_VAULT), 1)
    SET_VALUE(LW(0), STRING("n_bero_1"))
    CALL_CHILD(bero_case_switch_on)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(jin_60_init)
    CALL_CHILD(jin_common)
    SET_VALUE(LW(0), PTR(jin_60_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL(evt_map_playanim, STRING("hikari"), 1, 0)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 0, STRING("hahen"), MAPOBJ_FLAG_BBOX_CULL)
    CALL(evt_hit_bind_mapobj, STRING("a_oki01"), STRING("oki01"))
    CALL(evt_mobj_save_blk, STRING("mobj_save"), -560, 60, 60, 0, 0)
    /* a Heart Block in the open just inside the doorway (behind it the near pillar hides it): with field fights on, the chapels' foes and the
     * friends' moves had FP at 1/25 by the East Chapel, and the next refill was up on the gallery */
    CALL(evt_mobj_recovery_blk, STRING("mobj_heart"), 0, -450, 60, 0, 0, 0)
    /* by the Crown Door, on the landing: the last Save Block and Heart Block */
    CALL(evt_mobj_save_blk, STRING("mobj_save2"), -500, 260, -200, 0, 0)
    CALL(evt_mobj_recovery_blk, STRING("mobj_heart2"), 0, -380, 260, -200, 0, 0)
    IF_INT_EQUAL(SLF(F_AMBUSH), 1)
        CALL(sl6_goo, -240, -144, 240, 144, 1)
        /* the goo pit keeps Luigi out of the middle */
        CALL(evt_sl_lava_zone, -236, -140, 236, 140, 20, -400, 0, 0)
        CALL(evt_sl_enemy, STRING("sl_e1"), STRING(SJ6_BOO), STRING(SJ6_BOO), -330, 30, 190, 90, 40, STRING("BTLNO_JIN_00_02"))
        CALL(evt_sl_enemy, STRING("sl_e2"), STRING(SJ6_BOO), STRING(SJ6_BOO), -150, 30, -200, 90, 40, STRING("BTLNO_JIN_00_02"))
        CALL(evt_sl_point, PORT_SL_ABILITY_LOOK, 7, 0, 0, 0, 3000, 0, PTR(boos_look))
    END_IF
    /* the way up: the spring, once both chapels' switches are down */
    IF_INT_EQUAL(SLF(F_SWITCH_W), 1)
        IF_INT_EQUAL(SLF(F_SWITCH_E), 1)
            CALL(evt_mobj_jumpstand_red, 0, STRING("mobj_up"), HALL_SPRING_X, 0, HALL_SPRING_Z, PTR(jin_60_spring), 0)
        END_IF
    END_IF
    /* the Crown Door */
    IF_INT_EQUAL(SLF(F_UNLOCK), 0)
        CALL(evt_mobj_lock, STRING("mobj_lock00"), ITEM_STEEPLE_KEY_0x16, -435, 210, -245, 0, PTR(crown_lock_check),
             PTR(crown_unlock), SLF(F_UNLOCK))
        SET_VALUE(LW(0), STRING("n_bero_2"))
        CALL_CHILD(bero_case_switch_off)
    END_IF
    IF_INT_EQUAL(SLF(F_READY), 0)
        IF_INT_EQUAL(SLF(F_GOO), 1)
            SPAWN_EVENT(jin_60_ready_watch)
        END_IF
    END_IF
    /* the Vault: its door hidden in the wall until Screamy looks */
    IF_INT_EQUAL(SLF(F_VAULT), 0)
        SET_VALUE(LW(0), STRING("n_bero_1"))
        CALL_CHILD(bero_case_switch_off)
        CALL(evt_sl_point, PORT_SL_ABILITY_LOOK, 0, VAULT_X, 0, VAULT_Z, 130, 0, PTR(jin_60_look))
        SET_VALUE(LW(10), VAULT_X)
        SET_VALUE(LW(11), VAULT_Z)
        SET_VALUE(LW(12), 70)
        SET_VALUE(LW(13), PORT_SL_FRIEND_SCREAMY)
        SET_RAW_VALUE(LW(14), SLF(F_VAULT))
        SET_VALUE(LW(15), STRING("sl_c6_wall_64"))
        SET_VALUE(LW(7), 100)
        SPAWN_EVENT(puzzle_watch)
    END_IF
    IF_INT_EQUAL(SLF(F_AMBUSH), 0)
        SPAWN_EVENT(jin_60_ambush)
    END_IF
    RETURN
EVT_END()

/* ======================================================================
 * jin_61, West Chapel (jin_01): Torque frees the rusted gate
 * ====================================================================== */

DOOR(c61_s1, "door00")
DOOR(c61_s2, "door03")
static BeroEntry jin_61_bero[] = {
    DOOR_ENTRY("s_bero_1", c61_s1, "jin_60", "n_bero_3"),
    DOOR_ENTRY("s_bero_2", c61_s2, NULL, NULL),
    {0},
};

/* the switch lifts one end of the stair door's bar (the borrowed room's quake) */
static EVT_BEGIN(bar_shake)
    CALL(evt_snd_sfxon_3d, STRING("SFX_STG4_KAIDAN_MOVE1"), 300, 100, 200)
    CALL(evt_sub_intpl_init, 11, 0, 10, 30)
    LOOP(30)
        CALL(evt_sub_intpl_get_value)
        DIVIDE_FLOAT(LW(0), FLOAT(1000.0f))
        CALL(evt_cam_shake, 4, LW(0), FLOAT(0.0f), 100)
        WAIT_FRAMES(1)
    END_LOOP
    CALL(evt_cam_shake, 4, FLOAT(2.0f), FLOAT(0.0f), 400)
    CALL(evt_snd_sfxoff, LW(0))
    RETURN
EVT_END()

static EVT_BEGIN(switch_after)
    CALL(evt_mario_key_onoff, 0)
    CALL_CHILD(bar_shake)
    IF_INT_EQUAL(SLF(F_SWITCH_W), 1)
        IF_INT_EQUAL(SLF(F_SWITCH_E), 1)
            NARRATE("sl_c6_bar_open")
        ELSE
            NARRATE("sl_c6_bar_up")
        END_IF
    ELSE
        NARRATE("sl_c6_bar_up")
    END_IF
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(jin_61_switch)
    /* the switch is the game's reusable one: only the first press does anything */
    IF_INT_EQUAL(SLF(F_SWITCH_W), 1)
        RETURN
    END_IF
    SET_VALUE(SLF(F_SWITCH_W), 1)
    CALL_CHILD(switch_after)
    RETURN
EVT_END()

static EVT_BEGIN(jin_61_tune)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_snd_sfxon, STRING("SFX_STG4_GRID_MOVE1"), 0)
    WAIT_MS(500)
    SAY("sl_c6_tune_61", "party")
    SET_VALUE(SLF(F_RUST), 1)
    CALL(evt_mobj_switch_red, 0, STRING("mobj_switch1"), 485, 30, 10, PTR(jin_61_switch), SLF(F_SWITCH_W))
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

CHEST(jin_61_box, "mobj_box1", ITEM_JAMMIN_JELLY, 0)

static EVT_BEGIN(jin_61_init)
    CALL_CHILD(jin_common)
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG4_JIN2"))
    SET_VALUE(LW(0), PTR(jin_61_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL(evt_sl_enemy, STRING("sl_e1"), STRING(SJ6_SWOOPER), STRING(SJ6_SWOOPER_AI), 150, 120, 0, 80, 40, STRING("BTLNO_JIN_01_03"))
    CALL(evt_sl_enemy, STRING("sl_e2"), STRING(SJ6_SWOOPER), STRING(SJ6_SWOOPER_AI), -100, 120, 30, 80, 40, STRING("BTLNO_JIN_01_03"))
    CALL(evt_mobj_itembox, STRING("mobj_box1"), 250, 0, 0, 0, 0, PTR(jin_61_box), SLF(P_61_JELLY))
    /* behind the switch's dais, a hidden block: HP Plus */
    CALL(evt_mobj_brick, STRING("mobj_hidden"), 390, 75, 10, ITEM_HP_PLUS, 11, 0, SLF(P_61_HPPLUS))
    IF_INT_EQUAL(SLF(F_RUST), 1)
        CALL(evt_mobj_switch_red, 0, STRING("mobj_switch1"), 485, 30, 10, PTR(jin_61_switch), SLF(F_SWITCH_W))
    ELSE
        CALL(evt_sl_point, PORT_SL_ABILITY_TUNE, 0, 460, 0, 10, 90, 0, PTR(jin_61_tune))
        SET_VALUE(LW(10), 460)
        SET_VALUE(LW(11), 10)
        SET_VALUE(LW(12), 80)
        SET_VALUE(LW(13), PORT_SL_FRIEND_TORQUE)
        SET_RAW_VALUE(LW(14), SLF(F_RUST))
        SET_VALUE(LW(15), STRING("sl_c6_rust_61"))
        SET_VALUE(LW(7), 0)
        SPAWN_EVENT(puzzle_watch)
    END_IF
    RETURN
EVT_END()

/* ======================================================================
 * jin_62, East Chapel (jin_02): Hayzee sets the Boos dancing
 * ====================================================================== */

DOOR(c62_n1, "door00")
DOOR(c62_n2, "door03")
static BeroEntry jin_62_bero[] = {
    DOOR_ENTRY("n_bero_1", c62_n1, "jin_60", "s_bero"),
    DOOR_ENTRY("n_bero_2", c62_n2, NULL, NULL),
    {0},
};

static EVT_BEGIN(jin_62_switch)
    /* the switch is the game's reusable one: only the first press does anything */
    IF_INT_EQUAL(SLF(F_SWITCH_E), 1)
        RETURN
    END_IF
    SET_VALUE(SLF(F_SWITCH_E), 1)
    CALL_CHILD(switch_after)
    RETURN
EVT_END()

CHEST(jin_62_box, "mobj_box1", ITEM_FIRE_DRIVE, 1)

static EVT_BEGIN(jin_62_boos_place)
    SET_VALUE(LW(0), 0)
    LOOP(6)
        /* a slanting line across the chapel's strip of floor, in front of the switch */
        SET_VALUE(LW(1), LW(0))
        MULTIPLY(LW(1), 10)
        ADD(LW(1), 405)
        SET_VALUE(LW(2), LW(0))
        MULTIPLY(LW(2), 16)
        SUBTRACT(LW(2), 40)
        SET_VALUE(LW(3), LW(0))
        MODULO(LW(3), 2)
        MULTIPLY(LW(3), 25)
        ADD(LW(3), 20)
        SWITCH(LW(0))
            CASE_EQUAL(0)
                CALL(evt_npc_entry, STRING("sl_boo0"), STRING("c_teresa"))
                CALL(evt_npc_set_position, STRING("sl_boo0"), LW(1), LW(3), LW(2))
            CASE_EQUAL(1)
                CALL(evt_npc_entry, STRING("sl_boo1"), STRING("c_teresa"))
                CALL(evt_npc_set_position, STRING("sl_boo1"), LW(1), LW(3), LW(2))
            CASE_EQUAL(2)
                CALL(evt_npc_entry, STRING("sl_boo2"), STRING("c_teresa"))
                CALL(evt_npc_set_position, STRING("sl_boo2"), LW(1), LW(3), LW(2))
            CASE_EQUAL(3)
                CALL(evt_npc_entry, STRING("sl_boo3"), STRING("c_teresa"))
                CALL(evt_npc_set_position, STRING("sl_boo3"), LW(1), LW(3), LW(2))
            CASE_EQUAL(4)
                CALL(evt_npc_entry, STRING("sl_boo4"), STRING("c_teresa"))
                CALL(evt_npc_set_position, STRING("sl_boo4"), LW(1), LW(3), LW(2))
            CASE_DEFAULT
                CALL(evt_npc_entry, STRING("sl_boo5"), STRING("c_teresa"))
                CALL(evt_npc_set_position, STRING("sl_boo5"), LW(1), LW(3), LW(2))
        END_SWITCH
        ADD(LW(0), 1)
    END_LOOP
    RETURN
EVT_END()

static EVT_BEGIN(jin_62_sing)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_party_get_pos, 0, LW(0), LW(1), LW(2))
    ADD(LW(1), 40)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_PEACH_WALTZ1"))
    LOOP(4)
        CALL(evt_eff64, STRING(""), STRING("onpu_n64"), 1, LW(0), LW(1), LW(2), 0, 0, 0, 0, 0, 0, 0, 0)
        WAIT_MS(400)
    END_LOOP
    /* the Boos twirl away up into the rafters */
    BEGIN_INLINE_EVENT
        CALL(evt_sl_npc_fly, STRING("sl_boo0"), 300, 400, -100, 2200)
    END_INLINE_EVENT
    BEGIN_INLINE_EVENT
        CALL(evt_sl_npc_fly, STRING("sl_boo1"), 360, 420, 60, 2300)
    END_INLINE_EVENT
    BEGIN_INLINE_EVENT
        CALL(evt_sl_npc_fly, STRING("sl_boo2"), 440, 450, -60, 2400)
    END_INLINE_EVENT
    BEGIN_INLINE_EVENT
        CALL(evt_sl_npc_fly, STRING("sl_boo3"), 520, 410, 40, 2500)
    END_INLINE_EVENT
    BEGIN_INLINE_EVENT
        CALL(evt_sl_npc_fly, STRING("sl_boo4"), 600, 430, -40, 2600)
    END_INLINE_EVENT
    LOOP(16)
        CALL(evt_npc_add_rotate, STRING("sl_boo0"), 0, 45, 0)
        CALL(evt_npc_add_rotate, STRING("sl_boo1"), 0, 45, 0)
        CALL(evt_npc_add_rotate, STRING("sl_boo2"), 0, 45, 0)
        CALL(evt_npc_add_rotate, STRING("sl_boo3"), 0, 45, 0)
        CALL(evt_npc_add_rotate, STRING("sl_boo4"), 0, 45, 0)
        CALL(evt_npc_add_rotate, STRING("sl_boo5"), 0, 45, 0)
        WAIT_FRAMES(8)
    END_LOOP
    CALL(evt_sl_npc_fly, STRING("sl_boo5"), 680, 440, 0, 1200)
    WAIT_MS(400)
    CALL(evt_npc_delete, STRING("sl_boo0"))
    CALL(evt_npc_delete, STRING("sl_boo1"))
    CALL(evt_npc_delete, STRING("sl_boo2"))
    CALL(evt_npc_delete, STRING("sl_boo3"))
    CALL(evt_npc_delete, STRING("sl_boo4"))
    CALL(evt_npc_delete, STRING("sl_boo5"))
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG4_JIN1"))
    SAY("sl_c6_sing_62", "party")
    SET_VALUE(SLF(F_BOOS), 1)
    CALL(evt_mobj_switch_red, 0, STRING("mobj_switch1"), 485, 30, -10, PTR(jin_62_switch), SLF(F_SWITCH_E))
    CALL(evt_mobj_itembox, STRING("mobj_box1"), 380, 0, 5, 0, 0, PTR(jin_62_box), SLF(P_62_FIREDRIVE))
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(jin_62_init)
    CALL_CHILD(jin_common)
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG4_JIN2"))
    SET_VALUE(LW(0), PTR(jin_62_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL(evt_sl_enemy, STRING("sl_e1"), STRING(SJ6_BOO), STRING(SJ6_BOO), 100, 30, 0, 120, 60, STRING("BTLNO_JIN_00_02"))
    CALL(evt_sl_enemy, STRING("sl_e2"), STRING(SJ6_BUZZY), STRING(SJ6_BUZZY), -150, 0, 40, 100, 40, STRING("BTLNO_JIN_00_02"))
    CALL(evt_sl_point, PORT_SL_ABILITY_LOOK, 7, 0, 0, 0, 3000, 0, PTR(boos_look))
    CALL(evt_mobj_badgeblk, STRING("mobj_coin1"), 0, 60, 0, ITEM_COIN, 0, SLF(P_62_COIN1), 0)
    CALL(evt_mobj_badgeblk, STRING("mobj_coin2"), 250, 60, 0, ITEM_COIN, 0, SLF(P_62_COIN2), 0)
    IF_INT_EQUAL(SLF(F_BOOS), 1)
        CALL(evt_mobj_switch_red, 0, STRING("mobj_switch1"), 485, 30, -10, PTR(jin_62_switch), SLF(F_SWITCH_E))
        CALL(evt_mobj_itembox, STRING("mobj_box1"), 380, 0, 5, 0, 0, PTR(jin_62_box), SLF(P_62_FIREDRIVE))
    ELSE
        CALL_CHILD(jin_62_boos_place)
        /* Shake a Leaf (song 1) sets them dancing */
        CALL(evt_sl_point, PORT_SL_ABILITY_SING, 0, 330, 0, -10, 140, 1, PTR(jin_62_sing))
        SET_VALUE(LW(10), 330)
        SET_VALUE(LW(11), -10)
        SET_VALUE(LW(12), 80)
        SET_VALUE(LW(13), PORT_SL_FRIEND_HAYZEE)
        SET_RAW_VALUE(LW(14), SLF(F_BOOS))
        SET_VALUE(LW(15), STRING("sl_c6_boos_62"))
        SET_VALUE(LW(7), 0)
        SPAWN_EVENT(puzzle_watch)
    END_IF
    RETURN
EVT_END()

/* ======================================================================
 * jin_63, the Grand Stair (jin_03): the tile, and the Wailer
 * ====================================================================== */

DOOR(c63_w1, "door00C")
DOOR(c63_w2, "door00B")
DOOR(c63_w3, "door00A")
DOOR(c63_n, "door01")
DOOR(c63_s, "door02")
static BeroEntry jin_63_bero[] = {
    DOOR_ENTRY("w_bero1", c63_w1, "jin_60", "e_bero_1"),
    DOOR_ENTRY("w_bero2", c63_w2, NULL, NULL),
    DOOR_ENTRY("w_bero3", c63_w3, NULL, NULL),
    DOOR_ENTRY("n_bero", c63_n, "jin_65", "w_bero"),
    DOOR_ENTRY("s_bero", c63_s, NULL, NULL),
    {0},
};

static EVT_BEGIN(jin_63_wailer)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_STG5_GHOST1"))
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG4_JIN1"))
    CALL(evt_npc_entry, STRING("sl_wailer"), STRING("c_atmic_trs"))
    CALL(evt_npc_set_color, STRING("sl_wailer"), 150, 175, 220, 255)
    CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
    ADD(LW(0), 300)
    ADD(LW(1), 300)
    CALL(evt_npc_set_position, STRING("sl_wailer"), LW(0), LW(1), LW(2))
    CALL(evt_npc_set_ry, STRING("sl_wailer"), 270)
    SUBTRACT(LW(0), 160)
    SUBTRACT(LW(1), 270)
    CALL(evt_sl_npc_fly, STRING("sl_wailer"), LW(0), LW(1), LW(2), 2000)
    CALL(evt_cam_shake, 4, FLOAT(0.0f), FLOAT(2.0f), 800)
    CALL(evt_mario_set_dir_npc, STRING("sl_wailer"))
    CALL_CHILD(friends_in)
    SAY("sl_c6_wailer_00", "sl_wailer")
    SAY("sl_c6_wailer_01", "sl_hz")
    SAY("sl_c6_wailer_02", "sl_wailer")
    SAY("sl_c6_wailer_03", "mario")
    SAY("sl_c6_wailer_04", "sl_wailer")
    CALL_CHILD(friends_out)
    /* the mid-boss: the area's Atomic Boo battle, as ours (superluigi_ch06_boss_aurora.c) */
    CALL(evt_sl6_boss_arm, 1)
    CALL(evt_npc_set_battle_info, STRING("sl_wailer"), 7)
    CALL(evt_npc_battle_start, STRING("sl_wailer"))
    CALL(evt_npc_wait_battle_end)
    CALL(evt_sl6_boss_arm, 0)
    CALL(evt_npc_get_battle_result, LW(0))
    CALL(sl6_log, STRING("the Wailer's fight:"), LW(0))
    IF_INT_NOT_EQUAL(LW(0), 1)
        CALL(evt_npc_delete, STRING("sl_wailer"))
        CALL(evt_mario_key_onoff, 1)
        RETURN
    END_IF
    /* (the keys are still off from before the fight: evt_mario_key_onoff counts) */
    CALL(evt_npc_set_scale, STRING("sl_wailer"), FLOAT(0.4f), FLOAT(0.4f), FLOAT(0.4f))
    CALL(evt_sl6_boss_used, LW(0))
    IF_INT_EQUAL(LW(0), 1)
        SAY("sl_c6_wailer_05", "sl_wailer")
    ELSE
        SAY("sl_c6_wailer_06", "sl_wailer")
    END_IF
    CALL(evt_npc_get_position, STRING("sl_wailer"), LW(0), LW(1), LW(2))
    ADD(LW(1), 300)
    ADD(LW(0), 200)
    CALL(evt_sl_npc_fly, STRING("sl_wailer"), LW(0), LW(1), -200, 2500)
    CALL(evt_npc_delete, STRING("sl_wailer"))
    SET_VALUE(SLF(F_WAILER), 1)
    /* the wind outside softens */
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG4_JIN2"))
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG4_JIN1"))
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(jin_63_wailer_watch)
    CALL(evt_seq_wait, 2)
    WAIT_MS(600)
    CALL(sl6_wait_near, 0, 0, 260)
    CALL_CHILD(jin_63_wailer)
    RETURN
EVT_END()

static EVT_BEGIN(jin_63_init)
    CALL_CHILD(jin_common)
    SET_VALUE(LW(0), PTR(jin_63_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    /* the stair as the borrowed room starts it (only the middle flight) */
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("kaidan01"), MAPOBJ_FLAG_HIDE)
    CALL(evt_hitobj_onoff, STRING("a_kaidan01"), 1, 0)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("kaidan03"), MAPOBJ_FLAG_HIDE)
    CALL(evt_hitobj_onoff, STRING("a_kaidan03"), 1, 0)
    CALL(evt_mobj_kururing_floor, STRING("kururin1"), 125, 0, 0, STRING("S_yuka"), 125, SLF(P_63_TILE))
    CALL(evt_item_entry, STRING("item_piece"), ITEM_STAR_PIECE, 125, 0, -150, ITEM_ENTRY_FIXED, SLF(P_63_PIECE), 0)
    IF_INT_EQUAL(SLF(F_WAILER), 0)
        SPAWN_EVENT(jin_63_wailer_watch)
    END_IF
    RETURN
EVT_END()

/* ======================================================================
 * jin_64, the Vault (jin_05)
 * ====================================================================== */

static BeroEntry jin_64_bero[] = {
    ROAD_ENTRY("s_bero", "jin_60", "n_bero_1"),
    {0},
};
CHEST(jin_64_box0, "mobj_box00", ITEM_POWER_PLUS, 1)
CHEST(jin_64_box1, "mobj_box01", ITEM_ULTRA_SHROOM, 0)
CHEST(jin_64_box2, "mobj_box02", ITEM_STAR_PIECE, 0)

static EVT_BEGIN(jin_64_init)
    CALL_CHILD(jin_common)
    SET_VALUE(LW(0), PTR(jin_64_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL(evt_mobj_powerupblk, STRING("pblk00"), 65, 60, 35, 0, SLF(P_64_SHINE), 0)
    CALL(evt_mobj_itembox, STRING("mobj_box00"), 125, 0, 75, 0, 0, PTR(jin_64_box0), SLF(P_64_POWER))
    CALL(evt_mobj_itembox, STRING("mobj_box01"), -55, 0, 55, 0, 0, PTR(jin_64_box1), SLF(P_64_ULTRA))
    CALL(evt_mobj_itembox, STRING("mobj_box02"), -10, 0, -40, 0, 0, PTR(jin_64_box2), SLF(P_64_PIECE))
    RETURN
EVT_END()

/* ======================================================================
 * jin_65, the Spring Room (jin_06): Jerry blasts the cracked wall
 * ====================================================================== */

DOOR(c65_w, "door07")
static BeroEntry jin_65_bero[] = {
    DOOR_ENTRY("w_bero", c65_w, "jin_63", "n_bero"),
    {0},
};

static EVT_BEGIN(jin_65_spring)
    CALL(evt_mario_jump_jumpstand, 0, 0, 0, 0, 260, 0, 0, 0, 50, 900, 1)
    RETURN
EVT_END()

static EVT_BEGIN(jin_65_through)
    LOOP(0)
        CALL(sl6_wait_near, WALL_X, WALL_Z - 30, 30)
        CALL(evt_mario_key_onoff, 0)
        /* (the map change makes the fade itself: one of our own first flashes the room back) */
        CALL(evt_fade_set_mapchange_type, 0, FADE_OUT_BLACK, 400, FADE_IN_BLACK, 300)
        CALL(evt_fade_set_mapchange_type, 1, FADE_OUT_BLACK, 400, FADE_IN_BLACK, 300)
        CALL(evt_bero_mapchange, STRING("jin_66"), STRING("e_bero"))
        RETURN
    END_LOOP
    RETURN
EVT_END()

static EVT_BEGIN(jin_65_blast)
    /* Jerry's own blast has gone off by the wall: the cracked panel falls */
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_eff, STRING(""), STRING("kemuri_test"), 13, WALL_X, 40, WALL_Z, FLOAT(2.0f), 0, 0, 0, 0, 0, 0, 0)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("mekure"), MAPOBJ_FLAG_HIDE)
    CALL(evt_hitobj_onoff, STRING("a_mekure"), 0, 0)
    SET_VALUE(SLF(F_WALL), 1)
    WAIT_MS(900)
    SAY("sl_c6_blast_65b", "party")
    SPAWN_EVENT(jin_65_through)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(jin_65_init)
    CALL_CHILD(jin_common)
    SET_VALUE(LW(0), PTR(jin_65_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL(evt_sl_enemy, STRING("sl_e1"), STRING(SJ6_SPIKY), STRING(SJ6_SPIKY_AI), 110, 150, -80, 40, 30, STRING("BTLNO_JIN_10_01"))
    CALL(evt_sl_enemy, STRING("sl_e2"), STRING(SJ6_SPIKY), STRING(SJ6_SPIKY_AI), 70, 150, 70, 40, 30, STRING("BTLNO_JIN_10_01"))
    CALL(evt_mobj_jumpstand_red, 0, STRING("mobj_jump"), 0, 0, 0, PTR(jin_65_spring), 0)
    /* the spring's prize: Last Stand, hanging in the air over it, out of any jump's reach (the
     * spring throws him up through it; a block there he'd only land on top of) */
    CALL(evt_item_entry, STRING("item_laststand"), ITEM_LAST_STAND, 0, 200, 0, ITEM_ENTRY_FIXED, SLF(P_65_LASTSTAND), 0)
    IF_INT_EQUAL(SLF(F_WALL), 1)
        CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("mekure"), MAPOBJ_FLAG_HIDE)
        CALL(evt_hitobj_onoff, STRING("a_mekure"), 0, 0)
        SPAWN_EVENT(jin_65_through)
    ELSE
        CALL(evt_sl_point, PORT_SL_ABILITY_BLAST, 0, WALL_X, 0, WALL_Z, 110, 0, PTR(jin_65_blast))
        SET_VALUE(LW(10), WALL_X)
        SET_VALUE(LW(11), WALL_Z)
        SET_VALUE(LW(12), 90)
        SET_VALUE(LW(13), PORT_SL_FRIEND_JERRY)
        SET_RAW_VALUE(LW(14), SLF(F_WALL))
        SET_VALUE(LW(15), STRING("sl_c6_crack_65"))
        SET_VALUE(LW(7), 0)
        SPAWN_EVENT(puzzle_watch)
    END_IF
    RETURN
EVT_END()

/* ======================================================================
 * jin_66, the Choir of Torches (jin_07): Hayzee's Echo
 * ====================================================================== */

DOOR(c66_e, "door06")
DOOR(c66_w1, "door08")
static BeroEntry jin_66_bero[] = {
    DOOR_ENTRY("e_bero", c66_e, "jin_65", "w_bero"),
    DOOR_ENTRY("w_bero_1", c66_w1, "jin_67", "e_bero_1"),
    ROAD_ENTRY("w_bero_2", NULL, NULL), /* the borrowed room's tube passage: no Tube Mode here */
    {0},
};

/* The way on: the room's great box (the Boos' in Mario's game), its lid thrown open by the Choir:
 * stairs inside, up to the Broken Floor. The way back comes out in front of it. */
static EVT_BEGIN(jin_66_box_go)
    CALL(evt_mario_key_onoff, 0)
    NARRATE("sl_c6_box_go")
    CALL(evt_snd_sfxon, STRING("SFX_DOOR_OPEN_CASTLE1"), 0)
    /* (the map change makes the fade itself: one of our own first flashes the room back) */
    CALL(evt_fade_set_mapchange_type, 0, FADE_OUT_BLACK, 400, FADE_IN_BLACK, 300)
    CALL(evt_fade_set_mapchange_type, 1, FADE_OUT_BLACK, 400, FADE_IN_BLACK, 300)
    CALL(evt_bero_mapchange, STRING("jin_68"), STRING("s_bero"))
    RETURN
EVT_END()

/* LW(0): the torch (1-3): Hayzee leads, Luigi sings the phrase back */
static EVT_BEGIN(jin_66_echo)
    CALL(evt_mario_key_onoff, 0)
    SET_VALUE(LW(7), LW(0))
    SET_VALUE(LW(6), LW(7))
    SUBTRACT(LW(6), 1)
    LABEL(1)
    /* the lit torch hums the tune; Hayzee leads; Luigi sings it back */
    NARRATE("sl_c6_echo_hum")
    CALL(evt_party_set_pose, 0, STRING("F_1"))
    CALL(sl6_echo_hum, LW(6))
    CALL(evt_party_set_pose, 0, STRING("S_1"))
    SAY("sl_c6_echo_go", "party")
    CALL(sl6_echo, LW(6), LW(5))
    IF_INT_EQUAL(LW(5), 0)
        SAY("sl_c6_echo_bad", "party")
        GOTO(1)
    END_IF
    CALL(evt_snd_sfxon, STRING("SFX_BOSS_KAMEBABA_MAGIC1"), 0)
    SWITCH(LW(7))
        CASE_EQUAL(1)
            CALL(evt_map_replace_mapobj, STRING("honou02"), 0)
            SET_VALUE(SLF(F_TORCH_1), 1)
        CASE_EQUAL(2)
            CALL(evt_map_replace_mapobj, STRING("honou03"), 0)
            SET_VALUE(SLF(F_TORCH_2), 1)
        CASE_DEFAULT
            CALL(evt_map_replace_mapobj, STRING("honou04"), 0)
            SET_VALUE(SLF(F_TORCH_3), 1)
    END_SWITCH
    SAY("sl_c6_echo_ok", "party")
    IF_INT_EQUAL(SLF(F_TORCH_1), 1)
        IF_INT_EQUAL(SLF(F_TORCH_2), 1)
            IF_INT_EQUAL(SLF(F_TORCH_3), 1)
                SET_VALUE(SLF(F_CHOIR), 1)
                CALL(evt_snd_sfxon, STRING("SFX_DOOR_OPEN_CASTLE1"), 0)
                CALL(evt_snd_sfxon, STRING("SFX_STG4_TBOX_OPEN1"), 0)
                SET_VALUE(LW(0), 95)
                LOOP(45)
                    SUBTRACT(LW(0), 2)
                    CALL(evt_mapobj_rotate, 1, STRING("hako_futa"), LW(0), 0, 0)
                    WAIT_FRAMES(1)
                END_LOOP
                NARRATE("sl_c6_choir_done")
                SET_VALUE(LW(0), STRING("w_bero_1"))
                CALL_CHILD(bero_case_switch_on)
                CALL(evt_run_case_evt, CASE_COND_FRONT_A, 1, STRING("a_hako"), 0, PTR(jin_66_box_go), 0)
                CALL(evt_item_entry, STRING("item_piece"), ITEM_STAR_PIECE, 220, 75, -20, ITEM_ENTRY_FIXED, SLF(P_66_PIECE), 0)
            END_IF
        END_IF
    END_IF
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(jin_66_echo1)
    SET_VALUE(LW(0), 1)
    CALL_CHILD(jin_66_echo)
    RETURN
EVT_END()
static EVT_BEGIN(jin_66_echo2)
    SET_VALUE(LW(0), 2)
    CALL_CHILD(jin_66_echo)
    RETURN
EVT_END()
static EVT_BEGIN(jin_66_echo3)
    SET_VALUE(LW(0), 3)
    CALL_CHILD(jin_66_echo)
    RETURN
EVT_END()

CHEST(jin_66_box, "mobj_box1", ITEM_SHINE_SPRITE, 0)

static EVT_BEGIN(jin_66_shut_talk)
    CALL(evt_mario_key_onoff, 0)
    SAY("sl_c6_choir_shut", "mario")
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(jin_66_intro)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_seq_wait, 2)
    WAIT_MS(300)
    SAY("sl_c6_choir_66", "mario")
    CALL(sl6_friend_out, PORT_SL_FRIEND_HAYZEE, LW(5), LW(6))
    IF_INT_EQUAL(LW(5), 1)
        SAY("sl_c6_echo_hint", "party")
    END_IF
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(jin_66_init)
    CALL_CHILD(jin_common)
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG4_JIN3"))
    /* Hayzee knows Echo by now (Petunia taught him in chapter 4): the Choir needs it */
    SET_VALUE(SL_GSWF(4, 90), 1)
    SET_VALUE(LW(0), PTR(jin_66_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL(evt_map_replace_mapobj, STRING("honou01"), 0)
    /* two more torches, lit, at either end of the row (jin_07 has four; the Choir is six, three
     * burning and three dark: c_sl_torch) */
    CALL(evt_npc_entry, STRING("sl_torch1"), STRING("c_sl_torch"))
    CALL(evt_npc_set_position, STRING("sl_torch1"), -220, 0, 0)
    CALL(evt_npc_set_anim, STRING("sl_torch1"), STRING("S_1"))
    CALL(evt_npc_entry, STRING("sl_torch2"), STRING("c_sl_torch"))
    CALL(evt_npc_set_position, STRING("sl_torch2"), 215, 0, 0)
    CALL(evt_npc_set_anim, STRING("sl_torch2"), STRING("S_1"))
    CALL(evt_sl_enemy, STRING("sl_e1"), STRING(SJ6_BOO), STRING(SJ6_BOO), -230, 30, 100, 50, 30, STRING("BTLNO_JIN_00_04"))
    CALL(evt_sl_enemy, STRING("sl_e2"), STRING(SJ6_BOO), STRING(SJ6_BOO), -20, 30, 110, 50, 30, STRING("BTLNO_JIN_00_04"))
    CALL(evt_sl_enemy, STRING("sl_e3"), STRING(SJ6_SWOOPER), STRING(SJ6_SWOOPER_AI), -130, 130, 90, 50, 30, STRING("BTLNO_JIN_00_04"))
    CALL(evt_sl_point, PORT_SL_ABILITY_LOOK, 7, 0, 0, 0, 3000, 0, PTR(boos_look))
    CALL(evt_mobj_itembox, STRING("mobj_box1"), -220, 0, 60, 0, 0, PTR(jin_66_box), SLF(P_66_SHINE))
    IF_INT_EQUAL(SLF(F_TORCH_1), 1)
        CALL(evt_map_replace_mapobj, STRING("honou02"), 0)
    ELSE
        CALL(evt_sl_point, PORT_SL_ABILITY_SING, 1, TORCH1_X, 0, TORCH_Z, 50, 3, PTR(jin_66_echo1))
    END_IF
    IF_INT_EQUAL(SLF(F_TORCH_2), 1)
        CALL(evt_map_replace_mapobj, STRING("honou03"), 0)
    ELSE
        CALL(evt_sl_point, PORT_SL_ABILITY_SING, 2, TORCH2_X, 0, TORCH_Z, 50, 3, PTR(jin_66_echo2))
    END_IF
    IF_INT_EQUAL(SLF(F_TORCH_3), 1)
        CALL(evt_map_replace_mapobj, STRING("honou04"), 0)
    ELSE
        CALL(evt_sl_point, PORT_SL_ABILITY_SING, 3, TORCH3_X, 0, TORCH_Z, 50, 3, PTR(jin_66_echo3))
    END_IF
    /* the door and the box stay shut until all four burn */
    IF_INT_EQUAL(SLF(F_CHOIR), 0)
        SET_VALUE(LW(0), STRING("w_bero_1"))
        CALL_CHILD(bero_case_switch_off)
        CALL(evt_run_case_evt, CASE_COND_FRONT_A, 1, STRING("w_bero_1"), 0, PTR(jin_66_shut_talk), 0)
        CALL(evt_mapobj_rotate, 1, STRING("hako_futa"), 95, 0, 0)
    ELSE
        CALL(evt_mapobj_rotate, 1, STRING("hako_futa"), 5, 0, 0)
        CALL(evt_run_case_evt, CASE_COND_FRONT_A, 1, STRING("a_hako"), 0, PTR(jin_66_box_go), 0)
        CALL(evt_item_entry, STRING("item_piece"), ITEM_STAR_PIECE, 220, 75, -20, ITEM_ENTRY_FIXED, SLF(P_66_PIECE), 0)
    END_IF
    /* the watch: who can light them (Hayzee's Echo, at a dark torch) */
    IF_INT_EQUAL(SLF(F_CHOIR), 0)
        SET_VALUE(LW(10), TORCH_MID_X)
        SET_VALUE(LW(11), TORCH_Z)
        SET_VALUE(LW(12), 160)
        SET_VALUE(LW(13), PORT_SL_FRIEND_HAYZEE)
        SET_RAW_VALUE(LW(14), SLF(F_CHOIR))
        SET_VALUE(LW(15), 0)
        SET_VALUE(LW(7), 0)
        SPAWN_EVENT(puzzle_watch)
        IF_INT_EQUAL(SLF(F_TORCH_1), 0)
            IF_INT_EQUAL(SLF(F_TORCH_2), 0)
                SPAWN_EVENT(jin_66_intro)
            END_IF
        END_IF
    END_IF
    RETURN
EVT_END()

/* ======================================================================
 * jin_67, the Parrot's Perch (jin_08)
 * ====================================================================== */

DOOR(c67_e1, "door07")
static BeroEntry jin_67_bero[] = {
    DOOR_ENTRY("e_bero_1", c67_e1, "jin_66", "w_bero_1"),
    ROAD_ENTRY("e_bero_2", NULL, NULL),
    {0},
};
CHEST(jin_67_box1, "mobj_box01", ITEM_MEGA_RUSH, 1)
CHEST(jin_67_box2, "mobj_box02", ITEM_MAPLE_SYRUP, 0)

static EVT_BEGIN(jin_67_parrot_talk)
    CALL(evt_mario_key_onoff, 0)
    /* a random line, one not heard yet first */
    LABEL(0)
    CALL(evt_sub_random, 3, LW(0))
    SET_VALUE(LW(1), 0)
    IF_INT_EQUAL(SLF(F_PARROT_0), 1)
        ADD(LW(1), 1)
    END_IF
    IF_INT_EQUAL(SLF(F_PARROT_1), 1)
        ADD(LW(1), 1)
    END_IF
    IF_INT_EQUAL(SLF(F_PARROT_2), 1)
        ADD(LW(1), 1)
    END_IF
    IF_INT_EQUAL(SLF(F_PARROT_3), 1)
        ADD(LW(1), 1)
    END_IF
    IF_INT_LESS(LW(1), 4)
        SWITCH(LW(0))
            CASE_EQUAL(0)
                IF_INT_EQUAL(SLF(F_PARROT_0), 1)
                    GOTO(0)
                END_IF
            CASE_EQUAL(1)
                IF_INT_EQUAL(SLF(F_PARROT_1), 1)
                    GOTO(0)
                END_IF
            CASE_EQUAL(2)
                IF_INT_EQUAL(SLF(F_PARROT_2), 1)
                    GOTO(0)
                END_IF
            CASE_DEFAULT
                IF_INT_EQUAL(SLF(F_PARROT_3), 1)
                    GOTO(0)
                END_IF
        END_SWITCH
    END_IF
    CALL(evt_npc_set_anim, STRING("sl_parrot"), STRING("A_1"))
    SWITCH(LW(0))
        CASE_EQUAL(0)
            SAY("sl_c6_parrot_00", "sl_parrot")
            SET_VALUE(SLF(F_PARROT_0), 1)
        CASE_EQUAL(1)
            SAY("sl_c6_parrot_01", "sl_parrot")
            SET_VALUE(SLF(F_PARROT_1), 1)
        CASE_EQUAL(2)
            SAY("sl_c6_parrot_02", "sl_parrot")
            SET_VALUE(SLF(F_PARROT_2), 1)
        CASE_DEFAULT
            SAY("sl_c6_parrot_03", "sl_parrot")
            SET_VALUE(SLF(F_PARROT_3), 1)
    END_SWITCH
    CALL(evt_npc_set_anim, STRING("sl_parrot"), STRING("S_1"))
    IF_INT_EQUAL(LW(1), 0)
        SAY("sl_c6_parrot_04", "mario")
        /* Torque's dry line: from beside Luigi, whoever's out (the room's too narrow for all five) */
        CALL(sl6_friend_out, PORT_SL_FRIEND_TORQUE, LW(5), LW(6))
        IF_INT_EQUAL(LW(5), 1)
            SAY("sl_c6_parrot_05", "party")
        ELSE
            CALL(evt_mario_get_pos, 0, LW(2), LW(3), LW(4))
            ADD(LW(2), 40)
            CALL(evt_npc_entry, STRING("sl_tq"), STRING("c_lp_met"))
            CALL(evt_npc_set_position, STRING("sl_tq"), LW(2), LW(3), LW(4))
            CALL(evt_npc_set_ry, STRING("sl_tq"), 270)
            CALL(evt_npc_set_anim, STRING("sl_tq"), STRING("S_1"))
            CALL(evt_snd_sfxon, STRING("SFX_PARTY_OUT1"), 0)
            SAY("sl_c6_parrot_05", "sl_tq")
            CALL(evt_npc_delete, STRING("sl_tq"))
        END_IF
    END_IF
    IF_INT_EQUAL(SLF(F_KEY), 0)
        IF_INT_EQUAL(SLF(F_PARROT_0), 1)
            IF_INT_EQUAL(SLF(F_PARROT_1), 1)
                IF_INT_EQUAL(SLF(F_PARROT_2), 1)
                    IF_INT_EQUAL(SLF(F_PARROT_3), 1)
                        CALL(evt_npc_set_anim, STRING("sl_parrot"), STRING("A_1"))
                        SAY("sl_c6_parrot_key", "sl_parrot")
                        CALL(evt_npc_set_anim, STRING("sl_parrot"), STRING("S_1"))
                        CALL(evt_snd_sfxon, STRING("SFX_ITEM_KIRAKIRA_FALL1"), 0)
                        NARRATE("sl_c6_parrot_got")
                        SET_VALUE(LW(0), ITEM_STEEPLE_KEY_0x16)
                        CALL_CHILD(give_item)
                        SET_VALUE(SLF(F_KEY), 1)
                    END_IF
                END_IF
            END_IF
        END_IF
    END_IF
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(jin_67_init)
    CALL_CHILD(jin_common)
    SET_VALUE(LW(0), PTR(jin_67_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL(evt_mapobj_rotate, 1, STRING("hako_futa"), 90, 0, 0)
    CALL(evt_mapobj_rotate, 1, STRING("hako_futa2"), -90, 0, 0)
    CALL(evt_mobj_itembox, STRING("mobj_box01"), 60, 0, -65, 0, 0, PTR(jin_67_box1), SLF(P_67_MEGARUSH))
    CALL(evt_mobj_itembox, STRING("mobj_box02"), 120, 0, -65, 0, 0, PTR(jin_67_box2), SLF(P_67_SYRUP))
    /* a hidden block over the birdcage: a Star Piece */
    CALL(evt_mobj_brick, STRING("mobj_hidden"), -120, 120, -40, ITEM_STAR_PIECE, 11, 0, SLF(P_67_PIECE))
    CALL(evt_npc_entry, STRING("sl_parrot"), STRING("c_f_owm"))
    CALL(evt_npc_set_tribe, STRING("sl_parrot"), STRING(SJ6_PARROT))
    CALL(evt_npc_set_position, STRING("sl_parrot"), -40, 60, -60)
    CALL(evt_npc_set_anim, STRING("sl_parrot"), STRING("S_1"))
    /* up on its perch it's out of talking reach (the talk check wants an NPC no higher than
     * Luigi's head): an unseen twin at the perch's foot takes the talk */
    CALL(evt_npc_entry, STRING("sl_parrot_t"), STRING("c_f_owm"))
    CALL(evt_npc_set_tribe, STRING("sl_parrot_t"), STRING(SJ6_PARROT))
    CALL(evt_npc_flag_onoff, 1, STRING("sl_parrot_t"), NPC_FLAG_HIDDEN | NPC_FLAG_NO_SHADOW | NPC_FLAG_NO_TURN_TO_MARIO)
    CALL(evt_npc_set_position, STRING("sl_parrot_t"), -40, 0, -60)
    CALL(evt_npc_set_width, STRING("sl_parrot_t"), 70)
    CALL(evt_npc_change_interrupt, STRING("sl_parrot_t"), 6, PTR(jin_67_parrot_talk))
    RETURN
EVT_END()

/* ======================================================================
 * jin_68, the Broken Floor (jin_11): Look and Float over the shaft
 * ====================================================================== */

DOOR(c68_s, "door10")
/* through the broken floor: down to the Winch Gate's far corner (jin_69 places him, F_FELL) */
static EVT_BEGIN(jin_68_fall)
    SET_VALUE(SLF(F_FELL), 1)
    CALL(evt_bero_mapchange, STRING("jin_69"), STRING("sl_fall"))
    SET_VALUE(LW(0), 1)
    RETURN
EVT_END()
static BeroEntry jin_68_bero[] = {
    DOOR_ENTRY("s_bero", c68_s, "jin_66", "sl_box"), /* (not in its table: at the room's origin, by the box) */
    {"ana", 0, 0, BERO_DIR_S, 845, 115, 77, -1, NULL, -1, jin_68_fall, "jin_69", NULL, 4, 4, 0, 0},
    {0},
};

/* broken by a Spin Jump (no Screamy): Luigi drops into the shaft and lands on its floor far below,
 * still in this room (the hole's "ana" never fired: he stood there for good). Once he is down
 * there, he goes on to jin_69 as the fall does. */
static EVT_BEGIN(jin_68_drop)
    LOOP(0)
        WAIT_FRAMES(2)
        CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
        IF_INT_GREATER(LW(0), 780)
            IF_INT_LESS(LW(1), 80)
                BREAK_LOOP
            END_IF
        END_IF
    END_LOOP
    CALL(evt_mario_key_onoff, 0)
    CALL_CHILD(jin_68_fall)
    RETURN
EVT_END()

static EVT_BEGIN(jin_68_floor)
    SET_VALUE(SLF(F_FLOOR), 1)
    SPAWN_EVENT(jin_68_drop)
    RETURN
EVT_END()

/* The way from the door to the stairs is a slot 20 high at x 70 behind the black pillar, Mario's by Tube
 * Mode, which Luigi hasn't got; it was the only way on. Pushing into the slot takes him through it, either
 * way. Blocks until he pushes: LW out, the x to land on. */
static USER_FUNC(sl6_wait_slot) {
    MarioWork* m = marioGetPtr();
    const s32 stick = (s8)m->wStickDir1;
    (void)isFirstCall;
    if (!gp->inBattle && marioChkKey() && m->position.y < 10.0f && m->position.z > -22.0f && m->position.z < 32.0f) {
        if (m->position.x > 40.0f && m->position.x < 70.0f && stick > 30) {
            evtSetValue(event, event->args[0], 100);
            return EVT_RETURN_DONE;
        }
        if (m->position.x >= 70.0f && m->position.x < 100.0f && stick < -30) {
            evtSetValue(event, event->args[0], 35);
            return EVT_RETURN_DONE;
        }
    }
    return EVT_RETURN_BLOCK;
}

static EVT_BEGIN(jin_68_slot)
    LOOP(0)
        CALL(sl6_wait_slot, LW(0))
        CALL(evt_mario_key_onoff, 0)
        CALL(evt_mario_jump_pos, LW(0), 0, 5, 500, 0, 8)
        CALL(evt_mario_key_onoff, 1)
        WAIT_FRAMES(30)
    END_LOOP
    RETURN
EVT_END()

/* Screamy's Look at the rotten floor: a ledge far down the shaft under it, something shining
 * in its alcove; he floats Luigi down through the floor's breaking to it, and on down, gently,
 * to the Winch Gate. (The room has no far side: the floor ends at the panel, as Mario's did.)
 * Without him the floor just gives way under Luigi: the same room below, no Shine Sprite. */
static EVT_BEGIN(jin_68_look)
    CALL(evt_mario_key_onoff, 0)
    SAY("sl_c6_look_68", "party")
    SET_VALUE(SLF(F_FLOAT), 1)
    /* he takes hold; the floor gives under them, and they drift down */
    CALL(evt_party_set_pose, 0, STRING("F_2"))
    CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
    ADD(LW(1), 38)
    CALL(evt_party_set_pos, 0, LW(0), LW(1), LW(2))
    CALL(evt_luigi_story_pose, STRING("SL_HANG"))
    CALL(evt_snd_sfxon, STRING("SFX_PARTY_BATTLE_SUBHANG1"), 0)
    WAIT_MS(400)
    CALL(evt_mario_jump_pos, SHAFT_X, SHAFT_Y + 30, SHAFT_Z, 600, 0, 30)
    CALL(evt_snd_sfxon, STRING("SFX_BTL_SYS_BREAK1"), 0)
    CALL(evt_cam_shake, 4, FLOAT(0.0f), FLOAT(2.0f), 300)
    SET_VALUE(SLF(F_FLOOR), 1)
    CALL(evt_fade_entry, FADE_OUT_BLACK, 900, 0, 0, 0)
    CALL(evt_fade_end_wait)
    CALL(evt_luigi_story_pose, 0)
    IF_INT_EQUAL(SLF(P_68_SHINE), 0)
        SAY("sl_c6_alcove_68", "party")
        SET_VALUE(LW(0), ITEM_SHINE_SPRITE)
        CALL_CHILD(give_item)
        SET_VALUE(SLF(P_68_SHINE), 1)
    END_IF
    SET_VALUE(SLF(F_FELL), 1)
    CALL(evt_bero_mapchange, STRING("jin_69"), STRING("sl_fall"))
    RETURN
EVT_END()

static EVT_BEGIN(jin_68_init)
    CALL_CHILD(jin_common)
    /* the stairs were Mario's tube's: this roof over them stops anyone standing */
    CALL(evt_hitobj_onoff, STRING("a_gawa04"), 1, 0)
    SET_VALUE(LW(0), PTR(jin_68_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL(evt_mobj_save_blk, STRING("mobj_save"), -50, 60, -40, 0, 0)
    CALL(evt_mobj_badgeblk, STRING("mobj_item"), 40, 60, -40, ITEM_SUPER_SHROOM, 0, SLF(P_68_SHROOM), 0)
    /* (the corridor's floor is z -20..30) */
    CALL(evt_sl_enemy, STRING("sl_e1"), STRING(SJ6_BUZZY), STRING(SJ6_BUZZY), 200, 0, 5, 50, 30, STRING("BTLNO_JIN_10_02"))
    CALL(evt_sl_enemy, STRING("sl_e2"), STRING(SJ6_SPIKY), STRING(SJ6_SPIKY_AI), 500, 150, 5, 60, 30, STRING("BTLNO_JIN_10_02"))
    SPAWN_EVENT(jin_68_slot)
    IF_INT_EQUAL(SLF(F_FLOOR), 0)
        CALL(evt_mobj_breaking_floor, STRING("mobj_bf00"), 845, 115, 5, 0, PTR(jin_68_floor), SLF(F_FLOOR))
    END_IF
    /* the rotten floor: Screamy's Look sees the ledge below, and floats Luigi down to it */
    IF_INT_EQUAL(SLF(F_FLOOR), 0)
        CALL(evt_sl_point, PORT_SL_ABILITY_LOOK, 0, SHAFT_NEAR_X, SHAFT_Y, SHAFT_Z, 120, 0, PTR(jin_68_look))
    END_IF
    SET_VALUE(LW(10), SHAFT_NEAR_X)
    SET_VALUE(LW(11), SHAFT_Z)
    SET_VALUE(LW(12), 90)
    SET_VALUE(LW(13), PORT_SL_FRIEND_SCREAMY)
    SET_RAW_VALUE(LW(14), SLF(F_FLOAT))
    SET_VALUE(LW(15), STRING("sl_c6_shaft_68"))
    SET_VALUE(LW(7), 0)
    SPAWN_EVENT(puzzle_watch)
    RETURN
EVT_END()

/* ======================================================================
 * jin_69, the Winch Gate (jin_10): Torque frees the winch
 * ====================================================================== */

DOOR(c69_n, "door11")
static BeroEntry jin_69_bero[] = {
    ROAD_ENTRY("w_bero", "jin_70", "e_bero"),
    DOOR_ENTRY("n_bero", c69_n, "jin_68", "s_bero"),
    {0},
};

static EVT_BEGIN(jin_69_gate_open)
    CALL(evt_hit_bind_mapobj, STRING("a_saku"), STRING("saku"))
    CALL(evt_mapobj_trans, 1, STRING("saku"), 265, 66, 0)
    CALL(evt_hit_bind_update, STRING("a_saku"))
    RETURN
EVT_END()

static EVT_BEGIN(jin_69_tune)
    CALL(evt_mario_key_onoff, 0)
    SAY("sl_c6_tune_69", "party")
    /* the gate slides */
    CALL(evt_hit_bind_mapobj, STRING("a_saku"), STRING("saku"))
    CALL(evt_snd_sfxon, STRING("SFX_STG4_GRID_MOVE1"), 0)
    CALL(evt_sub_intpl_init, 0, 0, 265, 60)
    LOOP(60)
        CALL(evt_sub_intpl_get_value)
        SET_VALUE(LW(1), LW(0))
        DIVIDE(LW(1), 4)
        CALL(evt_mapobj_trans, 1, STRING("saku"), LW(0), LW(1), 0)
        CALL(evt_hit_bind_update, STRING("a_saku"))
        WAIT_FRAMES(1)
    END_LOOP
    SET_VALUE(SLF(F_WINCH), 1)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* down through jin_68's floor: he drops in from above, no harm (as jin_09's own drop-in) */
static EVT_BEGIN(jin_69_fell)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_mario_cont_onoff, 0)
    CALL(evt_party_cont_onoff, 0, 0)
    CALL(evt_mario_set_pos, FALL_X, 260, FALL_Z)
    CALL(evt_mario_get_party, LW(0))
    IF_INT_NOT_EQUAL(LW(0), 0)
        CALL(evt_party_set_pos, 0, FALL_X + 30, 290, FALL_Z)
    END_IF
    CALL(evt_cam_road_reset)
    CALL(evt_seq_wait, 2)
    CALL(evt_mario_cont_onoff, 1)
    WAIT_MS(180)
    CALL(evt_party_cont_onoff, 0, 1)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(jin_69_init)
    CALL_CHILD(jin_common)
    SET_VALUE(LW(0), PTR(jin_69_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    IF_INT_EQUAL(SLF(F_FELL), 1)
        SET_VALUE(SLF(F_FELL), 0)
        BEGIN_INLINE_EVENT
            CALL_CHILD(jin_69_fell)
        END_INLINE_EVENT
    END_IF
    CALL(evt_mobj_badgeblk, STRING("m"), 60, 60, -25, ITEM_TORNADO_JUMP, 0, SLF(P_69_TORNADO), 1)
    CALL(evt_sl_enemy, STRING("sl_e1"), STRING(SJ6_SWOOPER), STRING(SJ6_SWOOPER_AI), 150, 130, 0, 80, 30, STRING("BTLNO_JIN_10_03"))
    CALL(evt_sl_enemy, STRING("sl_e2"), STRING(SJ6_SWOOPER), STRING(SJ6_SWOOPER_AI), -50, 130, 40, 80, 30, STRING("BTLNO_JIN_10_03"))
    CALL(evt_sl_enemy, STRING("sl_e3"), STRING(SJ6_BOO), STRING(SJ6_BOO), 250, 60, 60, 60, 30, STRING("BTLNO_JIN_10_03"))
    CALL(evt_sl_point, PORT_SL_ABILITY_LOOK, 7, 0, 0, 0, 3000, 0, PTR(boos_look))
    IF_INT_EQUAL(SLF(F_WINCH), 1)
        CALL_CHILD(jin_69_gate_open)
    ELSE
        CALL(evt_sl_point, PORT_SL_ABILITY_TUNE, 0, WINCH_X, 0, WINCH_Z, 90, 0, PTR(jin_69_tune))
        SET_VALUE(LW(10), WINCH_X)
        SET_VALUE(LW(11), WINCH_Z)
        SET_VALUE(LW(12), 80)
        SET_VALUE(LW(13), PORT_SL_FRIEND_TORQUE)
        SET_RAW_VALUE(LW(14), SLF(F_WINCH))
        SET_VALUE(LW(15), STRING("sl_c6_winch_69"))
        SET_VALUE(LW(7), 0)
        SPAWN_EVENT(puzzle_watch)
    END_IF
    RETURN
EVT_END()

/* ======================================================================
 * jin_70, the Weeping Room (jin_09): Blooey swims the goo, and burns
 * ====================================================================== */

static BeroEntry jin_70_bero[] = {
    ROAD_ENTRY("e_bero", "jin_69", "w_bero"),
    {0},
};

static EVT_BEGIN(jin_70_spring)
    CALL(evt_mario_jump_jumpstand, 0, 0, 0, SPRING_X, 700, 0, SPRING_X, 0, 0, 1800, 1)
    RETURN
EVT_END()

/* up the spring's shaft: out on the Dark Hall's upper landing */
static EVT_BEGIN(jin_70_up)
    CALL(sl6_wait_above, 365)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_bero_mapchange, STRING("jin_60"), STRING("e_bero_3"))
    RETURN
EVT_END()

/* 3. The goo moat: the first time, Blooey goes */
static EVT_BEGIN(jin_70_goo)
    CALL(evt_mario_key_onoff, 0)
    SAY("sl_c6_goo_moat", "mario")
    CALL(sl6_friend_out, PORT_SL_FRIEND_BLOOEY, LW(5), LW(6))
    IF_INT_EQUAL(LW(5), 1)
        SAY("sl_c6_goo_00", "party")
        SAY("sl_c6_goo_01", "mario")
        SAY("sl_c6_goo_02", "party")
    ELSE
        CALL(evt_msg_print, 0, LW(6), 0, STRING("mario"))
    END_IF
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(jin_70_goo_watch)
    /* on the near shore: the goo, and (once Blooey's the one out) his offer; again after leaving */
    LOOP(0)
        CALL(sl6_wait_near, MOAT_NEAR_X, 0, 60)
        IF_INT_EQUAL(SLF(F_GOO), 1)
            BREAK_LOOP
        END_IF
        CALL_CHILD(jin_70_goo)
        CALL(sl6_friend_out, PORT_SL_FRIEND_BLOOEY, LW(5), LW(6))
        IF_INT_EQUAL(LW(5), 1)
            BREAK_LOOP
        END_IF
        LOOP(0)
            WAIT_FRAMES(30)
            CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
            CALL(evt_sub_get_dist, LW(0), LW(2), MOAT_NEAR_X, 0, LW(3))
            IF_INT_GREATER(LW(3), 120)
                BREAK_LOOP
            END_IF
        END_LOOP
    END_LOOP
    /* on the far shore: burnt */
    CALL(sl6_wait_near, MOAT_FAR_X, 0, 50)
    IF_INT_EQUAL(SLF(F_GOO), 0)
        CALL(evt_mario_key_onoff, 0)
        WAIT_MS(300)
        CALL(evt_party_get_pos, 0, LW(0), LW(1), LW(2))
        ADD(LW(1), 30)
        LOOP(3)
            CALL(evt_eff, STRING(""), STRING("kemuri_test"), 13, LW(0), LW(1), LW(2), FLOAT(1.0f), 0, 0, 0, 0, 0, 0, 0)
            CALL(evt_snd_sfxon, STRING("SFX_BTL_DAMAGE_FIRE1"), 0)
            WAIT_MS(250)
        END_LOOP
        SET_VALUE(SLF(F_GOO), 1)
        CALL(sl6_soot, 1)
        WAIT_MS(500)
        SAY("sl_c6_goo_03", "party")
        CALL(evt_mario_key_onoff, 1)
    END_IF
    RETURN
EVT_END()

/* the goo steams and bubbles */
static EVT_BEGIN(jin_70_steam)
    LOOP(0)
        CALL(evt_sub_random, 90, LW(0))
        ADD(LW(0), MOAT_X0)
        CALL(evt_sub_random, 200, LW(2))
        SUBTRACT(LW(2), 100)
        CALL(evt_eff, STRING(""), STRING("kemuri_test"), 4, LW(0), 0, LW(2), FLOAT(0.8f), 0, 0, 0, 0, 0, 0, 0)
        CALL(evt_sub_random, 30, LW(3))
        ADD(LW(3), 20)
        WAIT_FRAMES(LW(3))
    END_LOOP
    RETURN
EVT_END()

static EVT_BEGIN(jin_70_drip)
    LOOP(0)
        CALL(evt_sub_random, 180, LW(0))
        ADD(LW(0), 60)
        WAIT_FRAMES(LW(0))
        CALL(evt_map_playanim, STRING("sizuku_01"), 0, 0)
        CALL(evt_snd_sfxon_3d, STRING("SFX_STG4_AMB_WATERDROP1"), -76, 0, 13)
    END_LOOP
    RETURN
EVT_END()

static EVT_BEGIN(jin_70_init)
    CALL_CHILD(jin_common)
    SET_VALUE(LW(0), PTR(jin_70_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL(evt_mobj_jumpstand_red, 0, STRING("jumpstand"), SPRING_X, 0, 0, PTR(jin_70_spring), 0)
    CALL(evt_mobj_badgeblk, STRING("mobj_item"), -150, 60, -50, ITEM_LIFE_SHROOM, 0, SLF(P_70_LIFE), 0)
    CALL(evt_mobj_badgeblk, STRING("mobj_coin"), -60, 60, -60, ITEM_COIN, 0, SLF(P_70_COIN), 0)
    CALL(evt_sl_enemy, STRING("sl_e1"), STRING(SJ6_SPIKY), STRING(SJ6_SPIKY_AI), -60, 140, -60, 40, 30, STRING("BTLNO_JIN_10_04"))
    CALL(evt_sl_enemy, STRING("sl_e2"), STRING(SJ6_BUZZY), STRING(SJ6_BUZZY), -130, 0, 40, 30, 30, STRING("BTLNO_JIN_10_04"))
    /* the moat of hot goo across the low floor: only Blooey can swim it */
    CALL(sl6_goo, MOAT_X0, -144, MOAT_X1, 144, 2)
    CALL(evt_sl_swim_clear)
    CALL(evt_sl_swim_point, 0, MOAT_NEAR_X, 0, 0, MOAT_FAR_X, 0, 0)
    CALL(evt_sl_lava_zone, MOAT_X0 + 4, -150, MOAT_X1 - 8, 150, 10, MOAT_NEAR_X, 0, 0)
    SPAWN_EVENT(jin_70_steam)
    SPAWN_EVENT(jin_70_drip)
    SPAWN_EVENT(jin_70_goo_watch)
    SPAWN_EVENT(jin_70_up)
    RETURN
EVT_END()

/* ======================================================================
 * jin_71, the Crown Loft (jin_04): the King, Eclair, Crepe, the compass
 * ====================================================================== */

DOOR(c71_s, "door00")

/* the loft's staging: on the floor in front of the stairs' first tread (z 80), Luigi far enough
 * right that the five friends' line fits on his left; Crepe on the stairs behind them */
#define LOFT_Z 110
#define LOFT_LUIGI_X 40
#define LOFT_ECLAIR_X 70 /* clear of Crepe on the step (100) */
#define LOFT_ECLAIR_Z 120
#define LOFT_KING_X 145
#define LOFT_CREPE_TOP 120, 105, -10
#define LOFT_CREPE_STEP 100, 15, 50

static BeroEntry jin_71_bero[] = {
    DOOR_ENTRY("s_bero", c71_s, "jin_60", "n_bero_2"),
    {0},
};

static EVT_BEGIN(jin_71_after)
    /* 11. After: the curse breaks */
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_ENDING1"))
    SAY("sl_c6_after_00", "sl_crepe")
    SAY("sl_c6_after_01", "mario")
    CALL(evt_npc_set_anim, STRING("sl_king"), STRING("C_1"))
    CALL(evt_cam_shake, 4, FLOAT(0.0f), FLOAT(2.0f), 800)
    CALL(evt_npc_get_position, STRING("sl_king"), LW(0), LW(1), LW(2))
    LOOP(4)
        CALL(evt_eff, STRING(""), STRING("kemuri_test"), 13, LW(0), LW(1), LW(2), FLOAT(1.5f), 0, 0, 0, 0, 0, 0, 0)
        CALL(evt_snd_sfxon, STRING("SFX_BTL_SYS_BREAK1"), 0)
        WAIT_MS(300)
    END_LOOP
    WAIT_MS(800)
    /* King Marron, restored */
    CALL(evt_npc_get_position, STRING("sl_king"), LW(0), LW(1), LW(2))
    CALL(evt_npc_entry, STRING("sl_marron"), STRING("c_sl_marron"))
    CALL(evt_npc_set_position, STRING("sl_marron"), LW(0), LW(1), LW(2))
    CALL(evt_npc_set_ry, STRING("sl_marron"), 270)
    CALL(evt_npc_delete, STRING("sl_king"))
    CALL(evt_npc_set_anim, STRING("sl_marron"), STRING("U_1"))
    WAIT_MS(1600)
    CALL(evt_npc_set_anim, STRING("sl_marron"), STRING("S_1"))
    SAY("sl_c6_after_02", "sl_marron")
    SAY("sl_c6_after_03", "sl_eclair")
    CALL(evt_npc_set_anim, STRING("sl_eclair"), STRING("R_1"))
    CALL(evt_npc_get_position, STRING("sl_marron"), LW(0), LW(1), LW(2))
    SUBTRACT(LW(0), 24)
    CALL(evt_npc_move_position, STRING("sl_eclair"), LW(0), LW(2), 500, 0, 4)
    CALL(evt_npc_set_anim, STRING("sl_eclair"), STRING("E_1"))
    CALL(evt_npc_set_anim, STRING("sl_marron"), STRING("H_1"))
    WAIT_MS(2500)
    CALL(evt_npc_set_ry, STRING("sl_marron"), 270)
    CALL(evt_npc_set_anim, STRING("sl_marron"), STRING("T_1"))
    SAY("sl_c6_after_04", "sl_marron")
    CALL(evt_npc_set_anim, STRING("sl_marron"), STRING("B_1"))
    SAY("sl_c6_after_05", "mario")
    CALL_CHILD(friends_in)
    SAY("sl_c6_after_06", "sl_je")
    SAY("sl_c6_after_07", "sl_crepe")
    SAY("sl_c6_after_08", "sl_je")
    /* Crepe is marched off by Jerry and Torque */
    CALL(evt_npc_set_anim, STRING("sl_crepe"), STRING("W_1"))
    CALL(evt_npc_set_anim, STRING("sl_je"), STRING("W_1"))
    CALL(evt_npc_set_anim, STRING("sl_tq"), STRING("W_1"))
    BEGIN_INLINE_EVENT
        CALL(evt_npc_move_position, STRING("sl_je"), -400, 120, 2500, 0, 4)
    END_INLINE_EVENT
    BEGIN_INLINE_EVENT
        CALL(evt_npc_move_position, STRING("sl_tq"), -420, 60, 2500, 0, 4)
    END_INLINE_EVENT
    CALL(evt_npc_move_position, STRING("sl_crepe"), -400, 90, 2500, 0, 4)
    CALL(evt_npc_delete, STRING("sl_crepe"))
    CALL_CHILD(friends_out)
    /* 12. The compass */
    CALL(evt_npc_set_anim, STRING("sl_eclair"), STRING("TI_1"))
    WAIT_MS(800)
    CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
    ADD(LW(1), 40)
    CALL(evt_eff64, STRING("sl_glow"), STRING("akari_charge_n64"), 1, LW(0), LW(1), LW(2), FLOAT(2.0f), 0, 0, 0, 0, 0, 0, 0)
    CALL(evt_snd_sfxon, STRING("SFX_STAR_POINT_GET1"), 0)
    CALL(evt_mario_set_pose, STRING("M_I_2"))
    NARRATE("sl_c6_compass_whole")
    CALL(evt_npc_set_anim, STRING("sl_eclair"), STRING("S_1"))
    SAY("sl_c6_compass_00", "sl_eclair")
    SAY("sl_c6_compass_01", "mario")
    CALL(evt_msg_select, 0, STRING("sl_c6_compass_sel"))
    SET_VALUE(LW(5), LW(0))
    CALL(evt_msg_continue)
    IF_INT_EQUAL(LW(5), 0)
        CALL(evt_map_set_blend, 0, 255, 230, 140, 255)
        WAIT_MS(1500)
        CALL(evt_map_set_blend, 0, 255, 255, 255, 255)
        SAY("sl_c6_compass_02", "mario")
    ELSE
        SAY("sl_c6_compass_03", "mario")
    END_IF
    CALL(evt_mario_set_pose, STRING("M_S_1"))
    SAY("sl_c6_compass_04", "sl_eclair")
    SAY("sl_c6_compass_05", "mario")
    /* he breaks it: seven lights, up through the roof */
    CALL(evt_eff_delete, STRING("sl_glow"))
    WAIT_MS(400)
    CALL(evt_snd_sfxon, STRING("SFX_BTL_SYS_BREAK1"), 0)
    CALL(evt_cam_shake, 4, FLOAT(0.0f), FLOAT(2.0f), 300)
    CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
    ADD(LW(0), 30)
    LOOP(7)
        CALL(evt_eff64, STRING(""), STRING("stardust_n64"), 2, LW(0), LW(1), LW(2), FLOAT(20.0f), 0, 0, 0, 0, 0, 0, 0)
        CALL(evt_snd_sfxon, STRING("SFX_STAR_POINT_GET1"), 0)
        ADD(LW(1), 60)
        WAIT_MS(250)
    END_LOOP
    CALL(evt_mario_set_pose, STRING("M_S_1"))
    WAIT_MS(800)
    SAY("sl_c6_compass_06", "sl_eclair")
    SAY("sl_c6_compass_07", "mario")
    SET_VALUE(SLF(F_CREPE_DOWN), 1)
    /* (the map change makes the fade itself: one of our own first flashes the room back) */
    CALL(evt_fade_set_mapchange_type, 0, FADE_OUT_BLACK, 1500, FADE_IN_BLACK, 300)
    CALL(evt_fade_set_mapchange_type, 1, FADE_OUT_BLACK, 1500, FADE_IN_BLACK, 300)
    CALL(evt_bero_mapchange, STRING("gra_60"), STRING("sl_dawn")) /* not in its table: no walk-in, the scene places him */
    RETURN
EVT_END()

/* 6.-10. The King, the truth, Crepe and his fight */
static EVT_BEGIN(jin_71_scene)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_seq_wait, 2)
    WAIT_MS(400)
    IF_INT_EQUAL(SLF(F_KING_DOWN), 1)
        GOTO(20)
    END_IF
    CALL(evt_mario_mov_pos2, LOFT_LUIGI_X, LOFT_Z, FLOAT(80.0f))
    CALL(evt_mario_set_dir, 90, 0, 0)
    /* the door's in the near wall, in front of everything: gone for the scenes (it stays an exit) */
    CALL(evt_mapobj_flag_onoff, 0, 1, STRING("door00"), MAPOBJ_FLAG_HIDE)
    SAY("sl_c6_king_00", "sl_king")
    SAY("sl_c6_king_01", "mario")
    SAY("sl_c6_king_02", "sl_king")
    SET_VALUE(SLF(F_KING), 1)
    /* the Chestnut King (superluigi_ch06_boss_aurora.c) */
    CALL(evt_sl6_boss_arm, 2)
    CALL(evt_npc_set_battle_info, STRING("sl_king"), 5)
    CALL(evt_npc_battle_start, STRING("sl_king"))
    CALL(evt_npc_wait_battle_end)
    CALL(evt_sl6_boss_arm, 0)
    CALL(evt_npc_get_battle_result, LW(0))
    IF_INT_NOT_EQUAL(LW(0), 1)
        CALL(evt_mario_key_onoff, 1)
        RETURN
    END_IF
    /* the swing lands: black; the wind roars */
    CALL(evt_fade_entry, FADE_COVER_BLACK_2D, 0, 0, 0, 0)
    CALL(evt_snd_bgmoff, PSND_FADE_OUT)
    CALL(evt_snd_sfxon, STRING("SFX_AMB_WIND1"), 0)
    CALL(evt_cam_shake, 4, FLOAT(0.0f), FLOAT(3.0f), 1200)
    WAIT_MS(1500)
    NARRATE("sl_c6_dots_00")
    NARRATE("sl_c6_dots_01")
    NARRATE("sl_c6_dots_02")
    SET_VALUE(SLF(F_KING_DOWN), 1)
    LABEL(20)
    /* the scene as the swing stopped: Luigi over the King, the princess between */
    CALL(evt_mapobj_flag_onoff, 0, 1, STRING("door00"), MAPOBJ_FLAG_HIDE)
    CALL(evt_npc_set_position, STRING("sl_king"), LOFT_KING_X, 0, LOFT_Z)
    CALL(evt_npc_set_anim, STRING("sl_king"), STRING("K_1"))
    CALL(evt_mario_set_pos, LOFT_LUIGI_X, 0, LOFT_Z)
    CALL(evt_mario_set_dir, 90, 0, 0)
    CALL(evt_npc_entry, STRING("sl_eclair"), STRING("c_sl_eclair"))
    CALL(evt_npc_set_position, STRING("sl_eclair"), LOFT_ECLAIR_X, 0, LOFT_ECLAIR_Z)
    CALL(evt_npc_set_ry, STRING("sl_eclair"), 270)
    CALL(evt_npc_set_anim, STRING("sl_eclair"), STRING("P_1"))
    SAY("sl_c6_stop_00", "sl_eclair")
    CALL(evt_fade_entry, FADE_IN_BLACK_2D, 600, 0, 0, 0)
    CALL(evt_fade_end_wait)
    SAY("sl_c6_stop_01", "sl_eclair")
    CALL(evt_mario_set_pose, STRING("M_S_1"))
    SAY("sl_c6_stop_02", "mario")
    /* it's the voice; the hammer drops */
    CALL(evt_snd_sfxon, STRING("SFX_ITEM_KIRAKIRA_FALL1"), 0)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_PEACH1"))
    CALL(evt_npc_set_anim, STRING("sl_eclair"), STRING("T_1"))
    SAY("sl_c6_truth_00", "sl_eclair")
    SAY("sl_c6_truth_01", "mario")
    SAY("sl_c6_truth_02", "sl_eclair")
    CALL(evt_npc_set_anim, STRING("sl_eclair"), STRING("K_1"))
    WAIT_MS(800)
    CALL(evt_npc_set_anim, STRING("sl_king"), STRING("Q_1"))
    CALL(evt_snd_sfxon, STRING("SFX_BOSS_GNB_BREATH_CHANGE1"), 0)
    WAIT_MS(800)
    SAY("sl_c6_truth_03", "sl_eclair")
    SAY("sl_c6_truth_04", "mario")
    SAY("sl_c6_truth_05", "mario")
    CALL(evt_npc_set_anim, STRING("sl_eclair"), STRING("T_1"))
    SAY("sl_c6_truth_06", "sl_eclair")
    SAY("sl_c6_truth_07", "sl_eclair")
    CALL_CHILD(friends_in)
    SAY("sl_c6_truth_08", "sl_sc")
    SAY("sl_c6_truth_09", "mario")
    CALL_CHILD(friends_out)
    /* 7. A slow clap from the stairs */
    CALL(evt_snd_bgmoff, PSND_FADE_OUT)
    CALL(evt_npc_entry, STRING("sl_crepe"), STRING("c_sl_crepe"))
    CALL(evt_npc_set_position, STRING("sl_crepe"), LOFT_CREPE_TOP)
    CALL(evt_npc_set_ry, STRING("sl_crepe"), 270)
    CALL(evt_npc_set_anim, STRING("sl_crepe"), STRING("CL_1"))
    /* he claps from the lower flight's turn, in the room's own view (S_msk, the black sheet with
     * the room's hole in it, cuts off any camera raised or moved back), and comes down */
    LOOP(4)
        CALL(evt_snd_sfxon, STRING("SFX_BOSS_MARI_HAND_CLAP1"), 0)
        WAIT_MS(500)
    END_LOOP
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_3RD_ENEMY"))
    CALL(evt_npc_set_anim, STRING("sl_crepe"), STRING("W_2"))
    CALL(evt_sl_npc_fly, STRING("sl_crepe"), LOFT_CREPE_STEP, 2500)
    CALL(evt_npc_set_anim, STRING("sl_crepe"), STRING("T_1"))
    CALL(evt_npc_set_ry, STRING("sl_eclair"), 90)
    SAY("sl_c6_crepe_00", "sl_crepe")
    SAY("sl_c6_crepe_01", "mario")
    CALL(evt_npc_set_anim, STRING("sl_crepe"), STRING("T_2"))
    SAY("sl_c6_crepe_02", "sl_crepe")
    CALL(evt_npc_set_anim, STRING("sl_eclair"), STRING("T_1"))
    SAY("sl_c6_crepe_03", "sl_eclair")
    CALL(evt_npc_set_anim, STRING("sl_crepe"), STRING("T_1"))
    SAY("sl_c6_crepe_04", "sl_crepe")
    SAY("sl_c6_crepe_05", "mario")
    SAY("sl_c6_crepe_06", "sl_crepe")
    CALL(evt_npc_set_anim, STRING("sl_crepe"), STRING("T_2"))
    SAY("sl_c6_crepe_07", "sl_crepe")
    SAY("sl_c6_crepe_08", "sl_crepe")
    SAY("sl_c6_crepe_09", "mario")
    SAY("sl_c6_crepe_10", "sl_crepe")
    /* the pull: the compass flies from Luigi's pocket */
    CALL(evt_npc_set_anim, STRING("sl_crepe"), STRING("PU_1"))
    CALL(evt_snd_sfxon, STRING("SFX_BOSS_KAMEBABA_MAGIC1"), 0)
    CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
    ADD(LW(1), 30)
    LOOP(10)
        CALL(evt_eff64, STRING(""), STRING("stardust_n64"), 2, LW(0), LW(1), LW(2), 0, 0, 0, 0, 0, 0, 0, 0)
        ADD(LW(0), 18)
        WAIT_FRAMES(4)
    END_LOOP
    CALL(evt_npc_set_anim, STRING("sl_crepe"), STRING("G_1"))
    WAIT_MS(800)
    SAY("sl_c6_crepe_11", "sl_crepe")
    /* the fight: one battle, three phases */
    CALL(evt_sl6_boss_arm, 3)
    CALL(evt_npc_set_battle_info, STRING("sl_crepe"), 5)
    CALL(evt_npc_battle_start, STRING("sl_crepe"))
    CALL(evt_npc_wait_battle_end)
    CALL(evt_sl6_boss_arm, 0)
    CALL(evt_npc_get_battle_result, LW(0))
    IF_INT_NOT_EQUAL(LW(0), 1)
        CALL(evt_mario_key_onoff, 1)
        RETURN
    END_IF
    CALL(evt_npc_set_position, STRING("sl_crepe"), LOFT_CREPE_STEP)
    CALL(evt_npc_set_anim, STRING("sl_crepe"), STRING("K_1"))
    CALL(evt_npc_set_ry, STRING("sl_eclair"), 270)
    CALL_CHILD(jin_71_after)
    RETURN
EVT_END()

static EVT_BEGIN(jin_71_init)
    CALL(evt_snd_bgmoff, PSND_FADE_OUT)
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG4_JIN1"))
    CALL(evt_snd_set_rev_mode, 2)
    CALL(sl6_soot, SLF(F_GOO))
    SET_VALUE(LW(0), PTR(jin_71_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    /* the King waits in the middle, goo dripping */
    CALL(evt_npc_entry, STRING("sl_king"), STRING("c_sl_chestnut_king"))
    CALL(evt_npc_set_position, STRING("sl_king"), LOFT_KING_X, 0, LOFT_Z)
    CALL(evt_npc_set_ry, STRING("sl_king"), 270)
    CALL(evt_npc_set_anim, STRING("sl_king"), STRING("S_1"))
    SPAWN_EVENT(jin_71_scene)
    RETURN
EVT_END()

/* ======================================================================
 * aaa_60, Home (aaa_00): the epilogue, the dream, the tag
 * ====================================================================== */

static BeroEntry aaa_60_bero[] = {
    {"dokan_1", 2, 0, BERO_DIR_DOWN, BERO_POS_AUTO, 0, 0, -1, NULL, 6, NULL, NULL, NULL, 1, 1, 0, 0},
    {0},
};

/* each friend fades in, waves, and fades out (LW(0) the NPC, LW(1) its model) */
static EVT_BEGIN(dream_one)
    CALL(evt_npc_entry, LW(0), LW(1))
    /* over the table, in front of what's on it */
    CALL(evt_npc_set_position, LW(0), -40, 72, -190)
    CALL(evt_npc_set_anim, LW(0), STRING("Z_1"))
    SET_VALUE(LW(2), 0)
    LOOP(20)
        ADD(LW(2), 12)
        CALL(evt_npc_set_color, LW(0), 255, 255, 255, LW(2))
        WAIT_FRAMES(2)
    END_LOOP
    WAIT_MS(900)
    LOOP(20)
        SUBTRACT(LW(2), 12)
        CALL(evt_npc_set_color, LW(0), 255, 255, 255, LW(2))
        WAIT_FRAMES(2)
    END_LOOP
    CALL(evt_npc_delete, LW(0))
    RETURN
EVT_END()

/* where Luigi lies: the open floor left of the table, in the camera's middle */
#define TAG_X -126
#define TAG_Z -215

/* Blooey's tag: a held still, washed grey (the wash reaches the characters too, which is
 * what greys them), LW(10) which: 1 the goo swim, 2 the Smite */
static EVT_BEGIN(tag_still)
    CALL(evt_npc_set_position, STRING("sl_tag"), 0, -1000, 0)
    CALL(evt_snd_sfxon, STRING("SFX_STG0_GESO_CRASH1"), 0)
    IF_INT_EQUAL(LW(10), 1)
        CALL(evt_npc_entry, STRING("sl_st0"), STRING("c_sl_goo_pit"))
        CALL(evt_npc_flag_onoff, 1, STRING("sl_st0"), NPC_FLAG_NO_BILLBOARD | NPC_FLAG_NO_SHADOW)
        CALL(evt_npc_set_position, STRING("sl_st0"), TAG_X, 31, TAG_Z)
        CALL(evt_npc_set_scale, STRING("sl_st0"), FLOAT(1.2f), FLOAT(1.2f), FLOAT(1.2f))
        CALL(evt_npc_set_anim, STRING("sl_st0"), STRING("S_1"))
        CALL(evt_npc_entry, STRING("sl_st1"), STRING("c_lp_gesso"))
        CALL(evt_npc_set_position, STRING("sl_st1"), TAG_X, 30, TAG_Z + 4)
        CALL(evt_npc_set_anim, STRING("sl_st1"), STRING("F_1"))
        CALL(evt_npc_entry, STRING("sl_st2"), STRING("c_luigi_pc"))
        CALL(evt_npc_set_position, STRING("sl_st2"), TAG_X - 2, 58, TAG_Z + 6)
        CALL(evt_npc_set_anim, STRING("sl_st2"), STRING("M_D_6"))
    ELSE
        CALL(evt_npc_entry, STRING("sl_st1"), STRING("c_sl_nightmare"))
        CALL(evt_npc_set_position, STRING("sl_st1"), TAG_X + 45, 30, TAG_Z - 12)
        CALL(evt_npc_set_ry, STRING("sl_st1"), 270)
        CALL(evt_npc_set_anim, STRING("sl_st1"), STRING("C_1"))
        CALL(evt_npc_entry, STRING("sl_st2"), STRING("c_luigi_pc"))
        CALL(evt_npc_set_position, STRING("sl_st2"), TAG_X - 25, 30, TAG_Z + 4)
        CALL(evt_npc_set_ry, STRING("sl_st2"), 90)
        CALL(evt_npc_set_anim, STRING("sl_st2"), STRING("M_H_7"))
    END_IF
    WAIT_FRAMES(6)
    /* held: every pose stopped where it is */
    CALL(sl6_still_hold, 1)
    CALL(evt_sl_map_wash, 0, 0x8C8C8C, 85)
    WAIT_MS(2200)
    CALL(sl6_still_hold, 0)
    CALL(evt_npc_check_delete, STRING("sl_st0"))
    CALL(evt_npc_check_delete, STRING("sl_st1"))
    CALL(evt_npc_check_delete, STRING("sl_st2"))
    CALL(evt_sl_map_look, 0, 0x202020, 0)
    CALL(evt_npc_set_position, STRING("sl_tag"), TAG_X, 30, TAG_Z)
    RETURN
EVT_END()

static EVT_BEGIN(aaa_60_scene)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_mario_kill_party, 0)
    CALL(evt_seq_wait, 2)
    CALL(evt_mario_set_pos, -85, 30, -310)
    CALL(evt_fade_entry, FADE_IN_BLACK, 1200, 0, 0, 0)
    CALL(evt_fade_end_wait)
    CALL(evt_mario_mov_pos2, -100, -280, FLOAT(60.0f))
    CALL(evt_mario_set_dir, 90, 0, 0)
    WAIT_MS(800)
    SAY("sl_c6_home_00", "mario")
    WAIT_MS(600)
    CALL(evt_mario_set_dir, 270, 0, 0)
    SAY("sl_c6_home_01", "mario")
    /* the armchair and the book */
    CALL(evt_mario_mov_pos2, -126, -224, FLOAT(50.0f))
    CALL(evt_luigi_story_pose, STRING("M_I_2"))
    WAIT_MS(1500)
    CALL(evt_luigi_story_pose, STRING("SL_FAINT"))
    WAIT_MS(1200)
    /* the dream */
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_ENDING3"))
    CALL(evt_fade_entry, FADE_OUT_WHITE, 2000, 0, 0, 0)
    CALL(evt_fade_end_wait)
    CALL(evt_fade_entry, FADE_IN_WHITE, 800, 0, 0, 0)
    CALL(evt_map_set_blend, 0, 255, 255, 255, 255)
    CALL(evt_sl_map_wash, 0, 0xFFFFFF, 70)
    SET_VALUE(LW(0), STRING("sl_d1"))
    SET_VALUE(LW(1), STRING("c_lp_gesso"))
    CALL_CHILD(dream_one)
    SET_VALUE(LW(0), STRING("sl_d2"))
    SET_VALUE(LW(1), STRING("c_lp_bom"))
    CALL_CHILD(dream_one)
    SET_VALUE(LW(0), STRING("sl_d3"))
    SET_VALUE(LW(1), STRING("c_lp_met"))
    CALL_CHILD(dream_one)
    SET_VALUE(LW(0), STRING("sl_d4"))
    SET_VALUE(LW(1), STRING("c_lp_pansy"))
    CALL_CHILD(dream_one)
    SET_VALUE(LW(0), STRING("sl_d5"))
    SET_VALUE(LW(1), STRING("c_lp_moa"))
    CALL_CHILD(dream_one)
    /* a figure in cream and gold who doesn't turn round */
    CALL(evt_npc_entry, STRING("sl_d6"), STRING("c_sl_eclair"))
    CALL(evt_npc_set_position, STRING("sl_d6"), 75, 30, -235)
    CALL(evt_npc_set_ry, STRING("sl_d6"), 90)
    CALL(evt_npc_set_anim, STRING("sl_d6"), STRING("S_1"))
    CALL(evt_npc_set_color, STRING("sl_d6"), 255, 255, 255, 150)
    WAIT_MS(2000)
    CALL(evt_npc_delete, STRING("sl_d6"))
    CALL(evt_sl_map_wash, 0, 0xFFFFFF, 35)
    /* he mumbles lying down: an unseen speaker at his head (spoken by "mario" he'd stand to talk) */
    CALL(evt_npc_entry, STRING("sl_mumble"), STRING("c_luigi_pc"))
    CALL(evt_npc_set_position, STRING("sl_mumble"), -120, 30, -224)
    CALL(evt_npc_set_color, STRING("sl_mumble"), 255, 255, 255, 0)
    SAY("sl_c6_dream_00", "sl_mumble")
    CALL(evt_npc_delete, STRING("sl_mumble"))
    WAIT_MS(800)
    /* 15. The tag: Blooey's version, burnt */
    CALL(evt_fade_entry, FADE_OUT_BLACK, 1000, 0, 0, 0)
    CALL(evt_fade_end_wait)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_RUIJI1"))
    CALL(evt_sl_map_look, 0, 0x202020, 0)
    /* Luigi hidden where he lies (the camera stays on him): Blooey has the room's middle */
    CALL(evt_mario_dispflag_onoff, 1, MARIO_DISP_FLAG_HIDDEN)
    CALL(evt_npc_entry, STRING("sl_tag"), STRING("c_lp_gesso"))
    CALL(evt_npc_set_position, STRING("sl_tag"), TAG_X, 30, TAG_Z)
    CALL(evt_npc_set_anim, STRING("sl_tag"), STRING("S_1"))
    CALL(evt_fade_entry, FADE_IN_BLACK, 800, 0, 0, 0)
    CALL(evt_fade_end_wait)
    CALL(evt_sl_prompt, STRING("sl_c6_tag_caption"), 2500)
    CALL(evt_npc_set_anim, STRING("sl_tag"), STRING("T_1"))
    SAY("sl_c6_tag_00", "sl_tag")
    SAY("sl_c6_tag_01", "sl_tag")
    /* still: Blooey swimming the goo, Luigi on his back, mouth wide open */
    SET_VALUE(LW(10), 1)
    CALL_CHILD(tag_still)
    SAY("sl_c6_tag_02", "sl_tag")
    SAY("sl_c6_tag_03", "sl_tag")
    /* still: Luigi's hammer coming down on the Nightmare, Blooey nowhere in it */
    SET_VALUE(LW(10), 2)
    CALL_CHILD(tag_still)
    SAY("sl_c6_tag_04", "sl_tag")
    CALL(evt_npc_set_anim, STRING("sl_tag"), STRING("S_1"))
    WAIT_MS(500)
    SAY("sl_c6_tag_05", "sl_tag")
    CALL(evt_fade_entry, FADE_OUT_BLACK, 800, 0, 0, 0)
    CALL(evt_fade_end_wait)
    CALL(evt_npc_delete, STRING("sl_tag"))
    CALL(sl6_soot, 0)
    /* the ending, back in the house: Luigi up and showing, the room as it was */
    CALL(evt_sl_map_look, 0, 0xFFFFFF, 0)
    CALL(evt_luigi_story_pose, 0)
    CALL(evt_mario_set_pose, STRING("M_S_1"))
    CALL(evt_mario_dispflag_onoff, 0, MARIO_DISP_FLAG_HIDDEN)
    CALL(evt_fade_entry, FADE_IN_BLACK, 500, 0, 0, 0)
    CALL(evt_fade_end_wait)
    SET_VALUE(SLF(F_DONE), 1)
    CALL(evt_sl_chapter_end)
    RETURN
EVT_END()

static EVT_BEGIN(aaa_60_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_MARIO_HOUSE1"))
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_END_AAA1"))
    CALL(evt_fade_entry, FADE_OUT_BLACK, 0, 0, 0, 0)
    CALL(sl6_soot, SLF(F_GOO))
    SET_VALUE(LW(0), PTR(aaa_60_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    /* inside, at night */
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("S_aaa_00"), MAPOBJ_FLAG_HIDE)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 0, STRING("S_naka"), MAPOBJ_FLAG_HIDE)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 0, STRING("S_ie_door"), MAPOBJ_FLAG_HIDE)
    CALL(evt_map_set_blend, 0, 150, 160, 220, 255)
    SPAWN_EVENT(aaa_60_scene)
    RETURN
EVT_END()

/* ======================================================================
 * The chapter
 * ====================================================================== */

/* seven of the bag's ten (ch6.md's list less a Super Shroom, a Maple Syrup and the Ice Storm):
 * the tower's chests and blocks hold five more, which a full bag would make him toss */
static const short kItems6[] = {
    ITEM_SUPER_SHROOM, ITEM_ULTRA_SHROOM, ITEM_JAMMIN_JELLY, ITEM_MAPLE_SYRUP, ITEM_LIFE_SHROOM,
    ITEM_THUNDER_RAGE, ITEM_FIRE_FLOWER, 0,
};
static const short kBadges6[] = {
    ITEM_POWER_BOUNCE, ITEM_POWER_SMASH, ITEM_ICE_SMASH, ITEM_CLOSE_CALL, ITEM_HP_PLUS, ITEM_MULTIBOUNCE,
    ITEM_CHARGE, ITEM_SPIKE_SHIELD, ITEM_TIMING_TUTOR, ITEM_SUPER_APPEAL, ITEM_QUAKE_HAMMER, ITEM_SLEEPY_STOMP,
    ITEM_HAPPY_FLOWER, ITEM_FEELING_FINE, ITEM_DEFEND_PLUS, ITEM_DAMAGE_DODGE, ITEM_FP_PLUS, 0,
};

static const PortSlMap kMaps6[] = {
    {"gra_60", gra_60_init, "gra_06", 1},
    {"jin_60", jin_60_init, "jin_00", 1},
    {"jin_61", jin_61_init, "jin_01", 1},
    {"jin_62", jin_62_init, "jin_02", 1},
    {"jin_63", jin_63_init, "jin_03", 1},
    {"jin_64", jin_64_init, "jin_05", 1},
    {"jin_65", jin_65_init, "jin_06", 1},
    {"jin_66", jin_66_init, "jin_07", 1},
    {"jin_67", jin_67_init, "jin_08", 1},
    {"jin_68", jin_68_init, "jin_11", 1},
    {"jin_69", jin_69_init, "jin_10", 1},
    {"jin_70", jin_70_init, "jin_09", 1},
    {"jin_71", jin_71_init, "jin_04", 1},
    {"aaa_60", aaa_60_init, "aaa_00", 1},
    {NULL, NULL, NULL, 0},
};


/* unit stats in chapter 6 (ch6.md, "Unit overrides") */
static const PortSlUnitStats kStats6[] = {
    {UNIT_BOO, 8, 4, 0, -1, 33}, /* (8: two landed jumps, 2 + 2 each, at level 16) */
    {UNIT_SWOOPER, 6, 3, 0, -1, 32},
    {UNIT_BUZZY_BEETLE, 6, 4, 0, 1, 32}, /* (defense 1: Luigi has no Quake to flip them with; the hammer does 3) */
    {UNIT_SPIKE_TOP, 6, 4, 0, 1, 33},
    {UNIT_ATOMIC_BOO, 30, 4, 0, 0, 48},
    {0},
};

extern const char* const portSlCh6Text[];
void portSlCh6BossRegister(void); /* superluigi_ch06_boss_aurora.c */

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

/* The tower's Boos fade in and out in the field, on a three-second cycle, as they do in battle
 * (ch6.md, "First strikes"); Screamy's Look shows them for eight seconds (sl6_boos_seen). */
static int sBooSeen;

static USER_FUNC(sl6_boos_seen) {
    (void)event;
    (void)isFirstCall;
    sBooSeen = 8 * 60;
    return EVT_RETURN_DONE;
}

static void boosFrame(void) {
    static u32 sT;
    NpcWork* w;
    int i, a, c;
    if (portSuperLuigiChapter() != 6 || gp == NULL || gp->inBattle || strncmp(gp->currentMapName, "jin_", 4) != 0) {
        sBooSeen = 0;
        return;
    }
    c = (int)(sT++ % 180);
    if (sBooSeen > 0) {
        sBooSeen--;
        a = 255;
    } else if (c < 60) {
        a = 255;
    } else if (c < 90) {
        a = 255 - (c - 60) * (255 - 24) / 30;
    } else if (c < 150) {
        a = 24;
    } else {
        a = 24 + (c - 150) * (255 - 24) / 30;
    }
    w = npcGetWorkPtr();
    for (i = 0; i < w->total; i++) {
        NpcEntry* npc = &w->entries[i];
        /* tests: $TTYD_SL6_NOFOES clears the rooms' field enemies (walking the rooms, the puzzles) */
        if ((npc->flags & NPC_FLAG_USED) && (npc->flags & NPC_FLAG_ENEMY) && getenv("TTYD_SL6_NOFOES") != NULL) {
            npcDelete(npc);
            continue;
        }
        if ((npc->flags & NPC_FLAG_USED) && (npc->flags & NPC_FLAG_ENEMY) && npc->tribe != NULL &&
            npc->tribe->modelName != NULL && strcmp(npc->tribe->modelName, "c_teresa") == 0) {
            npc->color.a = (u8)a;
        }
    }
}

static void setup6(void) {
    portSlCh6BossRegister();
    portModHostAddHook(1, kSlot_seqMain, (void*)boosFrame);
}

static const PortSlChapter kChapter6 = {
    "Hatesong Tower", "gra_60", "sw_bero", "gor_luigi_talk_0006", PORT_SL_FRIEND_NONE, 16, 35, 25, 21, 2, 2, 250,
    kItems6, kBadges6,
    PORT_SL_BIT(PORT_SL_FRIEND_BLOOEY) | PORT_SL_BIT(PORT_SL_FRIEND_JERRY) | PORT_SL_BIT(PORT_SL_FRIEND_TORQUE) |
        PORT_SL_BIT(PORT_SL_FRIEND_HAYZEE) | PORT_SL_BIT(PORT_SL_FRIEND_SCREAMY),
    kMaps6, portSlCh6Text, kStats6, setup6, "sl6_diary",
};

const PortSlChapter* portSlChapter6(void) {
    return &kChapter6;
}
