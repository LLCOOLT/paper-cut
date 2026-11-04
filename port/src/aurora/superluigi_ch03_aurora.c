/* Super Luigi, chapter 3: Circuit Break Island (docs/super_luigi/ch3.md).
 *
 * The island's race day on the Glitz Pit's maps: Victory Plaza (tou_01, the
 * arrival, the awards and the crowning), Race HQ (tou_02), the Stadium
 * (tou_03, the tou2 REL: the Bumper Bouts and the King's Bout), Pit Lane
 * (tou_04) and its rooms (tou_05-tou_13), and the Pianta Parlor's courses as
 * the Start Grid (yuu_00) and the Circuit (yuu_02). Torque joins after the
 * second bout and his Tune-up opens the way to Big Green 01's three stolen
 * parts. The bouts and King Revvo are in superluigi_revvo_aurora.c, the
 * race (Kart Mode, the Lone Lap) in superluigi_race_aurora.c, the text in
 * superluigi_ch03_text_aurora.c.
 *
 * The plan's tou_30 (a cliff lift station on tou_00) is the plaza's west
 * end: tou_00 is the blimp's flight, with no floor (the plan's own
 * fallback). Flags: the framework's bank, SL3F(n) = SL_GSWF(3, n) (superluigi_ch03.h). */
#include "port/superluigi.h"
#include "port/superluigi_evt.h"
#include "port/superluigi_ch03.h"
#include "data/item_data.h"
#include "evt/evt_shop.h"
#include "mario/mario.h"
#include "mario/mario_party.h"
#include "mario/mario_pouch.h"
#include "party.h"
#include "mario/mariost.h"
#include "system.h"
#include "data_tables.h"
#include "driver/swdrv.h"

#include <stdio.h>
#include <string.h>

/* ======================================================================
 * Shared pieces
 * ====================================================================== */

#define M(tag, who) CALL(evt_msg_print, 0, STRING(tag), 0, STRING(who))
#define SYS(tag) CALL(evt_msg_print, 0, STRING(tag), 0, 0)

/* the Glitz Pit's doors: one leaf (rotated about y), or two opening apart */
#define DOOR1(n, obj, k)                                                   \
    static EVT_BEGIN(n)                                                    \
        MULTIPLY_FLOAT(LW(0), FLOAT(k))                                    \
        CALL(evt_mapobj_rotate, 1, STRING(obj), 0, LW(0), 0)               \
        RETURN                                                             \
    EVT_END()
#define DOOR2(n, a, b, k)                                                  \
    static EVT_BEGIN(n)                                                    \
        MULTIPLY_FLOAT(LW(0), FLOAT(k))                                    \
        CALL(evt_mapobj_rotate, 1, STRING(b), 0, LW(0), 0)                 \
        MULTIPLY_FLOAT(LW(0), FLOAT(-1.0f))                                \
        CALL(evt_mapobj_rotate, 1, STRING(a), 0, LW(0), 0)                 \
        RETURN                                                             \
    EVT_END()
#define DOORT(n, a, b, k)                                                  \
    static EVT_BEGIN(n)                                                    \
        MULTIPLY_FLOAT(LW(0), FLOAT(k))                                    \
        CALL(evt_mapobj_trans, 1, STRING(b), LW(0), 0, 0)                  \
        MULTIPLY_FLOAT(LW(0), FLOAT(-1.0f))                                \
        CALL(evt_mapobj_trans, 1, STRING(a), LW(0), 0, 0)                  \
        RETURN                                                             \
    EVT_END()
/* a door bero: hit, sound, to (map, bero), its animation */
#define BDOORD(hit, sfx, dir, map, bero, anim) {hit, 1, sfx, dir, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, map, bero, 5, 5, anim, anim}
#define BDOOR(hit, sfx, map, bero, anim) {hit, 1, sfx, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, map, bero, 5, 5, anim, anim}
/* an entrance with no walk-in and no exit, for the scenes that bring Luigi in (the game arms
 * a room's exits only after an arrival by one of its entrances); its exit is switched off */
#define BHERE(name) {name, BERO_TYPE_NONE, 0, BERO_DIR_AUTO, 0, 0, 0, -1, NULL, -1, NULL, NULL, NULL, 0, 0, 0, 0}
#define BROAD(hit, map, bero) {hit, 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, map, bero, 1, 1, 0, 0}
/* a door locked (its exit unregistered) or opened, by its hit's name: the game's own switch */
#define BERO_OFF(hit) SET_VALUE(LW(0), STRING(hit)) CALL_CHILD(bero_case_switch_off)
/* a door that opens while Luigi is in the room: the exits are registered as the room
 * comes up, so the room comes up again (a quick fade), Luigi where he was (LW(0) the map,
 * LW(1) the entrance name the room's init places him by) */
static EVT_BEGIN(reopen_evt)
    WAIT_FRAMES(40)
    CALL(evt_bero_mapchange, LW(0), LW(1))
    RETURN
EVT_END()
#define REOPEN(map, ent) SET_VALUE(LW(0), STRING(map)) SET_VALUE(LW(1), STRING(ent)) SPAWN_EVENT(reopen_evt)
/* in an init: placed by a reopen (LW(0) its name) */
#define REOPENED(ent, x, z, dir)                         CALL(evt_bero_get_entername, LW(0))                   IF_STR_EQUAL(LW(0), STRING(ent))                          CALL(evt_mario_set_pos, x, 0, z)                      CALL(evt_mario_set_dir, dir, 0, 0)                    CALL(evt_party_set_pos, 0, x, 0, z)               END_IF
#define BERO_RUN(tbl)                \
    SET_VALUE(LW(0), PTR(tbl))       \
    CALL(evt_bero_get_info)          \
    CALL_CHILD(evt_bero_info_run)


/* in: a friend; out: who speaks for him: "party" when he's the one out, else his scene NPC */
static USER_FUNC(sl3_speaker) {
    static const char* const kNpc[] = {"", "sl_bl", "sl_je", "sl_tq", "", ""};
    const int id = (int)evtGetValue(event, event->args[0]);
    const PortSlFriend* f = portSlGetFriend(id);
    /* marioGetPartyId() is the party entry's slot, not who's in it */
    const PartyEntry* p = partyGetPtr(marioGetPartyId());
    (void)isFirstCall;
    evtSetValue(event, event->args[1],
                (EvtWord)(AddrWord)(f != NULL && p != NULL && p->currentMemberId == f->partyId ? "party" : kNpc[id]));
    return EVT_RETURN_DONE;
}

/* the chapter's progress, for the room scripts (out: the next bout, 0 none) */
static int sBoutPending;
USER_FUNC(sl3_bout_set) {
    (void)isFirstCall;
    sBoutPending = (int)evtGetValue(event, event->args[0]);
    return EVT_RETURN_DONE;
}
USER_FUNC(sl3_bout_get) {
    (void)isFirstCall;
    evtSetValue(event, event->args[0], sBoutPending);
    return EVT_RETURN_DONE;
}

/* an NPC of ours: tribe t (0: none), its talk (0: none) as a literal: evt_npc_change_interrupt
 * takes it unevaluated. Only LW(15) is used: this once went through LW(0)-LW(6), which wrote over
 * the very variables a caller's x and z came from (Charlie, Torque's join and Lug and Nut were placed
 * by a string's address, and Lug and Nut walked at it for ever) */
#define NPC(n, m, t, x, y, z, ry, talk)              \
    CALL(evt_npc_entry, STRING(n), STRING(m))        \
    SET_VALUE(LW(15), t)                             \
    IF_INT_NOT_EQUAL(LW(15), 0)                      \
        CALL(evt_npc_set_tribe, STRING(n), LW(15))   \
    END_IF                                           \
    CALL(evt_npc_set_position, STRING(n), x, y, z)   \
    CALL(evt_npc_set_ry, STRING(n), ry)              \
    CALL(evt_npc_change_interrupt, STRING(n), 6, talk)

/* Blooey and Jerry, for the scenes: whichever isn't following Luigi stands
 * beside him as an NPC (LW(0)-LW(2): where Luigi is) */
static EVT_BEGIN(crew_in)
    CALL(evt_mario_get_pos, 0, LW(10), LW(11), LW(12))
    CALL(sl3_speaker, PORT_SL_FRIEND_BLOOEY, LW(13))
    IF_STR_NOT_EQUAL(LW(13), STRING("party"))
        CALL(evt_npc_entry, STRING("sl_bl"), STRING("c_lp_gesso"))
        CALL(evt_npc_set_tribe, STRING("sl_bl"), STRING(SJ3_BLOOEY))
        SET_VALUE(LW(14), LW(10))
        SUBTRACT(LW(14), 45)
        SET_VALUE(LW(15), LW(12))
        ADD(LW(15), 25)
        CALL(evt_npc_set_position, STRING("sl_bl"), LW(14), LW(11), LW(15))
        CALL(evt_npc_set_ry, STRING("sl_bl"), 90)
    END_IF
    CALL(sl3_speaker, PORT_SL_FRIEND_JERRY, LW(13))
    IF_STR_NOT_EQUAL(LW(13), STRING("party"))
        CALL(evt_npc_entry, STRING("sl_je"), STRING("c_lp_bom"))
        CALL(evt_npc_set_tribe, STRING("sl_je"), STRING(SJ3_JERRY))
        SET_VALUE(LW(14), LW(10))
        SUBTRACT(LW(14), 70)
        SET_VALUE(LW(15), LW(12))
        ADD(LW(15), -20)
        CALL(evt_npc_set_position, STRING("sl_je"), LW(14), LW(11), LW(15))
        CALL(evt_npc_set_ry, STRING("sl_je"), 90)
    END_IF
    RETURN
EVT_END()

static EVT_BEGIN(crew_out)
    CALL(evt_npc_check, STRING("sl_bl"), LW(13))
    IF_INT_EQUAL(LW(13), 1)
        CALL(evt_npc_delete, STRING("sl_bl"))
    END_IF
    CALL(evt_npc_check, STRING("sl_je"), LW(13))
    IF_INT_EQUAL(LW(13), 1)
        CALL(evt_npc_delete, STRING("sl_je"))
    END_IF
    RETURN
EVT_END()

/* in: a friend: he comes out as the partner beside Luigi when nobody is following (the chapter
 * starts with the party put away, and its slot can still hold the card's stale entry, which
 * makes evt_mario_set_party_pos do nothing) */
static USER_FUNC(sl3_friend_out) {
    MarioWork* m = marioGetPtr();
    const PortSlFriend* f = portSlGetFriend((int)evtGetValue(event, event->args[0]));
    s32 id;
    (void)isFirstCall;
    if (f == NULL || partyGetPtr(marioGetPartyId()) != NULL) {
        return EVT_RETURN_DONE;
    }
    id = partyEntryPos(f->partyId, m->position.x - 40.0f, m->position.y, m->position.z);
    if (id >= 0) {
        m->currPartySlotId[0] = id;
    }
    return EVT_RETURN_DONE;
}

static EVT_BEGIN(jerry_out)
    CALL(sl3_friend_out, PORT_SL_FRIEND_JERRY)
    RETURN
EVT_END()

/* Torque out as the partner (once he's joined), at Luigi's side */
static EVT_BEGIN(torque_out)
    IF_INT_EQUAL(SL3F(F_TORQUE), 1)
        CALL(sl3_speaker, PORT_SL_FRIEND_TORQUE, LW(13))
        IF_STR_NOT_EQUAL(LW(13), STRING("party"))
            CALL(evt_mario_get_pos, 0, LW(10), LW(11), LW(12))
            SUBTRACT(LW(10), 35)
            CALL(evt_mario_get_party, LW(13))
            IF_INT_NOT_EQUAL(LW(13), 0)
                CALL(evt_mario_goodbye_party, 0)
            END_IF
            CALL(evt_sl_friend_slot, PORT_SL_FRIEND_TORQUE, LW(13))
            CALL(evt_mario_set_party_pos, 0, LW(13), LW(10), LW(11), LW(12))
            WAIT_FRAMES(2)
        END_IF
    END_IF
    RETURN
EVT_END()
#define TQ(tag)                      CALL_CHILD(torque_out)           M(tag, "party")

/* a friend's line, whoever's out: SAY(PORT_SL_FRIEND_x, tag) */
#define SAY(f, tag)                          \
    CALL(sl3_speaker, f, LW(13))             \
    CALL(evt_msg_print, 0, STRING(tag), 0, LW(13))

