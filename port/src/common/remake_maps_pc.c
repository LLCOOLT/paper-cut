/* The remake's map changes (port/include/port/remake_maps.h, docs/REMAKE_MAPS.md).
 *
 * Everything is placed from evt_bero_get_info, which every room's init script
 * calls with its entrance table in LW(0): the room's new things (a table
 * below, by map, each under its setting and the story's flags) are a built-in
 * set of map edits (port/include/port/map_edits.h), placed by their runtime as
 * a mod's are, and an exit or two is pointed elsewhere in the table itself.
 *
 * What a room gets:
 *   pipes        a green pipe (the disc's MOBJ_GreenPipe_ks, a field mobj
 *                with a solid block's collision) that Mario goes down by
 *                holding the stick down on it, as the rooms' own pipes, to an
 *                existing entrance of another room. One way.
 *   Save Blocks  the game's own (evt_mobj_save_blk)
 *   Heart Blocks the game's own (evt_mobj_recovery_blk), a few coins a use
 * The sewers' warp pipe is the Director's Cut pipe rooms' last one (tik_14's
 * side pipe, which otherwise leads back to tik_06): going in asks where to,
 * the plaza and each chapter's hub once its Crystal Star is in.
 *
 * No state of our own but the room load things were last placed for: what's
 * placed is game state (mobjs, NPCs, case events), placed again as the room
 * loads. */
#include <stdio.h>
#include <string.h>
#include "port/remake_maps.h"
#include "port/fast_travel.h"
#include "port/map_edits.h"
#include "port/settings.h"
#include "port/tr.h"
#include "version.h"
#include "system.h"
#include "driver/animdrv.h"
#include "driver/casedrv.h"
#include "driver/fadedrv.h"
#include "driver/mobjdrv.h"
#include "driver/msgdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_bero.h"
#include "evt/evt_case.h"
#include "evt/evt_cmd.h"
#include "evt/evt_fade.h"
#include "evt/evt_hit.h"
#include "evt/evt_badgeshop.h"
#include "evt/evt_pouch.h"
#include "evt/evt_window.h"
#include "driver/npcdrv.h"
#include "evt/evt_mario.h"
#include "evt/evt_mobj.h"
#include "evt/evt_msg.h"
#include "evt/evt_npc.h"
#include "evt/evt_snd.h"
#include "evt/evt_story.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"

/* --- the settings --- */

static const char* const kKeys[PORT_RM_COUNT] = {
    "remake_warp_pipes", "remake_shortcut_pipes", "remake_express_skip", "remake_chapter_shortcuts",
    "remake_keelhaul",   "remake_steeple_stairs", "remake_twilight",     "remake_blocks",
    "remake_ambushes",   "remake_merchants",
};
static int* sOn[PORT_RM_COUNT];

int portRemakeMap(int which) {
    if (which < 0 || which >= PORT_RM_COUNT) {
        return 0;
    }
    if (sOn[which] == NULL) {
        int i;
        for (i = 0; i < PORT_RM_COUNT; i++) {
            sOn[i] = portSettingsDeclare(kKeys[i], 0, 0, 1);
        }
    }
    return *sOn[which];
}

const char* portRemakeMapKey(int which) {
    return which >= 0 && which < PORT_RM_COUNT ? kKeys[which] : "";
}

/* --- the rooms' new things --- */

enum { KIND_PIPE, KIND_SAVE, KIND_HEART, KIND_JUMP };

#define FLAG_KEELHAUL_STANDS (-2) /* Thing.flag: PORT_RM_GSW_KEELHAUL_STANDS set */

typedef struct Thing {
    const char* map;
    signed char which; /* PORT_RM_* */
    signed char kind;  /* KIND_* */
    short x, y, z;
    short story;   /* GSW_STORY_PROGRESS at least (0: any) */
    short flag;    /* a GSWF that must be set (-1: none; FLAG_KEELHAUL_STANDS: the save's switch for them) */
    const char* to;     /* a pipe's room */
    const char* toBero; /* and the entrance there */
    short arg;          /* a Heart Block's price, a jump stand's power */
} Thing;

/* Places are from the rooms' own data: the disc's collision (its walkable
 * floors and walls) and the scripts' items, chests and blocks. */
