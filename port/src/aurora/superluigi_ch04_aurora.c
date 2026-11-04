/* Super Luigi's chapter 4, "Jazzafrazz Town" (docs/super_luigi/ch4.md):
 * Poshley Heights as Jazzafrazz, Flurrie's house as Hayzee's studio and the
 * diva's dressing room, Twilight Trail as the moonlit Hum-Along Hills. The
 * compass piece is the prize of the Drama Slam; Hayzee casts Luigi as the
 * Grass. Rooms, people, scenes and text; Hayzee is his own file
 * (superluigi_hayzee_aurora.c), the Choir and the Drama Slam another
 * (superluigi_slam_aurora.c).
 *
 * Flags: SLF4(n), the framework's bank (SL_GSWF(4, n), docs/SUPER_LUIGI.md "Flags"). */
#include "port/superluigi.h"
#include "port/superluigi_evt.h"
#include "port/mod_host.h"
#include "port/mods.h"
#include "port/hook_list.h"
#include "port/luigi.h"
#include "data/item_data.h"
#include "driver/npcdrv.h"
#include "driver/msgdrv.h"
#include "manager/fontmgr.h"
#include "driver/swdrv.h"
#include "evt/evt_window.h"
#include "evt/evt_hit.h"
#include "evt/evt_win.h"
#include "battle/battle_unit.h"
#include "mario/mario.h"
#include "mario/mario_party.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "cam_road.h"
#include "driver/hitdrv.h"
#include "party.h"
#include "statuswindow.h"
#include "system.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The game's Japanese names these scripts use, as Shift-JIS (port/include/port/superluigi_sjis.h's way) */
#define SJ4_DAYZEE "\x83" "\x70" "\x83" "\x93" "\x83" "\x57" "\x81" "\x5B" "\x82" "\xB3" "\x82" "\xF1" /* パンジーさん */
#define SJ4_AMAZY "\x82" "\xAB" "\x82" "\xE7" "\x82" "\xDF" "\x82" "\xAD" "\x83" "\x70" "\x83" "\x93" "\x83" "\x57" "\x81" "\x5B" "\x82" "\xB3" "\x82" "\xF1" /* きらめくパンジーさん */
#define SJ4_TOAD "\x83" "\x4C" "\x83" "\x6D" "\x83" "\x73" "\x83" "\x49" /* キノピオ */
#define SJ4_TOADETTE "\x83" "\x4C" "\x83" "\x6D" "\x83" "\x73" "\x83" "\x49" "\x8F" "\x97" /* キノピオ女 */
#define SJ4_KOOPA "\x83" "\x6D" "\x83" "\x52" "\x83" "\x6D" "\x83" "\x52" /* ノコノコ */
#define SJ4_BOO "\x83" "\x65" "\x83" "\x8C" "\x83" "\x54" /* テレサ */
#define SJ4_HGOOMBA "\x83" "\x6E" "\x83" "\x43" "\x83" "\x70" "\x81" "\x5B" "\x83" "\x4E" "\x83" "\x8A" "\x83" "\x7B" "\x81" "\x5B" /* ハイパークリボー */
#define SJ4_HSPIKY "\x83" "\x6E" "\x83" "\x43" "\x83" "\x70" "\x81" "\x5B" "\x83" "\x67" "\x83" "\x51" "\x83" "\x4E" "\x83" "\x8A" /* ハイパートゲクリ */
#define SJ4_HCLEFT "\x83" "\x6E" "\x83" "\x43" "\x83" "\x70" "\x81" "\x5B" "\x83" "\x56" "\x83" "\x93" "\x83" "\x47" "\x83" "\x82" "\x83" "\x93" /* ハイパーシンエモン */
#define SJ4_BOMB "\x83" "\x7B" "\x83" "\x80" "\x95" "\xBA" /* ボム兵 */
#define SJ4_PORTER "\x89" "\x77" "\x88" "\xF5" /* 駅員 */
#define SJ4_RICHPA "\x83" "\x69" "\x83" "\x8A" "\x83" "\x4C" "\x83" "\x93" "\x83" "\x70" "\x83" "\x70" /* ナリキンパパ */
#define SJ4_RICHMA "\x83" "\x69" "\x83" "\x8A" "\x83" "\x4C" "\x83" "\x93" "\x83" "\x7D" "\x83" "\x7D" /* ナリキンママ */
#define SJ4_PIANTA "\x83" "\x7D" "\x83" "\x74" "\x83" "\x42" "\x83" "\x41" /* マフィア */
#define SJ4_MASTER "\x83" "\x7D" "\x83" "\x58" "\x83" "\x5E" "\x81" "\x5B" /* マスター */
#define SJ4_HAYZEE "\x83" "\x89" "\x83" "\x4E" "\x83" "\x4B" "\x83" "\x93" /* ラクガン */
#define SJ4_BUTLER "\x83" "\x5A" "\x83" "\x6F" "\x83" "\x58" "\x83" "\x60" "\x83" "\x83" "\x83" "\x93" /* セバスチャン */
#define SJ4_AI_GOOMBA "\x83" "\x4E" "\x83" "\x8A" "\x83" "\x7B" "\x81" "\x5B" /* クリボー */
#define SJ4_AI_SPIKY "\x83" "\x67" "\x83" "\x51" "\x83" "\x4E" "\x83" "\x8A" /* トゲクリ */
#define SJ4_AI_CLEFT "\x83" "\x56" "\x83" "\x93" "\x83" "\x47" "\x83" "\x82" "\x83" "\x93" /* シンエモン */
#define SJ4_AI_STILL "\x95" "\x73" "\x93" "\xAE" "\x83" "\x4C" "\x83" "\x83" "\x83" "\x89" "\x94" "\xC4" "\x97" "\x70" /* 不動キャラ汎用 */
#define SJ4_JERRY "\x83" "\x60" "\x83" "\x46" "\x83" "\x8A" "\x81" "\x5B" /* チェリー */
#define SJ4_TORQUE "\x83" "\x67" "\x83" "\x8B" "\x83" "\x4E" /* トルク */
#define SJ4_BLOOEY "\x83" "\x4C" "\x83" "\x62" "\x83" "\x4E" /* キック */

/* The models chapter 4's new assets will be (the models agent's, super-luigi-models):
 * until they land, each is used only if the game has it (portModsGetHostFile) */
#define MODEL_GRASS "c_luigi_grass"     /* Luigi as the Grass: a Luigi variant */
#define MODEL_HAT "c_sl_fiery_hat"      /* the Fiery Hat prop */
#define MODEL_LEAF "c_sl_leaf"          /* a fan's paper leaf (W_2: the paper grass) */
#define MODEL_BANNER "c_sl_grass_banner"
#define MODEL_TIARA "c_sl_dazzlina_tiara"
#define MODEL_BARON "c_sl_baron_hat"
#define MODEL_PETUNIA "c_sl_petunia"    /* Petunia: c_pansy with her pink petals */

/* the chapter's own flags */
#define SLF4(n) SL_GSWF(4, n)
enum {
    F_OPENED, F_POSTER, F_HAYZEE, F_CHORUS, F_CAST, F_PAGE1, F_PAGE2, F_PAGE3, F_PAGE4, F_PAGE5,
    F_PAGES, F_HAMMER, F_STONE1, F_BRUNO, F_HAT, F_DAZZLINA, F_CHOIR, F_PETUNIA, F_ECHO_DONE, F_NIGHT,
    F_SLAM, F_FANS, F_DONE, F_BACKSTAGE_OK, F_HAT_HINT, F_ALL_HINT, F_PIANO, F_SIGN,
    F_P40_SP = 30, F_P41_COIN1, F_P41_COIN2, F_P41_HONEY, F_TIP1, F_TIP2, F_TIP3, F_APPEAL,
    F_P42_SP = 39, F_P43_STOMP, F_P44_COIN1, F_P44_COIN2, F_P44_QUAKE, F_P44_STONE2, F_W40_SP, F_W41_SHINE,
    F_W41_FLOWER, F_G41_SHROOM, F_G41_COIN1, F_G41_COIN2, F_G41_STONE, F_G41_FPPLUS, F_THIMBLE, F_LUCKY,
    F_G43_SHINE, F_G44_SP, F_G45_FLOOR, F_G45_PEEK, F_G45_SP, F_REWARD, F_VOICE, F_HZ_COMES, F_THIMBLE_SP,
    F_PATRON, F_G40_COIN, F_P42_HEART_TALK, F_G43_COIN, F_G44_BLK, F_G45_ROCK, F_G45_HINT,
    F_ECHO = 90, /* Hayzee knows Echo (superluigi_hayzee_aurora.c reads it) */
};

USER_FUNC(evt_sl_hz_song);
USER_FUNC(evt_sl_hz_echo);
USER_FUNC(evt_sl_hz_echo_play);
USER_FUNC(evt_sl4_arm);   /* superluigi_slam_aurora.c */
USER_FUNC(evt_sl4_house);
USER_FUNC(evt_luigi_story_pose);
USER_FUNC(evt_luigi_costume);
void portSlHayzeeRegister(void);
void portSlSlamRegister(void);

#define SAY(tag, who) CALL(evt_msg_print, 0, STRING(tag), 0, STRING(who))
#define SYS(tag) CALL(evt_msg_print, 0, STRING(tag), 0, 0)
#define LUIGI(tag) SAY(tag, "mario")
#define KEY_OFF CALL(evt_mario_key_onoff, 0)
#define KEY_ON CALL(evt_mario_key_onoff, 1)
#define BGM(name) CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING(name))
#define NPC(name, model, tribe, x, y, z) \
    CALL(evt_npc_entry, STRING(name), STRING(model)) \
    CALL(evt_npc_set_tribe, STRING(name), STRING(tribe)) \
    CALL(evt_npc_set_position, STRING(name), x, y, z)
#define TALK(name, script) CALL(evt_npc_change_interrupt, STRING(name), 6, PTR(script))
#define ANIM(name, a) CALL(evt_npc_set_anim, STRING(name), STRING(a))
#define FACE(name, deg) CALL(evt_npc_set_ry, STRING(name), deg)
#define BEROS(tbl) \
    SET_VALUE(LW(0), PTR(tbl)) \
    CALL(evt_bero_get_info) \
    CALL_CHILD(evt_bero_info_run)
#define BERO(hit, map, to) {hit, 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, map, to, 1, 1, 0, 0}
#define PIPE(hit, map, to) {hit, 2, 0, BERO_DIR_DOWN, BERO_POS_AUTO, 0, 0, -1, NULL, 6, NULL, map, to, 1, 1, 0, 0}

/* ======================================================================
 * Helpers in C
 * ====================================================================== */

/* in: a friend; out: who speaks for him: "party" when he's out, else an NPC of
 * him that steps in beside Luigi for the scene (ch4_cast_off sends it away) */
static int sCast; /* PORT_SL_BIT of the stand-ins out now */
static const char* const kFriendNpc[PORT_SL_FRIENDS] = {NULL, "sl_blooey", "sl_jerry", "sl_torque", "sl_hayzee", NULL};
static const char* const kFriendModel[PORT_SL_FRIENDS] = {NULL, "c_lp_gesso", "c_lp_bom", "c_lp_met", "c_lp_pansy", NULL};
static const char* const kFriendTribe[PORT_SL_FRIENDS] = {NULL, SJ4_BLOOEY, SJ4_JERRY, SJ4_TORQUE, SJ4_HAYZEE, NULL};

static int partyIs(int friendId) {
    const PortSlFriend* f = portSlGetFriend(friendId);
    PartyEntry* p = partyGetPtr(marioGetPartyId());
    return f != NULL && p != NULL && p->currentMemberId == f->partyId;
}

/* gra_l45's hollow: which side of it Luigi last stood on, on the road (y 0, z over -200: not
 * down in it nor on the back lane). in: 0 note where he is now; 1 out: 1 if that was the east.
 * Only well clear of the hole counts (west of -300, where the hop out lands at -330; east of -200,
 * where ana_2 lands at -190): walking in from the west he went over the lip to past -260 still
 * above y -5, which read as the east, so he was hopped back out west, round and round (walking
 * from gra_l44 back to Petunia) */
static USER_FUNC(ch4_g45_side) {
    static float lastX = -770.0f;
    MarioWork* m = marioGetPtr();
    (void)isFirstCall;
    if (evtGetValue(event, event->args[0]) == 0) {
        if (m->position.y > -5.0f && m->position.y < 20.0f && m->position.z > -200.0f &&
            (m->position.x < -300.0f || m->position.x > -200.0f)) {
            lastX = m->position.x;
        }
    } else {
        evtSetValue(event, event->args[1], lastX > -250.0f);
    }
    return EVT_RETURN_DONE;
}

/* The game's Amazy Dayzee tribe names c_kpansy's poses with a _K (PAN_S_1_K...), which the model
 * hasn't got (its battle poses are PAN_S_1...): an NPC on it stood, talked and walked in its first
 * pose, PAN_Z_1. This gives the NPC args[0] the same tribe with the model's own names */
static USER_FUNC(ch4_amazy) {
    static NpcTribeInfo tribe;
    NpcEntry* npc = npcNameToPtr((const char*)evtGetValue(event, event->args[0]));
    (void)isFirstCall;
    if (tribe.modelName == NULL) {
        tribe = *npcGetTribe(SJ4_AMAZY);
        tribe.initialAnim = "PAN_S_1";
        tribe.stopAnimation = "PAN_S_1";
        tribe.stayAnim = "PAN_S_1";
        tribe.talkAnim = "PAN_T_1";
        tribe.walkAnimation = "PAN_W_1";
        tribe.runAnimation = "PAN_R_1";
        tribe.damageAnimation = "PAN_D_1";
        tribe.confuseAnimation = "PAN_K_1";
    }
    npc->tribe = &tribe;
    strcpy(npc->stayAnim, tribe.stayAnim);
    strcpy(npc->talkAnim, tribe.talkAnim);
    strcpy(npc->currentAnim, tribe.initialAnim);
    animPoseSetAnim(npc->poseId, npc->currentAnim, TRUE);
    return EVT_RETURN_DONE;
}

/* waits (at most args[0] frames) for the stick to be let go */
static USER_FUNC(ch4_stick_rest) {
    static int t;
    if (isFirstCall) {
        t = 0;
    }
    if ((keyGetStickX(0) > -10 && keyGetStickX(0) < 10 && keyGetStickY(0) > -10 && keyGetStickY(0) < 10) ||
        ++t >= evtGetValue(event, event->args[0])) {
        return EVT_RETURN_DONE;
    }
    return EVT_RETURN_BLOCK;
}

static USER_FUNC(ch4_who) {
    int id = evtGetValue(event, event->args[0]);
    MarioWork* m = marioGetPtr();
    NpcEntry* npc;
    if (partyIs(id)) {
        evtSetValue(event, event->args[1], (EvtWord)(AddrWord) "party");
        return EVT_RETURN_DONE;
    }
    if (npcNameToPtr_NoAssert(kFriendNpc[id]) == NULL) {
        if (!animGroupBaseAsync(kFriendModel[id], 0, NULL)) {
            return EVT_RETURN_BLOCK;
        }
        npcEntry(kFriendNpc[id], kFriendModel[id]);
        npc = npcNameToPtr(kFriendNpc[id]);
        npc->tribe = npcGetTribe(kFriendTribe[id]);
        npc->position = m->position;
        npc->position.x += (id == PORT_SL_FRIEND_HAYZEE ? 45.0f : -45.0f) + (id == 2 ? -25.0f : id == 3 ? -50.0f : 0.0f);
        npc->position.z += 15.0f * (id - 2);
        npc->flags |= NPC_FLAG_INTANGIBLE;
        strcpy(npc->currentAnim, "S_1");
        animPoseSetAnim(npc->poseId, npc->currentAnim, 1);
        sCast |= PORT_SL_BIT(id);
    }
    (void)isFirstCall;
    evtSetValue(event, event->args[1], (EvtWord)(AddrWord)kFriendNpc[id]);
    return EVT_RETURN_DONE;
}

/* the scene's stand-ins go */
static USER_FUNC(ch4_cast_off) {
    int id;
    (void)event, (void)isFirstCall;
    for (id = 1; id < PORT_SL_FRIENDS; id++) {
        if ((sCast & PORT_SL_BIT(id)) && npcNameToPtr_NoAssert(kFriendNpc[id]) != NULL) {
            npcDelete(npcNameToPtr(kFriendNpc[id]));
        }
    }
    sCast = 0;
    return EVT_RETURN_DONE;
}

/* in: a friend: out 1 when he's built (his field ability works) */
static USER_FUNC(ch4_built) {
    (void)isFirstCall;
    evtSetValue(event, event->args[1], portSlGetFriend(evtGetValue(event, event->args[0])) != NULL);
    return EVT_RETURN_DONE;
}

/* in: a friend: out 1 when he's the partner out now */
static USER_FUNC(ch4_out) {
    (void)isFirstCall;
    evtSetValue(event, event->args[1], partyIs(evtGetValue(event, event->args[0])));
    return EVT_RETURN_DONE;
}

/* in: a page (1-5): it's found; out: how many of the five are */
static USER_FUNC(ch4_page) {
    int n = evtGetValue(event, event->args[0]), i, c = 0;
    (void)isFirstCall;
    swSet(SL_FLAG(4, F_PAGE1) + n - 1);
    for (i = 0; i < 5; i++) {
        c += swGet(SL_FLAG(4, F_PAGE1) + i) != 0;
    }
    evtSetValue(event, event->args[1], c);
    return EVT_RETURN_DONE;
}

/* out: the three problems solved (pages, hat, Petunia) */
static USER_FUNC(ch4_ready) {
    (void)isFirstCall;
    evtSetValue(event, event->args[0],
                swGet(SL_FLAG(4, F_PAGES)) && swGet(SL_FLAG(4, F_HAT)) && swGet(SL_FLAG(4, F_PETUNIA)));
    return EVT_RETURN_DONE;
}

/* --- props that ride on an NPC (the Fiery Hat on Jerry, a fan's leaf, Dazzlina's tiara) --- */
#define PROPS 12
static struct {
    char name[32], owner[32];
    float dx, dy, dz;
} sProps[PROPS];

/* in: the prop NPC's name, its model, its owner NPC, the offset (x, y, z), its animation */
static USER_FUNC(ch4_prop) {
    EvtWord* a = event->args;
    const char* name = (const char*)evtGetValue(event, a[0]);
    const char* model = (const char*)evtGetValue(event, a[1]);
    NpcEntry* npc;
    char path[64];
    size_t n = 0;
    int i;
    (void)isFirstCall;
    snprintf(path, sizeof(path), "a/%s", model);
    if (portModsGetHostFile(path, &n) == NULL) {
        return EVT_RETURN_DONE; /* the model isn't in this build */
    }
    if (!animGroupBaseAsync(model, 0, NULL)) {
        return EVT_RETURN_BLOCK;
    }
    for (i = 0; i < PROPS && sProps[i].name[0]; i++) {
    }
    if (i == PROPS || npcNameToPtr_NoAssert(name) != NULL) {
        return EVT_RETURN_DONE;
    }
    npcEntry(name, model);
    npc = npcNameToPtr(name);
    npc->flags |= NPC_FLAG_INTANGIBLE | NPC_FLAG_NO_SHADOW;
    snprintf(npc->currentAnim, sizeof(npc->currentAnim), "%s", (const char*)evtGetValue(event, a[6]));
    animPoseSetAnim(npc->poseId, npc->currentAnim, 1);
    snprintf(sProps[i].name, sizeof(sProps[i].name), "%s", name);
    snprintf(sProps[i].owner, sizeof(sProps[i].owner), "%s", (const char*)evtGetValue(event, a[2]));
    sProps[i].dx = (float)evtGetValue(event, a[3]);
    sProps[i].dy = (float)evtGetValue(event, a[4]);
    sProps[i].dz = (float)evtGetValue(event, a[5]);
    return EVT_RETURN_DONE;
}

/* each frame: the props ride their owners; out of the chapter, Luigi's costume comes off */
static void propsFrame(void) {
    static char sMap[16];
    static int sWhereT;
    int i;
    if (gp == NULL) {
        return;
    }
    if (strncmp(sMap, gp->currentMapName, sizeof(sMap)) != 0) {
        snprintf(sMap, sizeof(sMap), "%s", gp->currentMapName);
        memset(sProps, 0, sizeof(sProps));
    }
    {
        /* as the chapter is left (each frame out of it took chapter 2's gown off) */
        static int sWas4;
        const int is4 = portSuperLuigiChapter() == 4;
        if (sWas4 && !is4) {
            portLuigiSetCostume(NULL);
        }
        sWas4 = is4;
    }
    if (getenv("TTYD_SL_WHERE") != NULL && ++sWhereT % 30 == 0) { /* building the rooms: where Luigi is */
        MarioWork* m = marioGetPtr();
        printf("[ch4] %s luigi %.0f %.0f %.0f\n", gp->currentMapName, m->position.x, m->position.y, m->position.z);
        fflush(stdout);
    }
    for (i = 0; i < PROPS; i++) {
        NpcEntry* prop;
        NpcEntry* owner;
        if (!sProps[i].name[0]) {
            continue;
        }
        prop = npcNameToPtr_NoAssert(sProps[i].name);
        owner = npcNameToPtr_NoAssert(sProps[i].owner);
        if (prop == NULL) {
            sProps[i].name[0] = 0;
            continue;
        }
        if (owner == NULL) {
            prop->flags |= NPC_FLAG_HIDDEN;
            sProps[i].name[0] = 0;
            continue;
        }
        prop->position.x = owner->position.x + sProps[i].dx;
        prop->position.y = owner->position.y + sProps[i].dy;
        prop->position.z = owner->position.z + sProps[i].dz;
    }
}

/* in: an NPC: deleted if it's there (a prop that may not be in the build) */
static USER_FUNC(ch4_del) {
    NpcEntry* npc = npcNameToPtr_NoAssert((const char*)evtGetValue(event, event->args[0]));
    (void)isFirstCall;
    if (npc != NULL) {
        npcDelete(npc);
    }
    return EVT_RETURN_DONE;
}

/* in: x, z, a radius: out 1 when Luigi is that near and free to move (a scene's trigger) */
static USER_FUNC(ch4_near) {
    MarioWork* m = marioGetPtr();
    float dx = m->position.x - (float)evtGetValue(event, event->args[0]);
    float dz = m->position.z - (float)evtGetValue(event, event->args[1]);
    float r = (float)evtGetValue(event, event->args[2]);
    (void)isFirstCall;
    evtSetValue(event, event->args[3], dx * dx + dz * dz < r * r && marioChkKey() && !gp->inBattle);
    return EVT_RETURN_DONE;
}

/* blocks until the room's entrance has played out (the walk in through a door
 * or an exit, the partner put down after it): a scene that starts on entry
 * waits for it, or the entrance moves Luigi and his partner after the scene
 * has placed them */
static USER_FUNC(ch4_entered) {
    (void)event, (void)isFirstCall;
    return marioChkKey() && !gp->inBattle ? EVT_RETURN_DONE : EVT_RETURN_BLOCK;
}

/* in: an item: it's Luigi's (a key upgrade given by a scene) */
static USER_FUNC(ch4_give) {
    (void)isFirstCall;
    pouchGetItem((ItemType)evtGetValue(event, event->args[0]));
    return EVT_RETURN_DONE;
}

/* in: a model: out 1 when the game has it (our new models, as they land) */
static USER_FUNC(ch4_has_model) {
    char path[64];
    size_t n = 0;
    (void)isFirstCall;
    snprintf(path, sizeof(path), "a/%s", (const char*)evtGetValue(event, event->args[0]));
    evtSetValue(event, event->args[1], portModsGetHostFile(path, &n) != NULL);
    return EVT_RETURN_DONE;
}

/* --- the shops: a list of goods in the game's selection box --- */

typedef struct Goods {
    short item, price;
} Goods;
static const Goods kCurtainCall[] = {
    {ITEM_SUPER_SHROOM, 20}, {ITEM_MUSHROOM, 5}, {ITEM_MAPLE_SYRUP, 25}, {ITEM_HONEY_SYRUP, 10}, {ITEM_FIRE_FLOWER, 10},
    {ITEM_THUNDER_RAGE, 18}, {ITEM_DIZZY_DIAL, 12}, {ITEM_SLEEPY_SHEEP, 8}, {ITEM_MINI_MR_MINI, 15}, {0, 0},
};
static const Goods kThimbles[] = {
    {ITEM_MUSHROOM, 5}, {ITEM_SUPER_SHROOM, 20}, {ITEM_HONEY_SYRUP, 10}, {ITEM_LIFE_SHROOM, 50},
    {ITEM_REPEL_CAPE, 15}, {ITEM_BOOS_SHEET, 15}, {ITEM_FRIGHT_MASK, 10}, {0, 0},
};
static const Goods* sShop;
static int sShopPage;

/* in: the shop (0 Curtain Call, 1 Thimble's), the page: the list as "sl4_goods"; out: rows */
static USER_FUNC(ch4_shop_menu) {
    static char text[512];
    int shop = evtGetValue(event, event->args[0]);
    int page = evtGetValue(event, event->args[1]);
    int i, n = 0, first = page * 5, total;
    char line[64];
    (void)isFirstCall;
    sShop = shop ? kThimbles : kCurtainCall;
    sShopPage = page;
    for (total = 0; sShop[total].item; total++) {
    }
    text[0] = 0;
    for (i = first; i < total && i < first + 5; i++, n++) {
        snprintf(line, sizeof(line), "\n%s - %d coins", msgSearch(itemDataTable[sShop[i].item].name), sShop[i].price);
        strncat(text, line, sizeof(text) - strlen(text) - 1);
    }
    if (total > 5) {
        strncat(text, page == 0 ? "\nMore goods..." : "\nBack...", sizeof(text) - strlen(text) - 1);
        n++;
    }
    strncat(text, "\nNothing, thanks", sizeof(text) - strlen(text) - 1);
    snprintf(line, sizeof(line), "<select 0 %d 0 40>", n);
    memmove(text + strlen(line), text, strlen(text) + 1);
    memcpy(text, line, strlen(line));
    portModsSetMessage("sl4_goods", text);
    evtSetValue(event, event->args[2], n + 1);
    evtSetValue(event, event->args[3], total > 5 ? n - 1 : -1);
    return EVT_RETURN_DONE;
}

