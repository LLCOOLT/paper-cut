/* Super Luigi's chapter 5, "Rapturous Ruins" (docs/super_luigi/ch5.md):
 * Grimble Forest on Boggly Woods (win_*), and the white ruin on Shhwonk
 * Fortress (hei_*) and the Rogueport Sewers' old rooms (tik_*), washed white
 * (map looks). Screamy joins in the Sleeper's Chamber (superluigi_screamy_aurora.c
 * has his Float and Look); the earlier friends' abilities open the rest
 * (Blooey's Swim, Jerry's Blast, Torque's Tune-up, Hayzee's Sing: points of
 * the framework's PORT_SL_ABILITY_*; while a friend's file isn't in the build,
 * checking the thing does what he would, so the chapter can always be played
 * through). No boss: the three lenses, the Room of Nothing, the sneeze,
 * Cranberry, the dark wood, and Screamy's version. */
#include "port/superluigi.h"
#include "port/superluigi_evt.h"
#include "port/mod_host.h"
#include "port/hook_list.h"
#include "battle/battle_unit.h"
#include "driver/hitdrv.h"
#include "evt/evt_hit.h"
#include "mario/mario.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "system.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

USER_FUNC(evt_luigi_story_pose);

/* the pad's buttons (dolphin/pad.h) */
#ifndef PAD_BUTTON_A
#define PAD_BUTTON_A 0x0100
#define PAD_BUTTON_B 0x0200
#define PAD_BUTTON_X 0x0400
#define PAD_BUTTON_Y 0x0800
#endif
USER_FUNC(evt_sl_float_point);
USER_FUNC(evt_sl_look_point);
int portSlRiding(void); /* superluigi_blooey_aurora.c */

/* the game's tribe and AI names (Shift-JIS, port/include/port/superluigi_sjis.h's way) */
#define SJ5_DARK_PUFF "\x83" "\x82" "\x83" "\x6D" "\x83" "\x4E" "\x83" "\x8D" "\x83" "\x4E" "\x83" "\x8D" "\x83" "\x4E" "\x83" "\x82" "\x81" "\x5B" "\x83" "\x93"
#define SJ5_PUFF_AI "\x83" "\x70" "\x83" "\x5E" "\x83" "\x4E" "\x83" "\x8A"
#define SJ5_PALE_PIRANHA "\x83" "\x70" "\x83" "\x62" "\x83" "\x4E" "\x83" "\x93" "\x83" "\x74" "\x83" "\x89" "\x83" "\x8F" "\x81" "\x5B" "\x81" "\x69" "\x8A" "\x44" "\x81" "\x6A"
#define SJ5_PIRANHA_AI "\x83" "\x70" "\x83" "\x62" "\x83" "\x4E" "\x83" "\x93" "\x83" "\x74" "\x83" "\x89" "\x83" "\x8F" "\x81" "\x5B"
#define SJ5_CLEFT_W "\x83" "\x82" "\x83" "\x6D" "\x83" "\x4E" "\x83" "\x8D" "\x83" "\x56" "\x83" "\x93" "\x83" "\x47" "\x83" "\x82" "\x83" "\x93"
#define SJ5_CLEFT "\x83" "\x56" "\x83" "\x93" "\x83" "\x47" "\x83" "\x82" "\x83" "\x93"
#define SJ5_BALD_CLEFT "\x83" "\x56" "\x83" "\x93" "\x83" "\x6D" "\x83" "\x58" "\x83" "\x50"
#define SJ5_FUZZY "\x83" "\x60" "\x83" "\x87" "\x83" "\x8D" "\x83" "\x7B" "\x83" "\x93"
#define SJ5_GOLD_FUZZY "\x83" "\x53" "\x81" "\x5B" "\x83" "\x8B" "\x83" "\x68" "\x83" "\x60" "\x83" "\x87" "\x83" "\x8D" "\x83" "\x7B" "\x83" "\x93"
#define SJ5_BRISTLE "\x83" "\x67" "\x83" "\x51" "\x83" "\x5F" "\x83" "\x8B" "\x83" "\x7D" "\x81" "\x5B"
#define SJ5_ELDER "\x83" "\x6D" "\x83" "\x52" "\x91" "\xBA" "\x91" "\xBA" "\x92" "\xB7" /* Petalburg's mayor */
#define SJ5_CROW "\x83" "\x4A" "\x83" "\x89" "\x83" "\x58"
#define SJ5_SPOT_TRIBE "\x83" "\x4C" "\x83" "\x62" "\x83" "\x4E" /* a talkable tribe for our people */
#define SJ5_TOAD "\x83" "\x4C" "\x83" "\x6D" "\x83" "\x73" "\x83" "\x49" /* the Toad tribe: talkable, in c_kinopio's names (KNP_S_1), for the spots */

/* Models: the game's own people, and the chapter's props (docs/LUIGI_ANIMS.md) */
#define M_CRANBERRY "c_sl_cranberry"
#define M_SPOT "c_kinopio"         /* an unseen spot to check (hidden: murals, sockets, stones, the gear) */
#define M_THORNS "c_togedaruma_b" /* the Dark Bristle curled asleep across the path */
#define M_CROW "c_f_karas"         /* the white crow on the button (Twilight Town's crow, washed white) */
#define M_MOSSBEARD "c_nokoson"    /* Old Mossbeard (Petalburg's bearded old mayor) */
/* chapter 5's props (docs/LUIGI_ANIMS.md, "Chapter 5's props") */
#define M_SOCKET_MOON "c_sl_socket_moon"
#define M_SOCKET_SUN "c_sl_socket_sun"
#define M_SOCKET_TEAR "c_sl_socket_tear"
#define M_MURAL "c_sl_mural"
#define M_EDGE "c_sl_look_edge_200"
#define M_EDGE_V "c_sl_look_edge_100_v"
#define M_GEAR_GATE "c_sl_gear_gate"
#define M_SIGNPOST "c_sl_signpost"

/* a prop: an NPC drawn as it is, in its pose, not turning, not in the way of anything */
#define PROP(name, model, x, y, z, pose)                                                   CALL(evt_npc_entry, STRING(name), STRING(model))                                      CALL(evt_npc_set_position, STRING(name), x, y, z)                                     CALL(evt_npc_set_anim, STRING(name), STRING(pose))                                    CALL(evt_npc_flag_onoff, 1, STRING(name), NPC_FLAG_NO_SHADOW | NPC_FLAG_NO_TURN_TO_MARIO | NPC_FLAG_INTANGIBLE)

/* the chapter's own flags: the framework's bank (SL_GSWF(5, n), docs/SUPER_LUIGI.md "Flags") */
#define SLF5(n) SL_GSWF(5, n)
enum {
    F_OPENED, F_MOSS, F_BTN1, F_BTN2, F_BTN3, F_TRADE1, F_TRADE2, F_TRADE3,
    F_L50_PIECE, F_L51_ITEM, F_L51_COIN, F_L52_STONE, F_L52_CHEST, F_L52_OWL, F_L53_THORNS, F_L53_CHEST,
    F_L53_COIN1, F_L53_COIN2, F_L54_POST, F_L54_PIECE, F_L54_DOME, F_QUIZ, F_GATE_PIECE, F_SCREAMY,
    F_FAKEDOOR, F_H51_SHINE, F_H51_ITEM, F_MURAL6, F_MURAL_OPEN, F_REFUND, F_GEAR_CLEAN, F_GEAR_OPEN,
    F_MOON, F_MOON_PIECE, F_ECHO1, F_ECHO2, F_ECHO3, F_FPPLUS, F_SUN, F_GLIMMER,
    F_SUN_SHINE, F_DODGE, F_LAKE_SEEN, F_LAKE_PIECE, F_TEAR, F_NOTHING_SEEN, F_NOTHING_PIECE, F_NOTHING_SHINE,
    F_SET_MOON, F_SET_SUN, F_SET_TEAR, F_PIECE, F_END, F_MURALS_READ, F_H50_SEEN, F_CHAMBER_LOOKED,
    F_H55_SEEN, F_ECHO_SEEN, F_LAKE_INTRO, F_MOON_SEEN, F_SHINE_TRADE, F_GEAR_TOLD, F_NEVER,
};

#define FRIEND_JERRY_SLOT 3
#define FRIEND_TORQUE_SLOT 2
#define FRIEND_HAYZEE_SLOT 5
#define SCREAMY_SLOT 6
#define BLOOEY_SLOT 1

/* ======================================================================
 * Helpers in C
 * ====================================================================== */

/* $TTYD_SL_SURVEY: the room's floor as a height map, and its exits, to the log (building the rooms) */
static USER_FUNC(sl5_survey) {
    static const char* const kHits[] = {"w_bero", "e_bero", "n_bero", "s_bero", "dokan", "dokan1", "dokan2",
                                        "dokan_1", "dokan_2", "w_bero_1", "w_bero_2", "w_bwro_2", NULL};
    MarioWork* m = marioGetPtr();
    char row[64];
    int i, x, z;
    (void)event;
    (void)isFirstCall;
    if (getenv("TTYD_SL_SURVEY") == NULL) {
        return EVT_RETURN_DONE;
    }
    printf("[survey] %s luigi %.0f %.0f %.0f\n", gp->currentMapName, m->position.x, m->position.y, m->position.z);
    for (i = 0; kHits[i] != NULL; i++) {
        if (hitNameToPtr(kHits[i]) != NULL) {
            Vec p;
            hitObjGetPos(kHits[i], &p);
            printf("[survey] hit %s %.0f %.0f %.0f\n", kHits[i], p.x, p.y, p.z);
        }
    }
    for (z = -480; z <= 480; z += 40) {
        int n = 0;
        for (x = -1000; x <= 1000 && n < 62; x += 40) {
            f32 hx, hy, hz, d = 1000.0f, nx, ny, nz;
            char c = ' ';
            if (hitCheckFilter(NULL, &hx, &hy, &hz, &d, &nx, &ny, &nz, (f32)x, 600.0f, (f32)z, 0.0f, -1.0f, 0.0f) !=
                NULL) {
                int h = (int)(hy / 25.0f);
                c = hy < -5.0f ? '-' : h < 10 ? (char)('0' + h) : h < 36 ? (char)('a' + h - 10) : '#';
            }
            row[n++] = c;
        }
        row[n] = 0;
        printf("[survey] z%5d |%s|\n", z, row);
    }
    /* the back lanes are narrower than that grid: their heights, every 20 */
    for (x = -300; x <= 400; x += 20) {
        f32 hx, hy, hz, d = 1000.0f, nx, ny, nz;
        if (hitCheckFilter(NULL, &hx, &hy, &hz, &d, &nx, &ny, &nz, (f32)x, 600.0f, -449.0f, 0.0f, -1.0f, 0.0f) != NULL) {
            printf("[survey] lane x%d y%.0f\n", x, hy);
        } else {
            printf("[survey] lane x%d none\n", x);
        }
    }
    fflush(stdout);
    return EVT_RETURN_DONE;
}

/* in: a friend (PORT_SL_FRIEND_*); out: 1 when his file is in the build (his ability can be used) */
static USER_FUNC(sl5_has_friend) {
    (void)isFirstCall;
    evtSetValue(event, event->args[1], portSlGetFriend((int)evtGetValue(event, event->args[0])) != NULL);
    return EVT_RETURN_DONE;
}

/* out: 1 while Luigi rides Blooey (his Swim) */
static USER_FUNC(sl5_riding) {
    (void)isFirstCall;
    evtSetValue(event, event->args[0], portSlRiding());
    return EVT_RETURN_DONE;
}

/* --- drops: where the floor isn't (the ruin's chasms, the lake, the Nothing room's edges) ---
 * Luigi below y in a rectangle is lifted back to a place, as the game's own fall does, no harm. */
#define DROPS 4
static struct {
    int on;
    f32 x0, z0, x1, z1, y;
    f32 sx, sy, sz;
} sDrop[DROPS];
static char sDropMap[16];
static int sDropping;

/* in: id, x0, z0, x1, z1, the height under which he has fallen, and where he comes back (x, y, z) */
static USER_FUNC(sl5_drop_zone) {
    EvtWord* a = event->args;
    int i = (int)evtGetValue(event, a[0]);
    (void)isFirstCall;
    if (strcmp(sDropMap, gp->currentMapName) != 0) {
        memset(sDrop, 0, sizeof(sDrop));
        snprintf(sDropMap, sizeof(sDropMap), "%s", gp->currentMapName);
    }
    if (i < 0 || i >= DROPS) {
        return EVT_RETURN_DONE;
    }
    sDrop[i].on = 1;
    sDrop[i].x0 = (f32)evtGetValue(event, a[1]);
    sDrop[i].z0 = (f32)evtGetValue(event, a[2]);
    sDrop[i].x1 = (f32)evtGetValue(event, a[3]);
    sDrop[i].z1 = (f32)evtGetValue(event, a[4]);
    sDrop[i].y = (f32)evtGetValue(event, a[5]);
    sDrop[i].sx = (f32)evtGetValue(event, a[6]);
    sDrop[i].sy = (f32)evtGetValue(event, a[7]);
    sDrop[i].sz = (f32)evtGetValue(event, a[8]);
    return EVT_RETURN_DONE;
}

static USER_FUNC(sl5_drop_where) {
    EvtWord* a = event->args;
    int i;
    (void)isFirstCall;
    for (i = 0; i < DROPS; i++) {
        if (sDrop[i].on && sDrop[i].sx != 0.0f) {
            break;
        }
    }
    i = sDropping - 1;
    if (i < 0 || i >= DROPS) {
        i = 0;
    }
    evtSetValue(event, a[0], (s32)sDrop[i].sx);
    evtSetValue(event, a[1], (s32)sDrop[i].sy);
    evtSetValue(event, a[2], (s32)sDrop[i].sz);
    return EVT_RETURN_DONE;
}

static USER_FUNC(sl5_drop_done) {
    (void)event;
    (void)isFirstCall;
    sDropping = 0;
    return EVT_RETURN_DONE;
}

static EVT_BEGIN(sl5_drop_back)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_snd_sfxon, STRING("SFX_MARIO_FALL1"), 0)
    WAIT_MS(300)
    CALL(evt_fade_out, 12)
    CALL(sl5_drop_where, LW(0), LW(1), LW(2))
    CALL(evt_mario_set_pos, LW(0), LW(1), LW(2))
    CALL(evt_party_set_pos, 0, LW(0), LW(1), LW(2))
    WAIT_FRAMES(4)
    CALL(evt_fade_in, 12)
    CALL(evt_mario_key_onoff, 1)
    CALL(sl5_drop_done)
    RETURN
EVT_END()

static void dropFrame(void) {
    MarioWork* m;
    int i;
    if (sDropping || gp == NULL || gp->inBattle || portSuperLuigiChapter() != 5 ||
        strcmp(sDropMap, gp->currentMapName) != 0 ||
        seqGetSeq() != SEQ_GAME) {
        return;
    }
    m = marioGetPtr();
    for (i = 0; i < DROPS; i++) {
        if (sDrop[i].on && m->position.x > sDrop[i].x0 && m->position.x < sDrop[i].x1 &&
            m->position.z > sDrop[i].z0 && m->position.z < sDrop[i].z1 && m->position.y < sDrop[i].y) {
            sDropping = i + 1;
            evtEntry(sl5_drop_back, 0, 0);
            return;
        }
    }
}

/* in: button; out: 1 if the A/B/X/Y pressed this frame matches it (2 for another of them), 0 none */
static USER_FUNC(sl5_note) {
    const u32 want = (u32)evtGetValue(event, event->args[0]);
    const u32 trg = keyGetButtonTrg(0) & (PAD_BUTTON_A | PAD_BUTTON_B | PAD_BUTTON_X | PAD_BUTTON_Y);
    (void)isFirstCall;
    evtSetValue(event, event->args[1], trg == 0 ? 0 : (trg & want) ? 1 : 2);
    return EVT_RETURN_DONE;
}

/* ======================================================================
 * Shared scripts
 * ====================================================================== */

#define BERO(hit, next, nextBero) \
    {hit, 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, next, nextBero, 1, 1, 0, 0}
#define PIPE(hit, type, dir, caseType, next, nextBero) \
    {hit, type, 0, dir, BERO_POS_AUTO, 0, 0, -1, NULL, caseType, NULL, next, nextBero, 1, 1, 0, 0}

#define SAY(tag) CALL(evt_msg_print, 0, STRING(tag), 0, STRING("mario"))
#define PARTY(tag) CALL(evt_msg_print, 0, STRING(tag), 0, STRING("party"))
#define NPC(tag, who) CALL(evt_msg_print, 0, STRING(tag), 0, STRING(who))
#define SYS(tag) CALL(evt_msg_print, 0, STRING(tag), 0, 0)

/* a line of Screamy's: from Screamy if he's out, else from his NPC in the scene ("sl_screamy"), else none */
static EVT_BEGIN(sl5_screamy_says)
    CALL(evt_mario_get_party, LW(15))
    IF_INT_EQUAL(LW(15), SCREAMY_SLOT)
        CALL(evt_msg_print, 0, LW(14), 0, STRING("party"))
    ELSE
        CALL(evt_msg_print, 0, LW(14), 0, 0)
    END_IF
    RETURN
EVT_END()
#define SCREAMY(tag) SET_VALUE(LW(14), STRING(tag)) CALL_CHILD(sl5_screamy_says)

/* a hidden thing glints where LW(10), LW(11), LW(12) are */
static EVT_BEGIN(sl5_glint)
    CALL(evt_snd_sfxon, STRING("SFX_STAR_POINT_GET1"), 0)
    CALL(evt_eff, STRING(""), STRING("stardust"), 2, LW(10), LW(11), LW(12), 30, 30, 30, 80, 0, 0, 0, 0)
    CALL(evt_eff, 0, STRING("small_star"), 3, LW(10), LW(11), LW(12), 0, -1, 0, 3, 0, 0, 0, 0)
    RETURN
EVT_END()

/* an unseen spot to check with A: an NPC (hidden) with a talk script */
#define SPOT(name, x, y, z, script)                                                   \
    CALL(evt_npc_entry, STRING(name), STRING(M_SPOT))                                 \
    CALL(evt_npc_set_tribe, STRING(name), STRING(SJ5_TOAD))                           \
    CALL(evt_npc_set_position, STRING(name), x, y, z)                                 \
    CALL(evt_npc_set_width, STRING(name), 50)                                           CALL(evt_npc_flag_onoff, 1, STRING(name), NPC_FLAG_HIDDEN | NPC_FLAG_NO_SHADOW | NPC_FLAG_NO_TURN_TO_MARIO | NPC_FLAG_INTANGIBLE) \
    CALL(evt_npc_change_interrupt, STRING(name), 6, PTR(script))

/* "Luigi found <LW(10)>": a key thing, held up */
static EVT_BEGIN(sl5_found)
    CALL(evt_snd_bgmon, PSND_FADE_OUT | 1, STRING("BGM_FF_GET_ITEM1"))
    CALL(evt_mario_set_pose, STRING("M_I_2"))
    CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
    ADD(LW(1), 50)
    CALL(evt_eff, 0, STRING("small_star"), 3, LW(0), LW(1), LW(2), 0, -1, 0, 3, 0, 0, 0, 0)
    CALL(evt_msg_print, 0, LW(10), 0, 0)
    CALL(evt_mario_set_pose, STRING("M_S_1"))
    RETURN
EVT_END()

/* a chest's badge or item (LW(10)) out of the chest LW(11) */
static EVT_BEGIN(sl5_chest)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_mobj_get_position, LW(11), LW(0), LW(1), LW(2))
    CALL(evt_item_entry, STRING("sl_chest_item"), LW(10), LW(0), LW(1), LW(2), ITEM_ENTRY_FIXED, -1, 0)
    CALL(evt_item_flag_on, STRING("sl_chest_item"), ITEM_FLAG_BADGE_RETURN | ITEM_FLAG_NO_TOUCH)
    CALL(evt_mobj_wait_animation_end, LW(11))
    CALL(evt_item_get_item, STRING("sl_chest_item"))
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()
#define CHEST_EVT(name, box, item)          \
    static EVT_BEGIN(name)                  \
        SET_VALUE(LW(10), item)             \
        SET_VALUE(LW(11), STRING(box))      \
        CALL_CHILD(sl5_chest)               \
        RETURN                              \
    EVT_END()

/* the friends as people in a scene: the partner out goes away, Luigi's friends stand round him;
 * LW(0)-LW(2) Luigi's place. Screamy too when F_SCREAMY. */
static EVT_BEGIN(sl5_friends_in)
    CALL(evt_mario_get_party, LW(3))
    IF_INT_NOT_EQUAL(LW(3), 0)
        CALL(evt_party_get_pos, 0, LW(4), LW(5), LW(6))
        CALL(evt_mario_kill_party, 0)
    END_IF
    CALL(evt_npc_entry, STRING("sl_blooey"), STRING("c_lp_gesso"))
    CALL(evt_npc_entry, STRING("sl_jerry"), STRING("c_lp_bom"))
    CALL(evt_npc_entry, STRING("sl_torque"), STRING("c_lp_met"))
    CALL(evt_npc_entry, STRING("sl_hayzee"), STRING("c_lp_pansy"))
    SET_VALUE(LW(4), LW(0))
    SUBTRACT(LW(4), 60)
    SET_VALUE(LW(5), LW(2))
    ADD(LW(5), 20)
    CALL(evt_npc_set_position, STRING("sl_blooey"), LW(4), LW(1), LW(5))
    SUBTRACT(LW(4), 65)
    SUBTRACT(LW(5), 35)
    CALL(evt_npc_set_position, STRING("sl_torque"), LW(4), LW(1), LW(5))
    SET_VALUE(LW(4), LW(0))
    ADD(LW(4), 60)
    SET_VALUE(LW(5), LW(2))
    ADD(LW(5), 20)
    CALL(evt_npc_set_position, STRING("sl_hayzee"), LW(4), LW(1), LW(5))
    ADD(LW(4), 65)
    SUBTRACT(LW(5), 35)
    CALL(evt_npc_set_position, STRING("sl_jerry"), LW(4), LW(1), LW(5))
    CALL(evt_npc_set_ry, STRING("sl_hayzee"), 270)
    CALL(evt_npc_set_ry, STRING("sl_jerry"), 270)
    IF_INT_EQUAL(SLF5(F_SCREAMY), 1)
        CALL(evt_npc_entry, STRING("sl_screamy"), STRING("c_lp_moa"))
        SET_VALUE(LW(4), LW(0))
        SET_VALUE(LW(5), LW(2))
        SUBTRACT(LW(5), 70)
        CALL(evt_npc_set_position, STRING("sl_screamy"), LW(4), LW(1), LW(5))
    END_IF
    RETURN
EVT_END()

/* and back: the friends go, Screamy (or Blooey, before him) comes out at Luigi's side */
static EVT_BEGIN(sl5_friends_out)
    CALL(evt_npc_delete, STRING("sl_blooey"))
    CALL(evt_npc_delete, STRING("sl_jerry"))
    CALL(evt_npc_delete, STRING("sl_torque"))
    CALL(evt_npc_delete, STRING("sl_hayzee"))
    IF_INT_EQUAL(SLF5(F_SCREAMY), 1)
        CALL(evt_npc_get_position, STRING("sl_screamy"), LW(0), LW(1), LW(2))
        CALL(evt_npc_delete, STRING("sl_screamy"))
        CALL(evt_mario_set_party_pos, 0, SCREAMY_SLOT, LW(0), LW(1), LW(2))
    ELSE
        CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
        SUBTRACT(LW(0), 40)
        CALL(evt_mario_set_party_pos, 0, BLOOEY_SLOT, LW(0), LW(1), LW(2))
    END_IF
    RETURN