static const Thing kThings[] = {
    /* --- the remake's shortcut pipes --- */
    /* Hooktail Castle: where Last Stand P was, at the top of the tall stair hall, once it's taken; down
     * to the castle's gate courtyard (gon_01, in from the entrance) */
    {"gon_05", PORT_RM_SHORTCUT_PIPES, KIND_PIPE, 215, 915, 150, 0, GSWF_GON05_LAST_STAND_P_TAKEN, "gon_01",
     "w_bero_1", 0},
    /* the Creepy Steeple's basement (the parrot's room; front left, clear of the perch and the Star Piece
     * panel) to Twilight Town (its pipe from the sewers) */
    {"jin_08", PORT_RM_SHORTCUT_PIPES, KIND_PIPE, -60, 0, 115, 0, -1, "usu_00", "dokan_1", 0},
    /* the Palace of Shadow: the hall before the throne rooms (by its Save Block) back to the gate */
    {"las_27", PORT_RM_SHORTCUT_PIPES, KIND_PIPE, 500, 0, 100, 0, -1, "las_00", "e_bero", 0},

    /* --- chapter shortcuts: the long walks each chapter makes more than once --- */
    /* chapter 4, Twilight Town and the Creepy Steeple (the whole Twilight Trail, every trip after Doopliss):
     * by the town's east gate, out to the Trail's end (in from gra_05), and back by its Save Block; once
     * the Steeple has been reached */
    {"usu_01", PORT_RM_CHAPTER_SHORTCUTS, KIND_PIPE, 330, 0, 130, STORY_STG4_08_STEEPLE_DEMO_EXT, -1, "gra_06",
     "sw_bero", 0},
    {"gra_06", PORT_RM_CHAPTER_SHORTCUTS, KIND_PIPE, -220, 0, 160, STORY_STG4_08_STEEPLE_DEMO_EXT, -1, "usu_01",
     "e_bero", 0},
    /* (chapter 5's is Keelhaul Key's jump stands, chapter 6's the Excess Express riding past Riverside) */
    /* chapter 8, the riddle tower's hub back to the palace's gate (the eight rooms' keys mean many trips
     * past the first halls) */
    {"las_10", PORT_RM_CHAPTER_SHORTCUTS, KIND_PIPE, -150, 0, 250, 0, -1, "las_00", "e_bero", 0},

    /* --- Keelhaul Key: a jump stand each way between the castaways' camp and Skull Rock (jungle, path,
     * bridge; Flavio's favor and Marco's gem send Mario back and forth), once Bobbery has joined at the
     * rock: it throws Mario up and over to the other end. The camp's is switched on by the first jump from
     * Skull Rock's (the save's PORT_RM_GSW_KEELHAUL_STANDS): Mario opens the shortcut from its far end --- */
    {"muj_01", PORT_RM_KEELHAUL, KIND_JUMP, 320, 0, -20, STORY_STG5_18_01_BOBBERY_JOINS, FLAG_KEELHAUL_STANDS, "muj_05",
     "w_bero", 130},
    {"muj_05", PORT_RM_KEELHAUL, KIND_JUMP, 20, 0, -110, STORY_STG5_18_01_BOBBERY_JOINS, -1, "muj_01", "e_bero1",
     130},

    /* --- the remake's new Save and Heart Blocks (60 above the floor, as the game's own) --- */
    /* Boggly Woods: a Heart Block by the Save Block at the sewer pipe */
    {"win_06", PORT_RM_BLOCKS, KIND_HEART, 205, 60, -50, 0, -1, NULL, NULL, 8},
    /* Twilight Trail: both, halfway along */
    {"gra_03", PORT_RM_BLOCKS, KIND_SAVE, -30, 60, -100, 0, -1, NULL, NULL, 0},
    {"gra_03", PORT_RM_BLOCKS, KIND_HEART, 40, 60, -100, 0, -1, NULL, NULL, 10},
    /* the Creepy Steeple: a Heart Block by the basement's Save Block */
    {"jin_11", PORT_RM_BLOCKS, KIND_HEART, -5, 60, -40, 0, -1, NULL, NULL, 10},
    /* the Pirate's Grotto: a Heart Block by the entrance's Save Block (to its left, on the lower floor: clear
     * of the stalactites) */
    {"dou_00", PORT_RM_BLOCKS, KIND_HEART, -45, 60, -75, 0, -1, NULL, NULL, 12},
    /* the Excess Express: a Heart Block by the engineer's cab's Save Block (each trip's copy of the car) */
    {"rsh_07_a", PORT_RM_BLOCKS, KIND_HEART, 140, 60, -145, 0, -1, NULL, NULL, 15},
    {"rsh_07_b", PORT_RM_BLOCKS, KIND_HEART, 140, 60, -145, 0, -1, NULL, NULL, 15},
    {"rsh_07_c", PORT_RM_BLOCKS, KIND_HEART, 140, 60, -145, 0, -1, NULL, NULL, 15},
    /* Riverside Station: both, in the hall with the stairs */
    {"eki_01", PORT_RM_BLOCKS, KIND_SAVE, -150, 60, 50, 0, -1, NULL, NULL, 0},
    {"eki_01", PORT_RM_BLOCKS, KIND_HEART, -80, 60, 50, 0, -1, NULL, NULL, 15},
    /* the X-Naut Fortress: both, before the door with the three locks */
    {"aji_18", PORT_RM_BLOCKS, KIND_SAVE, -80, 60, 40, 0, -1, NULL, NULL, 0},
    {"aji_18", PORT_RM_BLOCKS, KIND_HEART, 80, 60, 40, 0, -1, NULL, NULL, 20},
    /* the Palace of Shadow: a Heart Block by the fire-bar hall's Save Block, and both in the hall after the
     * riddle tower (on its floor at the foot of the stairs, right of the ? Block) */
    {"las_04", PORT_RM_BLOCKS, KIND_HEART, -950, 140, 100, 0, -1, NULL, NULL, 10},
    {"las_19", PORT_RM_BLOCKS, KIND_SAVE, 130, -480, 20, 0, -1, NULL, NULL, 0},
    {"las_19", PORT_RM_BLOCKS, KIND_HEART, 200, -480, 20, 0, -1, NULL, NULL, 10},
};
#define THINGS ((int)(sizeof(kThings) / sizeof(kThings[0])))