/* in: the row picked: out 0 bought, 1 no coins, 2 no room, 3 nothing picked */
static USER_FUNC(ch4_shop_buy) {
    int row = evtGetValue(event, event->args[0]);
    int i = sShopPage * 5 + row;
    PouchData* p = pouchGetPtr();
    int total;
    (void)isFirstCall;
    for (total = 0; sShop[total].item; total++) {
    }
    if (row < 0 || row >= 5 || i >= total) {
        evtSetValue(event, event->args[1], 3);
        return EVT_RETURN_DONE;
    }
    if (p->coins < sShop[i].price) {
        evtSetValue(event, event->args[1], 1);
        return EVT_RETURN_DONE;
    }
    if (pouchGetItem((ItemType)sShop[i].item) != 1) {
        evtSetValue(event, event->args[1], 2);
        return EVT_RETURN_DONE;
    }
    pouchAddCoin(-sShop[i].price);
    evtSetValue(event, event->args[1], 0);
    return EVT_RETURN_DONE;
}

/* the shop's script: LW(10) the shop, LW(11) the clerk */
static EVT_BEGIN(ch4_shop)
    KEY_OFF
    CALL(evt_unitwin_alloc, 0)
    CALL(unitwincoin_disp_onoff, 1)
    SET_VALUE(LW(12), 0)
    LABEL(1)
    CALL(ch4_shop_menu, LW(10), LW(12), LW(13), LW(14))
    IF_INT_EQUAL(LW(10), 0)
        CALL(evt_msg_print, 0, STRING("sl4_shop_hi"), 0, LW(11))
    ELSE
        CALL(evt_msg_print, 0, STRING("sl_c4_thimble_shop"), 0, LW(11))
    END_IF
    CALL(evt_msg_select, 0, STRING("sl4_goods"))
    CALL(evt_msg_continue)
    IF_INT_EQUAL(LW(0), LW(14))
        IF_INT_EQUAL(LW(12), 0)
            SET_VALUE(LW(12), 1)
        ELSE
            SET_VALUE(LW(12), 0)
        END_IF
        GOTO(1)
    END_IF
    CALL(ch4_shop_buy, LW(0), LW(1))
    SWITCH(LW(1))
        CASE_EQUAL(0)
            CALL(evt_snd_sfxon, STRING("SFX_COIN_GET1"), 0)
            CALL(unitwincoin_wait)
            CALL(evt_msg_print, 0, STRING("sl4_shop_thanks"), 0, LW(11))
            GOTO(1)
        CASE_EQUAL(1)
            CALL(evt_msg_print, 0, STRING("sl4_shop_poor"), 0, LW(11))
            GOTO(1)
        CASE_EQUAL(2)
            CALL(evt_msg_print, 0, STRING("sl4_shop_full"), 0, LW(11))
    END_SWITCH
    CALL(evt_msg_print, 0, STRING("sl4_shop_bye"), 0, LW(11))
    CALL(unitwincoin_disp_onoff, 0)
    CALL(evt_unitwin_free)
    KEY_ON
    RETURN
EVT_END()

/* ======================================================================
 * Shared scenes
 * ====================================================================== */

/* A script page found: LW(0) the page (1-5). Luigi reads it; with all five, Hayzee cheers. */
static EVT_BEGIN(page_found)
    KEY_OFF
    CALL(evt_mario_set_pose, STRING("M_I_2"))
    CALL(evt_snd_sfxon, STRING("SFX_EVT_STARSTONE_SHINE1"), 0)
    SET_VALUE(LW(9), LW(0))
    CALL(ch4_page, LW(9), LW(8))
    SWITCH(LW(9))
        CASE_EQUAL(1)
            SYS("sl_c4_page_got1")
        CASE_EQUAL(2)
            SYS("sl_c4_page_got2")
        CASE_EQUAL(3)
            SYS("sl_c4_page_got3")
        CASE_EQUAL(4)
            SYS("sl_c4_page_got4")
        CASE_EQUAL(5)
            SYS("sl_c4_page_got5")
    END_SWITCH
    CALL(evt_mario_set_pose, STRING("M_S_1"))
    /* his line from the page, deadpan; the friend whose line it is answers if he's out */
    SWITCH(LW(9))
        CASE_EQUAL(1)
            LUIGI("sl_c4_page3_read")
            CALL(ch4_out, PORT_SL_FRIEND_JERRY, LW(1))
            IF_INT_EQUAL(LW(1), 1)
                SAY("sl_c4_page3_jerry", "party")
            END_IF
        CASE_EQUAL(2)
            LUIGI("sl_c4_page7_read")
        CASE_EQUAL(3)
            LUIGI("sl_c4_page12_read")
        CASE_EQUAL(4)
            LUIGI("sl_c4_page18_read")
            CALL(ch4_out, PORT_SL_FRIEND_BLOOEY, LW(1))
            IF_INT_EQUAL(LW(1), 1)
                SAY("sl_c4_page18_blooey", "party")
            END_IF
        CASE_EQUAL(5)
            LUIGI("sl_c4_page24_read")
    END_SWITCH
    IF_INT_EQUAL(LW(8), 5)
        SET_VALUE(SLF4(F_PAGES), 1)
        CALL(ch4_who, PORT_SL_FRIEND_HAYZEE, LW(1))
        CALL(evt_msg_print, 0, STRING("sl_c4_pages_done"), 0, LW(1))
        CALL(ch4_cast_off)
    ELSE
        SYS("sl_c4_page_count")
    END_IF
    KEY_ON
    RETURN
EVT_END()

/* a friend's spot that his ability opens, when he isn't built yet (other
 * chapters' branches): checking it does it, with a word from Luigi */
static EVT_BEGIN(ability_stub)
    RETURN
EVT_END()

/* The night's ending scenes need Luigi as the Grass when the model is there */
static EVT_BEGIN(grass_on)
    CALL(ch4_has_model, STRING(MODEL_GRASS), LW(0))
    IF_INT_EQUAL(LW(0), 1)
        CALL(evt_luigi_costume, STRING(MODEL_GRASS))
    END_IF
    RETURN
EVT_END()

/* ======================================================================
 * pik_l40, Jazzafrazz Station (pik_00)
 * ====================================================================== */

/* the platform door behind them: the train they came on has gone (it stays shut, with a line) */
static EVT_BEGIN(pik_l40_platform)
    CALL(evt_party_stop, 0)
    LUIGI("sl_c4_platform")
    CALL(evt_party_run, 0)
    SET_VALUE(LW(0), 1) /* no exit */
    RETURN
EVT_END()

static BeroEntry pik_l40_bero[] = {
    BERO("se_bero", "pik_l41", "nw_bero"),
    {"n_bero", 1, 8, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, pik_l40_platform, NULL, NULL, 1, 1, "n_1_door_close", "n_1_door_open"},
    {0},
};

static EVT_BEGIN(porter_talk)
    IF_INT_EQUAL(SLF4(F_CAST), 0)
        SAY("sl_c4_npc_porter_1", "sl_porter")
    ELSE
        IF_INT_EQUAL(SLF4(F_SLAM), 0)
            SAY("sl_c4_npc_porter_2", "sl_porter")
        ELSE
            SAY("sl_c4_npc_porter_3", "sl_porter")
        END_IF
    END_IF
    RETURN
EVT_END()

static EVT_BEGIN(couple_talk)
    IF_INT_EQUAL(SLF4(F_CAST), 0)
        SAY("sl_c4_npc_couple_1", "sl_madame")
        SAY("sl_c4_npc_couple_2", "sl_sir")
    ELSE
        IF_INT_EQUAL(SLF4(F_SLAM), 0)
            SAY("sl_c4_npc_couple_3", "sl_madame")
            SAY("sl_c4_npc_couple_4", "sl_sir")
        ELSE
            SAY("sl_c4_npc_couple_5", "sl_madame")
        END_IF
    END_IF
    RETURN
EVT_END()

/* page 3: the station clock, jammed: Torque's Tune-up */
static EVT_BEGIN(clock_fixed)
    KEY_OFF
    CALL(evt_snd_sfxon, STRING("SFX_STG6_CROSSING_OPEN1"), 0)
    CALL(evt_sl_point_off, PORT_SL_ABILITY_TUNE, 0)
    SET_VALUE(LW(0), 3)
    CALL_CHILD(page_found)
    KEY_ON
    RETURN
EVT_END()

static EVT_BEGIN(clock_sign)
    KEY_OFF
    IF_INT_EQUAL(SLF4(F_PAGE3), 1)
        LUIGI("sl_c4_clock_done")
        KEY_ON
        RETURN
    END_IF
    LUIGI("sl_c4_clock_look")
    CALL(ch4_built, PORT_SL_FRIEND_TORQUE, LW(0))
    IF_INT_EQUAL(LW(0), 0)
        /* Torque's chapter isn't in this build: Luigi gives it a whack */
        LUIGI("sl_c4_clock_whack")
        CALL(evt_snd_sfxon, STRING("SFX_MARIO_HAMMER_WOOD_DON1"), 0)
        CALL_CHILD(clock_fixed)
        KEY_ON
        RETURN
    END_IF
    LUIGI("sl_c4_clock_torque")
    KEY_ON
    RETURN
EVT_END()

static EVT_BEGIN(pik_l40_arrive)
    KEY_OFF
    /* placed while the room's own fade-in is still black, so the narration
     * prints over the picture, as the other chapters open */
    CALL(evt_mario_set_pos, 40, 0, 230)
    CALL(evt_mario_set_dir, 270, 0, 0)
    CALL(evt_party_stop, 0)
    CALL(evt_party_set_pos, 0, 0, -1000, 0)
    CALL(evt_party_dispflg_onoff, 0, PARTY_DISP_FLAG_HIDDEN, 1)
    NPC("sl_blooey", "c_lp_gesso", SJ4_BLOOEY, -10, 0, 250)
    NPC("sl_jerry", "c_lp_bom", SJ4_JERRY, 95, 0, 255)
    NPC("sl_torque", "c_lp_met", SJ4_TORQUE, 130, 0, 225)
    FACE("sl_blooey", 90)
    FACE("sl_torque", 270)
    FACE("sl_jerry", 270)
    CALL(evt_seq_wait, 2)
    /* off the train: its bell behind the station, and away it goes */
    CALL(evt_snd_sfxon, STRING("SFX_STG6_TRAIN_BELL1"), 0)
    WAIT_MS(200)
    CALL(evt_snd_sfxon, STRING("SFX_STG6_TRAIN_BELL1"), 0)
    CALL(evt_snd_sfxon, STRING("SFX_STG6_TRAIN_MOVE1"), 0)
    WAIT_MS(600)
    SYS("sl_c4_arrive_00")
    WAIT_MS(200)
    CALL(evt_mario_set_pose, STRING("M_V_2"))
    LUIGI("sl_c4_arrive_01")
    CALL(evt_mario_set_pose, STRING("M_S_1"))
    SAY("sl_c4_arrive_02", "sl_torque")
    SAY("sl_c4_arrive_03", "sl_jerry")
    SAY("sl_c4_arrive_04", "sl_blooey")
    LUIGI("sl_c4_arrive_05")
    /* the town sings at them: a Dayzee dances past, another taps on the rail, a third does jazz hands */
    NPC("sl_dancer", "c_pansy", SJ4_DAYZEE, -300, 0, 300)
    CALL(evt_snd_sfxon, STRING("SFX_AUDIENCE_PANSY_SINGING1"), LW(13))
    BEGIN_INLINE_EVENT
        LOOP(6)
            CALL(evt_npc_get_position, STRING("sl_dancer"), LW(0), LW(1), LW(2))
            ADD(LW(1), 30)
            CALL(evt_eff64, STRING(""), STRING("onpu_n64"), 2, LW(0), LW(1), LW(2), 0, 0, 0, 0, 0, 0, 0, 0)
            WAIT_FRAMES(20)
        END_LOOP
    END_INLINE_EVENT
    CALL(evt_npc_move_position, STRING("sl_dancer"), 170, 300, 2500, 0, 4)
    CALL(evt_npc_reverse_ry, STRING("sl_dancer"))
    ANIM("sl_dancer", "PAN_T_1")
    WAIT_MS(700)
    SAY("sl_c4_arrive_06", "sl_dancer")
    CALL(evt_snd_sfxoff, LW(13))
    CALL(evt_npc_move_position, STRING("sl_dancer"), 450, 330, 1500, 0, 4)
    CALL(evt_npc_delete, STRING("sl_dancer"))
    SAY("sl_c4_arrive_07", "sl_torque")
    SET_VALUE(SLF4(F_OPENED), 1)
    /* the friends fall in behind him */
    CALL(ch4_del, STRING("sl_hat"))
    CALL(evt_npc_delete, STRING("sl_jerry"))
    CALL(evt_npc_delete, STRING("sl_torque"))
    CALL(evt_npc_get_position, STRING("sl_blooey"), LW(0), LW(1), LW(2))
    CALL(evt_npc_delete, STRING("sl_blooey"))
    CALL(evt_sl_friend_slot, PORT_SL_FRIEND_BLOOEY, LW(3))
    IF_INT_NOT_EQUAL(LW(3), 0)
        /* a partner already out was parked under the floor: he takes the stand-in's place */
        CALL(evt_party_set_pos, 0, LW(0), LW(1), LW(2))
        CALL(evt_party_dispflg_onoff, 0, PARTY_DISP_FLAG_HIDDEN, 0)
        CALL(evt_party_run, 0)
        CALL(evt_mario_set_party_pos, 0, LW(3), LW(0), LW(1), LW(2))
    END_IF
    KEY_ON
    RETURN
EVT_END()

static EVT_BEGIN(pik_l40_init)
    BGM("BGM_STG6_PIK1")
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG6_PIK1"))
    BEROS(pik_l40_bero)
    CALL(evt_mobj_save_blk, STRING("mobj_save"), -100, 60, 25, 0, 0)
    CALL(evt_mobj_brick, STRING("mobj_hidden"), 120, 60, 25, ITEM_STAR_PIECE, 11, 0, SLF4(F_P40_SP))
    NPC("sl_porter", "c_shasho_b", SJ4_PORTER, -200, 0, 200)
    TALK("sl_porter", porter_talk)
    NPC("sl_madame", "c_mamab", SJ4_RICHMA, 230, 0, 120)
    NPC("sl_sir", "c_papab", SJ4_RICHPA, 270, 0, 120)
    FACE("sl_sir", 270)
    TALK("sl_madame", couple_talk)
    TALK("sl_sir", couple_talk)
    /* the station clock: page 3 */
    CALL(evt_mobj_signboard, STRING("sl_clock"), 160, 0, 290, PTR(clock_sign), LSWF(0))
    IF_INT_EQUAL(SLF4(F_PAGE3), 0)
        CALL(evt_sl_point, PORT_SL_ABILITY_TUNE, 0, 160, 0, 290, 90, 0, PTR(clock_fixed))
    END_IF
    IF_INT_EQUAL(SLF4(F_OPENED), 0)
        SPAWN_EVENT(pik_l40_arrive)
    END_IF
    RETURN
EVT_END()

/* ======================================================================
 * pik_l41, Limelight Lane (pik_04), and its alley at night
 * ====================================================================== */

static BeroEntry pik_l41_bero[] = {
    BERO("nw_bero", "pik_l40", "se_bero"),
    BERO("se_bero", "pik_l42", "nw_bero"),
    {0},
};

/* the poster */
static EVT_BEGIN(poster_read)
    KEY_OFF
    SYS("sl_c4_poster_00")
    IF_INT_EQUAL(SLF4(F_POSTER), 0)
        SET_VALUE(SLF4(F_POSTER), 1)
        CALL(evt_mario_set_pose, STRING("M_N_2"))
        LUIGI("sl_c4_poster_01")
        CALL(evt_mario_set_pose, STRING("M_S_1"))
        CALL(ch4_who, PORT_SL_FRIEND_JERRY, LW(1))
        CALL(evt_msg_print, 0, STRING("sl_c4_poster_02"), 0, LW(1))
        CALL(ch4_cast_off)
        /* a Dayzee explains, unasked */
        CALL(evt_npc_get_position, STRING("sl_lane_dz"), LW(0), LW(1), LW(2))
        SAY("sl_c4_poster_03", "sl_lane_dz")
    END_IF
    KEY_ON
    RETURN
EVT_END()

static EVT_BEGIN(lane_dz_talk)
    IF_INT_EQUAL(SLF4(F_CAST), 0)
        SAY("sl_c4_poster_03", "sl_lane_dz")
    ELSE
        IF_INT_EQUAL(SLF4(F_SLAM), 0)
            SAY("sl_c4_npc_lane_2", "sl_lane_dz")
        ELSE
            SAY("sl_c4_npc_lane_3", "sl_lane_dz")
        END_IF
    END_IF
    RETURN
EVT_END()

/* the Lane Chorus at the studio's door: Shake a Leaf, and they conga off */
static EVT_BEGIN(chorus_talk)
    IF_INT_EQUAL(SLF4(F_HAYZEE), 0)
        SAY("sl_c4_chorus_busy", "sl_ch1")
    ELSE
        SAY("sl_c4_chorus_busy", "sl_ch1")
        CALL(ch4_who, PORT_SL_FRIEND_HAYZEE, LW(1))
        CALL(evt_msg_print, 0, STRING("sl_c4_sing_hint"), 0, LW(1))
        CALL(ch4_cast_off)
    END_IF
    RETURN
EVT_END()

static EVT_BEGIN(chorus_dance)
    KEY_OFF
    CALL(evt_sl_hz_song, LW(0))
    IF_INT_NOT_EQUAL(LW(0), 1)
        /* the wrong song: they just sing louder */
        SAY("sl_c4_chorus_wrong", "sl_ch1")
        KEY_ON
        RETURN
    END_IF
    CALL(evt_sl_point_off, PORT_SL_ABILITY_SING, 0)
    CALL(evt_snd_sfxon, STRING("SFX_AUDIENCE_PANSY_SINGING1"), LW(13))
    SAY("sl_c4_chorus_go", "sl_ch1")
    /* a conga line, down the steps and off along the lane; on a clock, not on the last of them
     * reaching the far end: one held up by Luigi or his friend, standing anywhere near, walked on the
     * spot for good, with the field held (scripts had stood elsewhere) */
    BEGIN_INLINE_EVENT_GET_ID(LW(3))
        CALL(evt_npc_move_position, STRING("sl_ch1"), -70, -175, 700, 0, 4)
        CALL(evt_npc_move_position, STRING("sl_ch1"), 200, 100, 1900, 0, 4)
    END_INLINE_EVENT
    BEGIN_INLINE_EVENT_GET_ID(LW(4))
        WAIT_FRAMES(10)
        CALL(evt_npc_move_position, STRING("sl_ch2"), -70, -175, 700, 0, 4)
        CALL(evt_npc_move_position, STRING("sl_ch2"), 170, 70, 1900, 0, 4)
    END_INLINE_EVENT
    BEGIN_INLINE_EVENT_GET_ID(LW(5))
        WAIT_FRAMES(20)
        CALL(evt_npc_move_position, STRING("sl_ch3"), -70, -175, 700, 0, 4)
        CALL(evt_npc_move_position, STRING("sl_ch3"), 140, 40, 1900, 0, 4)
    END_INLINE_EVENT
    BEGIN_INLINE_EVENT_GET_ID(LW(6))
        WAIT_FRAMES(30)
        CALL(evt_npc_move_position, STRING("sl_ch4"), -70, -175, 700, 0, 4)
        CALL(evt_npc_move_position, STRING("sl_ch4"), 110, 10, 1900, 0, 4)
    END_INLINE_EVENT
    BEGIN_INLINE_EVENT_GET_ID(LW(7))
        WAIT_FRAMES(40)
        CALL(evt_npc_move_position, STRING("sl_ch5"), -70, -175, 700, 0, 4)
        CALL(evt_npc_move_position, STRING("sl_ch5"), 80, -20, 1900, 0, 4)
    END_INLINE_EVENT
    BEGIN_INLINE_EVENT_GET_ID(LW(8))
        WAIT_FRAMES(50)
        CALL(evt_npc_move_position, STRING("sl_ch6"), -70, -175, 700, 0, 4)
        CALL(evt_npc_move_position, STRING("sl_ch6"), 50, -50, 1900, 0, 4)
    END_INLINE_EVENT
    WAIT_MS(3400)
    DELETE_EVENT(LW(3))
    DELETE_EVENT(LW(4))
    DELETE_EVENT(LW(5))
    DELETE_EVENT(LW(6))
    DELETE_EVENT(LW(7))
    DELETE_EVENT(LW(8))
    CALL(evt_snd_sfxoff, LW(13))
    CALL(evt_npc_delete, STRING("sl_ch1"))
    CALL(evt_npc_delete, STRING("sl_ch2"))
    CALL(evt_npc_delete, STRING("sl_ch3"))
    CALL(evt_npc_delete, STRING("sl_ch4"))
    CALL(evt_npc_delete, STRING("sl_ch5"))
    CALL(evt_npc_delete, STRING("sl_ch6"))
    SET_VALUE(SLF4(F_CHORUS), 1)
    CALL(ch4_who, PORT_SL_FRIEND_HAYZEE, LW(1))
    CALL(evt_msg_print, 0, STRING("sl_c4_sing_after"), 0, LW(1))
    CALL(ch4_cast_off)
    KEY_ON
    RETURN
EVT_END()

/* the studio's door */
static EVT_BEGIN(studio_door)
    IF_INT_EQUAL(SLF4(F_CHORUS), 0)
        KEY_OFF
        LUIGI("sl_c4_door_blocked")
        KEY_ON
        RETURN
    END_IF
    KEY_OFF
    CALL(evt_snd_sfxon, STRING("SFX_DOOR_OPEN_WOOD1"), 0)
    CALL(evt_bero_mapchange, STRING("win_l40"), STRING("w_bero"))
    RETURN
EVT_END()

/* the villa's door: Luigi on its top step */
static EVT_BEGIN(studio_watch)
    LOOP(0)
        CALL(ch4_near, -125, -275, 26, LW(0))
        IF_INT_EQUAL(LW(0), 1)
            CALL_CHILD(studio_door)
            IF_INT_EQUAL(SLF4(F_CHORUS), 1)
                RETURN
            END_IF
            LOOP(0)
                CALL(ch4_near, -125, -275, 45, LW(0))
                IF_INT_EQUAL(LW(0), 0)
                    BREAK_LOOP
                END_IF
                WAIT_FRAMES(1)
            END_LOOP
        END_IF
        WAIT_FRAMES(1)
    END_LOOP
    RETURN
EVT_END()

/* page 2: the cracked planter at the hedge's end: Jerry's Blast */
static EVT_BEGIN(planter_blown)
    KEY_OFF
    CALL(evt_sl_point_off, PORT_SL_ABILITY_BLAST, 0)
    CALL(evt_mobj_get_position, STRING("sl_planter"), LW(0), LW(1), LW(2))
    CALL(evt_eff, 0, STRING("bomb"), 0, LW(0), LW(1), LW(2), FLOAT(1.0f), 0, 0, 0, 0, 0, 0, 0)
    CALL(evt_snd_sfxon, STRING("SFX_AUDIENCE_BOMB_BURST1"), 0)
    CALL(evt_mobj_delete, STRING("sl_planter"))
    SET_VALUE(LW(0), 2)
    CALL_CHILD(page_found)
    KEY_ON
    RETURN
EVT_END()

static EVT_BEGIN(planter_sign)
    KEY_OFF
    LUIGI("sl_c4_planter_look")
    CALL(ch4_built, PORT_SL_FRIEND_JERRY, LW(0))
    IF_INT_EQUAL(LW(0), 0)
        LUIGI("sl_c4_planter_kick")
        CALL_CHILD(planter_blown)
        KEY_ON
        RETURN
    END_IF
    LUIGI("sl_c4_planter_jerry")
    KEY_ON
    RETURN
EVT_END()

/* the Pianta street musician: 10 coins, three times, and he gives Super Appeal */
static EVT_BEGIN(musician_talk)
    IF_INT_EQUAL(SLF4(F_APPEAL), 1)
        SAY("sl_c4_npc_busker_done", "sl_busker")
        RETURN
    END_IF
    CALL(evt_unitwin_alloc, 0)
    CALL(unitwincoin_disp_onoff, 1)
    SAY("sl_c4_npc_busker_1", "sl_busker")
    CALL(evt_msg_select, 0, STRING("sl4_yesno"))
    IF_INT_NOT_EQUAL(LW(0), 0)
        CALL(evt_msg_continue)
        SAY("sl_c4_npc_busker_no", "sl_busker")
        GOTO(99)
    END_IF
    CALL(evt_pouch_get_coin, LW(1))
    IF_INT_LESS(LW(1), 10)
        CALL(evt_msg_continue)
        SAY("sl_c4_npc_busker_poor", "sl_busker")
        GOTO(99)
    END_IF
    CALL(evt_msg_continue)
    CALL(evt_pouch_add_coin, -10)
    CALL(unitwincoin_wait)
    IF_INT_EQUAL(SLF4(F_TIP1), 0)
        SET_VALUE(SLF4(F_TIP1), 1)
        SAY("sl_c4_npc_busker_tip1", "sl_busker")
        GOTO(99)
    END_IF
    IF_INT_EQUAL(SLF4(F_TIP2), 0)
        SET_VALUE(SLF4(F_TIP2), 1)
        SAY("sl_c4_npc_busker_tip2", "sl_busker")
        GOTO(99)
    END_IF
    SET_VALUE(SLF4(F_TIP3), 1)
    SAY("sl_c4_npc_busker_gift", "sl_busker")
    CALL(unitwincoin_disp_onoff, 0)
    CALL(evt_unitwin_free)
    CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
    CALL(evt_item_entry, STRING("sl_appeal"), ITEM_SUPER_APPEAL, LW(0), LW(1), LW(2), ITEM_ENTRY_FIXED_NO_TOUCH, -1, 0)
    CALL(evt_item_get_item, STRING("sl_appeal"))
    SET_VALUE(SLF4(F_APPEAL), 1)
    RETURN
    LABEL(99)
    CALL(unitwincoin_disp_onoff, 0)
    CALL(evt_unitwin_free)
    RETURN
EVT_END()

/* --- the night: the fans, the voice, Hayzee comes along, his version --- */