/* "You got X!": the fanfare and a system box */
#define GOT(tag)                                                 \
    CALL(evt_snd_bgmon, PSND_FADE_OUT | PSND_FADE_HOLD, STRING("BGM_FF_GET_IMPORTANT_ITEM1")) \
    SYS(tag)

/* a chest that pops its item, as the game's own (tou_01's mobj_box00) */
#define BOXOPEN(n, box, item)                                                           static EVT_BEGIN(n)                                                                     CALL(evt_mario_key_onoff, 0)                                                        CALL(evt_mario_normalize)                                                           CALL(evt_mobj_get_position, STRING(box), LW(0), LW(1), LW(2))                       CALL(evt_item_entry, STRING("boxitem"), item, LW(0), LW(1), LW(2), ITEM_ENTRY_FIXED, -1, 0)         CALL(evt_item_flag_on, STRING("boxitem"), ITEM_FLAG_BADGE_RETURN | ITEM_FLAG_NO_TOUCH)         CALL(evt_mobj_wait_animation_end, STRING(box))                                      CALL(evt_item_get_item, STRING("boxitem"))                                          CALL(evt_mario_key_onoff, 1)                                                        RETURN                                                                          EVT_END()

/* a key item's get: the game's item-get pose and fanfare */
static EVT_BEGIN(key_get)
    CALL(evt_mario_set_pose, STRING("M_I_2"))
    CALL(evt_snd_sfxon, STRING("SFX_STAR_POINT_GET1"), 0)
    WAIT_MS(300)
    CALL(evt_msg_print, 0, LW(0), 0, 0)
    CALL(evt_mario_set_pose, STRING("M_S_1"))
    RETURN
EVT_END()
#define KEY(tag)                  \
    SET_VALUE(LW(0), STRING(tag)) \
    CALL_CHILD(key_get)

/* ======================================================================
 * tou_l31, Victory Plaza (tou_01): arrival, awards, shop, crowning
 * ====================================================================== */

DOOR2(d31_hq, "door_1_a", "door_1_b", 0.333f)
static BeroEntry b31[] = {
    BDOOR("a_door_mon", 0, "tou_l32", "s_bero", d31_hq),
    {0},
};

static ShopObj kShopSpots[] = {
    {"sina_1", "a_sina1"}, {"sina_2", "a_sina_2"}, {"sina_3", "a_sina_3"},
    {"sina_4", "a_sina_4"}, {"sina_5", "a_sina_5"}, {"sina_6", "a_sina_6"},
};
/* Charlie's Parts & Snacks: the plan's nine goods, six on the shelves */
static ShopItem kShopGoods[] = {
    {ITEM_MUSHROOM, 5}, {ITEM_SUPER_SHROOM, 20}, {ITEM_HONEY_SYRUP, 10},
    {ITEM_FIRE_FLOWER, 10}, {ITEM_THUNDER_BOLT, 10}, {ITEM_POW_BLOCK, 10},
};
static ShopDb kShopTrade[] = {
    {ITEM_MUSHROOM, 2, 0}, {ITEM_SUPER_SHROOM, 7, 0}, {ITEM_HONEY_SYRUP, 3, 0}, {ITEM_MAPLE_SYRUP, 10, 0},
    {ITEM_FIRE_FLOWER, 5, 0}, {ITEM_THUNDER_BOLT, 5, 0}, {ITEM_POW_BLOCK, 5, 0}, {ITEM_FRIGHT_MASK, 5, 0},
    {ITEM_DIZZY_DIAL, 6, 0}, {0},
};
static ShopperData35 kShopper = {
    "sl_charlie", NULL, NULL,
    {"shop_00", "shop_01", "shop_02", "shop_03", "shop_04", "shop_05", "shop_06", "shop_07", "shop_08", "shop_09",
     "shop_10", "shop_11", "shop_12", "shop_13", "shop_14", "shop_15", "shop_16", "shop_17", "shop_18", "shop_19",
     "shop_20", "shop_21", "shop_22", "shop_23", "shop_24", "shop_25", "shop_26", "shop_27", "shop_28", "shop_29",
     "shop_30", "shop_31", "shop_32", "shop_33", "shop_34"},
};

static EVT_BEGIN(fan_talk)
    CALL(evt_mario_key_onoff, 0)
    IF_INT_EQUAL(SL3F(F_RACE_WON), 1)
        M("sl_c3_npc_fan_03", "sl_fan")
    ELSE
        IF_INT_EQUAL(SL3F(F_KING), 1)
            M("sl_c3_npc_fan_02", "sl_fan")
        ELSE
            IF_INT_EQUAL(SL3F(F_FAN), 0)
                M("sl_c3_awards_09", "sl_fan")
                M("sl_c3_awards_10", "sl_fan")
                M("sl_c3_awards_11", "mario")
                M("sl_c3_awards_12", "sl_fan")
                SET_VALUE(SL3F(F_FAN), 1)
            ELSE
                M("sl_c3_npc_fan_01", "sl_fan")
            END_IF
        END_IF
    END_IF
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(clubkoopa_talk)
    IF_INT_EQUAL(SL3F(F_RACE_WON), 1)
        M("sl_c3_npc_club_02", "sl_club")
    ELSE
        M("sl_c3_npc_club_01", "sl_club")
    END_IF
    RETURN
EVT_END()

static EVT_BEGIN(pianta_talk)
    IF_INT_EQUAL(SL3F(F_RACE_WON), 1)
        M("sl_c3_npc_pianta_03", "sl_pianta")
    ELSE
        IF_INT_EQUAL(SL3F(F_BOUT2), 1)
            M("sl_c3_npc_pianta_02", "sl_pianta")
        ELSE
            M("sl_c3_npc_pianta_01", "sl_pianta")
        END_IF
    END_IF
    RETURN
EVT_END()

static EVT_BEGIN(sign31_read)
    SYS("sl_c3_sign_31")
    RETURN
EVT_END()

/* beat 1: the arrival (black, the noise, then the plaza's west end) */
static EVT_BEGIN(arrive_scene)
    /* black and the noise first (the door's walk-out happens behind it), then the plaza's west end */
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_seq_wait, 2)
    CALL(evt_fade_out, 0)
    WAIT_MS(1200)
    CALL(evt_mario_set_pos, -330, 0, 560)
    CALL(evt_mario_set_dir, 90, 0, 0)
    /* the boat in (the plan's ship's horn), then the island's racket */
    CALL(evt_snd_sfxon, STRING("SFX_EVT_OPN_SHIP_MOVE1"), 0)
    WAIT_MS(800)
    CALL(evt_snd_sfxon, STRING("SFX_STG3_WIN_AUDIENCE1"), 0)
    SYS("sl_c3_arrive_01")
    CALL(evt_fade_in, 700)
    WAIT_MS(300)
    CALL_CHILD(crew_in)
    CALL(evt_mario_set_pose, STRING("M_I_Y"))
    M("sl_c3_arrive_02", "mario")
    CALL(evt_mario_set_pose, STRING("M_S_1"))
    SAY(PORT_SL_FRIEND_JERRY, "sl_c3_arrive_03")
    SAY(PORT_SL_FRIEND_BLOOEY, "sl_c3_arrive_04")
    M("sl_c3_arrive_05", "mario")
    CALL_CHILD(crew_out)
    CALL_CHILD(jerry_out)
    SET_VALUE(SL3F(F_ARRIVED), 1)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* beat 2: the awards on the star stage */
static EVT_BEGIN(awards_scene)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_STG3_TOUGI1"))
    NPC("sl_revvo", "c_togenoko_b", STRING(SJ3_KOOPATROL), 0, 0, 230, 0, 0)
    NPC("sl_crown", "c_sl_crown", 0, 0, 40, 231, 0, 0)
    NPC("sl_trophy", "c_sl_trophy", 0, -40, 0, 236, 0, 0)
    CALL(evt_npc_set_scale, STRING("sl_trophy"), FLOAT(1.6f), FLOAT(1.6f), FLOAT(1.6f))
    NPC("sl_lakky", "c_jugemu", STRING(SJ3_LAKITU), 70, 45, 200, 270, 0)
    NPC("sl_mic", "c_sl_mic", 0, 58, 53, 202, 270, 0)
    CALL(evt_npc_set_anim, STRING("sl_mic"), STRING("T_1"))
    CALL(evt_mario_mov_pos2, -90, 420, FLOAT(110.0f))
    CALL(evt_mario_set_dir, 90, 0, 0)
    CALL_CHILD(crew_in)
    CALL(evt_cam3d_evt_set, 0, 150, 760, 0, 40, 260, 1000, 11)
    WAIT_MS(1000)
    M("sl_c3_awards_01", "sl_lakky")
    M("sl_c3_awards_02", "sl_lakky")
    CALL(evt_snd_sfxon, STRING("SFX_STG3_WIN_AUDIENCE2"), 0)
    /* the trophy up high: the piece on its lid glints, and Luigi's compass answers */
    CALL(evt_cam3d_evt_set, -40, 44, 400, -40, 20, 236, 900, 11)
    WAIT_MS(900)
    CALL(evt_eff, 0, STRING("stardust"), 0, -40, 26, 238, 0, 0, 0, 0, 0, 0, 0, 0)
    WAIT_MS(400)
    CALL(evt_cam3d_evt_off, 600, 11)
    CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
    ADD(LW(1), 30)
    CALL(evt_snd_sfxon, STRING("SFX_ITEM_KIRAKIRA_FALL1"), 0)
    CALL(evt_eff, 0, STRING("stardust"), 0, LW(0), LW(1), LW(2), 0, 0, 0, 0, 0, 0, 0, 0)
    M("sl_c3_awards_03", "mario")
    CALL(evt_luigi_story_pose, STRING("SL_WOBBLE"))
    WAIT_MS(1200)
    CALL(evt_luigi_story_pose, 0)
    SAY(PORT_SL_FRIEND_JERRY, "sl_c3_awards_04")
    M("sl_c3_awards_05", "sl_revvo")
    M("sl_c3_awards_06", "sl_revvo")
    CALL(evt_snd_sfxon, STRING("SFX_STG3_WIN_AUDIENCE1"), 0)
    SYS("sl_c3_awards_07")
    M("sl_c3_awards_08", "sl_revvo")
    /* the crowd thins; the King leaves the stage */
    CALL(evt_fade_out, 500)
    WAIT_MS(600)
    CALL(evt_npc_delete, STRING("sl_revvo"))
    CALL(evt_npc_delete, STRING("sl_crown"))
    CALL(evt_npc_delete, STRING("sl_trophy"))
    CALL(evt_npc_delete, STRING("sl_lakky"))
    CALL(evt_npc_delete, STRING("sl_mic"))
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG3_URN1"))
    CALL(evt_fade_in, 500)
    WAIT_MS(500)
    /* a fan explains, once */
    CALL(evt_npc_get_position, STRING("sl_fan"), LW(0), LW(1), LW(2))
    CALL(evt_npc_move_position, STRING("sl_fan"), -30, 450, 0, FLOAT(90.0f), 4)
    CALL(evt_mario_set_dir_npc, STRING("sl_fan"))
    M("sl_c3_awards_09", "sl_fan")
    M("sl_c3_awards_10", "sl_fan")
    M("sl_c3_awards_11", "mario")
    M("sl_c3_awards_12", "sl_fan")
    SET_VALUE(SL3F(F_FAN), 1)
    M("sl_c3_awards_13", "mario")
    SAY(PORT_SL_FRIEND_BLOOEY, "sl_c3_awards_14")
    SAY(PORT_SL_FRIEND_JERRY, "sl_c3_awards_15")
    M("sl_c3_awards_16", "mario")
    CALL_CHILD(crew_out)
    SET_VALUE(SL3F(F_AWARDS), 1)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(arrive_and_awards)
    IF_INT_EQUAL(SL3F(F_ARRIVED), 0)
        CALL_CHILD(arrive_scene)
    END_IF
    IF_INT_EQUAL(SL3F(F_AWARDS), 0)
        CALL_CHILD(awards_scene)
    END_IF
    RETURN
EVT_END()

