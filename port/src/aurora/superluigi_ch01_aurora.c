/* Super Luigi's prologue, "The Letter", and chapter 1, "Rumblebump Volcano"
 * (docs/SUPER_LUIGI.md has the rooms, people, fights and finds). Each room is
 * a map of the mods' host maps on borrowed geometry, with an init script of
 * its own (the borrowed map's init would play Mario's scenes): exits, music,
 * the game's own blocks, items and Save Blocks, the people and the scenes.
 * Field enemies are the borrowed map's own (its setup file), with its area's
 * battle table; the statue is a battle unit of ours (superluigi_statue_aurora.c). */
#include "port/superluigi.h"
#include "port/superluigi_evt.h"
#include "battle/battle_unit.h"
#include "data/item_data.h"
#include "driver/npcdrv.h"
#include "evt/evt_bero.h"
#include "evt/evt_cam.h"
#include "evt/evt_case.h"
#include "driver/hitdrv.h"
#include "evt/evt_door.h"
#include "evt/evt_kinopio.h"
#include "evt/evt_hit.h"
#include "evt/evt_cmd.h"
#include "evt/evt_story.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mario.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "pmario_sound.h"
#include "system.h"
#include "data_tables.h"
#include "port/superluigi_sjis.h"
#include "port/superluigi_sjis.h"
#include "evt/evt_shop.h"
#include "battle/battle.h"
#include "port/mod_host.h"
#include "port/hook_list.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* the chapters' flags, in Super Luigi's bank (port/superluigi.h): the
 * prologue's two in chapter 0's, the rest in chapter 1's */
#define SLF(n) SL_GSWF((n) <= F_LETTER ? 0 : 1, n)
enum {
    F_CH0_OPENED,  /* the prologue's opening played */
    F_LETTER,      /* Luigi read Crepe's letter */
    F_CH1_OPENED,  /* chapter 1's opening */
    F_BLOOEY,      /* Blooey joined */
    F_L2_BADGE,
    F_L2_BLOCK,
    F_L2_PIECE,
    F_L3_COIN,
    F_L3_HIDDEN,
    F_L3_BADGE,
    F_L3_PIECE,
    F_L3_HEART,
    F_D1_SAVE,
    F_D1_ITEM,
    F_STATUE_AWAKE,
    F_STATUE_DOWN,
    F_PIECE,
    F_D1_INTRO,
};

USER_FUNC(evt_sl_title_card);
USER_FUNC(evt_sl_where);
USER_FUNC(evt_sl_enemy);
USER_FUNC(evt_sl_npc_fly);
USER_FUNC(evt_sl_chapter_end);
USER_FUNC(evt_sl_mode);
USER_FUNC(evt_sl_friend_join);
USER_FUNC(evt_sl_swim_point);
USER_FUNC(evt_sl_swim_clear);
USER_FUNC(evt_sl_lava_zone);
USER_FUNC(evt_sl_boss_arm);

/* the prologue's own flags, from n 2 of chapter 0's bank (SLF's two come first) */
enum {
    F0_ROAD_OPENED = 2,
    F0_ROAD_MET,
    F0_ROAD_BLOCK,
    F0_ROAD_COIN,
    F0_TOWN_OPENED,
    F0_TOWN_PIECE,
    F0_WOOD_PIPE,
    F0_WOOD_BRIDGE,
    F0_WOOD_BLOCK,
    F0_WOOD_BADGE,
    F0_WOOD_STAR,
    F0_GATE_OPENED,
};

/* Griddle's stall: the counter's six spots (nok_00's) and the game's shop lines */
static ShopObj muj_l1_shop_spots0[] = {
    {"S_item_01", "A_item_01"}, {"S_item_02", "A_item_02"}, {"S_item_03", "A_item_03"},
    {"S_item_04", "A_item_04"}, {"S_item_05", "A_item_05"}, {"S_item_06", "A_item_06"},
};
static ShopperData35 nok_l0_shopper = {
    SJ_CLERK, NULL, NULL,
    {"shop_00", "shop_01", "shop_02", "shop_03", "shop_04", "shop_05", "shop_06", "shop_07", "shop_08", "shop_09",
     "shop_10", "shop_11", "shop_12", "shop_13", "shop_14", "shop_15", "shop_16", "shop_17", "shop_18", "shop_19",
     "shop_20", "shop_21", "shop_22", "shop_23", "shop_24", "shop_25", "shop_26", "shop_27", "shop_28", "shop_29",
     "shop_30", "shop_31", "shop_32", "shop_33", "shop_34"},
};

/* ======================================================================
 * Prologue: The Letter
 * ====================================================================== */

/* aaa_l0, the brothers' house (aaa_00): the pipe in the yard, once the letter's read */
static BeroEntry aaa_l0_pipe[] = {
    {"dokan_1", 2, 0, BERO_DIR_DOWN, BERO_POS_AUTO, 0, 0, -1, NULL, 6, NULL, "hei_l0", "dokan_2", 1, 1, 0, 0},
    {0},
};

static EVT_BEGIN(aaa_l0_read_letter)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_party_stop, 0)
    IF_INT_EQUAL(SLF(F_LETTER), 0)
        CALL(evt_mapobj_get_position, STRING("S_posuto"), LW(0), LW(1), LW(2))
        CALL(evt_snd_sfxon_3d, STRING("SFX_EVT_POST_SHAKE1"), LW(0), LW(1), LW(2), 0)
        CALL(evt_sl_where, STRING("reading"))
        CALL(evt_msg_print, 0, STRING("sl0_mail_open"), 0, STRING("mario"))
        CALL(evt_snd_bgmoff, PSND_FADE_HOLD)
        CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_RUIJI1"))
        CALL(evt_msg_print, 0, STRING("sl0_letter"), 0, 0)
        CALL(evt_msg_print, 0, STRING("sl0_after_letter"), 0, STRING("mario"))
        CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_MARIO_HOUSE1"))
        CALL(evt_msg_print, 0, STRING("sl0_note"), 0, STRING("mario"))
        SET_VALUE(SLF(F_LETTER), 1)
        CALL(evt_msg_print, 0, STRING("sl0_pipe_hint"), 0, STRING("mario"))
    ELSE
        CALL(evt_msg_print, 0, STRING("sl0_mail_empty"), 0, STRING("mario"))
    END_IF
    CALL(evt_party_run, 0)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* The mail comes: a mail Paratroopa drops it in the box and flies off */
static EVT_BEGIN(aaa_l0_opening)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_mario_set_pos, 120, 0, 80)
    CALL(evt_mario_set_dir, 90, 0, 0)
    CALL(evt_seq_wait, 2)
    WAIT_MS(900) /* the card's curtain opens first */
    CALL(evt_mapobj_get_position, STRING("S_posuto"), LW(0), LW(1), LW(2))
    CALL(evt_sl_where, STRING("opening, mailbox in lw"))
    CALL(evt_sl_title_card)
    CALL(evt_msg_print, 0, STRING("sl0_alone"), 0, STRING("mario"))
    CALL(evt_npc_entry, STRING("sl_paretta"), STRING("c_paretta"))
    CALL(evt_npc_set_position, STRING("sl_paretta"), 500, 500, 0)
    CALL(evt_sl_where, STRING("paretta in"))
    CALL(evt_sl_npc_fly, STRING("sl_paretta"), 230, 45, -80, 2200)
    CALL(evt_sl_where, STRING("paretta landed"))
    CALL(evt_npc_reverse_ry, STRING("sl_paretta"))
    WAIT_MS(400)
    CALL(evt_mapobj_get_position, STRING("S_posuto"), LW(0), LW(1), LW(2))
    CALL(evt_snd_sfxon_3d, STRING("SFX_EVT_POST_SHAKE1"), LW(0), LW(1), LW(2), 0)
    LOOP(3)
        CALL(evt_mapobj_trans, 1, STRING("S_posuto"), 2, 0, 0)
        WAIT_FRAMES(2)
        CALL(evt_mapobj_trans, 1, STRING("S_posuto"), 0, 0, 0)
        WAIT_FRAMES(2)
    END_LOOP
    CALL(evt_npc_reverse_ry, STRING("sl_paretta"))
    CALL(evt_msg_print, 0, STRING("sl0_paretta"), 0, STRING("sl_paretta"))
    CALL(evt_sl_npc_fly, STRING("sl_paretta"), -100, 320, -200, 1800)
    CALL(evt_npc_delete, STRING("sl_paretta"))
    CALL(evt_msg_print, 0, STRING("sl0_mail_here"), 0, STRING("mario"))
    SET_VALUE(SLF(F_CH0_OPENED), 1)
    CALL(evt_sl_where, STRING("to the mailbox"))
    /* Luigi goes to see */
    CALL(evt_mario_mov_pos2, 205, -95, FLOAT(110.0f))
    CALL(evt_mario_set_dir, 90, 0, 0)
    CALL_CHILD(aaa_l0_read_letter)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* the road east past the mailbox runs to the room's edge: no room past it, so Luigi turns back */
static EVT_BEGIN(aaa_l0_road_watch)
    LOOP(0)
        CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
        IF_INT_GREATER(LW(0), 440)
            CALL(evt_mario_key_onoff, 0)
            CALL(evt_mario_set_dir, 90, 0, 0)
            CALL(evt_msg_print, 0, STRING("sl0_road_town"), 0, STRING("mario"))
            CALL(evt_mario_mov_pos2, 380, LW(2), FLOAT(90.0f))
            CALL(evt_mario_set_dir, 270, 0, 0)
            CALL(evt_mario_key_onoff, 1)
        END_IF
        WAIT_FRAMES(4)
    END_LOOP
    RETURN
EVT_END()

static EVT_BEGIN(aaa_l0_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_MARIO_HOUSE1"))
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_OPN_AAA1"))
    /* the pipe out back: to the Waffle Kingdom (the letter's read in the opening, before Luigi can move) */
    SET_VALUE(LW(0), PTR(aaa_l0_pipe))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 0, STRING("sai_ki1"), MAPOBJ_FLAG_BBOX_CULL)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 0, STRING("sai_ki2"), MAPOBJ_FLAG_BBOX_CULL)
    /* the two trees the game's opening parts like curtains: out of the way */
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("sai_ki1"), MAPOBJ_FLAG_HIDE)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("sai_ki2"), MAPOBJ_FLAG_HIDE)
    /* the mailbox: A in front of it (the letter's read at once, in the opening; later, it's empty) */
    CALL(evt_run_case_evt, CASE_COND_FRONT_A, 1, STRING("A_posuto"), 0, PTR(aaa_l0_read_letter), 0)
    CALL(evt_mobj_save_blk, STRING("mobj_save"), -60, 60, 60, 0, 0)
    SPAWN_EVENT(aaa_l0_road_watch)
    IF_INT_EQUAL(SLF(F_CH0_OPENED), 0)
        SPAWN_EVENT(aaa_l0_opening)
    END_IF
    RETURN
EVT_END()

/* The road to the castle: rooms no other chapter borrows. Everyone Luigi talks to: LW(0) the name,
 * LW(1) the model, LW(7) its tribe, LW(2..4) the place, LW(5) the way they face, LW(6) their talk script */
static USER_FUNC(sl0_set_talk) {
    NpcEntry* npc = npcNameToPtr_NoAssert((const char*)evtGetValue(event, event->args[0]));
    (void)isFirstCall;
    if (npc != NULL) {
        npc->talkEvt = (void*)evtGetValue(event, event->args[1]);
    }
    return EVT_RETURN_DONE;
}

static EVT_BEGIN(sl0_person)
    CALL(evt_npc_entry, LW(0), LW(1))
    CALL(evt_npc_set_tribe, LW(0), LW(7))
    CALL(evt_npc_set_position, LW(0), LW(2), LW(3), LW(4))
    CALL(evt_npc_set_ry, LW(0), LW(5))
    IF_INT_NOT_EQUAL(LW(6), 0)
        CALL(sl0_set_talk, LW(0), LW(6))
    END_IF
    RETURN
EVT_END()

/* a line from Luigi or whoever's talked to: LW(14) the tag */
static EVT_BEGIN(sl0_say_luigi)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_party_stop, 0)
    CALL(evt_msg_print, 0, LW(14), 0, STRING("mario"))
    CALL(evt_party_run, 0)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

#define SL0_TALK(script, tag, who)                 \
    static EVT_BEGIN(script)                  \
        CALL(evt_mario_key_onoff, 0)          \
        CALL(evt_msg_print, 0, STRING(tag), 0, STRING(who)) \
        CALL(evt_mario_key_onoff, 1)          \
        RETURN                                \
    EVT_END()
#define SL0_LOOK(script, tag)                 \
    static EVT_BEGIN(script)                  \
        SET_VALUE(LW(14), STRING(tag))        \
        CALL_CHILD(sl0_say_luigi)             \
        RETURN                                \
    EVT_END()