static EVT_BEGIN(night_fans)
    CALL(evt_seq_wait, 2)
    CALL(ch4_entered)
    KEY_OFF
    CALL_CHILD(grass_on)
    CALL(evt_party_stop, 0)
    CALL(evt_party_set_pos, 0, 0, -1000, 0)
    CALL(evt_party_dispflg_onoff, 0, PARTY_DISP_FLAG_HIDDEN, 1)
    CALL(evt_mario_set_pos, -300, 5, -330)
    CALL(evt_mario_set_dir, 90, 0, 0)
    BGM("BGM_FF_ENV_ADMIRE1")
    WAIT_MS(400)
    CALL(evt_mario_mov_pos2, -230, -300, FLOAT(60.0f))
    WAIT_MS(300)
    CALL(evt_snd_sfxon, STRING("SFX_AUDIENCE_PANIC_SCREAM1"), 0)
    SAY("sl_c4_fans_00", "sl_fan1")
    /* they swarm */
    /* they pour down the lane from the west gate, round him on the path
     * (from the stage door's side they bunched up on its steps, out of view) */
    CALL(evt_mario_set_dir, 270, 0, 0)
    BEGIN_INLINE_EVENT
        CALL(evt_npc_move_position, STRING("sl_fan1"), -280, -312, 700, 0, 4)
    END_INLINE_EVENT
    BEGIN_INLINE_EVENT
        CALL(evt_npc_move_position, STRING("sl_fan2"), -298, -345, 700, 0, 4)
    END_INLINE_EVENT
    BEGIN_INLINE_EVENT
        CALL(evt_npc_move_position, STRING("sl_fan3"), -318, -305, 700, 0, 4)
    END_INLINE_EVENT
    BEGIN_INLINE_EVENT
        CALL(evt_npc_move_position, STRING("sl_fan5"), -352, -312, 700, 0, 4)
    END_INLINE_EVENT
    BEGIN_INLINE_EVENT
        CALL(evt_npc_move_position, STRING("sl_fan6"), -368, -345, 700, 0, 4)
    END_INLINE_EVENT
    CALL(evt_npc_move_position, STRING("sl_fan4"), -335, -342, 700, 0, 4)
    CALL(evt_cam_shake, 4, FLOAT(0.0f), FLOAT(1.0f), 400)
    SAY("sl_c4_fans_01", "sl_fan2")
    SAY("sl_c4_fans_02", "sl_fan3")
    SAY("sl_c4_fans_03", "sl_fan4")
    SAY("sl_c4_fans_04", "sl_fan1")
    CALL(evt_mario_set_pose, STRING("M_N_2"))
    LUIGI("sl_c4_fans_05")
    CALL(evt_snd_sfxon, STRING("SFX_AUDIENCE_CHEER_ALLY3"), 0)
    SYS("sl_c4_fans_06")
    CALL(evt_luigi_story_pose, STRING("GR_J"))
    CALL(evt_mario_set_pose, STRING("M_V_2"))
    LUIGI("sl_c4_fans_07")
    CALL(evt_luigi_story_pose, 0)
    CALL(evt_mario_set_pose, STRING("M_S_1"))
    SET_VALUE(SLF4(F_FANS), 1)
    /* later: the alley empty, a paper leaf blowing past */
    CALL(evt_fade_entry, FADE_OUT_BLACK, 800, 0, 0, 0)
    CALL(evt_fade_end_wait)
    CALL(evt_npc_delete, STRING("sl_fan1"))
    CALL(evt_npc_delete, STRING("sl_fan2"))
    CALL(evt_npc_delete, STRING("sl_fan3"))
    CALL(evt_npc_delete, STRING("sl_fan4"))
    CALL(evt_npc_delete, STRING("sl_fan5"))
    CALL(evt_npc_delete, STRING("sl_fan6"))
    CALL(evt_npc_delete, STRING("sl_bannerman"))
    CALL(evt_snd_bgmoff, PSND_FADE_OUT)
    WAIT_MS(500)
    CALL(evt_fade_entry, FADE_IN_BLACK, 800, 0, 0, 0)
    CALL(evt_fade_end_wait)
    /* the compass, and the voice */
    CALL(evt_mario_set_pose, STRING("M_I_2"))
    CALL(evt_snd_sfxon, STRING("SFX_STAR_POINT_GET1"), 0)
    LUIGI("sl_c4_voice_00")
    CALL(evt_map_set_blend, 0, 40, 40, 70, 255)
    BGM("BGM_EVT_ORGEL1")
    WAIT_MS(800)
    SYS("sl_c4_voice_01")
    SYS("sl_c4_voice_02")
    SYS("sl_c4_voice_03")
    CALL(evt_map_set_blend, 0, 110, 110, 170, 255)
    CALL(evt_mario_set_pose, STRING("M_S_1"))
    LUIGI("sl_c4_voice_04")
    LUIGI("sl_c4_voice_05")
    SET_VALUE(SLF4(F_VOICE), 1)
    /* Hayzee steps out of the stage door */
    NPC("sl_hayzee", "c_lp_pansy", SJ4_HAYZEE, -330, 5, -340)
    CALL(evt_npc_move_position, STRING("sl_hayzee"), -270, -310, 1000, 0, 4)
    BGM("BGM_EVT_CLOUDA1")
    SAY("sl_c4_hz_comes_00", "sl_hayzee")
    LUIGI("sl_c4_hz_comes_01")
    SAY("sl_c4_hz_comes_02", "sl_hayzee")
    SAY("sl_c4_hz_comes_03", "sl_hayzee")
    LUIGI("sl_c4_hz_comes_04")
    SAY("sl_c4_hz_comes_05", "sl_hayzee")
    SET_VALUE(SLF4(F_HZ_COMES), 1)
    CALL(evt_luigi_costume, 0)
    /* the tag: Hayzee's version, in the spotlight */
    CALL(evt_snd_bgmoff, PSND_FADE_OUT)
    CALL(evt_fade_entry, FADE_OUT_BLACK, 600, 0, 0, 0)
    CALL(evt_fade_end_wait)
    CALL(evt_mario_set_pos, -420, 5, -420)
    CALL(evt_npc_set_position, STRING("sl_hayzee"), -250, 5, -300)
    CALL(evt_npc_set_ry, STRING("sl_hayzee"), 0)
    CALL(evt_map_set_blend, 0, 25, 25, 35, 255)
    CALL(evt_fade_entry, FADE_IN_BLACK, 600, 0, 0, 0)
    CALL(evt_fade_end_wait)
    BGM("BGM_EVT_RUIJI1")
    SYS("sl_c4_tag_caption")
    SAY("sl_c4_tag_00", "sl_hayzee")
    SAY("sl_c4_tag_01", "sl_hayzee")
    SYS("sl_c4_tag_still")
    SAY("sl_c4_tag_02", "sl_hayzee")
    SAY("sl_c4_tag_03", "sl_hayzee")
    SAY("sl_c4_tag_04", "sl_hayzee")
    SET_VALUE(SLF4(F_DONE), 1)
    CALL(evt_party_dispflg_onoff, 0, PARTY_DISP_FLAG_HIDDEN, 0)
    CALL(evt_sl_chapter_end)
    RETURN
EVT_END()

static EVT_BEGIN(pik_l41_init)
    BEROS(pik_l41_bero)
    IF_INT_EQUAL(SLF4(F_NIGHT), 1)
        IF_INT_EQUAL(SLF4(F_DONE), 0)
            /* the stage door's alley, at night */
            CALL(evt_map_set_blend, 0, 110, 110, 170, 255)
            NPC("sl_fan1", "c_pansy", SJ4_DAYZEE, -385, 5, -320)
            NPC("sl_fan2", "c_kinopio", SJ4_TOAD, -405, 5, -345)
            NPC("sl_fan3", "c_nokonoko", SJ4_KOOPA, -425, 5, -310)
            NPC("sl_fan4", "c_kinopiko", SJ4_TOADETTE, -445, 5, -340)
            NPC("sl_fan5", "c_pansy", SJ4_DAYZEE, -465, 5, -320)
            NPC("sl_fan6", "c_kinopio_b", SJ4_TOAD, -485, 5, -345)
            FACE("sl_fan1", 90)
            FACE("sl_fan2", 90)
            FACE("sl_fan3", 90)
            FACE("sl_fan4", 90)
            FACE("sl_fan5", 90)
            FACE("sl_fan6", 90)
            CALL(ch4_prop, STRING("sl_lf1"), STRING(MODEL_LEAF), STRING("sl_fan1"), -8, 18, 4, STRING("W_2"))
            CALL(ch4_prop, STRING("sl_lf2"), STRING(MODEL_LEAF), STRING("sl_fan2"), -8, 14, 4, STRING("W_1"))
            CALL(ch4_prop, STRING("sl_lf3"), STRING(MODEL_LEAF), STRING("sl_fan3"), -8, 22, 4, STRING("W_2"))
            CALL(ch4_prop, STRING("sl_lf4"), STRING(MODEL_LEAF), STRING("sl_fan4"), -8, 14, 4, STRING("W_2"))
            CALL(ch4_prop, STRING("sl_lf5"), STRING(MODEL_LEAF), STRING("sl_fan5"), -8, 18, 4, STRING("W_1"))
            CALL(ch4_prop, STRING("sl_lf6"), STRING(MODEL_LEAF), STRING("sl_fan6"), -8, 14, 4, STRING("W_2"))
            NPC("sl_bannerman", "c_kinopio", SJ4_TOAD, -330, 5, -400)
            CALL(evt_npc_flag_onoff, 1, STRING("sl_bannerman"), NPC_FLAG_HIDDEN | NPC_FLAG_INTANGIBLE)
            CALL(ch4_prop, STRING("sl_banner"), STRING(MODEL_BANNER), STRING("sl_bannerman"), 0, 85, -20, STRING("W_1")) /* hangs from there: at 55 the hedge hid it */
            SPAWN_EVENT(night_fans)
            RETURN
        END_IF
    END_IF
    BGM("BGM_STG6_PIK1")
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG6_PIK1"))
    /* the hedge runs at z -375: the poster and the planter stand in front of it, in view */
    CALL(evt_mobj_signboard, STRING("sl_poster"), -315, 0, -362, PTR(poster_read), LSWF(0))
    NPC("sl_lane_dz", "c_pansy", SJ4_DAYZEE, -160, 5, -360)
    TALK("sl_lane_dz", lane_dz_talk)
    /* the studio's villa door */
    SPAWN_EVENT(studio_watch)
    IF_INT_EQUAL(SLF4(F_CHORUS), 0)
        NPC("sl_ch1", "c_pansy", SJ4_DAYZEE, -110, 30, -250)
        NPC("sl_ch2", "c_pansy", SJ4_DAYZEE, -80, 30, -250)
        NPC("sl_ch3", "c_pansy", SJ4_DAYZEE, -50, 30, -250)
        NPC("sl_ch4", "c_pansy", SJ4_DAYZEE, -110, 30, -220)
        NPC("sl_ch5", "c_pansy", SJ4_DAYZEE, -80, 30, -220)
        NPC("sl_ch6", "c_pansy", SJ4_DAYZEE, -50, 30, -220)
        ANIM("sl_ch1", "PAN_T_1")
        ANIM("sl_ch3", "PAN_T_1")
        ANIM("sl_ch5", "PAN_T_1")
        TALK("sl_ch1", chorus_talk)
        TALK("sl_ch2", chorus_talk)
        TALK("sl_ch3", chorus_talk)
        TALK("sl_ch4", chorus_talk)
        TALK("sl_ch5", chorus_talk)
        TALK("sl_ch6", chorus_talk)
        CALL(evt_sl_point, PORT_SL_ABILITY_SING, 0, -80, 30, -235, 160, 0, PTR(chorus_dance))
    END_IF
    /* blocks over the hedge, the honey block */
    CALL(evt_mobj_brick, STRING("sl_coin1"), -170, 60, -380, 0, 3, 0, SLF4(F_P41_COIN1))
    CALL(evt_mobj_brick, STRING("sl_coin2"), -40, 60, -380, 0, 3, 0, SLF4(F_P41_COIN2))
    CALL(evt_mobj_badgeblk, STRING("sl_honey"), 30, 60, -160, ITEM_HONEY_SYRUP, 0, SLF4(F_P41_HONEY), 0)
    /* page 2: the cracked planter at the hedge's end */
    IF_INT_EQUAL(SLF4(F_PAGE2), 0)
        CALL(evt_mobj_signboard, STRING("sl_planter"), -235, 5, -362, PTR(planter_sign), LSWF(2))
        CALL(evt_sl_point, PORT_SL_ABILITY_BLAST, 0, -235, 5, -362, 90, 0, PTR(planter_blown))
    END_IF
    /* the street musician */
    NPC("sl_busker", "c_m_buka", SJ4_PIANTA, 130, 0, 55) /* on the lawn: inside the hedge (60, -60) nobody could reach him */
    TALK("sl_busker", musician_talk)
    RETURN
EVT_END()

/* ======================================================================
 * pik_l42, Fountain Square (pik_01)
 * ====================================================================== */

static BeroEntry pik_l42_bero[] = {
    BERO("nw_bero", "pik_l41", "se_bero"),
    {"n_bero", 1, 8, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "pik_l43", "s_bero", 1, 1, 0, 0},
    PIPE("dokan_1", "gra_l40", "w_bero"),
    {0},
};

/* Hayzee at the fountain: he joins */
static EVT_BEGIN(hayzee_scene)
    KEY_OFF
    CALL(evt_mario_set_dir_npc, STRING("sl_hayzee"))
    SAY("sl_c4_hayzee_00", "sl_hayzee")
    LUIGI("sl_c4_hayzee_01")
    SAY("sl_c4_hayzee_02", "sl_hayzee")
    SAY("sl_c4_hayzee_03", "sl_hayzee")
    LUIGI("sl_c4_hayzee_04")
    /* the sunglasses come off; a long look at all of them; back on */
    ANIM("sl_hayzee", "T_1")
    WAIT_MS(600)
    CALL(evt_npc_reverse_ry, STRING("sl_hayzee"))
    WAIT_MS(400)
    CALL(evt_npc_reverse_ry, STRING("sl_hayzee"))
    WAIT_MS(300)
    SAY("sl_c4_hayzee_05", "sl_hayzee")
    LUIGI("sl_c4_hayzee_06")
    SAY("sl_c4_hayzee_07", "sl_hayzee")
    CALL(ch4_who, PORT_SL_FRIEND_TORQUE, LW(1))
    CALL(evt_msg_print, 0, STRING("sl_c4_hayzee_08"), 0, LW(1))
    SAY("sl_c4_hayzee_09", "sl_hayzee")
    LUIGI("sl_c4_hayzee_10")
    SAY("sl_c4_hayzee_11", "sl_hayzee")
    LUIGI("sl_c4_hayzee_12")
    SAY("sl_c4_hayzee_13", "sl_hayzee")
    SAY("sl_c4_hayzee_14", "sl_hayzee")
    LUIGI("sl_c4_hayzee_15")
    SAY("sl_c4_hayzee_16", "sl_hayzee")
    LUIGI("sl_c4_hayzee_17")
    SAY("sl_c4_hayzee_18", "sl_hayzee")
    CALL(ch4_cast_off)
    /* Hayzee joins */
    ANIM("sl_hayzee", "E_2")
    BGM("BGM_FF_GET_PARTY1")
    SYS("sl_c4_hayzee_joined")
    CALL(evt_npc_get_position, STRING("sl_hayzee"), LW(0), LW(1), LW(2))
    CALL(evt_npc_delete, STRING("sl_hayzee"))
    CALL(evt_sl_friend_join, PORT_SL_FRIEND_HAYZEE)
    CALL(evt_sl_friend_slot, PORT_SL_FRIEND_HAYZEE, LW(3))
    /* he comes out in the follower's place, as a new partner does: with one
     * out, evt_mario_set_party_pos does nothing, and the Sing tutorial's words
     * came from Blooey, with no Sing on X */
    CALL(evt_mario_get_party, LW(4))
    IF_INT_NOT_EQUAL(LW(4), 0)
        CALL(evt_mario_goodbye_party, 0)
    END_IF
    CALL(evt_party_run, 0)
    CALL(evt_mario_set_party_pos, 0, LW(3), LW(0), LW(1), LW(2))
    SET_VALUE(SLF4(F_HAYZEE), 1)
    WAIT_MS(1500)
    BGM("BGM_STG6_PIK1")
    /* the tutorial: the chorus at the studio's door */
    SAY("sl_c4_sing_00", "party")
    SYS("sl_c4_sing_help")
    KEY_ON
    RETURN
EVT_END()

static EVT_BEGIN(hayzee_near)
    LOOP(0)
        IF_INT_EQUAL(SLF4(F_HAYZEE), 1)
            RETURN
        END_IF
        CALL(ch4_near, -60, -10, 90, LW(0))
        IF_INT_EQUAL(LW(0), 1)
            BREAK_LOOP
        END_IF
        WAIT_FRAMES(1)
    END_LOOP
    CALL_CHILD(hayzee_scene)
    RETURN
EVT_END()

/* page 1: the fountain's basin: Blooey dives */
static EVT_BEGIN(fountain_sign)
    KEY_OFF
    IF_INT_EQUAL(SLF4(F_PAGE4), 1)
        LUIGI("sl_c4_fountain_done")
        KEY_ON
        RETURN
    END_IF
    LUIGI("sl_c4_fountain_look")
    CALL(ch4_out, PORT_SL_FRIEND_BLOOEY, LW(0))
    IF_INT_EQUAL(LW(0), 0)
        LUIGI("sl_c4_fountain_blooey")
        KEY_ON
        RETURN
    END_IF
    /* Blooey dives in and comes up with it */
    SAY("sl_c4_fountain_dive", "party")
    /* a dive into the basin: a partner's jump into water never lands
     * (evt_party_jump_pos waits for ground, and the scene stuck there), so he
     * hops, goes under in a splash at the basin, and comes up the same way */
    CALL(evt_party_stop, 0)
    CALL(evt_party_set_pose, 0, STRING("F_2"))
    SET_VALUE(LW(0), -200) /* the basin, before the jets */
    SET_VALUE(LW(1), 0)
    SET_VALUE(LW(2), 200)
    CALL(evt_party_get_pos, 0, LW(3), LW(4), LW(5))
    ADD(LW(4), 30)
    CALL(evt_party_set_pos, 0, LW(3), LW(4), LW(5))
    WAIT_FRAMES(6)
    CALL(evt_party_dispflg_onoff, 0, PARTY_DISP_FLAG_HIDDEN, 1)
    CALL(evt_snd_sfxon, STRING("SFX_MARIO_FALL_WATER1"), 0)
    CALL(evt_eff, 0, STRING("splash"), 3, LW(0), LW(1), LW(2), 0, 0, 0, 0, 0, 0, 0, 0)
    WAIT_MS(1500)
    CALL(evt_snd_sfxon, STRING("SFX_KUPPA_WATER_OUT1"), 0)
    CALL(evt_eff, 0, STRING("splash"), 3, LW(0), LW(1), LW(2), 0, 0, 0, 0, 0, 0, 0, 0)
    ADD(LW(4), -30)
    CALL(evt_party_set_pos, 0, LW(3), LW(4), LW(5))
    CALL(evt_party_dispflg_onoff, 0, PARTY_DISP_FLAG_HIDDEN, 0)
    CALL(evt_party_set_pose, 0, STRING("F_3"))
    WAIT_MS(500)
    CALL(evt_party_set_pose, 0, STRING("S_1"))
    CALL(evt_party_run, 0)
    SET_VALUE(LW(0), 4)
    CALL_CHILD(page_found)
    KEY_ON
    RETURN
EVT_END()

/* page 4: the stage door's sleeping Dayzee: Rise and Shine */
static EVT_BEGIN(sleeper_talk)
    KEY_OFF
    SYS("sl_c4_sleeper_zz")
    LUIGI("sl_c4_sleeper_look")
    KEY_ON
    RETURN
EVT_END()

static EVT_BEGIN(sleeper_wake)
    KEY_OFF
    CALL(evt_sl_hz_song, LW(0))
    IF_INT_NOT_EQUAL(LW(0), 2)
        SAY("sl_c4_sleeper_sway", "party")
        KEY_ON
        RETURN
    END_IF
    CALL(evt_sl_point_off, PORT_SL_ABILITY_SING, 1)
    ANIM("sl_sleeper", "PAN_S_1")
    CALL(evt_snd_sfxon, STRING("SFX_ENM_PANSY_DAMAGED1"), 0)
    SAY("sl_c4_sleeper_wake", "sl_sleeper")
    BEGIN_INLINE_EVENT_GET_ID(LW(15))
        CALL(evt_npc_move_position, STRING("sl_sleeper"), 330, 120, 1500, 0, 4)
    END_INLINE_EVENT
    WAIT_MS(1700) /* on a clock: held up by Luigi it walked on the spot for good */
    DELETE_EVENT(LW(15))
    CALL(evt_npc_delete, STRING("sl_sleeper"))
    SET_VALUE(LW(0), 5)
    CALL_CHILD(page_found)
    KEY_ON
    RETURN
EVT_END()

/* the page on the theatre's roof, over its doors: the spring before the doors
 * bounces Luigi up there, and it's his when he reaches it */
static EVT_BEGIN(ledge_page)
    LOOP(0)
        CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
        IF_INT_GREATER(LW(1), 300)
            CALL(ch4_near, 60, -160, 80, LW(3))
            IF_INT_EQUAL(LW(3), 1)
                BREAK_LOOP
            END_IF
        END_IF
        WAIT_FRAMES(1)
    END_LOOP
    KEY_OFF
    CALL(evt_item_delete, STRING("sl_page5"))
    SET_VALUE(LW(0), 1)
    CALL_CHILD(page_found)
    KEY_ON
    RETURN
EVT_END()

/* the Society's clerk: the guest rooms (the inn), and after the show, the patron's Shine */
static EVT_BEGIN(clerk_talk)
    CALL(evt_unitwin_alloc, 0)
    CALL(unitwincoin_disp_onoff, 1)
    IF_INT_EQUAL(SLF4(F_SLAM), 1)
        IF_INT_EQUAL(SLF4(F_REWARD), 1)
            IF_INT_EQUAL(SLF4(F_PATRON), 0)
                /* the standing ovation was missed: the Shine Sprite, for a patron's donation */
                SAY("sl_c4_clerk_patron", "sl_clerk")
                CALL(evt_msg_select, 0, STRING("sl4_yesno"))
                CALL(evt_msg_continue)
                IF_INT_EQUAL(LW(0), 0)
                    CALL(evt_pouch_get_coin, LW(1))
                    IF_INT_GREATER_OR_EQUAL(LW(1), 200)
                        CALL(evt_pouch_add_coin, -200)
                        CALL(unitwincoin_wait)
                        SET_VALUE(SLF4(F_PATRON), 1)
                        CALL(unitwincoin_disp_onoff, 0)
                        CALL(evt_unitwin_free)
                        CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
                        CALL(evt_item_entry, STRING("sl_shine3"), ITEM_SHINE_SPRITE, LW(0), LW(1), LW(2), ITEM_ENTRY_FIXED_NO_TOUCH, -1, 0)
                        CALL(evt_item_get_item, STRING("sl_shine3"))
                        RETURN
                    END_IF
                    SAY("sl_c4_clerk_poor", "sl_clerk")
                END_IF
            END_IF
        END_IF
    END_IF
    SAY("sl_c4_clerk_inn", "sl_clerk")
    CALL(evt_msg_select, 0, STRING("sl4_yesno"))
    CALL(evt_msg_continue)
    IF_INT_EQUAL(LW(0), 0)
        CALL(evt_pouch_get_coin, LW(1))
        IF_INT_LESS(LW(1), 15)
            SAY("sl_c4_clerk_poor", "sl_clerk")
            GOTO(99)
        END_IF
        CALL(evt_pouch_add_coin, -15)
        CALL(unitwincoin_wait)
        SAY("sl_c4_clerk_sleep", "sl_clerk")
        KEY_OFF
        CALL(unitwincoin_disp_onoff, 0)
        CALL(evt_fade_entry, FADE_OUT_BLACK, 600, 0, 0, 0)
        CALL(evt_fade_end_wait)
        BGM("BGM_FF_GO_SLEEPING1")
        WAIT_MS(3000)
        CALL(evt_pouch_mario_recovery)
        CALL(evt_pouch_all_party_recovery)
        BGM("BGM_STG6_PIK1")
        CALL(evt_fade_entry, FADE_IN_BLACK, 600, 0, 0, 0)
        CALL(evt_fade_end_wait)
        SAY("sl_c4_clerk_morning", "sl_clerk")
        KEY_ON
    ELSE
        SAY("sl_c4_clerk_no", "sl_clerk")
    END_IF
    LABEL(99)
    CALL(unitwincoin_disp_onoff, 0)
    CALL(evt_unitwin_free)
    RETURN
EVT_END()

static EVT_BEGIN(shopkeep_talk)
    SET_VALUE(LW(10), 0)
    SET_VALUE(LW(11), PTR("sl_shopkeep"))
    CALL_CHILD(ch4_shop)
    RETURN
EVT_END()

static EVT_BEGIN(square_dz_talk)
    IF_INT_EQUAL(SLF4(F_CAST), 0)
        SAY("sl_c4_npc_square_1", "sl_sq_dz")
    ELSE
        IF_INT_EQUAL(SLF4(F_SLAM), 0)
            SAY("sl_c4_npc_square_2", "sl_sq_dz")
        ELSE
            SAY("sl_c4_npc_square_3", "sl_sq_dz")
        END_IF
    END_IF
    RETURN
EVT_END()

static EVT_BEGIN(gate_sign)
    KEY_OFF
    SYS("sl_c4_pipe_sign")
    KEY_ON
    RETURN
EVT_END()

/* the theatre's door: shut till the casting, and on the night it's the show */
static EVT_BEGIN(pik_l42_init)
    BGM("BGM_STG6_PIK1")
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG6_PIK1"))
    BEROS(pik_l42_bero)
    CALL(evt_map_playanim, STRING("puku_mizu"), 1, 0)
    IF_INT_EQUAL(SLF4(F_CAST), 0)
        /* the theatre's shut until Hayzee's people are cast */
        CALL(evt_hitobj_onoff, STRING("n_bero"), 1, 0)
    END_IF
    IF_INT_EQUAL(SLF4(F_HAYZEE), 0)
        NPC("sl_hayzee", "c_lp_pansy", SJ4_HAYZEE, -60, 0, -10)
        ANIM("sl_hayzee", "D_2")
        TALK("sl_hayzee", hayzee_scene)
        SPAWN_EVENT(hayzee_near)
    END_IF
    /* the shop by the fountain, the Society's clerk by the theatre */
    NPC("sl_shopkeep", "c_kinopio_g", SJ4_TOAD, -230, 0, 60)
    TALK("sl_shopkeep", shopkeep_talk)
    NPC("sl_clerk", "c_pub_m", SJ4_MASTER, 30, 15, -210)
    TALK("sl_clerk", clerk_talk)
    NPC("sl_sq_dz", "c_pansy", SJ4_DAYZEE, 60, 0, 180)
    TALK("sl_sq_dz", square_dz_talk)
    CALL(evt_mobj_recovery_blk, STRING("mobj_heart"), 0, 60, 60, 40, 0, 0)
    CALL(evt_item_entry, STRING("sl_sp42"), ITEM_STAR_PIECE, -246, 15, 246, ITEM_ENTRY_FIXED, SLF4(F_P42_SP), 0)
    CALL(evt_mobj_signboard, STRING("sl_gate_sign"), -330, 30, 60, PTR(gate_sign), LSWF(3))
    /* the fountain (page_found 4: F_PAGE4) */
    CALL(evt_mobj_signboard, STRING("sl_fountain"), -130, -5, 330, PTR(fountain_sign), LSWF(4))
    /* the sleeping Dayzee at the theatre's door (page_found 5: F_PAGE5) */
    IF_INT_EQUAL(SLF4(F_PAGE5), 0)
        IF_INT_EQUAL(SLF4(F_HAYZEE), 1)
            NPC("sl_sleeper", "c_pansy", SJ4_DAYZEE, 200, 15, -140)
            ANIM("sl_sleeper", "PAN_N_1")
            TALK("sl_sleeper", sleeper_talk)
            CALL(evt_sl_point, PORT_SL_ABILITY_SING, 1, 200, 15, -140, 150, 2, PTR(sleeper_wake))
        END_IF
    END_IF
    /* up on the awning over the theatre's doors (page_found 1: F_PAGE1) */
    IF_INT_EQUAL(SLF4(F_PAGE1), 0)
        IF_INT_EQUAL(SLF4(F_CAST), 1)
            CALL(evt_item_entry, STRING("sl_page5"), ITEM_DUBIOUS_PAPER, 60, 345, -160, ITEM_ENTRY_FIXED_NO_TOUCH, -1, 0)
            CALL(evt_mobj_jumpstand_blue, 0, STRING("sl_awning"), 330, 80, 15, -120, 0, 0)
            SPAWN_EVENT(ledge_page)
        END_IF
    END_IF
    RETURN