/* beats 16-19: the crowning, the voice, Torque comes along, the tag */
static EVT_BEGIN(crown_scene)
    CALL(evt_seq_wait, 2)
    WAIT_MS(1200)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_mario_set_pos, -120, 0, 520)
    CALL(evt_mario_set_dir, 90, 0, 0)
    CALL(evt_map_set_blend, 0, 200, 160, 190, 255)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_STAGE_CLEAR1"))
    NPC("sl_revvo", "c_togenoko_b", STRING(SJ3_KOOPATROL), 70, 0, 260, 270, 0)
    NPC("sl_lakky", "c_jugemu", STRING(SJ3_LAKITU), 0, 45, 200, 270, 0)
    NPC("sl_mic", "c_sl_mic", 0, -12, 53, 202, 270, 0)
    CALL(evt_npc_set_anim, STRING("sl_mic"), STRING("T_1"))
    NPC("sl_trophy", "c_sl_trophy", 0, 30, 0, 236, 0, 0)
    CALL(evt_npc_set_scale, STRING("sl_trophy"), FLOAT(1.6f), FLOAT(1.6f), FLOAT(1.6f))
    CALL_CHILD(crew_in)
    CALL(evt_seq_wait, 2)
    CALL(evt_mario_mov_pos2, -20, 300, FLOAT(100.0f))
    CALL(evt_mario_set_dir, 90, 0, 0)
    CALL(evt_cam3d_evt_set, 0, 150, 760, 0, 40, 260, 1000, 11)
    WAIT_MS(800)
    M("sl_c3_crown_01", "sl_lakky")
    /* the crown on Luigi's cap */
    CALL(sl3_luigi_crown, 1)
    CALL(evt_snd_sfxon, STRING("SFX_STAR_POINT_GET1"), 0)
    CALL(evt_snd_sfxon, STRING("SFX_STG3_WIN_AUDIENCE2"), 0)
    SYS("sl_c3_crown_02")
    M("sl_c3_crown_03", "mario")
    M("sl_c3_crown_04", "sl_revvo")
    M("sl_c3_crown_05", "mario")
    M("sl_c3_crown_06", "sl_revvo")
    M("sl_c3_crown_07", "mario")
    /* the piece off the trophy's lid */
    CALL(evt_npc_set_anim, STRING("sl_trophy"), STRING("P_1"))
    CALL(evt_snd_sfxon, STRING("SFX_ITEM_KIRAKIRA_FALL1"), 0)
    WAIT_MS(450)
    CALL(evt_npc_set_anim, STRING("sl_trophy"), STRING("S_2"))
    CALL(evt_mario_set_pose, STRING("M_I_2"))
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_FF_GET_IMPORTANT_ITEM1"))
    SYS("sl_c3_crown_08")
    CALL(evt_mario_set_pose, STRING("M_S_1"))
    CALL(evt_cam3d_evt_off, 800, 11)
    /* beat 17: the voice */
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_ORGEL1"))
    CALL(evt_sl_map_wash, 0, 0x101020, 70)
    CALL(evt_snd_sfxon, STRING("SFX_ITEM_KIRAKIRA_FALL1"), 0)
    WAIT_MS(800)
    CALL(evt_msg_print, 0, STRING("sl_c3_voice_01"), 0, 0)
    CALL(evt_msg_print, 0, STRING("sl_c3_voice_02"), 0, 0)
    CALL(evt_sl_map_look, 0, 0xFFFFFF, 0)
    CALL(evt_map_set_blend, 0, 200, 160, 190, 255)
    WAIT_MS(500)
    M("sl_c3_voice_03", "mario")
    SAY(PORT_SL_FRIEND_BLOOEY, "sl_c3_voice_04")
    M("sl_c3_voice_05", "mario")
    SAY(PORT_SL_FRIEND_JERRY, "sl_c3_voice_06")
    M("sl_c3_voice_07", "mario")
    /* beat 18: Torque by the Girl, with a toolbox */
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG3_URN1"))
    NPC("sl_kart", "c_lp_kart", 0, -170, 0, 340, 0, 0)
    CALL(evt_npc_set_anim, STRING("sl_kart"), STRING("S_1"))
    CALL(evt_mario_set_dir, 270, 0, 0)
    CALL_CHILD(torque_out)
    CALL(evt_party_move_pos, 0, -130, 345, 0)
    CALL(evt_party_run, 0)
    TQ("sl_c3_torque_comes_01")
    CALL(evt_mario_set_dir, 270, 0, 0)
    TQ("sl_c3_torque_comes_02")
    TQ("sl_c3_torque_comes_03")
    M("sl_c3_torque_comes_04", "mario")
    TQ("sl_c3_torque_comes_05")
    SET_VALUE(SL3F(F_CROWN), 1)
    CALL_CHILD(crew_out)
    /* beat 19: the tag, Torque's version: one spotlight, held stills */
    CALL(evt_fade_out, 800)
    WAIT_MS(900)
    CALL(sl3_luigi_crown, 0)
    CALL(evt_snd_bgmoff, PSND_FADE_OUT)
    CALL(evt_npc_delete, STRING("sl_revvo"))
    CALL(evt_npc_delete, STRING("sl_lakky"))
    CALL(evt_npc_delete, STRING("sl_mic"))
    CALL(evt_npc_delete, STRING("sl_trophy"))
    CALL(evt_npc_set_position, STRING("sl_kart"), 0, -1000, 0)
    /* the plaza's crowd out of the shot */
    CALL(evt_npc_set_position, STRING("sl_club"), 0, -1000, 0)
    CALL(evt_npc_set_position, STRING("sl_fan"), 0, -1000, 0)
    CALL(evt_npc_set_position, STRING("sl_fan2"), 0, -1000, 0)
    CALL(evt_npc_set_position, STRING("sl_pianta"), 0, -1000, 0)
    CALL(evt_npc_set_position, STRING("sl_charlie"), 0, -1000, 0)
    CALL(evt_mario_set_pos, 0, -1000, 0)
    CALL(evt_mario_dispflag_onoff, 1, 1)
    CALL(evt_npc_check, STRING("sl_tq"), LW(0))
    IF_INT_EQUAL(LW(0), 0)
        NPC("sl_tq", "c_lp_met", STRING(SJ3_TORQUE), -250, 0, 600, 0, 0)
    END_IF
    CALL(evt_party_set_pos, 0, 0, -1000, 0)
    CALL(evt_npc_set_position, STRING("sl_tq"), -250, 0, 600)
    CALL(evt_npc_set_ry, STRING("sl_tq"), 0)
    CALL(evt_map_set_blend, 0, 150, 150, 170, 255)
    CALL(evt_sl_map_look, 0, 0x303040, 0)
    CALL(evt_cam3d_evt_set, -250, 40, 730, -250, 18, 600, 0, 11) /* the open plaza west of the stage */
    CALL(sl3_spotlight, 1, -250, 600)
    CALL(evt_fade_in, 800)
    WAIT_MS(900)
    CALL(sl3_caption, STRING("sl_c3_tag_caption"))
    M("sl_c3_tag_01", "sl_tq")
    CALL(sl3_caption, 0)
    M("sl_c3_tag_02", "sl_tq")
    /* "slingshot": the crash, held, grey */
    CALL(evt_npc_set_position, STRING("sl_tq"), 0, -1000, 0)
    NPC("sl_still", "c_lp_kart", 0, -250, 0, 600, 0, 0)
    CALL(evt_npc_set_anim, STRING("sl_still"), STRING("C_2"))
    CALL(evt_sl_map_wash, 0, 0x808080, 80)
    CALL(evt_snd_sfxon, STRING("SFX_STG0_GESO_CRASH1"), 0)
    WAIT_MS(1200)
    CALL(evt_npc_delete, STRING("sl_still"))
    CALL(evt_sl_map_look, 0, 0x303040, 0)
    CALL(evt_npc_set_position, STRING("sl_tq"), -250, 0, 600)
    M("sl_c3_tag_03", "sl_tq")
    M("sl_c3_tag_04", "sl_tq")
    /* "Even the driver didn't know": Luigi mid-scream */
    CALL(evt_npc_set_position, STRING("sl_tq"), 0, -1000, 0)
    NPC("sl_still", "c_luigi_kart", 0, -250, 0, 600, 0, 0)
    CALL(evt_npc_set_anim, STRING("sl_still"), STRING("B_1"))
    CALL(evt_sl_map_wash, 0, 0x808080, 80)
    WAIT_MS(1200)
    CALL(evt_npc_delete, STRING("sl_still"))
    CALL(evt_sl_map_look, 0, 0x303040, 0)
    CALL(evt_npc_set_position, STRING("sl_tq"), -250, 0, 600)
    M("sl_c3_tag_05", "sl_tq")
    /* the curtain comes down on Torque in his spotlight: a fade to black here left the whole ending
     * (the curtain, "Chapter Clear") on a black screen, as nothing fades back in */
    CALL(evt_mario_dispflag_onoff, 0, 1)
    CALL(evt_sl_chapter_end)
    CALL(sl3_spotlight, 0, 0, 0)
    RETURN
EVT_END()