EVT_END()

/* a field ability nobody here can use yet (his file not in this build): LW(10) the friend,
 * LW(11) the line that says what happens instead; then LW(12), the point's script, runs */
static EVT_BEGIN(sl5_ability_or_hint)
    CALL(sl5_has_friend, LW(10), LW(0))
    IF_INT_EQUAL(LW(0), 1)
        CALL(evt_msg_print, 0, LW(13), 0, STRING("mario"))
    ELSE
        CALL(evt_msg_print, 0, LW(11), 0, STRING("mario"))
        SET_VALUE(LW(0), 0)
        CALL_CHILD(LW(12))
    END_IF
    RETURN
EVT_END()

/* the ruin's white: every room of it washed toward old white stone, its fog the same */
static EVT_BEGIN(sl5_white_room)
    CALL(evt_sl_map_wash, 0, 0xF4F2EA, 70)
    RETURN
EVT_END()

/* --- Mossbeard's buttons: out: LW(0) how many found, LW(1) how many traded --- */
static EVT_BEGIN(sl5_button_counts)
    SET_VALUE(LW(0), 0)
    SET_VALUE(LW(1), 0)
    IF_INT_EQUAL(SLF5(F_BTN1), 1)
        ADD(LW(0), 1)
    END_IF
    IF_INT_EQUAL(SLF5(F_BTN2), 1)
        ADD(LW(0), 1)
    END_IF
    IF_INT_EQUAL(SLF5(F_BTN3), 1)
        ADD(LW(0), 1)
    END_IF
    IF_INT_EQUAL(SLF5(F_TRADE1), 1)
        ADD(LW(1), 1)
    END_IF
    IF_INT_EQUAL(SLF5(F_TRADE2), 1)
        ADD(LW(1), 1)
    END_IF
    IF_INT_EQUAL(SLF5(F_TRADE3), 1)
        ADD(LW(1), 1)
    END_IF
    RETURN
EVT_END()

static EVT_BEGIN(sl5_got_button)
    CALL(evt_snd_sfxon, STRING("SFX_STAR_POINT_GET1"), 0)
    /* the Button held up: an item of no other use here, its icon the Button's (portIconTexOverride) */
    CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
    ADD(LW(1), 46)
    CALL(evt_item_entry, STRING("sl_btn"), ITEM_WEDDING_RING, LW(0), LW(1), LW(2), ITEM_ENTRY_FIXED, -1, 0)
    CALL(evt_item_flag_on, STRING("sl_btn"), ITEM_FLAG_BADGE_RETURN | ITEM_FLAG_NO_TOUCH)
    SET_VALUE(LW(10), STRING("sl_c5_button_got"))
    CALL_CHILD(sl5_found)
    CALL(evt_item_delete, STRING("sl_btn"))
    CALL_CHILD(sl5_button_counts)
    IF_INT_EQUAL(LW(0), 3)
        SYS("sl_c5_button_all")
    END_IF
    RETURN
EVT_END()

/* ======================================================================
 * win_l50, Grimble Edge (win_06): the arrival, and the dark wood at the end
 * ====================================================================== */

static BeroEntry win_l50_bero[] = {
    PIPE("dokan1", 2, BERO_DIR_DOWN, 6, NULL, NULL), BERO("e_bero", "win_l51", "w_bero"), {0}};

static EVT_BEGIN(win_l50_arrive)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_seq_wait, 2)
    CALL(evt_bero_exec_wait, 65536)
    CALL(evt_snd_bgmoff, PSND_FADE_HOLD)
    WAIT_MS(1200)
    SET_VALUE(SLF5(F_OPENED), 1)
    /* he hops down off the hollow stump */
    CALL(evt_mario_jump_pos, 0, 0, 80, 500, 0, 30)
    CALL(evt_mario_set_dir, 270, 0, 0)
    SET_VALUE(LW(0), 0)
    SET_VALUE(LW(1), 0)
    SET_VALUE(LW(2), 80)
    CALL_CHILD(sl5_friends_in)
    SYS("sl_c5_arrive_00")
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG2_WIN1"))
    SAY("sl_c5_arrive_01")
    NPC("sl_c5_arrive_02", "sl_hayzee")
    NPC("sl_c5_arrive_03", "sl_torque")
    CALL(evt_snd_sfxon, STRING("SFX_VOICE_MARIO_SURPRISED1"), 0)
    CALL(evt_npc_set_anim, STRING("sl_jerry"), STRING("J_1A"))
    CALL(evt_npc_set_anim, STRING("sl_blooey"), STRING("J_1A"))
    SYS("sl_c5_arrive_04")
    CALL(evt_npc_set_anim, STRING("sl_jerry"), STRING("S_1"))
    CALL(evt_npc_set_anim, STRING("sl_blooey"), STRING("S_1"))
    NPC("sl_c5_arrive_05", "sl_torque")
    SAY("sl_c5_arrive_06")
    NPC("sl_c5_arrive_07", "sl_jerry")
    CALL(evt_npc_set_anim, STRING("sl_jerry"), STRING("L_1"))
    CALL(evt_snd_sfxon, STRING("SFX_ENM_BOMB_SPARK1"), 0)
    WAIT_MS(1200)
    CALL(evt_mario_set_dir_npc, STRING("sl_jerry"))
    NPC("sl_c5_arrive_08", "sl_jerry")
    CALL(evt_npc_set_anim, STRING("sl_jerry"), STRING("S_1"))
    CALL_CHILD(sl5_friends_out)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* the dark wood (beats 12-14): the ruin is gone; the compass; Screamy's version */
static EVT_BEGIN(win_l50_night)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_mario_set_pos, 0, 0, 85)
    CALL(evt_mario_set_dir, 90, 0, 0)
    CALL(evt_seq_wait, 2)
    CALL(evt_bero_exec_wait, 65536)
    SET_VALUE(LW(0), 0)
    SET_VALUE(LW(1), 0)
    SET_VALUE(LW(2), 85)
    CALL_CHILD(sl5_friends_in)
    /* Screamy a little apart, at the front, looking up at the sky */
    CALL(evt_npc_set_position, STRING("sl_screamy"), 120, 0, 115)
    CALL(evt_npc_set_ry, STRING("sl_screamy"), 270)
    WAIT_MS(1500)
    SAY("sl_c5_woods_00")
    NPC("sl_c5_woods_01", "sl_hayzee")
    NPC("sl_c5_woods_02", "sl_torque")
    NPC("sl_c5_woods_03", "sl_jerry")
    NPC("sl_c5_woods_04", "sl_blooey")
    CALL(evt_mario_set_dir_npc, STRING("sl_screamy"))
    NPC("sl_c5_woods_05", "sl_screamy")
    CALL(evt_mario_mov_pos2, 60, 100, FLOAT(60.0f))
    CALL(evt_mario_set_dir_npc, STRING("sl_screamy"))
    SAY("sl_c5_woods_06")
    CALL(evt_npc_set_anim, STRING("sl_screamy"), STRING("E_1"))
    NPC("sl_c5_woods_07", "sl_screamy")
    CALL(evt_npc_set_anim, STRING("sl_screamy"), STRING("S_1"))
    /* the compass, and the voice */
    WAIT_MS(500)
    CALL(evt_mario_set_pose, STRING("M_I_2"))
    SYS("sl_c5_voice_00")
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_ORGEL1"))
    CALL(evt_map_set_blend, 0, 30, 30, 50, 255)
    CALL(evt_eff, STRING(""), STRING("stardust"), 2, 60, 50, 100, 40, 40, 40, 100, 0, 0, 0, 0)
    WAIT_MS(1000)
    SYS("sl_c5_voice_01")
    SYS("sl_c5_voice_02")
    SYS("sl_c5_voice_03")
    CALL(evt_map_set_blend, 0, 70, 80, 130, 255)
    CALL(evt_mario_set_pose, STRING("M_S_1"))
    CALL(evt_snd_bgmoff, PSND_FADE_OUT)
    SAY("sl_c5_voice_04")
    SAY("sl_c5_voice_05")
    SAY("sl_c5_voice_06")
    SAY("sl_c5_voice_07")
    /* the tag: as Screamy tells it */
    CALL(evt_fade_out, 30)
    CALL(evt_npc_delete, STRING("sl_blooey"))
    CALL(evt_npc_delete, STRING("sl_jerry"))
    CALL(evt_npc_delete, STRING("sl_torque"))
    CALL(evt_npc_delete, STRING("sl_hayzee"))
    CALL(evt_mario_dispflag_onoff, 1, MARIO_DISP_FLAG_HIDDEN)
    CALL(evt_npc_set_position, STRING("sl_screamy"), 0, 0, 100)
    CALL(evt_npc_set_ry, STRING("sl_screamy"), 270)
    CALL(evt_map_set_blend, 0, 120, 120, 150, 255)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_RUIJI1"))
    CALL(evt_fade_in, 30)
    CALL(evt_eff, STRING(""), STRING("stardust"), 2, 0, 30, 100, 20, 20, 20, 30, 0, 0, 0, 0)
    SYS("sl_c5_tag_caption")
    NPC("sl_c5_tag_00", "sl_screamy")
    NPC("sl_c5_tag_01", "sl_screamy")
    NPC("sl_c5_tag_02", "sl_screamy")
    /* the held still: Screamy screaming at one startled Cleft */
    CALL(evt_npc_entry, STRING("sl_still"), STRING("c_sinemon_w"))
    CALL(evt_npc_set_position, STRING("sl_still"), 70, 0, 100)
    CALL(evt_npc_set_ry, STRING("sl_still"), 90)
    CALL(evt_npc_set_anim, STRING("sl_still"), STRING("SIN_K_1"))
    CALL(evt_npc_set_anim, STRING("sl_screamy"), STRING("A_2B"))
    CALL(evt_map_set_blend, 0, 170, 170, 170, 255)
    CALL(evt_snd_sfxon, STRING("SFX_AUDIENCE_PANIC_SCREAM1"), 0)
    WAIT_MS(1600)
    CALL(evt_npc_delete, STRING("sl_still"))
    CALL(evt_npc_set_anim, STRING("sl_screamy"), STRING("S_1"))
    CALL(evt_map_set_blend, 0, 120, 120, 150, 255)
    NPC("sl_c5_tag_03", "sl_screamy")
    /* and the other still: Luigi hanging from Screamy, alone, eyes shut */
    CALL(evt_npc_set_anim, STRING("sl_screamy"), STRING("F_2"))
    CALL(evt_npc_set_position, STRING("sl_screamy"), 0, 45, 100)
    CALL(evt_mario_set_pos, 0, 2, 100)
    CALL(evt_mario_dispflag_onoff, 0, MARIO_DISP_FLAG_HIDDEN)
    CALL(evt_luigi_story_pose, STRING("SL_HANG"))
    CALL(evt_map_set_blend, 0, 170, 170, 170, 255)
    WAIT_MS(1600)
    CALL(evt_luigi_story_pose, 0)
    CALL(evt_mario_dispflag_onoff, 1, MARIO_DISP_FLAG_HIDDEN)
    CALL(evt_npc_set_position, STRING("sl_screamy"), 0, 0, 100)
    CALL(evt_npc_set_anim, STRING("sl_screamy"), STRING("S_1"))
    CALL(evt_map_set_blend, 0, 120, 120, 150, 255)
    NPC("sl_c5_tag_04", "sl_screamy")
    CALL(evt_fade_out, 30)
    CALL(evt_mario_set_pos, 0, 0, 85)
    CALL(evt_mario_dispflag_onoff, 0, MARIO_DISP_FLAG_HIDDEN)
    CALL(evt_npc_delete, STRING("sl_screamy"))
    CALL(evt_mario_set_party_pos, 0, SCREAMY_SLOT, -40, 0, 85)
    CALL(evt_map_set_blend, 0, 70, 80, 130, 255)
    CALL(evt_fade_in, 20)
    SET_VALUE(SLF5(F_END), 1)
    CALL(evt_sl_chapter_end)
    RETURN
EVT_END()

static EVT_BEGIN(win_l50_look)
    SET_VALUE(LW(10), 120)
    SET_VALUE(LW(11), 70)
    SET_VALUE(LW(12), 100)
    IF_INT_EQUAL(SLF5(F_L50_PIECE), 0)
        CALL_CHILD(sl5_glint)
        PARTY("sl_c5_look_block")
    END_IF
    RETURN
EVT_END()