EVT_END()

/* ======================================================================
 * win_l40, Hayzee's Studio (win_04): the casting
 * ====================================================================== */

static BeroEntry win_l40_bero[] = {
    {"w_bero", 1, 8, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "pik_l41", "se_bero", 1, 1, 0, 0},
    {"e_bero", 1, 8, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, NULL, NULL, 1, 1, 0, 0},
    {0},
};

static EVT_BEGIN(casting)
    CALL(evt_seq_wait, 2)
    CALL(ch4_entered)
    KEY_OFF
    CALL(evt_party_stop, 0)
    CALL(evt_party_set_pos, 0, 0, -1000, 0)
    CALL(evt_party_dispflg_onoff, 0, PARTY_DISP_FLAG_HIDDEN, 1)
    /* the studio's floor in view runs from the door to the dais's rail
     * (x -190 to -70): Luigi by the rail, the cast before him */
    CALL(evt_mario_mov_pos2, -85, 35, FLOAT(80.0f))
    NPC("sl_hayzee", "c_lp_pansy", SJ4_HAYZEE, -140, 0, 25)
    NPC("sl_blooey", "c_lp_gesso", SJ4_BLOOEY, -180, 0, 55)
    NPC("sl_jerry", "c_lp_bom", SJ4_JERRY, -160, 0, 100)
    NPC("sl_torque", "c_lp_met", SJ4_TORQUE, -110, 0, 105)
    FACE("sl_blooey", 90)
    FACE("sl_jerry", 90)
    FACE("sl_hayzee", 90)
    CALL(evt_mario_set_dir, 270, 0, 0)
    BGM("BGM_EVT_CLOUDA1")
    SAY("sl_c4_casting_00", "sl_hayzee")
    SAY("sl_c4_casting_01", "sl_torque")
    SAY("sl_c4_casting_02", "sl_hayzee")
    SAY("sl_c4_casting_03", "sl_hayzee")
    SAY("sl_c4_casting_04", "sl_blooey")
    SAY("sl_c4_casting_05", "sl_hayzee")
    CALL(evt_npc_get_position, STRING("sl_jerry"), LW(0), LW(1), LW(2))
    ADD(LW(1), 20)
    CALL(evt_eff, 0, STRING("kemuri_test"), 4, LW(0), LW(1), LW(2), FLOAT(0.5f), 0, 0, 0, 0, 0, 0, 0)
    SAY("sl_c4_casting_06", "sl_jerry")
    SAY("sl_c4_casting_07", "sl_hayzee")
    SAY("sl_c4_casting_08", "sl_torque")
    CALL(evt_mario_set_pose, STRING("M_V_2"))
    LUIGI("sl_c4_casting_09")
    SAY("sl_c4_casting_10", "sl_hayzee")
    LUIGI("sl_c4_casting_11")
    CALL(evt_mario_set_pose, STRING("M_S_1"))
    SAY("sl_c4_casting_12", "sl_hayzee")
    CALL(evt_mario_set_pose, STRING("M_N_2"))
    WAIT_MS(500)
    LUIGI("sl_c4_casting_13")
    LUIGI("sl_c4_casting_14")
    CALL(evt_mario_set_pose, STRING("M_S_1"))
    SAY("sl_c4_casting_15", "sl_hayzee")
    LUIGI("sl_c4_casting_16")
    SAY("sl_c4_casting_17", "sl_hayzee")
    LUIGI("sl_c4_casting_18")
    SAY("sl_c4_casting_19", "sl_hayzee")
    LUIGI("sl_c4_casting_20")
    SAY("sl_c4_casting_21", "sl_hayzee")
    CALL(evt_mario_set_pose, STRING("M_D_2"))
    LUIGI("sl_c4_casting_22")
    CALL(evt_mario_set_pose, STRING("M_S_1"))
    /* the problems, counted on his leaves */
    SAY("sl_c4_problems_00", "sl_hayzee")
    SAY("sl_c4_problems_01", "sl_torque")
    SAY("sl_c4_problems_02", "sl_hayzee")
    SET_VALUE(SLF4(F_CAST), 1)
    SET_VALUE(SLF4(F_BACKSTAGE_OK), 1)
    /* back to the party, Hayzee out */
    CALL(evt_npc_delete, STRING("sl_blooey"))
    CALL(ch4_del, STRING("sl_hat"))
    CALL(evt_npc_delete, STRING("sl_jerry"))
    CALL(evt_npc_delete, STRING("sl_torque"))
    CALL(evt_npc_get_position, STRING("sl_hayzee"), LW(0), LW(1), LW(2))
    CALL(evt_npc_delete, STRING("sl_hayzee"))
    CALL(evt_sl_friend_slot, PORT_SL_FRIEND_HAYZEE, LW(3))
    /* whoever is out was parked under the floor: he steps out where Hayzee stood */
    CALL(evt_party_set_pos, 0, LW(0), LW(1), LW(2))
    CALL(evt_party_dispflg_onoff, 0, PARTY_DISP_FLAG_HIDDEN, 0)
    CALL(evt_party_run, 0)
    CALL(evt_mario_set_party_pos, 0, LW(3), LW(0), LW(1), LW(2))
    KEY_ON
    RETURN
EVT_END()

static EVT_BEGIN(piano)
    KEY_OFF
    CALL(evt_snd_sfxon, STRING("SFX_AC_FAIL1"), 0)
    WAIT_MS(250)
    CALL(evt_snd_sfxon, STRING("SFX_AC_FAIL1"), 0)
    WAIT_MS(250)
    CALL(evt_snd_sfxon, STRING("SFX_AC_FAIL1"), 0)
    SYS("sl_c4_piano_00")
    CALL(ch4_who, PORT_SL_FRIEND_HAYZEE, LW(1))
    CALL(evt_msg_print, 0, STRING("sl_c4_piano_01"), 0, LW(1))
    CALL(ch4_cast_off)
    KEY_ON
    RETURN
EVT_END()

/* the night: Hayzee's sent for them */
static EVT_BEGIN(studio_ready)
    KEY_OFF
    CALL(ch4_who, PORT_SL_FRIEND_HAYZEE, LW(1))
    CALL(evt_msg_print, 0, STRING("sl_c4_ready"), 0, LW(1))
    CALL(ch4_cast_off)
    SET_VALUE(SLF4(F_ALL_HINT), 1)
    KEY_ON
    RETURN
EVT_END()

static EVT_BEGIN(win_l40_init)
    BGM("BGM_EVT_CLOUDA1")
    BEROS(win_l40_bero)
    CALL(evt_mobj_save_blk, STRING("mobj_save"), -125, 60, -95, 0, 0)
    CALL(evt_mobj_brick, STRING("sl_hidden"), 60, 90, -40, ITEM_STAR_PIECE, 11, 0, SLF4(F_W40_SP))
    CALL(evt_mobj_signboard, STRING("sl_piano"), 150, 50, 60, PTR(piano), LSWF(0))
    IF_INT_EQUAL(SLF4(F_CAST), 0)
        SPAWN_EVENT(casting)
    ELSE
        CALL(ch4_ready, LW(0))
        IF_INT_EQUAL(LW(0), 1)
            IF_INT_EQUAL(SLF4(F_ALL_HINT), 0)
                SPAWN_EVENT(studio_ready)
            END_IF
        END_IF
    END_IF
    RETURN
EVT_END()

/* ======================================================================
 * pik_l43, the Grand Dramalama (pik_02): the night, the verdict, the party
 * ====================================================================== */

static BeroEntry pik_l43_bero[] = {
    {"s_bero", 1, 8, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "pik_l42", "n_bero", 1, 1, 0, 0},
    {0},
};

static EVT_BEGIN(usher_talk)
    IF_INT_EQUAL(SLF4(F_SLAM), 0)
        SAY("sl_c4_npc_usher", "sl_usher")
    ELSE
        SAY("sl_c4_npc_usher_after", "sl_usher")
    END_IF
    RETURN
EVT_END()

/* the side door to backstage */
static EVT_BEGIN(side_door)
    KEY_OFF
    IF_INT_EQUAL(SLF4(F_BACKSTAGE_OK), 0)
        SAY("sl_c4_side_shut", "sl_stagehand")
        KEY_ON
        RETURN
    END_IF
    IF_INT_EQUAL(SLF4(F_NIGHT), 0)
        SAY("sl_c4_side_open", "sl_stagehand")
    END_IF
    CALL(evt_snd_sfxon, STRING("SFX_DOOR_OPEN_WOOD1"), 0)
    CALL(evt_bero_mapchange, STRING("pik_l44"), STRING("s_bero"))
    RETURN
EVT_END()

/* the after-party's rewards: Old Thimble's Star Piece unless the rave gave
 * one, the Society's patron Shine unless the ovation did */
static EVT_BEGIN(party_rewards)
    IF_INT_EQUAL(SLF4(F_THIMBLE_SP), 0)
        NPC("sl_thimble", "c_pansy", SJ4_DAYZEE, -60, 0, 395)
        CALL(evt_npc_set_color, STRING("sl_thimble"), 170, 170, 170, 255)
        CALL(evt_npc_set_ry, STRING("sl_thimble"), 90)
        CALL(evt_sl4_house, LW(5))
        IF_INT_GREATER_OR_EQUAL(LW(5), 200)
            SAY("sl_c4_thimble_party", "sl_thimble")
        ELSE
            SAY("sl_c4_thimble_sp", "sl_thimble")
        END_IF
        CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
        CALL(evt_item_entry, STRING("sl_tsp"), ITEM_STAR_PIECE, LW(0), LW(1), LW(2), ITEM_ENTRY_FIXED_NO_TOUCH, -1, 0)
        CALL(evt_item_get_item, STRING("sl_tsp"))
        SET_VALUE(SLF4(F_THIMBLE_SP), 1)
    END_IF
    IF_INT_EQUAL(SLF4(F_PATRON), 0)
        NPC("sl_clerk", "c_pub_m", SJ4_MASTER, 70, 0, 395)
        CALL(evt_npc_set_ry, STRING("sl_clerk"), 270)
        CALL(evt_unitwin_alloc, 0)
        CALL(unitwincoin_disp_onoff, 1)
        SAY("sl_c4_clerk_patron", "sl_clerk")
        CALL(evt_msg_select, 0, STRING("sl4_yesno"))
        CALL(evt_msg_continue)
        IF_INT_EQUAL(LW(0), 0)
            CALL(evt_pouch_get_coin, LW(1))
            IF_INT_GREATER_OR_EQUAL(LW(1), 200)
                CALL(evt_pouch_add_coin, -200)
                CALL(unitwincoin_wait)
                SET_VALUE(SLF4(F_PATRON), 1)
                CALL(unitwincoin_disp_onoff, 0)
                CALL(evt_unitwin_free)
                CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
                CALL(evt_item_entry, STRING("sl_shine3"), ITEM_SHINE_SPRITE, LW(0), LW(1), LW(2), ITEM_ENTRY_FIXED_NO_TOUCH, -1, 0)
                CALL(evt_item_get_item, STRING("sl_shine3"))
                RETURN
            END_IF
            SAY("sl_c4_clerk_poor", "sl_clerk")
        ELSE
            SAY("sl_c4_clerk_patron_no", "sl_clerk")
        END_IF
        CALL(unitwincoin_disp_onoff, 0)
        CALL(evt_unitwin_free)
    END_IF
    RETURN
EVT_END()

/* the night: the rivals, the huddle, the costume, curtain up */
static EVT_BEGIN(slam_night)
    CALL(evt_seq_wait, 2)
    CALL(ch4_entered)
    KEY_OFF
    CALL(evt_party_stop, 0)
    CALL(evt_party_set_pos, 0, 0, -1000, 0)
    CALL(evt_party_dispflg_onoff, 0, PARTY_DISP_FLAG_HIDDEN, 1)
    CALL(evt_mario_set_pos, 0, 0, -350)
    CALL(evt_mario_set_dir, 0, 0, 0)
    BGM("BGM_EVT_STG3_TOUGI1")
    WAIT_MS(500)
    SAY("sl_c4_slam_mc_00", "sl_mc")
    SAY("sl_c4_slam_mc_01", "sl_mc")
    SYS("sl_c4_slam_still_01")
    SAY("sl_c4_slam_mc_02", "sl_mc")
    SYS("sl_c4_slam_still_02")
    SAY("sl_c4_slam_mc_03", "sl_mc")
    CALL(evt_snd_sfxon, STRING("SFX_AUDIENCE_CHEER_ENEMY2"), 0)
    SYS("sl_c4_slam_still_03")
    /* backstage, the cast's huddle */
    NPC("sl_hayzee", "c_lp_pansy", SJ4_HAYZEE, 60, 0, -330)
    NPC("sl_blooey", "c_lp_gesso", SJ4_BLOOEY, -60, 0, -320)
    NPC("sl_jerry", "c_lp_bom", SJ4_JERRY, -40, 0, -270)
    CALL(ch4_prop, STRING("sl_hat"), STRING(MODEL_HAT), STRING("sl_jerry"), 0, 24, 2, STRING("S_1"))
    NPC("sl_torque", "c_lp_met", SJ4_TORQUE, 30, 0, -260)
    NPC("sl_petunia", MODEL_PETUNIA, SJ4_DAYZEE, 80, 0, -280)
    FACE("sl_hayzee", 270)
    FACE("sl_blooey", 90)
    CALL(evt_mario_set_dir, 90, 0, 0)
    BGM("BGM_EVT_CLOUDA1")
    SAY("sl_c4_huddle_00", "sl_hayzee")
    SAY("sl_c4_huddle_01", "sl_torque")
    SAY("sl_c4_huddle_02", "sl_hayzee")
    LUIGI("sl_c4_huddle_03")
    SAY("sl_c4_huddle_04", "sl_hayzee")
    /* the costume */
    CALL(evt_fade_entry, FADE_OUT_BLACK, 500, 0, 0, 0)
    CALL(evt_fade_end_wait)
    CALL(evt_snd_sfxon, STRING("SFX_BOSS_RNPL_TRANSFORM1"), 0)
    CALL_CHILD(grass_on)
    WAIT_MS(500)
    CALL(evt_fade_entry, FADE_IN_BLACK, 500, 0, 0, 0)
    CALL(evt_fade_end_wait)
    WAIT_MS(1500)
    SAY("sl_c4_huddle_05", "sl_blooey")
    SAY("sl_c4_slam_mc_04", "sl_mc")
    CALL(evt_npc_delete, STRING("sl_blooey"))
    CALL(ch4_del, STRING("sl_hat"))
    CALL(evt_npc_delete, STRING("sl_jerry"))
    CALL(evt_npc_delete, STRING("sl_torque"))
    CALL(evt_npc_delete, STRING("sl_hayzee"))
    CALL(evt_npc_delete, STRING("sl_petunia"))
    /* curtain up: the stage battle (superluigi_slam_aurora.c) */
    CALL(evt_sl4_arm, 2)
    CALL(evt_npc_set_battle_info, STRING("sl_slam_e"), 0)
    CALL(evt_npc_battle_start, STRING("sl_slam_e"))
    CALL(evt_npc_wait_battle_end)
    CALL(evt_sl4_arm, 0)
    CALL(evt_npc_get_battle_result, LW(0))
    IF_INT_NOT_EQUAL(LW(0), 1)
        RETURN /* the show was cancelled: the game's Game Over */
    END_IF
    SET_VALUE(SLF4(F_SLAM), 1)
    CALL(evt_npc_delete, STRING("sl_slam_e"))
    /* the verdict: everyone round Luigi, in the shot (the battle's end brought his partner back
     * out, a second Hayzee; and the cast stood behind the camera, the judges cut off by its edge) */
    CALL(evt_party_stop, 0)
    CALL(evt_party_set_pos, 0, 0, -1000, 0)
    CALL(evt_party_dispflg_onoff, 0, PARTY_DISP_FLAG_HIDDEN, 1)
    BGM("BGM_EVT_STAGE_CLEAR1")
    NPC("sl_judge1", "c_teresa", SJ4_BOO, -130, 30, -240)
    NPC("sl_judge2", "c_nokonoko", SJ4_KOOPA, -85, 0, -225)
    NPC("sl_judge3", "c_pansy", SJ4_DAYZEE, -40, 0, -215)
    FACE("sl_judge1", 90)
    FACE("sl_judge2", 90)
    FACE("sl_judge3", 90)
    NPC("sl_petunia", MODEL_PETUNIA, SJ4_DAYZEE, 55, 0, -395)
    NPC("sl_hayzee", "c_lp_pansy", SJ4_HAYZEE, 100, 0, -370)
    FACE("sl_petunia", 270)
    FACE("sl_hayzee", 270)
    NPC("sl_dazz", "c_kpansy", SJ4_AMAZY, -115, 0, -395)
    CALL(ch4_amazy, STRING("sl_dazz"))
    CALL(evt_npc_set_scale, STRING("sl_dazz"), FLOAT(1.2f), FLOAT(1.2f), FLOAT(1.2f))
    CALL(ch4_prop, STRING("sl_tiara"), STRING(MODEL_TIARA), STRING("sl_dazz"), 0, 40, 1, STRING("S_1"))
    CALL(evt_snd_sfxon, STRING("SFX_AUDIENCE_CHEER_ALLY4"), 0)
    CALL(evt_luigi_story_pose, STRING("GR_R"))
    WAIT_MS(1200)
    CALL(evt_luigi_story_pose, 0)
    SAY("sl_c4_verdict_00", "sl_judge1")
    SAY("sl_c4_verdict_01", "sl_judge2")
    SAY("sl_c4_verdict_02", "sl_judge3")
    SAY("sl_c4_verdict_03", "sl_mc")
    CALL(evt_snd_sfxon, STRING("SFX_AUDIENCE_CHEER_ALLY3"), 0)
    /* Dazzlina storms off */
    CALL(evt_npc_move_position, STRING("sl_dazz"), -420, -500, 900, 0, 4)
    CALL(ch4_del, STRING("sl_tiara"))
    CALL(evt_npc_delete, STRING("sl_dazz"))
    /* Hayzee, carried round on the cast's shoulders */
    CALL(evt_npc_jump_position_nohit, STRING("sl_hayzee"), 100, 30, -370, 400, 10)
    WAIT_MS(600)
    CALL(evt_npc_jump_position_nohit, STRING("sl_hayzee"), 100, 0, -370, 400, 10)
    /* the Plaque: Petunia to Hayzee to Luigi */
    SYS("sl_c4_verdict_04")
    SAY("sl_c4_verdict_05", "sl_hayzee")
    CALL(evt_mario_set_pose, STRING("M_I_2"))
    CALL(evt_snd_sfxon, STRING("SFX_STAR_POINT_GET1"), 0)
    SYS("sl_c4_piece")
    CALL(evt_mario_set_pose, STRING("M_D_2"))
    LUIGI("sl_c4_verdict_06")
    CALL(evt_mario_set_pose, STRING("M_S_1"))
    /* the review's reward */
    CALL(evt_sl4_house, LW(5))
    SET_VALUE(SLF4(F_REWARD), 1)
    IF_INT_GREATER_OR_EQUAL(LW(5), 200)
        SYS("sl_c4_review_ovation")
        CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
        CALL(evt_item_entry, STRING("sl_shine3"), ITEM_SHINE_SPRITE, LW(0), LW(1), LW(2), ITEM_ENTRY_FIXED_NO_TOUCH, -1, 0)
        CALL(evt_item_get_item, STRING("sl_shine3"))
        SET_VALUE(SLF4(F_PATRON), 1)
        CALL(evt_pouch_add_coin, 50)
    ELSE
        IF_INT_GREATER_OR_EQUAL(LW(5), 150)
            SYS("sl_c4_review_rave")
            CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
            CALL(evt_item_entry, STRING("sl_rave_sp"), ITEM_STAR_PIECE, LW(0), LW(1), LW(2), ITEM_ENTRY_FIXED_NO_TOUCH, -1, 0)
            CALL(evt_item_get_item, STRING("sl_rave_sp"))
            SET_VALUE(SLF4(F_THIMBLE_SP), 1)
            CALL(evt_pouch_add_coin, 30)
        ELSE
            SYS("sl_c4_review_polite")
            CALL(evt_pouch_add_coin, 20)
        END_IF
    END_IF
    CALL(evt_npc_delete, STRING("sl_judge1"))
    CALL(evt_npc_delete, STRING("sl_judge2"))
    CALL(evt_npc_delete, STRING("sl_judge3"))
    /* the after-party in the foyer */
    CALL(evt_fade_entry, FADE_OUT_BLACK, 600, 0, 0, 0)
    CALL(evt_fade_end_wait)
    CALL(evt_mario_set_pos, 0, 0, 350)
    CALL(evt_npc_set_position, STRING("sl_petunia"), -60, 0, 270)
    CALL(evt_npc_set_position, STRING("sl_hayzee"), 80, 0, 280)
    NPC("sl_party1", "c_kinopiko", SJ4_TOADETTE, -30, 0, 290)
    NPC("sl_party2", "c_nokonoko", SJ4_KOOPA, -90, 0, 250)
    NPC("sl_party3", "c_pansy", SJ4_DAYZEE, 50, 0, 310)
    NPC("sl_party4", "c_kinopio", SJ4_TOAD, 120, 0, 250)
    NPC("sl_jerry", "c_lp_bom", SJ4_JERRY, 100, 0, 320)
    CALL(ch4_prop, STRING("sl_hat"), STRING(MODEL_HAT), STRING("sl_jerry"), 0, 24, 2, STRING("B_1"))
    NPC("sl_blooey", "c_lp_gesso", SJ4_BLOOEY, -120, 0, 310)
    NPC("sl_fanb", "c_kinopio_b", SJ4_TOAD, -150, 0, 320)
    BGM("BGM_EVT_HAPPY1")
    CALL(evt_fade_entry, FADE_IN_BLACK, 600, 0, 0, 0)
    CALL(evt_fade_end_wait)
    SAY("sl_c4_party_jerry", "sl_party4")
    SAY("sl_c4_party_blooey", "sl_fanb")
    SAY("sl_c4_party_00", "sl_party1")
    SAY("sl_c4_party_01", "sl_party2")
    SAY("sl_c4_party_02", "sl_party3")
    /* the review's other half: the night ends the chapter, so whatever the
     * review didn't give is offered here (Old Thimble's Star Piece, the
     * Society's Shine for a patron's donation), not in rooms Luigi won't see */
    CALL_CHILD(party_rewards)
    CALL(evt_mario_set_pose, STRING("M_D_2"))
    LUIGI("sl_c4_party_03")
    CALL(evt_mario_set_pose, STRING("M_S_1"))
    SET_VALUE(SLF4(F_NIGHT), 1)
    /* out of the stage door, into the alley */
    CALL(evt_mario_mov_pos2, 0, 600, FLOAT(80.0f))
    CALL(evt_bero_mapchange, STRING("pik_l41"), STRING("nw_bero"))
    RETURN
EVT_END()

/* the night's ready: Hayzee says go */
static EVT_BEGIN(slam_hint)
    KEY_OFF
    CALL(ch4_who, PORT_SL_FRIEND_HAYZEE, LW(1))
    CALL(evt_msg_print, 0, STRING("sl_c4_slam_go"), 0, LW(1))
    CALL(ch4_cast_off)
    KEY_ON
    RETURN
EVT_END()

/* the orchestra pit's hidden block: Sleepy Stomp */
static EVT_BEGIN(pik_l43_init)
    BEROS(pik_l43_bero)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("hikou"), MAPOBJ_FLAG_HIDE)
    IF_INT_EQUAL(SLF4(F_SLAM), 0)
        CALL(ch4_ready, LW(0))
        IF_INT_EQUAL(LW(0), 1)
            /* the night: the house full, the show */
            CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG3_TOU2"))
            NPC("sl_mc", "c_nokonoko", SJ4_KOOPA, 0, 0, -820)
            FACE("sl_mc", 270)
            CALL(evt_sl_enemy, STRING("sl_slam_e"), STRING(SJ4_AMAZY), STRING(SJ4_AI_STILL), 0, -1000, 0, 0, 0, STRING("BTLNO_PIK_03_01"))
            CALL(ch4_amazy, STRING("sl_slam_e"))
            CALL(evt_npc_flag_onoff, 1, STRING("sl_slam_e"), NPC_FLAG_HIDDEN | NPC_FLAG_INTANGIBLE)
            SPAWN_EVENT(slam_night)
            RETURN
        END_IF
    END_IF
    BGM("BGM_FF_ENV_TOU2")
    NPC("sl_usher", "c_kinopio", SJ4_TOAD, -70, 0, 450)
    TALK("sl_usher", usher_talk)
    NPC("sl_stagehand", "c_bomhey", SJ4_BOMB, 110, 0, -820)
    TALK("sl_stagehand", side_door)
    NPC("sl_goer1", "c_rose", SJ4_RICHMA, 60, 0, 200)
    NPC("sl_goer2", "c_kiza", SJ4_BUTLER, -50, 0, -100)
    TALK("sl_goer1", usher_talk)
    CALL(evt_mobj_brick, STRING("sl_pit"), 0, 60, -700, ITEM_SLEEPY_STOMP, 12, 0, SLF4(F_P43_STOMP))
    RETURN
EVT_END()

/* ======================================================================
 * pik_l44, Backstage (pik_03): the Super Hammer, the stone blocks, Bruno
 * ====================================================================== */

