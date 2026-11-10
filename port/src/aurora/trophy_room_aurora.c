/* The trophy room (port/include/port/postgame.h): a room under Rogueport
 * that fills as the file plays. The Crystal Stars the file has stand on the
 * pedestals they stood on when the Thousand-Year Door opened, the bosses it
 * has beaten stand round them as paper cut-outs (their own models, small),
 * and the achievements' plaques show while Mario stands on the pedestal.
 *
 * How the room is added (the safest way): a map of the host's own,
 * "tik_t0", reading the Thousand-Year Door room's files (tik_05: its
 * geometry, the pedestal and the seven sockets) with a script of its own
 * (portModsAddHostMap, as the modes' maps do), so nothing of the game's
 * tik_05 runs: no story scenes, no door, no save block. Its two side ways go
 * where tik_05's do (tik_06 and tik_04); the Door itself stays shut. Entered
 * from the Modes tab or the Start menu's form (portTrophyRoomVisit), from any
 * field room; it's a room like any other from there, so the way out is
 * Rogueport's underground.
 *
 * Draw-only: the room changes no flags, items or stats, but visiting moves
 * Mario, so it's off in replays, and online it goes from the Start menu's
 * form (lockstep: every game of a session in the same frame), never from the
 * settings page (one game's alone). The room is the file's (its stars, its
 * story): the same in every game; the plaques are each player's own
 * achievements, drawn over the picture. */
#include "port/postgame.h"
#include "port/modes.h"
#include "port/mods.h"
#include "port/mod_host.h"
#include "port/hook_list.h"
#include "port/playmodes.h"
#include "port/replay.h"
#include "driver/casedrv.h"
#include "driver/effdrv.h"
#include "driver/hitdrv.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "eff/eff_starstone.h"
#include "evt/evt_bero.h"
#include "evt/evt_case.h"
#include "evt/evt_cmd.h"
#include "evt/evt_eff.h"
#include "evt/evt_map.h"
#include "evt/evt_npc.h"
#include "evt/evt_snd.h"
#include "evt/evt_story.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "pmario_sound.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TROPHY_MAP "tik_t0"

static int sVisitAsked;  /* __atomic */
static int sOnPedestal;  /* Mario stands on the pedestal (a_dai) */
static int sStars;       /* the Crystal Stars set out this visit (bits) */

/* --- the cut-outs ------------------------------------------------------------ */

/* The bosses, each with its battle model's idle pose (the units' pose
 * tables, rels/<area>/src/unit/unit_boss_*.c), small, the boss refights'
 * index (modes_aurora.c kBosses) that says it's beaten (-1: below), and its
 * own place in the room.
 *
 * The places: a horseshoe of two rings round the pedestal, in story order
 * (the long ones, the dragons and Blooper, a step aside so they don't hide
 * the others), open to the camera so nothing stands between it and the
 * pedestal: up the room's left side (the ground floor, beside the pedestal),
 * along the raised floor before the Door (the Door's middle left clear), and
 * down the right side. Each place is clear of its neighbours as the camera
 * sees them from the front of the room (checked from TTYD_TROPHY_TEST's
 * screenshots). Each boss has its own place, so a file that hasn't beaten
 * them all has gaps, not a crowd. Their height is the floor's under them
 * (hitCheckFilter). */
typedef struct CutOut {
    const char* model;
    const char* pose;
    float scale;
    int refight; /* portModeBossBeaten's boss, or -1 */
    int flag;    /* for refight -1: a GSWF that says so, or a story value (flag < 0: -story) */
    float x, z;  /* its place */
} CutOut;