/* hei_l0, the Waffle Road (hei_00): Luigi comes up out of a pipe into the sunshine */
static BeroEntry hei_l0_bero[] = {
    {"dokan_2", 2, 0, BERO_DIR_DOWN, BERO_POS_AUTO, 0, 0, -1, NULL, 6, NULL, "aaa_l0", "dokan_1", 1, 1, 0, 0},
    {"e_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "nok_l0", "w_bero", 1, 1, 0, 0},
    {0},
};

SL0_LOOK(hei_l0_look_stones, "sl0_stones")

static EVT_BEGIN(hei_l0_cadet_talk)
    CALL(evt_mario_key_onoff, 0)
    IF_INT_EQUAL(SL_GSWF(0, F0_ROAD_MET), 0)
        SET_VALUE(SL_GSWF(0, F0_ROAD_MET), 1)
        CALL(evt_msg_print, 0, STRING("sl0_cadet_1"), 0, STRING("sl_cadet"))
        CALL(evt_msg_print, 0, STRING("sl0_cadet_luigi"), 0, STRING("mario"))
    END_IF
    CALL(evt_msg_print, 0, STRING("sl0_cadet_2"), 0, STRING("sl_cadet"))
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* a beat in the light first; the cadet's seen him and hurries over; then he's Luigi's to walk */
static EVT_BEGIN(hei_l0_opening)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_seq_wait, 2)
    CALL(evt_bero_exec_wait, 65536)
    WAIT_MS(800)
    CALL(evt_msg_print, 0, STRING("sl0_road_arrive"), 0, STRING("mario"))
    /* off the pipe and down the bank to the cadet, who's been waiting at the bottom */
    CALL(evt_mario_mov_pos2, -175, -25, FLOAT(90.0f))
    CALL(evt_mario_set_dir, 90, 0, 0)
    WAIT_MS(200)
    CALL_CHILD(hei_l0_cadet_talk)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(hei_l0_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG1_HEI1"))
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG1_HEI1"))
    CALL(evt_sl_enemy, STRING("sl_e1"), STRING(SJ_GOOMBA), STRING(SJ_GOOMBA), 60, 25, 50, 90, 60, STRING("BTLNO_HEI_01_01"))
    CALL(evt_sl_enemy, STRING("sl_e2"), STRING(SJ_GOOMBA), STRING(SJ_GOOMBA), 250, 25, 40, 90, 60, STRING("BTLNO_HEI_01_01"))
    SET_VALUE(LW(0), PTR(hei_l0_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    /* the carving and pipe of Mario's story aren't Luigi's */
    CALL(evt_mapobj_flag_onoff, MAPOBJ_SINGLE, 1, STRING("h_dokan"), MAPOBJ_FLAG_HIDE)
    CALL(evt_mapobj_trans, 0, STRING("h_dokan"), 0, -20, 0)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("s_moji"), MAPOBJ_FLAG_HIDE)
    CALL(evt_hitobj_onoff, STRING("a_moji"), 1, 0)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("taiyounoisi"), MAPOBJ_FLAG_HIDE)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("tukinoisi"), MAPOBJ_FLAG_HIDE)
    CALL(evt_run_case_evt, CASE_COND_FRONT_A, 1, STRING("a_isi_3"), 0, PTR(hei_l0_look_stones), 0)
    CALL(evt_run_case_evt, CASE_COND_FRONT_A, 1, STRING("a_isi_4"), 0, PTR(hei_l0_look_stones), 0)
    CALL(evt_map_playanim, STRING("anm_mugi_1"), 1, 0)
    CALL(evt_mobj_save_blk, STRING("mobj_save"), -455, 135, -75, 0, 0)
    CALL(evt_mobj_badgeblk, STRING("mobj_hatenablk"), 450, 60, 0, ITEM_MUSHROOM, 0, SL_GSWF(0, F0_ROAD_BLOCK), 0)
    /* over the road's way round the stones (z 60), where the ground is at 25: at 60 it was a wall across it
     * that Luigi couldn't pass under, so the road east was shut */
    CALL(evt_mobj_brick, STRING("mobj_coin"), 90, 85, 60, ITEM_COIN, 0, 0, SL_GSWF(0, F0_ROAD_COIN))
    SET_VALUE(LW(0), STRING("sl_cadet"))
    SET_VALUE(LW(1), STRING("c_kinopio_b"))
    SET_VALUE(LW(7), STRING(SJ_TOAD_BLUE))
    SET_VALUE(LW(2), -120)
    SET_VALUE(LW(3), 25)
    SET_VALUE(LW(4), -30)
    SET_VALUE(LW(5), 270)
    SET_VALUE(LW(6), PTR(hei_l0_cadet_talk))
    CALL_CHILD(sl0_person)
    IF_INT_EQUAL(SL_GSWF(0, F0_ROAD_OPENED), 0)
        SET_VALUE(SL_GSWF(0, F0_ROAD_OPENED), 1)
        SPAWN_EVENT(hei_l0_opening)
    END_IF
    RETURN
EVT_END()