static EVT_BEGIN(tou_l31_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG3_URN1"))
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG3_TOU1"))
    BERO_RUN(b31)
    CALL(evt_map_playanim, STRING("anm_den"), 1, 0)
    CALL(evt_map_playanim, STRING("anm_denden"), 1, 0)
    CALL(evt_map_playanim, STRING("anm_raito"), 1, 0)
    /* the old cliff station's corner: the sign, a Save Block, a hidden block */
    CALL(evt_mobj_save_blk, STRING("mobj_save"), -420, 60, 500, 0, 0)
    CALL(evt_mobj_signboard, STRING("board"), -210, 0, 640, PTR(sign31_read), LSWF(0))
    CALL(evt_mobj_brick, STRING("mobj_hidden"), -330, 75, 640, ITEM_STAR_PIECE, 11, 0, SL3F(F_31_HIDDEN))
    /* the star stage's points: a coin block and a Volt Shroom; a Star Piece under the back steps */
    CALL(evt_mobj_badgeblk, STRING("mobj_coin"), -110, 60, 330, ITEM_COIN, 0, SL3F(F_31_COIN), 0)
    CALL(evt_mobj_badgeblk, STRING("mobj_volt"), 110, 60, 330, ITEM_VOLT_SHROOM, 0, SL3F(F_31_VOLT), 0)
    CALL(evt_mobj_kururing_floor, STRING("kururin31"), 0, 0, 800, STRING("jimen"), 125, SL3F(F_31_PIECE))
    /* Charlie's Parts & Snacks */
    CALL(evt_mapobj_get_position, STRING("sina_3"), LW(0), LW(1), LW(2))
    SET_VALUE(LW(3), LW(2))
        SUBTRACT(LW(3), 60)
    NPC("sl_charlie", "c_kinopio", STRING(SJ3_TOAD), LW(0), 0, LW(3), 0, 0)
    CALL(evt_shop_setup, PTR(kShopSpots), PTR(kShopGoods), PTR(&kShopper), PTR(kShopTrade))
    /* the fans, the souvenir Pianta, the fan-club Koopa */
    NPC("sl_fan", "c_kinopio", STRING(SJ3_TOAD), 140, 0, 460, 270, PTR(fan_talk))
    NPC("sl_fan2", "c_kinopiko", STRING(SJ3_TOAD_F), 220, 0, 520, 270, PTR(fan_talk))
    NPC("sl_pianta", "c_roten", STRING(SJ3_PIANTA), 350, 0, 700, 270, PTR(pianta_talk))
    NPC("sl_club", "c_nokonoko", STRING(SJ3_KOOPA), -250, 0, 450, 90, PTR(clubkoopa_talk))
    IF_INT_EQUAL(SL3F(F_RACE_WON), 1)
        IF_INT_EQUAL(SL3F(F_CROWN), 0)
            SPAWN_EVENT(crown_scene)
            RETURN
        END_IF
    END_IF
    SPAWN_EVENT(arrive_and_awards)
    RETURN
EVT_END()

/* ======================================================================
 * tou_l32, Race HQ Lobby (tou_02)
 * ====================================================================== */

DOORT(d32_s, "door_1_a", "door_1_b", 0.333f)
DOOR2(d32_n1, "door_2_a", "door_2_b", -0.38867188f)
DOOR2(d32_n2, "door_3_a", "door_3_b", -0.38867188f)
DOOR1(d32_n3, "door_4", 0.44433594f)
DOOR1(d32_n4, "door_5", 0.44433594f)
static BeroEntry b32[] = {
    BDOOR("s_bero", 0, "tou_l31", "a_door_mon", d32_s),
    BDOOR("n_bero_1", 6, "tou_l33", "s_bero_1", d32_n1),
    BDOOR("n_bero_2", 6, "tou_l33", "s_bero_2", d32_n2),
    BDOOR("n_bero_3", 4, "tou_l34", "e_bero", d32_n3),
    BDOOR("n_bero_4", 4, "tou_l34", "w_bero", d32_n4),
    {0},
};

static EVT_BEGIN(checkers_talk)
    CALL(evt_mario_key_onoff, 0)
    IF_INT_EQUAL(SL3F(F_SIGNED), 0)
        M("sl_c3_hq_01", "sl_checkers")
        M("sl_c3_hq_02", "mario")
        M("sl_c3_hq_03", "sl_checkers")
        M("sl_c3_hq_04", "sl_checkers")
        M("sl_c3_hq_05", "sl_checkers")
        M("sl_c3_hq_06", "mario")
        M("sl_c3_hq_07", "sl_checkers")
        KEY("sl_c3_hq_got_pass")
        SET_VALUE(SL3F(F_SIGNED), 1)
        REOPEN("tou_l34", "e_bero") /* the west end: Torque's garage is the next door along */
        CALL(evt_mario_key_onoff, 1)
        RETURN
    END_IF
    IF_INT_EQUAL(SL3F(F_KING), 1)
        IF_INT_EQUAL(SL3F(F_LICENSE_STAMP), 0)
            M("sl_c3_hq_stamp", "sl_checkers")
            CALL(evt_snd_sfxon, STRING("SFX_STAR_POINT_GET1"), 0)
            SET_VALUE(SL3F(F_LICENSE_STAMP), 1)
            CALL(evt_mario_key_onoff, 1)
            RETURN
        END_IF
    END_IF
    IF_INT_EQUAL(SL3F(F_BOUT1), 1)
        IF_INT_EQUAL(SL3F(F_32_TUTOR), 0)
            M("sl_c3_hq_tutor", "sl_checkers")
            CALL(evt_mario_key_onoff, 1)
            RETURN
        END_IF
    END_IF
    IF_INT_EQUAL(SL3F(F_RACE_WON), 1)
        M("sl_c3_npc_checkers_03", "sl_checkers")
    ELSE
        IF_INT_EQUAL(SL3F(F_BOUT2), 1)
            M("sl_c3_npc_checkers_02", "sl_checkers")
        ELSE
            M("sl_c3_npc_checkers_01", "sl_checkers")
        END_IF
    END_IF
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* the reporter Toad: one line after each bout, escalating */
static EVT_BEGIN(reporter_talk)
    IF_INT_EQUAL(SL3F(F_RACE_WON), 1)
        M("sl_c3_npc_reporter_04", "sl_reporter")
        RETURN
    END_IF
    IF_INT_EQUAL(SL3F(F_BOUT4), 1)
        M("sl_c3_npc_reporter_03", "sl_reporter")
        RETURN
    END_IF
    IF_INT_EQUAL(SL3F(F_BOUT2), 1)
        M("sl_c3_npc_reporter_02", "sl_reporter")
        RETURN
    END_IF
    IF_INT_EQUAL(SL3F(F_BOUT1), 1)
        M("sl_c3_npc_reporter_01", "sl_reporter")
        RETURN
    END_IF
    M("sl_c3_npc_reporter_00", "sl_reporter")
    RETURN
EVT_END()

/* the lost-and-found chest: Checkers lets it go after Bout 1 */
static EVT_BEGIN(tutor_check)
    IF_INT_EQUAL(SL3F(F_BOUT1), 0)
        M("sl_c3_hq_tutor_no", "sl_checkers")
        SET_VALUE(LW(0), 0) /* not yet */
    ELSE
        SET_VALUE(LW(0), 1)
    END_IF
    RETURN
EVT_END()

BOXOPEN(box_tutor_open, "box_tutor", ITEM_TIMING_TUTOR)

static EVT_BEGIN(locked_pit)
    SYS("sl_c3_hq_locked")
    RETURN
EVT_END()

/* the Stadium's front doors: tou_03's lobby behind them is fenced off from the ring (the audience's), with
 * nothing to do in it and the camera on the shut doors */
static EVT_BEGIN(stadium_front_shut)
    SYS("sl_c3_hq_stadium_shut")
    RETURN
EVT_END()

static EVT_BEGIN(tou_l32_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG3_TOU1"))
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG3_TOU2"))
    BERO_RUN(b32)
    BERO_OFF("n_bero_1")
    BERO_OFF("n_bero_2")
    CALL(evt_run_case_evt, CASE_COND_FRONT_A, 1, STRING("n_bero_1"), 0, PTR(stadium_front_shut), 0)
    CALL(evt_run_case_evt, CASE_COND_FRONT_A, 1, STRING("n_bero_2"), 0, PTR(stadium_front_shut), 0)
    IF_INT_EQUAL(SL3F(F_SIGNED), 0)
        BERO_OFF("n_bero_3")
        BERO_OFF("n_bero_4")
        CALL(evt_run_case_evt, CASE_COND_FRONT_A, 1, STRING("n_bero_3"), 0, PTR(locked_pit), 0)
        CALL(evt_run_case_evt, CASE_COND_FRONT_A, 1, STRING("n_bero_4"), 0, PTR(locked_pit), 0)
    END_IF
    CALL(evt_map_playanim, STRING("anm_paneru"), 1, 0)
    NPC("sl_checkers", "c_kinopiko", STRING(SJ3_TOAD_F), -100, 0, -25, 90, PTR(checkers_talk))
    NPC("sl_reporter", "c_kinopio_g", STRING(SJ3_TOAD_G), 170, 0, 40, 270, PTR(reporter_talk))
    CALL(evt_mobj_recovery_blk, STRING("mobj_heart"), 0, -180, 60, -60, 0, 0)
    CALL(evt_mobj_itembox, STRING("box_tutor"), -170, 0, -110, 0, PTR(tutor_check), PTR(box_tutor_open), SL3F(F_32_TUTOR))
    RETURN
EVT_END()

/* ======================================================================
 * tou_l34, Pit Lane (tou_04)
 * ====================================================================== */

DOOR1(d34_w, "door_w", -0.44433594f)
DOOR1(d34_1, "door_1", -0.44433594f)
DOOR1(d34_2, "door_2", -0.44433594f)
DOOR1(d34_3, "door_3", -0.44433594f)
DOOR1(d34_4, "door_4", -0.44433594f)
DOOR1(d34_5, "door_5", -0.44433594f)
DOOR1(d34_6, "door_6", -0.44433594f)
DOOR1(d34_7, "door_7", -0.44433594f)
DOOR1(d34_e, "door_e", -0.44433594f)
DOOR2(d34_n1, "ao_a", "ao_b", 0.38867188f)
DOOR2(d34_n2, "aka_a", "aka_b", -0.38867188f)
static BeroEntry b34[] = {
    BDOORD("w_bero", 4, BERO_DIR_S, "tou_l32", "n_bero_4", d34_w),
    BDOORD("s_bero_1", 4, BERO_DIR_E, NULL, "n_bero", d34_1),          /* the old hidden bay: shut (Row B's lockers) */
    BDOORD("s_bero_2", 4, BERO_DIR_E, "tou_l40", "n_bero", d34_2),
    BDOORD("s_bero_5", 4, BERO_DIR_N, "tou_l35", "n_bero", d34_5),
    BDOORD("s_bero_6", 4, BERO_DIR_W, "tou_l38", "n_bero", d34_6),
    BDOORD("s_bero_7", 4, BERO_DIR_W, "tou_l39", "n_bero", d34_7),
    BDOORD("e_bero", 4, BERO_DIR_S, "tou_l32", "n_bero_3", d34_e),
    BDOORD("n_bero_1", 6, BERO_DIR_W, "tou_l33", "e_bero", d34_n1),
    BDOORD("n_bero_2", 6, BERO_DIR_E, "tou_l33", "w_bero", d34_n2),
    BDOORD("s_bero_3", 4, BERO_DIR_N, "tou_l37", "n_bero", d34_3),
    BDOORD("s_bero_4", 4, BERO_DIR_N, "tou_l36", "n_bero", d34_4),
    {0},
};

static EVT_BEGIN(suite_locked)
    IF_INT_EQUAL(SL3F(F_WHEEL), 0)
        SYS("sl_c3_suite_locked")
    END_IF
    RETURN
EVT_END()

static EVT_BEGIN(depot_jammed)
    IF_INT_EQUAL(SL3F(F_SHUTTER), 0)
        SYS("sl_c3_shutter_jammed")
    END_IF
    RETURN
EVT_END()

/* the shutter rolls up: the door works from now on */
static EVT_BEGIN(shutter_fixed)
    CALL(evt_snd_sfxon, STRING("SFX_STG7_SHUTTER_OPEN1"), 0)
    CALL(evt_npc_check, STRING("sl_shutter"), LW(1))
    IF_INT_EQUAL(LW(1), 1)
        CALL(evt_npc_set_anim, STRING("sl_shutter"), STRING("O_1"))
        WAIT_MS(750)
        CALL(evt_npc_delete, STRING("sl_shutter"))
    END_IF
    SET_VALUE(SL3F(F_SHUTTER), 1)
    IF_INT_EQUAL(SL3F(F_TUTORIAL_DONE), 0)
        SET_VALUE(SL3F(F_TUTORIAL_DONE), 1)
        TQ("sl_c3_tutorial_04")
        M("sl_c3_tutorial_05", "mario")
        TQ("sl_c3_tutorial_06")
    END_IF
    REOPEN("tou_l36", "n_bero")
    RETURN
EVT_END()

static EVT_BEGIN(crew_talk)
    IF_INT_EQUAL(SL3F(F_RACE_WON), 1)
        M("sl_c3_npc_crew_04", "sl_crew1")
        RETURN
    END_IF
    IF_INT_EQUAL(SL3F(F_KING), 1)
        M("sl_c3_npc_crew_03", "sl_crew1")
        RETURN
    END_IF
    IF_INT_EQUAL(SL3F(F_BOUT2), 1)
        M("sl_c3_npc_crew_02", "sl_crew1")
        RETURN
    END_IF
    M("sl_c3_npc_crew_01", "sl_crew1")
    RETURN
EVT_END()

static EVT_BEGIN(crew2_talk)
    IF_INT_EQUAL(SL3F(F_RACE_WON), 1)
        M("sl_c3_npc_crew_14", "sl_crew2")
        RETURN
    END_IF
    IF_INT_EQUAL(SL3F(F_KING), 1)
        M("sl_c3_npc_crew_13", "sl_crew2")
        RETURN
    END_IF
    IF_INT_EQUAL(SL3F(F_BOUT2), 1)
        M("sl_c3_npc_crew_12", "sl_crew2")
        RETURN
    END_IF
    M("sl_c3_npc_crew_11", "sl_crew2")
    RETURN
EVT_END()

/* beat 6's end and beat 7: Torque waits outside his garage; he joins; the shutter */
static EVT_BEGIN(torque_join_scene)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_seq_wait, 2)
    WAIT_MS(300)
    CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
    /* up the side corridor from Pit Lane (the Stadium's doors open at its north end) */
    SET_VALUE(LW(3), LW(2))
    SUBTRACT(LW(3), 240)
    SET_VALUE(LW(4), LW(2))
    SUBTRACT(LW(4), 45)
    NPC("sl_tq", "c_lp_met", STRING(SJ3_TORQUE), LW(0), LW(1), LW(3), 180, 0)
    CALL(evt_npc_set_anim, STRING("sl_tq"), STRING("W_1"))
    CALL(evt_sl_npc_fly, STRING("sl_tq"), LW(0), LW(1), LW(4), 1800)
    CALL(evt_npc_set_anim, STRING("sl_tq"), STRING("S_1"))
    CALL(evt_mario_set_dir_npc, STRING("sl_tq"))
    M("sl_c3_torque_join_01", "sl_tq")
    M("sl_c3_torque_join_02", "mario")
    M("sl_c3_torque_join_03", "sl_tq")
    M("sl_c3_torque_join_04", "sl_tq")
    M("sl_c3_torque_join_05", "sl_tq")
    M("sl_c3_torque_join_06", "mario")
    M("sl_c3_torque_join_07", "sl_tq")
    /* Torque joins */
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_FF_GET_PARTY1"))
    SYS("sl_c3_torque_joined")
    CALL(evt_npc_get_position, STRING("sl_tq"), LW(0), LW(1), LW(2))
    CALL(evt_npc_delete, STRING("sl_tq"))
    CALL(evt_sl_friend_join, PORT_SL_FRIEND_TORQUE)
    /* he takes the follower's place: with a partner out evt_mario_set_party_pos does nothing, and
     * Jerry would stay out, with no Tune-up on X at the shutter */
    CALL(evt_mario_get_party, LW(4))
    IF_INT_NOT_EQUAL(LW(4), 0)
        CALL(evt_mario_goodbye_party, 0)
    END_IF
    CALL(evt_sl_friend_slot, PORT_SL_FRIEND_TORQUE, LW(3))
    CALL(evt_mario_set_party_pos, 0, LW(3), LW(0), LW(1), LW(2))
    SET_VALUE(SL3F(F_TORQUE), 1)
    WAIT_MS(1200)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG3_TOU1"))
    /* beat 7: the shutter buzzes and sparks */
    CALL(evt_snd_sfxon, STRING("SFX_ENM_BOMB_SPARK1"), 0)
    CALL(evt_eff, 0, STRING("kemuri_test"), 0, 0, 30, -560, FLOAT(0.8f), 0, 0, 0, 0, 0, 0, 0)
    TQ("sl_c3_tutorial_01")
    SYS("sl_tq_tuneup_help")
    TQ("sl_c3_tutorial_03")
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* beat 10: after Bout 4, Lug and Nut swagger past */
static EVT_BEGIN(hint_scene)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_seq_wait, 2)
    WAIT_MS(300)
    /* up the side corridor from Pit Lane (Luigi came in from the Stadium) */
    CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
    SET_VALUE(LW(3), LW(2))
    SUBTRACT(LW(3), 300)
    SET_VALUE(LW(4), LW(0))
    ADD(LW(4), 22)
    SET_VALUE(LW(5), LW(3))
    SUBTRACT(LW(5), 40)
    NPC("sl_lug", "c_burosu_h", STRING(SJ3_HAMMER), LW(0), LW(1), LW(3), 180, 0)
    NPC("sl_nut", "c_burosu_b", STRING(SJ3_BOOMERANG), LW(4), LW(1), LW(5), 180, 0)
    SET_VALUE(LW(3), LW(2))
    SUBTRACT(LW(3), 60)
    SET_VALUE(LW(5), LW(3))
    SUBTRACT(LW(5), 40)
    BEGIN_INLINE_EVENT
        CALL(evt_npc_move_position, STRING("sl_nut"), LW(4), LW(5), 0, FLOAT(70.0f), 4)
    END_INLINE_EVENT
    CALL(evt_npc_move_position, STRING("sl_lug"), LW(0), LW(3), 0, FLOAT(70.0f), 4)
    WAIT_MS(300)
    CALL(evt_mario_set_dir_npc, STRING("sl_lug"))
    M("sl_c3_wheel_01", "sl_lug")
    M("sl_c3_wheel_02", "sl_nut")
    M("sl_c3_wheel_03", "sl_lug")
    M("sl_c3_wheel_04", "sl_nut")
    SET_VALUE(LW(3), LW(2))
    SUBTRACT(LW(3), 500)
    BEGIN_INLINE_EVENT
        CALL(evt_npc_move_position, STRING("sl_nut"), LW(4), LW(3), 0, FLOAT(90.0f), 4)
        CALL(evt_npc_delete, STRING("sl_nut"))
    END_INLINE_EVENT
    CALL(evt_npc_move_position, STRING("sl_lug"), LW(0), LW(3), 0, FLOAT(90.0f), 4)
    CALL(evt_npc_delete, STRING("sl_lug"))
    SET_VALUE(SL3F(F_HINT), 1)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(tou_l34_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG3_TOU1"))
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG3_TOU2"))
    BERO_RUN(b34)
    IF_INT_EQUAL(SL3F(F_WHEEL), 0)
        BERO_OFF("s_bero_3")
    END_IF
    IF_INT_EQUAL(SL3F(F_SHUTTER), 0)
        BERO_OFF("s_bero_4")
    END_IF
    CALL(evt_map_playanim, STRING("anm_mizu"), 1, 0)
    CALL(evt_run_case_evt, CASE_COND_FRONT_A, 1, STRING("s_bero_3"), 0, PTR(suite_locked), 0)
    CALL(evt_run_case_evt, CASE_COND_FRONT_A, 1, STRING("s_bero_4"), 0, PTR(depot_jammed), 0)
    /* the jammed shutter, half down over the depot's door */
    IF_INT_EQUAL(SL3F(F_SHUTTER), 0)
        NPC("sl_shutter", "c_sl_shutter", 0, 0, 0, -536, 180, 0)
        CALL(evt_npc_set_scale, STRING("sl_shutter"), FLOAT(1.25f), FLOAT(1.3f), FLOAT(1.0f))
        CALL(evt_sl_point, PORT_SL_ABILITY_TUNE, 0, 0, 0, -570, 90, 0, PTR(shutter_fixed))
    END_IF
    /* crews outside the garages */
    NPC("sl_crew1", "c_bomhey", STRING(SJ3_BOMB), -150, 0, -575, 90, PTR(crew_talk))
    NPC("sl_crew2", "c_nokonoko", STRING(SJ3_KOOPA), 180, 0, -575, 270, PTR(crew2_talk))
    IF_INT_EQUAL(SL3F(F_BOUT2), 1)
        IF_INT_EQUAL(SL3F(F_TORQUE), 0)
            SPAWN_EVENT(torque_join_scene)
            RETURN
        END_IF
    END_IF
    IF_INT_EQUAL(SL3F(F_BOUT4), 1)
        IF_INT_EQUAL(SL3F(F_HINT), 0)
            SPAWN_EVENT(hint_scene)
        END_IF
    END_IF
    RETURN
EVT_END()

/* ======================================================================
 * tou_l35, the Race Director's Office (tou_05)
 * ====================================================================== */

DOOR1(d35_n, "door_1", 0.44433594f)
static BeroEntry b35[] = {
    BDOOR("n_bero", 4, "tou_l34", "s_bero_5", d35_n),
    {"w_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "tou_l43", "w_bero_", 0, 0, 0, 0},
    {0},
};

/* Gasket's bout desk: the next bout, by the story */
static USER_FUNC(sl3_next_bout) {
    int n = 0;
    (void)isFirstCall;
    if (!swGet(SL_FLAG(3, F_TORQUE_MET))) {
        n = -1; /* find a mechanic first */
    } else if (!swGet(SL_FLAG(3, F_BOUT1))) {
        n = 1;
    } else if (!swGet(SL_FLAG(3, F_BOUT2))) {
        n = 2;
    } else if (!swGet(SL_FLAG(3, F_TORQUE))) {
        n = -2; /* Torque's waiting in Pit Lane */
    } else if (!swGet(SL_FLAG(3, F_BOUT3))) {
        n = 3;
    } else if (!swGet(SL_FLAG(3, F_BOUT4))) {
        n = 4;
    } else if (!swGet(SL_FLAG(3, F_KART))) {
        n = -3; /* no kart, no King's Bout */
    } else if (!swGet(SL_FLAG(3, F_KING))) {
        n = 5;
    } else if (!swGet(SL_FLAG(3, F_EX1))) {
        n = 6;
    } else if (!swGet(SL_FLAG(3, F_EX2))) {
        n = 7;
    } else if (!swGet(SL_FLAG(3, F_EX3))) {
        n = 8;
    } else {
        n = -4;
    }
    evtSetValue(event, event->args[0], n);
    return EVT_RETURN_DONE;
}

static EVT_BEGIN(gasket_talk)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_STG3_GANCE1"))
    IF_INT_EQUAL(SL3F(F_GASKET), 0)
        M("sl_c3_director_01", "sl_gasket")
        M("sl_c3_director_02", "sl_gasket")
        M("sl_c3_director_03", "mario")
        M("sl_c3_director_04", "sl_gasket")
        M("sl_c3_director_05", "sl_gasket")
        SET_VALUE(SL3F(F_GASKET), 1)
        CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG3_TOU1"))
        CALL(evt_mario_key_onoff, 1)
        RETURN
    END_IF
    CALL(sl3_next_bout, LW(0))
    SWITCH(LW(0))
        CASE_EQUAL(-1)
            M("sl_c3_director_mech", "sl_gasket")
        CASE_EQUAL(-2)
            M("sl_c3_director_torque", "sl_gasket")
        CASE_EQUAL(-3)
            M("sl_c3_director_kart", "sl_gasket")
        CASE_EQUAL(-4)
            M("sl_c3_director_done", "sl_gasket")
        CASE_DEFAULT
            SET_VALUE(LW(5), LW(0))
            IF_INT_GREATER_OR_EQUAL(LW(5), 6)
                IF_INT_EQUAL(LW(5), 6)
                    M("sl_c3_director_exhib", "sl_gasket")
                END_IF
            END_IF
            IF_INT_EQUAL(LW(5), 1)
                M("sl_c3_director_bout1", "sl_gasket")
            END_IF
            IF_INT_EQUAL(LW(5), 5)
                M("sl_c3_director_king", "sl_gasket")
            END_IF
            CALL(evt_msg_print, 0, STRING("sl_c3_director_ask"), 0, STRING("sl_gasket"))
            CALL(evt_msg_select, 0, STRING("sl_c3_director_yesno"))
            CALL(evt_msg_continue)
            IF_INT_EQUAL(LW(0), 0)
                CALL(sl3_bout_set, LW(5))
                CALL(evt_msg_print_add, 0, STRING("sl_c3_director_go"))
                CALL(evt_snd_bgmoff, PSND_FADE_OUT)
                CALL(evt_bero_mapchange, STRING("tou_l33"), STRING("s_bero_1"))
                RETURN
            END_IF
            CALL(evt_msg_print_add, 0, STRING("sl_c3_director_later"))
    END_SWITCH
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG3_TOU1"))
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* the desk's drawer (tou_05 has no cabinet hit: "a_tana" was never there, so this couldn't be checked) */
static EVT_BEGIN(cabinet_check)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_snd_sfxon, STRING("SFX_DOOR_OPEN_METAL1"), 0)
    CALL(evt_mapobj_trans, 1, STRING("hikidasi_N"), 0, 0, 10)
    WAIT_MS(300)
    IF_INT_EQUAL(SL3F(F_35_MUSH), 0)
        SET_VALUE(SL3F(F_35_MUSH), 1)
        /* handed over, as tou_05's own drawer does: an item dropped by Luigi waits for his touch, and his key is off */
        CALL(evt_item_entry, STRING("cab_item"), ITEM_MUSHROOM, 0, -1000, 0, ITEM_ENTRY_FIXED, -1, 0)
        CALL(evt_item_get_item, STRING("cab_item"))
    ELSE
        SYS("sl_c3_cabinet_empty")
    END_IF
    WAIT_MS(300)
    CALL(evt_mapobj_trans, 1, STRING("hikidasi_N"), 0, 0, 0)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(vent_fixed)
    CALL(evt_snd_sfxon, STRING("SFX_DOOR_OPEN_METAL1"), 0)
    CALL(evt_npc_check, STRING("sl_grille"), LW(1))
    IF_INT_EQUAL(LW(1), 1)
        CALL(evt_npc_delete, STRING("sl_grille"))
    END_IF
    SET_VALUE(SL3F(F_VENT), 1)
    TQ("sl_c3_vent_open")
    REOPEN("tou_l43", "w_bero_")
    RETURN