static const CutOut kCutOuts[] = {
    {"c_gesso", "GSO_N2_1", 0.16f, 1, 0, -205, -110}, /* Blooper */
    {"c_chorobon_k", "CBN_N_1", 1.0f, 2, 0, -330, -150}, /* Gold Fuzzy */
    {"c_gonbaba", "GNB_N_1", 0.16f, 3, 0, -360, -300}, /* Hooktail */
    {"c_majyorin", "MJR_N_1", 0.55f, 4, 0, -290, -235}, /* Beldam (the Shadow Sirens) */
    {"c_maririn", "MRR_N_1", 0.5f, 4, 0, -215, -345}, /* Marilyn */
    {"c_mb_robo_1b", "MGN_S_1", 0.3f, 5, 0, -320, -440}, /* Magnus von Grapple */
    {"c_m_gansu", "N_1", 0.45f, 6, 0, -150, -450}, /* Macho Grubba */
    {"c_ranpel", "N_1", 0.6f, 7, 0, -240, -540}, /* Doopliss */
    {"c_korutesu1", "KRT_N_1", 0.3f, 8, 0, -70, -520}, /* Cortez */
    {"c_kanbu", "N_1", 0.55f, 9, 0, 70, -520}, /* Lord Crump */
    {"c_g_moamoa_b", "S_1_3", 0.2f, 10, 0, 240, -540}, /* Smorg */
    {"c_mb_robo_2b", "MGN_S_1", 0.3f, 11, 0, 150, -450}, /* Magnus von Grapple 2.0 */
    /* (Gloomtail is c_gonbaba_b, standing as in battle, las's unit_boss_bunbaba; c_gonbaba_z is the
     * Pit's Bonetail, whose GNB_N_1 lay flat on its side) */
    {"c_gonbaba_b", "GNB_S_3", 0.16f, 12, 0, 320, -440}, /* Gloomtail */
    {"c_shuryo", "N_1", 0.55f, 13, 0, 215, -345}, /* Sir Grodus */
    {"c_koopa", "KPA_N_3", 0.45f, 14, 0, 360, -300}, /* Bowser */
    {"c_kamek_bb", "N_1", 0.65f, 14, 0, 290, -235}, /* Kammy Koopa */
    {"c_b_peach_b", "KRT_S_1", 0.5f, 15, 0, 195, -95}, /* the Shadow Queen */
    {"c_honezuka", "KRT_Z_1", 0.16f, -1, GSWF_JON_BONETAIL_DEFEATED, 330, -150}, /* Bonetail */
};
#define CUTOUTS ((int)(sizeof(kCutOuts) / sizeof(kCutOuts[0])))

static int cutOutEarned(const CutOut* c) {
    if (c->refight >= 0) {
        return portModeBossBeaten(c->refight);
    }
    return c->flag > 0 ? swGet(c->flag) != 0 : swStoryPeek() >= -c->flag;
}

/* The Crystal Stars' pedestals (tik_05's own places for them, round the
 * pedestal at (0, -250)) */
static const float kStarAt[7][3] = {
    {0, 50, -390}, {112, 50, -340}, {140, 50, -220}, {62, 50, -120},
    {-62, 50, -120}, {-140, 50, -220}, {-112, 50, -340},
};

/* the floor's height under a place (the room's ground, or the raised floor
 * before the Door) */
static float floorAt(float x, float z) {
    f32 hx, hy, hz, d = 1000.0f, nx, ny, nz;
    if (hitCheckFilter(NULL, &hx, &hy, &hz, &d, &nx, &ny, &nz, x, 400.0f, z, 0.0f, -1.0f, 0.0f) != NULL) {
        return hy;
    }
    return 0.0f;
}

/* --- the room's script ------------------------------------------------------ */

USER_FUNC(evt_trophy_setup);
USER_FUNC(evt_trophy_next);
USER_FUNC(evt_trophy_pedestal);

static int sCutList[CUTOUTS]; /* the earned cut-outs, kCutOuts indices */
static int sCutCount, sCutNext;
static char sCutName[16];