static BeroEntry pik_l44_bero[] = {
    {"s_bero", 1, 8, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "pik_l43", "s_bero", 1, 1, 0, 0},
    {0},
};

static EVT_BEGIN(hammer_chest)
    KEY_OFF
    CALL(evt_mobj_get_position, STRING("sl_bigbox"), LW(0), LW(1), LW(2))
    CALL(evt_item_entry, STRING("sl_hammer"), ITEM_SUPER_HAMMER, LW(0), LW(1), LW(2), ITEM_ENTRY_FIXED_NO_TOUCH, -1, 0)
    CALL(evt_mobj_wait_animation_end, STRING("sl_bigbox"))
    /* the hammer is a key upgrade: the item driver's get runs the area's
     * lecture script for it (lect_juyoitem_rel_set), which a borrowed room
     * hasn't got (a NULL child, the crash). So it's given here and shown
     * held up, to the game's fanfare, as chapter 2 does the boots */
    CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
    ADD(LW(1), 55)
    CALL(evt_item_set_position, STRING("sl_hammer"), LW(0), LW(1), LW(2))
    CALL(ch4_give, ITEM_SUPER_HAMMER)
    CALL(evt_snd_bgmon, PSND_CH(1), STRING("BGM_FF_GET_ITEM1"))
    CALL(evt_mario_set_pose, STRING("M_I_2"))
    SYS("sl_c4_hammer_got")
    CALL(evt_item_delete, STRING("sl_hammer"))
    CALL(evt_mario_set_pose, STRING("M_S_1"))
    SET_VALUE(SLF4(F_HAMMER), 1)
    SYS("sl_c4_hammer_help")
    LUIGI("sl_c4_hammer_00")
    CALL(ch4_who, PORT_SL_FRIEND_TORQUE, LW(1))
    CALL(evt_msg_print, 0, STRING("sl_c4_hammer_01"), 0, LW(1))
    CALL(ch4_cast_off)
    LUIGI("sl_c4_hammer_02")
    KEY_ON
    RETURN
EVT_END()

static EVT_BEGIN(quake_chest)
    KEY_OFF
    CALL(evt_mobj_get_position, STRING("sl_box2"), LW(0), LW(1), LW(2))
    CALL(evt_item_entry, STRING("sl_quake"), ITEM_QUAKE_HAMMER, LW(0), LW(1), LW(2), ITEM_ENTRY_FIXED_NO_TOUCH, -1, 0)
    CALL(evt_mobj_wait_animation_end, STRING("sl_box2"))
    CALL(evt_item_get_item, STRING("sl_quake"))
    KEY_ON
    RETURN
EVT_END()

/* Bruno, at the corridor door: Shake a Leaf breaks him */
static EVT_BEGIN(bruno_talk)
    IF_INT_EQUAL(SLF4(F_BRUNO), 0)
        SAY("sl_c4_bruno_00", "sl_bruno")
        CALL(ch4_who, PORT_SL_FRIEND_HAYZEE, LW(1))
        CALL(evt_msg_print, 0, STRING("sl_c4_bruno_01"), 0, LW(1))
        CALL(ch4_cast_off)
    END_IF
    RETURN
EVT_END()

static EVT_BEGIN(bruno_dance)
    KEY_OFF
    CALL(evt_sl_hz_song, LW(0))
    IF_INT_NOT_EQUAL(LW(0), 1)
        SAY("sl_c4_bruno_hum", "sl_bruno")
        KEY_ON
        RETURN
    END_IF
    CALL(evt_sl_point_off, PORT_SL_ABILITY_SING, 0)
    /* he fights it, shaking, sweating */
    LOOP(6)
        CALL(evt_npc_get_position, STRING("sl_bruno"), LW(0), LW(1), LW(2))
        ADD(LW(0), 2)
        CALL(evt_npc_set_position, STRING("sl_bruno"), LW(0), LW(1), LW(2))
        WAIT_FRAMES(3)
        ADD(LW(0), -4)
        CALL(evt_npc_set_position, STRING("sl_bruno"), LW(0), LW(1), LW(2))
        WAIT_FRAMES(3)
        ADD(LW(0), 2)
        CALL(evt_npc_set_position, STRING("sl_bruno"), LW(0), LW(1), LW(2))
    END_LOOP
    SAY("sl_c4_bruno_02", "sl_bruno")
    /* a spectacular solo */
    BGM("BGM_EVT_STG3_EGG1")
    LOOP(4)
        CALL(evt_npc_get_position, STRING("sl_bruno"), LW(0), LW(1), LW(2))
        CALL(evt_npc_jump_position_nohit, STRING("sl_bruno"), LW(0), LW(1), LW(2), 300, 10)
        CALL(evt_npc_reverse_ry, STRING("sl_bruno"))
    END_LOOP
    SAY("sl_c4_bruno_03", "sl_bruno")
    BEGIN_INLINE_EVENT_GET_ID(LW(15))
        CALL(evt_npc_move_position, STRING("sl_bruno"), 0, -1100, 1200, 0, 4)
    END_INLINE_EVENT
    WAIT_MS(1400) /* on a clock: held up by Luigi it walked on the spot for good */
    DELETE_EVENT(LW(15))
    CALL(evt_npc_delete, STRING("sl_bruno"))
    BGM("BGM_STG6_PIK2")
    SET_VALUE(SLF4(F_BRUNO), 1)
    KEY_ON
    RETURN
EVT_END()

/* the corridor to the dressing room: through the curtain behind the old
 * pedestal at the hall's far end; Bruno keeps it till he's danced off */
static EVT_BEGIN(corridor_watch)
    LOOP(0)
        CALL(ch4_near, 0, -930, 90, LW(0))
        IF_INT_EQUAL(LW(0), 1)
            KEY_OFF
            IF_INT_EQUAL(SLF4(F_BRUNO), 0)
                SAY("sl_c4_bruno_00", "sl_bruno")
                CALL(evt_mario_mov_pos2, 0, -760, FLOAT(80.0f))
                KEY_ON
            ELSE
                CALL(evt_snd_sfxon, STRING("SFX_DOOR_OPEN_WOOD1"), 0)
                CALL(evt_bero_mapchange, STRING("win_l41"), STRING("w_bero"))
                RETURN
            END_IF
        END_IF
        WAIT_FRAMES(1)
    END_LOOP
    RETURN
EVT_END()

static EVT_BEGIN(stone1_broken)
    SET_VALUE(SLF4(F_STONE1), 1)
    RETURN
EVT_END()

static EVT_BEGIN(prop_sign)
    KEY_OFF
    SYS("sl_c4_props")
    KEY_ON
    RETURN
EVT_END()

static EVT_BEGIN(pik_l44_init)
    BGM("BGM_STG6_PIK2")
    BEROS(pik_l44_bero)
    /* the Sanctum's Plane panel (its setup's, not ours) */
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("hikou"), MAPOBJ_FLAG_HIDE)
    /* backstage gloom: the hall washed toward the flies' blue-grey */
    CALL(evt_sl_map_wash, 0, 0x404860, 45)
    CALL(evt_mobj_signboard, STRING("sl_props"), -80, 0, 420, PTR(prop_sign), LSWF(0))
    CALL(evt_mobj_itembox, STRING("sl_bigbox"), 60, 0, 300, 1, 0, PTR(hammer_chest), SLF4(F_HAMMER))
    CALL(evt_mobj_lv_blk, STRING("sl_stone1"), 0, 0, -300, PTR(stone1_broken), SLF4(F_STONE1), STRING("MOBJ_Lv1BigBigBlock"))
    CALL(evt_mobj_lv_blk, STRING("sl_stone2"), -120, 0, 100, 0, SLF4(F_P44_STONE2), STRING("MOBJ_Lv1BigBigBlock"))
    CALL(evt_mobj_itembox, STRING("sl_box2"), -160, 0, 30, 0, 0, PTR(quake_chest), SLF4(F_P44_QUAKE))
    CALL(evt_mobj_brick, STRING("sl_coin1"), 120, 120, 0, 0, 3, 0, SLF4(F_P44_COIN1))
    CALL(evt_mobj_brick, STRING("sl_coin2"), 120, 120, -150, 0, 3, 0, SLF4(F_P44_COIN2))
    CALL(evt_mobj_jumpstand_blue, 0, STRING("sl_crate"), 280, 120, 0, -80, 0, 0)
    IF_INT_EQUAL(SLF4(F_BRUNO), 0)
        NPC("sl_bruno", "c_kuribo_h", SJ4_HGOOMBA, 0, 0, -860)
        ANIM("sl_bruno", "KUR_S_1")
        CALL(evt_npc_set_scale, STRING("sl_bruno"), FLOAT(1.7f), FLOAT(1.7f), FLOAT(1.7f)) /* a bouncer */
        TALK("sl_bruno", bruno_talk)
        CALL(evt_sl_point, PORT_SL_ABILITY_SING, 0, 0, 0, -860, 200, 0, PTR(bruno_dance))
    END_IF
    SPAWN_EVENT(corridor_watch)
    RETURN
EVT_END()

/* ======================================================================
 * win_l41, Dazzlina's Dressing Room (win_05): the Fiery Hat
 * ====================================================================== */

static BeroEntry win_l41_bero[] = {
    {"w_bero", 1, 8, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "pik_l44", "s_bero", 1, 1, 0, 0},
    {0},
};

static EVT_BEGIN(dazzlina_enters)
    KEY_OFF
    BGM("BGM_EVT_3RD_ENEMY")
    NPC("sl_dazz", "c_kpansy", SJ4_AMAZY, -170, 0, 0)
    CALL(ch4_amazy, STRING("sl_dazz"))
    CALL(evt_npc_set_scale, STRING("sl_dazz"), FLOAT(1.2f), FLOAT(1.2f), FLOAT(1.2f))
    CALL(ch4_prop, STRING("sl_tiara"), STRING(MODEL_TIARA), STRING("sl_dazz"), 0, 40, 1, STRING("S_1"))
    /* up to him, across the rug */
    CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
    ADD(LW(0), -90)
    ADD(LW(2), 15)
    CALL(evt_npc_move_position, STRING("sl_dazz"), LW(0), LW(2), 1400, 0, 4)
    CALL(evt_mario_set_dir_npc, STRING("sl_dazz"))
    SAY("sl_c4_dazzlina_00", "sl_dazz")
    LUIGI("sl_c4_dazzlina_01")
    SAY("sl_c4_dazzlina_02", "sl_dazz")
    CALL(evt_mario_set_pose, STRING("M_N_2"))
    LUIGI("sl_c4_dazzlina_03")
    CALL(evt_mario_set_pose, STRING("M_S_1"))
    SAY("sl_c4_dazzlina_04", "sl_dazz")
    /* she sweeps out, sparkling */
    CALL(evt_npc_get_position, STRING("sl_dazz"), LW(0), LW(1), LW(2))
    ADD(LW(1), 20)
    CALL(evt_eff, 0, STRING("kemuri_test"), 4, LW(0), LW(1), LW(2), FLOAT(0.6f), 0, 0, 0, 0, 0, 0, 0)
    CALL(evt_npc_move_position, STRING("sl_dazz"), -200, 0, 900, 0, 4)
    CALL(ch4_del, STRING("sl_tiara"))
    CALL(evt_npc_delete, STRING("sl_dazz"))
    SET_VALUE(SLF4(F_DAZZLINA), 1)
    BGM("BGM_EVT_CYUCYURINA1")
    CALL(ch4_ready, LW(0))
    IF_INT_EQUAL(LW(0), 0)
        SYS("sl_c4_problem_hat_done")
    END_IF
    KEY_ON
    RETURN
EVT_END()

/* the hatbox on the canopy: the Fiery Hat */
static EVT_BEGIN(hat_get)
    KEY_OFF
    CALL(evt_npc_delete, STRING("sl_hatbox"))
    CALL(evt_mario_set_pose, STRING("M_I_2"))
    CALL(evt_snd_sfxon, STRING("SFX_EVT_STARSTONE_SHINE1"), 0)
    SYS("sl_c4_hat_got")
    CALL(evt_mario_set_pose, STRING("M_S_1"))
    SET_VALUE(SLF4(F_HAT), 1)
    KEY_ON
    /* she walks in as he climbs down: off the bed (it stands 22 high) and
     * back on the floor */
    LOOP(0)
        CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
        IF_INT_LESS(LW(1), 5)
            CALL(ch4_near, LW(0), LW(2), 1, LW(3)) /* free to move: not mid-jump */
            IF_INT_EQUAL(LW(3), 1)
                BREAK_LOOP
            END_IF
        END_IF
        WAIT_FRAMES(1)
    END_LOOP
    CALL_CHILD(dazzlina_enters)
    RETURN
EVT_END()

/* the springy mattress: a jump on it bounces Luigi up to the canopy and the hat */
static EVT_BEGIN(hat_watch)
    LOOP(0)
        CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
        IF_INT_GREATER(LW(1), 8)
            CALL(ch4_near, 80, -45, 40, LW(3))
            IF_INT_EQUAL(LW(3), 1)
                BREAK_LOOP
            END_IF
        END_IF
        WAIT_FRAMES(1)
    END_LOOP
    KEY_OFF
    CALL(evt_snd_sfxon, STRING("SFX_MOBJ_JUMP_STAND1"), 0)
    CALL(evt_mario_jump_pos, 105, 95, -110, 600, 0, 40)
    WAIT_MS(300)
    KEY_ON
    CALL_CHILD(hat_get)
    RETURN
EVT_END()

static EVT_BEGIN(flower_chest)
    KEY_OFF
    CALL(evt_mobj_get_position, STRING("sl_vanity"), LW(0), LW(1), LW(2))
    CALL(evt_item_entry, STRING("sl_flower"), ITEM_HAPPY_FLOWER, LW(0), LW(1), LW(2), ITEM_ENTRY_FIXED_NO_TOUCH, -1, 0)
    CALL(evt_mobj_wait_animation_end, STRING("sl_vanity"))
    CALL(evt_item_get_item, STRING("sl_flower"))
    KEY_ON
    RETURN
EVT_END()

static EVT_BEGIN(win_l41_init)
    IF_INT_EQUAL(SLF4(F_DAZZLINA), 0)
        BGM("BGM_EVT_CYUCYURINA1")
    ELSE
        BGM("BGM_EVT_CYUCYURINA1")
    END_IF
    BEROS(win_l41_bero)
    /* the bed bounces Luigi up to the canopy */
    IF_INT_EQUAL(SLF4(F_HAT), 0)
        CALL(evt_npc_entry, STRING("sl_hatbox"), STRING(MODEL_HAT))
        CALL(evt_npc_set_position, STRING("sl_hatbox"), 110, 95, -120)
        CALL(evt_npc_flag_onoff, 1, STRING("sl_hatbox"), NPC_FLAG_INTANGIBLE | NPC_FLAG_NO_SHADOW)
        SPAWN_EVENT(hat_watch)
    END_IF
    CALL(evt_item_entry, STRING("sl_shine1"), ITEM_SHINE_SPRITE, 140, 120, -140, ITEM_ENTRY_FIXED, SLF4(F_W41_SHINE), 0)
    CALL(evt_mobj_itembox, STRING("sl_vanity"), -45, 0, -75, 0, 0, PTR(flower_chest), SLF4(F_W41_FLOWER))
    RETURN
EVT_END()

/* ======================================================================
 * The Hum-Along Hills (Twilight Trail, gra_00-gra_05)
 * ====================================================================== */

/* gra_l40, Hum-Along Gate (gra_00): the sign, the Save Block, Old Thimble's shed */
static BeroEntry gra_l40_bero[] = {
    BERO("w_bero", "pik_l42", "dokan_1"),
    BERO("e_bero", "gra_l41", "w_bero1"),
    {0},
};

static EVT_BEGIN(hills_sign)
    KEY_OFF
    SYS("sl_c4_hills_sign")
    KEY_ON
    RETURN
EVT_END()

/* Old Thimble hears only songs */
static EVT_BEGIN(thimble_talk)
    SAY("sl_c4_thimble_eh", "sl_thimble")
    RETURN
EVT_END()

static EVT_BEGIN(thimble_sung)
    KEY_OFF
    IF_INT_EQUAL(SLF4(F_THIMBLE), 0)
        SET_VALUE(SLF4(F_THIMBLE), 1)
        SAY("sl_c4_thimble_00", "sl_thimble")
        SAY("sl_c4_thimble_01", "sl_thimble")
    END_IF
    IF_INT_EQUAL(SLF4(F_CHOIR), 1)
        IF_INT_EQUAL(SLF4(F_LUCKY), 0)
            SAY("sl_c4_thimble_gift", "sl_thimble")
            CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
            CALL(evt_item_entry, STRING("sl_lucky"), ITEM_LUCKY_DAY, LW(0), LW(1), LW(2), ITEM_ENTRY_FIXED_NO_TOUCH, -1, 0)
            CALL(evt_item_get_item, STRING("sl_lucky"))
            SET_VALUE(SLF4(F_LUCKY), 1)
        END_IF
    END_IF
    IF_INT_EQUAL(SLF4(F_REWARD), 1)
        IF_INT_EQUAL(SLF4(F_THIMBLE_SP), 0)
            /* a "polite" review: Thimble makes it up to the Grass */
            SAY("sl_c4_thimble_sp", "sl_thimble")
            CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
            CALL(evt_item_entry, STRING("sl_tsp"), ITEM_STAR_PIECE, LW(0), LW(1), LW(2), ITEM_ENTRY_FIXED_NO_TOUCH, -1, 0)
            CALL(evt_item_get_item, STRING("sl_tsp"))
            SET_VALUE(SLF4(F_THIMBLE_SP), 1)
        END_IF
    END_IF
    KEY_ON
    SET_VALUE(LW(10), 1)
    SET_VALUE(LW(11), PTR("sl_thimble"))
    CALL_CHILD(ch4_shop)
    RETURN
EVT_END()

static EVT_BEGIN(gra_l40_init)
    BGM("BGM_STG4_GRA1")
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG4_GRA1"))
    BEROS(gra_l40_bero)
    CALL(evt_mobj_signboard, STRING("sl_sign"), -120, 0, 60, PTR(hills_sign), LSWF(0))
    CALL(evt_mobj_save_blk, STRING("mobj_save"), 60, 60, -60, 0, 0)
    CALL(evt_mobj_brick, STRING("sl_coin"), -265, 60, -85, 0, 3, 0, SLF4(F_G40_COIN))
    NPC("sl_thimble", "c_pansy", SJ4_DAYZEE, -180, 0, -20)
    CALL(evt_npc_set_color, STRING("sl_thimble"), 170, 170, 170, 255)
    TALK("sl_thimble", thimble_talk)
    CALL(evt_sl_point, PORT_SL_ABILITY_SING, 0, -180, 0, -20, 160, 0, PTR(thimble_sung))
    CALL(evt_sl_enemy, STRING("sl_e1"), STRING(SJ4_DAYZEE), STRING(SJ4_DAYZEE), 120, 0, 0, 120, 60, STRING("BTLNO_GRA_03_04"))
    CALL(evt_sl_enemy, STRING("sl_e2"), STRING(SJ4_HGOOMBA), STRING(SJ4_AI_GOOMBA), 220, 0, 20, 100, 60, STRING("BTLNO_GRA_01_01"))
    RETURN
EVT_END()

/* gra_l41, Lullaby Lane (gra_01) */
static BeroEntry gra_l41_bero[] = {
    BERO("w_bero1", "gra_l40", "e_bero"),
    BERO("e_bero1", "gra_l42", "w_bero_1"),
    {0},
};

static EVT_BEGIN(fpplus_chest)
    KEY_OFF
    CALL(evt_mobj_get_position, STRING("sl_box"), LW(0), LW(1), LW(2))
    CALL(evt_item_entry, STRING("sl_fpp"), ITEM_FP_PLUS, LW(0), LW(1), LW(2), ITEM_ENTRY_FIXED_NO_TOUCH, -1, 0)
    CALL(evt_mobj_wait_animation_end, STRING("sl_box"))
    CALL(evt_item_get_item, STRING("sl_fpp"))
    KEY_ON
    RETURN
EVT_END()

static EVT_BEGIN(gra_l41_init)
    BGM("BGM_STG4_GRA1")
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG4_GRA1"))
    BEROS(gra_l41_bero)
    CALL(evt_mobj_badgeblk, STRING("sl_shroom"), 0, 60, -100, ITEM_SUPER_SHROOM, 0, SLF4(F_G41_SHROOM), 0)
    CALL(evt_mobj_brick, STRING("sl_coin1"), -200, 60, 100, 0, 3, 0, SLF4(F_G41_COIN1))
    CALL(evt_mobj_brick, STRING("sl_coin2"), 200, 60, 100, 0, 3, 0, SLF4(F_G41_COIN2))
    /* the stone across the back path, and the chest past it */
    CALL(evt_mobj_lv_blk, STRING("sl_stone"), 380, 0, -100, 0, SLF4(F_G41_STONE), STRING("MOBJ_Lv1BigBigBlock"))
    CALL(evt_mobj_itembox, STRING("sl_box"), 500, 0, -120, 0, 0, PTR(fpplus_chest), SLF4(F_G41_FPPLUS))
    CALL(evt_sl_enemy, STRING("sl_e1"), STRING(SJ4_DAYZEE), STRING(SJ4_DAYZEE), -300, 0, 0, 120, 60, STRING("BTLNO_GRA_03_05"))
    CALL(evt_sl_enemy, STRING("sl_e2"), STRING(SJ4_DAYZEE), STRING(SJ4_DAYZEE), 100, 0, 30, 120, 60, STRING("BTLNO_GRA_03_04"))
    CALL(evt_sl_enemy, STRING("sl_e3"), STRING(SJ4_HSPIKY), STRING(SJ4_AI_SPIKY), 300, 0, 0, 120, 60, STRING("BTLNO_GRA_01_04"))
    RETURN
EVT_END()

/* gra_l42, the Old Shed Road (gra_02). The way on is the game's own: the pipe under the shed
 * (its straw is gone: nobody here can blow it away) to the back lane, and west along that */
static EVT_BEGIN(gra_l42_back_in)
    SET_VALUE(LW(0), 0)
    RETURN
EVT_END()

/* Cracks Mario slips through as paper (Paper Mode's slits, which Luigi hasn't got): pushing into
 * one from either side squeezes him through to the other.
 * - gra_l42: the dead tree fills the road's whole depth; the crack is in its trunk's front
 *   (z 100..170). The trunk stops him at x -125 from the west, x -80 from the east.
 * - gra_l43: the trunk at x 138..160 between the front path (from the east) and the back path
 *   (to the west); its slit (HIT_ATTR_PAPER_SLIT) runs from z 25 back to z -40. */
typedef struct {
    f32 x0, x1, z0, z1; /* where he stands */
    s8 axis, sign;      /* the stick pushing in: 0 x, 1 z (the stick's up is -z); its sign */
    s16 tx, tz;         /* where he comes out */
} Ch4Squeeze;

static const Ch4Squeeze sSqueeze42[] = {
    {-150, -110, 100, 170, 0, 1, -60, 140},
    {-95, -60, 100, 170, 0, -1, -145, 140},
    {0},
};
static const Ch4Squeeze sSqueeze43[] = {
    {130, 168, 20, 60, 1, -1, 150, -62},
    {130, 168, -85, -38, 1, 1, 150, 42},
    {0},
};

/* in: the room's squeezes (0 gra_l42, 1 gra_l43); out: where he comes out */
static USER_FUNC(ch4_wait_squeeze) {
    MarioWork* m = marioGetPtr();
    const Ch4Squeeze* q = evtGetValue(event, event->args[0]) ? sSqueeze43 : sSqueeze42;
    (void)isFirstCall;
    if (gp->inBattle || !marioChkKey() || m->position.y > 10.0f) {
        return EVT_RETURN_BLOCK;
    }
    for (; q->x1 != q->x0; q++) {
        const s32 stick = q->axis ? (s8)m->wStickDir2 : (s8)m->wStickDir1;
        if (m->position.x > q->x0 && m->position.x < q->x1 && m->position.z > q->z0 &&
            m->position.z < q->z1 && stick * q->sign > 30) {
            evtSetValue(event, event->args[1], q->tx);
            evtSetValue(event, event->args[2], q->tz);
            return EVT_RETURN_DONE;
        }
    }
    return EVT_RETURN_BLOCK;
}

/* LW(10): the room's squeezes */
static EVT_BEGIN(ch4_squeeze)
    LOOP(0)
        CALL(ch4_wait_squeeze, LW(10), LW(0), LW(2))
        CALL(evt_mario_key_onoff, 0)
        CALL(evt_snd_sfxon, STRING("SFX_MARIO_CHANGE_SLIT1"), 0)
        CALL(evt_mario_jump_pos, LW(0), 0, LW(2), 450, 0, 4)
        CALL(evt_mario_key_onoff, 1)
        WAIT_FRAMES(30)
    END_LOOP
    RETURN
EVT_END()

static EVT_BEGIN(gra_l42_squeeze)
    SET_VALUE(LW(10), 0)
    CALL_CHILD(ch4_squeeze)
    RETURN
EVT_END()

static EVT_BEGIN(gra_l43_squeeze)
    SET_VALUE(LW(10), 1)
    CALL_CHILD(ch4_squeeze)
    RETURN
EVT_END()

static BeroEntry gra_l42_bero[] = {
    BERO("w_bero_1", "gra_l41", "e_bero1"),
    {"w_bero_2", 0x1000, 0, BERO_DIR_W, 242, 21, -450, -1, (s32*)gra_l42_back_in, 0, NULL, "gra_l43", "e_bero", 1, 1, 0, 0},
    {"dokan_1", 2, 0, BERO_DIR_DOWN, BERO_POS_AUTO, 0, 0, -1, NULL, 6, NULL, NULL, "dokan_2", 1, 1, 0, 0},
    {"dokan_2", 0x1002, 0, BERO_DIR_DOWN, BERO_POS_AUTO, 0, 0, -1, NULL, 6, NULL, NULL, "dokan_1", 1, 1, 0, 0},
    {0},
};

static EVT_BEGIN(gra_l42_init)
    BGM("BGM_STG4_GRA1")
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG4_GRA1"))
    BEROS(gra_l42_bero)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("S_wara"), MAPOBJ_FLAG_HIDE)
    CALL(evt_hitobj_onoff, STRING("polySurface981_q023"), 0, 0)
    SPAWN_EVENT(gra_l42_squeeze)
    CALL(evt_sl_enemy, STRING("sl_e1"), STRING(SJ4_DAYZEE), STRING(SJ4_DAYZEE), -280, 0, 0, 100, 60, STRING("BTLNO_GRA_03_04"))
    CALL(evt_sl_enemy, STRING("sl_e2"), STRING(SJ4_HGOOMBA), STRING(SJ4_AI_GOOMBA), 150, 0, 0, 120, 60, STRING("BTLNO_GRA_01_01"))
    RETURN