EVT_END()

static EVT_BEGIN(tou_l35_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG3_TOU1"))
    BERO_RUN(b35)
    IF_INT_EQUAL(SL3F(F_VENT), 0)
        BERO_OFF("w_bero")
    END_IF
    CALL(evt_map_playanim, STRING("anm_camera"), 1, 0)
    /* the office lit: its blackout cards away */
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("kuro_Y"), MAPOBJ_FLAG_HIDE)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("kuro_P"), MAPOBJ_FLAG_HIDE)
    NPC("sl_gasket", "c_borodo", STRING(SJ3_CLUBBA), 15, 0, -55, 270, PTR(gasket_talk)) /* at the end of his desk, not hidden behind it */
    CALL(evt_run_case_evt, CASE_COND_FRONT_A, 1, STRING("a_hikidasi_N"), 0, PTR(cabinet_check), 0)
    IF_INT_EQUAL(SL3F(F_VENT), 0)
        CALL(evt_sl_point, PORT_SL_ABILITY_TUNE, 0, -140, 0, -80, 90, 0, PTR(vent_fixed)) /* under the vent (-155, 85, -55) */
    END_IF
    RETURN
EVT_END()

/* ======================================================================
 * tou_l36, the Parts Depot (tou_06): the freight lift, the dented container
 * ====================================================================== */

DOOR1(d36_n, "door_1", 0.44433594f)
DOOR1(d36_w, "door_2", 0.44433594f)
static BeroEntry b36[] = {
    BDOOR("n_bero", 4, "tou_l34", "s_bero_4", d36_n),
    BDOOR("w_bero", 4, "tou_l42", "w_bero", d36_w),
    {0},
};