/* nok_l0, Griddle, a town on the road (nok_00): the game's own town, with our people */
static BeroEntry nok_l0_bero[] = {
    {"w_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "hei_l0", "e_bero", 1, 1, 0, 0},
    {"e_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "hei_l1", "w_bero", 1, 1, 0, 0},
    {0},
};

static const char* nok_l0_inn_npc[] = {SJ_INNKEEP, NULL};
static const char* nok_l0_inn_map[] = {"S_yad_mae", NULL};
static HouseParam nok_l0_inn = {
    1, 9, "A_yad_doa01", 9, "A_yad_in_doa", {"S_yado_open", "S_yado_doa_open", "S_yado_close", "S_yado_doa_close"},
    "S_yad_mae", "S_yad_doa01", {"S_yado_in", "A_yado_in"}, {"S_yado_in", "A_yado_in"}, nok_l0_inn_npc, nok_l0_inn_map, NULL, NULL,
};
static const char* nok_l0_shop_npc[] = {SJ_CLERK, NULL};
static HouseParam nok_l0_shop = {
    1, 9, "A_mise_doa", 9, "A_mise_in_doa", {"S_mise_open", "S_mise_doa_open", "S_mise_close", "S_mise_doa_close"},
    "S_mise_mae", "S_mise_doa", {"S_mise_in", "A_mise_in"}, {"S_mise_in", "A_mise_in"}, nok_l0_shop_npc, NULL, NULL, NULL,
};
static const char* nok_l0_home_npc[] = {"sl_widow", NULL};
static const char* nok_l0_home_map[] = {"S_ie_mae", NULL};
static HouseParam nok_l0_home = {
    1, 9, "A_ie_doa", 9, "A_ie_in_doa", {"S_ie_open", "S_ie_doa_open", "S_ie_close", "S_ie_doa_close"},
    "S_ie_mae", "S_ie_doa", {"S_ie_in", "A_ie_in"}, {"S_ie_in", "A_ie_in"}, nok_l0_home_npc, nok_l0_home_map, NULL, NULL,
};
static ShopItem nok_l0_goods[] = {
    {ITEM_MUSHROOM, 4}, {ITEM_HONEY_SYRUP, 5}, {ITEM_FIRE_FLOWER, 8},
    {ITEM_POW_BLOCK, 5}, {ITEM_COURAGE_SHELL, 5}, {ITEM_SLEEPY_SHEEP, 8},
};
static ShopDb nok_l0_trade[] = {
    {ITEM_MUSHROOM, 2, 0}, {ITEM_HONEY_SYRUP, 2, 0}, {ITEM_FIRE_FLOWER, 4, 0}, {ITEM_POW_BLOCK, 2, 0},
    {ITEM_COURAGE_SHELL, 2, 0}, {ITEM_SLEEPY_SHEEP, 4, 0}, {0},
};
static KinopioData nok_l0_inn_data = {
    0, {0}, 5, {-269.0f, 41.0f, -256.0f}, {-268.0f, 25.0f, -220.0f},
    {-495.0f, 227.0f, -56.0f, -327.0f, 90.0f, -225.0f}, -30.0f, {-330.0f, 25.0f, -180.0f}, SJ_INNKEEP,
    {"yado_00", "yado_01", "yado_02", "yado_03", "yado_04", "yado_05", "yado_06", "yado_07", "yado_08", "yado_09",
     "yado_10", "yado_11", "yado_12", "yado_13"},
    NULL, NULL,
};

SL0_TALK(nok_l0_talk_baker, "sl0_town_baker", "sl_baker")
SL0_TALK(nok_l0_talk_gossip, "sl0_town_gossip", "sl_gossip")
SL0_TALK(nok_l0_talk_kid, "sl0_town_kid", "sl_kid")
SL0_TALK(nok_l0_talk_old, "sl0_town_old", "sl_old")
SL0_TALK(nok_l0_talk_widow, "sl0_town_widow", "sl_widow")

static EVT_BEGIN(nok_l0_opening)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_seq_wait, 2)
    CALL(evt_bero_exec_wait, 65536)
    WAIT_MS(700)
    CALL(evt_msg_print, 0, STRING("sl0_town_arrive"), 0, STRING("mario"))
    SET_VALUE(SL_GSWF(0, F0_TOWN_OPENED), 1)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(nok_l0_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG1_NOK1"))
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG1_NOK1"))
    SET_VALUE(LW(0), PTR(nok_l0_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL(evt_map_playanim, STRING("S_kawa"), 1, 0)
    /* the clerks first: the doors hide who's inside */
    SET_VALUE(LW(0), STRING(SJ_CLERK))
    SET_VALUE(LW(1), STRING("c_kinopio"))
    SET_VALUE(LW(7), STRING(SJ_TOAD))
    SET_VALUE(LW(2), 20)
    SET_VALUE(LW(3), 90)
    SET_VALUE(LW(4), -380)
    SET_VALUE(LW(5), 0)
    SET_VALUE(LW(6), 0)
    CALL_CHILD(sl0_person)
    SET_VALUE(LW(0), STRING(SJ_INNKEEP))
    SET_VALUE(LW(1), STRING("c_kinopiko"))
    SET_VALUE(LW(7), STRING(SJ_TOADETTE))
    SET_VALUE(LW(2), -360)
    SET_VALUE(LW(3), 25)
    SET_VALUE(LW(4), -180)
    SET_VALUE(LW(5), 0)
    SET_VALUE(LW(6), 0)
    CALL_CHILD(sl0_person)
    SET_VALUE(LW(0), STRING("sl_widow"))
    SET_VALUE(LW(1), STRING("c_kinopiko_p"))
    SET_VALUE(LW(7), STRING(SJ_TOADETTE_PINK))
    SET_VALUE(LW(2), 360)
    SET_VALUE(LW(3), 65)
    SET_VALUE(LW(4), -250)
    SET_VALUE(LW(5), 270)
    SET_VALUE(LW(6), PTR(nok_l0_talk_widow))
    CALL_CHILD(sl0_person)
    SET_VALUE(LW(0), STRING("sl_baker"))
    SET_VALUE(LW(1), STRING("c_kinopio_g"))
    SET_VALUE(LW(7), STRING(SJ_TOAD_GREEN))
    SET_VALUE(LW(2), -250)
    SET_VALUE(LW(3), 0)
    SET_VALUE(LW(4), 65)
    SET_VALUE(LW(5), 270)
    SET_VALUE(LW(6), PTR(nok_l0_talk_baker))
    CALL_CHILD(sl0_person)
    SET_VALUE(LW(0), STRING("sl_gossip"))
    SET_VALUE(LW(1), STRING("c_kinopiko"))
    SET_VALUE(LW(7), STRING(SJ_TOADETTE))
    SET_VALUE(LW(2), 144)
    SET_VALUE(LW(3), 0)
    SET_VALUE(LW(4), 16)
    SET_VALUE(LW(5), 90)
    SET_VALUE(LW(6), PTR(nok_l0_talk_gossip))
    CALL_CHILD(sl0_person)
    SET_VALUE(LW(0), STRING("sl_kid"))
    SET_VALUE(LW(1), STRING("c_kinopio"))
    SET_VALUE(LW(7), STRING(SJ_TOAD))
    SET_VALUE(LW(2), 130)
    SET_VALUE(LW(3), 0)
    SET_VALUE(LW(4), 120)
    SET_VALUE(LW(5), 270)
    SET_VALUE(LW(6), PTR(nok_l0_talk_kid))
    CALL_CHILD(sl0_person)
    SET_VALUE(LW(0), STRING("sl_old"))
    SET_VALUE(LW(1), STRING("c_kinopio_b"))
    SET_VALUE(LW(7), STRING(SJ_TOAD_BLUE))
    /* by the bridge's east end: Mario's spot for him (-40, 275) is the south plaza, which the river rings
     * and no bridge reaches; at z 150 he stood inside the fence along the river (A_saku_03), where Luigi
     * couldn't reach him to talk: on the road's side of it */
    SET_VALUE(LW(2), 60)
    SET_VALUE(LW(3), 0)
    SET_VALUE(LW(4), 112)
    SET_VALUE(LW(5), 270)
    SET_VALUE(LW(6), PTR(nok_l0_talk_old))
    CALL_CHILD(sl0_person)
    SET_VALUE(LW(0), PTR(&nok_l0_inn))
    CALL_CHILD(evt_door_setup)
    SET_VALUE(LW(0), PTR(&nok_l0_shop))
    CALL_CHILD(evt_door_setup)
    SET_VALUE(LW(0), PTR(&nok_l0_home))
    CALL_CHILD(evt_door_setup)
    CALL(evt_mobj_save_blk, STRING("mobj_save"), 155, 60, -60, 0, 0)
    CALL(evt_mobj_recovery_blk, STRING("mobj_heart"), 0, 225, 60, -60, 0, 0)
    CALL(evt_shop_setup, PTR(muj_l1_shop_spots0), PTR(nok_l0_goods), PTR(&nok_l0_shopper), PTR(nok_l0_trade))
    CALL(evt_kinopio_setup, PTR(&nok_l0_inn_data))
    /* the Star Piece half in the ground on the west lawn, south of the road in, as hei_01's is: Mario's town
     * keeps one under a spinning floor in the south plaza, but that needs a Spin Jump, which the prologue's
     * Luigi hasn't got, the plaza can't be reached, and the campaign never comes back here */
    CALL(evt_item_entry, STRING("item_01"), ITEM_STAR_PIECE, -380, 0, 250, ITEM_ENTRY_FIXED, SL_GSWF(0, F0_TOWN_PIECE), 0)
    CALL(evt_item_set_scale, STRING("item_01"), FLOAT(0.5996094f))
    IF_INT_EQUAL(SL_GSWF(0, F0_TOWN_OPENED), 0)
        SPAWN_EVENT(nok_l0_opening)
    END_IF
    RETURN
EVT_END()

/* hei_l1, the Maple Wood (hei_01): the river's bridge is flat; a bush hides the pipe to the path behind, where
 * a blue switch stands the bridge up, as in Mario's Petal Meadows */
static BeroEntry hei_l1_bero[] = {
    {"w_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "nok_l0", "e_bero", 1, 1, 0, 0},
    {"e_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "gon_l1", "w_bero", 1, 1, 0, 0},
    {"dokan_h", 4098, 0, BERO_DIR_DOWN, BERO_POS_AUTO, 0, 0, -1, NULL, 6, NULL, NULL, "dokan", 1, 1, 0, 0},
    {"dokan", 2, 0, BERO_DIR_DOWN, BERO_POS_AUTO, 0, 0, -1, NULL, 6, NULL, NULL, "dokan_h", 1, 1, 0, 0},
    {0},
};

static EVT_BEGIN(hei_l1_bush)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_mapobj_get_position, STRING("anm_kusa_1"), LW(5), LW(6), LW(7))
    CALL(evt_snd_sfxon_3d, STRING("SFX_GRASS_SHAKE1"), LW(5), LW(6), LW(7), 0)
    BEGIN_INLINE_EVENT
        WAIT_MS(600)
        CALL(evt_snd_sfxon_3d, STRING("SFX_DOKAN_APPEAR1"), LW(5), LW(6), LW(7), 0)
    END_INLINE_EVENT
    CALL(evt_map_playanim, STRING("anm_kusa_1"), 0, 0)
    CALL(evt_map_checkanim, STRING("anm_kusa_1"), LW(0), LW(1))
    WAIT_MS(LW(1))
    SET_VALUE(SL_GSWF(0, F0_WOOD_PIPE), 1)
    CALL(evt_hitobj_onoff, STRING("a_kusa_1"), 0, 0)
    SET_VALUE(LW(0), STRING("dokan"))
    CALL_CHILD(bero_case_switch_on)
    CALL(evt_mario_key_onoff, 1)
    CALL(evt_exit_case_evt)
    RETURN
EVT_END()

static EVT_BEGIN(hei_l1_bridge)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_npc_stop_for_event)
    BEGIN_INLINE_EVENT
        CALL(evt_mobj_wait_animation_end, STRING("bg_switch"))
        CALL(evt_mobj_delete, STRING("bg_switch"))
    END_INLINE_EVENT
    CALL(evt_cam3d_evt_set, -15, 150, 500, -15, 45, 85, 500, 11)
    WAIT_MS(800)
    CALL(evt_snd_sfxon, STRING("SFX_STG1_PERA1"), 0)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 0, STRING("hasi"), MAPOBJ_FLAG_HIDE)
    CALL(evt_hitobj_onoff, STRING("a_hasi"), 1, 1)
    SET_VALUE(SL_GSWF(0, F0_WOOD_BRIDGE), 1)
    BEGIN_INLINE_EVENT
        CALL(evt_snd_bgmoff, PSND_FADE_HOLD)
        CALL(evt_snd_bgmon, PSND_CH(1), STRING("BGM_FF_ENV_SUCCESS1"))
        WAIT_MS(2000)
        CALL(evt_snd_bgmoff, PSND_FADE_OUT | PSND_CH(1))
        CALL(evt_snd_bgmon, PSND_FADE_RESUME | PSND_FADE_IN, 0)
    END_INLINE_EVENT
    WAIT_MS(1200)
    CALL(evt_cam3d_evt_off, 500, 11)
    WAIT_MS(500)
    CALL(evt_msg_print, 0, STRING("sl0_wood_bridge"), 0, STRING("mario"))
    CALL(evt_npc_start_for_event)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

SL0_TALK(hei_l1_talk_ranger, "sl0_wood_ranger", "sl_ranger")

static EVT_BEGIN(hei_l1_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG1_HEI1"))
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG1_HEI2"))
    CALL(evt_sl_enemy, STRING("sl_e1"), STRING(SJ_GOOMBA), STRING(SJ_GOOMBA), -340, 0, 40, 90, 60, STRING("BTLNO_HEI_01_01"))
    CALL(evt_sl_enemy, STRING("sl_e2"), STRING(SJ_GOOMBA), STRING(SJ_GOOMBA), 300, 0, 20, 90, 60, STRING("BTLNO_HEI_01_01"))
    SET_VALUE(LW(0), PTR(hei_l1_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL(evt_mobj_badgeblk, STRING("mobj_hatenablk"), 315, 60, -75, ITEM_FIRE_FLOWER, 0, SL_GSWF(0, F0_WOOD_BLOCK), 0)
    CALL(evt_mobj_badgeblk, STRING("mobj_badge"), 420, 60, 60, ITEM_POWER_JUMP, 0, SL_GSWF(0, F0_WOOD_BADGE), 1)
    CALL(evt_item_entry, STRING("item_01"), ITEM_STAR_PIECE, 450, 40, -447, ITEM_ENTRY_FIXED, SL_GSWF(0, F0_WOOD_STAR), 0)
    CALL(evt_item_set_scale, STRING("item_01"), FLOAT(0.5996094f))
    SET_VALUE(LW(0), STRING("dokan"))
    CALL_CHILD(bero_case_switch_off)
    IF_INT_EQUAL(SL_GSWF(0, F0_WOOD_PIPE), 0)
        CALL(evt_run_case_evt, CASE_COND_FRONT_A, 1, STRING("a_kusa_1"), 0, PTR(hei_l1_bush), 0)
    ELSE
        CALL(evt_hitobj_onoff, STRING("a_kusa_1"), 0, 0)
        CALL(evt_map_playanim, STRING("anm_kusa_1"), 2, 0)
        SET_VALUE(LW(0), STRING("dokan"))
        CALL_CHILD(bero_case_switch_on)
    END_IF
    CALL(evt_map_playanim, STRING("anm_mizu"), 1, 0)
    IF_INT_EQUAL(SL_GSWF(0, F0_WOOD_BRIDGE), 0)
        CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("hasi"), MAPOBJ_FLAG_HIDE)
        CALL(evt_hitobj_onoff, STRING("a_hasi"), 1, 0)
        CALL(evt_mobj_switch_blue, 2, STRING("bg_switch"), -8, 40, -450, PTR(hei_l1_bridge), 0)
    END_IF
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 0, STRING("USIROKUSA"), MAPOBJ_FLAG_BBOX_CULL)
    SET_VALUE(LW(0), STRING("sl_ranger"))
    SET_VALUE(LW(1), STRING("c_kinopio_g"))
    SET_VALUE(LW(7), STRING(SJ_TOAD_GREEN))
    /* not by the bush: an NPC Luigi could talk to takes the A press (casedrv's front check) */
    SET_VALUE(LW(2), -330)
    SET_VALUE(LW(3), 0)
    SET_VALUE(LW(4), 70)
    SET_VALUE(LW(5), 270)
    SET_VALUE(LW(6), PTR(hei_l1_talk_ranger))
    CALL_CHILD(sl0_person)
    RETURN
EVT_END()

/* gon_l1, the castle's gatehouse (gon_00, warmed): the line said on arrival, a guard, and the door's Luigi's to walk to */
static BeroEntry gon_l1_bero[] = {
    {"w_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "hei_l1", "e_bero", 1, 1, 0, 0},
    {"e_bero_1", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "gon_l0", "w_bero_1", 1, 1, 0, 0},
    {0},
};

SL0_TALK(gon_l1_talk_guard, "sl0_gate_guard", "sl_gate")

static EVT_BEGIN(gon_l1_opening)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_seq_wait, 2)
    CALL(evt_bero_exec_wait, 65536)
    WAIT_MS(500)
    CALL(evt_mario_mov_pos2, -110, 0, FLOAT(80.0f))
    WAIT_MS(300)
    CALL(evt_msg_print, 0, STRING("sl0_castle_arrive"), 0, STRING("mario"))
    SET_VALUE(SL_GSWF(0, F0_GATE_OPENED), 1)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(gon_l1_init)
    CALL(evt_snd_bgmoff, PSND_FADE_OUT)
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG1_GON1"))
    CALL(evt_snd_set_rev_mode, 2)
    CALL(evt_map_set_blend, 0, 255, 225, 190, 255)
    SET_VALUE(LW(0), PTR(gon_l1_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL(evt_map_playanim, STRING("hokori"), 1, 0)
    CALL(evt_mobj_save_blk, STRING("mobj_save"), 100, 60, -90, 0, 0)
    CALL(evt_mobj_recovery_blk, STRING("mobj_heart"), 0, 40, 60, -90, 0, 0)
    SET_VALUE(LW(0), STRING("sl_gate"))
    SET_VALUE(LW(1), STRING("c_kinopio"))
    SET_VALUE(LW(7), STRING(SJ_TOAD))
    SET_VALUE(LW(2), 165)
    SET_VALUE(LW(3), 0)
    SET_VALUE(LW(4), -55)
    SET_VALUE(LW(5), 270)
    SET_VALUE(LW(6), PTR(gon_l1_talk_guard))
    CALL_CHILD(sl0_person)
    IF_INT_EQUAL(SL_GSWF(0, F0_GATE_OPENED), 0)
        SPAWN_EVENT(gon_l1_opening)
    END_IF
    RETURN
EVT_END()

/* gon_l0, the Waffle Kingdom's castle hall (gon_04, warmed): Minister Crepe hurries over to Luigi at the
 * door (x -572). The raiders burst in before he can hand over the base; Crepe doesn't seem surprised */
static BeroEntry gon_l0_bero[] = {
    {"w_bero_1", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, NULL, NULL, 1, 1, 0, 0},
    {0},
};

static EVT_BEGIN(gon_l0_scene)
    CALL(evt_mario_key_onoff, 0)
    /* in close on the door end, so the hall's outer edge stays out of the picture: on Luigi (-510) and
     * Crepe (-430) between them, not on Crepe, which left Luigi half off the picture's edge */
    CALL(evt_cam3d_evt_set, -455, 90, 440, -455, 50, 0, 0, 11)
    CALL(evt_seq_wait, 2)
    CALL(evt_bero_exec_wait, 65536)
    WAIT_MS(700)
    CALL(evt_mario_mov_pos2, -510, 0, FLOAT(80.0f))
    CALL(evt_npc_move_position, STRING("sl_crepe"), -430, 0, 1200, 0, 4)
    CALL(evt_mario_set_dir, 90, 0, 0)
    CALL(evt_msg_print, 0, STRING("sl0_crepe_1"), 0, STRING("sl_crepe"))
    CALL(evt_msg_print, 0, STRING("sl0_luigi_1"), 0, STRING("mario"))
    CALL(evt_msg_print, 0, STRING("sl0_crepe_2"), 0, STRING("sl_crepe"))
    CALL(evt_msg_print, 0, STRING("sl0_luigi_2"), 0, STRING("mario"))
    CALL(evt_msg_print, 0, STRING("sl0_crepe_3"), 0, STRING("sl_crepe"))
    /* it goes wrong: the far doors, the music stops, the raiders */
    CALL(evt_snd_bgmoff_f, PSND_FADE_OUT, 300)
    CALL(evt_snd_sfxon, STRING("SFX_BOSS_MBTN_STEPON1"), 0)
    WAIT_MS(600)
    CALL(evt_msg_print, 0, STRING("sl0_crash_guard"), 0, STRING("sl_guard1"))
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_BOSS_STG1_GONBABA1"))
    CALL(evt_npc_stop_for_event)
    CALL(evt_sl_npc_fly, STRING("sl_raider"), -350, 0, 0, 900)
    CALL(evt_msg_print, 0, STRING("sl0_raider_1"), 0, STRING("sl_raider"))
    CALL(evt_msg_print, 0, STRING("sl0_crepe_calm"), 0, STRING("sl_crepe"))
    CALL(evt_msg_print, 0, STRING("sl0_luigi_fight"), 0, STRING("mario"))
    LABEL(1)
    CALL(evt_npc_battle_start, STRING("sl_raider"))
    CALL(evt_npc_wait_battle_end)
    CALL(evt_npc_get_battle_result, LW(0))
    IF_INT_NOT_EQUAL(LW(0), 1)
        /* there's nowhere to run to: he's between them and the base */
        CALL(evt_msg_print, 0, STRING("sl0_no_run"), 0, STRING("sl_crepe"))
        GOTO(1)
    END_IF
    CALL(evt_npc_delete, STRING("sl_raider"))
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_PEACH_WALTZ1"))
    WAIT_MS(500)
    CALL(evt_msg_print, 0, STRING("sl0_after_luigi"), 0, STRING("mario"))
    CALL(evt_msg_print, 0, STRING("sl0_after_crepe"), 0, STRING("sl_crepe"))
    /* the compass base, handed over: it lights up and points south */
    CALL(evt_snd_sfxon, STRING("SFX_EVT_STARSTONE_SHINE1"), 0)
    CALL(evt_mario_set_pose, STRING("M_I_2"))
    CALL(evt_msg_print, 0, STRING("sl0_base"), 0, 0)
    CALL(evt_map_set_blend, 0, 255, 255, 230, 255)
    CALL(evt_snd_sfxon, STRING("SFX_MOBJ_BLOCK_POWER_SHINE1"), 0)
    WAIT_MS(600)
    CALL(evt_map_set_blend, 0, 255, 215, 170, 255)
    CALL(evt_mario_set_pose, STRING("M_S_1"))
    CALL(evt_msg_print, 0, STRING("sl0_crepe_4"), 0, STRING("sl_crepe"))
    CALL(evt_msg_print, 0, STRING("sl0_luigi_3"), 0, STRING("mario"))
    /* Crepe, to himself, once Luigi's turned away */
    CALL(evt_mario_set_dir, 270, 0, 0)
    WAIT_MS(500)
    CALL(evt_msg_print, 0, STRING("sl0_crepe_aside"), 0, STRING("sl_crepe"))
    WAIT_MS(400)
    CALL(evt_sl_chapter_end)
    RETURN
EVT_END()

static EVT_BEGIN(gon_l0_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_PEACH_WALTZ1"))
    CALL(evt_map_set_blend, 0, 255, 215, 170, 255)
    SET_VALUE(LW(0), PTR(gon_l0_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    /* Crepe as chapter 6 has him (his own model), not an elder Toad */
    CALL(evt_npc_entry, STRING("sl_crepe"), STRING("c_sl_crepe"))
    CALL(evt_npc_set_position, STRING("sl_crepe"), -300, 0, 0)
    CALL(evt_npc_set_ry, STRING("sl_crepe"), 270)
    CALL(evt_npc_entry, STRING("sl_guard1"), STRING("c_kinopio"))
    CALL(evt_npc_set_position, STRING("sl_guard1"), -260, 0, -70)
    CALL(evt_npc_entry, STRING("sl_guard2"), STRING("c_kinopio"))
    CALL(evt_npc_set_position, STRING("sl_guard2"), -260, 0, 70)
    /* the raid's leader, its fight the castle's own: out in the far hall until he's called */
    CALL(evt_sl_enemy, STRING("sl_raider"), STRING(SJ_RED_BONES), STRING(SJ_GOOMBA), -100, 0, 0, 0, 0, STRING("BTLNO_GON_03_01"))
    CALL(evt_npc_set_ry, STRING("sl_raider"), 270)
    SPAWN_EVENT(gon_l0_scene)
    RETURN
EVT_END()

/* ======================================================================
 * Chapter 1: Rumblebump Volcano
 * ====================================================================== */

/* muj_l1, a harbour town on the Pudding Continent (muj_01): Blooey.
 * Luigi comes in from the ferry dock (west, Mario's way up from the beach); the ferry's gone, so
 * that way is the dock's sign and a turn back, never a walk off into a room that isn't there. */
static EVT_BEGIN(muj_l1_dock_sign)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_msg_print, 0, STRING("sl1_dock_sign"), 0, 0)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(muj_l1_dock_back)
    CALL(evt_party_stop, 0)
    CALL(evt_mario_set_dir, 270, 0, 0)
    CALL(evt_msg_print, 0, STRING("sl1_dock_sign"), 0, 0)
    CALL(evt_msg_print, 0, STRING("sl1_dock_back"), 0, STRING("mario"))
    CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
    CALL(evt_mario_mov_pos2, -430, LW(2), FLOAT(90.0f))
    CALL(evt_mario_set_dir, 90, 0, 0)
    CALL(evt_party_run, 0)
    SET_VALUE(LW(0), 1) /* no exit: he stays in town */
    RETURN
EVT_END()

static BeroEntry muj_l1_bero[] = {
    {"w_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, muj_l1_dock_back, NULL, NULL, 1, 1, 0, 0},
    {"e_bero1", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "muj_l2", "w_bero", 1, 1, 0, 0},
    {0},
};

static EVT_BEGIN(muj_l1_blooey_talk)
    CALL(evt_mario_key_onoff, 0)
    IF_INT_EQUAL(SLF(F_BLOOEY), 0)
        CALL(evt_msg_print, 0, STRING("sl1_blooey_1"), 0, STRING("sl_blooey"))
        CALL(evt_msg_print, 0, STRING("sl1_luigi_1"), 0, STRING("mario"))
        CALL(evt_msg_print, 0, STRING("sl1_blooey_2"), 0, STRING("sl_blooey"))
        CALL(evt_msg_print, 0, STRING("sl1_luigi_2"), 0, STRING("mario"))
        CALL(evt_msg_print, 0, STRING("sl1_blooey_3"), 0, STRING("sl_blooey"))
        /* Blooey joins */
        CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_FF_GET_PARTY1"))
        CALL(evt_msg_print, 0, STRING("sl1_joined"), 0, 0)
        CALL(evt_npc_get_position, STRING("sl_blooey"), LW(0), LW(1), LW(2))
        CALL(evt_npc_delete, STRING("sl_blooey"))
        CALL(evt_sl_friend_join, PORT_SL_FRIEND_BLOOEY)
        CALL(evt_pouch_party_join, 1)
        CALL(evt_mario_set_party_pos, 0, 1, LW(0), LW(1), LW(2))
        SET_VALUE(SLF(F_BLOOEY), 1)
        WAIT_MS(1500)
        CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG5_MUJ1"))
        CALL(evt_msg_print, 0, STRING("sl1_blooey_swim"), 0, STRING("party"))
    END_IF
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* the town's shop: the counter's six spots (muj_01's), our goods */
static ShopObj muj_l1_shop_spots[] = {
    {"S_item_01", "A_item_01"}, {"S_item_02", "A_item_02"}, {"S_item_03", "A_item_03"},
    {"S_item_04", "A_item_04"}, {"S_item_05", "A_item_05"}, {"S_item_06", "A_item_06"},
};
static ShopItem muj_l1_goods[] = {
    {ITEM_MUSHROOM, 5}, {ITEM_FIRE_FLOWER, 6}, {ITEM_HONEY_SYRUP, 5},
    {ITEM_ICE_STORM, 15}, {ITEM_SLEEPY_SHEEP, 8}, {ITEM_SUPER_SHROOM, 15},
};
static ShopDb muj_l1_trade[] = {
    {ITEM_MUSHROOM, 2, 0}, {ITEM_FIRE_FLOWER, 3, 0}, {ITEM_HONEY_SYRUP, 2, 0}, {ITEM_ICE_STORM, 7, 0},
    {ITEM_SLEEPY_SHEEP, 4, 0}, {ITEM_SUPER_SHROOM, 7, 0}, {ITEM_KEEL_MANGO, 3, 0}, {0},
};
static ShopperData35 muj_l1_shopper = {
    SJ_CLERK, NULL, NULL,
    {"shop_00", "shop_01", "shop_02", "shop_03", "shop_04", "shop_05", "shop_06", "shop_07", "shop_08", "shop_09",
     "shop_10", "shop_11", "shop_12", "shop_13", "shop_14", "shop_15", "shop_16", "shop_17", "shop_18", "shop_19",
     "shop_20", "shop_21", "shop_22", "shop_23", "shop_24", "shop_25", "shop_26", "shop_27", "shop_28", "shop_29",
     "shop_30", "shop_31", "shop_32", "shop_33", "shop_34"},
};

/* the stall and the inn tent: their doors, as muj_01's (its door scripts live in its rel, so ours):
 * the outside fades off layer 1 and the music goes muffled while Luigi's indoors */
static EVT_BEGIN(muj_l1_indoors)
    CALL(evt_sub_intpl_msec_init, 11, 255, 0, 300)
    LABEL(0)
    CALL(evt_sub_intpl_msec_get_value)
    CALL(evt_map_set_blend, 1, 255, 255, 255, LW(0))
    WAIT_FRAMES(1)
    IF_INT_EQUAL(LW(1), 1)
        GOTO(0)
    END_IF
    CALL(evt_map_set_blend, 1, 255, 255, 255, 0)
    RETURN
EVT_END()

static EVT_BEGIN(muj_l1_outdoors)
    CALL(evt_sub_intpl_msec_init, 11, 0, 255, 300)
    LABEL(0)
    CALL(evt_sub_intpl_msec_get_value)
    CALL(evt_map_set_blend, 1, 255, 255, 255, LW(0))
    WAIT_FRAMES(1)
    IF_INT_EQUAL(LW(1), 1)
        GOTO(0)
    END_IF
    RETURN
EVT_END()

static EVT_BEGIN(muj_l1_stall_open)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 0, STRING("S_mise_kan"), MAPOBJ_FLAG_NO_BLEND2)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 0, STRING("S_mise_hasi04"), MAPOBJ_FLAG_NO_BLEND2)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 0, STRING("S_mise_hasi05"), MAPOBJ_FLAG_NO_BLEND2)
    SPAWN_EVENT(muj_l1_indoors)
    CALL(evt_snd_bgmoff, PSND_FADE_DIP_TO(50))
    CALL(evt_snd_envoff, PSND_FADE_DIP_TO(50))
    CALL(evt_snd_env_lpf, 0, 800)
    RETURN
EVT_END()

static EVT_BEGIN(muj_l1_stall_close)
    BEGIN_INLINE_EVENT
        CALL_CHILD(muj_l1_outdoors)
        CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("S_mise_kan"), MAPOBJ_FLAG_NO_BLEND2)
        CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("S_mise_hasi04"), MAPOBJ_FLAG_NO_BLEND2)
        CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("S_mise_hasi05"), MAPOBJ_FLAG_NO_BLEND2)
    END_INLINE_EVENT
    CALL(evt_snd_bgmon, PSND_FADE_RESUME | PSND_FADE_UNDIP, 0)
    CALL(evt_snd_envon, PSND_FADE_RESUME | PSND_FADE_UNDIP, 0)
    CALL(evt_snd_env_lpf, 0, 0)
    RETURN
EVT_END()

static EVT_BEGIN(muj_l1_tent_open)
    SPAWN_EVENT(door_dark)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("S_yado"), MAPOBJ_FLAG_NO_BLEND)
    SPAWN_EVENT(muj_l1_indoors)
    CALL(evt_snd_bgmoff, PSND_FADE_DIP_TO(50))
    CALL(evt_snd_envoff, PSND_FADE_DIP_TO(50))
    CALL(evt_snd_env_lpf, 0, 800)
    RETURN
EVT_END()

static EVT_BEGIN(muj_l1_tent_close)
    SPAWN_EVENT(door_light)
    SPAWN_EVENT(muj_l1_outdoors)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 0, STRING("S_yado"), MAPOBJ_FLAG_NO_BLEND)
    WAIT_MS(1000)
    CALL(evt_snd_bgmon, PSND_FADE_RESUME | PSND_FADE_UNDIP, 0)
    CALL(evt_snd_envon, PSND_FADE_RESUME | PSND_FADE_UNDIP, 0)
    CALL(evt_snd_env_lpf, 0, 0)
    RETURN
EVT_END()

static const char* muj_l1_stall_npc[] = {SJ_CLERK, NULL};
static const char* muj_l1_tent_npc[] = {SJ_INNKEEP, NULL};
static DoorParam muj_l1_stall = {0, 3, 0, NULL, "A_mise_in_doa", "A_mise_doa", NULL, NULL, "S_mise", NULL, NULL, NULL, NULL,
                                 muj_l1_stall_npc, NULL, NULL, muj_l1_stall_open, muj_l1_stall_close};
static DoorParam muj_l1_tent = {0, 3, 0, NULL, "A_yado_in_doa", "A_yado_doa", NULL, NULL, "S_yado_in", NULL, NULL, NULL, NULL,
                                muj_l1_tent_npc, NULL, NULL, muj_l1_tent_open, muj_l1_tent_close};

/* muj_01's own doors, ours as its scripts live in its rel: walked into, no A; the building stays in sight
 * from outside (evt_door_entry would hide its inside, the whole stall). */
static USER_FUNC(sl1_door_near) {
    EvtWord* args = event->args;
    DoorParam* door = (DoorParam*)evtGetValue(event, args[0]);
    f32 range = evtGetFloat(event, args[1]);
    Vec hitPos;
    Vec marioPos;
    (void)isFirstCall;
    hitObjGetPos(door->hitOut, &hitPos);
    marioPos = marioGetPtr()->position;
    marioPos.y = hitPos.y = 0.0f;
    evtSetValue(event, args[2], PSVECDistance(&hitPos, &marioPos) > range ? 0 : 1);
    return EVT_RETURN_DONE;
}

static EVT_BEGIN(muj_l1_door_in)
    CALL(evt_mario_party_use_check, LW(4))
    IF_INT_EQUAL(LW(4), 4)
        RETURN
    END_IF
    CALL(sl1_door_near, LW(0), FLOAT(30.0f), LW(1))
    IF_INT_EQUAL(LW(1), 0)
        RETURN
    END_IF
    CALL(evt_cam_letter_box_disable, 1)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_door_param, LW(0), 7, LW(1), LW(2))
    IF_INT_NOT_EQUAL(LW(1), 0)
        SET_VALUE(LF(0), 0)
        SPAWN_EVENT(LW(1))
    END_IF
    CALL(evt_door_param, LW(0), 2, LW(1), LW(2))
    IF_INT_NOT_EQUAL(LW(1), 0)
        CALL(evt_door_save_mapflag, LW(1))
        CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 0, LW(1), MAPOBJ_FLAG_BBOX_CULL)
    END_IF
    BEGIN_INLINE_EVENT
        CALL(evt_door_param, LW(0), 10, LW(1), LW(2), LW(3))
        CALL(evt_mario_get_pos, 0, LW(4), LW(5), LW(6))
        ADD(LW(5), 2)
        CALL(evt_cam3d_evt_set_xyz, LW(1), LW(5), LW(3), 0, 150, 11)
    END_INLINE_EVENT
    CALL(evt_door_param, LW(0), 2, LW(1), LW(2))
    IF_INT_NOT_EQUAL(LW(1), 0)
        CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, LW(1), MAPOBJ_FLAG_NO_BLEND)
    END_IF
    CALL(evt_door_param, LW(0), 1, LW(1), LW(2), LW(3))
    IF_INT_NOT_EQUAL(LW(1), 0)
        CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, LW(1), MAPOBJ_FLAG_NO_BLEND)
        CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, LW(1), MAPOBJ_FLAG_NO_BLEND2)
    END_IF
    CALL(evt_door_param, LW(0), 3, LW(1), LW(2))
    IF_INT_NOT_EQUAL(LW(1), 0)
        CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 0, LW(1), MAPOBJ_FLAG_HIDE)
        CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, LW(1), MAPOBJ_FLAG_NO_BLEND)
        CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, LW(1), MAPOBJ_FLAG_NO_BLEND2)
    END_IF
    IF_INT_NOT_EQUAL(LW(2), 0)
        CALL(evt_hitobj_onoff, LW(2), 1, 1)
    END_IF
    CALL(door_dark_flag, LW(0), 1)
    IF_BITS_CLEAR(LW(15), 2)
        SPAWN_EVENT(door_dark)
    END_IF
    SPAWN_EVENT(door_open_se)
    CALL(evt_door_param, LW(0), 6, LW(7), LW(8), LW(9))
    IF_INT_NOT_EQUAL(LW(7), 0)
        BEGIN_INLINE_EVENT
            CALL(evt_door_param, LW(0), 6, LW(7), LW(8), LW(9))
            CALL(evt_sub_intpl_msec_init, 5, 0, 180, 500)
            LABEL(0)
            CALL(evt_sub_intpl_msec_get_value)
            SET_VALUE(LW(5), LW(1))
            SET_VALUE(LW(1), 0)
            SET_VALUE(LF(0), 0)
            SPAWN_EVENT(LW(7))
            WAIT_FRAMES(1)
            IF_INT_EQUAL(LW(5), 1)
                GOTO(0)
            END_IF
        END_INLINE_EVENT
    END_IF
    WAIT_MS(500)
    CALL(evt_door_param, LW(0), 10, LW(1), LW(2), LW(3))
    CALL(evt_mario_party_door_move, LW(1), LW(3), FLOAT(135.0f))