static BeroEntry sBeros[] = {
    {"w_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "tik_06", "e_bero", 1, 1, 0, 0},
    {"e_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "tik_04", "w_bero", 1, 1, 0, 0},
    {0},
};

static EVT_BEGIN(trophy_on_pedestal)
    CALL(evt_trophy_pedestal, 1)
    RETURN
EVT_END()

static EVT_BEGIN(trophy_off_pedestal)
    CALL(evt_trophy_pedestal, 0)
    RETURN
EVT_END()

static EVT_BEGIN(trophy_map_init)
    SET_VALUE(LW(0), PTR(sBeros))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL(evt_snd_bgmon_f, PSND_FADE_IN | PSND_FADE_OUT, STRING("BGM_STG0_TIK2"), 3000)
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG0_TIK1"))
    CALL(evt_snd_set_rev_mode, 2)
    CALL(evt_mapobj_trans, 1, STRING("hosi_1"), 0, -5, 0)
    CALL(evt_run_case_evt, CASE_COND_STAND_ON, 1, STRING("a_dai"), 0, PTR(trophy_on_pedestal), 0)
    CALL(evt_run_case_evt, CASE_COND_NOT_STAND_ON, 1, STRING("a_dai"), 0, PTR(trophy_off_pedestal), 0)
    CALL(evt_trophy_setup)
    /* each earned cut-out: the game's own NPC, its model and idle pose, small, standing still */
    LOOP(0)
        CALL(evt_trophy_next, LW(0), LW(1), LW(2), LW(3), LW(4), LW(5), LW(6))
        IF_INT_EQUAL(LW(0), 0)
            BREAK_LOOP
        END_IF
        CALL(evt_npc_entry, LW(0), LW(1))
        CALL(evt_npc_flag_onoff, 1, LW(0), NPC_FLAG_INTANGIBLE)
        CALL(evt_npc_set_anim, LW(0), LW(2))
        CALL(evt_npc_set_position, LW(0), LW(3), LW(4), LW(5))
        CALL(evt_npc_set_scale, LW(0), LW(6), LW(6), LW(6))
    END_LOOP
    RETURN
EVT_END()

/* The stars and the cut-outs, as the file has earned them (the map is in). */
USER_FUNC(evt_trophy_setup) {
    PouchData* p = pouchGetPtr();
    int i, n = 0;
    (void)event;
    (void)isFirstCall;
    sStars = p != NULL ? (p->starPowersObtained >> 1) & 0x7F : 0;
    for (i = 0; i < 7; i++) {
        char name[16];
        EffectEntry* e;
        if (!(sStars & (1 << i))) {
            continue;
        }
        e = effStarStoneEntry(i, kStarAt[i][0], kStarAt[i][1], kStarAt[i][2], 1.0f);
        if (e != NULL) {
            snprintf(name, sizeof(name), "pc_trophy%d", i);
            effSetName(e, name);
            ((EffStarStoneWork*)e->userdata)->flags &= (u16)~STARSTONE_FLAG_SHINE_SFX; /* silent, as tik_05's */
        }
    }
    sCutCount = sCutNext = 0;
    for (i = 0; i < CUTOUTS; i++) {
        if (cutOutEarned(&kCutOuts[i])) {
            sCutList[sCutCount++] = i;
        }
    }
    n = sCutCount;
    printf("[trophy] stars %02x, %d cut-outs\n", sStars, n);
    fflush(stdout);
    return EVT_RETURN_DONE;
}

/* out: the next cut-out's NPC name (0 when none is left), model, pose, x, y, z, scale */
USER_FUNC(evt_trophy_next) {
    EvtWord* args = event->args;
    (void)isFirstCall;
    if (sCutNext >= sCutCount) {
        evtSetValue(event, args[0], 0);
        return EVT_RETURN_DONE;
    }
    {
        const int k = sCutNext++;
        const CutOut* c = &kCutOuts[sCutList[k]];
        snprintf(sCutName, sizeof(sCutName), "pc_cut%02d", k);
        evtSetValue(event, args[0], (EvtWord)sCutName); /* evt_npc_entry copies the name */
        evtSetValue(event, args[1], (EvtWord)c->model);
        evtSetValue(event, args[2], (EvtWord)c->pose);
        evtSetFloat(event, args[3], c->x);
        evtSetFloat(event, args[4], floorAt(c->x, c->z));
        evtSetFloat(event, args[5], c->z);
        evtSetFloat(event, args[6], c->scale);
    }
    return EVT_RETURN_DONE;
}

USER_FUNC(evt_trophy_pedestal) {
    (void)isFirstCall;
    sOnPedestal = evtGetValue(event, event->args[0]) != 0;
    return EVT_RETURN_DONE;
}

/* --- the game's frame ------------------------------------------------------- */

int portTrophyRoomHere(void) {
    return gp != NULL && seqGetSeq() == SEQ_GAME && strcmp(gp->currentMapName, TROPHY_MAP) == 0;
}

int portTrophyRoomFull(void) {
    return portTrophyRoomHere() && sStars == 0x7F && sCutCount == CUTOUTS;
}

int portTrophyRoomOnPedestal(void) {
    return portTrophyRoomHere() && sOnPedestal;
}

int portTrophyRoomVisit(const char** why) {
    if (seqGetSeq() != SEQ_GAME || gp == NULL || gp->inBattle || pouchGetPtr() == NULL) {
        *why = "Visit it from the field, with a file loaded.";
        return 0;
    }
    if (portReplayActive()) { /* (online: from the Start menu's form, every game in the same frame) */
        *why = "Not during a replay.";
        return 0;
    }
    if (portTrophyRoomHere()) {
        *why = "Mario is in the trophy room.";
        return 0;
    }
    if (portPitCoopActive() || portChapterActive() || portTowerActive() ||
        (portModeStatus()->mode != PORT_MODE_NONE && !portModeStatus()->finished)) {
        *why = "Not while another mode runs.";
        return 0;
    }
    __atomic_store_n(&sVisitAsked, 1, __ATOMIC_SEQ_CST);
    return 1;
}

void portTrophyRoomFrame(void) {
    static int sTestAsked;
    int i;
    if (!sTestAsked && seqGetSeq() == SEQ_GAME && getenv("TTYD_TROPHY_TEST") != NULL) {
        /* tests: a visit once a game runs (docs/PC_PORT.md) */
        const char* why = "";
        sTestAsked = 1;
        if (!portTrophyRoomVisit(&why)) {
            printf("[trophy] test visit refused: %s\n", why);
        }
    }
    if (__atomic_load_n(&sVisitAsked, __ATOMIC_SEQ_CST) && seqGetSeq() == SEQ_GAME && gp != NULL && !gp->inBattle &&
        !seqCheckSeq() && marioStGetSystemLevel() == 0) { /* a scene or a menu (the Start menu's form): after it */
        __atomic_store_n(&sVisitAsked, 0, __ATOMIC_SEQ_CST);
        sOnPedestal = 0;
        printf("[trophy] visit from %s\n", gp->currentMapName);
        fflush(stdout);
        seqSetSeq(SEQ_MAP_CHANGE, TROPHY_MAP, "w_bero");
        return;
    }
    if (!portTrophyRoomHere()) {
        sOnPedestal = 0;
        return;
    }
    /* the stars turn on their pedestals, as when the Door opened */
    for (i = 0; i < 7; i++) {
        char name[16];
        EffectEntry* e;
        if (!(sStars & (1 << i))) {
            continue;
        }
        snprintf(name, sizeof(name), "pc_trophy%d", i);
        e = effNameToPtr(name);
        if (e != NULL && e->userdata != NULL) {
            EffStarStoneWork* w = (EffStarStoneWork*)e->userdata;
            w->rotY = w->rotY + 2.0f >= 360.0f ? w->rotY + 2.0f - 360.0f : w->rotY + 2.0f;
        }
    }
}

void portTrophyRoomRegisterMap(void) {
    portModsAddHostMap(TROPHY_MAP, trophy_map_init, "tik_05");
}