static EVT_BEGIN(win_l50_init)
    SET_VALUE(LW(0), PTR(win_l50_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL(sl5_survey)
    CALL(evt_mobj_save_blk, STRING("mobj_save"), -160, 60, -160, 0, 0)
    CALL(evt_mobj_brick, STRING("mobj_hidden"), 120, 60, 100, ITEM_STAR_PIECE, 11, 0, SLF5(F_L50_PIECE))
    CALL(evt_sl_look_point, 0, 120, 0, 100, 260, PTR(win_l50_look))
    IF_INT_EQUAL(SLF5(F_PIECE), 1)
        IF_INT_EQUAL(SLF5(F_END), 0)
            /* night: no dome on the hill */
            CALL(evt_map_set_blend, 0, 70, 80, 130, 255)
            CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG2_WIN1"))
            SPAWN_EVENT(win_l50_night)
            RETURN
        END_IF
    END_IF
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG2_WIN1"))
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG2_WIN1"))
    IF_INT_EQUAL(SLF5(F_OPENED), 0)
        SPAWN_EVENT(win_l50_arrive)
    END_IF
    RETURN
EVT_END()

/* ======================================================================
 * win_l51, the Hushed Clearing (win_00): Old Mossbeard, the pond (Swim)
 * ====================================================================== */

static BeroEntry win_l51_bero[] = {BERO("w_bero", "win_l50", "e_bero"), BERO("e_bero", "win_l52", "w_bero"), {0}};

static EVT_BEGIN(win_l51_moss)
    CALL(evt_mario_key_onoff, 0)
    IF_INT_EQUAL(SLF5(F_MOSS), 0)
        NPC("sl_c5_moss_00", "sl_moss")
        SAY("sl_c5_moss_01")
        NPC("sl_c5_moss_02", "sl_moss")
        SAY("sl_c5_moss_03")
        NPC("sl_c5_moss_04", "sl_moss")
        NPC("sl_c5_moss_05", "sl_moss")
        NPC("sl_c5_moss_06", "sl_moss")
        SET_VALUE(SLF5(F_MOSS), 1)
        CALL(evt_mario_key_onoff, 1)
        RETURN
    END_IF
    CALL_CHILD(sl5_button_counts)
    IF_INT_LESS_OR_EQUAL(LW(0), LW(1))
        IF_INT_EQUAL(LW(1), 3)
            NPC("sl_c5_moss_done", "sl_moss")
        ELSE
            NPC("sl_c5_moss_again", "sl_moss")
        END_IF
        CALL(evt_mario_key_onoff, 1)
        RETURN
    END_IF
    /* a button for a little something, one at a time */
    NPC("sl_c5_moss_trade", "sl_moss")
    IF_INT_EQUAL(SLF5(F_TRADE1), 0)
        SET_VALUE(SLF5(F_TRADE1), 1)
        NPC("sl_c5_moss_give1", "sl_moss")
        CALL(evt_item_entry, STRING("sl_gift"), ITEM_MAPLE_SYRUP, 0, -1000, 0, ITEM_ENTRY_FIXED, -1, 0)
        CALL(evt_item_get_item, STRING("sl_gift"))
    ELSE
        IF_INT_EQUAL(SLF5(F_TRADE2), 0)
            SET_VALUE(SLF5(F_TRADE2), 1)
            /* a Shine Sprite unless the ruin's guardian already gave one (the plan's rule) */
            IF_INT_EQUAL(SLF5(F_SUN_SHINE), 0)
                NPC("sl_c5_moss_give2", "sl_moss")
                CALL(evt_item_entry, STRING("sl_gift"), ITEM_SHINE_SPRITE, 0, -1000, 0, ITEM_ENTRY_FIXED, -1, 0)
                SET_VALUE(SLF5(F_SHINE_TRADE), 1)
            ELSE
                NPC("sl_c5_moss_give2b", "sl_moss")
                CALL(evt_item_entry, STRING("sl_gift"), ITEM_JAMMIN_JELLY, 0, -1000, 0, ITEM_ENTRY_FIXED, -1, 0)
            END_IF
            CALL(evt_item_get_item, STRING("sl_gift"))
        ELSE
            SET_VALUE(SLF5(F_TRADE3), 1)
            NPC("sl_c5_moss_give3", "sl_moss")
            CALL(evt_item_entry, STRING("sl_gift"), ITEM_FEELING_FINE, 0, -1000, 0, ITEM_ENTRY_FIXED, -1, 0)
            CALL(evt_item_get_item, STRING("sl_gift"))
        END_IF
    END_IF
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* the pond: Blooey's Swim there is a dive (a swim point with a script: he goes alone)
 * and he brings up the first button */
static EVT_BEGIN(win_l51_dive)
    CALL(evt_mario_key_onoff, 0)
    IF_INT_EQUAL(SLF5(F_BTN1), 0)
        CALL(evt_mario_set_dir, 90, 0, 0)
        CALL(evt_party_get_pos, 0, LW(0), LW(1), LW(2))
        CALL(evt_party_set_pose, 0, STRING("F_1"))
        CALL(evt_party_jump_pos, 0, 120, 0, 20, 400, 0, 30)
        CALL(evt_snd_sfxon, STRING("SFX_MARIO_FALL_WATER1"), 0)
        CALL(evt_eff, STRING(""), STRING("splash"), 0, 120, -10, 20, FLOAT(2.0f), 0, 0, 0, 0, 0, 0, 0)
        CALL(evt_party_dispflg_onoff, 0, PARTY_DISP_FLAG_HIDDEN, 1)
        WAIT_MS(1600)
        CALL(evt_snd_sfxon, STRING("SFX_MARIO_FALL_WATER1"), 0)
        CALL(evt_eff, STRING(""), STRING("splash"), 0, 120, -10, 20, FLOAT(2.0f), 0, 0, 0, 0, 0, 0, 0)
        CALL(evt_party_dispflg_onoff, 0, PARTY_DISP_FLAG_HIDDEN, 0)
        CALL(evt_party_set_pose, 0, STRING("F_3"))
        CALL(evt_party_jump_pos, 0, LW(0), LW(1), LW(2), 400, 0, 40)
        CALL(evt_party_set_pose, 0, STRING("S_1"))
        /* the partner moves are done in his use state; then he follows again */
        CALL(evt_sl_ability_done)
        PARTY("sl_c5_pond_00")
        SET_VALUE(SLF5(F_BTN1), 1)
        CALL_CHILD(sl5_got_button)
    ELSE
        CALL(evt_sl_ability_done)
        PARTY("sl_c5_pond_dived")
    END_IF
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(win_l51_pond_check)
    CALL(evt_mario_key_onoff, 0)
    IF_INT_EQUAL(SLF5(F_BTN1), 0)
        SAY("sl_c5_pond_check")
        CALL(evt_mario_get_party, LW(0))
        IF_INT_EQUAL(LW(0), BLOOEY_SLOT)
            PARTY("sl_c5_pond_hint")
        ELSE
            SAY("sl_c5_pond_blooey")
        END_IF
    ELSE
        SAY("sl_c5_pond_empty")
    END_IF
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(win_l51_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG2_WIN1"))
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG2_WIN1"))
    CALL(evt_sl_enemy, STRING("sl_e1"), STRING(SJ5_DARK_PUFF), STRING(SJ5_PUFF_AI), -350, 20, 40, 150, 60, STRING("BTLNO_WIN_00_01"))
    CALL(evt_sl_enemy, STRING("sl_e2"), STRING(SJ5_CLEFT_W), STRING(SJ5_CLEFT), 440, 0, 70, 40, 30, STRING("BTLNO_WIN_00_02"))
    CALL(evt_sl_enemy, STRING("sl_e3"), STRING(SJ5_DARK_PUFF), STRING(SJ5_PUFF_AI), -620, 20, -40, 100, 50, STRING("BTLNO_WIN_00_01"))
    SET_VALUE(LW(0), PTR(win_l51_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL(sl5_survey)
    CALL(evt_npc_entry, STRING("sl_moss"), STRING(M_MOSSBEARD))
    CALL(evt_npc_set_tribe, STRING("sl_moss"), STRING(SJ5_ELDER))
    CALL(evt_npc_set_width, STRING("sl_moss"), 50)
    CALL(evt_npc_set_position, STRING("sl_moss"), 200, 0, -90)
    CALL(evt_npc_set_ry, STRING("sl_moss"), 270)
    CALL(evt_npc_set_color, STRING("sl_moss"), 215, 230, 205, 255)
    CALL(evt_npc_change_interrupt, STRING("sl_moss"), 6, PTR(win_l51_moss))
    CALL(evt_mobj_badgeblk, STRING("mobj_item"), -140, 60, -70, ITEM_HONEY_SYRUP, 0, SLF5(F_L51_ITEM), 0)
    CALL(evt_mobj_brick, STRING("mobj_coin"), -80, 60, -70, 0, 3, 0, SLF5(F_L51_COIN))
    /* the dark pond: Blooey dives it */
    SPOT("sl_pond", 120, 0, 20, win_l51_pond_check)
    CALL(evt_npc_flag_onoff, 1, STRING("sl_pond"), NPC_FLAG_INTANGIBLE)
    CALL(evt_sl_point, PORT_SL_ABILITY_SWIM, 0, 120, 0, 20, 130, 0, PTR(win_l51_dive))
    RETURN
EVT_END()

/* ======================================================================
 * win_l52, Old Root Path (win_01): the plugged stump (Super Hammer), the owl (Sing)
 * ====================================================================== */

static BeroEntry win_l52_bero[] = {
    BERO("w_bero", "win_l51", "e_bero"),
    BERO("e_bero", "win_l53", "w_bero"),
    {"dokan1", 2, 0, BERO_DIR_DOWN, BERO_POS_AUTO, 0, 0, -1, NULL, 6, NULL, NULL, "dokan_2", 1, 1, 0, 0},
    {"dokan_2", 4098, 0, BERO_DIR_DOWN, BERO_POS_AUTO, 0, 0, -1, NULL, 6, NULL, NULL, "dokan1", 1, 1, 0, 0},
    {0},
};


/* the crow wakes, glares, caws once and flies off; the button drops */
static EVT_BEGIN(win_l52_owl_wake)
    CALL(evt_mario_key_onoff, 0)
    IF_INT_EQUAL(SLF5(F_L52_OWL), 0)
        SET_VALUE(SLF5(F_L52_OWL), 1)
        CALL(evt_npc_set_anim, STRING("sl_owl"), STRING("S_1"))
        CALL(evt_npc_set_ry, STRING("sl_owl"), 90)
        WAIT_MS(400)
        CALL(evt_npc_set_anim, STRING("sl_owl"), STRING("T_1"))
        CALL(evt_snd_sfxon, STRING("SFX_STG4_CROW_HOWL1"), 0)
        NPC("sl_c5_owl_hoot", "sl_owl")
        CALL(evt_npc_set_anim, STRING("sl_owl"), STRING("A_1"))
        CALL(evt_sl_npc_fly, STRING("sl_owl"), -600, 400, -300, 1400)
        CALL(evt_npc_delete, STRING("sl_owl"))
        CALL(evt_npc_delete, STRING("sl_crow"))
        CALL(evt_mario_get_party, LW(0))
        IF_INT_EQUAL(LW(0), FRIEND_HAYZEE_SLOT)
            PARTY("sl_c5_owl_hayzee")
        ELSE
            SAY("sl_c5_owl_gone")
        END_IF
        SET_VALUE(SLF5(F_BTN2), 1)
        CALL_CHILD(sl5_got_button)
    END_IF
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(win_l52_owl_check)
    CALL(evt_mario_key_onoff, 0)
    SAY("sl_c5_owl_see")
    SET_VALUE(LW(10), PORT_SL_FRIEND_HAYZEE)
    SET_VALUE(LW(11), STRING("sl_c5_owl_hum"))
    SET_VALUE(LW(12), PTR(win_l52_owl_wake))
    SET_VALUE(LW(13), STRING("sl_c5_owl_hint"))
    CALL(evt_mario_key_onoff, 1)
    CALL_CHILD(sl5_ability_or_hint)
    RETURN
EVT_END()

/* the stump's plug: a stone gone at the hammer's blow */
static EVT_BEGIN(win_l52_stone)
    LOOP(0)
        IF_INT_EQUAL(SLF5(F_L52_STONE), 1)
            BREAK_LOOP
        END_IF
        WAIT_FRAMES(6)
    END_LOOP
    WAIT_MS(300)
    CALL(evt_mario_key_onoff, 0)
    PARTY("sl_c5_stump_open")
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* out: 1 while the stick's held down, with Luigi free to move */
static USER_FUNC(sl5_stick_down) {
    (void)isFirstCall;
    evtSetValue(event, event->args[0], marioChkKey() && (s8)marioGetPtr()->wStickDir2 > 30);
    return EVT_RETURN_DONE;
}

/* the way back from a background ledge: the stick held down on its pipe (LW(10) its x) for a moment
 * takes him back through the room's front pipe (LW(11)). The pipe's own exit didn't answer in tests;
 * win_l52's ledge is a dead end without this. If the pipe's exit fires first, the keys go off and this waits */
static EVT_BEGIN(sl5_ledge_pipe)
    SET_VALUE(LW(3), 0)
    LOOP(0)
        WAIT_FRAMES(1)
        CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
        CALL(sl5_stick_down, LW(4))
        SET_VALUE(LW(5), LW(0))
        SUBTRACT(LW(5), LW(10))
        IF_INT_EQUAL(LW(4), 1)
            IF_INT_LESS(LW(2), -400)
                IF_INT_LESS(LW(5), 25)
                    IF_INT_GREATER(LW(5), -25)
                        ADD(LW(3), 1)
                        IF_INT_GREATER(LW(3), 20)
                            BREAK_LOOP
                        END_IF
                        CONTINUE_LOOP
                    END_IF
                END_IF
            END_IF
        END_IF
        SET_VALUE(LW(3), 0)
    END_LOOP
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_snd_sfxon, STRING("SFX_MARIO_DOKAN1"), 0)
    CALL(evt_bero_mapchange, LW(12), LW(11))
    RETURN
EVT_END()

static EVT_BEGIN(win_l52_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG2_WIN1"))
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG2_WIN1"))
    CALL(evt_sl_enemy, STRING("sl_e1"), STRING(SJ5_PALE_PIRANHA), STRING(SJ5_PIRANHA_AI), 260, 0, 60, 60, 40, STRING("BTLNO_WIN_02_01"))
    CALL(evt_sl_enemy, STRING("sl_e2"), STRING(SJ5_DARK_PUFF), STRING(SJ5_PUFF_AI), -220, 20, 40, 120, 60, STRING("BTLNO_WIN_00_01"))
    CALL(evt_sl_enemy, STRING("sl_e3"), STRING(SJ5_PALE_PIRANHA), STRING(SJ5_PIRANHA_AI), 400, 0, -60, 40, 40, STRING("BTLNO_WIN_02_02"))
    SET_VALUE(LW(0), PTR(win_l52_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL(sl5_survey)
    SET_VALUE(LW(10), -159)
    SET_VALUE(LW(11), STRING("dokan1"))
    SET_VALUE(LW(12), STRING("win_l52"))
    SPAWN_EVENT(sl5_ledge_pipe)
    /* the grey stone on the stump: smash it, and the stump is a way up to the ledge behind */
    CALL(evt_mobj_lv_blk, STRING("mobj_stone"), 0, 24, 0, 0, SLF5(F_L52_STONE), STRING("MOBJ_Lv1Block"))
    /* the badge on the ledge behind, up the stump (the ledge is too narrow for a chest's front) */
    CALL(evt_item_entry, STRING("item_dplus"), ITEM_DEFEND_PLUS, -135, 22, -449, ITEM_ENTRY_FIXED, SLF5(F_L52_CHEST), 0)
    IF_INT_EQUAL(SLF5(F_L52_STONE), 0)
        SPAWN_EVENT(win_l52_stone)
    END_IF
    IF_INT_EQUAL(SLF5(F_L52_OWL), 0)
        CALL(evt_npc_entry, STRING("sl_owl"), STRING(M_CROW))
        CALL(evt_npc_set_tribe, STRING("sl_owl"), STRING(SJ5_CROW))
        CALL(evt_npc_set_width, STRING("sl_owl"), 60)
        CALL(evt_npc_set_position, STRING("sl_owl"), -300, 0, 70)
        CALL(evt_npc_set_anim, STRING("sl_owl"), STRING("Z_1"))
        CALL(evt_npc_flag_onoff, 1, STRING("sl_owl"), NPC_FLAG_NO_TURN_TO_MARIO | NPC_FLAG_INTANGIBLE)
        /* checked through a spot of its own: the crow's tribe doesn't talk */
        SPOT("sl_crow", -300, 0, 70, win_l52_owl_check)
        CALL(evt_sl_point, PORT_SL_ABILITY_SING, 0, -300, 0, 70, 140, 2, PTR(win_l52_owl_wake))
    END_IF
    RETURN
EVT_END()

/* ======================================================================
 * win_l53, Thorn Walk (win_02): the black thorns (Blast), a Heart Block
 * ====================================================================== */

static BeroEntry win_l53_bero[] = {
    BERO("w_bero", "win_l52", "e_bero"),
    BERO("e_bero", "win_l54", "w_bero"),
    {"dokan2", 2, 0, BERO_DIR_W, BERO_POS_AUTO, 0, 0, -1, NULL, 10, NULL, NULL, "dokan1", 1, 1, 0, 0},
    {"dokan1", 2, 0, BERO_DIR_DOWN, BERO_POS_AUTO, 0, 0, -1, NULL, 6, NULL, NULL, "dokan2", 1, 1, 0, 0},
    {0},
};

CHEST_EVT(win_l53_chest_open, "mobj_box", ITEM_FLOWER_SAVER)

static EVT_BEGIN(win_l53_thorns_gone)
    CALL(evt_mario_key_onoff, 0)
    IF_INT_EQUAL(SLF5(F_L53_THORNS), 0)
        SET_VALUE(SLF5(F_L53_THORNS), 1)
        CALL(evt_npc_get_position, STRING("sl_thorns"), LW(0), LW(1), LW(2))
        CALL(evt_snd_sfxon, STRING("SFX_AUDIENCE_BOMB_BURST1"), 0)
        CALL(evt_eff, 0, STRING("bomb"), 0, LW(0), LW(1), LW(2), FLOAT(1.5f), 0, 0, 0, 0, 0, 0, 0)
        CALL(evt_cam_shake, 4, FLOAT(0.0f), FLOAT(2.0f), 400)
        /* blown off its feet: it bounces away into the trees, and stays away */
        CALL(evt_npc_set_anim, STRING("sl_thorns"), STRING("TGD_D_1"))
        CALL(evt_sl_npc_fly, STRING("sl_thorns"), 520, 240, -260, 700)
        CALL(evt_npc_delete, STRING("sl_thorns"))
        CALL(evt_sl_point_off, PORT_SL_ABILITY_BLAST, 0)
        WAIT_MS(500)
        CALL(evt_mobj_brick, STRING("mobj_c1"), 60, 60, -100, 0, 3, 0, SLF5(F_L53_COIN1))
        CALL(evt_mobj_brick, STRING("mobj_c2"), 120, 60, -100, 0, 3, 0, SLF5(F_L53_COIN2))
        CALL(evt_mobj_itembox, STRING("mobj_box"), 200, 0, -100, 0, 0, PTR(win_l53_chest_open), SLF5(F_L53_CHEST))
        SAY("sl_c5_thorns_gone")
        IF_INT_EQUAL(SLF5(F_BTN3), 0)
            SAY("sl_c5_thorns_button")
            SET_VALUE(SLF5(F_BTN3), 1)
            CALL_CHILD(sl5_got_button)
        END_IF
    END_IF
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(win_l53_thorns_check)
    CALL(evt_mario_key_onoff, 0)
    SAY("sl_c5_thorns_see")
    SET_VALUE(LW(10), PORT_SL_FRIEND_JERRY)
    SET_VALUE(LW(11), STRING("sl_c5_thorns_pull"))
    SET_VALUE(LW(12), PTR(win_l53_thorns_gone))
    SET_VALUE(LW(13), STRING("sl_c5_thorns_hint"))
    CALL(evt_mario_key_onoff, 1)
    CALL_CHILD(sl5_ability_or_hint)
    RETURN
EVT_END()

static EVT_BEGIN(win_l53_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG2_WIN1"))
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG2_WIN1"))
    CALL(evt_sl_enemy, STRING("sl_e1"), STRING(SJ5_PALE_PIRANHA), STRING(SJ5_PIRANHA_AI), -460, 0, -40, 40, 40, STRING("BTLNO_WIN_03_01"))
    CALL(evt_sl_enemy, STRING("sl_e2"), STRING(SJ5_CLEFT_W), STRING(SJ5_CLEFT), -40, 0, 90, 40, 30, STRING("BTLNO_WIN_02_02"))
    CALL(evt_sl_enemy, STRING("sl_e3"), STRING(SJ5_PALE_PIRANHA), STRING(SJ5_PIRANHA_AI), 330, 0, 80, 30, 30, STRING("BTLNO_WIN_02_01"))
    SET_VALUE(LW(0), PTR(win_l53_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL(sl5_survey)
    CALL(evt_mobj_recovery_blk, STRING("mobj_heart"), 0, -120, 60, 60, 0, 0)
    /* the way up the far bank: win_02 flew it, by a plane panel Luigi can't use */
    CALL(evt_mobj_jumpstand_blue, 0, STRING("sl_spring"), 300, 480, 0, 0, 0, 0)
    IF_INT_EQUAL(SLF5(F_L53_THORNS), 0)
        CALL(evt_npc_entry, STRING("sl_thorns"), STRING(M_THORNS))
        CALL(evt_npc_set_tribe, STRING("sl_thorns"), STRING(SJ5_SPOT_TRIBE))
        CALL(evt_npc_set_width, STRING("sl_thorns"), 60)
        CALL(evt_npc_set_position, STRING("sl_thorns"), 120, 0, -70)
        CALL(evt_npc_set_anim, STRING("sl_thorns"), STRING("TGD_Z_1"))
        CALL(evt_npc_set_scale, STRING("sl_thorns"), FLOAT(1.6f), FLOAT(1.6f), FLOAT(1.6f))
        CALL(evt_npc_flag_onoff, 1, STRING("sl_thorns"), NPC_FLAG_NO_TURN_TO_MARIO)
        CALL(evt_npc_change_interrupt, STRING("sl_thorns"), 6, PTR(win_l53_thorns_check))
        CALL(evt_sl_point, PORT_SL_ABILITY_BLAST, 0, 120, 0, -70, 110, 0, PTR(win_l53_thorns_gone))
    ELSE
        CALL(evt_mobj_brick, STRING("mobj_c1"), 60, 60, -100, 0, 3, 0, SLF5(F_L53_COIN1))
        CALL(evt_mobj_brick, STRING("mobj_c2"), 120, 60, -100, 0, 3, 0, SLF5(F_L53_COIN2))
        CALL(evt_mobj_itembox, STRING("mobj_box"), 200, 0, -100, 0, 0, PTR(win_l53_chest_open), SLF5(F_L53_CHEST))
    END_IF
    RETURN
EVT_END()

/* ======================================================================
 * win_l54, the Grey Stumps (win_03): the Luff post (Tune-up), the way to the dome
 * ====================================================================== */

static BeroEntry win_l54_bero[] = {
    BERO("w_bero", "win_l53", "e_bero"),
    {"n_bero", 0x1000, 0, BERO_DIR_E, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "hei_l50", "w_bero", 1, 1, 0, 0},
    {"dokan_1", 2, 0, BERO_DIR_DOWN, BERO_POS_AUTO, 0, 0, -1, NULL, 6, NULL, NULL, "dokan_2", 1, 1, 0, 0},
    {"dokan_2", 4098, 0, BERO_DIR_DOWN, BERO_POS_AUTO, 0, 0, -1, NULL, 6, NULL, NULL, "dokan_1", 1, 1, 0, 0},
    {0},
};

static EVT_BEGIN(win_l54_post_fixed)
    CALL(evt_mario_key_onoff, 0)
    IF_INT_EQUAL(SLF5(F_L54_POST), 0)
        SET_VALUE(SLF5(F_L54_POST), 1)
        CALL(evt_sl_point_off, PORT_SL_ABILITY_TUNE, 0)
        CALL(evt_npc_set_anim, STRING("sl_signpost"), STRING("T_1"))
        WAIT_MS(500)
        CALL(evt_npc_set_anim, STRING("sl_signpost"), STRING("F_1"))
        CALL(evt_snd_sfxon, STRING("SFX_STG1_QUAKE2"), 0)
        CALL(evt_cam_shake, 4, FLOAT(0.0f), FLOAT(1.0f), 600)
        WAIT_MS(1000)
        CALL(evt_npc_set_anim, STRING("sl_signpost"), STRING("S_2"))
        SYS("sl_c5_post_turns")
        SET_VALUE(LW(10), -67)
        SET_VALUE(LW(11), 50)
        SET_VALUE(LW(12), -96)
        CALL_CHILD(sl5_glint)
        CALL(evt_item_entry, STRING("item_piece"), ITEM_STAR_PIECE, -67, 50, -40, ITEM_ENTRY_BOUNCE, SLF5(F_L54_PIECE), 0)
        CALL(evt_pouch_add_coin, 20)
        CALL(evt_snd_sfxon, STRING("SFX_COIN_GET1"), 0)
        SYS("sl_c5_post_coins")
    END_IF
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(win_l54_post_check)
    CALL(evt_mario_key_onoff, 0)
    IF_INT_EQUAL(SLF5(F_L54_POST), 0)
        SAY("sl_c5_post_see")
        SET_VALUE(LW(10), PORT_SL_FRIEND_TORQUE)
        SET_VALUE(LW(11), STRING("sl_c5_post_bang"))
        SET_VALUE(LW(12), PTR(win_l54_post_fixed))
        SET_VALUE(LW(13), STRING("sl_c5_post_hint"))
        CALL(evt_mario_key_onoff, 1)
        CALL_CHILD(sl5_ability_or_hint)
        RETURN
    END_IF
    SYS("sl_c5_post_points")
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(win_l54_dome)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_seq_wait, 2)
    CALL(evt_bero_exec_wait, 65536)
    WAIT_MS(400)
    SAY("sl_c5_dome_00")
    SET_VALUE(SLF5(F_L54_DOME), 1)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* the house's door on the hill (A, as win_03's own): in, to the white gate */
static EVT_BEGIN(win_l54_house_door)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_snd_sfxon, STRING("SFX_DOOR_OPEN_WOOD1"), 0)
    CALL(evt_bero_mapchange, STRING("hei_l50"), STRING("w_bero"))
    RETURN
EVT_END()

/* over the hedge, to the stump's pipe and back: win_03 slips through it in a form Luigi hasn't got.
 * The game's red springs, each a scripted leap */
static EVT_BEGIN(win_l54_spring_in)
    CALL(evt_mario_jump_jumpstand, 0, 0, 0, -67, 300, -96, -67, 34, -96, 900, 1)
    RETURN
EVT_END()

static EVT_BEGIN(win_l54_spring_out)
    CALL(evt_mario_jump_jumpstand, 0, 0, 0, -20, 300, 60, -20, 0, 60, 900, 1)
    RETURN
EVT_END()

/* and over the break in the hill path (no jump clears it) */
static EVT_BEGIN(win_l54_spring_east)
    CALL(evt_mario_jump_jumpstand, 0, 0, 0, 115, 120, -449, 115, -2, -449, 800, 1)
    RETURN
EVT_END()

static EVT_BEGIN(win_l54_spring_west)
    CALL(evt_mario_jump_jumpstand, 0, 0, 0, -70, 120, -449, -70, 11, -449, 800, 1)
    RETURN
EVT_END()

static EVT_BEGIN(win_l54_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG2_WIN1"))
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG2_WIN1"))
    CALL(evt_sl_enemy, STRING("sl_e1"), STRING(SJ5_DARK_PUFF), STRING(SJ5_PUFF_AI), -300, 20, 40, 120, 60, STRING("BTLNO_WIN_00_03"))
    CALL(evt_sl_enemy, STRING("sl_e2"), STRING(SJ5_PALE_PIRANHA), STRING(SJ5_PIRANHA_AI), 330, 0, 60, 40, 40, STRING("BTLNO_WIN_02_01"))
    CALL(evt_sl_enemy, STRING("sl_e3"), STRING(SJ5_CLEFT_W), STRING(SJ5_CLEFT), 60, 0, 80, 120, 40, STRING("BTLNO_WIN_00_02"))
    SET_VALUE(LW(0), PTR(win_l54_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL(sl5_survey)
    /* as win_03's own init: the hole and the stone it hides stay hidden, and their wall off */
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("S_ana"), MAPOBJ_FLAG_HIDE)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("S_siro_asu"), MAPOBJ_FLAG_HIDE)
    CALL(evt_hitobj_onoff, STRING("A_siro_asu"), 1, 0)
    CALL(evt_run_case_evt, CASE_COND_STAND_ON_A, 1, STRING("n_bero"), 0, PTR(win_l54_house_door), 0)
    SET_VALUE(LW(10), -180)
    SET_VALUE(LW(11), STRING("dokan_1"))
    SET_VALUE(LW(12), STRING("win_l54"))
    SPAWN_EVENT(sl5_ledge_pipe)
    CALL(evt_mobj_jumpstand_red, 0, STRING("sl_spring_in"), -67, 0, 85, PTR(win_l54_spring_in), 0)
    CALL(evt_mobj_jumpstand_red, 0, STRING("sl_spring_out"), -20, 0, -80, PTR(win_l54_spring_out), 0)
    CALL(evt_mobj_jumpstand_red, 0, STRING("sl_spring_east"), -25, 9, -449, PTR(win_l54_spring_east), 0)
    CALL(evt_mobj_jumpstand_red, 0, STRING("sl_spring_west"), 75, -5, -449, PTR(win_l54_spring_west), 0)
    CALL(evt_mobj_save_blk, STRING("mobj_save"), -140, 60, 40, 0, 0)
    PROP("sl_signpost", M_SIGNPOST, 230, 0, -50, "S_1")
    CALL(evt_npc_set_scale, STRING("sl_signpost"), FLOAT(2.5f), FLOAT(2.5f), FLOAT(2.5f))
    IF_INT_EQUAL(SLF5(F_L54_POST), 1)
        CALL(evt_npc_set_anim, STRING("sl_signpost"), STRING("S_2"))
    END_IF
    SPOT("sl_post", 230, 0, -50, win_l54_post_check)
    IF_INT_EQUAL(SLF5(F_L54_POST), 0)
        CALL(evt_sl_point, PORT_SL_ABILITY_TUNE, 0, 230, 0, -50, 110, 0, PTR(win_l54_post_fixed))
    END_IF
    IF_INT_EQUAL(SLF5(F_L54_DOME), 0)
        SPAWN_EVENT(win_l54_dome)
    END_IF
    RETURN
EVT_END()

/* ======================================================================
 * hei_l50, the White Gate (hei_07): the Sentinel's three questions
 * ====================================================================== */

static BeroEntry hei_l50_bero[] = {
    BERO("w_bero", "win_l54", "n_bero"),
    {"dokan", 2, 0, BERO_DIR_DOWN, BERO_POS_AUTO, 0, 0, -1, NULL, 6, NULL, "tik_l50", "e_bero", 1, 1, 0, 0},
    {0},
};

/* the wrong answer: two Clefts drop from the ceiling */
static EVT_BEGIN(hei_l50_wrong)
    CALL(evt_snd_sfxon, STRING("SFX_STG1_QUAKE2"), 0)
    CALL(evt_cam_shake, 4, FLOAT(0.0f), FLOAT(2.0f), 500)
    CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
    ADD(LW(0), 70)
    CALL(evt_sl_enemy, STRING("sl_drop"), STRING(SJ5_CLEFT), STRING(SJ5_CLEFT), LW(0), 0, LW(2), 0, 0, STRING("BTLNO_HEI_03_01"))
    CALL(evt_npc_set_position, STRING("sl_drop"), LW(0), 200, LW(2))
    CALL(evt_npc_jump_position, STRING("sl_drop"), LW(0), 0, LW(2), 400, 0, 0, 1)
    CALL(evt_npc_battle_start, STRING("sl_drop"))
    CALL(evt_npc_wait_battle_end)
    CALL(evt_npc_delete, STRING("sl_drop"))
    CALL(evt_cam3d_evt_set, -20, 110, 480, 70, 60, 0, 0, 11)
    RETURN
EVT_END()

/* one question: LW(5) its tag, LW(6) the right answer (0-2); repeats until it's answered right */
static EVT_BEGIN(hei_l50_ask)
    LABEL(1)
    CALL(evt_msg_print, 0, LW(5), 0, STRING("sl_sentinel"))
    CALL(evt_msg_select, 0, LW(7))
    IF_INT_EQUAL(LW(0), LW(6))
        CALL(evt_msg_print_add, 0, STRING("sl_c5_quiz_right"))
        CALL(evt_snd_sfxon, STRING("SFX_SYSTEM_GOOD1"), 0)
        RETURN
    END_IF
    IF_INT_EQUAL(LW(8), 1)
        IF_INT_EQUAL(LW(0), 1)
            CALL(evt_msg_print_add, 0, STRING("sl_c5_quiz_buttons"))
            GOTO(2)
        END_IF
    END_IF
    CALL(evt_msg_print_add, 0, STRING("sl_c5_quiz_wrong"))
    LABEL(2)
    CALL(evt_snd_sfxon, STRING("SFX_CURSOR_NOT_CHOICE1"), 0)
    CALL_CHILD(hei_l50_wrong)
    GOTO(1)
    RETURN
EVT_END()

static EVT_BEGIN(hei_l50_quiz)
    CALL(evt_mario_key_onoff, 0)
    IF_INT_EQUAL(SLF5(F_QUIZ), 1)
        NPC("sl_c5_gate_after", "sl_sentinel")
        CALL(evt_mario_key_onoff, 1)
        RETURN
    END_IF
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_QUIZ2"))
    CALL(evt_map_playanim, STRING("anm_kuti_1"), 2, 0)
    /* the quiz room's own shot: Luigi and the stone face */
    CALL(evt_mario_mov_pos2, 10, 30, FLOAT(80.0f))
    CALL(evt_mario_set_dir, 90, 0, 0)
    CALL(evt_cam3d_evt_set, -20, 110, 480, 70, 60, 0, 1000, 11)
    NPC("sl_c5_gate_00", "sl_sentinel")
    SAY("sl_c5_gate_01")
    NPC("sl_c5_gate_02", "sl_sentinel")
    SAY("sl_c5_gate_03")
    NPC("sl_c5_gate_04", "sl_sentinel")
    SAY("sl_c5_gate_05")
    NPC("sl_c5_gate_06", "sl_sentinel")
    SET_VALUE(LW(5), STRING("sl_c5_quiz_q1"))
    SET_VALUE(LW(6), 1)
    SET_VALUE(LW(7), STRING("sl_c5_quiz_a1"))
    SET_VALUE(LW(8), 0)
    CALL_CHILD(hei_l50_ask)
    SET_VALUE(LW(5), STRING("sl_c5_quiz_q2"))
    SET_VALUE(LW(6), 0)
    SET_VALUE(LW(7), STRING("sl_c5_quiz_a2"))
    SET_VALUE(LW(8), 1)
    CALL_CHILD(hei_l50_ask)
    SET_VALUE(LW(5), STRING("sl_c5_quiz_q3"))
    SET_VALUE(LW(6), 1)
    SET_VALUE(LW(7), STRING("sl_c5_quiz_a3"))
    SET_VALUE(LW(8), 0)
    CALL_CHILD(hei_l50_ask)
    NPC("sl_c5_gate_07", "sl_sentinel")
    SAY("sl_c5_gate_08")
    SET_VALUE(SLF5(F_QUIZ), 1)
    CALL(evt_snd_bgmoff, PSND_FADE_OUT)
    /* the statue slides aside; a way down rises out of the floor */
    CALL(evt_snd_sfxon, STRING("SFX_STG1_QUAKE1"), 0)
    CALL(evt_cam_shake, 4, FLOAT(0.001953125f), FLOAT(0.0f), 3000)
    CALL(evt_map_playanim, STRING("anm_sekizou"), 0, 0)
    CALL(evt_map_checkanim, STRING("anm_sekizou"), LW(0), LW(1))
    WAIT_MS(LW(1))
    CALL(evt_snd_sfxon, STRING("SFX_DOKAN_APPEAR1"), 0)
    CALL(evt_hitobj_onoff, STRING("a_sekizou_1_a"), 0, 0)
    CALL(evt_hitobj_onoff, STRING("a_sekizou_1_b"), 0, 0)
    CALL(evt_hitobj_onoff, STRING("a_sekizou_2"), 0, 1)
    SET_VALUE(LW(0), STRING("dokan"))
    CALL_CHILD(bero_case_switch_on)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG1_TORIDE1"))
    CALL(evt_cam3d_evt_off, 500, 11)
    CALL(evt_snd_bgmoff, PSND_FADE_DIP_TO(20))
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(hei_l50_look)
    SET_VALUE(LW(10), 40)
    SET_VALUE(LW(11), 110)
    SET_VALUE(LW(12), 0)
    IF_INT_EQUAL(SLF5(F_GATE_PIECE), 0)
        CALL_CHILD(sl5_glint)
        PARTY("sl_c5_look_block")
    END_IF
    RETURN
EVT_END()

static EVT_BEGIN(hei_l50_enter)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_seq_wait, 2)
    CALL(evt_bero_exec_wait, 65536)
    WAIT_MS(300)
    SAY("sl_c5_gate_see")
    SET_VALUE(SLF5(F_H50_SEEN), 1)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(hei_l50_init)
    SET_VALUE(LW(0), PTR(hei_l50_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL(sl5_survey)
    CALL_CHILD(sl5_white_room)
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG1_HEI4"))
    CALL(evt_snd_set_rev_mode, 1)
    CALL(evt_map_playanim, STRING("anm_hokori"), 1, 0)
    CALL(evt_mapobj_trans, 1, STRING("hei_07_2"), 0, -1000, 0)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("hei_07_2"), MAPOBJ_FLAG_HIDE)
    /* the Sentinel's voice: unseen, at its face */
    CALL(evt_npc_entry, STRING("sl_sentinel"), STRING(M_SPOT))
    CALL(evt_npc_set_position, STRING("sl_sentinel"), 40, 70, 0)
    CALL(evt_npc_flag_onoff, 1, STRING("sl_sentinel"), NPC_FLAG_HIDDEN | NPC_FLAG_NO_SHADOW | NPC_FLAG_INTANGIBLE)
    IF_INT_EQUAL(SLF5(F_QUIZ), 0)
        SET_VALUE(LW(0), STRING("dokan"))
        CALL_CHILD(bero_case_switch_off)
        CALL(evt_hitobj_onoff, STRING("a_sekizou_2"), 0, 0)
        CALL(evt_run_case_evt, CASE_COND_FRONT_A, 1, STRING("a_sekizou_1_a"), 0, PTR(hei_l50_quiz), 0)
        /* and anywhere before its plinth: checking it starts the questions */
        SPOT("sl_plinth", 40, 0, 0, hei_l50_quiz)
        IF_INT_EQUAL(SLF5(F_H50_SEEN), 0)
            SPAWN_EVENT(hei_l50_enter)
        END_IF
    ELSE
        CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG1_TORIDE1"))
        CALL(evt_snd_bgmoff, PSND_FADE_DIP_TO(20))
        CALL(evt_hitobj_onoff, STRING("a_sekizou_2"), 0, 1)
        CALL(evt_hitobj_onoff, STRING("a_sekizou_1_a"), 0, 0)
        CALL(evt_hitobj_onoff, STRING("a_sekizou_1_b"), 0, 0)
        CALL(evt_map_playanim, STRING("anm_sekizou"), 2, 0)
    END_IF
    CALL(evt_mobj_brick, STRING("mobj_hidden"), 40, 110, 0, ITEM_STAR_PIECE, 11, 0, SLF5(F_GATE_PIECE))
    CALL(evt_sl_look_point, 0, 40, 0, 0, 300, PTR(hei_l50_look))
    RETURN
EVT_END()

/* ======================================================================
 * tik_l50, the Sleeper's Chamber (tik_05): Screamy, the tutorial, the sockets,
 * the sneeze, Cranberry
 * ====================================================================== */

/* the great door's two leaves, as tik_05's own door turns them (a bero's anime type 5 runs these scripts) */
static EVT_BEGIN(tik_l50_door_close)
    MULTIPLY_FLOAT(LW(0), FLOAT(0.38867188f))
    CALL(evt_mapobj_rotate, 0, STRING("door_b"), 0, LW(0), 0)
    MULTIPLY_FLOAT(LW(0), FLOAT(-1.0f))
    CALL(evt_mapobj_rotate, 0, STRING("door_a"), 0, LW(0), 0)
    RETURN
EVT_END()

static EVT_BEGIN(tik_l50_door_open)
    MULTIPLY_FLOAT(LW(0), FLOAT(-0.38867188f))
    CALL(evt_mapobj_rotate, 0, STRING("door_b"), 0, LW(0), 0)
    MULTIPLY_FLOAT(LW(0), FLOAT(-1.0f))
    CALL(evt_mapobj_rotate, 0, STRING("door_a"), 0, LW(0), 0)
    RETURN
EVT_END()

static BeroEntry tik_l50_bero[] = {
    BERO("e_bero", "hei_l50", "dokan"),
    BERO("w_bero", "hei_l51", "dokan"),
    {"n_bero", 1, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "tik_l51", "w_bero_1", 5, 5, tik_l50_door_close, tik_l50_door_open},
    {0},
};

/* Screamy, and he joins (beat 5) */
static EVT_BEGIN(tik_l50_screamy)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_seq_wait, 2)
    CALL(evt_bero_exec_wait, 65536)
    /* down the east steps, toward the dais, where the boy sleeps */
    CALL(evt_mario_mov_pos2, 380, 40, FLOAT(70.0f))
    CALL(evt_mario_mov_pos2, 160, -120, FLOAT(70.0f))
    CALL(evt_mario_set_dir, 270, 0, 0)
    WAIT_MS(500)
    SAY("sl_c5_chamber_00")
    CALL(evt_mario_get_party, LW(3))
    IF_INT_EQUAL(LW(3), FRIEND_HAYZEE_SLOT)
        PARTY("sl_c5_chamber_01")
    END_IF
    /* a step closer, and a shriek right behind him */
    CALL(evt_mario_mov_pos2, 130, -140, FLOAT(40.0f))
    CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_FF_ENV_SURPRISED3"))
    CALL(evt_npc_entry, STRING("sl_screamy"), STRING("c_lp_moa"))
    CALL(evt_npc_set_tribe, STRING("sl_screamy"), STRING(SJ5_SPOT_TRIBE))
    ADD(LW(0), 80)
    CALL(evt_npc_set_position, STRING("sl_screamy"), LW(0), 0, LW(2))
    CALL(evt_npc_set_ry, STRING("sl_screamy"), 270)
    CALL(evt_npc_set_anim, STRING("sl_screamy"), STRING("A_2B"))
    CALL(evt_snd_sfxon, STRING("SFX_AUDIENCE_PANIC_SCREAM1"), 0)
    CALL(evt_snd_sfxon, STRING("SFX_VOICE_MARIO_SURPRISED1"), 0)
    CALL(evt_mario_set_pose, STRING("M_N_7"))
    SUBTRACT(LW(0), 80)
    CALL(evt_mario_jump_pos, LW(0), LW(1), LW(2), 400, 1, 40)
    SUBTRACT(LW(0), 50)
    CALL(evt_mario_jump_pos, LW(0), LW(1), LW(2), 400, 1, 30)
    NPC("sl_c5_screamy_00", "sl_screamy")
    CALL(evt_npc_set_anim, STRING("sl_screamy"), STRING("S_1"))
    CALL(evt_mario_set_pose, STRING("M_S_1"))
    CALL(evt_mario_set_dir_npc, STRING("sl_screamy"))
    NPC("sl_c5_screamy_01", "sl_screamy")
    SAY("sl_c5_screamy_02")
    NPC("sl_c5_screamy_03", "sl_screamy")
    SAY("sl_c5_screamy_04")
    NPC("sl_c5_screamy_05", "sl_screamy")
    NPC("sl_c5_screamy_06", "sl_screamy")
    SAY("sl_c5_screamy_07")
    NPC("sl_c5_screamy_08", "sl_screamy")
    SAY("sl_c5_screamy_09")
    CALL(evt_npc_set_anim, STRING("sl_screamy"), STRING("A_2B"))
    NPC("sl_c5_screamy_10", "sl_screamy")
    CALL(evt_npc_set_anim, STRING("sl_screamy"), STRING("E_1"))
    NPC("sl_c5_screamy_11", "sl_screamy")
    CALL(evt_npc_set_anim, STRING("sl_screamy"), STRING("S_1"))
    SAY("sl_c5_screamy_12")
    CALL(evt_npc_set_anim, STRING("sl_screamy"), STRING("A_2B"))
    CALL(evt_snd_sfxon, STRING("SFX_AUDIENCE_PANIC_SCREAM1"), 0)
    NPC("sl_c5_screamy_13", "sl_screamy")
    /* Screamy joins */
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_FF_GET_PARTY1"))
    SYS("sl_c5_screamy_joined")
    CALL(evt_npc_get_position, STRING("sl_screamy"), LW(0), LW(1), LW(2))
    CALL(evt_npc_delete, STRING("sl_screamy"))
    CALL(evt_mario_get_party, LW(3))
    IF_INT_NOT_EQUAL(LW(3), 0)
        CALL(evt_mario_kill_party, 0)
    END_IF
    CALL(evt_sl_friend_join, PORT_SL_FRIEND_SCREAMY)
    CALL(evt_mario_set_party_pos, 0, SCREAMY_SLOT, LW(0), LW(1), LW(2))
    SET_VALUE(SLF5(F_SCREAMY), 1)
    WAIT_MS(1500)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_STG2_PUNI1"))
    /* the sockets, and the tutorial */
    PARTY("sl_c5_sockets_00")
    PARTY("sl_c5_float_00")
    SYS("sl_sc_float_help")
    PARTY("sl_c5_look_00")
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* Look at the west wall: the real door beside the fake one */
/* the real door, once Look has shown it: walked into, it takes him through (an exit switched
 * on while he's in the room never armed: this stands in for it) */
static EVT_BEGIN(tik_l50_westdoor)
    LOOP(0)
        WAIT_FRAMES(2)
        IF_INT_EQUAL(SLF5(F_FAKEDOOR), 1)
            CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
            IF_INT_LESS(LW(0), -548)
                IF_INT_GREATER(LW(1), 60)
                    BREAK_LOOP
                END_IF
            END_IF
        END_IF
    END_LOOP
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_mario_mov_pos2, -620, 30, FLOAT(100.0f))
    CALL(evt_bero_mapchange, STRING("hei_l51"), STRING("dokan"))
    RETURN
EVT_END()

static EVT_BEGIN(tik_l50_look)
    IF_INT_EQUAL(SLF5(F_FAKEDOOR), 0)
        SET_VALUE(SLF5(F_FAKEDOOR), 1)
        SET_VALUE(LW(10), -600)
        SET_VALUE(LW(11), 150)
        SET_VALUE(LW(12), 30)
        CALL_CHILD(sl5_glint)
        CALL(evt_sl_map_look, STRING("door"), 0xFFFFFF, 0x6080C0)
        PARTY("sl_c5_look_01")
    ELSE
        IF_INT_EQUAL(SLF5(F_CHAMBER_LOOKED), 0)
            SET_VALUE(SLF5(F_CHAMBER_LOOKED), 1)
            PARTY("sl_c5_look_02")
        ELSE
            PARTY("sl_c5_look_03")
        END_IF
    END_IF
    RETURN
EVT_END()

static EVT_BEGIN(tik_l50_cranberry_check)
    CALL(evt_mario_key_onoff, 0)
    IF_INT_EQUAL(SLF5(F_SCREAMY), 0)
        SAY("sl_c5_chamber_00")
    ELSE
        SAY("sl_c5_cranberry_check")
        SCREAMY("sl_c5_cranberry_check_sc")
    END_IF
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* the sockets: LW(1) which (0 crescent, 1 circle, 2 teardrop) */
static EVT_BEGIN(tik_l50_beam)
    /* a beam of light from socket LW(1) to the ceiling (its colour: silver, gold, blue) */
    SWITCH(LW(1))
        CASE_EQUAL(0)
            SET_VALUE(LW(10), -110)
            SET_VALUE(LW(12), -290)
        CASE_EQUAL(1)
            SET_VALUE(LW(10), 0)
            SET_VALUE(LW(12), -230)
        CASE_DEFAULT
            SET_VALUE(LW(10), 110)
            SET_VALUE(LW(12), -290)
    END_SWITCH
    SET_VALUE(LW(11), 30)
    CALL(evt_snd_sfxon, STRING("SFX_BOSS_KAMEBABA_MAGIC1"), 0)
    LOOP(6)
        CALL(evt_eff, 0, STRING("small_star"), 3, LW(10), LW(11), LW(12), 0, -1, 0, 3, 0, 0, 0, 0)
        ADD(LW(11), 40)
        WAIT_FRAMES(4)
    END_LOOP
    CALL(evt_eff, STRING(""), STRING("stardust"), 2, LW(10), 120, LW(12), 20, 160, 20, 60, 0, 0, 0, 0)
    RETURN
EVT_END()

/* how many lenses are set, to LW(0); and the light in the room follows it */
static EVT_BEGIN(tik_l50_lights)
    SET_VALUE(LW(0), 0)
    IF_INT_EQUAL(SLF5(F_SET_MOON), 1)
        ADD(LW(0), 1)
    END_IF
    IF_INT_EQUAL(SLF5(F_SET_SUN), 1)
        ADD(LW(0), 1)
    END_IF
    IF_INT_EQUAL(SLF5(F_SET_TEAR), 1)
        ADD(LW(0), 1)
    END_IF
    RETURN
EVT_END()

/* the sneeze, Cranberry wakes, the piece, the fading (beats 10-11) */
static EVT_BEGIN(tik_l50_awaken)
    CALL(evt_mario_key_onoff, 0)
    WAIT_MS(600)
    /* the beams meet above him; the dust of a thousand years lifts */
    CALL(evt_snd_sfxon, STRING("SFX_STAR_POINT_GET1"), 0)
    CALL(evt_eff, STRING(""), STRING("stardust"), 2, 0, 160, -300, 60, 60, 60, 120, 0, 0, 0, 0)
    CALL(evt_sl_map_wash, 0, 0xFFFDF0, 82)
    WAIT_MS(800)
    CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
    ADD(LW(1), 30)
    CALL(evt_eff, STRING(""), STRING("stardust"), 2, LW(0), LW(1), LW(2), 40, 40, 40, 120, 0, 0, 0, 0)
    PARTY("sl_c5_sneeze_00")
    CALL(evt_snd_bgmoff, PSND_FADE_OUT)
    CALL(evt_luigi_story_pose, STRING("SL_SNEEZE_A"))
    SAY("sl_c5_sneeze_01")
    CALL(evt_party_set_pose, 0, STRING("A_2B"))
    PARTY("sl_c5_sneeze_02")
    CALL(evt_party_set_pose, 0, STRING("S_1"))
    SAY("sl_c5_sneeze_03")
    /* "Hold it in! Press B!": it never holds */
    CALL(evt_sl_prompt, STRING("sl_c5_sneeze_prompt"), 3000)
    SET_VALUE(LW(3), 0)
    LOOP(180)
        CALL(evt_sl_buttons, LW(4))
        IF_INT_NOT_EQUAL(LW(4), 0)
            ADD(LW(3), 1)
            CALL(evt_mario_get_pos, 0, LW(5), LW(6), LW(7))
            CALL(evt_cam_shake, 4, FLOAT(0.0f), FLOAT(0.5f), 80)
        END_IF
        WAIT_FRAMES(1)
    END_LOOP
    CALL(evt_sl_prompt, STRING("sl_c5_sneeze_crack"), 900)
    CALL(evt_snd_sfxon, STRING("SFX_BTL_SYS_BREAK1"), 0)
    WAIT_MS(700)
    CALL(evt_luigi_story_pose, STRING("SL_SNEEZE_B"))
    CALL(evt_snd_sfxon, STRING("SFX_VOICE_MARIO_SURPRISED2"), 0)
    CALL(evt_snd_sfxon, STRING("SFX_AUDIENCE_BOMB_BURST1"), 0)
    CALL(evt_cam_shake, 4, FLOAT(0.0f), FLOAT(6.0f), 900)
    CALL(evt_fade_entry, FADE_OUT_WHITE, 80, 255, 255, 255)
    CALL(evt_fade_end_wait)
    CALL(evt_fade_entry, FADE_IN_WHITE, 500, 255, 255, 255)
    CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
    CALL(evt_eff, 0, STRING("kemuri_test"), 4, LW(0), LW(1), LW(2), FLOAT(2.0f), 0, 0, 0, 0, 0, 0, 0)
    CALL(evt_party_get_pos, 0, LW(4), LW(5), LW(6))
    ADD(LW(4), 40)
    CALL(evt_party_set_pose, 0, STRING("A_2B"))
    CALL(evt_party_jump_pos, 0, LW(4), LW(5), LW(6), 300, 0, 20)
    CALL(evt_snd_sfxon, STRING("SFX_AUDIENCE_PANIC_SCREAM1"), 0)
    PARTY("sl_c5_sneeze_04")
    SAY("sl_c5_sneeze_05")
    CALL(evt_luigi_story_pose, 0)
    CALL(evt_party_set_pose, 0, STRING("S_1"))
    CALL(evt_sl_map_wash, 0, 0xF4F2EA, 70)
    WAIT_MS(1500)
    /* he opens his eyes */
    CALL(evt_npc_set_anim, STRING("sl_cranberry"), STRING("U_1"))
    WAIT_MS(1600)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_PEACH1"))
    CALL(evt_mario_set_dir_npc, STRING("sl_cranberry"))
    CALL(evt_npc_set_anim, STRING("sl_cranberry"), STRING("T_1"))
    NPC("sl_c5_cranberry_00", "sl_cranberry")
    CALL(evt_npc_set_anim, STRING("sl_cranberry"), STRING("S_1"))
    SAY("sl_c5_cranberry_01")
    /* a light from his eyes to Luigi's */
    CALL(evt_npc_get_position, STRING("sl_cranberry"), LW(0), LW(1), LW(2))
    ADD(LW(1), 30)
    CALL(evt_mario_get_pos, 0, LW(3), LW(4), LW(5))
    ADD(LW(4), 40)
    CALL(evt_eff, 0, STRING("small_star"), 3, LW(0), LW(1), LW(2), 0, -1, 0, 3, 0, 0, 0, 0)
    WAIT_MS(300)
    CALL(evt_eff, 0, STRING("small_star"), 3, LW(3), LW(4), LW(5), 0, -1, 0, 3, 0, 0, 0, 0)
    CALL(evt_snd_sfxon, STRING("SFX_BOSS_KAMEBABA_MAGIC1"), 0)
    WAIT_MS(500)
    CALL(evt_npc_set_anim, STRING("sl_cranberry"), STRING("T_1"))
    NPC("sl_c5_cranberry_02", "sl_cranberry")
    SAY("sl_c5_cranberry_03")
    NPC("sl_c5_cranberry_04", "sl_cranberry")
    NPC("sl_c5_cranberry_05", "sl_cranberry")
    CALL(evt_party_set_pose, 0, STRING("E_1"))
    PARTY("sl_c5_cranberry_06")
    CALL(evt_party_set_pose, 0, STRING("S_1"))
    CALL(evt_mario_set_dir_npc, STRING("sl_cranberry"))
    NPC("sl_c5_cranberry_07", "sl_cranberry")
    SAY("sl_c5_cranberry_08")
    NPC("sl_c5_cranberry_09", "sl_cranberry")
    NPC("sl_c5_cranberry_10", "sl_cranberry")
    NPC("sl_c5_cranberry_11", "sl_cranberry")
    SAY("sl_c5_cranberry_12")
    CALL(evt_npc_set_anim, STRING("sl_cranberry"), STRING("S_2"))
    NPC("sl_c5_cranberry_13", "sl_cranberry")
    /* the hand-over */
    CALL(evt_npc_set_anim, STRING("sl_cranberry"), STRING("H_1"))
    WAIT_MS(900)
    CALL(evt_snd_bgmon, PSND_FADE_OUT | 1, STRING("BGM_FF_GET_ITEM1"))
    SET_VALUE(LW(10), STRING("sl_c5_piece"))
    CALL_CHILD(sl5_found)
    SET_VALUE(SLF5(F_PIECE), 1)
    CALL(evt_mario_set_dir_npc, STRING("sl_cranberry"))
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_PEACH1"))
    CALL(evt_npc_set_anim, STRING("sl_cranberry"), STRING("T_1"))
    NPC("sl_c5_cranberry_14", "sl_cranberry")
    SAY("sl_c5_cranberry_15")
    NPC("sl_c5_cranberry_16", "sl_cranberry")
    /* he fades, from the edges in, smiling */
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_ENDING2"))
    CALL(evt_npc_set_anim, STRING("sl_cranberry"), STRING("F_1"))
    CALL(evt_party_get_pos, 0, LW(4), LW(5), LW(6))
    CALL(evt_party_move_pos, 0, -40, -260, 1200)
    PARTY("sl_c5_fade_00")
    CALL(evt_mario_set_dir_npc, STRING("sl_cranberry"))
    CALL(evt_npc_set_anim, STRING("sl_cranberry"), STRING("T_1"))
    NPC("sl_c5_fade_01", "sl_cranberry")
    CALL(evt_npc_set_anim, STRING("sl_cranberry"), STRING("F_1"))
    PARTY("sl_c5_fade_02")
    SET_VALUE(LW(3), 255)
    LOOP(51)
        SUBTRACT(LW(3), 5)
        CALL(evt_npc_set_color, STRING("sl_cranberry"), 255, 255, 255, LW(3))
        SET_VALUE(LW(4), 255)
        SUBTRACT(LW(4), LW(3))
        DIVIDE(LW(4), 9)
        ADD(LW(4), 70)
        CALL(evt_sl_map_wash, 0, 0xFFFFFF, LW(4))
        WAIT_FRAMES(3)
    END_LOOP
    CALL(evt_npc_delete, STRING("sl_cranberry"))
    CALL(evt_fade_entry, FADE_OUT_WHITE, 1500, 255, 255, 255)
    CALL(evt_fade_end_wait)
    CALL(evt_sl_map_wash, 0, 0xFFFFFF, 88)
    WAIT_MS(1200)
    CALL(evt_snd_bgmoff, PSND_FADE_OUT)
    /* the light fills the screen: and they're in the dark wood */
    CALL(evt_seq_set_seq, SEQ_MAP_CHANGE, STRING("win_l50"), STRING("sl_night"))
    RETURN
EVT_END()

static EVT_BEGIN(tik_l50_socket)
    CALL(evt_mario_key_onoff, 0)
    /* LW(1): the socket; the lens it wants is LW(2)'s flag, set LW(3)'s */
    SWITCH(LW(1))
        CASE_EQUAL(0)
            SET_VALUE(LW(2), SLF5(F_MOON))
            SET_VALUE(LW(3), SLF5(F_SET_MOON))
        CASE_EQUAL(1)
            SET_VALUE(LW(2), SLF5(F_SUN))
            SET_VALUE(LW(3), SLF5(F_SET_SUN))
        CASE_DEFAULT
            SET_VALUE(LW(2), SLF5(F_TEAR))
            SET_VALUE(LW(3), SLF5(F_SET_TEAR))
    END_SWITCH
    IF_INT_EQUAL(LW(3), 1)
        SYS("sl_c5_socket_lit")
        CALL(evt_mario_key_onoff, 1)
        RETURN
    END_IF
    IF_INT_EQUAL(LW(2), 0)
        SWITCH(LW(1))
            CASE_EQUAL(0)
                SYS("sl_c5_socket_0")
            CASE_EQUAL(1)
                SYS("sl_c5_socket_1")
            CASE_DEFAULT
                SYS("sl_c5_socket_2")
        END_SWITCH
        CALL(evt_mario_key_onoff, 1)
        RETURN
    END_IF
    /* the lens clicks in, and a beam rises */
    CALL(evt_snd_sfxon, STRING("SFX_MOBJ_UNLOCK1"), 0)
    SWITCH(LW(1))
        CASE_EQUAL(0)
            SET_VALUE(SLF5(F_SET_MOON), 1)
            SYS("sl_c5_set_moon")
        CASE_EQUAL(1)
            SET_VALUE(SLF5(F_SET_SUN), 1)
            SYS("sl_c5_set_sun")
        CASE_DEFAULT
            SET_VALUE(SLF5(F_SET_TEAR), 1)
            SYS("sl_c5_set_tear")
    END_SWITCH
    SWITCH(LW(1))
        CASE_EQUAL(0)
            SET_VALUE(LW(9), STRING("sl_sock0"))
        CASE_EQUAL(1)
            SET_VALUE(LW(9), STRING("sl_sock1"))
        CASE_DEFAULT
            SET_VALUE(LW(9), STRING("sl_sock2"))
    END_SWITCH
    CALL(evt_npc_set_anim, LW(9), STRING("P_1"))
    WAIT_MS(500)
    CALL(evt_npc_set_anim, LW(9), STRING("L_1"))
    CALL_CHILD(tik_l50_beam)
    CALL(evt_npc_set_anim, LW(9), STRING("L_2"))
    CALL_CHILD(tik_l50_lights)
    IF_INT_EQUAL(LW(0), 3)
        CALL_CHILD(tik_l50_awaken)
        RETURN
    END_IF
    CALL(evt_party_set_pose, 0, STRING("A_2B"))
    CALL(evt_snd_sfxon, STRING("SFX_AUDIENCE_PANIC_SCREAM1"), 0)
    SCREAMY("sl_c5_lens_joy")
    CALL(evt_party_set_pose, 0, STRING("S_1"))
    IF_INT_EQUAL(LW(0), 1)
        /* the first light, whichever lens, shows the great door: it opens (it was the Moon socket's
         * alone, and the Sun Lens set first left the door shut) */
        CALL(evt_snd_sfxon, STRING("SFX_STG1_QUAKE2"), 0)
        CALL(evt_cam_shake, 4, FLOAT(0.0f), FLOAT(1.5f), 800)
        SET_VALUE(LW(0), STRING("n_bero"))
        CALL_CHILD(bero_case_switch_on)
        CALL(evt_mobj_recovery_blk, STRING("mobj_heart"), 0, 300, 60, 120, 0, 0)
        PARTY("sl_c5_door_open")
    END_IF
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(tik_l50_socket0)
    SET_VALUE(LW(1), 0)
    CALL_CHILD(tik_l50_socket)
    RETURN
EVT_END()
static EVT_BEGIN(tik_l50_socket1)
    SET_VALUE(LW(1), 1)
    CALL_CHILD(tik_l50_socket)
    RETURN
EVT_END()
static EVT_BEGIN(tik_l50_socket2)
    SET_VALUE(LW(1), 2)
    CALL_CHILD(tik_l50_socket)
    RETURN
EVT_END()

static EVT_BEGIN(tik_l50_init)
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG0_TIK4"))
    IF_INT_EQUAL(SLF5(F_PIECE), 0)
        CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_STG2_PUNI1"))
    END_IF
    SET_VALUE(LW(0), PTR(tik_l50_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL(sl5_survey)
    CALL_CHILD(sl5_white_room)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("n_yuka1"), MAPOBJ_FLAG_HIDE)
    CALL(evt_mobj_save_blk, STRING("mobj_save"), 420, 60, 300, 0, 0)
    /* the west door is no door until Screamy Looks; the great door, until the first light */
    IF_INT_EQUAL(SLF5(F_FAKEDOOR), 0)
        SET_VALUE(LW(0), STRING("w_bero"))
        CALL_CHILD(bero_case_switch_off)
        SPAWN_EVENT(tik_l50_westdoor)
    ELSE
        CALL(evt_sl_map_look, STRING("door"), 0xFFFFFF, 0x6080C0)
    END_IF
    CALL_CHILD(tik_l50_lights)
    IF_INT_EQUAL(LW(0), 0)
        SET_VALUE(LW(0), STRING("n_bero"))
        CALL_CHILD(bero_case_switch_off)
    ELSE
        CALL(evt_mobj_recovery_blk, STRING("mobj_heart"), 0, 300, 60, 120, 0, 0)
    END_IF
    IF_INT_EQUAL(SLF5(F_PIECE), 0)
        CALL(evt_npc_entry, STRING("sl_cranberry"), STRING(M_CRANBERRY))
        CALL(evt_npc_set_tribe, STRING("sl_cranberry"), STRING(SJ5_SPOT_TRIBE))
    CALL(evt_npc_set_width, STRING("sl_cranberry"), 60)
        CALL(evt_npc_set_position, STRING("sl_cranberry"), 0, 25, -340)
        CALL(evt_npc_set_anim, STRING("sl_cranberry"), STRING("N_1"))
        CALL(evt_npc_flag_onoff, 1, STRING("sl_cranberry"), NPC_FLAG_NO_TURN_TO_MARIO)
        CALL(evt_npc_change_interrupt, STRING("sl_cranberry"), 6, PTR(tik_l50_cranberry_check))
    END_IF
    PROP("sl_sock0", M_SOCKET_MOON, -110, 25, -290, "S_1")
    PROP("sl_sock1", M_SOCKET_SUN, 0, 25, -230, "S_1")
    PROP("sl_sock2", M_SOCKET_TEAR, 110, 25, -290, "S_1")
    CALL(evt_npc_set_scale, STRING("sl_sock0"), FLOAT(1.0f), FLOAT(1.0f), FLOAT(1.0f))
    SPOT("sl_socket0", -110, 25, -290, tik_l50_socket0)
    SPOT("sl_socket1", 0, 25, -230, tik_l50_socket1)
    SPOT("sl_socket2", 110, 25, -290, tik_l50_socket2)
    /* the lit sockets keep their light */
    IF_INT_EQUAL(SLF5(F_SET_MOON), 1)
        CALL(evt_npc_set_anim, STRING("sl_sock0"), STRING("L_2"))
    END_IF
    IF_INT_EQUAL(SLF5(F_SET_SUN), 1)
        CALL(evt_npc_set_anim, STRING("sl_sock1"), STRING("L_2"))
    END_IF
    IF_INT_EQUAL(SLF5(F_SET_TEAR), 1)
        CALL(evt_npc_set_anim, STRING("sl_sock2"), STRING("L_2"))
    END_IF
    /* Float: from the chamber floor up onto the west ledge (the floor's fallen in there); Look: the door */
    CALL(evt_sl_float_point, 0, -340, 0, 40, -480, 100, 40)
    CALL(evt_sl_look_point, 0, -380, 0, 40, 260, PTR(tik_l50_look))
    IF_INT_EQUAL(SLF5(F_SCREAMY), 0)
        SPAWN_EVENT(tik_l50_screamy)
    END_IF
    RETURN
EVT_END()

/* ======================================================================
 * hei_l51, the Hall of Still Water (hei_10)
 * ====================================================================== */

static BeroEntry hei_l51_bero[] = {
    BERO("w_bero", "hei_l52", "e_bero"),
    BERO("e_bero", "hei_l54", "w_bero"),
    {"dokan", 2, 0, BERO_DIR_W, BERO_POS_AUTO, 0, 0, -1, NULL, 10, NULL, "tik_l50", "w_bero", 1, 1, 0, 0},
    {0},
};

static EVT_BEGIN(hei_l51_look)
    SET_VALUE(LW(10), 170)
    SET_VALUE(LW(11), 45)
    SET_VALUE(LW(12), 30)
    IF_INT_EQUAL(SLF5(F_H51_SHINE), 0)
        CALL_CHILD(sl5_glint)
        PARTY("sl_c5_look_block")
    END_IF
    RETURN
EVT_END()

static EVT_BEGIN(hei_l51_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG1_TORIDE1"))
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG1_HEI4"))
    CALL(evt_sl_enemy, STRING("sl_e1"), STRING(SJ5_FUZZY), STRING(SJ5_FUZZY), 120, 0, 50, 60, 30, STRING("BTLNO_HEI_09_01"))
    CALL(evt_sl_enemy, STRING("sl_e2"), STRING(SJ5_FUZZY), STRING(SJ5_FUZZY), 60, 0, -50, 60, 20, STRING("BTLNO_HEI_11_01"))
    SET_VALUE(LW(0), PTR(hei_l51_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL(sl5_survey)
    CALL(evt_map_playanim, STRING("mizu"), 1, 0)
    CALL_CHILD(sl5_white_room)
    CALL(evt_mobj_badgeblk, STRING("mobj_item"), -40, 45, 0, ITEM_SUPER_SHROOM, 0, SLF5(F_H51_ITEM), 0)
    CALL(evt_mobj_brick, STRING("mobj_hidden"), 170, 45, 30, ITEM_SHINE_SPRITE, 11, 0, SLF5(F_H51_SHINE))
    CALL(evt_sl_look_point, 0, 170, 0, 30, 300, PTR(hei_l51_look))
    RETURN
EVT_END()

/* ======================================================================
 * hei_l52, the Gallery of Murals (hei_09)
 * ====================================================================== */

static BeroEntry hei_l52_bero[] = {BERO("e_bero", "hei_l51", "w_bero"), BERO("w_bero", "hei_l53", "e_bero"), {0}};

CHEST_EVT(hei_l52_chest_open, "mobj_box", ITEM_REFUND)

/* a mural: LW(1) which (0-5) */
static EVT_BEGIN(hei_l52_mural)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_mario_get_party, LW(15))
    IF_INT_NOT_EQUAL(LW(15), SCREAMY_SLOT)
        SYS("sl_c5_mural_faded")
        CALL(evt_mario_key_onoff, 1)
        RETURN
    END_IF
    SWITCH(LW(1))
        CASE_EQUAL(0)
            PARTY("sl_c5_mural1")
        CASE_EQUAL(1)
            PARTY("sl_c5_mural2")
        CASE_EQUAL(2)
            PARTY("sl_c5_mural3")
        CASE_EQUAL(3)
            PARTY("sl_c5_mural4")
        CASE_EQUAL(4)
            PARTY("sl_c5_mural5")
        CASE_DEFAULT
            SYS("sl_c5_mural6")
            IF_INT_EQUAL(SLF5(F_MURAL6), 0)
                SET_VALUE(SLF5(F_MURAL6), 1)
                PARTY("sl_c5_mural_end_00")
                SAY("sl_c5_mural_end_01")
                CALL(evt_party_set_pose, 0, STRING("A_2B"))
                CALL(evt_snd_sfxon, STRING("SFX_AUDIENCE_PANIC_SCREAM1"), 0)
                PARTY("sl_c5_mural_end_02")
                CALL(evt_party_set_pose, 0, STRING("S_1"))
                SAY("sl_c5_mural_end_03")
                CALL(evt_party_set_pose, 0, STRING("E_1"))
                PARTY("sl_c5_mural_end_04")
                CALL(evt_party_set_pose, 0, STRING("S_1"))
                PARTY("sl_c5_mural_crack")
            END_IF
    END_SWITCH
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(hei_l52_mural0)
    SET_VALUE(LW(1), 0)
    CALL_CHILD(hei_l52_mural)
    RETURN
EVT_END()
static EVT_BEGIN(hei_l52_mural1)
    SET_VALUE(LW(1), 1)
    CALL_CHILD(hei_l52_mural)
    RETURN
EVT_END()
static EVT_BEGIN(hei_l52_mural2)
    SET_VALUE(LW(1), 2)
    CALL_CHILD(hei_l52_mural)
    RETURN
EVT_END()
static EVT_BEGIN(hei_l52_mural3)
    SET_VALUE(LW(1), 3)
    CALL_CHILD(hei_l52_mural)
    RETURN
EVT_END()
static EVT_BEGIN(hei_l52_mural4)
    SET_VALUE(LW(1), 4)
    CALL_CHILD(hei_l52_mural)
    RETURN
EVT_END()

/* the cracked sixth mural, blown open */
static EVT_BEGIN(hei_l52_blast)
    CALL(evt_mario_key_onoff, 0)
    IF_INT_EQUAL(SLF5(F_MURAL_OPEN), 0)
        SET_VALUE(SLF5(F_MURAL_OPEN), 1)
        CALL(evt_snd_sfxon, STRING("SFX_AUDIENCE_BOMB_BURST1"), 0)
        CALL(evt_eff, 0, STRING("bomb"), 0, -400, 60, -90, FLOAT(1.5f), 0, 0, 0, 0, 0, 0, 0)
        CALL(evt_npc_set_anim, STRING("sl_card5"), STRING("B_1"))
        CALL(evt_cam_shake, 4, FLOAT(0.0f), FLOAT(2.0f), 400)
        CALL(evt_sl_point_off, PORT_SL_ABILITY_BLAST, 0)
        WAIT_MS(400)
        CALL(evt_mobj_itembox, STRING("mobj_box"), -320, -15, -20, 0, 0, PTR(hei_l52_chest_open), SLF5(F_REFUND))
        SAY("sl_c5_mural_open")
    END_IF
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(hei_l52_mural5)
    SET_VALUE(LW(1), 5)
    CALL_CHILD(hei_l52_mural)
    IF_INT_EQUAL(SLF5(F_MURAL6), 1)
        IF_INT_EQUAL(SLF5(F_MURAL_OPEN), 0)
            SET_VALUE(LW(10), PORT_SL_FRIEND_JERRY)
            SET_VALUE(LW(11), STRING("sl_c5_mural_push"))
            SET_VALUE(LW(12), PTR(hei_l52_blast))
            SET_VALUE(LW(13), STRING("sl_c5_mural_hint"))
            CALL_CHILD(sl5_ability_or_hint)
        END_IF
    END_IF
    RETURN
EVT_END()

static EVT_BEGIN(hei_l52_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG1_TORIDE1"))
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG1_HEI4"))
    CALL(evt_sl_enemy, STRING("sl_e1"), STRING(SJ5_BALD_CLEFT), STRING(SJ5_BALD_CLEFT), -200, 0, 40, 100, 40, STRING("BTLNO_HEI_03_01"))
    CALL(evt_sl_enemy, STRING("sl_e2"), STRING(SJ5_FUZZY), STRING(SJ5_FUZZY), 250, 0, 40, 100, 40, STRING("BTLNO_HEI_11_01"))
    SET_VALUE(LW(0), PTR(hei_l52_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL(sl5_survey)
    CALL(evt_map_playanim, STRING("mizu"), 1, 0)
    CALL_CHILD(sl5_white_room)
    PROP("sl_card0", M_MURAL, 400, -25, -103, "M_1")
    PROP("sl_card1", M_MURAL, 240, -25, -103, "M_2")
    PROP("sl_card2", M_MURAL, 80, -25, -103, "M_3")
    PROP("sl_card3", M_MURAL, -80, -25, -103, "M_4")
    PROP("sl_card4", M_MURAL, -240, -25, -103, "M_5")
    PROP("sl_card5", M_MURAL, -400, -25, -103, "M_6")
    IF_INT_EQUAL(SLF5(F_MURAL_OPEN), 1)
        CALL(evt_npc_set_anim, STRING("sl_card5"), STRING("M_6B"))
    END_IF
    SPOT("sl_mural0", 400, 0, -70, hei_l52_mural0)
    SPOT("sl_mural1", 240, 0, -70, hei_l52_mural1)
    SPOT("sl_mural2", 80, 0, -70, hei_l52_mural2)
    SPOT("sl_mural3", -80, 0, -70, hei_l52_mural3)
    SPOT("sl_mural4", -240, 0, -70, hei_l52_mural4)
    SPOT("sl_mural5", -400, 0, -70, hei_l52_mural5)
    IF_INT_EQUAL(SLF5(F_MURAL_OPEN), 0)
        CALL(evt_sl_point, PORT_SL_ABILITY_BLAST, 0, -400, 0, -60, 110, 0, PTR(hei_l52_blast))
    ELSE
        CALL(evt_mobj_itembox, STRING("mobj_box"), -320, -15, -20, 0, 0, PTR(hei_l52_chest_open), SLF5(F_REFUND))
    END_IF
    RETURN
EVT_END()

/* ======================================================================
 * hei_l53, the Moon Room (hei_08): the gear-gate (Tune-up), the Moon Lens
 * ====================================================================== */

static BeroEntry hei_l53_bero[] = {BERO("e_bero", "hei_l52", "w_bero"), {0}};

static EVT_BEGIN(hei_l53_cleaned)
    CALL(evt_mario_key_onoff, 0)
    IF_INT_EQUAL(SLF5(F_GEAR_CLEAN), 0)
        SET_VALUE(SLF5(F_GEAR_CLEAN), 1)
        CALL(evt_sl_point_off, PORT_SL_ABILITY_TUNE, 0)
        CALL(evt_snd_sfxon, STRING("SFX_MOBJ_UNLOCK1"), 0)
        /* at the gear: the model's (-13, 10), the gate at 1.6 */
        CALL(evt_eff, 0, STRING("kemuri_test"), 4, -72, 55, 30, FLOAT(1.0f), 0, 0, 0, 0, 0, 0, 0)
        CALL(evt_sl_where, STRING("the gear cleaned"))
        CALL(evt_npc_set_anim, STRING("sl_gate"), STRING("S_2"))
        CALL(evt_mario_get_party, LW(0))
        IF_INT_EQUAL(LW(0), FRIEND_TORQUE_SLOT)
            PARTY("sl_c5_gear_torque")
        END_IF
        SYS("sl_c5_gear_clean")
    END_IF
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* push the gear: four in a row (A each time the gate settles), or it slips back */
static EVT_BEGIN(hei_l53_gear)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_sl_where, STRING("the gear pushed"))
    IF_INT_EQUAL(SLF5(F_GEAR_OPEN), 1)
        SYS("sl_c5_gear_up")
        CALL(evt_mario_key_onoff, 1)
        RETURN
    END_IF
    IF_INT_EQUAL(SLF5(F_GEAR_CLEAN), 0)
        SYS("sl_c5_gear_stuck")
        SET_VALUE(LW(10), PORT_SL_FRIEND_TORQUE)
        SET_VALUE(LW(11), STRING("sl_c5_gear_scrub"))
        SET_VALUE(LW(12), PTR(hei_l53_cleaned))
        SET_VALUE(LW(13), STRING("sl_c5_gear_hint"))
        CALL(evt_mario_key_onoff, 1)
        CALL_CHILD(sl5_ability_or_hint)
        RETURN
    END_IF
    /* forgiving: the way is told once; the A that closes that box, or (told already) the A that reaches
     * the gear, is the first push. A push wants A within two seconds of the last, and an A pressed while
     * the gate is still moving from the last push counts for the next: no press is lost to a box or a shake */
    SET_VALUE(LW(5), 0)
    SET_VALUE(LW(9), 4)
    SET_VALUE(LW(10), 1)
    SET_VALUE(LW(11), 120)
    IF_INT_EQUAL(SLF5(F_GEAR_TOLD), 0)
        SYS("sl_c5_gear_push")
        SET_VALUE(SLF5(F_GEAR_TOLD), 1)
    END_IF
    LOOP(LW(9))
        SET_VALUE(LW(6), LW(10))
        SET_VALUE(LW(10), 0)
        IF_INT_EQUAL(LW(6), 0)
            LOOP(LW(11))
                CALL(sl5_note, PAD_BUTTON_A, LW(7))
                IF_INT_EQUAL(LW(7), 1)
                    SET_VALUE(LW(6), 1)
                    BREAK_LOOP
                END_IF
                WAIT_FRAMES(1)
            END_LOOP
        END_IF
        SET_VALUE(LW(11), 120)
        IF_INT_EQUAL(LW(6), 0)
            BREAK_LOOP
        END_IF
        ADD(LW(5), 1)
        SWITCH(LW(5))
            CASE_EQUAL(1)
                CALL(evt_npc_set_anim, STRING("sl_gate"), STRING("P_1"))
            CASE_EQUAL(2)
                CALL(evt_npc_set_anim, STRING("sl_gate"), STRING("P_2"))
            CASE_EQUAL(3)
                CALL(evt_npc_set_anim, STRING("sl_gate"), STRING("P_3"))
            CASE_DEFAULT
                CALL(evt_npc_set_anim, STRING("sl_gate"), STRING("P_4"))
        END_SWITCH
        CALL(evt_mario_set_pose, STRING("M_P_1"))
        CALL(evt_snd_sfxon, STRING("SFX_STG1_DSN_ROUND1"), 0)
        CALL(evt_cam_shake, 4, FLOAT(0.0f), FLOAT(0.6f), 200)
        LOOP(12)
            CALL(sl5_note, PAD_BUTTON_A, LW(7))
            IF_INT_EQUAL(LW(7), 1)
                SET_VALUE(LW(10), 1)
            END_IF
            WAIT_FRAMES(1)
        END_LOOP
        CALL(evt_mario_set_pose, STRING("M_S_1"))
    END_LOOP
    IF_INT_LESS(LW(5), 4)
        SWITCH(LW(5))
            CASE_EQUAL(0)
                CALL(evt_npc_set_anim, STRING("sl_gate"), STRING("J_1"))
            CASE_EQUAL(1)
                CALL(evt_npc_set_anim, STRING("sl_gate"), STRING("D_1"))
            CASE_EQUAL(2)
                CALL(evt_npc_set_anim, STRING("sl_gate"), STRING("D_2"))
            CASE_DEFAULT
                CALL(evt_npc_set_anim, STRING("sl_gate"), STRING("D_3"))
        END_SWITCH
        CALL(evt_snd_sfxon, STRING("SFX_STG1_QUAKE2"), 0)
        WAIT_MS(400)
        CALL(evt_npc_set_anim, STRING("sl_gate"), STRING("S_2"))
        CALL(evt_sl_where, STRING("the gear slipped back"))
        SYS("sl_c5_gear_slip")
        CALL(evt_mario_key_onoff, 1)
        RETURN
    END_IF
    SET_VALUE(SLF5(F_GEAR_OPEN), 1)
    CALL(evt_sl_where, STRING("the gear gate up"))
    CALL(evt_npc_set_anim, STRING("sl_gate"), STRING("P_5"))
    CALL(evt_snd_sfxon, STRING("SFX_STG1_QUAKE1"), 0)
    CALL(evt_cam_shake, 4, FLOAT(0.0f), FLOAT(1.5f), 900)
    SYS("sl_c5_gear_open")
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(hei_l53_lens)
    CALL(evt_mario_key_onoff, 0)
    IF_INT_EQUAL(SLF5(F_MOON), 1)
        SYS("sl_c5_pedestal_empty")
        CALL(evt_mario_key_onoff, 1)
        RETURN
    END_IF
    IF_INT_EQUAL(SLF5(F_GEAR_OPEN), 0)
        SYS("sl_c5_moon_barred")
        CALL(evt_mario_key_onoff, 1)
        RETURN
    END_IF
    SET_VALUE(SLF5(F_MOON), 1)
    CALL(evt_eff, STRING(""), STRING("stardust"), 2, 0, 40, 0, 30, 30, 30, 60, 0, 0, 0, 0)
    SET_VALUE(LW(10), STRING("sl_c5_moon_got"))
    CALL_CHILD(sl5_found)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG1_TORIDE1"))
    CALL(evt_party_set_pose, 0, STRING("A_2B"))
    CALL(evt_snd_sfxon, STRING("SFX_AUDIENCE_PANIC_SCREAM1"), 0)
    SCREAMY("sl_c5_lens_found")
    CALL(evt_party_set_pose, 0, STRING("S_1"))
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(hei_l53_look)
    SET_VALUE(LW(10), -170)
    SET_VALUE(LW(11), 10)
    SET_VALUE(LW(12), 50)
    IF_INT_EQUAL(SLF5(F_MOON_PIECE), 0)
        CALL_CHILD(sl5_glint)
        CALL(evt_item_entry, STRING("item_piece"), ITEM_STAR_PIECE, -170, -15, 50, ITEM_ENTRY_STAY, SLF5(F_MOON_PIECE), 0)
        PARTY("sl_c5_look_floor")
    END_IF
    RETURN
EVT_END()

static EVT_BEGIN(hei_l53_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG1_TORIDE1"))
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG1_HEI4"))
    CALL(evt_sl_enemy, STRING("sl_e1"), STRING(SJ5_CLEFT), STRING(SJ5_CLEFT), -200, -15, -40, 30, 30, STRING("BTLNO_HEI_03_01"))
    SET_VALUE(LW(0), PTR(hei_l53_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL(sl5_survey)
    CALL(evt_map_playanim, STRING("mizu"), 1, 0)
    CALL_CHILD(sl5_white_room)
    /* the gear-gate before the pedestal, its gear on the wall at its left */
    PROP("sl_gate", M_GEAR_GATE, 0, 0, 30, "S_1")
    /* at 1.0 the gate stood shorter than Luigi; 1.6 is a gate he'd walk through (its gear goes out to -72) */
    CALL(evt_npc_set_scale, STRING("sl_gate"), FLOAT(1.6f), FLOAT(1.6f), FLOAT(1.6f))
    IF_INT_EQUAL(SLF5(F_GEAR_CLEAN), 1)
        CALL(evt_npc_set_anim, STRING("sl_gate"), STRING("S_2"))
    END_IF
    IF_INT_EQUAL(SLF5(F_GEAR_OPEN), 1)
        CALL(evt_npc_set_anim, STRING("sl_gate"), STRING("N_5"))
    END_IF
    SPOT("sl_gear", -72, 0, 30, hei_l53_gear)
    SPOT("sl_pedestal", 25, 0, -10, hei_l53_lens)
    IF_INT_EQUAL(SLF5(F_GEAR_CLEAN), 0)
        CALL(evt_sl_point, PORT_SL_ABILITY_TUNE, 0, -72, 0, 30, 110, 0, PTR(hei_l53_cleaned))
    END_IF
    IF_INT_EQUAL(SLF5(F_MOON), 0)
        CALL(evt_eff, STRING(""), STRING("stardust"), 2, 0, 40, 0, 10, 10, 10, 400, 0, 0, 0, 0)
    END_IF
    CALL(evt_sl_look_point, 0, -170, 0, 50, 260, PTR(hei_l53_look))
    RETURN
EVT_END()

/* ======================================================================
 * hei_l54, the Long Echo (hei_11): three echo stones, a bridge of light
 * ====================================================================== */

static BeroEntry hei_l54_bero[] = {BERO("w_bero", "hei_l51", "e_bero"), BERO("e_bero", "hei_l55", "w_bero"), {0}};

CHEST_EVT(hei_l54_chest_open, "mobj_box", ITEM_FP_PLUS)

/* the stones lit and the bars of light standing, from the flags */
static EVT_BEGIN(hei_l54_lights)
    IF_INT_EQUAL(SLF5(F_ECHO1), 1)
        CALL(evt_npc_set_anim, STRING("sl_echo0"), STRING("L_2"))
        CALL(evt_npc_flag_onoff, 1, STRING("sl_bar0"), NPC_FLAG_HIDDEN)
    END_IF
    IF_INT_EQUAL(SLF5(F_ECHO2), 1)
        CALL(evt_npc_set_anim, STRING("sl_echo1"), STRING("L_2"))
        CALL(evt_npc_flag_onoff, 1, STRING("sl_bar1"), NPC_FLAG_HIDDEN)
    END_IF
    IF_INT_EQUAL(SLF5(F_ECHO3), 1)
        CALL(evt_npc_set_anim, STRING("sl_echo2"), STRING("L_2"))
        CALL(evt_npc_flag_onoff, 1, STRING("sl_bar2"), NPC_FLAG_HIDDEN)
    END_IF
    RETURN
EVT_END()

/* a phrase: LW(1) the stone; its notes are sung back (A/B/X/Y). LW(2)-LW(6) the notes, LW(7) how many */
static EVT_BEGIN(hei_l54_echo)
    CALL(evt_mario_key_onoff, 0)
    SWITCH(LW(1))
        CASE_EQUAL(0)
            SET_VALUE(LW(8), SLF5(F_ECHO1))
        CASE_EQUAL(1)
            SET_VALUE(LW(8), SLF5(F_ECHO2))
        CASE_DEFAULT
            SET_VALUE(LW(8), SLF5(F_ECHO3))
    END_SWITCH
    IF_INT_EQUAL(LW(8), 1)
        SYS("sl_c5_echo_done")
        CALL(evt_mario_key_onoff, 1)
        RETURN
    END_IF
    /* Luigi taps it with the hammer; it sings */
    CALL(evt_mario_set_pose, STRING("M_H_1"))
    CALL(evt_snd_sfxon, STRING("SFX_MARIO_HAMMER_WOOD_DON1"), 0)
    WAIT_MS(500)
    CALL(evt_mario_set_pose, STRING("M_S_1"))
    SWITCH(LW(1))
        CASE_EQUAL(0)
            SYS("sl_c5_echo_1")
        CASE_EQUAL(1)
            SYS("sl_c5_echo_2")
        CASE_DEFAULT
            SYS("sl_c5_echo_3")
    END_SWITCH
    CALL(evt_mario_get_party, LW(9))
    IF_INT_EQUAL(LW(9), FRIEND_HAYZEE_SLOT)
        PARTY("sl_c5_echo_hayzee")
    ELSE
        SYS("sl_c5_echo_sing")
    END_IF
    /* the notes back, one press each (the press that closed the box isn't one) */
    WAIT_FRAMES(12)
    SET_VALUE(LW(9), 0)
    SET_VALUE(LW(10), 0)
    LOOP(LW(7))
        SWITCH(LW(10))
            CASE_EQUAL(0)
                SET_VALUE(LW(11), LW(2))
            CASE_EQUAL(1)
                SET_VALUE(LW(11), LW(3))
            CASE_EQUAL(2)
                SET_VALUE(LW(11), LW(4))
            CASE_EQUAL(3)
                SET_VALUE(LW(11), LW(5))
            CASE_DEFAULT
                SET_VALUE(LW(11), LW(6))
        END_SWITCH
        SET_VALUE(LW(12), 0)
        LOOP(300)
            CALL(sl5_note, LW(11), LW(12))
            IF_INT_NOT_EQUAL(LW(12), 0)
                BREAK_LOOP
            END_IF
            WAIT_FRAMES(1)
        END_LOOP
        IF_INT_NOT_EQUAL(LW(12), 1)
            SET_VALUE(LW(9), 1)
            BREAK_LOOP
        END_IF
        CALL(evt_snd_sfxon, STRING("SFX_SYSTEM_GOOD1"), 0)
        ADD(LW(10), 1)
        /* one press is one note: the next is read from the next frame */
        CALL(evt_mario_get_pos, 0, LW(13), LW(14), LW(15))
        ADD(LW(14), 40)
        CALL(evt_eff, 0, STRING("small_star"), 3, LW(13), LW(14), LW(15), 0, -1, 0, 3, 0, 0, 0, 0)
        WAIT_FRAMES(1)
    END_LOOP
    IF_INT_EQUAL(LW(9), 1)
        CALL(evt_snd_sfxon, STRING("SFX_CURSOR_NOT_CHOICE1"), 0)
        SYS("sl_c5_echo_wrong")
        CALL(evt_mario_key_onoff, 1)
        RETURN
    END_IF
    /* right: a section of the bridge of light */
    SWITCH(LW(1))
        CASE_EQUAL(0)
            SET_VALUE(SLF5(F_ECHO1), 1)
        CASE_EQUAL(1)
            SET_VALUE(SLF5(F_ECHO2), 1)
        CASE_DEFAULT
            SET_VALUE(SLF5(F_ECHO3), 1)
            CALL(evt_mario_get_party, LW(9))
            IF_INT_EQUAL(LW(9), FRIEND_HAYZEE_SLOT)
                PARTY("sl_c5_echo_keychange")
            END_IF
    END_SWITCH
    CALL(evt_snd_sfxon, STRING("SFX_BOSS_KAMEBABA_MAGIC1"), 0)
    CALL(evt_eff, STRING(""), STRING("stardust"), 2, 380, 40, 0, 20, 60, 60, 90, 0, 0, 0, 0)
    CALL_CHILD(hei_l54_lights)
    SYS("sl_c5_echo_bridge")
    IF_INT_EQUAL(SLF5(F_ECHO1), 1)
        IF_INT_EQUAL(SLF5(F_ECHO2), 1)
            IF_INT_EQUAL(SLF5(F_ECHO3), 1)
                SYS("sl_c5_echo_whole")
                SET_VALUE(LW(0), STRING("e_bero"))
                CALL_CHILD(bero_case_switch_on)
                CALL(evt_mobj_itembox, STRING("mobj_box"), 420, -15, -20, 0, 0, PTR(hei_l54_chest_open), SLF5(F_FPPLUS))
            END_IF
        END_IF
    END_IF
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(hei_l54_stone0)
    SET_VALUE(LW(1), 0)
    SET_VALUE(LW(2), PAD_BUTTON_A)
    SET_VALUE(LW(3), PAD_BUTTON_A)
    SET_VALUE(LW(4), PAD_BUTTON_B)
    SET_VALUE(LW(5), PAD_BUTTON_A)
    SET_VALUE(LW(7), 4)
    CALL_CHILD(hei_l54_echo)
    RETURN
EVT_END()
static EVT_BEGIN(hei_l54_stone1)
    SET_VALUE(LW(1), 1)
    SET_VALUE(LW(2), PAD_BUTTON_X)
    SET_VALUE(LW(3), PAD_BUTTON_B)
    SET_VALUE(LW(4), PAD_BUTTON_A)
    SET_VALUE(LW(5), PAD_BUTTON_Y)
    SET_VALUE(LW(7), 4)
    CALL_CHILD(hei_l54_echo)
    RETURN
EVT_END()
static EVT_BEGIN(hei_l54_stone2)
    SET_VALUE(LW(1), 2)
    SET_VALUE(LW(2), PAD_BUTTON_Y)
    SET_VALUE(LW(3), PAD_BUTTON_X)
    SET_VALUE(LW(4), PAD_BUTTON_Y)
    SET_VALUE(LW(5), PAD_BUTTON_B)
    SET_VALUE(LW(6), PAD_BUTTON_A)
    SET_VALUE(LW(7), 5)
    CALL_CHILD(hei_l54_echo)
    RETURN
EVT_END()

/* the drop at the passage's end, until the bridge is whole */
static EVT_BEGIN(hei_l54_edge)
    LOOP(0)
        IF_INT_EQUAL(SLF5(F_ECHO3), 1)
            IF_INT_EQUAL(SLF5(F_ECHO2), 1)
                IF_INT_EQUAL(SLF5(F_ECHO1), 1)
                    BREAK_LOOP
                END_IF
            END_IF
        END_IF
        CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
        IF_INT_GREATER(LW(0), 360)
            CALL(evt_mario_key_onoff, 0)
            CALL(evt_mario_mov_pos2, 320, LW(2), FLOAT(120.0f))
            SAY("sl_c5_echo_drop")
            CALL(evt_mario_key_onoff, 1)
        END_IF
        WAIT_FRAMES(2)
    END_LOOP
    RETURN
EVT_END()

static EVT_BEGIN(hei_l54_enter)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_seq_wait, 2)
    CALL(evt_bero_exec_wait, 65536)
    WAIT_MS(300)
    SAY("sl_c5_echo_see")
    SET_VALUE(SLF5(F_ECHO_SEEN), 1)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(hei_l54_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG1_TORIDE1"))
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG1_HEI4"))
    CALL(evt_sl_enemy, STRING("sl_e1"), STRING(SJ5_BRISTLE), STRING(SJ5_BRISTLE), 150, -15, 40, 60, 30, STRING("BTLNO_HEI_05_01"))
    CALL(evt_sl_enemy, STRING("sl_e2"), STRING(SJ5_BALD_CLEFT), STRING(SJ5_BALD_CLEFT), -150, -15, 40, 60, 30, STRING("BTLNO_HEI_03_01"))
    SET_VALUE(LW(0), PTR(hei_l54_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL(sl5_survey)
    CALL(evt_map_playanim, STRING("mizu"), 1, 0)
    CALL_CHILD(sl5_white_room)
    /* the echo stones: Luff stone stands (the sockets' stone, empty); bars of light across the end */
    PROP("sl_echo0", M_SOCKET_SUN, -300, -15, -75, "S_1")
    CALL(evt_npc_set_scale, STRING("sl_echo0"), FLOAT(2.5f), FLOAT(2.5f), FLOAT(2.5f))
    PROP("sl_echo1", M_SOCKET_SUN, 0, -15, -75, "S_1")
    CALL(evt_npc_set_scale, STRING("sl_echo1"), FLOAT(2.5f), FLOAT(2.5f), FLOAT(2.5f))
    PROP("sl_echo2", M_SOCKET_SUN, 300, -15, -75, "S_1")
    CALL(evt_npc_set_scale, STRING("sl_echo2"), FLOAT(2.5f), FLOAT(2.5f), FLOAT(2.5f))
    PROP("sl_bar0", M_EDGE_V, 380, -15, -50, "S_1")
    PROP("sl_bar1", M_EDGE_V, 380, -15, 0, "S_1")
    PROP("sl_bar2", M_EDGE_V, 380, -15, 50, "S_1")
    CALL(evt_npc_set_scale, STRING("sl_bar0"), FLOAT(3.0f), FLOAT(1.0f), FLOAT(3.0f))
    CALL(evt_npc_set_scale, STRING("sl_bar1"), FLOAT(3.0f), FLOAT(1.0f), FLOAT(3.0f))
    CALL(evt_npc_set_scale, STRING("sl_bar2"), FLOAT(3.0f), FLOAT(1.0f), FLOAT(3.0f))
    CALL_CHILD(hei_l54_lights)
    SPOT("sl_stone0", -300, -15, -45, hei_l54_stone0)
    SPOT("sl_stone1", 0, -15, -45, hei_l54_stone1)
    SPOT("sl_stone2", 300, -15, -45, hei_l54_stone2)
    CALL(evt_mobj_brick, STRING("mobj_c1"), -150, 45, 0, 0, 3, 0, 0)
    CALL(evt_mobj_brick, STRING("mobj_c2"), 150, 45, 0, 0, 3, 0, 0)
    IF_INT_EQUAL(SLF5(F_ECHO3), 1)
        IF_INT_EQUAL(SLF5(F_ECHO2), 1)
            IF_INT_EQUAL(SLF5(F_ECHO1), 1)
                CALL(evt_mobj_itembox, STRING("mobj_box"), 420, -15, -20, 0, 0, PTR(hei_l54_chest_open), SLF5(F_FPPLUS))
                RETURN
            END_IF
        END_IF
    END_IF
    SET_VALUE(LW(0), STRING("e_bero"))
    CALL_CHILD(bero_case_switch_off)
    SPAWN_EVENT(hei_l54_edge)
    IF_INT_EQUAL(SLF5(F_ECHO_SEEN), 0)
        SPAWN_EVENT(hei_l54_enter)
    END_IF
    RETURN
EVT_END()

/* ======================================================================
 * hei_l55, the Sun Room (hei_12): Float to the pedestal; the Glimmerfuzz
 * ====================================================================== */

static BeroEntry hei_l55_bero[] = {BERO("w_bero", "hei_l54", "e_bero"), {0}};

CHEST_EVT(hei_l55_chest_open, "mobj_box", ITEM_DAMAGE_DODGE)

static EVT_BEGIN(hei_l55_lens)
    CALL(evt_mario_key_onoff, 0)
    IF_INT_EQUAL(SLF5(F_SUN), 1)
        SYS("sl_c5_pedestal_empty")
        CALL(evt_mario_key_onoff, 1)
        RETURN
    END_IF
    SET_VALUE(SLF5(F_SUN), 1)
    CALL(evt_eff, STRING(""), STRING("stardust"), 2, 0, 40, 0, 30, 30, 30, 60, 0, 0, 0, 0)
    SET_VALUE(LW(10), STRING("sl_c5_sun_got"))
    CALL_CHILD(sl5_found)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG1_TORIDE1"))
    CALL(evt_party_set_pose, 0, STRING("A_2B"))
    CALL(evt_snd_sfxon, STRING("SFX_AUDIENCE_PANIC_SCREAM1"), 0)
    SCREAMY("sl_c5_lens_found")
    CALL(evt_party_set_pose, 0, STRING("S_1"))
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* the Glimmerfuzz: once it's gone from its niche, the Shine Sprite it sat on */
static EVT_BEGIN(hei_l55_guard)
    LOOP(0)
        CALL(evt_npc_check, STRING("sl_glimmer"), LW(0))
        IF_INT_EQUAL(LW(0), 0)
            BREAK_LOOP
        END_IF
        WAIT_FRAMES(6)
    END_LOOP
    SET_VALUE(SLF5(F_GLIMMER), 1)
    CALL(evt_item_entry, STRING("item_shine"), ITEM_SHINE_SPRITE, 140, 30, -50, ITEM_ENTRY_FIXED, SLF5(F_SUN_SHINE), 0)
    RETURN
EVT_END()

static EVT_BEGIN(hei_l55_enter)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_seq_wait, 2)
    CALL(evt_bero_exec_wait, 65536)
    WAIT_MS(300)
    SAY("sl_c5_sun_see")
    SYS("sl_c5_glimmer_see")
    SET_VALUE(SLF5(F_H55_SEEN), 1)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(hei_l55_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG1_TORIDE1"))
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG1_HEI4"))
    IF_INT_EQUAL(SLF5(F_GLIMMER), 0)
        CALL(evt_sl_enemy, STRING("sl_glimmer"), STRING(SJ5_GOLD_FUZZY), STRING(SJ5_FUZZY), 140, -15, -50, 30, 30, STRING("BTLNO_HEI_10_01"))
    END_IF
    SET_VALUE(LW(0), PTR(hei_l55_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL(sl5_survey)
    CALL(evt_map_playanim, STRING("mizu"), 1, 0)
    CALL_CHILD(sl5_white_room)
    SPOT("sl_pedestal", 0, 0, 0, hei_l55_lens)
    CALL(evt_mobj_itembox, STRING("mobj_box"), 180, -15, 40, 0, 0, PTR(hei_l55_chest_open), SLF5(F_DODGE))
    IF_INT_EQUAL(SLF5(F_SUN), 0)
        CALL(evt_eff, STRING(""), STRING("stardust"), 2, 0, 40, 0, 10, 10, 10, 400, 0, 0, 0, 0)
    END_IF
    IF_INT_EQUAL(SLF5(F_GLIMMER), 0)
        SPAWN_EVENT(hei_l55_guard)
    ELSE
        IF_INT_EQUAL(SLF5(F_SUN_SHINE), 0)
            CALL(evt_item_entry, STRING("item_shine"), ITEM_SHINE_SPRITE, 140, 30, -50, ITEM_ENTRY_FIXED, SLF5(F_SUN_SHINE), 0)
        END_IF
    END_IF
    IF_INT_EQUAL(SLF5(F_H55_SEEN), 0)
        SPAWN_EVENT(hei_l55_enter)
    END_IF
    RETURN
EVT_END()

/* ======================================================================
 * tik_l51, the White Lake (tik_02)
 * ====================================================================== */

static BeroEntry tik_l51_bero[] = {
    BERO("w_bero_1", "tik_l50", "n_bero"),
    {"dokan_1", 2, 0, BERO_DIR_DOWN, BERO_POS_AUTO, 0, 0, -1, NULL, 6, NULL, "tik_l52", "w_bero_2", 1, 1, 0, 0},
    {0},
};

/* the stepping stones from the island to the far shore (Look shows them, and they stay):
 * old stone blocks no hammer of Luigi's breaks */
#define STONE_MUL 0x103030
#define STONE_ADD 0xBAB6AD
static EVT_BEGIN(tik_l51_stones)
    CALL(evt_mobj_lv_blk, STRING("sl_stone0"), 145, -25, 25, 0, SLF5(F_NEVER), STRING("MOBJ_Lv3Block"))
    CALL(evt_mobj_lv_blk, STRING("sl_stone1"), 170, -25, 34, 0, SLF5(F_NEVER), STRING("MOBJ_Lv3Block"))
    CALL(evt_mobj_lv_blk, STRING("sl_stone2"), 195, -25, 43, 0, SLF5(F_NEVER), STRING("MOBJ_Lv3Block"))
    CALL(evt_mobj_lv_blk, STRING("sl_stone3"), 220, -25, 52, 0, SLF5(F_NEVER), STRING("MOBJ_Lv3Block"))
    CALL(evt_mobj_lv_blk, STRING("sl_stone4"), 245, -25, 61, 0, SLF5(F_NEVER), STRING("MOBJ_Lv3Block"))
    CALL(evt_mobj_lv_blk, STRING("sl_stone5"), 270, -25, 70, 0, SLF5(F_NEVER), STRING("MOBJ_Lv3Block"))
    CALL(evt_mobj_lv_blk, STRING("sl_stone6"), 295, -25, 79, 0, SLF5(F_NEVER), STRING("MOBJ_Lv3Block"))
    CALL(evt_mobj_lv_blk, STRING("sl_stone7"), 320, -25, 88, 0, SLF5(F_NEVER), STRING("MOBJ_Lv3Block"))
    /* the Ultra Hammer block is bright red: greyed to the ruin's stone (red cut most, so it isn't pink) */
    CALL(evt_sl_mobj_look, STRING("sl_stone0"), STONE_MUL, STONE_ADD)
    CALL(evt_sl_mobj_look, STRING("sl_stone1"), STONE_MUL, STONE_ADD)
    CALL(evt_sl_mobj_look, STRING("sl_stone2"), STONE_MUL, STONE_ADD)
    CALL(evt_sl_mobj_look, STRING("sl_stone3"), STONE_MUL, STONE_ADD)
    CALL(evt_sl_mobj_look, STRING("sl_stone4"), STONE_MUL, STONE_ADD)
    CALL(evt_sl_mobj_look, STRING("sl_stone5"), STONE_MUL, STONE_ADD)
    CALL(evt_sl_mobj_look, STRING("sl_stone6"), STONE_MUL, STONE_ADD)
    CALL(evt_sl_mobj_look, STRING("sl_stone7"), STONE_MUL, STONE_ADD)
    CALL(evt_item_entry, STRING("item_piece"), ITEM_STAR_PIECE, 245, 25, 61, ITEM_ENTRY_FIXED, SLF5(F_LAKE_PIECE), 0)
    RETURN
EVT_END()

/* Look on the island: the stepping stones show, and stay */
static EVT_BEGIN(tik_l51_look)
    IF_INT_EQUAL(SLF5(F_LAKE_SEEN), 0)
        SET_VALUE(SLF5(F_LAKE_SEEN), 1)
        SET_VALUE(LW(10), 230)
        SET_VALUE(LW(11), 10)
        SET_VALUE(LW(12), 55)
        CALL_CHILD(sl5_glint)
        CALL_CHILD(tik_l51_stones)
        PARTY("sl_c5_lake_look")
    ELSE
        PARTY("sl_c5_look_03")
    END_IF
    RETURN
EVT_END()

static EVT_BEGIN(tik_l51_enter)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_seq_wait, 2)
    CALL(evt_bero_exec_wait, 65536)
    WAIT_MS(300)
    SAY("sl_c5_lake_00")
    SCREAMY("sl_c5_lake_01")
    SET_VALUE(SLF5(F_LAKE_INTRO), 1)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(tik_l51_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG0_TIK2"))
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG0_TIK2"))
    SET_VALUE(LW(0), PTR(tik_l51_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL(sl5_survey)
    CALL(evt_map_playanim, STRING("anm_nami_1"), 1, 0)
    CALL(evt_map_playanim, STRING("anm_nami_2"), 1, 0)
    CALL_CHILD(sl5_white_room)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("bom_kabe_3"), MAPOBJ_FLAG_HIDE)
    /* the lake: deep and cold. The shore to the island: Float (or Blooey's Swim); the island
     * to the far shore: the stones Look shows; the far shore to the pipe's ledge: Float */
    CALL(sl5_drop_zone, 0, -290, -200, -110, 200, -20, -340, 0, 0)
    CALL(sl5_drop_zone, 1, 130, -200, 320, 200, -20, 0, 0, 0)
    CALL(sl5_drop_zone, 2, 320, -70, 600, 60, -20, 400, 0, 100)
    CALL(evt_sl_float_point, 0, -300, 0, 0, -60, 0, 0)
    CALL(evt_sl_swim_point, 0, -300, 0, 0, -60, 0, 0)
    CALL(evt_sl_float_point, 1, 420, 0, 95, 520, 0, -100)
    CALL(evt_sl_look_point, 0, 40, 0, 0, 200, PTR(tik_l51_look))
    IF_INT_EQUAL(SLF5(F_LAKE_SEEN), 1)
        CALL_CHILD(tik_l51_stones)
    END_IF
    IF_INT_EQUAL(SLF5(F_LAKE_INTRO), 0)
        SPAWN_EVENT(tik_l51_enter)
    END_IF
    RETURN
EVT_END()

/* ======================================================================
 * tik_l52, the Room of Nothing (tik_15): pure white; Look shows the edges
 * ====================================================================== */

/* in by the west door, on the floor: tik_15's east way is over the water, and he came in thrown about and hurt */
static BeroEntry tik_l52_bero[] = {BERO("w_bero_2", "tik_l51", "dokan_1"), {0}};

CHEST_EVT(tik_l52_chest_open, "mobj_box", ITEM_SHINE_SPRITE)

/* Look's edge glow: the strips along every edge, at alpha LW(0) */
static EVT_BEGIN(tik_l52_edges)
    CALL(evt_npc_set_color, STRING("sl_edge0"), 255, 255, 255, LW(0))
    CALL(evt_npc_set_color, STRING("sl_edge1"), 255, 255, 255, LW(0))
    CALL(evt_npc_set_color, STRING("sl_edge2"), 255, 255, 255, LW(0))
    CALL(evt_npc_set_color, STRING("sl_edge3"), 255, 255, 255, LW(0))
    CALL(evt_npc_set_color, STRING("sl_edge4"), 255, 255, 255, LW(0))
    CALL(evt_npc_set_color, STRING("sl_edge5"), 255, 255, 255, LW(0))
    CALL(evt_npc_set_color, STRING("sl_edge6"), 255, 255, 255, LW(0))
    RETURN
EVT_END()

/* Look: the edges glow for 8 seconds (the white lifts), and the hidden shows for good */
static EVT_BEGIN(tik_l52_look)
    BEGIN_INLINE_EVENT
    CALL(evt_npc_set_anim, STRING("sl_edge0"), STRING("L_1"))
    CALL(evt_npc_set_anim, STRING("sl_edge1"), STRING("L_1"))
    CALL(evt_npc_set_anim, STRING("sl_edge2"), STRING("L_1"))
    CALL(evt_npc_set_anim, STRING("sl_edge3"), STRING("L_1"))
    CALL(evt_npc_set_anim, STRING("sl_edge4"), STRING("L_1"))
    CALL(evt_npc_set_anim, STRING("sl_edge5"), STRING("L_1"))
    CALL(evt_npc_set_anim, STRING("sl_edge6"), STRING("L_1"))
        SET_VALUE(LW(0), 255)
        CALL_CHILD(tik_l52_edges)
        WAIT_MS(7000)
        SET_VALUE(LW(2), 255)
        LOOP(15)
            SUBTRACT(LW(2), 17)
            SET_VALUE(LW(0), LW(2))
            CALL_CHILD(tik_l52_edges)
            WAIT_FRAMES(4)
        END_LOOP
    END_INLINE_EVENT
    BEGIN_INLINE_EVENT
        CALL(evt_sub_intpl_msec_init, 0, 88, 40, 400)
        LOOP(0)
            CALL(evt_sub_intpl_msec_get_value)
            CALL(evt_sl_map_wash, 0, 0xFFFFFF, LW(0))
            WAIT_FRAMES(1)
            IF_INT_EQUAL(LW(1), 0)
                BREAK_LOOP
            END_IF
        END_LOOP
        WAIT_MS(8000)
        CALL(evt_sub_intpl_msec_init, 0, 40, 88, 1200)
        LOOP(0)
            CALL(evt_sub_intpl_msec_get_value)
            CALL(evt_sl_map_wash, 0, 0xFFFFFF, LW(0))
            WAIT_FRAMES(1)
            IF_INT_EQUAL(LW(1), 0)
                BREAK_LOOP
            END_IF
        END_LOOP
    END_INLINE_EVENT
    IF_INT_EQUAL(SLF5(F_NOTHING_SEEN), 0)
        SET_VALUE(SLF5(F_NOTHING_SEEN), 1)
        CALL(evt_mobj_itembox, STRING("mobj_box"), -340, 125, -105, 0, 0, PTR(tik_l52_chest_open), SLF5(F_NOTHING_SHINE))
        CALL(evt_item_entry, STRING("item_piece"), ITEM_STAR_PIECE, 0, 150, -100, ITEM_ENTRY_FIXED, SLF5(F_NOTHING_PIECE), 0)
        CALL(evt_mobj_brick, STRING("mobj_white"), 200, 125, -100, 0, 0, 0, 0)
        PARTY("sl_c5_nothing_look")
    END_IF
    RETURN
EVT_END()

static EVT_BEGIN(tik_l52_lens)
    CALL(evt_mario_key_onoff, 0)
    IF_INT_EQUAL(SLF5(F_TEAR), 1)
        SYS("sl_c5_pedestal_empty")
        CALL(evt_mario_key_onoff, 1)
        RETURN
    END_IF
    /* (the stairs at the east end reach here without Float: the lens still wants Look) */
    IF_INT_EQUAL(SLF5(F_NOTHING_SEEN), 0)
        SYS("sl_c5_nothing_unseen")
        CALL(evt_mario_key_onoff, 1)
        RETURN
    END_IF
    SET_VALUE(SLF5(F_TEAR), 1)
    CALL(evt_eff, STRING(""), STRING("stardust"), 2, 420, 165, -100, 30, 30, 30, 60, 0, 0, 0, 0)
    SET_VALUE(LW(10), STRING("sl_c5_tear_got"))
    CALL_CHILD(sl5_found)
    CALL(evt_snd_bgmoff, PSND_FADE_OUT)
    CALL(evt_party_set_pose, 0, STRING("A_2B"))
    CALL(evt_snd_sfxon, STRING("SFX_AUDIENCE_PANIC_SCREAM1"), 0)
    SCREAMY("sl_c5_lens_found")
    CALL(evt_party_set_pose, 0, STRING("S_1"))
    SCREAMY("sl_c5_lens_home")
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(tik_l52_enter)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_seq_wait, 2)
    CALL(evt_bero_exec_wait, 65536)
    WAIT_MS(600)
    SAY("sl_c5_nothing_00")
    SCREAMY("sl_c5_nothing_01")
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(tik_l52_init)
    CALL(evt_snd_bgmoff, PSND_FADE_OUT)
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG0_TIK4"))
    SET_VALUE(LW(0), PTR(tik_l52_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL(sl5_survey)
    CALL(evt_map_playanim, STRING("anm_mizu"), 1, 0)
    /* nothing: every surface pure white, no shade */
    CALL(evt_sl_map_wash, 0, 0xFFFFFF, 88)
    CALL(evt_map_fog_onoff, 0)
    /* the water's a drop, back to the start; up to the walkway, Float */
    CALL(sl5_drop_zone, 0, -560, 70, 560, 300, -20, -420, 0, 0)
    CALL(evt_sl_float_point, 0, -300, 0, 0, -300, 125, -100)
    CALL(evt_sl_look_point, 0, 0, 0, 0, 2000, PTR(tik_l52_look))
    SPOT("sl_pedestal", 420, 125, -100, tik_l52_lens)
    /* Look's edge glow, unseen until he Looks */
    PROP("sl_edge0", M_EDGE, -400, 125, -62, "S_1")
    PROP("sl_edge1", M_EDGE, -200, 125, -62, "S_1")
    PROP("sl_edge2", M_EDGE, 0, 125, -62, "S_1")
    PROP("sl_edge3", M_EDGE, -400, 0, 62, "S_1")
    PROP("sl_edge4", M_EDGE, -200, 0, 62, "S_1")
    PROP("sl_edge5", M_EDGE, 0, 0, 62, "S_1")
    PROP("sl_edge6", M_EDGE, 200, 0, 62, "S_1")
    SET_VALUE(LW(0), 0)
    CALL_CHILD(tik_l52_edges)
    IF_INT_EQUAL(SLF5(F_NOTHING_SEEN), 1)
        CALL(evt_mobj_itembox, STRING("mobj_box"), -340, 125, -105, 0, 0, PTR(tik_l52_chest_open), SLF5(F_NOTHING_SHINE))
        CALL(evt_item_entry, STRING("item_piece"), ITEM_STAR_PIECE, 0, 150, -100, ITEM_ENTRY_FIXED, SLF5(F_NOTHING_PIECE), 0)
        CALL(evt_mobj_brick, STRING("mobj_white"), 200, 125, -100, 0, 0, 0, 0)
    END_IF
    IF_INT_EQUAL(SLF5(F_TEAR), 0)
        CALL(evt_eff, STRING(""), STRING("stardust"), 2, 420, 155, -100, 10, 10, 10, 400, 0, 0, 0, 0)
        SPAWN_EVENT(tik_l52_enter)
    END_IF
    RETURN
EVT_END()

/* ======================================================================
 * The chapter
 * ====================================================================== */

static const PortSlMap kMaps5[] = {
    {"win_l50", win_l50_init, "win_06", 1}, {"win_l51", win_l51_init, "win_00", 1},
    {"win_l52", win_l52_init, "win_01", 1}, {"win_l53", win_l53_init, "win_02", 1},
    {"win_l54", win_l54_init, "win_03", 1}, {"hei_l50", hei_l50_init, "hei_07", 1},
    {"tik_l50", tik_l50_init, "tik_05", 1}, {"hei_l51", hei_l51_init, "hei_10", 1},
    {"hei_l52", hei_l52_init, "hei_09", 1}, {"hei_l53", hei_l53_init, "hei_08", 1},
    {"hei_l54", hei_l54_init, "hei_11", 1}, {"hei_l55", hei_l55_init, "hei_12", 1},
    {"tik_l51", tik_l51_init, "tik_02", 1}, {"tik_l52", tik_l52_init, "tik_15", 1},
    {NULL, NULL, NULL, 0},
};

static const short kItems5[] = {ITEM_SUPER_SHROOM, ITEM_SUPER_SHROOM, ITEM_MAPLE_SYRUP, ITEM_HONEY_SYRUP,
                                ITEM_HONEY_SYRUP, ITEM_THUNDER_RAGE, ITEM_ICE_STORM, ITEM_LIFE_SHROOM, 0};
static const short kBadges5[] = {ITEM_POWER_BOUNCE, ITEM_POWER_SMASH, ITEM_ICE_SMASH, ITEM_CLOSE_CALL, ITEM_HP_PLUS,
                                 ITEM_MULTIBOUNCE, ITEM_CHARGE, ITEM_SPIKE_SHIELD, ITEM_TIMING_TUTOR,
                                 ITEM_SUPER_APPEAL, ITEM_QUAKE_HAMMER, ITEM_SLEEPY_STOMP, ITEM_HAPPY_FLOWER, 0};

/* the units at Luigi's level 14-16 (docs/super_luigi/ch5.md, "Unit overrides") */
static const PortSlUnitStats kStats5[] = {
    {UNIT_DARK_PUFF, 6, 3, 0, -1, 26},  {UNIT_PALE_PIRANHA, 6, 3, 0, -1, 26}, {UNIT_CLEFT, 4, 3, 0, 1, 27},
    {UNIT_FUZZY, 4, 2, 0, -1, 25},      {UNIT_BALD_CLEFT, 4, 3, 0, 1, 26},    {UNIT_BRISTLE, 4, 3, 0, 2, 27},
    {UNIT_GOLD_FUZZY, 20, 3, 0, -1, 45}, {0, 0, 0, 0, 0, 0},
};


#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

static void setup5(void) {
    portModHostAddHook(1, kSlot_marioStMain, (void*)dropFrame);
}

extern const char* const portSlText5[];

static const PortSlChapter kChapter5 = {
    "Rapturous Ruins", "win_l50", "dokan1", "gor_luigi_talk_0005", PORT_SL_FRIEND_SCREAMY, 14, 35, 20, 18, 2, 2, 220,
    kItems5, kBadges5,
    PORT_SL_BIT(PORT_SL_FRIEND_BLOOEY) | PORT_SL_BIT(PORT_SL_FRIEND_JERRY) | PORT_SL_BIT(PORT_SL_FRIEND_TORQUE) |
        PORT_SL_BIT(PORT_SL_FRIEND_HAYZEE),
    kMaps5, portSlText5, kStats5, setup5, "sl_c5_diary",
};

const PortSlChapter* portSlChapter5(void) {
    return &kChapter5;
}

/* The chapter's text (docs/super_luigi/ch5.md: every line as the plan writes it) */
const char* const portSlText5[] = {
    /* 1. arrival */
    "sl_c5_arrive_00", "<wait 600>Grimble Forest.\n<k>\n<p>\nWhere the leaves are white,\nand nothing makes a sound\nunless it has to.\n<k>",
    "sl_c5_arrive_01", "<scale 0.8>(Why is it so QUIET?)\n<k>",
    "sl_c5_arrive_02", "<scale 0.8>(No audience, baby.\nNot a single one.)\n<k>",
    "sl_c5_arrive_03", "Probably 'cause nobody comes\nhere.\n<k>",
    "sl_c5_arrive_04", "<shake>SHHHH!</shake>\n<k>",
    "sl_c5_arrive_05", "...\n<k>",
    "sl_c5_arrive_06", "<scale 0.8>(The compass says north.\nStraight through the trees.)\n<k>\n<p>\n<scale 0.8>(Somewhere in here are the\nRapturous Ruins, and somewhere\nin the ruins is... something.)\n<k>\n<p>\n<scale 0.8>(Let's be quiet and careful\nand not wake anything up.)\n<k>",
    "sl_c5_arrive_07", "<scale 0.8>(Plumpbelly's champion is\nalways quiet and careful.)\n<k>",
    "sl_c5_arrive_08", "<shake>Fzzzzzzzzt...</shake>\n\n<wait 400>...\n<k>",

    /* 2. Old Mossbeard */
    "sl_c5_moss_00", "Visitors. Hm. Haven't had\nvisitors since...\n<wait 300>what year is it?\n<k>",
    "sl_c5_moss_01", "Er. This one?\n<k>",
    "sl_c5_moss_02", "Then not since a while before\nthis one.\n<k>\n<p>\nYou're after the white dome,\naren't you. Everyone who gets\nthis far is after the dome.\n<k>\n<p>\nNobody comes back from the\nwhite dome.\n<k>",
    "sl_c5_moss_03", "N-nobody?\n<k>",
    "sl_c5_moss_04", "Well. Nobody comes back THIS\nway. There's another path out.\nProbably. I've never been.\n<k>\n<p>\nI like buttons.\n<k>",
    "sl_c5_moss_05", "Word of advice. The forest likes\nquiet. The things in it don't\nlike noise...\n<k>\n<p>\n...and the dome likes it least\nof all. Whatever you do in\nthere... don't shout.\n<k>",
    "sl_c5_moss_06", "Oh, and if you see any buttons\nlying about, I collect 'em.\nThree went missing in this\n<k>\n<p>\nforest. Bring 'em here, and\nI'll make it worth your while.\nA little.\n<k>",
    "sl_c5_moss_again", "Buttons. In the pond, maybe.\nUnder a bird. Behind something\nprickly. They get about.\n<k>",
    "sl_c5_moss_trade", "Is that... a button? For me?\nOh, that's a good one. Four\nholes. Very traditional.\n<k>",
    "sl_c5_moss_give1", "Here. Have this for your\ntrouble. Sticky, but sweet.\n<k>",
    "sl_c5_moss_give2", "And this. Found it in a button\ntin. It's not a button.\nDoesn't belong.\n<k>",
    "sl_c5_moss_give2b", "And this. Found it in a button\ntin. It's not a button.\nDoesn't belong. It's jam.\n<k>",
    "sl_c5_moss_give3", "That's all three! The set's\ncomplete! Take this. It made me\nfeel fine. You look like you\n<k>\n<p>\ncould use feeling fine.\n<k>",
    "sl_c5_moss_done", "All my buttons, home again.\nGo on, then. Mind the dome.\nDon't shout.\n<k>",
    "sl_c5_button_got", "<system>\nLuigi found a Button!\n<k>",
    "sl_c5_button_all", "<system>\nThat's all three Buttons.\nOld Mossbeard will want them!\n<k>",
    "sl_c5_pond_00", "Hey, man! Something down there\nwas shiny! Got it!\n<k>",
    "sl_c5_pond_check", "A dark little pond. Something\nshiny's on the bottom,\nway down.\n<k>",
    "sl_c5_pond_blooey", "If Blooey were out, he could\ndive for it. That pond is way\ntoo dark for me.\n<k>",
    "sl_c5_pond_empty", "Just a dark little pond.\n<k>",
    "sl_c5_pond_hint", "Dark water? Pfft. I was born\nin dark water, man. Let me\nat it!\n<k>",
    "sl_c5_pond_dived", "Nothing else down there, man.\nJust mud. Cold mud.\n<k>",

    /* the forest's puzzles */
    "sl_c5_owl_see", "A big black crow, fast asleep.\nSomething shiny is tucked\nunder it.\n<k>",
    "sl_c5_owl_hint", "If somebody sang it a wake-up\nsong... Hayzee?\n<k>",
    "sl_c5_owl_hum", "Maybe if I hum something...\n<wait 300>Doo-doo, dee-doo...\n<k>",
    "sl_c5_owl_hoot", "<shake>CAAAW.</shake>\n<k>",
    "sl_c5_owl_hayzee", "Not a fan of the classics,\nthat one.\n<k>",
    "sl_c5_owl_gone", "It didn't like that.\n<wait 300>But it left this behind!\n<k>",
    "sl_c5_stump_open", "The stone's gone! The old stump\nis hollow. It goes somewhere!\n<k>",
    "sl_c5_thorns_see", "A Dark Bristle, curled up asleep\nright across the path. It's all\nspikes. Not touching that.\n<k>\n<p>\nSomething's back there,\nbehind it.\n<k>",
    "sl_c5_thorns_hint", "Jerry could blast it out of the\nway. Stand him close and let\nhim go off!\n<k>",
    "sl_c5_thorns_pull", "Ow! Ow! Ow! I'll just...\n<wait 300>push... it... OVER!\n<k>",
    "sl_c5_thorns_gone", "It's gone! And there's a whole\nother path back here!\n<k>",
    "sl_c5_thorns_button", "And there, under the white\nmound... a button!\n<k>",
    "sl_c5_post_see", "An old stone post with a crank.\nIt's rusted solid.\n<k>",
    "sl_c5_post_hint", "Torque could fix this. He\nfixes everything. Stand him\nnext to it!\n<k>",
    "sl_c5_post_bang", "Maybe if I just give it a\nlittle... <wait 200>BANG!\n<k>",
    "sl_c5_post_turns", "The post creaks round...\nand points at the old stump.\nThe stump splits open!\n<k>",
    "sl_c5_post_coins", "<system>\nLuigi found 20 coins in the\nstump!\n<k>",
    "sl_c5_post_points", "The old post points at the\nstump. It looks proud of\nitself.\n<k>",
    "sl_c5_dome_00", "The trees are opening up...\n<k>\n<p>\nAnd up on the hill, an old\nhouse. Nobody's lived there\nin years.\n<k>\n<p>\n<scale 0.8>(The compass needle swings\nstraight at it. The ruins must\nbe in there somewhere!)\n<k>",

    /* 4. the White Gate */
    "sl_c5_gate_see", "Everything's white in here.\nThe floor, the walls...\nThat big stone face, too.\n<k>",
    "sl_c5_gate_00", "WHO...\n<wait 300>COMES...\n<wait 300>TO THE RAPTUROUS... <wait 200>RUINS?\n<k>",
    "sl_c5_gate_01", "Hi! Luigi! Super Luigi, sort\nof. These are my friends.\nWe're looking for...\n<k>",
    "sl_c5_gate_02", "WHAT?\n<k>",
    "sl_c5_gate_03", "<shake>We're looking for a piece of\nthe Marvelous Compass!</shake>\n<k>",
    "sl_c5_gate_04", "NO SHOUTING...\n<wait 300>IN THE RUINS.\n<k>",
    "sl_c5_gate_05", "But you said \"what\"!\n<k>",
    "sl_c5_gate_06", "THE SLEEPER...\n<wait 300>SLEEPS.\n<k>\n<p>\nONLY THE WORTHY...\n<wait 300>MAY PASS.\n<k>\n<p>\nANSWER...\n<wait 300>MY QUESTIONS...\n<wait 300>THREE.\n<k>",
    "sl_c5_quiz_q1", "WHAT COLOUR...\n<wait 200>ARE THE LEAVES...\n<wait 200>OF GRIMBLE FOREST?\n<o>",
    "sl_c5_quiz_a1", "<select 0 2 0 40>\nGreen\nWhite\nPlaid",
    "sl_c5_quiz_q2", "WHAT DOES...\n<wait 200>THE FOREST...\n<wait 200>LIKE?\n<o>",
    "sl_c5_quiz_a2", "<select 0 2 0 40>\nQuiet\nButtons\nPie",
    "sl_c5_quiz_q3", "WHAT...\n<wait 200>IS IN YOUR POCKET...\n<wait 200>THAT POINTS?\n<o>",
    "sl_c5_quiz_a3", "<select 0 2 0 40>\nA pie\nA compass\nBlooey",
    "sl_c5_quiz_right", "<p>\nCORRECT.\n<k>",
    "sl_c5_quiz_wrong", "<p>\nWRONG.\n<k>",
    "sl_c5_quiz_buttons", "<p>\nTHAT IS...\n<wait 200>MOSSBEARD.\n<wait 200>NOT THE FOREST.\n<k>",
    "sl_c5_gate_07", "YOU...\n<wait 300>MAY PASS.\n<k>\n<p>\nGO...\n<wait 300>STRAIGHT.\n<k>\n<p>\nDO NOT...\n<wait 300>WAKE HIM.\n<k>",
    "sl_c5_gate_08", "Wake who?\n<k>",
    "sl_c5_gate_after", "...\n<wait 300>DO NOT...\n<wait 300>WAKE HIM.\n<k>",

    /* 5. the Sleeper's Chamber, and Screamy */
    "sl_c5_chamber_00", "<scale 0.8>(It's... a kid. A little kid.\nAsleep. In here?)\n<k>",
    "sl_c5_chamber_01", "<scale 0.8>(Maybe he's waiting for his\ncall.)\n<k>",
    "sl_c5_screamy_00", "<shake>EEEEEEEEEEEEE!</shake>\n<k>",
    "sl_c5_screamy_01", "EEE... I mean... sorry. Sorry,\nsorry. You surprised me.\n<k>",
    "sl_c5_screamy_02", "WE surprised YOU?!\n<k>",
    "sl_c5_screamy_03", "Nobody comes in here. Not for\na long time. Not for ever,\nmaybe.\n<k>\n<p>\nI'm Screamy. I live here.\nI look after him.\n<k>",
    "sl_c5_screamy_04", "Him? The boy?\n<k>",
    "sl_c5_screamy_05", "That's Cranberry. He's asleep.\nHe's been asleep since before\nI can remember...\n<k>\n<p>\n...and I can remember a really\nlong way.\n<k>",
    "sl_c5_screamy_06", "The walls say he's waiting.\nThey say he'll wake when the\nright one comes.\n<k>\n<p>\nThe one who's meant to.\n<k>",
    "sl_c5_screamy_07", "Meant to? Meant to what?\n<k>",
    "sl_c5_screamy_08", "They don't say. I don't know\nwhat it means either. I just\nread the walls.\n<k>\n<p>\nI can read in the dark.\nI can see things other\npeople can't.\n<k>\n<p>\nIt's not as fun as\nit sounds.\n<k>",
    "sl_c5_screamy_09", "Well... we're not here to\nbother him. I'm Luigi. I'm\nlooking for a piece of the\n<k>\n<p>\nMarvelous Compass.\n<k>\n<p>\nIt's shiny and gold and sort of\na curve...\n<k>",
    "sl_c5_screamy_10", "<shake>EEE!</shake> Oh! Sorry. I know\nwhere the shiny things are!\nThere's lots in the ruin!\n<k>\n<p>\nRooms and rooms! I'll show you.\nI know all the hidden bits.\nI'll help!\n<k>",
    "sl_c5_screamy_11", "...If that's OK. Is that OK?\nYou can say no.\n<k>\n<p>\nPeople don't usually say\nanything, because there aren't\nany people.\n<k>",
    "sl_c5_screamy_12", "Of course it's OK! Welcome to\nthe team, Screamy!\n<k>",
    "sl_c5_screamy_13", "A TEAM! <shake>EEEEEEEEE!</shake>\n<k>",
    "sl_c5_screamy_joined", "<system>\nScreamy joined your party!\n<k>",
    "sl_c5_sockets_00", "See the three holes round him?\nThose had lenses in.\nLong ago.\n<k>\n<p>\nThe walls say when the three\nlights shine, the ruin\nremembers.\n<k>\n<p>\nI don't know what that means\neither. I think the walls like\nto sound mysterious.\n<k>",
    "sl_c5_socket_0", "An empty socket shaped like\na crescent moon. Something\nused to sit here.\n<k>",
    "sl_c5_socket_1", "An empty socket shaped like\na circle, like the sun.\nSomething used to sit here.\n<k>",
    "sl_c5_socket_2", "An empty socket shaped like\na teardrop. Something used\nto sit here.\n<k>",
    "sl_c5_socket_lit", "The lens sits in its socket,\nshining up at the ceiling.\n<k>",
    "sl_c5_set_moon", "<system>\nLuigi set the Moon Lens.\nA beam of silver light rises!\n<k>",
    "sl_c5_set_sun", "<system>\nLuigi set the Sun Lens.\nA beam of gold light rises!\n<k>",
    "sl_c5_set_tear", "<system>\nLuigi set the Tear Lens.\nA beam of blue light rises!\n<k>",
    "sl_c5_lens_joy", "<shake>EEEEEEE!</shake> It's shining! Sorry.\nSorry. But it's SHINING!\n<k>",
    "sl_c5_door_open", "The big door! It's never opened\nbefore! Not even once!\n<k>",
    "sl_c5_cranberry_check", "He's fast asleep. He's\nbreathing so slowly...\n<k>",
    "sl_c5_cranberry_check_sc", "Shh. He's waiting. I don't know\nfor what. Nobody does.\n<k>",

    /* 6. Float and Look */
    "sl_c5_float_00", "That's where the floor fell in.\nI can carry you over. I'm\nstronger than I look.\n<k>\n<p>\nEveryone's stronger than I look.\nI look very weak.\n<k>",
    "sl_sc_float_help", "<system>\nFloat: At the edge of a gap,\nuse Screamy's ability, and\nScreamy lifts Luigi and drifts\n<k>\n<p>\nacross.\n<k>\n<p>\nLook: Anywhere else, Screamy\nlooks hard at everything nearby\nfor a few seconds, and hidden\n<k>\n<p>\nthings show: invisible blocks,\nsecret doors, and the edges of\nfloors you can't see.\n<k>",
    "sl_c5_look_00", "Oh, and that door up there's\nnot a door. The real door's\nnext to it. Look!\n<k>",
    "sl_c5_look_01", "See? Hidden. Everything in here\nis hidden. It's very rude.\n<k>",
    "sl_c5_look_02", "Nothing else here. Just walls.\nWalls, walls, walls.\n<k>",
    "sl_c5_look_03", "Everything's where it should\nbe. Spooky.\n<k>",
    "sl_c5_look_block", "There! Up there! Something's\nhiding in the air!\n<k>",
    "sl_c5_look_floor", "Something's glinting in the\nfloor! Right there!\n<k>",

    /* 7. the search */
    "sl_c5_nothing_unseen", "Nothing here. Just white.\nAt least, nothing you can see.\n<k>",
    "sl_c5_pedestal_empty", "An empty pedestal.\n<k>",
    "sl_c5_moon_barred", "A pedestal with a crescent of\nsilver glass on it... behind a\ngate of stone teeth.\n<k>",
    "sl_c5_moon_got", "<system>\nLuigi found the Moon Lens!\nA crescent of silver glass.\n<k>",
    "sl_c5_sun_got", "<system>\nLuigi found the Sun Lens!\nA disc of golden glass.\n<k>",
    "sl_c5_tear_got", "<system>\nLuigi found the Tear Lens!\nA teardrop of blue glass.\n<k>",
    "sl_c5_lens_found", "<shake>EEEEEEEEE!</shake>\n\n<wait 300>...Sorry. Sorry. I just really\nlike lenses.\n<k>",
    "sl_c5_lens_home", "That's the last one! Back to\nthe chamber! Back to\nCranberry!\n<k>",
    "sl_c5_gear_stuck", "A big stone gear on the wall,\njammed with a thousand years\nof grit. It won't budge.\n<k>",
    "sl_c5_gear_hint", "Torque could clean this out.\nGet him up close to it!\n<k>",
    "sl_c5_gear_scrub", "If I scrub it out with my\nsleeve... <wait 300>there!\n<k>",
    "sl_c5_gear_torque", "Gears like these, somebody made\n'em to last. Nobody made 'em\nto wait.\n<k>",
    "sl_c5_gear_clean", "<system>\nThe gear is clean! It could\nturn now.\n<k>",
    "sl_c5_gear_push", "<system>\nPush the gear! Press <icon PAD_A 0.6 0 22 0>\n\nfour times in a row, and don't\nstop!\n<k>",
    "sl_c5_gear_slip", "<system>\nThe gate rose a little... and\nslid back down.\n<k>",
    "sl_c5_gear_open", "<system>\nThe gate of stone teeth rises,\nand stays up!\n<k>",
    "sl_c5_gear_up", "The gear's turned all the way.\nThe gate's up.\n<k>",
    "sl_c5_echo_see", "Three white stones along the\nwall... and bars of light right\nacross the passage.\n<k>\n<p>\nThey're humming. Like they're\nwaiting for a tune.\n<k>",
    "sl_c5_echo_1", "<system>\nThe stone sings four notes:\n<icon PAD_A 0.6 0 22 0> <icon PAD_A 0.6 0 22 0> <icon PAD_B 0.6 0 22 0> <icon PAD_A 0.6 0 22 0>\n<k>",
    "sl_c5_echo_2", "<system>\nThe stone sings four notes:\n<icon PAD_X 0.6 0 22 0> <icon PAD_B 0.6 0 22 0> <icon PAD_A 0.6 0 22 0> <icon PAD_Y 0.6 0 22 0>\n<k>",
    "sl_c5_echo_3", "<system>\nThe stone sings five notes:\n<icon PAD_Y 0.6 0 22 0> <icon PAD_X 0.6 0 22 0> <icon PAD_Y 0.6 0 22 0> <icon PAD_B 0.6 0 22 0> <icon PAD_A 0.6 0 22 0>\n<k>",
    "sl_c5_echo_hayzee", "Ooh, I know this one, baby!\nGive me the notes, one at a\ntime!\n<k>",
    "sl_c5_echo_sing", "<system>\nSing it back! Press the\nbuttons in the same order.\n<k>",
    "sl_c5_echo_wrong", "<system>\nThe stone falls silent. That\nwasn't quite it.\n<k>",
    "sl_c5_echo_keychange", "Ooh, a key change.\n<k>",
    "sl_c5_echo_bridge", "<system>\nThe stone lights up, and a bar\nof light across the passage\nfades away!\n<k>",
    "sl_c5_echo_whole", "<system>\nThe last bar fades. The way\nis open!\n<k>",
    "sl_c5_echo_done", "The stone glows, humming its\ntune to itself.\n<k>",
    "sl_c5_echo_drop", "Ow! It tingles! The light\nwon't let me through.\n<k>",
    "sl_c5_sun_see", "There's the pedestal! And\nthere's something gold over\nin the corner...\n<k>",
    "sl_c5_glimmer_see", "The golden fuzzball glares at\nyou. It's guarding something\nshiny.\n<k>\n<p>\nIt looks like it would rather\nfight than share.\n<k>",
    "sl_c5_lake_00", "A lake. Inside a ruin. Under a\nforest. Sure. Why not.\n<k>",
    "sl_c5_lake_01", "It's very deep, and very cold.\nBut I think there's more out\nthere than you can see.\n<k>",
    "sl_c5_lake_look", "There! Stepping stones! They\nwere there all along. Sneaky\nstones.\n<k>",
    "sl_c5_lake_cold", "Too cold. Even for me.\n<k>",

    /* 8. the murals */
    "sl_c5_mural_faded", "A faded picture on the wall.\nThere's writing, too, but it's\ntoo worn to make out.\n<k>",
    "sl_c5_mural1", "\"Here the Luffs lived, in the\nwhite city, and the white city\nwas happy...\n<k>\n<p>\n...and nobody knew what\ntomorrow would bring.\"\n<k>",
    "sl_c5_mural2", "\"The wisest Luff made a\ncompass that did not point\nnorth or south.\n<k>\n<p>\nIt pointed to tomorrow.\"\n<k>",
    "sl_c5_mural3", "\"With the compass, the Luffs\nknew every storm before it\ncame, every war before it\n<k>\n<p>\nstarted.\n<k>\n<p>\nThey ruled from sea to sea.\"\n<k>",
    "sl_c5_mural4", "\"They looked further. Next year.\nThe next hundred. They forgot\nto look at today.\n<k>\n<p>\nThey stopped planting. They\nstopped singing. Why bother?\nThey already knew.\"\n<k>",
    "sl_c5_mural5", "\"The compass grew heavy with\nall the tomorrows it held.\n<k>\n<p>\nIt cracked. The city fell\nquiet.\n<k>\n<p>\nEveryone went to sleep, and did\nnot wake.\"\n<k>",
    "sl_c5_mural6", "The last panel is blank, but\nfor a small drawing: a boy\nbreaking a circle into seven\n<k>\n<p>\npieces.\n<k>",
    "sl_c5_mural_end_00", "That's the end. The last one's\nalways been blank. I used to\nthink someone forgot it.\n<k>",
    "sl_c5_mural_end_01", "A compass that points to\ntomorrow... and it broke into\nseven...\n<k>\n<p>\nScreamy, that's MY compass.\nThe Marvelous Compass.\nThe pieces...\n<k>",
    "sl_c5_mural_end_02", "<shake>EEE!</shake> You have the\ntomorrow-compass?! The one\nfrom the walls?!\n<k>\n<p>\nBut the walls say it's bad!\n<k>",
    "sl_c5_mural_end_03", "It's... I need it to find\nsomeone. A princess. She's in\ntrouble.\n<k>",
    "sl_c5_mural_end_04", "<scale 0.8>...Then I hope it's kind to you.\nIt wasn't kind to them.\n<k>",
    "sl_c5_mural_crack", "That last one's cracked, though.\nRight down the middle. There's\nair coming through it.\n<k>",
    "sl_c5_mural_hint", "Jerry could open it up. Stand\nhim by the cracked panel and\nlet him go off!\n<k>",
    "sl_c5_mural_push", "If I lean on it... just a\nlittle...\n<wait 300>Whoa!\n<k>",
    "sl_c5_mural_open", "The panel's open! There's a\nlittle room behind it, and a\nchest!\n<k>",

    /* 9. the Room of Nothing */
    "sl_c5_nothing_00", "Where... where's the floor?\nWhere's the WALL?\nWhere's ME?\n<k>",
    "sl_c5_nothing_01", "This is the Nothing room. I hate\nthe Nothing room. Everything's\nhere, you just can't see it.\n<k>\n<p>\nHold on to me.\n<k>",
    "sl_c5_nothing_look", "There's the edges! And a chest,\nand... a white stone, in the\nway. Hit it! Hit it!\n<k>",

    /* 10. the three lights, the sneeze */
    "sl_c5_sneeze_00", "The ruin's remembering! It's\nREMEMBERING! Oh, it's so pretty.\nOh, it's so... dusty.\n<k>",
    "sl_c5_sneeze_01", "<scale 0.8>Ah...\n<wait 400>ahh...\n<k>",
    "sl_c5_sneeze_02", "No! No, no, no! Not in here!\nThe walls said no noise!\nThe SENTINEL said no...\n<k>",
    "sl_c5_sneeze_03", "AHHHH...\n<k>",
    "sl_c5_sneeze_prompt", "Hold it in! Press <icon PAD_B 0.6 0 22 0>!",
    "sl_c5_sneeze_crack", "<shake>Hold it in! Press <icon PAD_B 0.6 0 22 0>!</shake>",
    "sl_c5_sneeze_04", "<shake>EEEEEEEEEEEEEEEEE\nEEEEEEEEEEEEEEEEE\nEEEEEEEEEEEEEE!</shake>\n<k>",
    "sl_c5_sneeze_05", "<shake>...CHOOOOOOOO!</shake>\n<k>",

    /* 11. Cranberry */
    "sl_c5_cranberry_00", "<i>Ellune. Ellune sorrima.\nTavi nor Luff-ka, ennu velim?</i>\n<k>",
    "sl_c5_cranberry_01", "Er. Bless you? I mean...\nbless ME. Sorry. Sorry for\nthe noise.\n<k>",
    "sl_c5_cranberry_02", "...There. Now we understand each\nother. Hello. Thank you for\nwaking me.\n<k>",
    "sl_c5_cranberry_03", "I... I didn't mean to wake you!\nIt was the dust! I'm so\nsorry!\n<k>",
    "sl_c5_cranberry_04", "Don't be. A thousand years of\nwaiting, and I always thought\nI'd be woken by a sword,\n<k>\n<p>\nor a spell, or a song. I never\nimagined a sneeze.\n<k>\n<p>\nThat's the nicest surprise I've\nhad in a very long time.\n<k>",
    "sl_c5_cranberry_05", "I'm Cranberry. I'm the last of\nthe Luffs.\n<k>\n<p>\nScreamy, you're still here.\nYou stayed.\n<k>",
    "sl_c5_cranberry_06", "Where would I go? You're my\nonly... you're my...\n<wait 300><shake>EEEEEE.</shake>\n<k>",
    "sl_c5_cranberry_07", "You've read the walls. You know\nwhat the compass did to us.\n<k>",
    "sl_c5_cranberry_08", "It showed you tomorrow. And\nyou stopped living today.\n<k>",
    "sl_c5_cranberry_09", "That's the curse.\nNot a spell.\n<k>\n<p>\nJust what happens to anyone\nwho can see everything\ncoming.\n<k>\n<p>\nWhy plant a seed when you've\nalready seen the tree?\n<k>\n<p>\nWhy say hello when you've\nalready seen goodbye?\n<k>",
    "sl_c5_cranberry_10", "When the city went quiet, I\nbroke the compass in seven.\n<k>\n<p>\nI hid six pieces as far apart\nas the world would let me.\n<k>\n<p>\nI kept the last, and I lay down\nhere to wait for someone who\nwould carry it\n<k>\n<p>\nwithout looking too far.\n<k>",
    "sl_c5_cranberry_11", "You've come so far. You have\nfive pieces, and you haven't\nonce used them to peek at\n<k>\n<p>\nyour own tomorrow,\nhave you?\n<k>",
    "sl_c5_cranberry_12", "I... I didn't know I could.\n<k>",
    "sl_c5_cranberry_13", "Then you're exactly who I was\nwaiting for.\n<k>",
    "sl_c5_piece", "<system>\nYou got a piece of the\nMarvelous Compass!\n<k>",
    "sl_c5_cranberry_14", "Find the one you're looking for.\nBe careful of the compass when\nit's whole.\n<k>\n<p>\nIt will want to show you\neverything. Look only as far as\nyou need.\n<k>\n<p>\nThe rest... let it be\na surprise.\n<k>",
    "sl_c5_cranberry_15", "I'll be careful. I promise.\n<k>",
    "sl_c5_cranberry_16", "Thank you, Luigi. I can rest\nnow.\n<k>",
    "sl_c5_fade_00", "Cranberry? Cranberry, wait...\n<k>",
    "sl_c5_fade_01", "Go with him, Screamy. You've\nlooked after these walls long\nenough.\n<k>\n<p>\nGo see what's out there.\nGo be surprised.\n<k>",
    "sl_c5_fade_02", "...OK. OK. I'll be surprised.\nI'll be SO surprised.\n<k>",

    /* 12. the dark wood */
    "sl_c5_woods_00", "...It's gone. All of it.\n<k>",
    "sl_c5_woods_01", "Best exit I ever saw, baby.\nNo curtain call. Just... gone.\n<k>",
    "sl_c5_woods_02", "...Yeah.\n<k>",
    "sl_c5_woods_03", "I shall never speak of it in\nPlumpbelly. They would not\nunderstand. I do not either.\n<k>",
    "sl_c5_woods_04", "...Good kid.\n<k>",
    "sl_c5_woods_05", "I've never been outside. It's\nvery big. It's very, very big.\n<k>",
    "sl_c5_woods_06", "It is. You're with us now.\nOK?\n<k>",
    "sl_c5_woods_07", "<scale 0.8>OK.\n<k>",

    /* 13. the compass, and the voice */
    "sl_c5_voice_00", "<system>\nLuigi set the sixth piece in\nthe compass.\n<k>",
    "sl_c5_voice_01", "<i>Can you hear me now?\nI can hear you. Your heartbeat,\nlike footsteps on a stair.</i>\n<k>",
    "sl_c5_voice_02", "<i>I'm at the top of the tower in\nthe north, where the wind sings.\nHe's here with me.</i>\n\n<k>\n<p>\n<i>Please, please hurry.</i>\n<k>",
    "sl_c5_voice_03", "<i>There's someone else coming.\nSomeone who wants what you're\ncarrying.</i>\n\n<k>\n<p>\n<i>Don't let him take it.</i>\n<k>",
    "sl_c5_voice_04", "\"He's here with me\"... the\nChestnut King! She's with the\nChestnut King!\n<k>\n<p>\nAt the top of Hatesong\nTower!\n<k>",
    "sl_c5_voice_05", "And \"someone else coming\"?\nWho else? Who else would\nwant...\n<k>",
    "sl_c5_voice_06", "It doesn't matter! Princess\nEclair, I'm coming! I'll save\nyou! I'll be super!\n<k>\n<p>\nI'll be SUPER super! And after\nthat I'll... I'll...\n<k>",
    "sl_c5_voice_07", "...I'll figure that part out\nwhen I get there.\n<k>",

    /* 14. the tag: Screamy's version */
    "sl_c5_tag_caption", "<system>\n<scale 0.8>As Screamy tells it...\n<k>",
    "sl_c5_tag_00", "H-hi. I'm supposed to tell you\nwhat happened. In the ruin.\n<k>",
    "sl_c5_tag_01", "A very brave man came in\nwith a very loud sneeze, and\nhe woke up my friend,\n<k>\n<p>\nand my friend went away happy,\nand the ruin went away too,\nand now I'm here,\n<k>\n<p>\nand it's very big out here.\n<k>",
    "sl_c5_tag_02", "Oh, and before that, there were\nmonsters. Hundreds. Big ones,\nwith teeth.\n<k>\n<p>\nAnd I screamed at them, and\nthey all ran away. All of them.\nEvery one.\n<k>",
    "sl_c5_tag_03", "And the Nothing room, I carried\neveryone across. All five of\nthem. At once. Upside down.\n<k>",
    "sl_c5_tag_04", "...Is this how you tell a story?\nIt's fun. <shake>EEEE.</shake>\n<k>",

    /* the ending's diary page */
    "sl_c5_diary", "Deep in Grimble Forest, the last\nof the Luffs woke to a sneeze,\nand went to his rest smiling.\n<k>\n<p>\nThe Marvelous Compass had its\nsixth part, and its needle\nswung north, toward Hatesong.\n<k>\n<p>\nAnd Screamy saw the sky for\nthe very first time.\n<k>",
    NULL,
};