#if VERSION <= VERSION_DEMO_JP
    CALL(evt_party_move_behind_mario, 0)
    CALL(evt_party_move_behind_mario, 1)
#endif
    CALL(evt_cam3d_evt_xyz_off, 0, 11)
    CALL(evt_mario_key_onoff, 1)
    CALL(evt_cam_letter_box_disable, 0)
    RETURN
EVT_END()

static EVT_BEGIN(muj_l1_door_out)
    CALL(evt_mario_party_use_check, LW(4))
    IF_INT_EQUAL(LW(4), 4)
        RETURN
    END_IF
    CALL(sl1_door_near, LW(0), FLOAT(30.0f), LW(1))
    IF_INT_EQUAL(LW(1), 0)
        RETURN
    END_IF
    CALL_CHILD(evt_door_close)
    RETURN
EVT_END()

/* LW(0) the door; LW(14) its door flags (bits for door_dark_flag: 17 the stall's, 19 the tent's) */
static EVT_BEGIN(muj_l1_door_entry)
    CALL(evt_door_param, LW(0), 1, LW(1), LW(2), LW(3))
    IF_INT_NOT_EQUAL(LW(2), 0)
        SET_VALUE(LW(15), LW(14))
        CALL(evt_run_case_evt, CASE_COND_FRONT, 1, LW(2), 0, PTR(muj_l1_door_out), 0)
    END_IF
    IF_INT_NOT_EQUAL(LW(3), 0)
        SET_VALUE(LW(15), LW(14))
        CALL(evt_run_case_evt, CASE_COND_FRONT, 1, LW(3), 0, PTR(muj_l1_door_in), 0)
    END_IF
    RETURN