/* The table as a built-in set of map edits (port/include/port/map_edits.h):
 * each thing under its setting and the story's flags; the map edits' runtime
 * places them as any mod's. */
static void builtinSet(void) {
    static const char* const kFences[] = {"a_kaidan_saku01", "a_kaidan_saku02", "a_kaidan_saku03"};
    static const int kTypes[] = {PORT_MT_PIPE, PORT_MT_SAVE_BLOCK, PORT_MT_HEART_BLOCK, PORT_MT_JUMP_STAND};
    static int sDone;
    PortMapThing m;
    int i;
    if (sDone) {
        return;
    }
    sDone = 1;
    for (i = 0; i < THINGS; i++) {
        const Thing* t = &kThings[i];
        portMapThingInit(&m, kTypes[t->kind]);
        m.pos[0] = t->x;
        m.pos[1] = t->y;
        m.pos[2] = t->z;
        m.setting = t->which;
        m.story = t->story;
        if (t->flag == FLAG_KEELHAUL_STANDS) {
            m.gsw = PORT_RM_GSW_KEELHAUL_STANDS; /* the save's switch for them */
            m.gswMin = 1;
        } else {
            m.gswf = t->flag;
        }
        snprintf(m.to, sizeof(m.to), "%s", t->to != NULL ? t->to : "");
        snprintf(m.entrance, sizeof(m.entrance), "%s", t->toBero != NULL ? t->toBero : "");
        m.arg = t->arg;
        if (t->which == PORT_RM_KEELHAUL && strcmp(t->map, "muj_05") == 0) {
            m.usesGsw = PORT_RM_GSW_KEELHAUL_STANDS; /* (the camp's stand switched on by the first jump) */
            m.usesValue = 1;
        }
        portMapEditsAddBuiltin("remake", t->map, &m);
    }
    /* the Creepy Steeple's stairs: jin_03's three stairs (one stands at a time, by the switches in jin_01
     * and jin_02) each have a fence of collision along both sides (as tall as the room): off, Mario can
     * jump down from them to the floor */
    for (i = 0; i < 3; i++) {
        portMapThingInit(&m, PORT_MT_HIDE_COLLISION);
        m.setting = PORT_RM_STEEPLE_STAIRS;
        snprintf(m.target, sizeof(m.target), "%s", kFences[i]);
        portMapEditsAddBuiltin("remake", "jin_03", &m);
    }
}