EVT_END()

/* gra_l43, Moonrise Rise (gra_03): the pond, Shine Sprite #2, a Heart Block */
static BeroEntry gra_l43_bero[] = {
    BERO("e_bero", "gra_l42", "w_bero_2"),
    BERO("w_bero", "gra_l44", "e_bero"),
    {0},
};

/* the spring's bounce has the controls off, and the game picks up no item then: he'd sail through
 * the Shine over it and land empty-handed. Up by it, he catches it */
#define G43_SPRING_X -415
#define G43_SPRING_Z -45
static EVT_BEGIN(gra_l43_shine_catch)
    LOOP(0)
        IF_INT_EQUAL(SLF4(F_G43_SHINE), 1)
            BREAK_LOOP
        END_IF
        CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
        IF_INT_GREATER_OR_EQUAL(LW(1), 140)
            IF_INT_GREATER(LW(0), G43_SPRING_X - 30)
                IF_INT_LESS(LW(0), G43_SPRING_X + 30)
                    IF_INT_GREATER(LW(2), G43_SPRING_Z - 30)
                        IF_INT_LESS(LW(2), G43_SPRING_Z + 30)
                            CALL(evt_item_get_item, STRING("sl_shine2"))
                            SET_VALUE(SLF4(F_G43_SHINE), 1)
                            BREAK_LOOP
                        END_IF
                    END_IF
                END_IF
            END_IF
        END_IF
        WAIT_FRAMES(1)
    END_LOOP
    RETURN
EVT_END()

static EVT_BEGIN(gra_l43_init)
    BGM("BGM_STG4_GRA1")
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG4_GRA1"))
    BEROS(gra_l43_bero)
    CALL(evt_mobj_recovery_blk, STRING("mobj_heart"), 0, -220, 60, -55, 0, 0)
    /* in the pocket by the west exit, at its back: the way past stays open in front (at x -330 the
     * path is one spring wide, and there it barred the trail). 240: up through the Shine at 170 and
     * back; at 330 he rose to y 320 and hung in the canopy */
    CALL(evt_mobj_jumpstand_blue, 0, STRING("sl_stump"), 240, G43_SPRING_X, 0, G43_SPRING_Z, 0, 0)
    CALL(evt_item_entry, STRING("sl_shine2"), ITEM_SHINE_SPRITE, G43_SPRING_X, 170, G43_SPRING_Z, ITEM_ENTRY_FIXED, SLF4(F_G43_SHINE), 0)
    SPAWN_EVENT(gra_l43_shine_catch)
    CALL(evt_mobj_brick, STRING("sl_coin"), -120, 60, 90, 0, 3, 0, SLF4(F_G43_COIN))
    SPAWN_EVENT(gra_l43_squeeze)
    CALL(evt_sl_enemy, STRING("sl_e1"), STRING(SJ4_HCLEFT), STRING(SJ4_AI_CLEFT), 200, 0, 0, 100, 50, STRING("BTLNO_GRA_05_01"))
    CALL(evt_sl_enemy, STRING("sl_e2"), STRING(SJ4_DAYZEE), STRING(SJ4_DAYZEE), -50, 0, 70, 120, 60, STRING("BTLNO_GRA_04_02"))
    CALL(evt_sl_enemy, STRING("sl_e3"), STRING(SJ4_HCLEFT), STRING(SJ4_AI_CLEFT), -200, 0, -60, 100, 50, STRING("BTLNO_GRA_05_01"))
    RETURN
EVT_END()

/* gra_l44, Dayzee Dell (gra_04): the Hum-Along Choir. The way on is the game's own: through the
 * hollow tree (its lid is gone: nobody here can blow it away) to the back lane, and east along
 * that. The scripts are gra_04.c's in_evt, out_evt and out_evt2 */
static EVT_BEGIN(gra_l44_lane_in)
    SET_VALUE(LW(0), 1)
    RETURN
EVT_END()

static EVT_BEGIN(gra_l44_lane_out)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_mario_normalize)
    CALL(evt_mario_get_party, LW(0))
    SET_VALUE(LF(1), 0)
    IF_INT_NOT_EQUAL(LW(0), 0)
        SET_VALUE(LF(1), 1)
        BEGIN_INLINE_EVENT_GET_ID(LW(11))
            CALL(evt_party_stop, 0)
            CALL(evt_party_move_pos2, 0, -375, -25, FLOAT(150.0f))
            CALL(evt_mario_kill_party, 0)
        END_INLINE_EVENT
    END_IF
    CALL(evt_mario_mov_pos2, -375, -30, FLOAT(150.0f))
    WAIT_MS(800)
    CALL(evt_cam3d_evt_set_now)
    IF_INT_EQUAL(LF(1), 1)
        DELETE_EVENT(LW(11))
    END_IF
    WAIT_FRAMES(1)
    CALL(evt_mario_bgmode_on)
    CALL(evt_mario_cam_y_off)
    CALL(evt_mario_kill_party, 0)
    CALL(evt_mario_set_pos, -242, 10, -450)
    CALL(evt_mario_mov_pos2, -190, -450, FLOAT(150.0f))
    CALL(evt_mario_key_onoff, 1)
    CALL(evt_cam_road_reset)
    CALL(evt_cam_shift_reset)
    CALL(evt_cam3d_evt_off, 1000, 11)
    SET_VALUE(LW(0), 1)
    RETURN
EVT_END()

static EVT_BEGIN(gra_l44_lane_back)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_mario_normalize)
    CALL(evt_mario_mov_pos2, -242, -450, FLOAT(150.0f))
    WAIT_MS(800)
    CALL(evt_cam3d_evt_set_now)
    CALL(evt_mario_bgmode_off)
    CALL(evt_mario_cam_y_on)
    CALL(evt_mario_set_pos, -380, 0, -10)
    CALL(evt_mario_set_prev_party_pos, 0, -380, 0, -15)
    CALL(evt_party_stop, 0)
    WAIT_FRAMES(1)
    CALL(evt_party_set_pos, 0, -373, 0, -32)
    CALL(evt_party_run, 0)
    BEGIN_INLINE_EVENT
        WAIT_MS(500)
        CALL(evt_party_move_pos2, 0, -295, -15, FLOAT(150.0f))
    END_INLINE_EVENT
    /* (Mario's -280 is a step from the hollow's floor, w_bero_1: a player still holding left
     * from the lane walked straight back in, round and round. Out a little further, and the
     * controls back once the stick's let go, or after a second) */
    CALL(evt_mario_mov_pos2, -240, -10, FLOAT(150.0f))
    CALL(N_evt_bero_set_mario_bottomless_respawn_pos_to_current_mario_pos)
    CALL(ch4_stick_rest, 60)
    CALL(evt_mario_key_onoff, 1)
    CALL(evt_cam_road_reset)
    CALL(evt_cam_shift_reset)
    CALL(evt_cam3d_evt_off, 1000, 11)
    SET_VALUE(LW(0), 1)
    RETURN
EVT_END()

static BeroEntry gra_l44_bero[] = {
    BERO("e_bero", "gra_l43", "w_bero"),
    {"w_bero_1", 0, 0, BERO_DIR_W, BERO_POS_AUTO, 0, 0, -1, (s32*)gra_l44_lane_in, -1, (s32*)gra_l44_lane_out, "gra_l44", "w_bero_2", 1, 1, 0, 0},
    {"w_bero_2", 0x1000, 0, BERO_DIR_W, BERO_POS_AUTO, 0, 0, -1, (s32*)gra_l44_lane_in, 0, (s32*)gra_l44_lane_back, "gra_l44", "w_bero_1", 1, 1, 0, 0},
    {"e_bero_2", 0x1000, 0, BERO_DIR_E, -132, 63, -450, -1, NULL, 0, NULL, "gra_l45", "w_bero", 1, 1, 0, 0},
    {0},
};

static EVT_BEGIN(choir_scene)
    CALL(evt_seq_wait, 2)
    CALL(ch4_entered)
    KEY_OFF
    BGM("BGM_EVT_QUIZ1")
    CALL(evt_mario_mov_pos2, 200, 0, FLOAT(80.0f))
    SAY("sl_c4_choir_00", "sl_cm")
    CALL(ch4_who, PORT_SL_FRIEND_HAYZEE, LW(1))
    CALL(evt_msg_print, 0, STRING("sl_c4_choir_01"), 0, LW(1))
    CALL(ch4_cast_off)
    SAY("sl_c4_choir_02", "sl_cm")
    /* the fight (superluigi_slam_aurora.c) */
    CALL(evt_sl4_arm, 1)
    CALL(evt_npc_set_battle_info, STRING("sl_cm_e"), 0)
    CALL(evt_npc_battle_start, STRING("sl_cm_e"))
    CALL(evt_npc_wait_battle_end)
    CALL(evt_sl4_arm, 0)
    CALL(evt_npc_get_battle_result, LW(0))
    IF_INT_NOT_EQUAL(LW(0), 1)
        KEY_ON /* fled: the Choir sings on; back in, it starts again */
        RETURN
    END_IF
    SET_VALUE(SLF4(F_CHOIR), 1)
    CALL(evt_npc_delete, STRING("sl_cm_e"))
    ANIM("sl_cm", "PAN_X_1")
    SAY("sl_c4_choir_03", "sl_cm")
    CALL(evt_npc_move_position, STRING("sl_cm"), -600, 0, 2000, 0, 4)
    CALL(evt_npc_delete, STRING("sl_cm"))
    CALL(evt_npc_delete, STRING("sl_cd1"))
    CALL(evt_npc_delete, STRING("sl_cd2"))
    CALL(evt_npc_delete, STRING("sl_cd3"))
    CALL(evt_npc_delete, STRING("sl_cd4"))
    BGM("BGM_STG4_GRA1")
    CALL(evt_item_entry, STRING("sl_sp44"), ITEM_STAR_PIECE, -40, 0, 30, ITEM_ENTRY_FIXED, SLF4(F_G44_SP), 0)
    KEY_ON
    RETURN
EVT_END()

static EVT_BEGIN(gra_l44_init)
    BGM("BGM_STG4_GRA1")
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG4_GRA1"))
    BEROS(gra_l44_bero)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("S_futa"), MAPOBJ_FLAG_HIDE)
    CALL(evt_hitobj_onoff, STRING("A_futa"), 0, 0)
    CALL(evt_mobj_badgeblk, STRING("sl_blk"), 250, 60, -20, ITEM_MUSHROOM, 0, SLF4(F_G44_BLK), 0)
    IF_INT_EQUAL(SLF4(F_CHOIR), 0)
        NPC("sl_cm", "c_kpansy", SJ4_AMAZY, -40, 0, 0)
        CALL(ch4_amazy, STRING("sl_cm"))
        NPC("sl_cd1", "c_pansy", SJ4_DAYZEE, -110, 0, -40)
        NPC("sl_cd2", "c_pansy", SJ4_DAYZEE, -80, 0, 40)
        NPC("sl_cd3", "c_pansy", SJ4_DAYZEE, 0, 0, -40)
        NPC("sl_cd4", "c_pansy", SJ4_DAYZEE, 30, 0, 40)
        ANIM("sl_cd1", "PAN_T_1")
        ANIM("sl_cd3", "PAN_T_1")
        CALL(evt_sl_enemy, STRING("sl_cm_e"), STRING(SJ4_AMAZY), STRING(SJ4_AI_STILL), -40, -1000, 0, 0, 0, STRING("BTLNO_GRA_03_04"))
        CALL(ch4_amazy, STRING("sl_cm_e"))
        CALL(evt_npc_flag_onoff, 1, STRING("sl_cm_e"), NPC_FLAG_HIDDEN | NPC_FLAG_INTANGIBLE)
        SPAWN_EVENT(choir_scene)
    ELSE
        CALL(evt_item_entry, STRING("sl_sp44"), ITEM_STAR_PIECE, -40, 0, 30, ITEM_ENTRY_FIXED, SLF4(F_G44_SP), 0)
        CALL(evt_sl_enemy, STRING("sl_e1"), STRING(SJ4_DAYZEE), STRING(SJ4_DAYZEE), -150, 0, 0, 120, 60, STRING("BTLNO_GRA_03_05"))
        CALL(evt_sl_enemy, STRING("sl_e2"), STRING(SJ4_DAYZEE), STRING(SJ4_DAYZEE), 150, 0, 20, 120, 60, STRING("BTLNO_GRA_03_05"))
    END_IF
    RETURN
EVT_END()

/* gra_l45, the Snoring Woods (gra_05): Petunia, the echo stone, the breaking floor */
/* The way past the boulder is the game's own (gra_05.c): a Spin Jump through the breaking floor (its
 * lid is gone: nobody here can blow it away) drops to the back lane; pushing the lane's rock moves the
 * whole boulder, the hole under it opens, and the two holes in the road are a tunnel past it. Without
 * these entries a player never got past the boulder, and the holes and the hollow held Luigi for good
 * (scripts had gone straight to Petunia). The scripts are gra_05.c's, without its story words. */
static EVT_BEGIN(gra_l45_hole_in)
    SET_VALUE(LW(0), 1)
    RETURN
EVT_END()

/* A_ana_1, the hole the boulder left: up out of A_ana_2, on the east side */
static EVT_BEGIN(gra_l45_hole1_out)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_mario_normalize)
    CALL(evt_cam3d_evt_set_now)
    CALL(evt_mario_cam_y_off)
    CALL(evt_party_stop, 0)
    BEGIN_INLINE_EVENT_GET_ID(LW(10))
        CALL(evt_hit_get_position, STRING("A_ana_1"), LW(0), LW(1), LW(2))
        CALL(evt_party_force_reset_outofscreen, 0)
        CALL(evt_party_move_pos2, 0, LW(0), LW(2), FLOAT(150.0f))
    END_INLINE_EVENT
    LOOP(0)
        IS_EVENT_RUNNING(LW(10), LW(0))
        IF_INT_EQUAL(LW(0), 0)
            BREAK_LOOP
        END_IF
        WAIT_FRAMES(1)
    END_LOOP
    CALL(evt_mario_set_pos, 250, -50, 0)
    CALL(evt_party_set_pos, 0, 250, -50, 0)
    CALL(evt_mario_set_dir, 90, 0, 0)
    CALL(evt_party_set_dir, 0, 90, -1)
    CALL(evt_cam3d_evt_off, 500, 0)
    WAIT_MS(500)
    BEGIN_INLINE_EVENT
        WAIT_MS(400)
        CALL(evt_party_jump_pos, 0, 310, 0, 0, 700, 0, -1000)
        CALL(evt_party_run, 0)
    END_INLINE_EVENT
    CALL(evt_mario_get_pos, 0, LW(1), LW(2), LW(3))
    CALL(evt_snd_sfxon_3d, STRING("SFX_VOICE_MARIO_FLD_JUMP2"), LW(1), LW(2), LW(3), 0)
    CALL(evt_mario_jump_pos, 310, 0, 0, 700, 0, -1000)
    CALL(N_evt_bero_set_mario_bottomless_respawn_pos_to_current_mario_pos)
    CALL(evt_mario_cam_y_on)
    CALL(evt_mario_key_onoff, 1)
    SET_VALUE(LW(0), 1)
    RETURN
EVT_END()

/* A_ana_2: back the other way, up out of A_ana_1 */
static EVT_BEGIN(gra_l45_hole2_out)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_mario_normalize)
    CALL(evt_cam3d_evt_set_now)
    CALL(evt_mario_cam_y_off)
    CALL(evt_party_stop, 0)
    BEGIN_INLINE_EVENT_GET_ID(LW(10))
        CALL(evt_hit_get_position, STRING("A_ana_2"), LW(0), LW(1), LW(2))
        CALL(evt_party_force_reset_outofscreen, 0)
        CALL(evt_party_move_pos2, 0, LW(0), LW(2), FLOAT(150.0f))
    END_INLINE_EVENT
    LOOP(0)
        IS_EVENT_RUNNING(LW(10), LW(0))
        IF_INT_EQUAL(LW(0), 0)
            BREAK_LOOP
        END_IF
        WAIT_FRAMES(1)
    END_LOOP
    CALL(evt_mario_set_pos, 0, -50, 0)
    CALL(evt_party_set_pos, 0, 0, -50, 0)
    CALL(evt_mario_set_dir, 270, 0, 0)
    CALL(evt_party_set_dir, 0, 270, -1)
    CALL(evt_cam3d_evt_off, 500, 0)
    WAIT_MS(500)
    BEGIN_INLINE_EVENT
        WAIT_MS(400)
        CALL(evt_party_jump_pos, 0, -60, 0, 0, 700, 0, -1000)
        CALL(evt_party_run, 0)
    END_INLINE_EVENT
    CALL(evt_mario_get_pos, 0, LW(1), LW(2), LW(3))
    CALL(evt_snd_sfxon_3d, STRING("SFX_VOICE_MARIO_FLD_JUMP4"), LW(1), LW(2), LW(3), 0)
    CALL(evt_mario_jump_pos, -60, 0, 0, 700, 0, -1000)
    CALL(N_evt_bero_set_mario_bottomless_respawn_pos_to_current_mario_pos)
    CALL(evt_mario_cam_y_on)
    CALL(evt_mario_key_onoff, 1)
    SET_VALUE(LW(0), 1)
    RETURN
EVT_END()

/* ana_1, the hollow under the breaking floor: out onto the back lane. The hollow fills the road's
 * whole depth, and the lane only leads back east of it (ana_2 hops him out at x -190), so once the
 * floor was broken the way west to the Choir and town was gone: a player east of it was shut in.
 * Once the rock's moved (the lane done), stepping in from the east hops him out on the west side */
static EVT_BEGIN(gra_l45_lane_out)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_mario_normalize)
    CALL(evt_cam3d_evt_set_now)
    IF_INT_EQUAL(SLF4(F_G45_ROCK), 1)
        CALL(ch4_g45_side, 1, LW(3))
        IF_INT_EQUAL(LW(3), 1)
            CALL(evt_party_stop, 0)
            CALL(evt_mario_set_pos, -260, -60, 0)
            CALL(evt_party_set_pos, 0, -260, -70, -5)
            CALL(evt_cam_road_reset)
            CALL(evt_cam_shift_reset)
            CALL(evt_cam3d_evt_off, 700, 11)
            CALL(evt_mario_set_dir, 270, 0, 0)
            CALL(evt_party_set_dir, 0, 270, -1)
            WAIT_MS(300)
            BEGIN_INLINE_EVENT
                WAIT_MS(400)
                CALL(evt_party_jump_pos, 0, -320, 0, -10, 700, 0, -1000)
                CALL(evt_party_run, 0)
            END_INLINE_EVENT
            CALL(evt_mario_get_pos, 0, LW(1), LW(2), LW(3))
            CALL(evt_snd_sfxon_3d, STRING("SFX_VOICE_MARIO_FLD_JUMP3"), LW(1), LW(2), LW(3), 0)
            CALL(evt_mario_jump_pos, -330, 0, 0, 700, 0, -1000)
            CALL(N_evt_bero_set_mario_bottomless_respawn_pos_to_current_mario_pos)
            CALL(evt_mario_key_onoff, 1)
            SET_VALUE(LW(0), 1)
            RETURN
        END_IF
    END_IF
    CALL(evt_party_stop, 0)
    BEGIN_INLINE_EVENT_GET_ID(LW(10))
        CALL(evt_hit_get_position, STRING("ana_1"), LW(0), LW(1), LW(2))
        CALL(evt_party_force_reset_outofscreen, 0)
        CALL(evt_party_move_pos2, 0, LW(0), LW(2), FLOAT(150.0f))
    END_INLINE_EVENT
    LOOP(0)
        IS_EVENT_RUNNING(LW(10), LW(0))
        IF_INT_EQUAL(LW(0), 0)
            BREAK_LOOP
        END_IF
        WAIT_FRAMES(1)
    END_LOOP
    CALL(evt_party_run, 0)
    CALL(evt_mario_bgmode_on)
    CALL(evt_mario_kill_party, 0)
    CALL(evt_mario_cam_y_off)
    CALL(evt_cam_road_reset)
    CALL(evt_cam_shift_reset)
    CALL(evt_cam3d_evt_off, 700, 11)
    CALL(evt_hit_get_position, STRING("ana_2"), LW(0), LW(1), LW(2))
    SUBTRACT(LW(1), 50)
    ADD(LW(2), -10)
    CALL(evt_mario_set_pos, LW(0), LW(1), LW(2))
    CALL(evt_mario_set_dir, 90, 0, 0)
    CALL(evt_mario_get_pos, 0, LW(1), LW(2), LW(3))
    CALL(evt_snd_sfxon_3d, STRING("SFX_VOICE_MARIO_FLD_JUMP5"), LW(1), LW(2), LW(3), 0)
    CALL(evt_mario_jump_pos, -140, 60, -450, 700, 0, -1000)
    CALL(evt_mario_key_onoff, 1)
    SET_VALUE(LW(0), 1)
    RETURN
EVT_END()

/* ana_2, the lane's end: back down, up out of the hollow */
static EVT_BEGIN(gra_l45_lane_back)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_mario_normalize)
    CALL(evt_cam3d_evt_set_now)
    CALL(evt_hit_get_position, STRING("ana_2"), LW(0), LW(1), LW(2))
    SUBTRACT(LW(1), 50)
    CALL(evt_snd_sfxon_3d, STRING("SFX_VOICE_MARIO_FLD_JUMP1"), LW(0), LW(1), LW(2), 0)
    CALL(evt_mario_jump_pos, LW(0), LW(1), LW(2), 500, 0, -1000)
    CALL(evt_party_stop, 0)
    CALL(evt_mario_set_pos, -260, -60, 0)
    CALL(evt_party_set_pos, 0, -260, -70, -5)
    CALL(evt_mario_bgmode_off)
    CALL(evt_mario_set_prev_party, 0)
    CALL(evt_cam_road_reset)
    CALL(evt_cam_shift_reset)
    CALL(evt_cam3d_evt_off, 700, 11)
    CALL(evt_mario_set_dir, 90, 0, 0)
    CALL(evt_party_set_dir, 0, 90, -1)
    WAIT_MS(300)
    BEGIN_INLINE_EVENT
        WAIT_MS(400)
        CALL(evt_party_jump_pos, 0, -190, 0, -10, 700, 0, -1000)
        CALL(evt_party_run, 0)
    END_INLINE_EVENT
    CALL(evt_mario_get_pos, 0, LW(1), LW(2), LW(3))
    CALL(evt_snd_sfxon_3d, STRING("SFX_VOICE_MARIO_FLD_JUMP3"), LW(1), LW(2), LW(3), 0)
    CALL(evt_mario_jump_pos, -190, 0, 0, 700, 0, -1000)
    CALL(N_evt_bero_set_mario_bottomless_respawn_pos_to_current_mario_pos)
    CALL(evt_mario_key_onoff, 1)
    SET_VALUE(LW(0), 1)
    RETURN
EVT_END()

/* the rock slides, Luigi behind it (gra_05.c's evt_gra_05iwa_move_evt) */
static EVT_BEGIN(gra_l45_rock_moves)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_mapobj_get_position, STRING("S_iwa"), LW(0), LW(1), LW(2))
    CALL(evt_snd_sfxon_3d, STRING("SFX_STG4_MOVE_ROCK1"), LW(0), LW(1), LW(2), LW(9))
    BEGIN_INLINE_EVENT
        CALL(evt_cam_shake, 4, FLOAT(0.0048828125f), FLOAT(0.0f), 3000)
    END_INLINE_EVENT
    CALL(evt_mario_get_pos, 0, LW(2), LW(3), LW(4))
    CALL(evt_mario_set_pose, STRING("M_O_1"))
    CALL(evt_mario_dispflag_onoff, 1, 2)
    CALL(evt_sub_intpl_msec_init, 0, 0, 120, 3000)
    LABEL(0)
    CALL(evt_sub_intpl_msec_get_value)
    CALL(evt_mapobj_trans, 0, STRING("S_iwa"), LW(0), 0, 0)
    SET_VALUE(LW(5), LW(2))
    ADD(LW(5), LW(0))
    CALL(evt_mario_set_pos, LW(5), LW(3), LW(4))
    WAIT_FRAMES(1)
    IF_INT_EQUAL(LW(1), 1)
        GOTO(0)
    END_IF
    CALL(evt_snd_sfxoff, LW(9))
    CALL(evt_mario_dispflag_onoff, 0, 2)
    CALL(evt_mario_set_pose, STRING("M_S_1"))
    CALL(evt_hit_bind_update, STRING("A_iwa"))
    SET_VALUE(SLF4(F_G45_ROCK), 1)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* gra_l45's rock: walking into the lane's end of it and holding right (evt_iwamove.c) */
static USER_FUNC(ch4_rock_watch) {
    static int state, timer;
    MarioWork* mario = marioGetPtr();
    if (isFirstCall) {
        state = 0;
    }
    switch (state) {
        case 0:
            if (mario->hitobjPush != NULL && mario->hitobjPush == hitNameToPtr("A_h_iwa")) {
                timer = 80;
                state = 1;
                marioKeyOff();
                partyKeyOff();
                marioChgPose("M_O_1");
                mario->dispFlags |= MARIO_DISP_FLAG_POSE_LOCK;
                statusWinForceOpen();
            }
            break;
        case 1:
            if (keyGetStickX(0) > 10) {
                if (timer-- == 0) {
                    statusWinForceOff();
                    marioKeyOn(); /* the slide's script holds him now */
                    partyKeyOn();
                    return EVT_RETURN_DONE;
                }
            } else {
                state = 0;
                mario->dispFlags &= ~MARIO_DISP_FLAG_POSE_LOCK;
                marioChgPose("M_S_1");
                marioKeyOn();
                partyKeyOn();
                statusWinForceOff();
            }
            break;
    }
    return EVT_RETURN_BLOCK;
}

static EVT_BEGIN(gra_l45_rock_wait)
    CALL(ch4_rock_watch)
    CALL_CHILD(gra_l45_rock_moves)
    RETURN
EVT_END()

/* Hayzee's word at the boulder, once */
static EVT_BEGIN(gra_l45_boulder_hint)
    LOOP(0)
        CALL(ch4_near, -85, 0, 45, LW(0))
        IF_INT_EQUAL(LW(0), 1)
            BREAK_LOOP
        END_IF
        WAIT_FRAMES(1)
    END_LOOP
    KEY_OFF
    SET_VALUE(SLF4(F_G45_HINT), 1)
    CALL(ch4_who, PORT_SL_FRIEND_HAYZEE, LW(1))
    CALL(evt_msg_print, 0, STRING("sl_c4_boulder"), 0, LW(1))
    CALL(ch4_cast_off)
    KEY_ON
    RETURN