EVT_END()

/* the tent's inn: 10 coins, muj_01's bed and camera */
static KinopioData muj_l1_inn = {
    0, {0}, 10, {403.0f, 17.0f, -182.0f}, {355.0f, 0.0f, -155.0f},
    {213.0f, 122.0f, -97.0f, 341.0f, 45.0f, -178.0f}, -30.0f, {352.0f, 0.0f, -135.0f}, SJ_INNKEEP,
    {"yado_00", "yado_01", "yado_02", "yado_03", "yado_04", "yado_05", "yado_06", "yado_07", "yado_08", "yado_09",
     "yado_10", "yado_11", "yado_12", "yado_13"},
    NULL, NULL,
};

/* things by the fire a player would check */
static EVT_BEGIN(muj_l1_look)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_party_stop, 0)
    CALL(evt_msg_print, 0, LW(14), 0, STRING("mario"))
    CALL(evt_party_run, 0)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()
static EVT_BEGIN(muj_l1_look_kettle)
    SET_VALUE(LW(14), STRING("sl1_kettle"))
    CALL_CHILD(muj_l1_look)
    RETURN
EVT_END()
static EVT_BEGIN(muj_l1_look_bucket)
    SET_VALUE(LW(14), STRING("sl1_bucket"))
    CALL_CHILD(muj_l1_look)
    RETURN
EVT_END()
static EVT_BEGIN(muj_l1_look_chair)
    SET_VALUE(LW(14), STRING("sl1_chair"))
    CALL_CHILD(muj_l1_look)
    RETURN
EVT_END()

/* the kettle's map animation, over and over */
static EVT_BEGIN(muj_l1_kettle)
    LOOP(0)
        CALL(evt_map_playanim, STRING("yakan"), 1, 0)
        CALL(evt_map_checkanim, STRING("yakan"), LW(0), LW(1))
        WAIT_MS(LW(1))
    END_LOOP
    RETURN
EVT_END()