/* --- the sewers' warp pipe --- */

static const struct {
    const char* area; /* the Journal's name for it: menu_monosiri_<area> */
    const char* map;
    const char* bero;
} kHubs[] = {
    {"gor", "gor_01", "s_bero"},     /* Rogueport's plaza, in from the harbor */
    {"nok", "nok_01", "dokan_1"},    /* Petalburg's pipe */
    {"win", "win_06", "dokan1"},     /* Boggly Woods' */
    {"tou", "tou_01", "a_door_mon"}, /* Glitzville: out of the Glitz Pit's door */
    {"usu", "usu_00", "dokan_1"},    /* Twilight Town's pipe */
    {"muj", "muj_02", "dokan_1"},    /* Keelhaul Key's */
    {"pik", "pik_01", "dokan_1"},    /* Poshley Heights' */
    {"bom", "bom_00", "dokan"},      /* Fahr Outpost's */
};
#define HUBS ((int)(sizeof(kHubs) / sizeof(kHubs[0])))

/* the plaza always; a chapter's hub once its Crystal Star is in, and while the story lets Mario arrive there as
 * fast travel does (port/include/port/fast_travel.h: the Sapphire Star is in hand from STG5_59, Keelhaul Key
 * open to sail to only from STG5_63; Poshley Heights while the train's chapter runs) */
static int hubLit(int i) {
    return i == 0 ||
           (pouchCheckItem((s16)(ITEM_DIAMOND_STAR + i - 1)) != 0 && portTravelStoryLets(kHubs[i].map));
}

/* the lit hubs, in order (the choice's rows), at most 7 and Stay here (a choice has 8 rows) */
static int litHubs(int* out) {
    int i, n = 0;
    for (i = 0; i < HUBS && n < 7; i++) {
        if (hubLit(i)) {
            out[n++] = i;
        }
    }
    return n;
}

/* The exit script of tik_14's side pipe: LW(0) 0 goes, with LW(13)/LW(14)
 * set, 1 stays (bero_case_init_check). */
static USER_FUNC(evt_rm_warp_pick) {
    int lit[HUBS];
    const int n = litHubs(lit);
    const int pick = (int)evtGetValue(event, event->args[0]);
    (void)isFirstCall;
    if (pick < 0 || pick >= n) {
        event->lwData[0] = 1;
        return EVT_RETURN_DONE;
    }
    event->lwData[13] = (EvtWord)kHubs[lit[pick]].map;
    event->lwData[14] = (EvtWord)kHubs[lit[pick]].bero;
    event->lwData[0] = 0;
    printf("[remake] warp pipe: %s\n", kHubs[lit[pick]].map);
    fflush(stdout);
    return EVT_RETURN_DONE;
}

static EVT_BEGIN(rm_warp_out)
    CALL(portEvtRemakeText, PORT_RM_TEXT_WARP_ASK, LW(12))
    CALL(evt_msg_print, MSG_PRINT_DIRECT, LW(12), 0, 0)
    CALL(portEvtRemakeText, PORT_RM_TEXT_WARP_SELECT, LW(12))
    CALL(evt_msg_select, MSG_PRINT_DIRECT, LW(12))
    CALL(evt_msg_continue)
    CALL(evt_rm_warp_pick, LW(0))
    RETURN
EVT_END()

/* tik_14's entrances: its side pipe (dokan_1, back to tik_06 otherwise)
 * asks where to while the setting is on */
static void warpPipe(BeroEntry* table) {
    BeroEntry* e;
    for (e = table; e != NULL && e->hitName != NULL; e++) {
        if (strcmp(e->hitName, "dokan_1") == 0) {
            e->exitEvent = portRemakeMap(PORT_RM_WARP_PIPES) ? rm_warp_out : NULL; /* (the REL's is NULL) */
        }
    }
}

/* --- Charlieton in Boggly Woods, a Toad at the end of the Pirate's Grotto --- */

/* Charlieton outside the Great Tree by its pipe from the sewers (mri_00), once
 * chapter 2 is over: his own stock, the one he sells from in Rogueport's
 * plaza (the badge shop's bargain slots, evt_win_other_select 14) */