EVT_END()

static BeroEntry gra_l45_bero[] = {
    BERO("w_bero", "gra_l44", "e_bero_2"),
    {"ana_1", 0xF00, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, (s32*)gra_l45_hole_in, 0, (s32*)gra_l45_lane_out, "gra_l45", "ana_2", 4, 4, 0, 0},
    {"ana_2", 0x1F00, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, (s32*)gra_l45_hole_in, 0, (s32*)gra_l45_lane_back, "gra_l45", "ana_1", 4, 4, 0, 0},
    {"A_ana_1", 0xF00, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, (s32*)gra_l45_hole_in, 0, (s32*)gra_l45_hole1_out, NULL, NULL, 4, 4, 0, 0},
    {"A_ana_2", 0xF00, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, (s32*)gra_l45_hole_in, 0, (s32*)gra_l45_hole2_out, NULL, NULL, 4, 4, 0, 0},
    {0},
};

#define ECHO_PHRASE (0 | 2 << 2 | 1 << 4 | 3 << 6) /* A X B Y */

/* A on the stone: it rings its four notes; once Hayzee knows Echo, he says
 * what to do */
static EVT_BEGIN(echo_stone)
    KEY_OFF
    CALL(evt_snd_sfxon, STRING("SFX_MARIO_HAMMER_WOOD_DON1"), 0)
    CALL(evt_sl_hz_echo_play, ECHO_PHRASE)
    IF_INT_EQUAL(SLF4(F_ECHO), 0)
        SYS("sl_c4_echo_stone")
        KEY_ON
        RETURN
    END_IF
    IF_INT_EQUAL(SLF4(F_ECHO_DONE), 0)
        CALL(ch4_who, PORT_SL_FRIEND_HAYZEE, LW(1))
        CALL(evt_msg_print, 0, STRING("sl_c4_echo_hint"), 0, LW(1))
        CALL(ch4_cast_off)
    END_IF
    KEY_ON
    RETURN
EVT_END()

/* Hayzee sings Echo by the stone (its sing point, tune 3): the stone plays,
 * and Luigi sings it back through him */
static EVT_BEGIN(echo_sung)
    KEY_OFF
    CALL(evt_sl_point_off, PORT_SL_ABILITY_SING, 1)
    WAIT_MS(300)
    CALL(evt_sl_hz_echo_play, ECHO_PHRASE)
    SET_VALUE(LW(1), PTR("party"))
    LABEL(1)
    CALL(evt_sl_hz_echo, ECHO_PHRASE, LW(0))
    IF_INT_EQUAL(LW(0), 0)
        CALL(evt_msg_print, 0, STRING("sl_c4_echo_wrong"), 0, LW(1))
        CALL(evt_sl_hz_echo_play, ECHO_PHRASE)
        GOTO(1)
    END_IF
    CALL(evt_msg_print, 0, STRING("sl_c4_echo_right"), 0, LW(1))
    CALL(ch4_cast_off)
    SET_VALUE(SLF4(F_ECHO_DONE), 1)
    CALL(evt_snd_sfxon, STRING("SFX_STAR_POINT_GET1"), 0)
    CALL(evt_item_entry, STRING("sl_sp45"), ITEM_STAR_PIECE, 355, 0, 20, ITEM_ENTRY_FIXED, SLF4(F_G45_SP), 0)
    KEY_ON
    RETURN
EVT_END()

static EVT_BEGIN(petunia_talk)
    KEY_OFF
    IF_INT_EQUAL(SLF4(F_PETUNIA), 0)
        SYS("sl_c4_petunia_zz")
        CALL(ch4_who, PORT_SL_FRIEND_HAYZEE, LW(1))
        CALL(evt_msg_print, 0, STRING("sl_c4_petunia_hint"), 0, LW(1))
        CALL(ch4_cast_off)
    ELSE
        SAY("sl_c4_npc_petunia", "sl_petunia")
    END_IF
    KEY_ON
    RETURN
EVT_END()

static EVT_BEGIN(petunia_wake)
    KEY_OFF
    CALL(evt_sl_hz_song, LW(0))
    IF_INT_NOT_EQUAL(LW(0), 2)
        SAY("sl_c4_petunia_deeper", "party")
        KEY_ON
        RETURN
    END_IF
    CALL(evt_sl_point_off, PORT_SL_ABILITY_SING, 0)
    BGM("BGM_STG4_GRA1")
    ANIM("sl_petunia", "PAN_S_1")
    CALL(evt_npc_get_position, STRING("sl_petunia"), LW(0), LW(1), LW(2))
    CALL(evt_npc_jump_position_nohit, STRING("sl_petunia"), LW(0), LW(1), LW(2), 300, 10)
    CALL(ch4_who, PORT_SL_FRIEND_HAYZEE, LW(9))
    SAY("sl_c4_petunia_00", "sl_petunia")
    CALL(evt_msg_print, 0, STRING("sl_c4_petunia_01"), 0, LW(9))
    SAY("sl_c4_petunia_02", "sl_petunia")
    SAY("sl_c4_petunia_03", "sl_petunia")
    CALL(evt_msg_print, 0, STRING("sl_c4_petunia_04"), 0, LW(9))
    ANIM("sl_petunia", "PAN_Y_1")
    SAY("sl_c4_petunia_05", "sl_petunia")
    ANIM("sl_petunia", "PAN_S_1")
    LUIGI("sl_c4_petunia_06")
    SAY("sl_c4_petunia_07", "sl_petunia")
    /* she teaches him Echo at the stone */
    CALL(evt_sl_hz_echo_play, ECHO_PHRASE)
    SET_VALUE(SLF4(F_ECHO), 1)
    SET_VALUE(SLF4(F_PETUNIA), 1)
    CALL(evt_snd_sfxon, STRING("SFX_EVT_STARSTONE_SHINE1"), 0)
    SYS("sl_c4_echo_learned")
    CALL(evt_msg_print, 0, STRING("sl_c4_echo_try"), 0, LW(9))
    CALL(ch4_cast_off)
    /* she heads back to town */
    BEGIN_INLINE_EVENT_GET_ID(LW(15))
        CALL(evt_npc_move_position, STRING("sl_petunia"), -900, 0, 3000, 0, 4)
    END_INLINE_EVENT
    WAIT_MS(3200) /* on a clock: held up by Luigi it walked on the spot for good */
    DELETE_EVENT(LW(15))
    CALL(evt_npc_delete, STRING("sl_petunia"))
    CALL(evt_sl_point, PORT_SL_ABILITY_SING, 1, 300, 0, -40, 160, 3, PTR(echo_sung))
    CALL(ch4_ready, LW(0))
    IF_INT_EQUAL(LW(0), 0)
        SYS("sl_c4_problem_lady_done")
    END_IF
    KEY_ON
    RETURN
EVT_END()

static EVT_BEGIN(floor_broken)
    SET_VALUE(SLF4(F_G45_FLOOR), 1)
    RETURN
EVT_END()

static EVT_BEGIN(peek_chest)
    KEY_OFF
    CALL(evt_mobj_get_position, STRING("sl_box"), LW(0), LW(1), LW(2))
    CALL(evt_item_entry, STRING("sl_peek"), ITEM_PEEKABOO, LW(0), LW(1), LW(2), ITEM_ENTRY_FIXED_NO_TOUCH, -1, 0)
    CALL(evt_mobj_wait_animation_end, STRING("sl_box"))
    CALL(evt_item_get_item, STRING("sl_peek"))
    KEY_ON
    RETURN
EVT_END()

/* which side of the hollow he's on, every frame (gra_l45_lane_out asks) */
static EVT_BEGIN(gra_l45_side_watch)
    LOOP(0)
        CALL(ch4_g45_side, 0, 0)
        WAIT_FRAMES(1)
    END_LOOP
    RETURN
EVT_END()

static EVT_BEGIN(gra_l45_init)
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG4_GRA1"))
    BEROS(gra_l45_bero)
    SPAWN_EVENT(gra_l45_side_watch)
    CALL(evt_hitobj_onoff, STRING("e_bero"), 1, 0)
    /* the lid over the breaking floor (the game's gale blows it off) */
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("S_futa"), MAPOBJ_FLAG_HIDE)
    CALL(evt_hitobj_onoff, STRING("A_futa"), 0, 0)
    CALL(evt_mobj_signboard, STRING("sl_stone"), 300, 0, -40, PTR(echo_stone), LSWF(0))
    IF_INT_EQUAL(SLF4(F_PETUNIA), 0)
        BGM("BGM_FF_GO_SLEEPING1")
        NPC("sl_petunia", MODEL_PETUNIA, SJ4_DAYZEE, 450, 0, 0)
        ANIM("sl_petunia", "PAN_N_1")
        TALK("sl_petunia", petunia_talk)
        CALL(evt_sl_point, PORT_SL_ABILITY_SING, 0, 450, 0, 0, 180, 0, PTR(petunia_wake))
    ELSE
        BGM("BGM_STG4_GRA1")
        IF_INT_EQUAL(SLF4(F_ECHO_DONE), 1)
            CALL(evt_item_entry, STRING("sl_sp45"), ITEM_STAR_PIECE, 355, 0, 20, ITEM_ENTRY_FIXED, SLF4(F_G45_SP), 0)
        ELSE
            CALL(evt_sl_point, PORT_SL_ABILITY_SING, 1, 300, 0, -40, 160, 3, PTR(echo_sung))
        END_IF
    END_IF
    /* the floor before the boulder: a Spin Jump breaks it, the way to the lane and its rock */
    CALL(evt_mobj_breaking_floor, STRING("sl_floor"), -250, -7, 0, 0, PTR(floor_broken), SLF4(F_G45_FLOOR))
    CALL(evt_hit_bind_mapobj, STRING("A_iwa"), STRING("S_iwa"))
    IF_INT_EQUAL(SLF4(F_G45_ROCK), 1)
        CALL(evt_mapobj_trans, 0, STRING("S_iwa"), FLOAT(120.0f), 0, 0)
        CALL(evt_hit_bind_update, STRING("A_iwa"))
    ELSE
        SPAWN_EVENT(gra_l45_rock_wait)
    END_IF
    IF_INT_EQUAL(SLF4(F_G45_FLOOR), 0)
        IF_INT_EQUAL(SLF4(F_G45_HINT), 0)
            SPAWN_EVENT(gra_l45_boulder_hint)
        END_IF
    END_IF
    /* Peekaboo, past the boulder (the hollow's floor is its way out, no place for a chest) */
    CALL(evt_mobj_itembox, STRING("sl_box"), 500, 0, -50, 0, 0, PTR(peek_chest), SLF4(F_G45_PEEK))
    CALL(evt_sl_enemy, STRING("sl_e1"), STRING(SJ4_HGOOMBA), STRING(SJ4_AI_GOOMBA), -500, 0, 0, 120, 60, STRING("BTLNO_GRA_01_01"))
    CALL(evt_sl_enemy, STRING("sl_e2"), STRING(SJ4_DAYZEE), STRING(SJ4_AI_STILL), -140, 0, 40, 0, 0, STRING("BTLNO_GRA_03_04"))
    ANIM("sl_e2", "PAN_N_1")
    RETURN
EVT_END()

/* ======================================================================
 * The chapter
 * ====================================================================== */

/* seven of ten: the chapter's blocks and Curtain Call's list have room (nine left one free, and the shop's "pockets are full") */
static const short kItems4[] = {ITEM_SUPER_SHROOM, ITEM_SUPER_SHROOM, ITEM_HONEY_SYRUP, ITEM_MAPLE_SYRUP, ITEM_THUNDER_RAGE,
                                ITEM_FIRE_FLOWER, ITEM_VOLT_SHROOM, 0};
static const short kBadges4[] = {ITEM_POWER_BOUNCE, ITEM_POWER_SMASH, ITEM_ICE_SMASH, ITEM_CLOSE_CALL, ITEM_HP_PLUS,
                                 ITEM_MULTIBOUNCE, ITEM_CHARGE, ITEM_SPIKE_SHIELD, ITEM_TIMING_TUTOR, 0};

static const PortSlMap kMaps4[] = {
    {"pik_l40", pik_l40_init, "pik_00", 1},
    {"pik_l41", pik_l41_init, "pik_04", 1},
    {"pik_l42", pik_l42_init, "pik_01", 1},
    {"pik_l43", pik_l43_init, "pik_02", 1},
    {"pik_l44", pik_l44_init, "pik_03", 1},
    {"win_l40", win_l40_init, "win_04", 1},
    {"win_l41", win_l41_init, "win_05", 1},
    {"gra_l40", gra_l40_init, "gra_00", 1},
    {"gra_l41", gra_l41_init, "gra_01", 1},
    {"gra_l42", gra_l42_init, "gra_02", 1},
    {"gra_l43", gra_l43_init, "gra_03", 1},
    {"gra_l44", gra_l44_init, "gra_04", 1},
    {"gra_l45", gra_l45_init, "gra_05", 1},
    {NULL, NULL, NULL, 0},
};

/* The hills' units at Luigi's level (ch4.md, "Unit overrides") */
static const PortSlUnitStats kStats4[] = {
    {UNIT_CRAZEE_DAYZEE, 0, 0, 0, -1, 24},
    {UNIT_HYPER_GOOMBA, 0, 0, 0, -1, 23},
    {UNIT_HYPER_SPIKY_GOOMBA, 0, 0, 0, -1, 24},
    {UNIT_HYPER_CLEFT, 0, 0, 0, -1, 25},
    {UNIT_HYPER_PARAGOOMBA, 0, 0, 0, -1, 23},
    {UNIT_AMAZY_DAYZEE, 15, 5, 0, -1, 60},
    {0},
};


const char* const* portSlSlamText(void);

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

static const char* const kText4[];

/* $TTYD_SL_TEXTCHECK=W (building the text): every line of the chapter's,
 * the Slam's and Hayzee's text measured with the game's font, and each one
 * wider than W pixels (a page's last line, beside the <k> star, W - 62; a
 * system window's, 30 less; W 472 for the speech bubble), or a
 * page of more than three lines, logged once */
static void textCheckList(const char* const* t, int limit) {
    static char buf[1024];
    for (; t[0] != NULL; t += 2) {
        char* line;
        char* next;
        int n = 0, page = 0;
        snprintf(buf, sizeof(buf), "%s", t[1]);
        for (line = buf; *line; line++) {
            if ((unsigned char)*line >= 0x80) {
                *line = 'W'; /* a wide stand-in: the US font's lookup has no multibyte map */
            }
        }
        for (line = buf; line != NULL; line = next) {
            int w, lim = limit, last;
            next = strchr(line, '\n');
            if (next != NULL) {
                *next++ = 0;
            }
            if (strncmp(line, "<p>", 3) == 0) {
                page = 0;
                continue;
            }
            if (line[0] == 0 || strcmp(line, "<k>") == 0 || (line[0] == '<' && strchr(line, '>') != NULL && strchr(line, '>')[1] == 0)) {
                continue;
            }
            last = next != NULL && strncmp(next, "<k>", 3) == 0;
            if (strstr(t[1], "<system>") != NULL) {
                lim -= 30;
            }
            if (last) {
                lim -= 62;
            }
            w = FontGetMessageWidth(line);
            n++;
            page++;
            if (w > lim || page > 3) {
                printf("[ch4 text] %s line %d width %d/%d page line %d: %s\n", t[0], n, w, lim, page, line);
            }
        }
    }
    fflush(stdout);
}

static void textCheck(void) {
    static int done, t;
    const PortSlFriend* hz = portSlGetFriend(PORT_SL_FRIEND_HAYZEE);
    const char* e = getenv("TTYD_SL_TEXTCHECK");
    if (done || e == NULL || gp == NULL || gp->inBattle || !marioChkKey() || ++t < 300) {
        return;
    }
    done = 1;
    {
        int c;
        char one[2] = {0, 0};
        printf("[ch4 text] widths");
        for (c = 32; c < 127; c++) {
            one[0] = (char)(c == 60 ? 32 : c); /* a lone < is a tag */
            printf(" %d", (int)FontGetMessageWidth(one));
        }
        printf("\n");
    }
    textCheckList(kText4, atoi(e));
    textCheckList(portSlSlamText(), atoi(e));
    if (hz != NULL) {
        textCheckList(hz->text, atoi(e));
    }
}

/* tests: $TTYD_SL4_NOFOES clears the rooms' field enemies (walking the chapter's rooms; the Choir's
 * and the Slam's hidden NPCs stay), as chapter 6's $TTYD_SL6_NOFOES does */
static void noFoes(void) {
    NpcWork* w;
    int i;
    if (getenv("TTYD_SL4_NOFOES") == NULL || portSuperLuigiChapter() != 4 || gp == NULL || gp->inBattle) {
        return;
    }
    w = npcGetWorkPtr();
    for (i = 0; i < w->total; i++) {
        NpcEntry* npc = &w->entries[i];
        if ((npc->flags & NPC_FLAG_USED) && (npc->flags & NPC_FLAG_ENEMY) && strcmp(npc->description, "sl_cm_e") != 0 &&
            strcmp(npc->description, "sl_slam_e") != 0) {
            npcDelete(npc);
        }
    }
}

static void setup4(void) {
    portModHostAddHook(1, kSlot_marioStMain, (void*)noFoes);
    portModHostAddHook(1, kSlot_marioStMain, (void*)propsFrame);
    portModHostAddHook(1, kSlot_marioStMain, (void*)textCheck);
    const char* const* t;
    for (t = portSlSlamText(); t[0] != NULL; t += 2) {
        portModsSetMessage(t[0], t[1]);
    }
    portSlHayzeeRegister();
    portSlSlamRegister();
}

static const char* const kText4[];

static const PortSlChapter kChapter4 = {
    "Jazzafrazz Town", "pik_l40", "sl_arrive", "gor_luigi_talk_0004", PORT_SL_FRIEND_HAYZEE, 12, 30, 20, 15, 2, 1, 200,
    kItems4, kBadges4,
    PORT_SL_BIT(PORT_SL_FRIEND_BLOOEY) | PORT_SL_BIT(PORT_SL_FRIEND_JERRY) | PORT_SL_BIT(PORT_SL_FRIEND_TORQUE),
    kMaps4, kText4, kStats4, setup4, "sl4_diary",
};

const PortSlChapter* portSlChapter4(void) {
    return &kChapter4;
}

/* The chapter's text: ours, never the game's (docs/super_luigi/ch4.md). Luigi
 * speaks as "mario", the friend who's out as "party". */