static EVT_BEGIN(muj_l1_opening)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_seq_wait, 2)
    WAIT_MS(900) /* the card's curtain opens first */
    CALL(evt_sl_title_card)
    /* up from the dock: the ferry that brought him pulls out behind him (his walk-in
     * finished first: until then it holds his facing) */
    CALL(evt_bero_exec_wait, 65536)
    CALL(evt_snd_sfxon, STRING("SFX_EVT_OPN_SHIP_MOVE1"), 0)
    WAIT_MS(500)
    CALL(evt_mario_set_dir, 270, 0, 0)
    WAIT_MS(600)
    CALL(evt_msg_print, 0, STRING("sl1_ferry"), 0, STRING("mario"))
    CALL(evt_mario_set_dir, 90, 0, 0)
    WAIT_MS(300)
    CALL(evt_msg_print, 0, STRING("sl1_arrive"), 0, STRING("mario"))
    SET_VALUE(SLF(F_CH1_OPENED), 1)
    /* Blooey heard him coming */
    CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
    ADD(LW(0), 70)
    CALL(evt_npc_set_anim, STRING("sl_blooey"), STRING("R_1"))
    CALL(evt_npc_move_position, STRING("sl_blooey"), LW(0), LW(2), 1200, 0, 4)
    CALL(evt_npc_set_anim, STRING("sl_blooey"), STRING("S_1"))
    CALL(evt_mario_set_dir_npc, STRING("sl_blooey"))
    CALL_CHILD(muj_l1_blooey_talk)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(muj_l1_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG5_MUJ1"))
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG5_MUJ1_1"))
    SET_VALUE(LW(0), PTR(muj_l1_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL(evt_map_playanim, STRING("mizu"), 1, 0)
    CALL(evt_map_playanim, STRING("fuku_hata"), 1, 0)
    CALL(evt_mobj_save_blk, STRING("mobj_save"), 100, 60, -25, 0, 0)
    /* the dock's sign on the path past the way out west (w_bero, x -500) */
    CALL(evt_mobj_signboard, STRING("sl_dock_sign"), -545, 0, -20, PTR(muj_l1_dock_sign), LSWF(0))
    SET_VALUE(LW(0), PTR(&muj_l1_stall))
    SET_VALUE(LW(14), 17)
    SPAWN_EVENT(muj_l1_door_entry)
    SET_VALUE(LW(0), PTR(&muj_l1_tent))
    SET_VALUE(LW(14), 19)
    SPAWN_EVENT(muj_l1_door_entry)
    CALL(evt_shop_setup, PTR(muj_l1_shop_spots), PTR(muj_l1_goods), PTR(&muj_l1_shopper), PTR(muj_l1_trade))
    CALL(evt_kinopio_setup, PTR(&muj_l1_inn))
    SPAWN_EVENT(muj_l1_kettle)
    CALL(evt_run_case_evt, CASE_COND_FRONT_A, 1, STRING("A_takibi"), 0, PTR(muj_l1_look_kettle), 0)
    CALL(evt_run_case_evt, CASE_COND_FRONT_A, 1, STRING("A_baketu"), 0, PTR(muj_l1_look_bucket), 0)
    CALL(evt_run_case_evt, CASE_COND_FRONT_A, 1, STRING("A_isu01"), 0, PTR(muj_l1_look_chair), 0)
    IF_INT_EQUAL(SLF(F_BLOOEY), 0)
        CALL(evt_npc_entry, STRING("sl_blooey"), STRING("c_lp_gesso"))
        CALL(evt_npc_set_tribe, STRING("sl_blooey"), STRING(SJ_BLOOEY_TRIBE))
        CALL(evt_npc_set_position, STRING("sl_blooey"), -180, 21, 40) /* on the quay, in sight of the gate (Luigi comes in at -470, 21) */
        CALL(evt_npc_set_ry, STRING("sl_blooey"), 270)
        CALL(evt_npc_change_interrupt, STRING("sl_blooey"), 6, PTR(muj_l1_blooey_talk))
    END_IF
    IF_INT_EQUAL(SLF(F_CH1_OPENED), 0)
        SPAWN_EVENT(muj_l1_opening)
    END_IF
    RETURN
EVT_END()

/* muj_l2, the jungle (muj_02): its Fuzzies and Piranhas, blocks */
static BeroEntry muj_l2_bero[] = {
    {"w_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "muj_l1", "e_bero1", 1, 1, 0, 0},
    {"e_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "muj_l3", "w_bero", 1, 1, 0, 0},
    {0},
};

static EVT_BEGIN(muj_l2_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG5_MUJ1"))
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG5_MUJ4"))
    CALL(evt_sl_enemy, STRING("sl_e1"), STRING(SJ_GREEN_FUZZY), STRING(SJ_FUZZY_AI), -325, 0, 0, 150, 100, STRING("BTLNO_MUJ_02_01"))
    CALL(evt_sl_enemy, STRING("sl_e2"), STRING(SJ_FLOWER_FUZZY), STRING(SJ_FUZZY_AI), -150, 0, 50, 150, 100, STRING("BTLNO_MUJ_02_02"))
    CALL(evt_sl_enemy, STRING("sl_e3"), STRING(SJ_GREEN_FUZZY), STRING(SJ_FUZZY_AI), 425, 0, 0, 100, 100, STRING("BTLNO_MUJ_02_07"))
    SET_VALUE(LW(0), PTR(muj_l2_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    /* the pipe to Rogueport's sewers (Mario's, once its switch is thrown): Luigi has no sewers to go to, so as the game has it shut */
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("S_dakan"), MAPOBJ_FLAG_HIDE)
    CALL(evt_hitobj_onoff, STRING("A_dokan"), 1, 0)
    CALL(evt_mobj_badgeblk, STRING("mobj_badge"), -200, 60, -90, ITEM_POWER_SMASH, 0, SLF(F_L2_BADGE), 1)
    CALL(evt_mobj_badgeblk, STRING("mobj_hatena"), -135, 0, 135, ITEM_FIRE_FLOWER, 0, SLF(F_L2_BLOCK), 0)
    CALL(evt_item_entry, STRING("item_piece"), ITEM_STAR_PIECE, -500, 0, 110, ITEM_ENTRY_FIXED, SLF(F_L2_PIECE), 0)
    RETURN
EVT_END()

/* muj_l3, the cliffs (muj_03) */
static BeroEntry muj_l3_bero[] = {
    {"w_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "muj_l2", "e_bero", 1, 1, 0, 0},
    {"e_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_l1", "w_bero", 1, 1, 0, 0},
    {0},
};

static EVT_BEGIN(muj_l3_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG5_MUJ1"))
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG5_MUJ5"))
    CALL(evt_sl_enemy, STRING("sl_e1"), STRING(SJ_PUTRID_PIRANHA), STRING(SJ_PIRANHA_AI), 125, 10, 75, 150, 100, STRING("BTLNO_MUJ_03_01"))
    CALL(evt_sl_enemy, STRING("sl_e2"), STRING(SJ_PUTRID_PIRANHA), STRING(SJ_PIRANHA_AI), -175, 10, 75, 150, 100, STRING("BTLNO_MUJ_03_02"))
    CALL(evt_sl_enemy, STRING("sl_e3"), STRING(SJ_FLOWER_FUZZY), STRING(SJ_FUZZY_AI), -100, 210, -75, 200, 25, STRING("BTLNO_MUJ_03_04"))
    SET_VALUE(LW(0), PTR(muj_l3_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL(evt_mobj_badgeblk, STRING("mobj01"), -50, 9, 0, ITEM_COIN, 0, SLF(F_L3_COIN), 0)
    CALL(evt_mobj_brick, STRING("mobj_hidden"), -50, 94, 0, ITEM_SUPER_SHROOM, 11, 0, SLF(F_L3_HIDDEN))
    /* where Mario's muj_03 has a brick to stand on (155, out of reach from the floor below at 5) */
    CALL(evt_mobj_recovery_blk, STRING("mobj_heart"), 0, -470, 65, -50, 0, 0)
    CALL(evt_mobj_badgeblk, STRING("mobj03"), -50, 270, -100, ITEM_CLOSE_CALL, 0, SLF(F_L3_BADGE), 1)
    CALL(evt_item_entry, STRING("item_piece"), ITEM_STAR_PIECE, 275, 10, 100, ITEM_ENTRY_FIXED, SLF(F_L3_PIECE), 0)
    /* Mario crosses this room with a partner's float, over ledges too far apart for Luigi: a spring on the
     * shelf below the way on (the stairs up from the floor, then straight up) */
    CALL(evt_mobj_jumpstand_blue, 0, STRING("mobj_jump"), 170, 540, 60, 95, 0, 0)
    RETURN
EVT_END()

/* dou_l1, the mouth of the volcano (dou_00, its pool as lava): Swim */
static BeroEntry dou_l1_bero[] = {
    {"w_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "muj_l3", "e_bero", 1, 1, 0, 0},
    {"e_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_l2", "w_bero", 1, 1, 0, 0},
    {0},
};

static EVT_BEGIN(dou_l1_intro)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_seq_wait, 2)
    /* the Embers hold still while they talk */
    CALL(evt_npc_stop_for_event)
    WAIT_MS(400)
    CALL(evt_msg_print, 0, STRING("sl1_volcano"), 0, STRING("mario"))
    CALL(evt_msg_print, 0, STRING("sl1_volcano_bl"), 0, STRING("party"))
    SET_VALUE(SLF(F_D1_INTRO), 1)
    CALL(evt_npc_start_for_event)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* the lava's glow: the light breathes between two oranges */
static EVT_BEGIN(lava_glow)
    LOOP(0)
        CALL(evt_sub_intpl_msec_init, 11, 0, 40, 1500)
        LOOP(0)
            CALL(evt_sub_intpl_msec_get_value)
            SET_VALUE(LW(2), 150)
            ADD(LW(2), LW(0))
            CALL(evt_map_set_blend, 0, 255, LW(2), 110, 255)
            WAIT_FRAMES(1)
            IF_INT_EQUAL(LW(1), 0)
                BREAK_LOOP
            END_IF
        END_LOOP
        CALL(evt_sub_intpl_msec_init, 11, 40, 0, 1500)
        LOOP(0)
            CALL(evt_sub_intpl_msec_get_value)
            SET_VALUE(LW(2), 150)
            ADD(LW(2), LW(0))
            CALL(evt_map_set_blend, 0, 255, LW(2), 110, 255)
            WAIT_FRAMES(1)
            IF_INT_EQUAL(LW(1), 0)
                BREAK_LOOP
            END_IF
        END_LOOP
    END_LOOP
    RETURN
EVT_END()

static EVT_BEGIN(lava_room)
    CALL(evt_map_blend_set_flag, 0, STRING("mario"), ANIM_MATERIAL_FLAG_NO_BLEND)
    CALL(evt_map_set_fog, 0, FLOAT(300.0f), FLOAT(1600.0f), 120, 30, 10)
    CALL(evt_map_fog_onoff, 1)
    SPAWN_EVENT(lava_glow)
    RETURN
EVT_END()

static EVT_BEGIN(dou_l1_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG5_DOU1"))
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG5_DOU1"))
    CALL(evt_snd_set_rev_mode, 2)
    CALL(evt_sl_enemy, STRING("sl_e1"), STRING(SJ_EMBER), STRING(SJ_EMBER), -140, 25, -70, 60, 40, STRING("BTLNO_DOU_02_01"))
    CALL(evt_sl_enemy, STRING("sl_e2"), STRING(SJ_EMBER), STRING(SJ_EMBER), 60, 30, 110, 90, 50, STRING("BTLNO_DOU_02_02"))
    SET_VALUE(LW(0), PTR(dou_l1_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    WAIT_FRAMES(1)
    CALL(evt_map_playanim, STRING("nami"), 1, 0)
    /* the pool as lava: the game's object colour only multiplies (orange over dark
     * water is darker water), so a look of ours: tinted toward red, and glowing */
    CALL(evt_sl_map_look, STRING("s00_suimen"), 0xFF8040, 0xC03000)
    CALL_CHILD(lava_room)
    CALL(evt_mobj_save_blk, STRING("mobj_save"), 30, 85, -75, 0, 0)
    CALL(evt_mobj_recovery_blk, STRING("mobj_heart"), 0, 90, 85, -75, 0, 0)
    CALL(evt_item_entry, STRING("item_01"), ITEM_HONEY_SYRUP, -245, 0, 85, ITEM_ENTRY_FIXED, SLF(F_D1_ITEM), 0)
    CALL(evt_sl_swim_clear)
    /* the near shore by the entrance; the far one the ledge under the way on (e_bero, up its steps) */
    CALL(evt_sl_swim_point, 0, -120, 0, 40, 190, 50, 30)
    CALL(evt_sl_lava_zone, -60, -150, 260, 200, 5, -160, 10, 40)
    IF_INT_EQUAL(SLF(F_D1_INTRO), 0)
        SPAWN_EVENT(dou_l1_intro)
    END_IF
    RETURN
EVT_END()

/* dou_l2, the statue's hall (dou_13): the boss, the piece, Blooey's version */
static BeroEntry dou_l2_bero[] = {
    {"w_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_l1", "e_bero", 1, 1, 0, 0},
    {0},
};

static EVT_BEGIN(dou_l2_piece)
    CALL(evt_mario_key_onoff, 0)
    /* the lid up, then Luigi holds the piece high, to the game's own fanfare */
    CALL(evt_mobj_wait_animation_end, STRING("box"))
    CALL(evt_mario_set_pose, STRING("M_I_1"))
    CALL(evt_snd_bgmon, PSND_FADE_RESTART | PSND_FADE_OUT | PSND_CH(1), STRING("BGM_FF_GET_IMPORTANT_ITEM1"))
    CALL(evt_snd_sfxon, STRING("SFX_EVT_STARSTONE_SHINE1"), 0)
    CALL(evt_msg_print, 0, STRING("sl1_piece"), 0, 0)
    CALL(evt_mario_set_pose, STRING("M_S_1"))
    CALL(evt_msg_print, 0, STRING("sl1_piece_luigi"), 0, STRING("mario"))
    CALL(evt_map_set_blend, 0, 255, 255, 230, 255)
    CALL(evt_snd_sfxon, STRING("SFX_MOBJ_BLOCK_POWER_SHINE1"), 0)
    WAIT_MS(500)
    CALL(evt_map_set_blend, 0, 255, 170, 120, 255)
    CALL(evt_msg_print, 0, STRING("sl1_compass_west"), 0, STRING("mario"))
    /* Blooey's version, which never matches */
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_RUIJI1"))
    CALL(evt_msg_print, 0, STRING("sl1_tag_1"), 0, STRING("party"))
    CALL(evt_msg_print, 0, STRING("sl1_tag_2"), 0, STRING("mario"))
    CALL(evt_msg_print, 0, STRING("sl1_tag_3"), 0, STRING("party"))
    SET_VALUE(SLF(F_PIECE), 1)
    CALL(evt_sl_chapter_end)
    RETURN
EVT_END()

static EVT_BEGIN(dou_l2_open_chest)
    CALL_CHILD(dou_l2_piece)
    RETURN
EVT_END()

static EVT_BEGIN(dou_l2_fight)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_seq_wait, 2)
    /* Luigi walks in until the statue is in view */
    CALL(evt_mario_mov_pos2, -10, -20, FLOAT(90.0f))
    CALL(evt_mario_set_dir, 90, 0, 0)
    WAIT_MS(300)
    CALL(evt_msg_print, 0, STRING("sl1_statue_see"), 0, STRING("mario"))
    /* it wakes: the hall shakes, its eyes light, it takes a step */
    CALL(evt_snd_sfxon, STRING("SFX_BOSS_MBTN_STEPON1"), 0)
    CALL(evt_cam_shake, 4, FLOAT(0.0f), FLOAT(2.0f), 800)
    CALL(evt_npc_set_anim, STRING("sl_statue"), STRING("W_1"))
    CALL(evt_npc_move_position, STRING("sl_statue"), 110, -20, 1600, 0, 4)
    CALL(evt_npc_set_anim, STRING("sl_statue"), STRING("S_1"))
    CALL(evt_snd_sfxon, STRING("SFX_BOSS_MBTN_STEPON1"), 0)
    CALL(evt_cam_shake, 4, FLOAT(0.0f), FLOAT(3.0f), 600)
    CALL(evt_msg_print, 0, STRING("sl1_statue_bl"), 0, STRING("party"))
    CALL(evt_msg_print, 0, STRING("sl1_statue_luigi"), 0, STRING("mario"))
    CALL(evt_msg_print, 0, STRING("sl1_statue_bl2"), 0, STRING("party"))
    SET_VALUE(SLF(F_STATUE_AWAKE), 1)
    /* the fight: the statue's own unit (superluigi_statue_aurora.c) */
    CALL(evt_sl_boss_arm, 1)
    CALL(evt_npc_set_battle_info, STRING("sl_statue"), 0)
    CALL(evt_npc_battle_start, STRING("sl_statue"))
    CALL(evt_npc_wait_battle_end)
    CALL(evt_sl_boss_arm, 0)
    CALL(evt_npc_get_battle_result, LW(0))
    IF_INT_NOT_EQUAL(LW(0), 1)
        CALL(evt_mario_key_onoff, 1)
        RETURN /* lost or fled: the game's Game Over, or back in the hall */
    END_IF
    /* it crumbles (the keys are still off from the scene before the fight) */
    CALL(evt_npc_set_anim, STRING("sl_statue"), STRING("D_3"))
    WAIT_MS(500)
    CALL(evt_npc_get_position, STRING("sl_statue"), LW(0), LW(1), LW(2))
    LOOP(6)
        CALL(evt_snd_sfxon, STRING("SFX_KUPPA_STONE_BREAK1"), 0)
        CALL(evt_eff, 0, STRING("bomb"), 0, LW(0), LW(1), LW(2), FLOAT(1.5f), 0, 0, 0, 0, 0, 0, 0)
        CALL(evt_cam_shake, 4, FLOAT(0.0f), FLOAT(3.0f), 300)
        ADD(LW(1), 18)
        WAIT_MS(250)
    END_LOOP
    CALL(evt_npc_delete, STRING("sl_statue"))
    SET_VALUE(SLF(F_STATUE_DOWN), 1)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG5_DOU1"))
    /* where it stood, a chest drops out of the dust */
    CALL(evt_mobj_itembox, STRING("box"), 170, 90, -20, 1, 0, PTR(dou_l2_open_chest), SLF(F_PIECE))
    CALL(evt_eff, 0, STRING("kemuri_test"), 4, 170, 0, -20, FLOAT(1.5f), 0, 0, 0, 0, 0, 0, 0)
    WAIT_MS(400)
    CALL(evt_snd_sfxon, STRING("SFX_BOSS_MBTN_STEPON1"), 0)
    CALL(evt_cam_shake, 4, FLOAT(0.0f), FLOAT(1.0f), 300)
    WAIT_MS(300)
    CALL(evt_msg_print, 0, STRING("sl1_after_bl"), 0, STRING("party"))
    /* a big chest opens from its front: Luigi steps round to it */
    CALL(evt_mario_mov_pos2, 170, 40, FLOAT(110.0f))
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(dou_l2_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG5_DOU1"))
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG5_DOU9"))
    SET_VALUE(LW(0), PTR(dou_l2_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL_CHILD(lava_room)
    IF_INT_EQUAL(SLF(F_STATUE_DOWN), 0)
        CALL(evt_npc_entry, STRING("sl_statue"), STRING("c_lp_statue"))
        CALL(evt_npc_set_position, STRING("sl_statue"), 170, 0, -20)
        CALL(evt_npc_set_ry, STRING("sl_statue"), 270)
        CALL(evt_npc_set_anim, STRING("sl_statue"), STRING("S_1"))
        SPAWN_EVENT(dou_l2_fight)
    ELSE
        IF_INT_EQUAL(SLF(F_PIECE), 0)
            CALL(evt_mobj_itembox, STRING("box"), 170, 0, -20, 1, 0, PTR(dou_l2_open_chest), SLF(F_PIECE))
        END_IF
    END_IF
    RETURN
EVT_END()

/* ======================================================================
 * The chapters, maps and text
 * ====================================================================== */

static const short kItems1[] = {ITEM_MUSHROOM, ITEM_MUSHROOM, ITEM_HONEY_SYRUP, ITEM_FIRE_FLOWER, 0};
static const short kBadges1[] = {ITEM_POWER_JUMP, 0};

/* Keelhaul Key's and the Pirate's Grotto's own enemies, made chapter 1's: a
 * level-1 Luigi (the campaign's, from the prologue) takes them on as Mario
 * takes on the Goombas and Koopas of his: a few HP, hits of 1 or 2, and enough
 * Star Points for a level or two before the statue */
static const PortSlUnitStats kStats1[] = {
    {UNIT_GREEN_FUZZY, 2, 1, 0, -1, 20},
    {UNIT_FLOWER_FUZZY, 2, 1, 0, -1, 20},
    {UNIT_PUTRID_PIRANHA, 2, 1, 0, -1, 20},
    {UNIT_EMBER, 2, 1, 0, -1, 20},
    {UNIT_LAVA_BUBBLE, 2, 1, 0, -1, 20}, /* dou_l1's Embers fight as the Grotto's Lava Bubbles */
    {0},
};

/* the prologue's first fights: a level-1 Luigi's, and his first level from the road and the wood; the
 * raiders' Red Bones is the game's own size (5 HP), his Dull Bones one stomp each */
static const PortSlUnitStats kStats0[] = {
    {UNIT_GOOMBA, 2, 1, 0, -1, 18},
    /* defense 0: the cadet's lesson was Jump, and a level-1 jump does nothing against their own 1 */
    {UNIT_RED_BONES, 5, 1, 0, 0, 40},
    {UNIT_DULL_BONES, 1, 1, 0, 0, 12},
    {0},
};

/* The raid on the hall: Hooktail Castle's BTLNO_GON_03_01 is Red Bones and four Dull Bones, five blows a
 * round against a level-1 Luigi's 10 HP. A first fight's is Red Bones and one Dull Bones (he calls more in
 * as he goes, as the game's does; with two to start, a run with every guard played still lost 6 HP over 8
 * rounds): a copy of the group, the NPC's own battle info pointed at it (that info is the NPC's copy; the
 * castle's table is untouched) */
#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK
static BattleUnitSetup sRaidUnits[2];
static BattleGroupSetup sRaidGroup;

/* The road's and the wood's fights the same way: Petal Meadows' BTLNO_HEI_01_01 is two Goombas, a Spiky
 * Goomba and a Paragoomba, four blows a round; a level-1 Luigi coming into the wood on 5 HP from the road's
 * fight lost it (a Game Over in the run from the title). The prologue's fights are its Goombas alone: one
 * on the road, where the cadet has just taught Jump, two in the wood */
static void fieldGroupCut(BattleGroupSetup* g, NpcBattleInfo* info, int keep) {
    int i, n = 0;
    for (i = 0; i < g->numEnemies && n < keep; i++) {
        const BattleUnitKind* k = g->enemyData[i].unitKindParams;
        if (k != NULL && k->unitType == UNIT_GOOMBA) {
            sRaidUnits[n++] = g->enemyData[i];
        }
    }
    if (n == 0) {
        return;
    }
    sRaidGroup = *g;
    sRaidGroup.numEnemies = n;
    sRaidGroup.enemyData = sRaidUnits;
    info->groupSetup = &sRaidGroup;
    printf("[superluigi] the field fight: %d Goomba(s)\n", n);
    fflush(stdout);
}

static int raidSetupPre(BOOL* result, BattleWork** wp) {
    NpcBattleInfo* info = (*wp)->fbatInfo != NULL ? (*wp)->fbatInfo->setup : NULL;
    BattleGroupSetup* g;
    (void)result;
    if (portSuperLuigiChapter() != 0 || info == NULL || (g = info->groupSetup) == NULL || g == &sRaidGroup) {
        return 0;
    }
    if (strcmp(gp->currentMapName, "hei_l0") == 0 || strcmp(gp->currentMapName, "hei_l1") == 0) {
        fieldGroupCut(g, info, strcmp(gp->currentMapName, "hei_l0") == 0 ? 1 : 2);
        return 0;
    }
    if (strcmp(gp->currentMapName, "gon_l0") != 0 || g->numEnemies <= 2) {
        return 0;
    }
    memcpy(sRaidUnits, g->enemyData, sizeof(sRaidUnits));
    sRaidGroup = *g;
    sRaidGroup.numEnemies = 2;
    sRaidGroup.enemyData = sRaidUnits;
    info->groupSetup = &sRaidGroup;
    printf("[superluigi] the raid: Red Bones and a Dull Bones\n");
    fflush(stdout);
    return 0;
}

/* BtlUnit_Entry: the raiders stay down. Hooktail Castle's bones collapse and rebuild themselves in two or
 * three turns unless burnt or blown up; a level-1 Luigi with one stomp a turn and no fire never got all
 * three down at once, and a playtest of it ended in a Game Over. In the prologue a beaten one is beaten */
static void raidUnitEntryPost(BattleUnit** result, BattleUnitSetup* setup) {
    BattleUnit* u = *result;
    (void)setup;
    if (u != NULL && portSuperLuigiChapter() == 0 &&
        (u->currentKind == UNIT_RED_BONES || u->currentKind == UNIT_DULL_BONES)) {
        u->attributeFlags &= ~UNIT_ATTR_REVIVES_AFTER_KO;
    }
}

static void setup0(void) {
    portModHostAddHook(0, kSlot_Btl_UnitSetup, (void*)raidSetupPre);
    portModHostAddHook(1, kSlot_BtlUnit_Entry, (void*)raidUnitEntryPost);
}

static const PortSlMap kMaps0[] = {
    {"aaa_l0", aaa_l0_init, "aaa_00", 1},
    {"hei_l0", hei_l0_init, "hei_00", 1},
    {"nok_l0", nok_l0_init, "nok_00", 1},
    {"hei_l1", hei_l1_init, "hei_01", 1},
    {"gon_l1", gon_l1_init, "gon_00", 1},
    {"gon_l0", gon_l0_init, "gon_04", 1},
    {NULL, NULL, NULL, 0},
};

static const PortSlMap kMaps1[] = {
    {"muj_l1", muj_l1_init, "muj_01", 0},
    {"muj_l2", muj_l2_init, "muj_02", 1},
    {"muj_l3", muj_l3_init, "muj_03", 1},
    {"dou_l1", dou_l1_init, "dou_00", 1},
    {"dou_l2", dou_l2_init, "dou_13", 1},
    {NULL, NULL, NULL, 0},
};


void portSlStatueRegister(void); /* superluigi_statue_aurora.c */

/* chapter 1's setup: the statue, and Keelhaul Key's jungle and harbour turned volcanic */
static void setup1(void) {
    portSlStatueRegister();
    portSlMapTextures("muj_l1,muj_l2,muj_l3", "textures/ch1_lava/");
}

extern const char* const portSlText[];

static const PortSlChapter kChapter0 = {
    "The Letter", "aaa_l0", "sl_start", "gor_luigi_talk_0000", PORT_SL_FRIEND_NONE, 1, 10, 5, 3, 1, 1, 0, NULL, NULL,
    0, kMaps0, portSlText, kStats0, setup0, "sl0_diary",
};

static const PortSlChapter kChapter1 = {
    "Rumblebump Volcano", "muj_l1", "w_bero", "gor_luigi_talk_0001", PORT_SL_FRIEND_BLOOEY, 3, 15, 10, 6, 1, 1, 50,
    kItems1, kBadges1, 0, kMaps1, NULL, kStats1, setup1, "sl1_diary",
};

const PortSlChapter* portSlChapter0(void) {
    return &kChapter0;
}
const PortSlChapter* portSlChapter1(void) {
    return &kChapter1;
}

/* The chapters' own text: never the game's. Luigi speaks as "mario" (the
 * player); the friends as "party". */
const char* const portSlText[] = {
    /* the inn */
    "sl_inn_offer", "Say, Bro... you've got that look.\nLike you don't believe a word.\n<k>\n<p>\nTell you what. Why don't I\n<wait 200>SHOW you how it went?\n<o>",
    "sl_inn_showme", "<select 0 1 0 40>\nShow me!\nMaybe later",
    "sl_inn_go", "<p>\nHeh heh! Close your eyes,\nBro. Picture it...\n<k>",
    "sl_inn_later", "<p>\nSuit yourself. The offer\nstands!\n<k>",

    /* prologue */
    "sl0_alone", "Mario's off on another big\nadventure. Left behind again!\n<k>\n<p>\nOh well. More snacks for me.\n<k>",
    "sl0_paretta", "Special delivery! Long way from\nhome, this one. Smells like...\nsyrup?\n<k>",
    "sl0_mail_here", "Mail? For us?\n<wait 300>Nobody writes to us.\n<k>",
    "sl0_mail_open", "There's a letter in here...\nand it's got a royal seal on it!\n<k>",
    "sl0_letter", "\"To the brave Mario Brothers:\n<k>\n<p>\n\"I write from the Waffle Kingdom,\nfar across the sea. Our princess,\nEclair, has been taken!\n<k>\n<p>\n\"The villain is the Chestnut King.\nHe struck in the night and\nvanished without a trace.\n<k>\n<p>\n\"We have no hero of our own.\nPlease, come at once.\n<k>\n<p>\n\"Your humble servant,\nCrepe, Minister of the Crown\"\n<k>",
    "sl0_after_letter", "A princess... kidnapped!\nAnd Mario's not here!\n<k>\n<p>\nWhich means...\n<wait 300>it's up to me!\n<k>",
    "sl0_note", "Better leave Mario a note,\nso he doesn't worry. Or eat my\nsnack. Mostly the snack.\n<k>",
    "sl0_road_town", "The road to town. Mario went\nthat way. MY way's the pipe\nout back!\n<k>",
    "sl0_pipe_hint","The pipe out back goes all\nkinds of places. Waffle\nKingdom, here I come!\n<k>",
    "sl0_road_arrive", "Whew! So that's the Waffle\nKingdom. Even the air smells\nlike somebody's making brunch.\n<k>",
    "sl0_cadet_1", "Halt! Who goes... oh, thank\ngoodness. You're the plumber!\nThe minister said you'd come.\n<k>\n<p>\nCadet Scone, Royal Guard.\nThe castle's up this road, past\nGriddle and the Maple Wood.\n<k>",
    "sl0_cadet_luigi", "Great! Is it far? Is it safe?\nNot that I'm scared. I just\nlike to know these things.\n<k>",
    "sl0_cadet_2", "There are Goombas on the road\nsince the princess was taken.\nTouch one, and it's a fight.\n<k>\n<p>\nPick Jump or Hammer. Then press\n<icon PAD_A 0.6 0 22 0> just as you land for a second\nstomp! That's an Action Command.\n<k>\n<p>\nHammer one in the field first,\nand you strike before it can!\nWhen it swings, press <icon PAD_A 0.6 0 22 0> to guard.\n<k>",
    "sl0_stones", "Two old stones with holes in\nthem. Looks like somebody lost\nthe bits that go in there.\n<k>\n<p>\nNot my problem! I've got a\nprincess to find.\n<k>",
    "sl0_town_arrive", "A real town! Hey, there's a shop\nand an inn. If this quest takes\nan hour, I'm having a nap first.\n<k>",
    "sl0_town_baker", "Welcome to Griddle, stranger!\nWe've been out of syrup since\nthat Chestnut King came by.\n<k>\n<p>\nHe took the princess AND the\nsyrup. Honestly, I'm not sure\nwhich hit us harder.\n<k>",
    "sl0_town_gossip", "The minister writes to everyone,\nyou know. Heroes, wizards, one\nvery confused goat.\n<k>\n<p>\nYou're the first who came. Odd,\nhow he knew which of them would.\nClever man, that Crepe!\n<k>",
    "sl0_town_kid", "You're a hero? But you're the\ngreen one! My book says the\nhero's the red one.\n<k>\n<p>\nIs he coming later?\n<k>",
    "sl0_town_old", "The wood's bridge? Flat as a\npancake, son. Has been for years.\n<k>\n<p>\nThey say a bush there's got a\nsecret. Give every bush a good\nlook. A hammer helps, too.\n<k>",
    "sl0_town_widow", "Come in, come in. You look\nhungry. Heroes always look\nhungry, and they never stay.\n<k>\n<p>\nBe careful at the castle, dear.\nThose bony brutes have been\nseen on the road after dark.\n<k>",
    "sl0_wood_ranger", "River's too wide to jump, friend.\nThe bridge only stands up if you\nhit the switch on the far path.\n<k>\n<p>\nHow do you reach the far path?\nWell. Try that big bush by the\nwater. I've said too much.\n<k>",
    "sl0_wood_bridge", "Ha! Look at that! Luigi, you\nmagnificent plumber. Somebody\nshould really write this down.\n<k>",
    "sl0_gate_guard", "The minister's waiting in the\nhall, through that door.\n<k>\n<p>\nHe's been expecting you. Since\nbefore the letter went out, if\nyou ask me. Go on in!\n<k>",
    "sl0_crash_guard", "Minister! They're through the\nfar doors! It's the Chestnut\nKing's raiders!\n<k>",
    "sl0_raider_1", "Heh heh heh. The base, old man.\nThe King wants the set, and\nthat's the last piece of it.\n<k>",
    "sl0_crepe_calm", "How very... unfortunate.\nMaster Luigi, if you'd be so\nkind? I'll just stand back here.\n<k>",
    "sl0_luigi_fight", "Me? Now? I just got here!\nOkay. Okay! Let's-a... let's-a\ngo, I guess!\n<k>",
    "sl0_no_run", "Running won't help, Master Luigi!\nThey're between you and the door!\n<k>",
    "sl0_after_luigi", "I did it! Did you see that?\nI hope somebody saw that.\n<k>",
    "sl0_after_crepe", "Splendid. Splendid! Stronger\nthan you look, the green one.\n<k>\n<p>\nAnd now, the base is yours.\nBetter you than them.\n<k>",
    "sl0_crepe_aside", "<scale 0.7>Yes... he'll do nicely.</scale>\n<k>",
    "sl0_mail_empty", "No more mail. Just the one\nletter that changed my life!\n<k>",
    "sl0_castle_arrive", "Wow... so this is the Waffle\nKingdom's castle. It smells\nlike breakfast in here!\n<k>",
    "sl0_crepe_1", "Ah! At last! You must be one of\nthe famous brothers!\n<k>\n<p>\nAnd you are...\n<wait 300>the green one.\n<k>",
    "sl0_luigi_1", "Luigi! The name's Luigi!\nI got your letter, Mr. Minister.\nWhat happened to the princess?\n<k>",
    "sl0_crepe_2", "The Chestnut King took her, and\nno one knows where. But our\noracle spoke of a way.\n<k>\n<p>\nThe Marvelous Compass! A curse\nbroke it into seven parts, and\nscattered them over the land.\n<k>\n<p>\nEach part points to the next.\nAnd the last one is set in the\nprincess's own tiara!\n<k>",
    "sl_luigi_2_unused", "",
    "sl0_luigi_2", "So if I find all the parts...\nI find Princess Eclair!\nLeave it to me!\n<k>",
    "sl0_crepe_3", "Then take this. The compass's\nbase: all that we have left.\n<k>",
    "sl0_base", "<system>\nLuigi got the Compass Base!\n<k>",
    "sl0_crepe_4", "It glows! It points... south!\nTo the Pudding Continent, and\nRumblebump Volcano!\n<k>\n<p>\nBe careful, Master Luigi.\nThe volcano is guarded by\nthings older than our kingdom.\n<k>",
    "sl0_luigi_3", "A volcano, huh? No problem!\n<wait 300>Probably. Here goes!\n<k>",

    /* chapter 1 */
    "sl1_ferry", "...And there goes the ferry.\nNext one back's not for a WEEK.\n<k>\n<p>\nNo turning back now, Luigi!\n<k>",
    "sl1_dock_sign", "<system>\n\"FERRY DOCK.\nNext sailing: one week.\"\n<k>",
    "sl1_dock_back", "Nothing down there but an\nempty dock. The volcano's\nthe other way!\n<k>",
    "sl1_arrive", "The Pudding Continent!\nThe compass points right past\nthat jungle. Let's-a go!\n<k>\n<p>\nSomebody around here must know\nthe way to the volcano...\n<k>",
    "sl1_blooey_1", "Whoa, whoa! Were YOU the guy\nscreaming on that ship?\nThey heard you all the way here!\n<k>",
    "sl1_luigi_1", "I wasn't screaming! I was...\nscaring off sea monsters.\nI'm Luigi. I'm on a quest!\n<k>",
    "sl1_blooey_2", "A quest! Name's Blooey. Back\nhome they call me the White\nTorpedo. I'm a pretty big deal.\n<k>\n<p>\nRumblebump Volcano, huh?\nLava, traps, a big statue that\nstomps on folks... Sounds GREAT.\n<k>",
    "sl1_luigi_2", "It does? I mean... it does!\nWant to come along?\n<k>",
    "sl1_blooey_3", "Do I! A hero needs a sidekick.\nI'll be the hero, you can be...\nkidding! Let's go, man!\n<k>",
    "sl1_joined", "<system>\nBlooey joined your party!\n<k>",
    "sl1_blooey_swim", "Lava's no problem for the White\nTorpedo. Stand at the edge and\npress <icon PAD_X 0.6 0 22 0>: I'll swim you over!\n<k>",
    "sl1_kettle", "A kettle on a campfire. Whoever\nleft it is a trusting soul.\n<k>\n<p>\nI'd have some, but last time I\nburned my mustache. Moving on!\n<k>",
    "sl1_bucket", "A bucket of seawater. Nothing\nin it. I checked twice.\nA plumber can tell.\n<k>",
    "sl1_chair", "A deck chair. Now THIS is how\nto run a quest. <wait 300>Five minutes...\n<k>\n<p>\nNo! Princess first. Lounging\nlater. Probably.\n<k>",
    "sl1_volcano", "Rumblebump Volcano! It's\nso hot my mustache is\ncurling!\n<k>",
    "sl1_volcano_bl", "That's lava, man. Don't step in\nit! Stand at the edge and\npress <icon PAD_X 0.6 0 22 0>. I'll get us across.\n<k>",
    "sl_lava_hot", "Hot! Hot! HOT!\n<k>",
    "sl1_statue_see", "Wh-what is THAT?\n<wait 300>A statue. Just a statue.\nStatues don't move.\n<k>",
    "sl1_statue_bl", "It moved, man. It's moving!\n<k>",
    "sl1_statue_luigi", "It's a hundred feet tall!\nAnd made of rock! How do we\nbeat THAT?\n<k>",
    "sl1_statue_bl2", "See the red gem on its head?\nThat's its weak spot! When it\nstops to rest, throw me at it!\n<k>",
    "sl1_after_bl", "Bull's-eye! Did you see that?\nThe White Torpedo strikes!\n<k>\n<p>\nLook, a chest! Right where it\nwas standing!\n<k>",
    "sl1_piece", "<system>\nLuigi found a piece of the\nMarvelous Compass!\n<k>",
    "sl1_piece_luigi", "It fits the base! Two parts\ndown, five to go!\n<k>",
    "sl1_compass_west", "Now it's pointing west...\nPlumpbelly Village, on the\nStrudel Continent!\n<k>",
    "sl1_tag_1", "So when we tell this one, here's\nhow it went: the statue came\nat ME, and I said, \"Nope!\"\n<k>\n<p>\nThen I hurled LUIGI at the gem,\nand he hit it with his face.\nTotally heroic. For him.\n<k>",
    "sl1_tag_2", "That's not what happened!\n<k>",
    "sl1_tag_3", "Heh heh. Nobody back home will\nknow the difference, man.\n<k>",

    /* the chapters' diary pages, as the curtain closes */
    "sl0_diary", "And so Luigi set off from the\nWaffle Kingdom with the compass's\nbase, and a lot of nerve.\n<k>\n<p>\nHis very own adventure had\nbegun! Mario, of course, had\nno idea.\n<k>",
    "sl1_diary", "The statue of Rumblebump Volcano\nlay in pieces, and the Marvelous\nCompass had its second part.\n<k>\n<p>\nIts needle swung west, toward\nPlumpbelly Village. Blooey was\nalready telling it wrong.\n<k>",

    /* the statue's fight */
    "sl_un_statue", "Stone Statue",
    "sl_un_gem", "Red Gem",
    "sl_bt_gem_open", "The statue stops to rest...\nThe gem on its head glows!\n<k>",
    "sl_bt_gem_shut", "The statue rears up!\nIts arm covers the gem!\n<k>",
    "sl_bt_angry", "The statue is shaking with\nrage! It stomps faster!\n<k>",
    "sl_bt_hint", "That's it, man! Throw me at\nthe gem! Pick \"Blooey Toss\"!\n<k>",
    "sl_bt_stagger", "The statue staggers!\nIts arm slips off the gem!\n<k>",
    "sl_bt_stagger_hint", "You rocked it, man! Quick,\nthrow me at the gem before it\ngets its guard back up!\n<k>",
    "sl_bt_finish", "The gem is cracking! Finish it:\nBlooey Toss!",
    "sl_bl_toss", "Blooey Toss",
    "sl_bl_toss_h", "Blooey rockets up in a high arc\nat one foe, reaching where no\njump or hammer can.",
    NULL,
};