#define CHARLIETON "rm_charlieton"
/* the Toad on the hidden dock past the Grotto (muj_10), once Mario has come
 * out there: supplies for Cortez (the badge shop's window over our list,
 * PORT_SELECT_REMAKE_TOAD) */
#define TOAD "rm_toad"

static const s32 kToadStock[] = {ITEM_SUPER_SHROOM, ITEM_MAPLE_SYRUP, ITEM_LIFE_SHROOM, ITEM_FIRE_FLOWER,
                                 ITEM_VOLT_SHROOM, ITEM_SLEEPY_SHEEP};

int portRemakeToadStock(int* out, int max) {
    int i, n = portMapEditsShopStock(out, max); /* (a map edit's shop NPC's window, the same one) */
    if (n >= 0) {
        return n;
    }
    n = 0;
    for (i = 0; i < (int)(sizeof(kToadStock) / sizeof(kToadStock[0])) && n < max; i++) {
        out[n++] = kToadStock[i];
    }
    return n;
}

/* (text, out): our line, printed by the talking NPC */
#define SAY(which)                                       \
    CALL(portEvtRemakeText, which, LW(10))               \
    CALL(evt_msg_print, MSG_PRINT_DIRECT, LW(10), 0, STRING("me"))

static EVT_BEGIN(rm_charlieton_talk)
#if VERSION >= VERSION_JP
    CALL(U_evt_badgeShop_get_special_zaiko, LW(0))
    IF_INT_EQUAL(LW(0), 0)
        SAY(PORT_RM_TEXT_CHARLIETON_SOLDOUT)
        RETURN
    END_IF
#endif
    CALL(evt_badgeShop_bottakuru_get_kind_cnt, LW(0))
    IF_INT_LESS_OR_EQUAL(LW(0), 0)
        SAY(PORT_RM_TEXT_CHARLIETON_SOLDOUT)
        RETURN
    END_IF
    SAY(PORT_RM_TEXT_CHARLIETON_HELLO)
    LABEL(0)
    CALL(evt_win_coin_on, 0, LW(12))
    CALL(evt_win_other_select, 14)
    IF_INT_EQUAL(LW(0), 0)
        CALL(evt_win_coin_off, LW(12))
        SAY(PORT_RM_TEXT_CHARLIETON_BYE)
        RETURN
    END_IF
    CALL(evt_pouch_get_coin, LW(0))
    IF_INT_LESS(LW(0), LW(3))
        CALL(evt_win_coin_off, LW(12))
        SAY(PORT_RM_TEXT_CHARLIETON_POOR)
        GOTO(0)
    END_IF
    CALL(evt_pouch_add_item, LW(1), LW(0))
    IF_INT_EQUAL(LW(0), -1)
        CALL(evt_win_coin_off, LW(12))
        SAY(PORT_RM_TEXT_CHARLIETON_FULL)
        RETURN
    END_IF
    CALL(evt_badgeShop_bottakuru_dec, LW(1))
    MULTIPLY(LW(3), -1)
    CALL(evt_pouch_add_coin, LW(3))
    CALL(evt_win_coin_wait, LW(12))
    CALL(evt_win_coin_off, LW(12))
    CALL(evt_badgeShop_bottakuru_get_kind_cnt, LW(0))
    IF_INT_LESS_OR_EQUAL(LW(0), 0)
        SAY(PORT_RM_TEXT_CHARLIETON_SOLDOUT)
        RETURN
    END_IF
    SAY(PORT_RM_TEXT_CHARLIETON_THANKS)
    GOTO(0)
    RETURN
EVT_END()