static EVT_BEGIN(plug_get)
    CALL(evt_mario_key_onoff, 0)
    KEY("sl_c3_plug_got")
    SET_VALUE(SL3F(F_PLUG), 1)
    TQ("sl_c3_plug_01")
    /* and the hoist lets him down: the stack's top is walled in (a player stood on it for good) */
    CALL(evt_snd_sfxon, STRING("SFX_STG6_ELEVATOR_MOVE1"), 0)
    CALL(evt_mario_jump_pos, 35, 0, -20, 900, 0, 0)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* the Spark Plug glints up on the right-hand stack till it's taken */
static EVT_BEGIN(plug_glint)
    LOOP(0)
        IF_INT_EQUAL(SL3F(F_PLUG), 1)
            BREAK_LOOP
        END_IF
        CALL(evt_eff, 0, STRING("stardust"), 0, 168, 62, -105, 0, 0, 0, 0, 0, 0, 0, 0)
        WAIT_MS(1600)
    END_LOOP
    RETURN
EVT_END()

/* the lift: the hoist in front of the right-hand containers puts Luigi on top of them, with a hum */
static EVT_BEGIN(lift_ride)
    LOOP(0)
        /* with the plug got there's nothing up there: no more rides */
        IF_INT_EQUAL(SL3F(F_PLUG), 1)
            BREAK_LOOP
        END_IF
        CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
        IF_INT_GREATER(LW(0), 55)
            IF_INT_LESS(LW(0), 100)
                IF_INT_LESS(LW(1), 10)
                    IF_INT_GREATER(LW(2), -70)
                        IF_INT_LESS(LW(2), 10)
                            CALL(evt_mario_key_onoff, 0)
                            CALL(evt_snd_sfxon, STRING("SFX_STG6_ELEVATOR_MOVE1"), 0)
                            CALL(evt_mario_jump_pos, 165, 50, -100, 900, 0, 0)
                            CALL(evt_mario_key_onoff, 1)
                            /* the Spark Plug, in the open crate on top */
                            IF_INT_EQUAL(SL3F(F_PLUG), 0)
                                CALL_CHILD(plug_get)
                            END_IF
                        END_IF
                    END_IF
                END_IF
            END_IF
        END_IF
        WAIT_FRAMES(6)
    END_LOOP
    RETURN
EVT_END()

static EVT_BEGIN(lift_fixed)
    CALL(evt_snd_sfxon, STRING("SFX_STG7_ELEVATOR_ACCESS1"), 0)
    SET_VALUE(SL3F(F_LIFT), 1)
    SPAWN_EVENT(lift_ride)
    TQ("sl_c3_lift_fixed")
    RETURN
EVT_END()

static EVT_BEGIN(container_blown)
    CALL(evt_snd_sfxon, STRING("SFX_DOOR_OPEN_METAL1"), 0)
    SET_VALUE(SL3F(F_36_BLOWN), 1)
    CALL(evt_item_entry, STRING("shine36"), ITEM_SHINE_SPRITE, -100, 0, 30, ITEM_ENTRY_FIXED, SL3F(F_36_SHINE), 0)
    RETURN
EVT_END()