static const char* const kText4[] = {
    "sl4_yesno", "<select 0 1 0 40>\nYes\nNo",
    "sl4_diary", "The Drama Slam was won, and\nthe Dramalama Plaque was the\nMarvelous Compass's next piece.\n<k>\n<p>\nIts needle swung north, to the\nRapturous Ruins. Luigi had\nplayed the Grass. Perfectly.\n<k>",

    /* 1. arrival */
    "sl_c4_platform", "The train's long gone. And the\ncompass says the piece is right\nhere in town anyway!\n<k>",
    "sl_c4_arrive_00", "Jazzafrazz Town. The town\nthat never stops singing.\n<k>\n<p>\nMostly because nobody asks it\nto.\n<k>",
    "sl_c4_arrive_01", "WOW! Look at this place!\nGold! Flowers! Music!\nEverybody's SMILING!\n<k>",
    "sl_c4_arrive_02", "Everybody's smiling.\nSuspicious.\n<k>",
    "sl_c4_arrive_03", "In Plumpbelly we smile only on\nfeast days. And then mostly at\nthe pies.\n<k>",
    "sl_c4_arrive_04", "Too bright.\n<k>",
    "sl_c4_arrive_05", "Come on, guys! The compass says\nthe piece is right here in town!\nThis'll be easy.\n<k>\n<p>\nNo snakes, no karts, no bazookas!\nJust a nice, normal, friendly\ntown!\n<k>",
    "sl_c4_arrive_06", "La la LAAA! Welcome to\nJazzafrazz! Jazz hands!\nJAZZ HANDS!\n<k>",
    "sl_c4_arrive_07", "I hate it here.\n<k>",

    /* 2. the poster */
    "sl_c4_poster_00", "\"THE DRAMA SLAM! Tomorrow\nnight at the Grand\nDramalama!\n<k>\n<p>\n\"Grand prize: the DRAMALAMA\nPLAQUE!\"\n<k>\n<p>\nIn the picture: a curved golden\nplate, glowing.\n<k>",
    "sl_c4_poster_01", "That plaque... that's the compass\npiece! Again! Why do people keep\nputting them on PRIZES?!\n<k>",
    "sl_c4_poster_02", "Perhaps prizes are where shiny\nthings go, sir.\n<k>",
    "sl_c4_poster_03", "The Drama Slam! Only the biggest\nnight in Jazzafrazz! Every troupe\nin town puts on a show,\n<k>\n<p>\nand the Dramalama Society picks\nthe best. Dazzlina's won five\nyears running.\n<k>\n<p>\nShe'll win again. She always wins.\nIt's very boring. We love it.\n<k>",
    "sl_c4_npc_lane_2", "Did you hear? Hayzee's back in\nthe Slam! And his lead is a...\nI'm told it's a grass.\n<k>\n<p>\nA grass! Bold. Very bold.\nDazzlina must be shaking.\n<k>",
    "sl_c4_npc_lane_3", "I saw the grass. I SAW it.\nI'll never look at a lawn the\nsame way again.\n<k>",

    /* 3. Hayzee */
    "sl_c4_hayzee_00", "Gone. All gone. My whole cast.\nTwelve actors, two dancers and\na tuba.\n<k>\n<p>\nPoached. Every last petal.\n<k>",
    "sl_c4_hayzee_01", "Er. Are you OK?\n<k>",
    "sl_c4_hayzee_02", "OK? Baby, I'm a producer with a\nshow and no players. That's not\nOK. That's a tragedy.\n<k>\n<p>\nAnd not the good kind with a\ndance number.\n<k>",
    "sl_c4_hayzee_03", "Name's Hayzee. Hayzee\nProductions. Maybe you've heard\nof me?\n<k>\n<p>\n\"Petals in Peril\"? \"The Wilting\nof Lord Stem\"? Three stars in the\nJazzafrazz Gazette?\n<k>",
    "sl_c4_hayzee_04", "I'm... not from around here.\nI'm Luigi.\n<k>",
    "sl_c4_hayzee_05", "...Luigi. Baby.\nYou have a FACE.\n<k>",
    "sl_c4_hayzee_06", "I... thank you?\n<k>",
    "sl_c4_hayzee_07", "A face for the stage! And your\nfriends! A Blooper! A Bob-omb!\nA... whatever he is!\n<k>",
    "sl_c4_hayzee_08", "Buzzy Beetle.\n<k>",
    "sl_c4_hayzee_09", "A Buzzy Beetle! It's a cast! It's\na CAST! Dazzlina took my players,\nbut you could be my players!\n<k>\n<p>\nThe Drama Slam's tomorrow night!\nWhat do you say?\n<k>",
    "sl_c4_hayzee_10", "Oh! Oh, no, I'm sorry, we can't.\nWe're on a quest. We're looking\nfor a piece of a compass.\n<k>\n<p>\nIt's very important. There's a\nprincess involved.\n<k>",
    "sl_c4_hayzee_11", "A compass? Shiny? Gold? Kind of\ncurved, like a slice of moon?\n<k>",
    "sl_c4_hayzee_12", "...Yes?\n<k>",
    "sl_c4_hayzee_13", "Baby. That's the Dramalama\nPlaque. Whoever wins the Drama\nSlam takes it home.\n<k>",
    "sl_c4_hayzee_14", "So. You want your compass.\nI want my show. Want to put on\na musical?\n<k>",
    "sl_c4_hayzee_15", "...What's it called?\n<k>",
    "sl_c4_hayzee_16", "\"The Mystery of the Fiery Hat\nof Social Awareness.\"\n<k>",
    "sl_c4_hayzee_17", "That's... a lot of title.\n<k>",
    "sl_c4_hayzee_18", "It's a lot of show, baby!\n<k>",
    "sl_c4_hayzee_joined", "<system>\nHayzee joined your party!\n<k>",

    /* 4. Sing */
    "sl_c4_sing_00", "The studio's up on Limelight\nLane. But the Lane Chorus is\nparked on the door, baby.\n<k>\n<p>\nThey'll be there till dawn.\nWatch this. You never ask a\nDayzee to move.\n<k>\n<p>\nYou give 'em a reason\nto dance.\n<k>",
    "sl_c4_sing_help", "<system>\nSing: press <icon PAD_X 0.6 0 22 0> to use Hayzee's\nability and choose a song.\n<k>\n<p>\n\"Shake a Leaf\" sets everyone\nnearby dancing. Crowds dance\nout of the way;\n<k>\n<p>\nfield enemies dance in place\n(walk into one for a first\nstrike).\n<k>\n<p>\n\"Rise and Shine\" wakes anyone\nasleep, and some things that\naren't.\n<k>",
    "sl_c4_sing_hint", "Ugh, the Lane Chorus. They'll be\nthere till dawn. Give 'em a\nreason to dance, baby.\n<k>\n<p>\nShake a Leaf.\n<k>",
    "sl_c4_sing_after", "See? Everybody loves\na number.\n<k>",
    "sl_c4_chorus_busy", "La la la! Not now! We're in the\nmiddle of a sing-off!\nLa la LA!\n<k>",
    "sl_c4_chorus_wrong", "Louder! LOUDER! We will NOT be\nout-sung!\n<k>",
    "sl_c4_chorus_go", "Ooh! A number! Conga! CONGA!\nEverybody, down the lane!\n<k>",
    "sl_c4_door_blocked", "There's a whole choir on the\nstudio's steps. I'm not getting\nthrough that.\n<k>",

    /* 5. casting */
    "sl_c4_casting_00", "OK, people! Places! The story:\nLady Petunia, a milkmaid, finds\na hat by the side of a road.\n<k>\n<p>\nA hat that's on FIRE. Whoever\nwears it understands how\neverybody feels!\n<k>\n<p>\nThe evil Baron Brambleworth\nwants it, so nobody will ever\nbe understood again!\n<k>\n<p>\nThere's a chase! There's a ball!\nThere's a volcano!\n<k>",
    "sl_c4_casting_01", "Why's there a volcano.\n<k>",
    "sl_c4_casting_02", "It's ART, baby. Now. Casting!\n<k>",
    "sl_c4_casting_03", "Blooey! You're the River. You\nflow, you sparkle, you carry Lady\nPetunia to safety in Act Two.\n<k>",
    "sl_c4_casting_04", "...I can do that.\n<k>",
    "sl_c4_casting_05", "Jerry! You're the Fiery Hat!\nYou're the title role, baby!\nYou're ON FIRE!\n<k>",
    "sl_c4_casting_06", "At last, a role worthy of me.\n<k>",
    "sl_c4_casting_07", "Torque! Stage manager and\nspecial effects. Smoke, lights, the\nvolcano.\n<k>",
    "sl_c4_casting_08", "Finally something that makes\nsense.\n<k>",
    "sl_c4_casting_09", "And me? And me? Do I get to be\nthe hero? A dashing knight?\nA prince?\n<k>\n<p>\nI can do a prince! I've been\npractising my prince!\n<k>",
    "sl_c4_casting_10", "Luigi, baby. You've got the most\nimportant role in the show.\n<k>",
    "sl_c4_casting_11", "I KNEW it!\n<k>",
    "sl_c4_casting_12", "You're the Grass.\n<k>",
    "sl_c4_casting_13", "...\n<k>",
    "sl_c4_casting_14", "The what?\n<k>",
    "sl_c4_casting_15", "The Grass! By the side of the\nroad, where Lady Petunia finds\nthe hat!\n<k>\n<p>\nYou're there at the start,\nyou're there at the end, and\nyou're there in between.\n<k>\n<p>\nYou see EVERYTHING. You're\nthe silent witness of it all!\n<k>",
    "sl_c4_casting_16", "Do I have any lines?\n<k>",
    "sl_c4_casting_17", "Grass doesn't talk, baby.\n<k>",
    "sl_c4_casting_18", "Do I get to MOVE?\n<k>",
    "sl_c4_casting_19", "A little rustle. In a breeze.\nIf it feels true.\n<k>",
    "sl_c4_casting_20", "But... I'm green! I'm green and\nI'm the hero of this whole...\nwait. Wait.\n<k>\n<p>\nIs it BECAUSE I'm green?\n<k>",
    "sl_c4_casting_21", "It's because you've got stillness,\nbaby. ...And yes, partly the\ngreen.\n<k>",
    "sl_c4_casting_22", "This is the worst day of my\nlife. And I've been a BRIDE.\n<k>",
    "sl_c4_problems_00", "Three little problems before\ntomorrow night.\n<k>\n<p>\nOne: our Lady Petunia, Petunia\nPrimrose, went to the Hum-Along\nHills to find her motivation.\n<k>\n<p>\nThree days ago.\n<k>\n<p>\nTwo: Dazzlina's people swiped our\nFiery Hat prop.\n<k>\n<p>\nThree: the wind took the script\nout the window this morning. Five\npages, all over town.\n<k>",
    "sl_c4_problems_01", "That's three big problems.\n<k>",
    "sl_c4_problems_02", "It's showbiz! Every show's got\nproblems! Let's go solve 'em!\n<k>",
    "sl_c4_piano_00", "<system>\nLuigi plays three notes.\nBadly.\n<k>",
    "sl_c4_piano_01", "Leave the piano to the\nprofessionals, baby.\n<k>",
    "sl_c4_ready", "The script's back, the hat's back,\nthe lady's back! Tonight's the\nnight, baby!\n<k>\n<p>\nEverybody to the Grand\nDramalama. The theatre on the\nsquare. Curtain's at eight!\n<k>",

    /* 6. the script pages */
    "sl_c4_page_got1", "<system>\nLuigi found Script Page 3!\n<k>",
    "sl_c4_page_got2", "<system>\nLuigi found Script Page 7!\n<k>",
    "sl_c4_page_got3", "<system>\nLuigi found Script Page 12!\n<k>",
    "sl_c4_page_got4", "<system>\nLuigi found Script Page 18!\n<k>",
    "sl_c4_page_got5", "<system>\nLuigi found Script Page 24!\n<k>",
    "sl_c4_page3_read", "Page 3. \"Lady Petunia: Oh, hat!\nOh, burning hat! Why do you\nburn?\"\n<k>\n<p>\n\"The Hat: Because I CARE.\"\n<k>",
    "sl_c4_page3_jerry", "My line. I shall make it sing.\n<k>",
    "sl_c4_page7_read", "Page 7. \"Stage direction: the\nGrass rustles meaningfully.\"\n<k>\n<p>\n...Meaningfully. OK. OK. I can\nrustle meaningfully.\n<k>",
    "sl_c4_page12_read", "Page 12. \"Baron Brambleworth:\nFeelings are for peasants! I am\nNO peasant! I am a BARON!\"\n<k>",
    "sl_c4_page18_read", "Page 18. \"The River carries Lady\nPetunia over the rapids. The\nRiver does not speak,\n<k>\n<p>\nbut the River understands.\"\n<k>",
    "sl_c4_page18_blooey", "Good.\n<k>",
    "sl_c4_page24_read", "Page 24. \"The volcano erupts.\nEverybody is understood. The\nGrass is still there.\"\n<k>\n<p>\n...The Grass is still there. That's\nmy ending. That's my WHOLE\nending.\n<k>",
    "sl_c4_page_count", "<system>\nThe script still has pages\nmissing, all over town.\n<k>",
    "sl_c4_pages_done", "The script is BACK, baby!\nProblem three: solved!\n<k>",
    "sl_c4_clock_look", "The station clock's stuck at\nten past nothing. There's paper\njammed in the works!\n<k>",
    "sl_c4_clock_torque", "Torque could get this open. If\nhe were the one following me.\n<k>",
    "sl_c4_clock_whack", "Maybe if I just... give it a\nlittle tap...\n<k>",
    "sl_c4_clock_done", "Ten past nothing. Some things\nnever change.\n<k>",
    "sl_c4_planter_look", "\"Please mind the planter. It's\ncracked.\" There's a page stuck\nbehind the crack!\n<k>",
    "sl_c4_planter_jerry", "If only something would blow it\nopen. Something like a Bob-omb.\nOut in front. Following me.\n<k>",
    "sl_c4_planter_kick", "Sorry, planter.\n<k>",
    "sl_c4_fountain_look", "\"Please do not throw scripts in\nthe fountain.\" ...There's a page\nat the bottom!\n<k>",
    "sl_c4_fountain_blooey", "It's too deep for me. Blooey\ncould dive for it, if he were\nthe one out with me.\n<k>",
    "sl_c4_fountain_dive", "Hold this thought.\n<k>",
    "sl_c4_fountain_done", "The fountain sparkles. It doesn't\nunderstand.\n<k>",
    "sl_c4_sleeper_zz", "<system>\nA Dayzee doorman is fast\nasleep, sitting on a sheet of\npaper.\n<k>",
    "sl_c4_sleeper_look", "He's sitting on a script page!\nHe's out cold. Maybe a song would\nwake him.\n<k>",
    "sl_c4_sleeper_sway", "He's swaying. He's not waking.\nWrong number, baby.\n<k>",
    "sl_c4_sleeper_wake", "Wha--? I'm awake! I'm on duty!\nI'm... going to get a coffee.\n<k>",

    /* 7. the Super Hammer */
    "sl_c4_hammer_got", "<system>\nLuigi got the Super Hammer!\n<k>",
    "sl_c4_hammer_help", "<system>\nHold <icon PAD_B 0.6 0 22 0> and turn <icon STICK 0.6 0 22 0> round\nto spin the hammer. A spin\nbreaks the big stone blocks.\n<k>",
    "sl_c4_hammer_00", "A hammer! A REAL hammer!\nWait, is this a prop?\n<k>",
    "sl_c4_hammer_01", "Steel. Prop department's been\ncheating.\n<k>",
    "sl_c4_hammer_02", "Finally! A hero's weapon!\n...For the grass.\n<k>",
    "sl_c4_props", "<system>\nThe prop room: fake swords,\npainted trees, rubber\nducks.\n<k>",
    "sl_c4_side_shut", "Backstage? Not today, pal.\nCast and crew only.\n<k>",
    "sl_c4_side_open", "Load-in day! Go on through.\n<k>",

    /* 8. the Fiery Hat */
    "sl_c4_bruno_00", "Dressing room's private. Miss\nDazzlina's orders. Nobody in,\nnobody out, no exceptions,\n<k>\n<p>\nno autographs, no dancing.\n<k>",
    "sl_c4_bruno_01", "(No dancing. Big guy loves to\ndance, baby. Everybody knows.\nHe just won't do it at work.)\n<k>",
    "sl_c4_bruno_hum", "...Hm hm hm. No. I'm not humming.\nYou didn't hear anything.\n<k>",
    "sl_c4_bruno_02", "No... no... must... not...\n<k>",
    "sl_c4_bruno_03", "Don't... tell... ANYBODY!\n<k>",
    "sl_c4_hat_got", "<system>\nLuigi got the Fiery Hat!\n<k>",
    "sl_c4_dazzlina_00", "Well, well. A mustache in my\nboudoir. How common.\n<k>",
    "sl_c4_dazzlina_01", "This hat belongs to Hayzee's\nshow!\n<k>",
    "sl_c4_dazzlina_02", "Hayzee's \"show.\" Darling.\nHayzee hasn't got a show.\n<k>\n<p>\nHayzee's got a Blooper, a\nBob-omb, a beetle and a... what\nare you, exactly?\n<k>",
    "sl_c4_dazzlina_03", "I'm the GRASS!\n<k>",
    "sl_c4_dazzlina_04", "...Hah! HAH! Oh, that's priceless.\nTake the hat, Grass. Take it!\n<k>\n<p>\nI want you on that stage\ntomorrow. I want the whole town\nto see Hayzee's little show.\n<k>\n<p>\nAnd then they'll\nsee ME.\n<k>",
    "sl_c4_problem_hat_done", "<system>\nThe Fiery Hat is back.\nProblem two: solved!\n<k>",

    /* 9. the hills: Thimble, the Choir, Petunia */
    "sl_c4_hills_sign", "<system>\n\"HUM-ALONG HILLS. Do not\nlisten to the Dayzees.\n<k>\n<p>\nDo not sing back. Do not fall\nasleep.\"\n<k>",
    "sl_c4_thimble_eh", "EH?\n<k>",
    "sl_c4_thimble_00", "A SONG! A song, a song, at last a\nsong! Old Thimble sews, and Old\nThimble's NEVER wrong!\n<k>",
    "sl_c4_thimble_01", "I made the GRASS, the GRASS I\nmade, it's my FINEST work, it's\nNOT for TRADE!\n<k>\n<p>\nBut capes and sheets and masks\nI sell, so sing me what you want,\nand sing it WELL!\n<k>",
    "sl_c4_thimble_shop", "What'll it BE, what'll it BE, the\nfinest WARES you'll ever see!\n<k>",
    "sl_c4_thimble_gift", "You beat the CHOIR! The hills are\nFREE! Now take this, take this,\nfrom ME!\n<k>",
    "sl_c4_thimble_sp", "A POLITE review? For MY grass?\nThe critics are FOOLS! Take this,\nand let it PASS!\n<k>",
    "sl_c4_thimble_party", "A STANDING ovation, for the\nGRASS! My GRASS! Take this,\nand wear it with CLASS!\n<k>",
    "sl_c4_choir_00", "Hush, travellers! Hush! The Choir\nis in rehearsal. Nobody passes\nwhile the Choir rehearses.\n<k>\n<p>\nThe Choir is ALWAYS\nin rehearsal.\n<k>",
    "sl_c4_choir_01", "Petunia's past you, baby. We\nneed to get through.\n<k>",
    "sl_c4_choir_02", "Then you'll have to listen to our\nnew piece. It's called \"Sleep\nForever and Ever.\"\n<k>\n<p>\nIt's very soothing. Choir!\nFrom the top!\n<k>",
    "sl_c4_choir_03", "Fine... fine... rehearsal's\nover... we'll pick it up\ntomorrow...\n<k>",
    "sl_c4_petunia_zz", "<system>\nA pink Dayzee lies asleep in\na ring of mushrooms, snoring\nmusically.\n<k>",
    "sl_c4_petunia_hint", "That's my leading lady! She's out\nlike a light. Rise and Shine,\nbaby. Rise and Shine.\n<k>",
    "sl_c4_petunia_deeper", "...She's sleeping deeper. That's\nthe wrong song, baby.\n<k>",
    "sl_c4_petunia_00", "Wh-what? Where am I? What day\nis it? Am I late? Is it the Slam?\nHave I missed my call?\n<k>",
    "sl_c4_petunia_01", "Petunia, baby! It's tomorrow!\nTonight! You've got time!\n<k>",
    "sl_c4_petunia_02", "Oh, thank goodness. I came up\nhere to feel what Lady Petunia\nfeels when she finds the hat.\n<k>\n<p>\nLonely. Lost. A little bit cold.\n<k>\n<p>\nAnd then the Choir sang, and I\nfelt very, very sleepy, and now I\nfeel... motivated!\n<k>\n<p>\nHayzee, I've FOUND IT!\n<k>",
    "sl_c4_petunia_03", "And who's this?\n<k>",
    "sl_c4_petunia_04", "The Grass.\n<k>",
    "sl_c4_petunia_05", "The GRASS! The silent witness!\nOh, it's the hardest part in the\nplay.\n<k>\n<p>\nYou must be an extraordinary\nactor.\n<k>",
    "sl_c4_petunia_06", "Well... I wouldn't say\nextraordinary...\n<k>",
    "sl_c4_petunia_07", "I'll show you a song, Grass, to\nwarm up. Not that you'll need a\nvoice. But Hayzee will.\n<k>\n<p>\nListen to the stone...\n<k>",
    "sl_c4_echo_learned", "<system>\nHayzee learned Echo!\n<k>\n<p>\nAt an echo stone, sing\nback its notes: <icon PAD_A 0.6 0 22 0> <icon PAD_B 0.6 0 22 0> <icon PAD_X 0.6 0 22 0> <icon PAD_Y 0.6 0 22 0>.\n<k>",
    "sl_c4_echo_try", "Sing that stone an Echo, baby,\nand you call the notes. See you\nat the theatre, Petunia!\n<k>",
    "sl_c4_echo_hint", "An echo stone, baby! Let me\nsing it an Echo, and you call\nthe notes.\n<k>",
    "sl_c4_npc_petunia", "Lonely. Lost. A little cold.\nAnd then... the HAT!\n<k>",
    "sl_c4_boulder", "That boulder's not budging from\nthis side, baby. But that patch\nof ground back there...\n<k>\n<p>\nIt sounds hollow. A good Spin\nJump might go right through.\n<k>",
    "sl_c4_echo_stone", "<system>\nA pale stone. It rings with four\nnotes when it's struck.\n<k>",
    "sl_c4_echo_wrong", "From the top!\n<k>",
    "sl_c4_echo_right", "Ooh, that's the one, baby!\nThe stone's humming back!\n<k>",
    "sl_c4_problem_lady_done", "<system>\nPetunia's on her way back to\ntown. Problem one: solved!\n<k>",

    /* 10. Slam night */
    "sl_c4_slam_go", "This is it, baby. The Drama\nSlam! The Grand Dramalama,\non the square. Places!\n<k>",
    "sl_c4_slam_mc_00", "Ladies and gentlemen, beetles\nand blooms! Welcome to the\nDRAMA SLAM!\n<k>",
    "sl_c4_slam_mc_01", "First: the Toad Mime Troupe,\nwith \"A Box.\"\n<k>",
    "sl_c4_slam_still_01", "<system>\nToads mime a box. Silence.\nOne cough.\n<k>",
    "sl_c4_slam_mc_02", "Next: the Koopa Players, with\n\"Shell, Interrupted.\"\n<k>",
    "sl_c4_slam_still_02", "<system>\nA Koopa weeps on a shell.\n<k>",
    "sl_c4_slam_mc_03", "And now, five-time champion,\nthe incomparable Diva Dazzlina,\nin her one-Dayzee show...\n<k>\n<p>\n\"Me!\"\n<k>",
    "sl_c4_slam_still_03", "<system>\nDazzlina sparkles, arms wide.\nThe crowd roars.\n<k>",
    "sl_c4_huddle_00", "OK, people. This is it. Petunia,\nyou're a star. Jerry, you're on\nfire. Blooey, flow.\n<k>\n<p>\nTorque, don't blow up the volcano\nearly.\n<k>",
    "sl_c4_huddle_01", "No promises.\n<k>",
    "sl_c4_huddle_02", "And Luigi... Grass. Baby.\nJust be.\n<k>",
    "sl_c4_huddle_03", "Just be grass.\n<k>",
    "sl_c4_huddle_04", "Just be the best grass this town\nhas ever seen.\n<k>",
    "sl_c4_huddle_05", "...\n<k>\n<p>\nGood grass.\n<k>",
    "sl_c4_slam_mc_04", "And finally! Hayzee Productions\npresents...\n<k>\n<p>\n\"The Mystery of the Fiery Hat\nof Social Awareness!\"\n<k>",

    /* 11. the verdict */
    "sl_c4_verdict_00", "A triumph! The pathos!\nThe hat!\n<k>",
    "sl_c4_verdict_01", "The River moved me. Literally.\nI got wet.\n<k>",
    "sl_c4_verdict_02", "And the grass. I've never seen\ngrass so... still. So committed.\nI believed it was grass.\n<k>",
    "sl_c4_verdict_03", "The Dramalama Society has\ndecided! This year's Slam goes\nto... HAYZEE PRODUCTIONS!\n<k>",
    "sl_c4_verdict_04", "<system>\nThe Dramalama Plaque goes to\nPetunia. She hands it\nto Hayzee.\n<k>\n<p>\nHayzee hands it to Luigi.\n<k>",
    "sl_c4_verdict_05", "It's yours, baby. A deal's a deal.\nCouldn't have done it without\nthe grass.\n<k>",
    "sl_c4_piece", "<system>\nYou got a piece of the\nMarvelous Compass!\n<k>",
    "sl_c4_verdict_06", "Thanks. Yeah. Great. I'm happy.\nI'm so happy. This is what happy\nlooks like.\n<k>",
    "sl_c4_review_ovation", "<system>\nThe Gazette's review: \"A\nstanding ovation!\" The Society\nadds a prize, and 50 coins.\n<k>",
    "sl_c4_review_rave", "<system>\nThe Gazette's review: \"A rave!\"\nThe Society adds a Star Piece,\nand 30 coins.\n<k>",
    "sl_c4_review_polite", "<system>\nThe Gazette's review: \"Polite\napplause.\" The Society adds\n20 coins.\n<k>",

    /* 12. the after-party, the fans */
    "sl_c4_party_jerry", "Darling, the HAT! The HAT!\n<k>",
    "sl_c4_party_blooey", "Blooey! Sign my program! It's\nall wet now, but sign it!\n<k>",
    "sl_c4_party_00", "Oh! Excuse me, I thought you\nwere a plant. Can you hold my\ndrink?\n<k>",
    "sl_c4_party_01", "Have you seen the Grass?\nThey say the Grass is here.\n<k>",
    "sl_c4_party_02", "Is there a draught? That shrub's\nmoving.\n<k>",
    "sl_c4_party_03", "Nobody even knows it was me.\nI was grass. Grass doesn't get\na party. Grass gets nothing.\n<k>\n<p>\nGrass gets mowed.\n<k>",
    "sl_c4_fans_00", "IT'S HIM! IT'S THE GRASS!\n<k>",
    "sl_c4_fans_01", "You were SO STILL!\n<k>",
    "sl_c4_fans_02", "When the volcano went off and\nyou didn't even FLINCH...\n<k>",
    "sl_c4_fans_03", "Can you sign my lawn?\nI brought a piece!\n<k>",
    "sl_c4_fans_04", "Rustle for us! RUSTLE!\n<k>",
    "sl_c4_fans_05", "You... you're here for... ME?\nFor the GRASS?\n<k>",
    "sl_c4_fans_06", "<system>\nGRASS! GRASS! GRASS!\n<k>",
    "sl_c4_fans_07", "I love you all! I love every\nsingle one of you!\n<k>\n<p>\nGrass forever!\n<k>",

    /* 13. the compass, and the voice */
    "sl_c4_voice_00", "The piece... it fits! And it's\nglowing...\n<k>",
    "sl_c4_voice_01", "You're close now. Closer than\nanyone has ever come.\n<k>",
    "sl_c4_voice_02", "Go north, to the white place in\nthe forest, where someone has\nwaited longer than I have.\n<k>",
    "sl_c4_voice_03", "And thank you. Whoever you are,\nwherever you are... thank you for\nnot stopping.\n<k>",
    "sl_c4_voice_04", "Princess... I'd be grass for you.\nI'd be grass every day for you.\nI'd be a whole LAWN.\n<k>",
    "sl_c4_voice_05", "North. Grimble Forest. The\nRapturous Ruins. Here I come!\n<k>",

    /* 14. Hayzee comes along */
    "sl_c4_hz_comes_00", "Thought I'd find you out here.\nRough night, baby?\n<k>",
    "sl_c4_hz_comes_01", "Best night of my life.\n<k>",
    "sl_c4_hz_comes_02", "I've been doing this twenty years.\nKnow what I learned tonight?\nI don't know anything.\n<k>\n<p>\nYou took the smallest part in\nthe show and made it the\nbiggest.\n<k>\n<p>\nThat's not acting. That's\nsomething else.\n<k>",
    "sl_c4_hz_comes_03", "I want to learn it. Wherever\nyou're going, I'm coming.\n<k>\n<p>\nTeacher to student. Producer\nto... whatever you are.\n<k>",
    "sl_c4_hz_comes_04", "I'm grass.\n<k>",
    "sl_c4_hz_comes_05", "You're grass, baby. Let's go.\n<k>",

    /* 15. the tag: Hayzee's version */
    "sl_c4_tag_caption", "<system>\nAs Hayzee tells it...\n<k>",
    "sl_c4_tag_00", "Let's get one thing straight.\nLuigi was the LEAD.\n<k>",
    "sl_c4_tag_01", "The Spirit of the Ground. Ancient.\nEternal. The ground everybody\nelse stands on.\n<k>\n<p>\nWithout him there's no road, no\nhat, no show.\n<k>\n<p>\nI wrote the part for him the\nsecond I saw him. Months before\nI met him.\n<k>",
    "sl_c4_tag_still", "<system>\nLuigi as the Grass, face down\non the stage, one eye\nopen.\n<k>",
    "sl_c4_tag_02", "\"Grass\"? That's what the reviews\ncalled it. Critics. What do they\nknow.\n<k>",
    "sl_c4_tag_03", "And he made a choice, baby.\nA brave choice. To play it silent.\nTo play it still.\n<k>\n<p>\nTo lie face down for two hours\nwhile a volcano went off on his\nback. That's commitment.\n<k>",
    "sl_c4_tag_04", "...I told him to. But he made\nit his.\n<k>",

    /* the town's people */
    "sl_c4_npc_porter_1", "~ Welcome to Jazzafrazz, the\nstation of song ~ the trams\nrun all day long ~\n<k>",
    "sl_c4_npc_porter_2", "~ They say there's a grass in the\nSlam this year ~ a grass with a\nmustache, oh dear, oh dear ~\n<k>",
    "sl_c4_npc_porter_3", "~ The grass, the grass, the grass\nwent by ~ I saw it go and I\nstarted to cry ~\n<k>",
    "sl_c4_npc_couple_1", "Darling, look. Tourists.\nHow quaint.\n<k>",
    "sl_c4_npc_couple_2", "Ahem. Welcome, sir. Do try not\nto sing off-key. We have a\nreputation.\n<k>",
    "sl_c4_npc_couple_3", "A GRASS, darling. In the Slam.\nHayzee's lost his mind.\n<k>",
    "sl_c4_npc_couple_4", "Or found it. I hear it's very\navant-garde.\n<k>",
    "sl_c4_npc_couple_5", "We saw you, young man. The\nstillness. The QUIET. I wept\ninto my pearls.\n<k>",
    "sl_c4_npc_square_1", "Hayzee's been sitting on that\nfountain all morning. Poor thing.\nDazzlina took everybody.\n<k>",
    "sl_c4_npc_square_2", "Grass? In a musical? I don't\nknow whether to laugh or buy a\nticket. I bought a ticket.\n<k>",
    "sl_c4_npc_square_3", "Grass. Grass! I can't stop\nsaying it. GRASS!\n<k>",
    "sl_c4_npc_usher", "Tickets? Tomorrow. Tomorrow\ntickets. Today: no tickets.\n<k>",
    "sl_c4_npc_usher_after", "Tickets? Sold out. Sold out\nfor the grass. Forever.\n<k>",
    "sl_c4_npc_busker_1", "Every song's free, my friend.\nBut tips keep the music going.\nTen coins in the hat?\n<k>",
    "sl_c4_npc_busker_no", "No worries.\nThe music plays on.\n<k>",
    "sl_c4_npc_busker_poor", "Light pockets, heavy heart.\nI know the feeling, my friend.\n<k>",
    "sl_c4_npc_busker_tip1", "Thank you kindly! This one's\nfor you.\n<k>",
    "sl_c4_npc_busker_tip2", "Two tips! You've got rhythm,\nmy friend.\n<k>",
    "sl_c4_npc_busker_gift", "You've got the soul, my friend.\nTake this. It's for the stage.\n<k>",
    "sl_c4_npc_busker_done", "Play it loud on that stage,\nmy friend.\n<k>",
    "sl_c4_pipe_sign", "<system>\n\"To the Hum-Along Hills. Take\nthe pipe. Mind the singing.\"\n<k>",
    "sl_c4_clerk_inn", "The Dramalama Society's guest\nrooms, sir. A night's rest is\n15 coins. Will you stay?\n<k>",
    "sl_c4_clerk_no", "Very good, sir. The rooms will\nbe here.\n<k>",
    "sl_c4_clerk_patron_no", "Very good, sir. The Society\nthanks you all the same.\n<k>",
    "sl_c4_clerk_poor", "I'm afraid that won't cover it,\nsir.\n<k>",
    "sl_c4_clerk_sleep", "Very good, sir. Sleep well.\nNo singing after midnight.\n<k>",
    "sl_c4_clerk_morning", "Good morning, sir! You look\nrested. And green.\nVery green.\n<k>",
    "sl_c4_clerk_patron", "Ah, the Grass! The Society would\nlike to thank you with a Shine\nSprite...\n<k>\n<p>\n...in exchange for a patron's\ndonation of 200 coins. Deal?\n<k>",

    /* the shops */
    "sl4_shop_hi", "Curtain Call Sundries!\nEverything an actor needs.\nWhat'll it be?\n<k>",
    "sl4_shop_thanks", "Thank you! Break a leg!\n<k>",
    "sl4_shop_poor", "Not enough coins for that one,\nI'm afraid.\n<k>",
    "sl4_shop_full", "Your pockets are full! Come back\nwhen you've used something.\n<k>",
    "sl4_shop_bye", "Come back soon!\n<k>",

    /* the cast in the field (as the friend who's out) */
    "sl_c4_hayzee_wait", "Places, baby!\n<k>",
    NULL,
};