static EVT_BEGIN(rm_toad_talk)
    SAY(PORT_RM_TEXT_TOAD_HELLO)
    LABEL(0)
    CALL(evt_win_coin_on, 0, LW(12))
    CALL(evt_win_other_select, PORT_SELECT_REMAKE_TOAD)
    IF_INT_EQUAL(LW(0), 0)
        CALL(evt_win_coin_off, LW(12))
        SAY(PORT_RM_TEXT_TOAD_BYE)
        RETURN
    END_IF
    CALL(evt_pouch_get_coin, LW(0))
    IF_INT_LESS(LW(0), LW(3))
        CALL(evt_win_coin_off, LW(12))
        SAY(PORT_RM_TEXT_TOAD_POOR)
        GOTO(0)
    END_IF
    CALL(evt_pouch_add_item, LW(1), LW(0))
    IF_INT_EQUAL(LW(0), -1)
        CALL(evt_win_coin_off, LW(12))
        SAY(PORT_RM_TEXT_TOAD_FULL)
        RETURN
    END_IF
    MULTIPLY(LW(3), -1)
    CALL(evt_pouch_add_coin, LW(3))
    CALL(evt_win_coin_wait, LW(12))
    CALL(evt_win_coin_off, LW(12))
    SAY(PORT_RM_TEXT_TOAD_THANKS)
    GOTO(0)
    RETURN
EVT_END()
#undef SAY

static NpcSetupInfo sCharlietonSetup[] = {
    {CHARLIETON, NPC_FLAG_SHADOW_NO_COLLISION | NPC_FLAG_MOVE_ANIM | NPC_FLAG_TALK_ANIM, 0, NULL, NULL,
     rm_charlieton_talk},
    {NULL},
};
static NpcSetupInfo sToadSetup[] = {
    {TOAD, NPC_FLAG_SHADOW_NO_COLLISION | NPC_FLAG_MOVE_ANIM | NPC_FLAG_TALK_ANIM, 0, NULL, NULL, rm_toad_talk},
    {NULL},
};

/* LW(0) name, LW(1) model, LW(2) tribe (Shift-JIS, as the game's), LW(3..5) where, LW(6) facing, LW(7) setup */
static EVT_BEGIN(rm_merchant_spawn)
    CALL(evt_npc_entry, LW(0), LW(1))
    CALL(evt_npc_set_tribe, LW(0), LW(2))
    CALL(evt_npc_set_position, LW(0), LW(3), LW(4), LW(5))
    CALL(evt_npc_set_ry, LW(0), LW(6))
    CALL(evt_npc_setup, LW(7))
    CALL(evt_npc_set_anim, LW(0), STRING("S_1"))
    RETURN
EVT_END()

static void merchant(const char* name, const char* model, const char* tribe, int x, int y, int z, int ry,
                     NpcSetupInfo* setup) {
    EventEntry* e;
    if (npcNameToPtr_NoAssert(name) != NULL) {
        return;
    }
    e = evtEntry(rm_merchant_spawn, 0, 0);
    e->lwData[0] = (EvtWord)name;
    e->lwData[1] = (EvtWord)model;
    e->lwData[2] = (EvtWord)tribe;
    e->lwData[3] = x;
    e->lwData[4] = y;
    e->lwData[5] = z;
    e->lwData[6] = ry;
    e->lwData[7] = (EvtWord)setup;
    printf("[remake] %s: %s\n", gp->currentMapName, name);
    fflush(stdout);
}

static void merchants(const char* map) {
    if (!portRemakeMap(PORT_RM_MERCHANTS)) {
        return;
    }
    /* ボッタクール, キノピオ */
    if (strcmp(map, "mri_00") == 0 && swByteGet(GSW_STORY_PROGRESS) >= STORY_STG2_44_FINDING_DON_PIANTA) {
        merchant(CHARLIETON, "c_botta", "\x83\x7B\x83\x62\x83\x5E\x83\x4E\x81\x5B\x83\x8B", 330, 40, 330, 90, /* (y: the ground there) */
                 sCharlietonSetup);
    }
    if (strcmp(map, "muj_10") == 0 && swByteGet(GSW_STORY_PROGRESS) >= STORY_STG5_32_CORTEZS_WARNING) {
        merchant(TOAD, "c_kinopio", "\x83\x4C\x83\x6D\x83\x73\x83\x49", -430, 25, -20, 90, sToadSetup); /* (on the dock: y 25) */
    }
}

/* --- texts --- */

int portEvtRemakeOn(struct EventEntry* event, int isFirstCall) {
    (void)isFirstCall;
    evtSetValue(event, event->args[1], portRemakeMap((int)evtGetValue(event, event->args[0])));
    return EVT_RETURN_DONE;
}

