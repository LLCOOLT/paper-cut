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
 * Draw-only and not online: the room changes no flags, items or stats, but
 * visiting moves Mario, so it's off during online co-op and replays. */
#include "port/postgame.h"
#include "port/modes.h"
#include "port/mods.h"
#include "port/mod_host.h"
#include "port/hook_list.h"
#include "port/online.h"
#include "port/playmodes.h"
#include "port/replay.h"
#include "driver/casedrv.h"
#include "driver/effdrv.h"
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
#include <string.h>

#define TROPHY_MAP "tik_t0"

static int sVisitAsked;  /* __atomic */
static int sOnPedestal;  /* Mario stands on the pedestal (a_dai) */
static int sStars;       /* the Crystal Stars set out this visit (bits) */

/* --- the cut-outs ------------------------------------------------------------ */

/* The bosses, each with its battle model's idle pose (the units' pose
 * tables, rels/<area>/src/unit/unit_boss_*.c), small, and the boss refights'
 * index (modes_aurora.c kBosses) that says it's beaten (-1: below). */
typedef struct CutOut {
    const char* model;
    const char* pose;
    float scale;
    int refight; /* portModeBossBeaten's boss, or -1 */
    int flag;    /* for refight -1: a GSWF that says so, or a story value (flag < 0: -story) */
} CutOut;

static const CutOut kCutOuts[] = {
    {"c_gesso", "GSO_N2_1", 0.22f, 1, 0},             /* Blooper */
    {"c_chorobon_k", "CBN_N_1", 0.8f, 2, 0},          /* Gold Fuzzy */
    {"c_gonbaba", "GNB_N_1", 0.16f, 3, 0},            /* Hooktail */
    {"c_majyorin", "MJR_N_1", 0.55f, 4, 0},           /* Beldam (the Shadow Sirens) */
    {"c_maririn", "MRR_N_1", 0.5f, 4, 0},             /* Marilyn */
    {"c_mb_robo_1b", "MGN_S_1", 0.3f, 5, 0},          /* Magnus von Grapple */
    {"c_m_gansu", "N_1", 0.55f, 6, 0},                /* Macho Grubba */
    {"c_ranpel", "N_1", 0.6f, 7, 0},                  /* Doopliss */
    {"c_korutesu1", "KRT_N_1", 0.3f, 8, 0},           /* Cortez */
    {"c_kanbu", "N_1", 0.55f, 9, 0},                  /* Lord Crump */
    {"c_g_moamoa_b", "S_1_3", 0.2f, 10, 0},           /* Smorg */
    {"c_mb_robo_2b", "MGN_S_1", 0.3f, 11, 0},         /* Magnus von Grapple 2.0 */
    {"c_gonbaba_z", "GNB_N_1", 0.16f, 12, 0},         /* Gloomtail */
    {"c_shuryo", "N_1", 0.55f, 13, 0},                /* Sir Grodus */
    {"c_koopa", "KPA_N_3", 0.45f, 14, 0},             /* Bowser */
    {"c_kamek_bb", "N_1", 0.55f, 14, 0},              /* Kammy Koopa */
    {"c_b_peach_b", "KRT_S_1", 0.35f, 15, 0},         /* the Shadow Queen */
    {"c_honezuka", "KRT_Z_1", 0.16f, -1, GSWF_JON_BONETAIL_DEFEATED}, /* Bonetail */
};
#define CUTOUTS ((int)(sizeof(kCutOuts) / sizeof(kCutOuts[0])))

static int cutOutEarned(const CutOut* c) {
    if (c->refight >= 0) {
        return portModeBossBeaten(c->refight);
    }
    return c->flag > 0 ? swGet(c->flag) != 0 : swByteGet(GSW_STORY_PROGRESS) >= -c->flag;
}

/* The Crystal Stars' pedestals (tik_05's own places for them, round the
 * pedestal at (0, -250)) */
static const float kStarAt[7][3] = {
    {0, 50, -390}, {112, 50, -340}, {140, 50, -220}, {62, 50, -120},
    {-62, 50, -120}, {-140, 50, -220}, {-112, 50, -340},
};

/* the cut-outs' places: an arc in front of the pedestal, then the room's sides */
static void cutOutAt(int i, float* x, float* z) {
    static const float kAt[][2] = {
        {-330, -60}, {330, -60}, {-260, 40},  {260, 40},  {-180, 110}, {180, 110}, {-90, 150}, {90, 150},
        {0, 170},    {-400, -160}, {400, -160}, {-420, -300}, {420, -300}, {-330, -420}, {330, -420},
        {-230, -470}, {230, -470}, {0, -520},
    };
    *x = kAt[i % 18][0];
    *z = kAt[i % 18][1];
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
        float x, z;
        cutOutAt(k, &x, &z);
        snprintf(sCutName, sizeof(sCutName), "pc_cut%02d", k);
        evtSetValue(event, args[0], (EvtWord)sCutName); /* evt_npc_entry copies the name */
        evtSetValue(event, args[1], (EvtWord)c->model);
        evtSetValue(event, args[2], (EvtWord)c->pose);
        evtSetFloat(event, args[3], x);
        evtSetFloat(event, args[4], 20.0f);
        evtSetFloat(event, args[5], z);
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
    if (portOnlineActive() || portReplayActive()) {
        *why = "Not during online co-op or a replay.";
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
    int i;
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