static EVT_BEGIN(tou_l36_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG3_TOU1"))
    BERO_RUN(b36)
    CALL(evt_map_playanim, STRING("anm_hae"), 1, 0)
    /* tou_06 is the ground floor (x -190..190, z -140..60) with containers stacked 50 high at
     * both ends, and the stairs on the left up to the loft's door: the Spark Plug's chest is
     * on the right-hand stack, reached by the hoist in front of it once Torque fixes it */
    CALL(evt_mobj_badgeblk, STRING("mobj_shroom"), 30, 60, -40, ITEM_SUPER_SHROOM, 0, SL3F(F_36_SHROOM), 0) /* its prize used to land on the container's top */
    IF_INT_EQUAL(SL3F(F_PLUG), 0)
        SPAWN_EVENT(plug_glint)
    END_IF
    IF_INT_EQUAL(SL3F(F_LIFT), 0)
        CALL(evt_sl_point, PORT_SL_ABILITY_TUNE, 0, 78, 0, -30, 60, 0, PTR(lift_fixed))
    ELSE
        SPAWN_EVENT(lift_ride)
    END_IF
    IF_INT_EQUAL(SL3F(F_36_BLOWN), 0)
        CALL(evt_sl_point, PORT_SL_ABILITY_BLAST, 0, -100, 0, 0, 70, 0, PTR(container_blown))
    ELSE
        CALL(evt_item_entry, STRING("shine36"), ITEM_SHINE_SPRITE, -100, 0, 30, ITEM_ENTRY_FIXED, SL3F(F_36_SHINE), 0)
    END_IF
    RETURN
EVT_END()

/* ======================================================================
 * tou_l37, the King's Suite (tou_07): the Steering Wheel
 * ====================================================================== */

DOOR1(d37_n, "door_1", 0.44433594f)
static BeroEntry b37[] = {
    {"e_bero", 0, 1, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "tou_l43", "e_bero_", 0, 0, 0, 0},
    BDOOR("n_bero", 4, "tou_l34", "s_bero_3", d37_n),
    {0},
};


static EVT_BEGIN(wheel_scene)
    CALL(evt_mario_key_onoff, 0)
    KEY("sl_c3_wheel_got")
    SET_VALUE(SL3F(F_WHEEL), 1)
    CALL(evt_npc_check, STRING("sl_wheelnpc"), LW(0))
    IF_INT_EQUAL(LW(0), 1)
        CALL(evt_npc_delete, STRING("sl_wheelnpc"))
        CALL(evt_npc_delete, STRING("sl_wheelcrown"))
    END_IF
    /* the door opens: Revvo, in a bathrobe */
    CALL(evt_snd_sfxon, STRING("SFX_DOOR_OPEN_METAL1"), 0)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_3RD_ENEMY"))
    NPC("sl_revvo", "c_togenoko_b", STRING(SJ3_KOOPATROL), 0, 0, -155, 0, 0)
    CALL(evt_npc_move_position, STRING("sl_revvo"), 0, -90, 0, FLOAT(60.0f), 4)
    CALL(evt_mario_set_dir_npc, STRING("sl_revvo"))
    M("sl_c3_wheel_05", "sl_revvo")
    M("sl_c3_wheel_06", "mario")
    M("sl_c3_wheel_07", "sl_revvo")
    M("sl_c3_wheel_08", "sl_revvo")
    M("sl_c3_wheel_09", "sl_revvo")
    M("sl_c3_wheel_10", "sl_revvo")
    CALL(evt_npc_move_position, STRING("sl_revvo"), 0, -170, 0, FLOAT(80.0f), 4)
    CALL(evt_npc_delete, STRING("sl_revvo"))
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG3_TOU1"))
    REOPEN("tou_l34", "s_bero_3")
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(wheel_take)
    CALL_CHILD(wheel_scene)
    RETURN
EVT_END()

BOXOPEN(box_fp_open, "box_fp", ITEM_FP_PLUS)

static EVT_BEGIN(tou_l37_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG3_TOU1"))
    BERO_RUN(b37)
    IF_INT_EQUAL(SL3F(F_WHEEL), 0)
        BERO_OFF("n_bero")
    END_IF
    CALL(evt_map_playanim, STRING("anm_futa"), 1, 0)
    CALL(evt_mobj_itembox, STRING("box_fp"), 85, 0, -60, 0, 0, PTR(box_fp_open), SL3F(F_37_FPPLUS)) /* not right in front of the door (0, -150) */
    CALL(evt_mobj_brick, STRING("mobj_hidden"), 60, 60, -20, ITEM_STAR_PIECE, 11, 0, SL3F(F_37_PIECE))
    IF_INT_EQUAL(SL3F(F_WHEEL), 0)
        /* the hat stand: the wheel with Revvo's crown on it */
        NPC("sl_wheelnpc", "c_sl_wheel", STRING(SJ3_TOAD), -75, 0, 55, 0, PTR(wheel_take))
        CALL(evt_npc_set_scale, STRING("sl_wheelnpc"), FLOAT(2.0f), FLOAT(2.0f), FLOAT(2.0f))
        NPC("sl_wheelcrown", "c_sl_crown", 0, -75, 11, 56, 0, 0)
    END_IF
    RETURN
EVT_END()

/* ======================================================================
 * tou_l38, Team Revvo's Garage (tou_08)
 * ====================================================================== */

DOOR1(d38_n, "door_1", 0.44433594f)
static BeroEntry b38[] = {
    BDOOR("n_bero", 4, "tou_l34", "s_bero_6", d38_n),
    {0},
};

static EVT_BEGIN(driver_talk)
    IF_INT_EQUAL(SL3F(F_RACE_WON), 1)
        M("sl_c3_npc_driver_02", "sl_drv1")
    ELSE
        M("sl_c3_npc_driver_01", "sl_drv1")
    END_IF
    RETURN
EVT_END()
static EVT_BEGIN(driver2_talk)
    IF_INT_EQUAL(SL3F(F_RACE_WON), 1)
        M("sl_c3_npc_driver_12", "sl_drv2")
    ELSE
        M("sl_c3_npc_driver_11", "sl_drv2")
    END_IF
    RETURN
EVT_END()
static EVT_BEGIN(driver3_talk)
    IF_INT_EQUAL(SL3F(F_RACE_WON), 1)
        M("sl_c3_npc_driver_22", "sl_drv3")
    ELSE
        M("sl_c3_npc_driver_21", "sl_drv3")
    END_IF
    RETURN
EVT_END()

BOXOPEN(box_spike_open, "box_spike", ITEM_SPIKE_SHIELD)

/* the locker jolts open when Luigi Spin Jumps on the loose panel in front of it */
static EVT_BEGIN(locker_watch)
    LOOP(0)
        CALL(sl3_spin_landed, -130, -100, 40, LW(0))
        IF_INT_EQUAL(LW(0), 1)
            BREAK_LOOP
        END_IF
        WAIT_FRAMES(1)
    END_LOOP
    CALL(evt_snd_sfxon, STRING("SFX_DOOR_OPEN_METAL1"), 0)
    CALL(evt_cam_shake, 4, FLOAT(0.0f), FLOAT(2.0f), 300)
    SET_VALUE(SL3F(F_38_LOCKER), 1)
    CALL(evt_mobj_itembox, STRING("box_spike"), -175, 0, -95, 0, 0, PTR(box_spike_open), SL3F(F_38_SPIKE)) /* z -150 was inside the lockers' wall */
    RETURN
EVT_END()

static EVT_BEGIN(tou_l38_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG3_TOU2"))
    BERO_RUN(b38)
    CALL(evt_map_playanim, STRING("anm_futa"), 1, 0)
    CALL(evt_map_playanim, STRING("anm_mizu"), 1, 0)
    NPC("sl_kart1", "c_lp_kart_red", 0, 60, 0, -60, 0, 0)
    NPC("sl_kart2", "c_lp_kart_red", 0, 140, 0, 20, 0, 0)
    NPC("sl_drv1", "c_nokonoko", STRING(SJ3_KOOPA), 90, 0, -100, 270, PTR(driver_talk))
    NPC("sl_drv2", "c_bomhey", STRING(SJ3_BOMB), 200, 0, 0, 270, PTR(driver2_talk))
    NPC("sl_drv3", "c_uranoko", STRING(SJ3_SHADY), 20, 0, 40, 90, PTR(driver3_talk))
    IF_INT_EQUAL(SL3F(F_38_LOCKER), 0)
        SPAWN_EVENT(locker_watch)
    ELSE
        CALL(evt_mobj_itembox, STRING("box_spike"), -175, 0, -95, 0, 0, PTR(box_spike_open), SL3F(F_38_SPIKE))
    END_IF
    RETURN
EVT_END()

/* ======================================================================
 * tou_l39, Torque's Garage (tou_10)
 * ====================================================================== */

DOOR1(d39_n, "door_1", 0.44433594f)
static BeroEntry b39[] = {
    BDOOR("n_bero", 4, "tou_l34", "s_bero_7", d39_n),
    {0},
};

/* beat 5: Torque, under his bench */
static EVT_BEGIN(torque_meet)
    CALL(evt_mario_key_onoff, 0)
    CALL_CHILD(crew_in)
    /* crew_in stands Blooey a step nearer the camera, which here is just behind the end of the
     * red bench (x -140 to 60, z -18 to 60): he looked to float on it. Both further back. */
    CALL(evt_mario_get_pos, 0, LW(10), LW(11), LW(12))
    CALL(evt_npc_check, STRING("sl_bl"), LW(13))
    IF_INT_EQUAL(LW(13), 1)
        SET_VALUE(LW(14), LW(10))
        SUBTRACT(LW(14), 75)
        SET_VALUE(LW(15), LW(12))
        SUBTRACT(LW(15), 40)
        CALL(evt_npc_set_position, STRING("sl_bl"), LW(14), LW(11), LW(15))
    END_IF
    CALL(evt_npc_check, STRING("sl_je"), LW(13))
    IF_INT_EQUAL(LW(13), 1)
        SET_VALUE(LW(14), LW(10))
        SUBTRACT(LW(14), 45)
        CALL(evt_npc_set_position, STRING("sl_je"), LW(14), LW(11), LW(12))
    END_IF
    IF_INT_EQUAL(SL3F(F_TORQUE_MET), 0)
        M("sl_c3_torque_meet_01", "sl_tq")
        M("sl_c3_torque_meet_02", "mario")
        M("sl_c3_torque_meet_03", "sl_tq")
        M("sl_c3_torque_meet_04", "mario")
        /* out on the creeper */
        CALL(evt_npc_set_anim, STRING("sl_tq"), STRING("W_1"))
        CALL(evt_sl_npc_fly, STRING("sl_tq"), 40, 0, -30, 1500)
        CALL(evt_npc_set_anim, STRING("sl_tq"), STRING("S_1"))
        CALL(evt_mario_set_dir_npc, STRING("sl_tq"))
        M("sl_c3_torque_meet_05", "sl_tq")
        M("sl_c3_torque_meet_06", "mario")
        CALL(evt_npc_set_ry, STRING("sl_tq"), 90)
        WAIT_MS(1000)
        M("sl_c3_torque_meet_07", "sl_tq")
        SAY(PORT_SL_FRIEND_JERRY, "sl_c3_torque_meet_08")
        CALL(evt_npc_set_ry, STRING("sl_tq"), 270)
        M("sl_c3_torque_meet_09", "sl_tq")
        M("sl_c3_torque_meet_10", "mario")
        M("sl_c3_torque_meet_11", "sl_tq")
        SAY(PORT_SL_FRIEND_BLOOEY, "sl_c3_torque_meet_12")
        M("sl_c3_torque_meet_13", "sl_tq")
        M("sl_c3_torque_meet_14", "mario")
        M("sl_c3_torque_meet_15", "sl_tq")
        CALL(evt_npc_set_anim, STRING("sl_tq"), STRING("W_1"))
        CALL(evt_sl_npc_fly, STRING("sl_tq"), 100, 0, -80, 1500)
        CALL(evt_npc_set_anim, STRING("sl_tq"), STRING("F_1"))
        SET_VALUE(SL3F(F_TORQUE_MET), 1)
    ELSE
        M("sl_c3_npc_torque_under", "sl_tq")
    END_IF
    CALL_CHILD(crew_out)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* beat 11: with the three parts, Torque builds Big Green 01 */
static EVT_BEGIN(kart_scene)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_seq_wait, 2)
    WAIT_MS(300)
    TQ("sl_c3_kart_00")
    CALL(evt_fade_out, 500)
    WAIT_MS(500)
    LOOP(4)
        CALL(evt_snd_sfxon, STRING("SFX_STG7_CRANE_BUTTON_PUSH1"), 0)
        WAIT_MS(250)
        CALL(evt_snd_sfxon, STRING("SFX_PEACH_MACHINE_SWITCH1"), 0)
        WAIT_MS(250)
    END_LOOP
    CALL(evt_fade_in, 500)
    WAIT_MS(600)
    /* the reveal: the tarp whisked off, the engine catches */
    CALL(evt_npc_check, STRING("sl_tarp"), LW(0))
    IF_INT_EQUAL(LW(0), 1)
        CALL(evt_npc_set_anim, STRING("sl_tarp"), STRING("K_1"))
        CALL(evt_snd_sfxon, STRING("SFX_STAR_POINT_GET1"), 0)
        WAIT_MS(500)
        CALL(evt_npc_delete, STRING("sl_tarp"))
    END_IF
    CALL(evt_npc_set_anim, STRING("sl_kart"), STRING("S_2"))
    WAIT_MS(600)
    TQ("sl_c3_kart_01")
    M("sl_c3_kart_02", "mario")
    TQ("sl_c3_kart_03")
    TQ("sl_c3_kart_04")
    CALL(evt_npc_set_anim, STRING("sl_kart"), STRING("S_1"))
    SET_VALUE(SL3F(F_KART), 1)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* beat 13: the night before */
static EVT_BEGIN(night_scene)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_FF_GO_SLEEPING1"))
    CALL(evt_map_set_blend, 0, 90, 90, 150, 255)
    CALL(evt_mario_set_pos, -60, 0, 40)
    CALL(evt_mario_set_dir, 90, 0, 0)
    CALL(evt_party_set_pos, 0, 0, -1000, 0)
    NPC("sl_tq", "c_lp_met", STRING(SJ3_TORQUE), 60, 0, 30, 90, 0)
    CALL(evt_npc_set_anim, STRING("sl_tq"), STRING("F_1"))
    NPC("sl_bl", "c_lp_gesso", STRING(SJ3_BLOOEY), -150, 0, -60, 90, 0)
    CALL(evt_npc_set_anim, STRING("sl_bl"), STRING("N_1"))
    NPC("sl_je", "c_lp_bom", STRING(SJ3_JERRY), -130, 0, 70, 90, 0)
    CALL(evt_seq_wait, 2)
    WAIT_MS(800)
    M("sl_c3_night_01", "mario")
    CALL(evt_npc_set_anim, STRING("sl_tq"), STRING("S_1"))
    M("sl_c3_night_02", "sl_tq")
    M("sl_c3_night_03", "sl_tq")
    M("sl_c3_night_04", "mario")
    M("sl_c3_night_05", "sl_tq")
    CALL(evt_npc_set_anim, STRING("sl_tq"), STRING("F_1"))
    CALL(evt_npc_set_ry, STRING("sl_tq"), 90)
    M("sl_c3_night_06", "sl_tq")
    CALL(evt_snd_bgmoff, PSND_FADE_OUT)
    CALL(evt_fade_out, 1500)
    WAIT_MS(1600)
    SET_VALUE(SL3F(F_NIGHT), 1)
    CALL(evt_npc_delete, STRING("sl_tq"))
    CALL(evt_npc_delete, STRING("sl_bl"))
    CALL(evt_npc_delete, STRING("sl_je"))
    CALL(evt_map_set_blend, 0, 255, 255, 255, 255)
    CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
    CALL(evt_party_set_pos, 0, LW(0), LW(1), LW(2))
    SYS("sl_c3_night_morning")
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG3_TOU2"))
    CALL(evt_fade_in, 1000)
    TQ("sl_c3_night_07")
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(bench_fixed)
    SET_VALUE(SL3F(F_BENCH), 1)
    TQ("sl_c3_bench")
    /* after his line (picked up during it, the item's own window met his); in front of the bench
     * (17 was ITEM_ENTRY_FIXED_NO_TOUCH: it could never be picked up) */
    CALL(evt_item_entry, STRING("rage"), ITEM_THUNDER_RAGE, 70, 0, -40, ITEM_ENTRY_FIXED, SL3F(F_BENCH_RAGE), 0)
    RETURN
EVT_END()

static EVT_BEGIN(kart_talk)
    IF_INT_EQUAL(SL3F(F_KART), 1)
        SYS("sl_c3_kart_look")
    ELSE
        SYS("sl_c3_tarp_look")
    END_IF
    RETURN
EVT_END()

static EVT_BEGIN(parts_check)
    /* back from the parts hunt with all three: the build */
    IF_INT_EQUAL(SL3F(F_KART), 0)
        IF_INT_EQUAL(SL3F(F_PLUG), 1)
            IF_INT_EQUAL(SL3F(F_GEARBOX), 1)
                IF_INT_EQUAL(SL3F(F_WHEEL), 1)
                    CALL_CHILD(kart_scene)
                END_IF
            END_IF
        END_IF
    END_IF
    IF_INT_EQUAL(SL3F(F_KING), 1)
        IF_INT_EQUAL(SL3F(F_NIGHT), 0)
            CALL_CHILD(night_scene)
        END_IF
    END_IF
    RETURN
EVT_END()

static EVT_BEGIN(tou_l39_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG3_TOU2"))
    BERO_RUN(b39)
    CALL(evt_mobj_save_blk, STRING("mobj_save"), -60, 60, -150, 0, 0)
    NPC("sl_kart", "c_lp_kart", 0, -45, 0, 150, 0, PTR(kart_talk)) /* on the open floor in front of the bench */
    CALL(evt_npc_set_tribe, STRING("sl_kart"), STRING(SJ3_TOAD))
    CALL(evt_npc_set_anim, STRING("sl_kart"), STRING("S_1"))
    IF_INT_EQUAL(SL3F(F_KART), 0)
        /* the green tarp over the Girl: a sheet that hides her (her wheels show) */
        NPC("sl_tarp", "c_sl_tarp", 0, -45, 0, 154, 0, 0)
    END_IF
    IF_INT_EQUAL(SL3F(F_TORQUE), 0)
        IF_INT_EQUAL(SL3F(F_TORQUE_MET), 0)
            NPC("sl_tq", "c_lp_met", STRING(SJ3_TORQUE), 100, 0, -80, 270, PTR(torque_meet))
            CALL(evt_npc_set_anim, STRING("sl_tq"), STRING("F_1"))
        ELSE
            NPC("sl_tq", "c_lp_met", STRING(SJ3_TORQUE), 100, 0, -80, 270, PTR(torque_meet))
            CALL(evt_npc_set_anim, STRING("sl_tq"), STRING("F_1"))
        END_IF
    ELSE
        IF_INT_EQUAL(SL3F(F_BENCH), 0)
            CALL(evt_sl_point, PORT_SL_ABILITY_TUNE, 0, 100, 0, -80, 70, 0, PTR(bench_fixed))
        ELSE
            IF_INT_EQUAL(SL3F(F_BENCH_RAGE), 0)
                CALL(evt_item_entry, STRING("rage"), ITEM_THUNDER_RAGE, 70, 0, -40, ITEM_ENTRY_FIXED, SL3F(F_BENCH_RAGE), 0)
            END_IF
        END_IF
        SPAWN_EVENT(parts_check)
    END_IF
    RETURN
EVT_END()

/* ======================================================================
 * tou_l40, Garage Row B (tou_09), and tou_l41, the Hidden Bay (tou_11)
 * ====================================================================== */

DOOR1(d40_n, "door_1", 0.44433594f)
DOOR1(d40_w, "R_door_9", -0.44433594f)
static BeroEntry b40[] = {
    BDOOR("n_bero", 4, "tou_l34", "s_bero_2", d40_n),
    {"dokan", 2, 0, BERO_DIR_DOWN, BERO_POS_AUTO, 0, 0, 100, NULL, -1, NULL, NULL, NULL, 1, 1, 0, 0},
    {"a_door_9_omote", 1, 16, BERO_DIR_W, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "tou_l41", "e_bero", 5, 5, d40_w, d40_w},
    {0},
};


static EVT_BEGIN(blastard_talk)
    IF_INT_EQUAL(SL3F(F_BLASTARD), 0)
        M("sl_c3_npc_blastard_01", "sl_blastard")
        SAY(PORT_SL_FRIEND_JERRY, "sl_c3_npc_blastard_02")
        SET_VALUE(SL3F(F_BLASTARD), 1)
    ELSE
        IF_INT_EQUAL(SL3F(F_RACE_WON), 1)
            M("sl_c3_npc_blastard_04", "sl_blastard")
        ELSE
            M("sl_c3_npc_blastard_03", "sl_blastard")
        END_IF
    END_IF
    RETURN
EVT_END()

static EVT_BEGIN(lockers_blown)
    SET_VALUE(SL3F(F_40_BLOWN), 1)
    REOPEN("tou_l41", "e_bero")
    CALL(evt_map_playanim, STRING("anm_futa"), 1, 0)
    CALL(evt_map_playanim, STRING("anm_mizu"), 1, 0)
    CALL(evt_snd_sfxon, STRING("SFX_WALL_BREAK1"), 0)
    RETURN
EVT_END()

static EVT_BEGIN(tou_l40_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG3_TOU2"))
    BERO_RUN(b40)
    IF_INT_EQUAL(SL3F(F_40_BLOWN), 0)
        BERO_OFF("a_door_9_omote")
    END_IF
    CALL(evt_map_playanim, STRING("anm_futa"), 1, 0)
    CALL(evt_map_playanim, STRING("anm_mizu"), 1, 0)
    NPC("sl_blastard", "c_bomhey", STRING(SJ3_BOMB), 60, 0, -60, 270, PTR(blastard_talk))
    NPC("sl_blastard2", "c_bomhey", STRING(SJ3_BOMB), 110, 0, 20, 270, PTR(blastard_talk))
    CALL(evt_mobj_badgeblk, STRING("mobj_c1"), -100, 60, 60, ITEM_COIN, 0, SL3F(F_40_COIN1), 0)
    CALL(evt_mobj_badgeblk, STRING("mobj_c2"), 170, 60, 60, ITEM_COIN, 0, SL3F(F_40_COIN2), 0) /* both clear of the bench (x -50 to 130, z 40 to 80): over it they were out of reach */
    IF_INT_EQUAL(SL3F(F_40_BLOWN), 0)
        CALL(evt_sl_point, PORT_SL_ABILITY_BLAST, 0, -235, 0, 135, 90, 0, PTR(lockers_blown))
    END_IF
    RETURN
EVT_END()

DOOR1(d41_n, "door_1", 0.44433594f)
DOOR1(d41_e, "door_2", 0.44433594f)
static BeroEntry b41[] = {
    BDOOR("n_bero", 4, NULL, "s_bero_1", d41_n),
    {"e_bero", 1, 16, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "tou_l40", "a_door_9_omote", 5, 5, d41_e, d41_e},
    {0},
};

static EVT_BEGIN(bay_first)
    IF_INT_EQUAL(SL3F(F_41_SEEN), 0)
        SET_VALUE(SL3F(F_41_SEEN), 1)
        CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_FF_ENV_SUCCESS1"))
        WAIT_MS(2500)
        CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG3_TOU2"))
    END_IF
    RETURN
EVT_END()

static EVT_BEGIN(muffler_open)
    CALL(evt_mario_key_onoff, 0)
    KEY("sl_c3_muffler_got")
    SET_VALUE(SL3F(F_MUFFLER), 1)
    TQ("sl_c3_muffler")
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(tou_l41_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG3_TOU2"))
    BERO_RUN(b41)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("kuro_1"), MAPOBJ_FLAG_HIDE)
    /* the room behind the lockers is tou_11's east closet (x 290-439, z 39-139, the walked floor):
     * the rest of the map can't be reached from its door, so everything is in here */
    NPC("sl_frame", "c_lp_kart_wreck", 0, 330, 0, 122, 0, 0)
    CALL(evt_npc_flag_onoff, 1, STRING("sl_frame"), NPC_FLAG_NO_MARIO_SIDE_HIT) /* it filled the closet's front: Luigi couldn't get round it */
    CALL(evt_npc_set_scale, STRING("sl_frame"), FLOAT(0.7f), FLOAT(0.7f), FLOAT(0.7f))
    CALL(evt_npc_set_anim, STRING("sl_frame"), STRING("C_2"))
    CALL(evt_item_entry, STRING("shine41"), ITEM_SHINE_SPRITE, 330, 25, 122, ITEM_ENTRY_FIXED, SL3F(F_41_SHINE), 0)
    CALL(evt_mobj_itembox, STRING("box_muffler"), 310, 0, 55, 0, 0, PTR(muffler_open), SL3F(F_MUFFLER))
    CALL(evt_item_entry, STRING("piece41"), ITEM_STAR_PIECE, 375, 0, 45, ITEM_ENTRY_FIXED, SL3F(F_41_PIECE), 0)
    SPAWN_EVENT(bay_first)
    RETURN
EVT_END()

/* ======================================================================
 * tou_l42, the Back Lot Store (tou_12): the crane and the Gearbox
 * ====================================================================== */

DOOR1(d42_w, "door_1", -0.44433594f)
static BeroEntry b42[] = {
    BDOOR("w_bero", 4, "tou_l36", "w_bero", d42_w),
    {0},
};

static EVT_BEGIN(gearbox_get)
    CALL(evt_mario_key_onoff, 0)
    KEY("sl_c3_gearbox_got")
    SET_VALUE(SL3F(F_GEARBOX), 1)
    TQ("sl_c3_gearbox_01")
    M("sl_c3_gearbox_02", "mario")
    TQ("sl_c3_gearbox_03")
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(crane_fixed)
    SET_VALUE(SL3F(F_CRANE), 1)
    CALL(evt_snd_sfxon, STRING("SFX_STG7_CRANE_MOVE1"), 0)
    /* the big block goes up on its cable and stays up */
    CALL(evt_sub_intpl_msec_init, 11, 0, 160, 1500)
    LOOP(0)
        CALL(evt_sub_intpl_msec_get_value)
        CALL(evt_mobj_set_y_position, STRING("aa"), LW(0))
        WAIT_FRAMES(1)
        IF_INT_EQUAL(LW(1), 0)
            BREAK_LOOP
        END_IF
    END_LOOP
    CALL(evt_mobj_itembox, STRING("box_gear"), -120, 0, 40, 0, 0, PTR(gearbox_get), SL3F(F_GEARBOX)) /* the crates wall off the block's side of the room (x -72, z -100 to 80, too high to jump): it's let down this side */
    RETURN
EVT_END()

static EVT_BEGIN(tou_l42_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG3_TOU1"))
    BERO_RUN(b42)
    CALL(evt_map_playanim, STRING("anm_hae"), 1, 0)
    CALL(evt_mobj_brick, STRING("mobj_hidden"), -110, 70, -70, ITEM_MULTIBOUNCE, 12, 0, SL3F(F_42_MULTI))
    IF_INT_EQUAL(SL3F(F_CRANE), 0)
        CALL(evt_mobj_lv_blk, STRING("aa"), 125, 0, 0, 0, SL3F(F_NEVER), STRING("MOBJ_Lv1BigBigBlock"))
        CALL(evt_sl_point, PORT_SL_ABILITY_TUNE, 0, -110, 0, -40, 90, 0, PTR(crane_fixed)) /* on the door's side of the crates */
    ELSE
        CALL(evt_mobj_lv_blk, STRING("aa"), 125, 160, 0, 0, SL3F(F_NEVER), STRING("MOBJ_Lv1BigBigBlock"))
        CALL(evt_mobj_itembox, STRING("box_gear"), -120, 0, 40, 0, 0, PTR(gearbox_get), SL3F(F_GEARBOX)) /* the crates wall off the block's side of the room (x -72, z -100 to 80, too high to jump): it's let down this side */
    END_IF
    RETURN
EVT_END()

/* ======================================================================
 * tou_l43, the Air Duct (tou_13)
 * ====================================================================== */

static BeroEntry b43[] = {
    {"e_bero_", 0, 0, BERO_DIR_W, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "tou_l37", "e_bero", 0, 0, 0, 0},
    {"w_bero_", 0, 0, BERO_DIR_E, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "tou_l35", "w_bero", 0, 0, 0, 0},
    {0},
};

static EVT_BEGIN(duct_echo)
    CALL(evt_seq_wait, 2)
    WAIT_MS(600)
    SAY(PORT_SL_FRIEND_BLOOEY, "sl_c3_duct_echo")
    SET_VALUE(SL3F(F_43_ECHO), 1)
    RETURN
EVT_END()

static EVT_BEGIN(tou_l43_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG3_TOU1"))
    CALL(evt_snd_set_rev_mode, 2)
    BERO_RUN(b43)
    CALL(evt_item_entry, STRING("coin43"), ITEM_COIN, -40, 0, 0, ITEM_ENTRY_FIXED, SL3F(F_43_COIN), 0) /* a coin someone dropped in the duct */
    IF_INT_EQUAL(SL3F(F_43_ECHO), 0)
        SPAWN_EVENT(duct_echo)
    END_IF
    RETURN
EVT_END()

/* ======================================================================
 * The chapter
 * ====================================================================== */

extern EvtWord sl3_stadium_init[], sl3_grid_init[], sl3_circuit_init[];
extern const char* const portSl3Text[];
void portSl3BattleRegister(void); /* superluigi_revvo_aurora.c */
void portSl3RaceRegister(void);   /* superluigi_race_aurora.c */

static const short kItems3[] = {ITEM_MUSHROOM, ITEM_MUSHROOM, ITEM_SUPER_SHROOM, ITEM_HONEY_SYRUP,
                                ITEM_HONEY_SYRUP, ITEM_FIRE_FLOWER, ITEM_FRIGHT_MASK, 0};
static const short kBadges3[] = {ITEM_POWER_BOUNCE, ITEM_POWER_SMASH, ITEM_ICE_SMASH, ITEM_CLOSE_CALL, ITEM_HP_PLUS, 0};

static const PortSlMap kMaps3[] = {
    {"tou_l31", tou_l31_init, "tou_01", 1},
    {"tou_l32", tou_l32_init, "tou_02", 1},
    {"tou_l33", sl3_stadium_init, "tou_03", 1},
    {"tou_l34", tou_l34_init, "tou_04", 1},
    {"tou_l35", tou_l35_init, "tou_05", 1},
    {"tou_l36", tou_l36_init, "tou_06", 1},
    {"tou_l37", tou_l37_init, "tou_07", 1},
    {"tou_l38", tou_l38_init, "tou_08", 1},
    {"tou_l39", tou_l39_init, "tou_10", 1},
    {"tou_l40", tou_l40_init, "tou_09", 1},
    {"tou_l41", tou_l41_init, "tou_11", 1},
    {"tou_l42", tou_l42_init, "tou_12", 1},
    {"tou_l43", tou_l43_init, "tou_13", 1},
    {"yuu_l30", sl3_grid_init, "yuu_00", 1},
    {"yuu_l31", sl3_circuit_init, "yuu_02", 1},
    {NULL, NULL, NULL, 0},
};

/* the units in this chapter (ch3.md, "Battles"); Revvo's own numbers are his unit's */
static const PortSlUnitStats kStats3[] = {
    {UNIT_BOB_OMB, 0, 0, 0, -1, 20},
    {UNIT_SPINY, 3, 0, 0, 0, 20}, /* Defense 0, 3 HP: at 1 a Hammer hit, four of them out-lasted a level-9 Luigi's 25 HP */
    {UNIT_LAKITU, 0, 0, 0, -1, 22},
    {UNIT_SHADY_KOOPA, 5, 0, -2, -1, 24}, /* three of them at 8 HP and a hit less took 11 HP a round off a level-9 Luigi and beat him in five: the plain Hammer and Super Boots need three rounds */
    {UNIT_SHADY_PARATROOPA, 5, 0, -2, -1, 24},
    {UNIT_POKEY, 5, 0, 0, -1, 24},
    {UNIT_DARK_KOOPATROL, 0, 0, 0, 1, 40}, /* Revvo: the plain Hammer does 2, so 2 of Defense left only flips to hurt him */
    {UNIT_HAMMER_BRO, 6, 2, 0, 0, 26}, /* with Revvo and Nut, 3 a hammer took 11 HP a round off a level-10 Luigi */
    {UNIT_BOOMERANG_BRO, 6, 1, 0, 0, 26},
    {UNIT_IRON_CLEFT_RED, 4, 0, -1, 101, 26}, /* iron's immune to everything Luigi has here (25 HP gone with not a scratch on them): Defense 1, a hit the less */
    {UNIT_IRON_CLEFT_GREEN, 4, 0, -1, 101, 26},
    {UNIT_RED_SPIKY_BUZZY, 4, 0, 0, -1, 26}, /* (and Shell Slide goes through them for this bout: portTorqueSlidePierce) */
    {UNIT_BANDIT, 0, 0, 0, -1, 26},
    {UNIT_BIG_BANDIT, 0, 0, 0, -1, 26},
    {0},
};


static void setup3(void) {
    portSl3BattleRegister();
    portSl3RaceRegister();
    portSlMapTextures("tou_l32,yuu_l31", "textures/ch3_posters/");
    portSl3PosterSets();
}

static const PortSlChapter kChapter3 = {
    "Circuit Break Island", "tou_l31", "a_door_mon", "gor_luigi_talk_0003", PORT_SL_FRIEND_TORQUE,
    9, 25, 15, 12, 2, 1, 150, kItems3, kBadges3,
    PORT_SL_BIT(PORT_SL_FRIEND_BLOOEY) | PORT_SL_BIT(PORT_SL_FRIEND_JERRY),
    kMaps3, portSl3Text, kStats3, setup3, "sl_c3_diary",
};

const PortSlChapter* portSlChapter3(void) {
    return &kChapter3;
}