/* the merchants' lines, from PORT_RM_TEXT_CHARLIETON_HELLO on (English, the
 * key in port/assets/lang/<code>/remake-maps.txt; a line at most about 30
 * letters, as the game's own speech) */
static const char* const kSpeech[] = {
    "Well, well! Fancy meeting you\nout here in the woods, pal!<p>I brought a few of my finest\nwares along. Take a look!<k>",
    "Come back anytime, pal!<k>",
    "Whoa there! You're a little\nshort on coins, buddy.<k>",
    "Your hands are full, pal!\nCome back when there's room.<k>",
    "Pleasure doing business!\nAnything else catch your eye?<k>",
    "Sorry, pal, I'm all sold out!\nCatch me in Rogueport later.<k>",
    "Phew, you made it through!\nI've got supplies for the\nrest of the trip.<k>",
    "Good luck out there!<k>",
    "Oh no, you don't have\nenough coins for that.<k>",
    "You can't carry any more!<k>",
    "Thanks! Anything else?<k>",
};

/* the menu language's, in the game's encoding (port/assets/lang/<code>/remake-maps.txt) */
int portEvtRemakeText(struct EventEntry* event, int isFirstCall) {
    static char sText[PORT_RM_TEXT_COUNT][512];
    const int which = (int)evtGetValue(event, event->args[0]);
    char* out;
    size_t n = 0;
    int i;
    (void)isFirstCall;
    if (which < 0 || which >= PORT_RM_TEXT_COUNT) {
        return EVT_RETURN_DONE;
    }
    out = sText[which];
    out[0] = '\0';
    switch (which) {
        case PORT_RM_TEXT_WARP_ASK:
            snprintf(out, sizeof(sText[0]), "%s<o>", portTrGame("Where would you like to go?"));
            break;
        case PORT_RM_TEXT_WARP_SELECT: {
            int lit[HUBS];
            const int count = litHubs(lit);
            /* (its top at 140, as the game's own eight-row list in the Glitz Pit (tou_04): higher, eight rows
             * cover the question's line) */
            n = (size_t)snprintf(out, sizeof(sText[0]), "<select 0 -2 260 140>");
            for (i = 0; i < count && n < sizeof(sText[0]); i++) {
                char tag[24];
                const char* name;
                snprintf(tag, sizeof(tag), "menu_monosiri_%s", kHubs[lit[i]].area);
                name = msgSearch(tag);
                n += (size_t)snprintf(out + n, sizeof(sText[0]) - n, "\n%s", name != NULL ? name : kHubs[lit[i]].map);
            }
            if (n < sizeof(sText[0])) {
                snprintf(out + n, sizeof(sText[0]) - n, "\n%s", portTrGame("Stay here"));
            }
            break;
        }
        case PORT_RM_TEXT_EXPRESS_ASK:
            snprintf(out, sizeof(sText[0]), "%s<o>", portTrGame("Stop at Riverside Station on the way?"));
            break;
        case PORT_RM_TEXT_EXPRESS_SELECT:
            snprintf(out, sizeof(sText[0]), "<select 0 0 260 140>\n%s\n%s", portTrGame("Stop at Riverside"),
                     portTrGame("Ride straight through"));
            break;
        default:
            if (which >= PORT_RM_TEXT_CHARLIETON_HELLO) {
                snprintf(out, sizeof(sText[0]), "%s", portTrGame(kSpeech[which - PORT_RM_TEXT_CHARLIETON_HELLO]));
            }
            break;
    }
    evtSetValue(event, event->args[1], (EvtWord)out);
    return EVT_RETURN_DONE;
}

static s32 sMerchantsFor; /* the room load the merchants were placed for */

/* --- the room --- */

void portRemakeMapsRoom(struct EventEntry* event) {
    BeroEntry* table = (BeroEntry*)event->lwData[0];
    if (gp->inBattle) {
        return;
    }
    builtinSet();
    if (strcmp(gp->currentMapName, "tik_14") == 0) {
        warpPipe(table);
    }
    /* (the merchants once a room load, as the map edits' things) */
    if (gp->mapEventId == 0 || gp->mapEventId != sMerchantsFor) {
        sMerchantsFor = gp->mapEventId;
        merchants(gp->currentMapName);
    }
    portMapEditsRoom(table); /* the map edits: the table above's things, the mods' */
}
