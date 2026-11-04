/* Super Luigi's chapter 2, "Plumpbelly Village" (docs/super_luigi/ch2.md):
 * a snowbound Bob-omb village on Fahr Outpost's maps (bom_20-bom_24), the
 * snake's grotto on Pirate's Grotto's (dou_20-dou_30, tinted cold) and its den
 * on Hooktail's lair (gon_20). Jerry joins in the square; Luigi goes up the
 * Bridal Road in a gown; the guards let a bride pass while she walks like
 * one; Sergeant Bulkhead sees through it; Hizza is beaten by stepping aside.
 *
 * Each room is a map of the mods' host maps on borrowed geometry with an
 * init of ours (noSetup: none of the borrowed map's enemies or people). Its
 * battles: superluigi_hizza_aurora.c (the formations, the mid-boss and the
 * boss); Jerry: superluigi_jerry_aurora.c. */
#include "port/superluigi.h"
#include "port/superluigi_evt.h"
#include "port/luigi.h"
#include "port/mods.h"
#include "port/mod_host.h"
#include "port/hook_list.h"
#include "port/platform.h"
#include "data/item_data.h"
#include "data/mapdata.h"
#include "driver/animdrv.h"
#include "driver/hitdrv.h"
#include "driver/itemdrv.h"
#include "driver/mobjdrv.h"
#include "driver/npcdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_damage.h"
#include "evt/evt_door.h"
#include "evt/evt_kinopio.h"
#include "evt/evt_hit.h"
#include "evt/evt_shop.h"
#include "evt/evt_story.h"
#include "evt/evt_win.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mario.h"
#include "mario/mario_party.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "data_tables.h"
#include "pmario_sound.h"
#include "system.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The game's Japanese names (tribes, AI types) as Shift-JIS: the port's own
 * sources aren't converted (port/include/port/superluigi_sjis.h's note) */
#define SJ2_GOOMBA "\x83" "\x4E" "\x83" "\x8A" "\x83" "\x7B" "\x81" "\x5B"                                   /* クリボー */
#define SJ2_ICE_PUFF "\x83" "\x75" "\x83" "\x8A" "\x83" "\x55" "\x81" "\x5B" "\x83" "\x68"                     /* ブリザード */
#define SJ2_FROST_PIRANHA "\x83" "\x41" "\x83" "\x43" "\x83" "\x58" "\x83" "\x70" "\x83" "\x62" "\x83" "\x4E" "\x83" "\x93" /* アイスパックン */
#define SJ2_PARABUZZY "\x83" "\x70" "\x83" "\x5E" "\x83" "\x81" "\x83" "\x62" "\x83" "\x67"                    /* パタメット */
#define SJ2_BULKY "\x83" "\x77" "\x83" "\x72" "\x81" "\x5B" "\x83" "\x7B" "\x83" "\x80"                        /* ヘビーボム */
#define SJ2_BILL_BLASTER "\x83" "\x4C" "\x83" "\x89" "\x81" "\x5B" "\x91" "\xE5" "\x96" "\x43"                 /* キラー大砲 */
#define SJ2_EMBER "\x83" "\x6F" "\x83" "\x75" "\x83" "\x8B"                                                   /* バブル */
#define SJ2_MAYOR "\x83" "\x7B" "\x83" "\x80" "\x91" "\xBA" "\x91" "\xBA" "\x92" "\xB7"                        /* ボム村村長 */
#define SJ2_VILLAGER "\x83" "\x7B" "\x83" "\x80" "\x91" "\xBA" "\x91" "\xBA" "\x90" "\x6C"                     /* ボム村村人 */
#define SJ2_AI_PUFF "\x83" "\x68" "\x83" "\x4E" "\x83" "\x4B" "\x83" "\x62" "\x83" "\x58" "\x83" "\x93"        /* ドクガッスン */
#define SJ2_AI_PIRANHA "\x83" "\x70" "\x83" "\x62" "\x83" "\x4E" "\x83" "\x93" "\x83" "\x74" "\x83" "\x89" "\x83" "\x8F" "\x81" "\x5B" /* パックンフラワー */

/* chapter 2's flags: the framework's bank (SL_GSWF, 0-99) */
#define SLF(n) SL_GSWF(2, n)
enum {
    F_ARRIVED, F_GATE, F_MAYOR, F_JERRY, F_LANE, F_TAILOR, F_SILK, F_PEARLS, F_GOWN, F_PROCESSION,
    F_HOLLOW, F_BOOTS, F_WINCH, F_GATE_UP, F_BULKHEAD, F_WALL29, F_DEN, F_HIZZA, F_FEAST, F_PROPOSAL,
    F_VOICE, F_TREE, F_NOOK, F_BERTHA, F_HAPPY, F_SIDEWALL, F_PIE, F_GUARD_D20A, F_GUARD_D20B, F_GUARD_D21,
    F_GUARD_D24A, F_GUARD_D24B, F_GUARD_D27A, F_GUARD_D27B, F_PANEL25, F_PLANK26, F_PANEL30, F_BRIDES,
    /* pickups */
    F_P20_COIN = 40, F_P20_PIECE, F_P21_ITEM, F_P21_COIN1, F_P21_COIN2, F_P21_BADGE, F_P21_SHINE, F_P21_PIECE,
    F_P22_ITEM, F_P22_HEART, F_P22_HPPLUS, F_P23_PIECE, F_P24_PIECE, F_P24_SHINE, F_D20_COIN, F_D21_CHEST,
    F_D22_ITEM, F_D23_PIECE, F_D23_COIN, F_D25_BADGE, F_D26_BADGE, F_D27_COIN1, F_D27_COIN2, F_D27_COIN3,
    F_D28_ITEM, F_D29_PIECE, F_D30_SHINE, F_D30_BADGE, F_D29_HEART, F_P22_PANEL,
};

USER_FUNC(evt_sl_c2_arm); /* superluigi_hizza_aurora.c */
USER_FUNC(evt_luigi_story_pose);
void portSlC2BattleRegister(void);
int portSlJerryBlasting(void);

#define BLOOEY 1 /* Goombella's slot */
#define JERRY 3  /* Bobbery's */

/* ======================================================================
 * Shared pieces
 * ====================================================================== */

/* out: the partner out now (1 Blooey, 3 Jerry, 0 none) */
static USER_FUNC(c2_out) {
    (void)isFirstCall;
    /* marioGetPartyId is the party entry's slot (-1 for none), not who it is */
    PartyEntry* party = partyGetPtr(marioGetPartyId());
    evtSetValue(event, event->args[0], party != NULL ? party->currentMemberId : 0);
    return EVT_RETURN_DONE;
}

/* out: Luigi's motion (0 standing, 29 a pipe's exit, ...) */
static USER_FUNC(c2_mario_motion) {
    (void)isFirstCall;
    evtSetValue(event, event->args[0], marioGetPtr()->currMotionId);
    return EVT_RETURN_DONE;
}

/* in: an NPC and its talk script, as a variable's value: evt_npc_change_interrupt
 * takes its script as written, so given LW(6) it set the variable's own word as the
 * talk, and talking to a villager ran garbage */
static USER_FUNC(c2_set_talk) {
    NpcEntry* npc = npcNameToPtr_NoAssert((const char*)evtGetValue(event, event->args[0]));
    (void)isFirstCall;
    if (npc != NULL) {
        npc->talkEvt = (void*)evtGetValue(event, event->args[1]);
    }
    return EVT_RETURN_DONE;
}

/* in: an item: into the pouch, no scene (the boots, the pie) */
static USER_FUNC(c2_give) {
    (void)isFirstCall;
    pouchGetItem((ItemType)evtGetValue(event, event->args[0]));
    return EVT_RETURN_DONE;
}

/* out: 1 if Jerry's Blast is going (a scene waits for it to finish) */
static USER_FUNC(c2_blasting) {
    (void)isFirstCall;
    evtSetValue(event, event->args[0], portSlJerryBlasting());
    return EVT_RETURN_DONE;
}

/* in: a hit object's name: out: its place (x, y, z) */
static USER_FUNC(c2_hit_pos) {
    EvtWord* a = event->args;
    const char* name = (const char*)evtGetValue(event, a[0]);
    Vec p = {0.0f, 0.0f, 0.0f};
    (void)isFirstCall;
    if (hitNameToPtr(name) != NULL) {
        hitObjGetPos(name, &p);
    }
    evtSetValue(event, a[1], (s32)p.x);
    evtSetValue(event, a[2], (s32)p.y);
    evtSetValue(event, a[3], (s32)p.z);
    return EVT_RETURN_DONE;
}

/* in: an NPC and a pose: played if that NPC is here (a prop that may not have landed) */
static USER_FUNC(c2_anim) {
    NpcEntry* npc = npcNameToPtr_NoAssert((const char*)evtGetValue(event, event->args[0]));
    (void)isFirstCall;
    if (npc != NULL) {
        strcpy(npc->currentAnim, (const char*)evtGetValue(event, event->args[1]));
        animPoseSetAnim(npc->poseId, npc->currentAnim, 1);
    }
    return EVT_RETURN_DONE;
}

/* out: 1 while Luigi is the player's to move (no scene holds him) */
static USER_FUNC(c2_can_act) {
    (void)isFirstCall;
    evtSetValue(event, event->args[0], marioChkKey() ? 1 : 0);
    return EVT_RETURN_DONE;
}

/* in: a flag (its GSWF number): set */
static USER_FUNC(c2_flag_on) {
    (void)isFirstCall;
    swSet(evtGetValue(event, event->args[0]));
    return EVT_RETURN_DONE;
}

/* in: one of our flags by its index (F_*): out: it */
static USER_FUNC(c2_flag_get) {
    (void)isFirstCall;
    evtSetValue(event, event->args[1], swGet(SL_FLAG(2, 0) + evtGetValue(event, event->args[0])));
    return EVT_RETURN_DONE;
}

/* The bride: Luigi's model in the field (and battle) while he wears the gown */
static USER_FUNC(c2_bride) {
    (void)isFirstCall;
    portLuigiSetCostume(evtGetValue(event, event->args[0]) ? "c_luigi_bride" : NULL);
    return EVT_RETURN_DONE;
}

/* in: an NPC's name: kept lit inside a house, as the door does for the NPCs it lists (npc_dark: the town's
 * blend left them as a shadow only). For NPCs that come in during a scene indoors. "party" is skipped */
static USER_FUNC(c2_lit) {
    const char* name = (const char*)evtGetValue(event, event->args[0]);
    NpcEntry* npc = name != NULL && strcmp(name, "party") != 0 ? npcNameToPtr_NoAssert(name) : NULL;
    (void)isFirstCall;
    if (npc != NULL) {
        animPoseSetMaterialFlagOn(npc->poseId, 0x1800);
    }
    return EVT_RETURN_DONE;
}

/* in: model name: out: 1 if this build has the model (a prop the models work hasn't landed yet: skipped) */
static USER_FUNC(c2_has_model) {
    char path[64];
    size_t size = 0;
    (void)isFirstCall;
    snprintf(path, sizeof(path), "a/%s", (const char*)evtGetValue(event, event->args[0]));
    evtSetValue(event, event->args[1], portModsGetHostFile(path, &size) != NULL);
    return EVT_RETURN_DONE;
}

/* The game's enemy first strike for the next battle (as tou_03 does for its match) */
static USER_FUNC(c2_enemy_first) {
    FieldBattleData* fbat = fbatGetPointer();
    (void)event;
    (void)isFirstCall;
    fbat->hitKind = 0x10000000;
    fbat->firstAttackParam = 0x10;
    fbat->firstAttackFlags &= ~1;
    fbatSetAttackAnnounce(0x10000000);
    fbatSetAttackAnnounceEnable();
    return EVT_RETURN_DONE;
}

/* $TTYD_SL_C2_GRID: the room's floors surveyed for the tour, top-down, to <shots>/<map>.grid:
 * x z y name, for each surface a ray down meets; then every map object and NPC with the floor under it */
static BOOL c2_grid_filter(HitCheckQuery* q, HitEntry* e) {
    (void)q;
    return (e->attributes & HIT_ATTR_MOBJ) == 0;
}
static float c2_floor_under(float x, float y, float z, const char** name, HitEntry** hit) {
    f32 hx, hy, hz, nx, ny, nz, dist = 4000.0f;
    HitEntry* e = hitCheckFilter(c2_grid_filter, &hx, &hy, &hz, &dist, &nx, &ny, &nz, x, y, z, 0.0f, -1.0f, 0.0f);
    if (e == NULL) {
        return -9999.0f;
    }
    if (name != NULL) {
        *name = hitGetName(e);
    }
    if (hit != NULL) {
        *hit = e;
    }
    return hy;
}
static void c2_survey(void) {
    static const char* const kNames[] = {"mobj_save", "mobj_coin", "mobj_coin1", "mobj_coin2", "mobj_coin3", "mobj_item",
                                         "mobj_hidden", "mobj_heart", "mobj_hpplus", "box_c2", "box_boots", "pblk_c2",
                                         "mobj_jump00", "mobj_jump01", "kururin1", NULL};
    const char* dir = getenv("TTYD_SHOT_DIR");
    char path[512];
    FILE* f;
    int x, z, i;
    NpcWork* w = npcGetWorkPtr();
    snprintf(path, sizeof(path), "%s/%s.grid", dir != NULL ? dir : ".", gp->currentMapName);
    f = fopen(path, "w");
    if (f == NULL) {
        return;
    }
    for (x = -1400; x <= 1400; x += 20) {
        for (z = -800; z <= 800; z += 20) {
            float y = 1500.0f;
            int n;
            for (n = 0; n < 4; n++) {
                const char* name = "?";
                HitEntry* hit = NULL;
                const float h = c2_floor_under((float)x, y, (float)z, &name, &hit);
                if (h < -9000.0f) {
                    break;
                }
                fprintf(f, "G %d %d %.0f %s %x\n", x, z, h, name, hit != NULL ? (unsigned)hit->attributes : 0u);
                y = h - 5.0f;
            }
        }
    }
    /* walls along x, at Luigi's height over the floor below: W z y x name attr */
    for (z = -700; z <= 700; z += 20) {
        static const int kY[] = {15, 40, 65, 90, 115, 290, 315};
        unsigned k;
        for (k = 0; k < sizeof(kY) / sizeof(kY[0]); k++) {
            float rx = -1400.0f;
            int n;
            for (n = 0; n < 24; n++) {
                f32 hx, hy, hz, nx, ny, nz, dist = 2800.0f;
                HitEntry* e = hitCheckFilter(c2_grid_filter, &hx, &hy, &hz, &dist, &nx, &ny, &nz, rx, (float)kY[k], (float)z, 1.0f, 0.0f, 0.0f);
                if (e == NULL) {
                    break;
                }
                fprintf(f, "W %d %d %.0f %s %x\n", z, kY[k], hx, hitGetName(e), (unsigned)e->attributes);
                rx = hx + 1.0f;
            }
        }
    }
    for (i = 0; kNames[i] != NULL; i++) {
        MapObjectEntry* o = mobjNameToPtrNoAssert(kNames[i]);
        if (o != NULL) {
            const float h = c2_floor_under(o->position.x, o->position.y + 1.0f, o->position.z, NULL, NULL);
            fprintf(f, "M %s %.0f %.0f %.0f floor %.0f\n", kNames[i], o->position.x, o->position.y, o->position.z, h);
        }
    }
    for (i = 0; i < w->total; i++) {
        NpcEntry* n = &w->entries[i];
        if ((n->flags & NPC_FLAG_USED) && n->position.y > -500.0f) {
            const float h = c2_floor_under(n->position.x, n->position.y + 40.0f, n->position.z, NULL, NULL);
            fprintf(f, "N %s %.0f %.0f %.0f floor %.0f\n", n->description, n->position.x, n->position.y, n->position.z, h);
        }
    }
    fclose(f);
}

/* a tour of the rooms for building them ($TTYD_SL_C2_TOUR): each room's exits' places to the log, a capture, the next room */
static const char* const kTour[] = {
    "bom_20", "bom_21", "bom_22", "bom_23", "bom_24", "dou_20", "dou_21", "dou_22", "dou_23",
    "dou_24", "dou_25", "dou_26", "dou_27", "dou_28", "dou_29", "dou_30", "gon_20", NULL,
};
static USER_FUNC(c2_tour) {
    static int frames;
    const BeroEntry* b = (const BeroEntry*)evtGetValue(event, event->args[0]);
    const char* extra = (const char*)evtGetValue(event, event->args[1]);
    const char* t = getenv("TTYD_SL_C2_TOUR");
    MarioWork* m = marioGetPtr();
    int i;
    if (t == NULL) {
        return EVT_RETURN_DONE;
    }
    if (isFirstCall) {
        frames = 0;
    }
    if (frames == 2) {
        /* no battles on a tour: the room's foes go */
        NpcWork* w = npcGetWorkPtr();
        for (i = 0; i < w->total; i++) {
            NpcEntry* n = &w->entries[i];
            if ((n->flags & NPC_FLAG_USED) && strncmp(n->description, "c2f_", 4) == 0) {
                n->position.y = -3000.0f; /* (out of the way: deleting one mid-init crashes its AI) */
            }
        }
    }
    if (++frames == 100) {
        printf("[c2tour] %s luigi %.0f %.0f %.0f\n", gp->currentMapName, m->position.x, m->position.y, m->position.z);
        for (; b != NULL && b->hitName != NULL; b++) {
            Vec p;
            if (hitNameToPtr(b->hitName) != NULL) {
                hitObjGetPos(b->hitName, &p);
                printf("[c2tour] %s bero %s %.0f %.0f %.0f\n", gp->currentMapName, b->hitName, p.x, p.y, p.z);
            }
        }
        while (extra != NULL && *extra) {
            char name[48];
            const char* comma = strchr(extra, ',');
            size_t n = comma ? (size_t)(comma - extra) : strlen(extra);
            Vec p;
            snprintf(name, sizeof(name), "%.*s", (int)n, extra);
            if (hitNameToPtr(name) != NULL) {
                hitObjGetPos(name, &p);
                printf("[c2tour] %s hit %s %.0f %.0f %.0f\n", gp->currentMapName, name, p.x, p.y, p.z);
            }
            extra = comma ? comma + 1 : NULL;
        }
        fflush(stdout);
        portShotRequest(gp->currentMapName);
        if (getenv("TTYD_SL_C2_GRID") != NULL) {
            c2_survey();
        }
    }
    if (frames < 110) {
        return EVT_RETURN_BLOCK;
    }
    for (i = 0; kTour[i] != NULL; i++) {
        if (strcmp(kTour[i], gp->currentMapName) == 0) {
            if (kTour[i + 1] == NULL || (t[0] != '1' && strcmp(t, gp->currentMapName) == 0)) {
                printf("[c2tour] done\n");
                fflush(stdout);
                exit(0); /* (_exit isn't portable: the Mac's build stopped on it) */
            }
            seqSetSeq(SEQ_MAP_CHANGE, kTour[i + 1], NULL);
            break;
        }
    }
    return EVT_RETURN_DONE;
}

#define TOUR(bero, extra) \
    BEGIN_INLINE_EVENT \
        CALL(c2_tour, PTR(bero), STRING(extra)) \
    END_INLINE_EVENT

static EVT_BEGIN(c2_snow)
    CALL(evt_eff64, 0, STRING("snowfall_n64"), 0, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
    RETURN
EVT_END()

/* the grotto, cold: a blue-grey wash (ch2.md: about 20% toward #9EB8D0) */
static EVT_BEGIN(c2_cold)
    CALL(evt_sl_map_wash, 0, 0x9EB8D0, 20)
    CALL(evt_snd_set_rev_mode, 2)
    RETURN
EVT_END()

/* The other friend beside Luigi for a scene where both talk: LW(10) Blooey's
 * speaker, LW(11) Jerry's ("party" for the one out, else a stand-in NPC) */
static EVT_BEGIN(c2_cast_in)
    CALL(c2_out, LW(0))
    SET_VALUE(LW(10), PTR("party"))
    SET_VALUE(LW(11), PTR("party"))
    CALL(evt_mario_get_pos, 0, LW(1), LW(2), LW(3))
    IF_INT_NOT_EQUAL(LW(0), BLOOEY)
        CALL(evt_npc_entry, STRING("c2_bl"), STRING("c_lp_gesso"))
        CALL(evt_npc_set_tribe, STRING("c2_bl"), STRING(SJ2_VILLAGER))
        SUBTRACT(LW(1), 45)
        CALL(evt_npc_set_position, STRING("c2_bl"), LW(1), LW(2), LW(3))
        CALL(evt_npc_set_anim, STRING("c2_bl"), STRING("S_1"))
        ADD(LW(1), 45)
        SET_VALUE(LW(10), PTR("c2_bl"))
    END_IF
    IF_INT_NOT_EQUAL(LW(0), JERRY)
        IF_INT_EQUAL(SLF(F_JERRY), 1)
            CALL(evt_npc_entry, STRING("c2_je"), STRING("c_lp_bom"))
            CALL(evt_npc_set_tribe, STRING("c2_je"), STRING(SJ2_VILLAGER))
            SUBTRACT(LW(1), 70)
            ADD(LW(3), 15)
            CALL(evt_npc_set_position, STRING("c2_je"), LW(1), LW(2), LW(3))
            CALL(evt_npc_set_anim, STRING("c2_je"), STRING("S_1"))
            SET_VALUE(LW(11), PTR("c2_je"))
        END_IF
    END_IF
    RETURN
EVT_END()

static EVT_BEGIN(c2_cast_out)
    IF_STR_EQUAL(LW(10), STRING("c2_bl"))
        CALL(evt_npc_delete, STRING("c2_bl"))
    END_IF
    IF_STR_EQUAL(LW(11), STRING("c2_je"))
        CALL(evt_npc_delete, STRING("c2_je"))
    END_IF
    RETURN
EVT_END()

/* A get-something moment of the story's own (a key item, the compass piece): the fanfare and our line. LW(0): the tag */
static EVT_BEGIN(c2_got)
    CALL(evt_snd_bgmoff, PSND_FADE_DIP_TO(30))
    CALL(evt_snd_sfxon, STRING("SFX_EVT_STARSTONE_SHINE1"), 0)
    CALL(evt_mario_set_pose, STRING("M_I_2"))
    CALL(evt_msg_print, 0, LW(0), 0, 0)
    CALL(evt_mario_set_pose, STRING("M_S_1"))
    CALL(evt_snd_bgmon, PSND_FADE_RESUME | PSND_FADE_UNDIP, 0)
    RETURN
EVT_END()

/* an NPC of ours to talk to: LW(0) name, LW(1) model, LW(2..4) place, LW(5) facing, LW(6) its talk */
static EVT_BEGIN(c2_person)
    CALL(evt_npc_entry, LW(0), LW(1))
    CALL(evt_npc_set_tribe, LW(0), STRING(SJ2_VILLAGER))
    CALL(evt_npc_set_position, LW(0), LW(2), LW(3), LW(4))
    CALL(evt_npc_set_ry, LW(0), LW(5))
    CALL(evt_npc_set_anim, LW(0), STRING("S_1"))
    IF_INT_NOT_EQUAL(LW(6), 0)
        CALL(c2_set_talk, LW(0), LW(6))
    END_IF
    RETURN
EVT_END()

/* A field enemy: LW(0) name, LW(1) tribe, LW(2) AI, LW(3..5) place, LW(6) its battle number */
static EVT_BEGIN(c2_foe)
    CALL(evt_sl_enemy, LW(0), LW(1), LW(2), LW(3), LW(4), LW(5), 120, 80, LW(6))
    RETURN
EVT_END()

/* ======================================================================
 * The disguise rule (dou_20 to the mid-boss): Hissguards
 * ====================================================================== */

#define GUARDS 4
static struct {
    char name[16];
    int on;
    int flag;       /* GSWF index: beaten */
    float turnMs;   /* turns around every this (0: never) */
    float timer;
} sGuard[GUARDS];
static int sGuardBusy, sAlert;
static char sGuardMap[16];

/* in: slot, NPC name, its flag (SLF index), turn period (ms, 0 none) */
static USER_FUNC(c2_guard) {
    EvtWord* a = event->args;
    const int i = evtGetValue(event, a[0]);
    (void)isFirstCall;
    if (i >= 0 && i < GUARDS) {
        snprintf(sGuard[i].name, sizeof(sGuard[i].name), "%s", (const char*)evtGetValue(event, a[1]));
        sGuard[i].flag = evtGetValue(event, a[2]);
        sGuard[i].turnMs = (float)evtGetValue(event, a[3]);
        sGuard[i].timer = 0;
        sGuard[i].on = 1;
        snprintf(sGuardMap, sizeof(sGuardMap), "%s", gp->currentMapName);
    }
    return EVT_RETURN_DONE;
}

static USER_FUNC(c2_guard_done) {
    (void)event;
    (void)isFirstCall;
    sGuardBusy = 0;
    sAlert = 20 * 60; /* the others on alert for 20 s */
    return EVT_RETURN_DONE;
}

/* Luigi doing what no bride does: running, jumping, a hammer, a spin */
static int unbridely(void) {
    MarioWork* m = marioGetPtr();
    switch (m->currMotionId) {
        case MARIO_MOTION_DASH:
        case MARIO_MOTION_JUMP:
        case MARIO_MOTION_JUMP_2:
        case MARIO_MOTION_JUMP_3:
        case MARIO_MOTION_JUMP_SMALL:
        case MARIO_MOTION_HIP:
        case MARIO_MOTION_HIP_2:
        case MARIO_MOTION_HAMMER:
        case MARIO_MOTION_HAMMER_2:
            return 1;
        default:
            return 0;
    }
}

extern EvtWord c2_guard_blown[];
static void guardFrame(void) {
    MarioWork* m;
    int i;
    if (gp == NULL || portSuperLuigiChapter() != 2) {
        return;
    }
    if (strcmp(sGuardMap, gp->currentMapName) != 0) {
        memset(sGuard, 0, sizeof(sGuard));
        sGuardMap[0] = 0;
        return;
    }
    if (sAlert > 0) {
        sAlert--;
    }
    if (sGuardBusy || gp->inBattle || !marioChkKey() || swGet(SL_FLAG(2, F_BULKHEAD))) {
        return;
    }
    m = marioGetPtr();
    for (i = 0; i < GUARDS; i++) {
        NpcEntry* g;
        float dx, dz, d, facing, ang;
        if (!sGuard[i].on || swGet(SL_FLAG(2, 0) + sGuard[i].flag)) {
            continue;
        }
        g = npcNameToPtr_NoAssert(sGuard[i].name);
        if (g == NULL) {
            continue;
        }
        if (sGuard[i].turnMs > 0) {
            sGuard[i].timer += 1000.0f / 60.0f;
            if (sGuard[i].timer >= sGuard[i].turnMs) {
                sGuard[i].timer = 0;
                g->direction = g->direction < 180.0f ? 270.0f : 90.0f;
            }
        }
        dx = m->position.x - g->position.x;
        dz = m->position.z - g->position.z;
        d = hypotf(dx, dz);
        if (d > 150.0f || fabsf(m->position.y - g->position.y) > 60.0f) {
            continue; /* (not a ledge above or below him: dou_21's guard saw Luigi on the shelf over his head) */
        }
        /* his facing: 90 right (+x), 270 left */
        facing = g->direction < 180.0f ? 1.0f : -1.0f;
        if (dx * facing <= 0.0f) {
            continue;
        }
        ang = atanf(fabsf(dz) / fabsf(dx)) * 57.29578f;
        if (ang > 30.0f) {
            continue;
        }
        if (unbridely() || (sAlert > 0 && m->currMotionId == MARIO_MOTION_DASH)) {
            EventEntry* e;
            sGuardBusy = 1;
            e = evtEntry(c2_guard_blown, 0, 0);
            if (e != NULL) {
                e->lwData[0] = (EvtWord)(AddrWord)sGuard[i].name;
                e->lwData[1] = SL_FLAG(2, 0) + sGuard[i].flag;
            }
            printf("[superluigi] chapter 2: %s saw the bride run\n", sGuard[i].name);
            fflush(stdout);
            return;
        }
    }
}

/* "OI! BRIDES DON'T DO THAT!": he charges, the battle starts with his first strike */
EVT_BEGIN(c2_guard_blown)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_sub_random, 2, LW(2))
    SWITCH(LW(2))
        CASE_EQUAL(0)
        CALL(evt_msg_print, 0, STRING("sl_c2_guard_04"), 0, LW(0))
        CASE_EQUAL(1)
        CALL(evt_msg_print, 0, STRING("sl_c2_guard_05"), 0, LW(0))
        CASE_DEFAULT
        CALL(evt_msg_print, 0, STRING("sl_c2_guard_06"), 0, LW(0))
    END_SWITCH
    CALL(evt_mario_get_pos, 0, LW(3), LW(4), LW(5))
    CALL(evt_npc_move_position, LW(0), LW(3), LW(5), 0, FLOAT(200.0f), 4)
    CALL(evt_mario_key_onoff, 1)
    CALL(evt_npc_set_attack_mode, LW(0), 16)
    CALL(c2_enemy_first)
    CALL(evt_npc_battle_start, LW(0))
    CALL(evt_npc_wait_battle_end)
    CALL(evt_npc_get_battle_result, LW(2))
    IF_INT_EQUAL(LW(2), 1)
        CALL(c2_flag_on, LW(1))
        /* beaten, he goes as a beaten foe does */
        CALL(evt_npc_delete, LW(0))
    END_IF
    CALL(c2_guard_done)
    RETURN
EVT_END()

/* ======================================================================
 * bom_20, Strudel Pass (bom_00): the well
 * ====================================================================== */

#define B20_WELL_X -175
#define B20_WELL_Z 0

static BeroEntry bom_20_bero[] = {
    {"dokan", 2, 0, BERO_DIR_DOWN, BERO_POS_AUTO, 0, 0, -1, NULL, 6, NULL, NULL, NULL, 1, 1, 0, 0},
    {"e_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "bom_21", "w_bero", 1, 1, 0, 0},
    {0},
};

static EVT_BEGIN(bom_20_arrive)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_seq_wait, 2)
    CALL(evt_msg_print, 0, STRING("sl_c2_arrive_00"), 0, 0)
    /* the pipe's own exit (the entrance) finished first: a jump begun while it still
     * rises left Luigi under the snow's floor, and he fell through it at his next jump */
    LOOP(0)
        CALL(c2_mario_motion, LW(0))
        CALL(evt_mario_get_pos, 0, LW(1), LW(2), LW(3))
        IF_INT_EQUAL(LW(0), 0)
            IF_INT_GREATER(LW(2), 25)
                BREAK_LOOP
            END_IF
        END_IF
        WAIT_FRAMES(1)
    END_LOOP
    CALL(evt_snd_sfxon, STRING("SFX_DOKAN_GOOUT1"), 0)
    CALL(evt_cam_shake, 4, FLOAT(0.02f), FLOAT(0.0f), 400)
    WAIT_MS(400)
    /* off the well, and Blooey pops out after him */
    CALL(evt_mario_jump_pos, B20_WELL_X + 60, 10, B20_WELL_Z + 30, 400, 0, 30)
    CALL(evt_mario_set_dir, 270, 0, 0)
    CALL(c2_out, LW(0))
    IF_INT_EQUAL(LW(0), 0)
        CALL(evt_snd_sfxon, STRING("SFX_DOKAN_GOOUT1"), 0)
        CALL(evt_mario_set_party_pos, 0, BLOOEY, B20_WELL_X, 40, B20_WELL_Z)
        CALL(evt_party_jump_pos, 0, B20_WELL_X + 25, 0, B20_WELL_Z + 40, 300, 0, -1000)
    END_IF
    WAIT_MS(300)
    CALL_CHILD(c2_cast_in)
    CALL(evt_msg_print, 0, STRING("sl_c2_arrive_01"), 0, STRING("mario"))
    CALL(evt_msg_print, 0, STRING("sl_c2_arrive_02"), 0, STRING("mario"))
    CALL(evt_msg_print, 0, STRING("sl_c2_arrive_03"), 0, LW(10))
    CALL(evt_msg_print, 0, STRING("sl_c2_arrive_04"), 0, STRING("mario"))
    CALL_CHILD(c2_cast_out)
    SET_VALUE(SLF(F_ARRIVED), 1)
    CALL(evt_party_run, 0)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* The Bridal Pearls: Blooey dives into the well alone and comes up with them (his Swim at the well) */
static EVT_BEGIN(bom_20_pearls)
    /* the friend handed back first: inside the ability (Swim may start a dive as its own
     * type-26 script) the partner moves never finish; a second hand-back is harmless */
    CALL(evt_sl_ability_done)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_party_stop, 0)
    CALL(evt_party_move_pos, 0, B20_WELL_X, B20_WELL_Z, FLOAT(80.0f))
    CALL(evt_party_set_pose, 0, STRING("F_2"))
    CALL(evt_snd_sfxon, STRING("SFX_MARIO_FALL_WATER1"), 0)
    WAIT_MS(900)
    CALL(evt_party_dispflg_onoff, 0, PARTY_DISP_FLAG_HIDDEN, 1)
    WAIT_MS(1600)
    CALL(evt_snd_sfxon, STRING("SFX_MARIO_FALL_WATER1"), 0)
    CALL(evt_party_dispflg_onoff, 0, PARTY_DISP_FLAG_HIDDEN, 0)
    CALL(evt_party_set_pose, 0, STRING("F_3"))
    WAIT_MS(500)
    CALL(evt_party_set_pose, 0, STRING("S_1"))
    CALL(evt_msg_print, 0, STRING("sl_c2_pearls_00"), 0, STRING("party"))
    SET_VALUE(LW(0), PTR("sl_c2_pearls_01"))
    CALL_CHILD(c2_got)
    SET_VALUE(SLF(F_PEARLS), 1)
    CALL(evt_party_run, 0)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* Before dawn: the note on the well, and into it */
static EVT_BEGIN(bom_20_dawn)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_seq_wait, 2)
    CALL(evt_snd_bgmoff, PSND_FADE_OUT)
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG7_BOM1"))
    CALL(evt_mario_mov_pos2, B20_WELL_X + 30, B20_WELL_Z + 30, FLOAT(40.0f))
    CALL(evt_mario_set_dir, 270, 0, 0)
    CALL(evt_msg_print, 0, STRING("sl_c2_note_00"), 0, 0)
    CALL_CHILD(c2_cast_in)
    CALL(evt_msg_print, 0, STRING("sl_c2_dawn_00"), 0, LW(11))
    CALL(evt_msg_print, 0, STRING("sl_c2_dawn_01"), 0, STRING("mario"))
    CALL(evt_msg_print, 0, STRING("sl_c2_dawn_02"), 0, LW(11))
    CALL_CHILD(c2_cast_out)
    CALL(evt_snd_sfxon, STRING("SFX_EVT_STARSTONE_SHINE1"), 0)
    CALL(evt_msg_print, 0, STRING("sl_c2_dawn_03"), 0, 0)
    CALL(evt_mario_jump_pos, B20_WELL_X, 20, B20_WELL_Z, 500, 0, 40)
    CALL(evt_snd_sfxon, STRING("SFX_MARIO_DOKAN1"), 0)
    CALL(evt_mario_dispflag_onoff, 1, 1)
    CALL(evt_party_dispflg_onoff, 0, PARTY_DISP_FLAG_HIDDEN, 1)
    WAIT_MS(800)
    /* the tag: Jerry's version */
    CALL(evt_map_set_blend, 0, 40, 40, 60, 255)
    CALL(evt_fade_entry, FADE_OUT_BLACK, 600, 0, 0, 0)
    CALL(evt_fade_end_wait)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_RUIJI1"))
    CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
    CALL(evt_npc_entry, STRING("c2_tag"), STRING("c_lp_bom"))
    CALL(evt_npc_set_position, STRING("c2_tag"), B20_WELL_X, 5, B20_WELL_Z + 60)
    CALL(evt_npc_set_anim, STRING("c2_tag"), STRING("L_1"))
    /* (from higher up: at eye height 40 the snow's near bank hid the lower half of him) */
    CALL(evt_cam3d_evt_set, B20_WELL_X, 90, B20_WELL_Z + 320, B20_WELL_X, 25, B20_WELL_Z + 60, 0, 11)
    CALL(evt_fade_entry, FADE_IN_SPOT, 800, 0, 0, 0)
    CALL(evt_fade_end_wait)
    CALL(evt_msg_print, 0, STRING("sl_c2_tag_00"), 0, STRING("c2_tag"))
    CALL(evt_msg_print, 0, STRING("sl_c2_tag_01"), 0, STRING("c2_tag"))
    CALL(evt_msg_print, 0, STRING("sl_c2_tag_02"), 0, STRING("c2_tag"))
    CALL(evt_msg_print, 0, STRING("sl_c2_tag_03"), 0, STRING("c2_tag"))
    CALL(evt_msg_print, 0, STRING("sl_c2_tag_04"), 0, STRING("c2_tag"))
    CALL(evt_msg_print, 0, STRING("sl_c2_tag_05"), 0, STRING("c2_tag"))
    CALL(evt_msg_print, 0, STRING("sl_c2_tag_06"), 0, STRING("c2_tag"))
    CALL(evt_fade_entry, FADE_OUT_BLACK, 600, 0, 0, 0)
    CALL(evt_fade_end_wait)
    CALL(evt_mario_dispflag_onoff, 0, 1)
    CALL(evt_npc_delete, STRING("c2_tag"))
    CALL(evt_cam3d_evt_off, 0, 11)
    CALL(evt_fade_entry, FADE_IN_BLACK, 300, 0, 0, 0)
    CALL(evt_sl_chapter_end)
    RETURN
EVT_END()

static EVT_BEGIN(bom_20_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG7_BOM1"))
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG7_BOM1"))
    SET_VALUE(LW(0), PTR(bom_20_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL_CHILD(c2_snow)
    CALL(evt_mobj_save_blk, STRING("mobj_save"), 75, 60, -60, 0, 0)
    CALL(evt_mobj_badgeblk, STRING("mobj_coin"), 230, 60, 60, ITEM_COIN, 0, SLF(F_P20_COIN), 0)
    CALL(evt_mobj_brick, STRING("mobj_hidden"), -160, 60, 60, ITEM_STAR_PIECE, 11, 0, SLF(F_P20_PIECE))
    CALL(evt_mobj_kururing_floor, STRING("kururin1"), -300, 5, 0, STRING("S_gra02_dai"), 125, SLF(F_P22_PANEL))
    /* the Bridal Pearls: Blooey's Swim at the well, once the tailor wants them */
    IF_INT_EQUAL(SLF(F_TAILOR), 1)
        IF_INT_EQUAL(SLF(F_PEARLS), 0)
            CALL(evt_sl_point, PORT_SL_ABILITY_SWIM, 0, B20_WELL_X, 0, B20_WELL_Z, 70, 0, PTR(bom_20_pearls))
        END_IF
    END_IF
    IF_INT_EQUAL(SLF(F_ARRIVED), 0)
        SPAWN_EVENT(bom_20_arrive)
    END_IF
    IF_INT_EQUAL(SLF(F_VOICE), 1)
        SPAWN_EVENT(bom_20_dawn)
    END_IF
    TOUR(bom_20_bero, "dokan")
    RETURN
EVT_END()

/* ======================================================================
 * bom_21, Frostcrust Path (bom_03): the hollow tree
 * ====================================================================== */

#define B21_TREE_X 222
#define B21_TREE_Z -120

static BeroEntry bom_21_bero[] = {
    {"w_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "bom_20", "e_bero", 1, 1, 0, 0},
    {"e_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "bom_22", "w_bero4", 1, 1, 0, 0},
    {0},
};

/* the Snowmoth Silk, on its stump in the hollow: picked up as Luigi gets to it */
static EVT_BEGIN(bom_21_silk_watch)
    LOOP(0)
        CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
        SUBTRACT(LW(0), B21_TREE_X)
        SUBTRACT(LW(2), B21_TREE_Z)
        IF_INT_GREATER(LW(0), -30)
            IF_INT_LESS(LW(0), 30)
                IF_INT_GREATER(LW(2), -40)
                    IF_INT_LESS(LW(2), 40)
                        BREAK_LOOP
                    END_IF
                END_IF
            END_IF
        END_IF
        WAIT_FRAMES(2)
    END_LOOP
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_item_delete, STRING("c2_silk"))
    SET_VALUE(LW(0), PTR("sl_c2_silk_01"))
    CALL_CHILD(c2_got)
    CALL(evt_msg_print, 0, STRING("sl_c2_silk_02"), 0, STRING("mario"))
    SET_VALUE(SLF(F_SILK), 1)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(bom_21_silk_place)
    CALL(evt_item_entry, STRING("c2_silk"), ITEM_BLANKET, B21_TREE_X, 12, B21_TREE_Z, ITEM_ENTRY_FIXED_NO_TOUCH, -1, 0)
    SPAWN_EVENT(bom_21_silk_watch)
    RETURN
EVT_END()

/* Jerry's Blast on the packed ice over the hollow */
static EVT_BEGIN(bom_21_tree_blast)
    CALL(evt_npc_set_anim, STRING("c2_ice"), STRING("B_1"))
    CALL(evt_snd_sfxon, STRING("SFX_OFF_WALL_BREAK1"), 0)
    WAIT_MS(700)
    CALL(evt_npc_delete, STRING("c2_ice"))
    SET_VALUE(SLF(F_TREE), 1)
    CALL(evt_mobj_powerupblk, STRING("pblk_c2"), B21_TREE_X + 40, 60, B21_TREE_Z, 0, SLF(F_P21_SHINE), 0)
    IF_INT_EQUAL(SLF(F_TAILOR), 1)
        IF_INT_EQUAL(SLF(F_SILK), 0)
            CALL_CHILD(bom_21_silk_place)
        END_IF
    END_IF
    RETURN
EVT_END()

static EVT_BEGIN(bom_21_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG7_BOM1"))
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG7_BOM1"))
    CALL(evt_sl_enemy, STRING("c2f_goomba_1"), STRING(SJ2_GOOMBA), STRING(SJ2_GOOMBA), -200, 0, 40, 120, 60, STRING("BTLNO_BOM_03_01"))
    CALL(evt_sl_enemy, STRING("c2f_goomba_2"), STRING(SJ2_GOOMBA), STRING(SJ2_GOOMBA), 80, 0, 60, 120, 60, STRING("BTLNO_BOM_03_01"))
    CALL(evt_sl_enemy, STRING("c2f_puff_1"), STRING(SJ2_ICE_PUFF), STRING(SJ2_AI_PUFF), 250, 30, 30, 100, 60, STRING("BTLNO_BOM_03_02"))
    SET_VALUE(LW(0), PTR(bom_21_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL_CHILD(c2_snow)
    CALL(evt_mobj_badgeblk, STRING("mobj_item"), -300, 60, 60, ITEM_MUSHROOM, 0, SLF(F_P21_ITEM), 0)
    CALL(evt_mobj_badgeblk, STRING("mobj_coin1"), -50, 60, 60, ITEM_COIN, 0, SLF(F_P21_COIN1), 0)
    CALL(evt_mobj_badgeblk, STRING("mobj_coin2"), 0, 60, 60, ITEM_COIN, 0, SLF(F_P21_COIN2), 0)
    CALL(evt_mobj_brick, STRING("mobj_hidden"), 150, 60, 100, ITEM_ICE_SMASH, 12, 0, SLF(F_P21_BADGE))
    CALL(evt_item_entry, STRING("item_piece"), ITEM_STAR_PIECE, 450, 5, 120, ITEM_ENTRY_FIXED, SLF(F_P21_PIECE), 0)
    IF_INT_EQUAL(SLF(F_TREE), 0)
        CALL(evt_npc_entry, STRING("c2_ice"), STRING("c_sl_ice_wall"))
        CALL(evt_npc_set_position, STRING("c2_ice"), B21_TREE_X, 0, B21_TREE_Z + 20)
        CALL(evt_npc_set_anim, STRING("c2_ice"), STRING("S_1"))
        CALL(evt_sl_point, PORT_SL_ABILITY_BLAST, 0, B21_TREE_X, 0, B21_TREE_Z + 20, 50, 0, PTR(bom_21_tree_blast))
    ELSE
        CALL(evt_mobj_powerupblk, STRING("pblk_c2"), B21_TREE_X + 40, 60, B21_TREE_Z, 0, SLF(F_P21_SHINE), 0)
        IF_INT_EQUAL(SLF(F_TAILOR), 1)
            IF_INT_EQUAL(SLF(F_SILK), 0)
                CALL_CHILD(bom_21_silk_place)
            END_IF
        END_IF
    END_IF
    TOUR(bom_21_bero, "A_ki1,A_ki2,A_ki3")
    RETURN
EVT_END()

/* ======================================================================
 * bom_22, Plumpbelly Gate (bom_04)
 * ====================================================================== */

static BeroEntry bom_22_bero[] = {
    {"w_bero4", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "bom_21", "e_bero", 1, 1, 0, 0},
    {"e_bero4", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "bom_23", "w_bero", 1, 1, 0, 0},
    {0},
};

static EVT_BEGIN(bom_22_gate_talk)
    CALL(evt_mario_key_onoff, 0)
    IF_INT_EQUAL(SLF(F_HIZZA), 1)
        CALL(evt_msg_print, 0, STRING("sl_c2_npc_gate_10"), 0, STRING("c2_gateguard"))
    ELSE
        IF_INT_EQUAL(SLF(F_GATE), 0)
            CALL_CHILD(c2_cast_in)
            CALL(evt_msg_print, 0, STRING("sl_c2_gate_00"), 0, STRING("c2_gateguard"))
            CALL(evt_msg_print, 0, STRING("sl_c2_gate_01"), 0, STRING("mario"))
            CALL(evt_msg_print, 0, STRING("sl_c2_gate_02"), 0, STRING("c2_gateguard"))
            CALL(evt_msg_print, 0, STRING("sl_c2_gate_03"), 0, LW(10))
            CALL(evt_msg_print, 0, STRING("sl_c2_gate_04"), 0, STRING("c2_gateguard"))
            CALL_CHILD(c2_cast_out)
            SET_VALUE(SLF(F_GATE), 1)
        ELSE
            CALL(evt_msg_print, 0, STRING("sl_c2_npc_gate_00"), 0, STRING("c2_gateguard"))
        END_IF
    END_IF
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* the gate soldier stops Luigi the first time he comes up to the gate */
static EVT_BEGIN(bom_22_gate_watch)
    LOOP(0)
        CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
        IF_INT_GREATER(LW(0), 300)
            BREAK_LOOP
        END_IF
        WAIT_FRAMES(2)
    END_LOOP
    CALL(evt_mario_set_dir_npc, STRING("c2_gateguard"))
    CALL_CHILD(bom_22_gate_talk)
    RETURN
EVT_END()

static EVT_BEGIN(bom_22_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG7_BOM1"))
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG7_BOM1"))
    CALL(evt_sl_enemy, STRING("c2f_piranha_1"), STRING(SJ2_FROST_PIRANHA), STRING(SJ2_AI_PIRANHA), -40, 0, 50, 0, 0, STRING("BTLNO_BOM_04_01"))
    CALL(evt_sl_enemy, STRING("c2f_puff22_1"), STRING(SJ2_ICE_PUFF), STRING(SJ2_AI_PUFF), -250, 30, 20, 100, 60, STRING("BTLNO_BOM_04_02"))
    CALL(evt_sl_enemy, STRING("c2f_puff22_2"), STRING(SJ2_ICE_PUFF), STRING(SJ2_AI_PUFF), 180, 30, 40, 100, 60, STRING("BTLNO_BOM_04_02"))
    SET_VALUE(LW(0), PTR(bom_22_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL_CHILD(c2_snow)
    /* a Fire Flower right before the Frost Piranha, a Heart Block just past it */
    CALL(evt_mobj_badgeblk, STRING("mobj_item"), -140, 60, 60, ITEM_FIRE_FLOWER, 0, SLF(F_P22_ITEM), 0)
    CALL(evt_mobj_recovery_blk, STRING("mobj_heart"), 0, 60, 60, 60, 0, 0)
    /* HP Plus, in a hidden block by the fence (at x 380 it was within the gate soldier's reach: A under it, facing
     * the gate, talked to him instead of jumping) */
    CALL(evt_mobj_brick, STRING("mobj_hpplus"), 320, 60, 60, ITEM_HP_PLUS, 12, 0, SLF(F_P22_HPPLUS))
    /* the gate soldier */
    SET_VALUE(LW(0), PTR("c2_gateguard"))
    SET_VALUE(LW(1), PTR("c_bomhey"))
    SET_VALUE(LW(2), 430)
    SET_VALUE(LW(3), 0)
    SET_VALUE(LW(4), 40)
    SET_VALUE(LW(5), 270)
    SET_VALUE(LW(6), PTR(bom_22_gate_talk))
    CALL_CHILD(c2_person)
    CALL(evt_npc_set_anim, STRING("c2_gateguard"), STRING("BOM_S_1"))
    IF_INT_EQUAL(SLF(F_GATE), 0)
        SPAWN_EVENT(bom_22_gate_watch)
    END_IF
    TOUR(bom_22_bero, "")
    RETURN
EVT_END()

/* ======================================================================
 * bom_23, Plumpbelly Square (bom_01) and the mayor's house
 * ====================================================================== */

#define B23_DOOR_X 0
#define B23_DOOR_Z -304

static BeroEntry bom_23_bero[] = {
    {"w_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "bom_22", "e_bero4", 1, 1, 0, 0},
    {"e_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "bom_24", "w_bero", 1, 1, 0, 0},
    {0},
};

/* The mayor's plea and the bride plan */
static EVT_BEGIN(bom_23_mayor_plea)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_STG8_VOICE1"))
    CALL_CHILD(c2_cast_in)
    CALL(evt_msg_print, 0, STRING("sl_c2_mayor_00"), 0, STRING("c2_mayor"))
    CALL(evt_msg_print, 0, STRING("sl_c2_mayor_01"), 0, STRING("mario"))
    CALL(evt_msg_print, 0, STRING("sl_c2_mayor_02"), 0, STRING("c2_mayor"))
    CALL(evt_msg_print, 0, STRING("sl_c2_mayor_03"), 0, STRING("c2_mayor"))
    CALL(evt_msg_print, 0, STRING("sl_c2_mayor_04"), 0, STRING("c2_mayor"))
    CALL(evt_npc_set_anim, STRING("c2_fuselle"), STRING("T_2"))
    CALL(evt_msg_print, 0, STRING("sl_c2_mayor_05"), 0, STRING("c2_fuselle"))
    CALL(evt_npc_set_anim, STRING("c2_fuselle"), STRING("S_1"))
    CALL(evt_msg_print, 0, STRING("sl_c2_mayor_06"), 0, STRING("c2_mayor"))
    CALL(evt_msg_print, 0, STRING("sl_c2_mayor_07"), 0, STRING("mario"))
    CALL(evt_msg_print, 0, STRING("sl_c2_mayor_08"), 0, STRING("c2_mayor"))
    /* Luigi looks at Fuselle, then at Blooey, then at the ceiling */
    CALL(evt_mario_set_dir_npc, STRING("c2_fuselle"))
    WAIT_MS(500)
    CALL(evt_mario_set_dir, 270, 200, 0)
    WAIT_MS(500)
    CALL(evt_mario_set_pose, STRING("M_I_Y"))
    WAIT_MS(700)
    CALL(evt_mario_set_pose, STRING("M_S_1"))
    CALL(evt_msg_print, 0, STRING("sl_c2_mayor_09"), 0, STRING("mario"))
    CALL(evt_msg_print, 0, STRING("sl_c2_mayor_10"), 0, STRING("c2_mayor"))
    CALL(evt_msg_print, 0, STRING("sl_c2_mayor_11"), 0, STRING("mario"))
    CALL(evt_msg_print, 0, STRING("sl_c2_mayor_12"), 0, STRING("c2_fuselle"))
    CALL(evt_msg_print, 0, STRING("sl_c2_mayor_13"), 0, STRING("mario"))
    CALL(evt_msg_print, 0, STRING("sl_c2_mayor_14"), 0, STRING("mario"))
    CALL(evt_msg_print, 0, STRING("sl_c2_mayor_15"), 0, LW(10))
    CALL(evt_msg_print, 0, STRING("sl_c2_mayor_16"), 0, STRING("c2_mayor"))
    CALL(evt_msg_print, 0, STRING("sl_c2_mayor_17"), 0, STRING("mario"))
    CALL(evt_msg_print, 0, STRING("sl_c2_mayor_18"), 0, STRING("c2_mayor"))
    CALL(evt_msg_print, 0, STRING("sl_c2_mayor_19"), 0, STRING("mario"))
    CALL_CHILD(c2_cast_out)
    /* happy tears; the music lifts */
    CALL(evt_npc_set_anim, STRING("c2_fuselle"), STRING("C_1"))
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_HAPPY1"))
    WAIT_MS(1500)
    CALL(evt_npc_set_anim, STRING("c2_fuselle"), STRING("S_1"))
    SET_VALUE(SLF(F_MAYOR), 1)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG7_BOM1"))
    RETURN
EVT_END()

/* the feast's proposal */
static EVT_BEGIN(bom_23_proposal)
    /* to the mayor's side, where the camera sees the three of them (from below he stood behind Luigi) */
    CALL(evt_mario_mov_pos2, -140, -100, FLOAT(70.0f))
    CALL(evt_mario_set_dir, 90, 0, 0)
    CALL(evt_npc_set_ry, STRING("c2_mayor"), 270)
    CALL(evt_msg_print, 0, STRING("sl_c2_proposal_00"), 0, STRING("c2_mayor"))
    CALL(evt_msg_print, 0, STRING("sl_c2_proposal_01"), 0, STRING("c2_mayor"))
    CALL(evt_npc_set_anim, STRING("c2_fuselle"), STRING("T_1"))
    CALL(evt_msg_print, 0, STRING("sl_c2_proposal_02"), 0, STRING("c2_fuselle"))
    CALL(evt_npc_set_anim, STRING("c2_fuselle"), STRING("S_1"))
    CALL(evt_msg_print, 0, STRING("sl_c2_proposal_03"), 0, STRING("mario"))
    CALL(evt_msg_select, 0, STRING("sl_c2_proposal_04"))
    /* (the question's window is still open: Luigi's answer closes it; print alone flashed past. c2_cast_in takes LW(0)) */
    SET_VALUE(LW(5), LW(0))
    IF_INT_EQUAL(LW(5), 0)
        CALL(evt_msg_print_add, 0, STRING("sl_c2_proposal_08"))
    ELSE
        CALL(evt_msg_print_add, 0, STRING("sl_c2_proposal_09"))
    END_IF
    CALL_CHILD(c2_cast_in)
    IF_INT_EQUAL(LW(5), 0)
        CALL(evt_msg_print, 0, STRING("sl_c2_proposal_05"), 0, STRING("c2_mayor"))
        CALL(evt_msg_print, 0, STRING("sl_c2_proposal_06"), 0, LW(11))
    ELSE
        CALL(evt_msg_print, 0, STRING("sl_c2_proposal_07"), 0, STRING("c2_mayor"))
    END_IF
    CALL_CHILD(c2_cast_out)
    CALL(c2_anim, STRING("c2_banner"), STRING("C_3"))
    SET_VALUE(SLF(F_PROPOSAL), 1)
    RETURN
EVT_END()

static EVT_BEGIN(bom_23_mayor_talk)
    CALL(evt_mario_key_onoff, 0)
    IF_INT_EQUAL(SLF(F_HIZZA), 1)
        IF_INT_EQUAL(SLF(F_PROPOSAL), 0)
            CALL_CHILD(bom_23_proposal)
            /* Luigi stays held: the voice comes next (bom_23_voice_watch), and A in between talked to the mayor again */
            RETURN
        ELSE
            CALL(evt_msg_print, 0, STRING("sl_c2_npc_mayor_20"), 0, STRING("c2_mayor"))
        END_IF
    ELSE
        IF_INT_EQUAL(SLF(F_MAYOR), 0)
            CALL_CHILD(bom_23_mayor_plea)
        ELSE
            IF_INT_EQUAL(SLF(F_GOWN), 0)
                CALL(evt_msg_print, 0, STRING("sl_c2_npc_mayor_10"), 0, STRING("c2_mayor"))
            ELSE
                CALL(evt_msg_print, 0, STRING("sl_c2_npc_mayor_11"), 0, STRING("c2_mayor"))
            END_IF
        END_IF
    END_IF
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(bom_23_fuselle_talk)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_npc_set_anim, STRING("c2_fuselle"), STRING("T_1"))
    IF_INT_EQUAL(SLF(F_HIZZA), 1)
        CALL(evt_msg_print, 0, STRING("sl_c2_npc_fuselle_20"), 0, STRING("c2_fuselle"))
    ELSE
        IF_INT_EQUAL(SLF(F_MAYOR), 0)
            CALL(evt_msg_print, 0, STRING("sl_c2_npc_fuselle_00"), 0, STRING("c2_fuselle"))
        ELSE
            CALL(evt_msg_print, 0, STRING("sl_c2_npc_fuselle_10"), 0, STRING("c2_fuselle"))
        END_IF
    END_IF
    CALL(evt_npc_set_anim, STRING("c2_fuselle"), STRING("S_1"))
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* Jerry, on the way out of the mayor's house */
static EVT_BEGIN(bom_23_jerry)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_mario_set_dir_npc, STRING("c2_jerry"))
    CALL(evt_npc_set_anim, STRING("c2_jerry"), STRING("L_1"))
    CALL(evt_msg_print, 0, STRING("sl_c2_jerry_00"), 0, STRING("c2_jerry"))
    CALL(evt_npc_set_anim, STRING("c2_jerry"), STRING("S_1"))
    CALL(evt_msg_print, 0, STRING("sl_c2_jerry_01"), 0, STRING("mario"))
    CALL(evt_msg_print, 0, STRING("sl_c2_jerry_02"), 0, STRING("c2_jerry"))
    CALL(evt_msg_print, 0, STRING("sl_c2_jerry_03"), 0, STRING("mario"))
    CALL(evt_msg_print, 0, STRING("sl_c2_jerry_04"), 0, STRING("c2_jerry"))
    CALL(evt_msg_print, 0, STRING("sl_c2_jerry_05"), 0, STRING("c2_jerry"))
    CALL(evt_msg_print, 0, STRING("sl_c2_jerry_06"), 0, STRING("party"))
    CALL(evt_msg_print, 0, STRING("sl_c2_jerry_07"), 0, STRING("c2_jerry"))
    CALL(evt_msg_print, 0, STRING("sl_c2_jerry_08"), 0, STRING("mario"))
    CALL(evt_npc_set_anim, STRING("c2_jerry"), STRING("L_1"))
    WAIT_MS(900)
    CALL(evt_msg_print, 0, STRING("sl_c2_jerry_09"), 0, STRING("c2_jerry"))
    CALL(evt_npc_set_anim, STRING("c2_jerry"), STRING("S_1"))
    CALL(evt_msg_print, 0, STRING("sl_c2_jerry_10"), 0, STRING("c2_jerry"))
    /* Jerry joins */
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_FF_GET_PARTY1"))
    CALL(evt_msg_print, 0, STRING("sl_c2_jerry_11"), 0, 0)
    CALL(evt_npc_get_position, STRING("c2_jerry"), LW(0), LW(1), LW(2))
    CALL(evt_npc_delete, STRING("c2_jerry"))
    CALL(evt_sl_friend_join, PORT_SL_FRIEND_JERRY)
    /* Jerry comes out in Blooey's place, as a new partner does */
    CALL(evt_mario_goodbye_party, 0)
    CALL(evt_mario_set_party_pos, 0, JERRY, LW(0), LW(1), LW(2))
    SET_VALUE(SLF(F_JERRY), 1)
    WAIT_MS(1500)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG7_BOM1"))
    CALL(evt_msg_print, 0, STRING("sl_c2_jerry_12"), 0, STRING("party"))
    CALL(evt_party_run, 0)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(bom_23_jerry_watch)
    LOOP(0)
        IF_INT_EQUAL(SLF(F_MAYOR), 1)
            CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
            IF_INT_GREATER(LW(2), -60)
                CALL(c2_can_act, LW(3))
                IF_INT_EQUAL(LW(3), 1)
                    BREAK_LOOP
                END_IF
            END_IF
        END_IF
        WAIT_FRAMES(4)
    END_LOOP
    CALL_CHILD(bom_23_jerry)
    RETURN
EVT_END()

#define VILLAGER_TALK(who, tag0, tag1, tag2) \
    static EVT_BEGIN(bom_23_talk_##who) \
        CALL(evt_mario_key_onoff, 0) \
        IF_INT_EQUAL(SLF(F_HIZZA), 1) \
            CALL(evt_msg_print, 0, STRING(tag2), 0, STRING("c2_" #who)) \
        ELSE \
            IF_INT_EQUAL(SLF(F_MAYOR), 1) \
                CALL(evt_msg_print, 0, STRING(tag1), 0, STRING("c2_" #who)) \
            ELSE \
                CALL(evt_msg_print, 0, STRING(tag0), 0, STRING("c2_" #who)) \
            END_IF \
        END_IF \
        CALL(evt_mario_key_onoff, 1) \
        RETURN \
    EVT_END()

VILLAGER_TALK(elder, "sl_c2_npc_elder_00", "sl_c2_npc_elder_10", "sl_c2_npc_elder_20")
VILLAGER_TALK(child, "sl_c2_npc_child_00", "sl_c2_npc_child_10", "sl_c2_npc_child_20")
VILLAGER_TALK(cook, "sl_c2_npc_cook_00", "sl_c2_npc_cook_10", "sl_c2_npc_cook_20")
VILLAGER_TALK(vil4, "sl_c2_npc_vil4_00", "sl_c2_npc_vil4_10", "sl_c2_npc_vil4_20")
VILLAGER_TALK(vil5, "sl_c2_npc_vil5_00", "sl_c2_npc_vil5_10", "sl_c2_npc_vil5_20")
VILLAGER_TALK(vil6, "sl_c2_npc_vil6_00", "sl_c2_npc_vil6_10", "sl_c2_npc_vil6_20")

/* The feast at night, and later the voice on the balcony */
static EVT_BEGIN(bom_23_feast)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_seq_wait, 2)
    CALL(evt_msg_print, 0, STRING("sl_c2_feast_00"), 0, STRING("c2_mayor"))
    CALL(evt_msg_print, 0, STRING("sl_c2_feast_01"), 0, STRING("mario"))
    CALL(c2_anim, STRING("c2_banner"), STRING("C_2"))
    SET_VALUE(SLF(F_FEAST), 1)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* the compass, and the voice: the vignette, the music box, the princess's unheard voice */
static EVT_BEGIN(bom_23_voice)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_snd_bgmoff, PSND_FADE_OUT)
    CALL(evt_fade_entry, FADE_OUT_BLACK, 600, 0, 0, 0)
    CALL(evt_fade_end_wait)
    CALL(evt_mario_set_pos, B23_DOOR_X + 200, 0, B23_DOOR_Z + 120)
    CALL(evt_mario_set_dir, 270, 0, 0)
    CALL(evt_npc_set_position, STRING("c2_mayor"), 0, -1000, 0)
    CALL(evt_npc_set_position, STRING("c2_fuselle"), 0, -1000, 0)
    CALL(evt_fade_entry, FADE_IN_BLACK, 600, 0, 0, 0)
    CALL(evt_fade_end_wait)
    CALL(evt_snd_sfxon, STRING("SFX_EVT_STARSTONE_SHINE1"), 0)
    CALL(evt_mario_set_pose, STRING("M_I_2"))
    CALL(evt_msg_print, 0, STRING("sl_c2_voice_00"), 0, 0)
    CALL(evt_mario_set_pose, STRING("M_S_1"))
    CALL(evt_map_set_blend, 0, 70, 70, 110, 255)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_ORGEL1"))
    CALL(evt_fade_entry, FADE_OUT_SPOT, 800, 0, 0, 0)
    CALL(evt_fade_end_wait)
    CALL(evt_msg_print, 0, STRING("sl_c2_voice_01"), 0, 0)
    CALL(evt_msg_print, 0, STRING("sl_c2_voice_02"), 0, 0)
    CALL(evt_msg_print, 0, STRING("sl_c2_voice_03"), 0, 0)
    CALL(evt_fade_entry, FADE_IN_SPOT, 800, 0, 0, 0)
    CALL(evt_fade_end_wait)
    CALL(evt_snd_sfxon, STRING("SFX_EVT_STARSTONE_SHINE1"), 0)
    CALL(evt_msg_print, 0, STRING("sl_c2_voice_04"), 0, 0)
    CALL(evt_msg_print, 0, STRING("sl_c2_voice_05"), 0, STRING("mario"))
    CALL(evt_msg_print, 0, STRING("sl_c2_voice_06"), 0, STRING("mario"))
    CALL_CHILD(c2_cast_in)
    CALL(evt_msg_print, 0, STRING("sl_c2_voice_07"), 0, LW(11))
    CALL(evt_msg_print, 0, STRING("sl_c2_voice_08"), 0, LW(11))
    CALL(evt_msg_print, 0, STRING("sl_c2_voice_09"), 0, STRING("mario"))
    CALL(evt_msg_print, 0, STRING("sl_c2_voice_10"), 0, LW(11))
    CALL_CHILD(c2_cast_out)
    SET_VALUE(SLF(F_VOICE), 1)
    /* before dawn, out of his gown, to the well */
    CALL(c2_bride, 0)
    CALL(evt_fade_set_mapchange_type, 0, FADE_OUT_BLACK, 1000, FADE_IN_BLACK, 1000)
    CALL(evt_bero_mapchange, STRING("bom_20"), STRING("e_bero"))
    RETURN
EVT_END()

static EVT_BEGIN(bom_23_voice_watch)
    LOOP(0)
        IF_INT_EQUAL(SLF(F_PROPOSAL), 1)
            BREAK_LOOP
        END_IF
        WAIT_FRAMES(10)
    END_LOOP
    WAIT_MS(600)
    CALL_CHILD(bom_23_voice)
    RETURN
EVT_END()

static EVT_BEGIN(bom_23_init)
    IF_INT_EQUAL(SLF(F_HIZZA), 1)
        CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_HAPPY1"))
        CALL(evt_map_set_blend, 0, 110, 110, 170, 255)
    ELSE
        CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG7_BOM1"))
    END_IF
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG7_BOM2"))
    SET_VALUE(LW(0), PTR(bom_23_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("chika_off"), MAPOBJ_FLAG_HIDE)
    CALL(evt_hitobj_onoff, STRING("A_chika"), 1, 0)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("S_moon"), MAPOBJ_FLAG_HIDE)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("S_yuka2"), MAPOBJ_FLAG_HIDE)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("S_yuka3"), MAPOBJ_FLAG_HIDE)
    CALL(evt_mapobj_trans, 1, STRING("chika_off"), 0, FLOAT(-1.0f), 0)
    CALL_CHILD(c2_snow)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("S_ie_naka"), MAPOBJ_FLAG_HIDE)
    /* the feast: the mayor and Fuselle out in the square */
    IF_INT_EQUAL(SLF(F_HIZZA), 1)
        SET_VALUE(LW(0), PTR("c2_mayor"))
        SET_VALUE(LW(1), PTR("c_bomson"))
        SET_VALUE(LW(2), -90)
        SET_VALUE(LW(3), 0)
        SET_VALUE(LW(4), -110)
        SET_VALUE(LW(5), 90)
        SET_VALUE(LW(6), PTR(bom_23_mayor_talk))
        CALL_CHILD(c2_person)
        CALL(evt_npc_set_tribe, STRING("c2_mayor"), STRING(SJ2_MAYOR))
        SET_VALUE(LW(0), PTR("c2_fuselle"))
        SET_VALUE(LW(1), PTR("c_sl_fuselle"))
        SET_VALUE(LW(2), -50)
        SET_VALUE(LW(4), -100)
        SET_VALUE(LW(6), PTR(bom_23_fuselle_talk))
        CALL_CHILD(c2_person)
    END_IF
    /* the square's people */
    SET_VALUE(LW(1), PTR("c_hito_b"))
    SET_VALUE(LW(3), 0)
    SET_VALUE(LW(0), PTR("c2_elder"))
    SET_VALUE(LW(2), -260)
    SET_VALUE(LW(4), 60)
    SET_VALUE(LW(5), 90)
    SET_VALUE(LW(6), PTR(bom_23_talk_elder))
    CALL_CHILD(c2_person)
    SET_VALUE(LW(0), PTR("c2_child"))
    SET_VALUE(LW(1), PTR("c_bom_n"))
    SET_VALUE(LW(2), -150)
    SET_VALUE(LW(4), 160)
    SET_VALUE(LW(6), PTR(bom_23_talk_child))
    CALL_CHILD(c2_person)
    CALL(evt_npc_set_scale, STRING("c2_child"), FLOAT(0.7f), FLOAT(0.7f), FLOAT(0.7f))
    SET_VALUE(LW(0), PTR("c2_cook"))
    SET_VALUE(LW(1), PTR("c_bom_deb"))
    SET_VALUE(LW(2), 140)
    SET_VALUE(LW(4), 140)
    SET_VALUE(LW(5), 270)
    SET_VALUE(LW(6), PTR(bom_23_talk_cook))
    CALL_CHILD(c2_person)
    SET_VALUE(LW(1), PTR("c_hito_b"))
    SET_VALUE(LW(0), PTR("c2_vil4"))
    SET_VALUE(LW(2), 260)
    SET_VALUE(LW(4), 40)
    SET_VALUE(LW(6), PTR(bom_23_talk_vil4))
    CALL_CHILD(c2_person)
    SET_VALUE(LW(1), PTR("c_bom_n"))
    SET_VALUE(LW(0), PTR("c2_vil5"))
    SET_VALUE(LW(2), 60)
    SET_VALUE(LW(4), 220)
    SET_VALUE(LW(6), PTR(bom_23_talk_vil5))
    CALL_CHILD(c2_person)
    SET_VALUE(LW(0), PTR("c2_vil6"))
    SET_VALUE(LW(2), -40)
    SET_VALUE(LW(4), 240)
    SET_VALUE(LW(5), 90)
    SET_VALUE(LW(6), PTR(bom_23_talk_vil6))
    CALL_CHILD(c2_person)
    IF_INT_EQUAL(SLF(F_HIZZA), 0)
        /* gloomy: heads down */
        CALL(evt_npc_set_anim, STRING("c2_elder"), STRING("Z_1"))
        CALL(evt_npc_set_anim, STRING("c2_vil4"), STRING("Z_1"))
    END_IF
    IF_INT_EQUAL(SLF(F_HIZZA), 1)
        IF_INT_EQUAL(SLF(F_FEAST), 0)
            SPAWN_EVENT(bom_23_feast)
        END_IF
        IF_INT_EQUAL(SLF(F_VOICE), 0)
            SPAWN_EVENT(bom_23_voice_watch)
        END_IF
    END_IF
    /* the banner over the mayor's door: FUS, then LUIGI, then LUIGI & FUS */
    CALL(c2_has_model, STRING("c_sl_banner"), LW(0))
    IF_INT_EQUAL(LW(0), 1)
        CALL(evt_npc_entry, STRING("c2_banner"), STRING("c_sl_banner"))
        CALL(evt_npc_set_position, STRING("c2_banner"), B23_DOOR_X, 95, B23_DOOR_Z + 15)
        IF_INT_EQUAL(SLF(F_PROPOSAL), 1)
            CALL(evt_npc_set_anim, STRING("c2_banner"), STRING("S_3"))
        ELSE
            IF_INT_EQUAL(SLF(F_HIZZA), 1)
                IF_INT_EQUAL(SLF(F_FEAST), 0)
                    CALL(evt_npc_set_anim, STRING("c2_banner"), STRING("S_1"))
                ELSE
                    CALL(evt_npc_set_anim, STRING("c2_banner"), STRING("S_2"))
                END_IF
            ELSE
                CALL(evt_npc_set_anim, STRING("c2_banner"), STRING("S_1"))
            END_IF
        END_IF
    END_IF
    TOUR(bom_23_bero, "A_ie_door,A_ie_door_u")
    RETURN
EVT_END()

/* ======================================================================
 * bom_24, Plumpbelly East (bom_02): the tailor, the shop, the inn, Old Bertha, the Bridal Road
 * ====================================================================== */

#define B24_WALL_X -33
#define B24_WALL_Z -120
/* Old Bertha: bom_02's cannon, its base (pCube98) about 80 across round 70, 90, in front of the houses; the
 * barrel's mouth up to the west (pCylinder39). (At 200, -40 her point stood by the houses, out of anyone's reach.) */
#define B24_CANNON_X 70
#define B24_CANNON_Z 90
#define B24_MUZZLE_X 0
#define B24_MUZZLE_Y 170
#define B24_MUZZLE_Z 110
#define B24_KID_X 140 /* the kid, to her east, facing her */
#define B24_KID_Z 40
#define B24_SHINE_X 10 /* where the Shine lands, in front of her to the west */
#define B24_SHINE_Z 30
#define B24_ROAD_X 430

static BeroEntry bom_24_bero[] = {
    {"w_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "bom_23", "e_bero", 1, 1, 0, 0},
    {0},
};

/* the inn (ie1), the shop (ie2) and the tailor's (ie3) doors, as bom_02 has them */
static EVT_BEGIN(b24_ie1_in)
    MULTIPLY_FLOAT(LW(0), FLOAT(0.44433594f))
    CALL(evt_mapobj_rotate, 1, STRING("S_ie1_door"), FLOAT(0.0f), LW(0), FLOAT(0.0f))
    RETURN
EVT_END()
static EVT_BEGIN(b24_ie1_wall)
    MULTIPLY_FLOAT(LW(0), FLOAT(-0.6660156f))
    CALL(evt_mapobj_rotate, 1, STRING("S_ie1_kai"), FLOAT(0.0f), FLOAT(0.0f), LW(0))
    RETURN
EVT_END()
static EVT_BEGIN(b24_ie2_in)
    MULTIPLY_FLOAT(LW(0), FLOAT(0.44433594f))
    CALL(evt_mapobj_rotate, 1, STRING("S_ie2_door"), FLOAT(0.0f), LW(0), FLOAT(0.0f))
    RETURN
EVT_END()
static EVT_BEGIN(b24_ie2_wall)
    MULTIPLY_FLOAT(LW(0), FLOAT(0.5f))
    CALL(evt_mapobj_rotate, 1, STRING("S_ie2_kai"), FLOAT(0.0f), FLOAT(0.0f), LW(0))
    RETURN
EVT_END()
static EVT_BEGIN(b24_ie3_in)
    MULTIPLY_FLOAT(LW(0), FLOAT(0.44433594f))
    CALL(evt_mapobj_rotate, 1, STRING("S_ie3_door"), FLOAT(0.0f), LW(0), FLOAT(0.0f))
    RETURN
EVT_END()
static EVT_BEGIN(b24_ie3_out)
    MULTIPLY_FLOAT(LW(0), FLOAT(0.5f))
    CALL(evt_mapobj_rotate, 1, STRING("S_ie3_door"), FLOAT(0.0f), FLOAT(0.0f), LW(0))
    RETURN
EVT_END()
static EVT_BEGIN(b24_ie3_wall)
    MULTIPLY_FLOAT(LW(0), FLOAT(-0.5f))
    CALL(evt_mapobj_rotate, 1, STRING("S_ie3_kai"), FLOAT(0.0f), FLOAT(0.0f), LW(0))
    RETURN
EVT_END()
static EVT_BEGIN(b24_door_open)
    CALL(evt_snd_bgmoff, PSND_FADE_DIP_TO(50))
    CALL(evt_snd_envoff, PSND_FADE_DIP_TO(50))
    CALL(evt_snd_env_lpf, 0, 800)
    RETURN
EVT_END()
static EVT_BEGIN(b24_door_close)
    CALL(evt_snd_bgmon, PSND_FADE_RESUME | PSND_FADE_UNDIP, 0)
    CALL(evt_snd_envon, PSND_FADE_RESUME | PSND_FADE_UNDIP, 0)
    CALL(evt_snd_env_lpf, 0, 0)
    RETURN
EVT_END()
static const char* b24_ie1_npc[] = {"c2_innkeep", NULL};
static const char* b24_ie3_item[] = {"item_hearth", NULL};
static const char* b24_ie2_npc[] = {"c2_shopkeep", "c2_tailor", NULL};
static const char* b24_ie3_npc[] = {"c2_mayor", "c2_fuselle", NULL};
static DoorParam b24_ie1 = {0, 3, 1, "S_ie1_door", "A_ie1_door1", "A_ie1_door1_u", "S_ie1_soto", NULL, "S_ie1_naka", NULL,
                            b24_ie1_in, NULL, b24_ie1_wall, b24_ie1_npc, NULL, NULL, b24_door_open, b24_door_close};
static DoorParam b24_ie2 = {0, 3, 1, "S_ie2_door", "A__ie2_door2", "A__ie2_door2_u", "S_ie2_soto", NULL, "S_ie2_naka", NULL,
                            b24_ie2_in, NULL, b24_ie2_wall, b24_ie2_npc, NULL, NULL, b24_door_open, b24_door_close};
static DoorParam b24_ie3 = {0, 3, 1, "S_ie3_door", "A_ie3_door3_u", "A_ie3_door3", "S_ie3_soto", NULL, "S_ie3_naka", NULL,
                            b24_ie3_in, b24_ie3_out, b24_ie3_wall, b24_ie3_npc, NULL, b24_ie3_item, b24_door_open, b24_door_close};

/* Plumpbelly Provisions */
static ShopObj b24_shop_spots[] = {
    {"S_item_01", "A_item_01"}, {"S_item_02", "A_item_02"}, {"S_item_03", "A_item_03"},
    {"S_item_04", "A_item_04"}, {"S_item_05", "A_item_05"}, {"S_item_06", "A_item_06"},
};
static ShopItem b24_goods[] = {
    {ITEM_MUSHROOM, 5}, {ITEM_SUPER_SHROOM, 20}, {ITEM_HONEY_SYRUP, 10},
    {ITEM_FIRE_FLOWER, 10}, {ITEM_ICE_STORM, 15}, {ITEM_SLEEPY_SHEEP, 8},
};
static ShopItem b24_goods_after[] = {
    {ITEM_MUSHROOM, 5}, {ITEM_SUPER_SHROOM, 20}, {ITEM_HONEY_SYRUP, 10},
    {ITEM_FIRE_FLOWER, 10}, {ITEM_POW_BLOCK, 10}, {ITEM_MOUSSE_CAKE, 20},
};
static ShopDb b24_trade[] = {
    {ITEM_MUSHROOM, 2, 0}, {ITEM_SUPER_SHROOM, 10, 0}, {ITEM_HONEY_SYRUP, 5, 0}, {ITEM_FIRE_FLOWER, 5, 0},
    {ITEM_ICE_STORM, 7, 0}, {ITEM_FRIGHT_MASK, 5, 0}, {ITEM_POW_BLOCK, 5, 0}, {ITEM_SLEEPY_SHEEP, 4, 0},
    {ITEM_MOUSSE_CAKE, 10, 0}, {0},
};
static ShopperData35 b24_shopper = {
    "c2_shopkeep", NULL, NULL,
    {"shop_00", "shop_01", "shop_02", "shop_03", "shop_04", "shop_05", "shop_06", "shop_07", "shop_08", "shop_09",
     "shop_10", "shop_11", "shop_12", "shop_13", "shop_14", "shop_15", "shop_16", "shop_17", "shop_18", "shop_19",
     "shop_20", "shop_21", "shop_22", "shop_23", "shop_24", "shop_25", "shop_26", "shop_27", "shop_28", "shop_29",
     "shop_30", "shop_31", "shop_32", "shop_33", "shop_34"},
};

/* The Full Belly (bom_02's inn rooms), our innkeeper's words */
static KinopioData b24_inn = {
    ITEM_MUSHROOM, {-425.0f, 50.0f, -200.0f}, 10, {-285.0f, 46.0f, -223.0f}, {-273.0f, 30.0f, -177.0f},
    {-445.0f, 258.0f, 88.0f, -341.0f, 92.0f, -191.0f}, -25.0f, {-320.0f, 30.0f, -115.0f}, "c2_innkeep",
    {"sl_c2_yado_00", "sl_c2_yado_01", "sl_c2_yado_02", "sl_c2_yado_03", "sl_c2_yado_04", "sl_c2_yado_05",
     "sl_c2_yado_06", "sl_c2_yado_07", "sl_c2_yado_08", "sl_c2_yado_09", "sl_c2_yado_10", "sl_c2_yado_11",
     "sl_c2_yado_12", "sl_c2_yado_13"},
    NULL, NULL,
};

/* The Blast tutorial: Jerry and the packed ice across the tailor's lane */
static EVT_BEGIN(bom_24_lane_talk)
    CALL(evt_mario_key_onoff, 0)
    CALL(c2_out, LW(0))
    IF_INT_NOT_EQUAL(LW(0), JERRY)
        CALL(evt_mario_goodbye_party, 0)
        CALL(evt_mario_get_pos, 0, LW(1), LW(2), LW(3))
        CALL(evt_mario_set_party_pos, 0, JERRY, LW(1), LW(2), LW(3))
        CALL(evt_party_run, 0)
        WAIT_MS(400)
    END_IF
    CALL(evt_msg_print, 0, STRING("sl_c2_blast_00"), 0, STRING("party"))
    CALL(evt_msg_print, 0, STRING("sl_c2_blast_01"), 0, STRING("party"))
    CALL(evt_msg_print, 0, STRING("sl_je_blast_help"), 0, 0)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(bom_24_lane_watch)
    LOOP(0)
        IF_INT_EQUAL(SLF(F_JERRY), 1)
            CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
            SUBTRACT(LW(0), B24_WALL_X)
            IF_INT_LESS(LW(0), 120)
                IF_INT_GREATER(LW(0), -120)
                    BREAK_LOOP
                END_IF
            END_IF
        END_IF
        WAIT_FRAMES(4)
    END_LOOP
    CALL_CHILD(bom_24_lane_talk)
    RETURN
EVT_END()

static EVT_BEGIN(bom_24_lane_blast)
    /* held while it crumbles: his line came after Luigi had walked into the shop */
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_npc_set_anim, STRING("c2_lane_ice"), STRING("B_1"))
    CALL(evt_snd_sfxon, STRING("SFX_OFF_WALL_BREAK1"), 0)
    WAIT_MS(700)
    CALL(evt_npc_delete, STRING("c2_lane_ice"))
    SET_VALUE(SLF(F_LANE), 1)
    WAIT_MS(600)
    CALL(evt_msg_print, 0, STRING("sl_c2_blast_02"), 0, STRING("party"))
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* Madame Wickworth: the gown, the errands, the reveal */
static EVT_BEGIN(bom_24_gown)
    CALL(evt_msg_print, 0, STRING("sl_c2_gown_00"), 0, STRING("c2_tailor"))
    CALL(evt_snd_bgmoff, PSND_FADE_OUT)
    CALL(evt_fade_entry, FADE_OUT_BLACK, 500, 0, 0, 0)
    CALL(evt_fade_end_wait)
    LOOP(4)
        CALL(evt_snd_sfxon, STRING("SFX_PARTY_BATTLE_MOVE1L"), 0)
        WAIT_MS(450)
    END_LOOP
    WAIT_MS(400)
    CALL(c2_bride, 1)
    /* staged in the dark: Luigi on the open floor, the tailor stepped back to admire her work (she stood
     * between him and the camera, over the gown) */
    CALL(evt_mario_set_pos, 15, 30, -360)
    CALL(evt_mario_set_dir, 90, 0, 0)
    CALL(evt_npc_set_position, STRING("c2_tailor"), 135, 30, -415)
    SET_VALUE(SLF(F_GOWN), 1)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_FF_ENV_SURPRISED2"))
    CALL(evt_fade_entry, FADE_IN_BLACK, 500, 0, 0, 0)
    CALL(evt_fade_end_wait)
    CALL(evt_npc_entry, STRING("c2_fuselle2"), STRING("c_sl_fuselle"))
    CALL(evt_npc_set_tribe, STRING("c2_fuselle2"), STRING(SJ2_VILLAGER))
    CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
    /* beside him and a step back: the gown is wide */
    ADD(LW(0), 72)
    SUBTRACT(LW(2), 22)
    CALL(evt_npc_set_position, STRING("c2_fuselle2"), LW(0), LW(1), LW(2))
    CALL(evt_npc_set_ry, STRING("c2_fuselle2"), 270)
    CALL(c2_lit, STRING("c2_fuselle2"))
    CALL(evt_luigi_story_pose, STRING("TW_1"))
    WAIT_FRAMES(48)
    CALL(evt_luigi_story_pose, 0)
    CALL(evt_msg_print, 0, STRING("sl_c2_gown_01"), 0, STRING("c2_tailor"))
    CALL(evt_npc_set_anim, STRING("c2_fuselle2"), STRING("Q_1"))
    CALL(evt_msg_print, 0, STRING("sl_c2_gown_02"), 0, STRING("c2_fuselle2"))
    CALL(evt_msg_print, 0, STRING("sl_c2_gown_03"), 0, STRING("mario"))
    CALL(evt_npc_set_anim, STRING("c2_fuselle2"), STRING("T_1"))
    CALL(evt_msg_print, 0, STRING("sl_c2_gown_04"), 0, STRING("c2_fuselle2"))
    CALL_CHILD(c2_cast_in)
    CALL(c2_lit, LW(10))
    CALL(c2_lit, LW(11))
    CALL(evt_msg_print, 0, STRING("sl_c2_gown_05"), 0, LW(10))
    CALL(evt_msg_print, 0, STRING("sl_c2_gown_06"), 0, LW(11))
    CALL(evt_msg_print, 0, STRING("sl_c2_gown_07"), 0, STRING("mario"))
    CALL_CHILD(c2_cast_out)
    CALL(evt_npc_delete, STRING("c2_fuselle2"))
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG7_BOM1"))
    RETURN
EVT_END()

static EVT_BEGIN(bom_24_tailor_talk)
    CALL(evt_mario_key_onoff, 0)
    IF_INT_EQUAL(SLF(F_HIZZA), 1)
        CALL(evt_msg_print, 0, STRING("sl_c2_npc_tailor_20"), 0, STRING("c2_tailor"))
        GOTO(99)
    END_IF
    IF_INT_EQUAL(SLF(F_GOWN), 1)
        CALL(evt_msg_print, 0, STRING("sl_c2_npc_tailor_10"), 0, STRING("c2_tailor"))
        GOTO(99)
    END_IF
    IF_INT_EQUAL(SLF(F_TAILOR), 0)
        CALL(evt_msg_print, 0, STRING("sl_c2_tailor_00"), 0, STRING("c2_tailor"))
        CALL(evt_msg_print, 0, STRING("sl_c2_tailor_01"), 0, STRING("c2_tailor"))
        CALL(evt_msg_print, 0, STRING("sl_c2_tailor_02"), 0, STRING("mario"))
        CALL(evt_msg_print, 0, STRING("sl_c2_tailor_03"), 0, STRING("c2_tailor"))
        CALL(evt_msg_print, 0, STRING("sl_c2_tailor_04"), 0, STRING("c2_tailor"))
        SET_VALUE(SLF(F_TAILOR), 1)
        GOTO(99)
    END_IF
    IF_INT_EQUAL(SLF(F_SILK), 1)
        IF_INT_EQUAL(SLF(F_PEARLS), 1)
            CALL_CHILD(bom_24_gown)
            GOTO(99)
        END_IF
    END_IF
    CALL(evt_msg_print, 0, STRING("sl_c2_tailor_05"), 0, STRING("c2_tailor"))
    LABEL(99)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* The Bridal Road: the procession, the pie, up the mountain */
static EVT_BEGIN(bom_24_procession)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_STG8_VOICE1"))
    CALL(evt_npc_entry, STRING("c2_mayor_p"), STRING("c_bomson"))
    CALL(evt_npc_set_tribe, STRING("c2_mayor_p"), STRING(SJ2_MAYOR))
    CALL(evt_npc_set_position, STRING("c2_mayor_p"), B24_ROAD_X - 120, 0, 120)
    CALL(evt_npc_entry, STRING("c2_fuselle_p"), STRING("c_sl_fuselle"))
    CALL(evt_npc_set_tribe, STRING("c2_fuselle_p"), STRING(SJ2_VILLAGER))
    CALL(evt_npc_set_position, STRING("c2_fuselle_p"), B24_ROAD_X - 300, 0, 100)
    CALL(evt_mario_mov_pos2, B24_ROAD_X - 60, 80, FLOAT(60.0f))
    CALL(evt_mario_set_dir, 270, 0, 0)
    CALL(evt_npc_set_anim, STRING("c2_fuselle_p"), STRING("R_1"))
    CALL(evt_npc_move_position, STRING("c2_fuselle_p"), B24_ROAD_X - 110, 80, 0, FLOAT(120.0f), 4)
    CALL(evt_npc_set_anim, STRING("c2_fuselle_p"), STRING("G_1"))
    CALL(evt_msg_print, 0, STRING("sl_c2_procession_00"), 0, STRING("c2_fuselle_p"))
    CALL(evt_npc_set_anim, STRING("c2_fuselle_p"), STRING("S_1"))
    CALL(evt_msg_print, 0, STRING("sl_c2_procession_01"), 0, STRING("c2_mayor_p"))
    CALL(evt_msg_print, 0, STRING("sl_c2_procession_02"), 0, STRING("mario"))
    CALL(evt_msg_print, 0, STRING("sl_c2_procession_03"), 0, STRING("c2_mayor_p"))
    CALL(c2_give, ITEM_MOUSSE_CAKE)
    SET_VALUE(LW(0), PTR("sl_c2_procession_04"))
    CALL_CHILD(c2_got)
    SET_VALUE(SLF(F_PIE), 1)
    SET_VALUE(SLF(F_PROCESSION), 1)
    CALL(evt_mario_mov_pos2, B24_ROAD_X + 40, 80, FLOAT(40.0f))
    CALL(evt_fade_set_mapchange_type, 0, FADE_OUT_BLACK, 1200, FADE_IN_BLACK, 800)
    CALL(evt_bero_mapchange, STRING("dou_20"), STRING("w_bero"))
    RETURN
EVT_END()

static EVT_BEGIN(bom_24_road_watch)
    LOOP(0)
        CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
        IF_INT_GREATER(LW(0), B24_ROAD_X)
            IF_INT_EQUAL(SLF(F_GOWN), 1)
                BREAK_LOOP
            END_IF
            CALL(evt_mario_key_onoff, 0)
            CALL(evt_msg_print, 0, STRING("sl_c2_road_shut"), 0, STRING("mario"))
            CALL(evt_mario_mov_pos2, B24_ROAD_X - 40, LW(2), FLOAT(80.0f))
            CALL(evt_mario_key_onoff, 1)
        END_IF
        WAIT_FRAMES(4)
    END_LOOP
    CALL_CHILD(bom_24_procession)
    RETURN
EVT_END()

/* after the boss: back down the road from the grotto */
static EVT_BEGIN(bom_24_road_back)
    LOOP(0)
        CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
        IF_INT_GREATER(LW(0), B24_ROAD_X + 20)
            BREAK_LOOP
        END_IF
        WAIT_FRAMES(4)
    END_LOOP
    CALL(evt_bero_mapchange, STRING("dou_20"), STRING("w_bero"))
    RETURN
EVT_END()

static EVT_BEGIN(bom_24_kid_talk)
    CALL(evt_mario_key_onoff, 0)
    IF_INT_EQUAL(SLF(F_HIZZA), 1)
        CALL(evt_msg_print, 0, STRING("sl_c2_npc_kid_20"), 0, STRING("c2_kid"))
    ELSE
        CALL(evt_msg_print, 0, STRING("sl_c2_npc_kid_00"), 0, STRING("c2_kid"))
    END_IF
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(bom_24_innkeep_badge)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_msg_print, 0, STRING("sl_c2_npc_inn_20"), 0, STRING("c2_innkeep"))
    CALL(evt_item_entry, STRING("c2_hh"), ITEM_HAPPY_HEART, 0, -1000, 0, ITEM_ENTRY_FIXED, -1, 0)
    CALL(evt_item_get_item, STRING("c2_hh"))
    SET_VALUE(SLF(F_HAPPY), 1)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* Old Bertha's pie salute (Blast at her fuse, after the boss): a Shine Sprite stuck in the barrel */
static EVT_BEGIN(bom_24_bertha)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_snd_sfxon, STRING("SFX_ENM_KILLER_CANNON1"), 0)
    CALL(evt_cam_shake, 4, FLOAT(0.05f), FLOAT(0.05f), 700)
    CALL(evt_eff, 0, STRING("kemuri_test"), 4, B24_CANNON_X, 80, B24_CANNON_Z, FLOAT(3.0f), 0, 0, 0, 0, 0, 0, 0)
    CALL(evt_eff, 0, STRING("hit"), 1, B24_MUZZLE_X, B24_MUZZLE_Y, B24_MUZZLE_Z, FLOAT(3.0f), 4, 0, 0, 0, 0, 0, 0)
    WAIT_MS(500)
    CALL(evt_eff, 0, STRING("confetti"), 0, B24_MUZZLE_X, B24_MUZZLE_Y + 60, B24_MUZZLE_Z, 300, 0, 0, 0, 0, 0, 0, 0)
    SET_VALUE(SLF(F_BERTHA), 1)
    CALL(evt_msg_print, 0, STRING("sl_c2_bertha_00"), 0, STRING("c2_kid"))
    CALL(evt_item_entry, STRING("c2_shine2"), ITEM_SHINE_SPRITE, B24_SHINE_X, 0, B24_SHINE_Z, ITEM_ENTRY_STAY, SLF(F_P24_SHINE), 0)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(bom_24_innkeep_talk)
    IF_INT_EQUAL(SLF(F_HIZZA), 1)
        IF_INT_EQUAL(SLF(F_HAPPY), 0)
            CALL_CHILD(bom_24_innkeep_badge)
            RETURN
        END_IF
    END_IF
    RETURN
EVT_END()

static EVT_BEGIN(bom_24_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG7_BOM1"))
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG7_BOM3"))
    SET_VALUE(LW(0), PTR(bom_24_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL(evt_bero_get_entername, LW(0))
    IF_STR_EQUAL(LW(0), STRING("sl_road"))
        CALL(evt_mario_set_pos, B24_ROAD_X - 20, 0, 80)
        CALL(evt_mario_set_dir, 270, 0, 0)
    END_IF
    CALL_CHILD(c2_snow)
    CALL(evt_map_playanim, STRING("yuge"), 1, 0)
    /* the shop, the inn and the tailor's: their people and doors */
    SET_VALUE(LW(0), PTR("c2_shopkeep"))
    SET_VALUE(LW(1), PTR("c_hito_b"))
    SET_VALUE(LW(2), 125)
    SET_VALUE(LW(3), 30)
    SET_VALUE(LW(4), -450)
    SET_VALUE(LW(5), 90)
    SET_VALUE(LW(6), 0)
    CALL_CHILD(c2_person)
    IF_INT_EQUAL(SLF(F_HIZZA), 1)
        CALL(evt_shop_setup, PTR(b24_shop_spots), PTR(b24_goods_after), PTR(&b24_shopper), PTR(b24_trade))
    ELSE
        CALL(evt_shop_setup, PTR(b24_shop_spots), PTR(b24_goods), PTR(&b24_shopper), PTR(b24_trade))
    END_IF
    SET_VALUE(LW(0), PTR("c2_innkeep"))
    SET_VALUE(LW(1), PTR("c_bom_deb"))
    SET_VALUE(LW(2), -320)
    SET_VALUE(LW(3), 30)
    SET_VALUE(LW(4), -160)
    SET_VALUE(LW(5), 90)
    SET_VALUE(LW(6), 0)
    CALL_CHILD(c2_person)
    IF_INT_EQUAL(SLF(F_HIZZA), 1)
        IF_INT_EQUAL(SLF(F_HAPPY), 0)
            CALL(evt_npc_change_interrupt, STRING("c2_innkeep"), 6, PTR(bom_24_innkeep_badge))
        ELSE
            CALL(evt_kinopio_setup, PTR(&b24_inn))
        END_IF
    ELSE
        CALL(evt_kinopio_setup, PTR(&b24_inn))
    END_IF
    SET_VALUE(LW(0), PTR("c2_tailor"))
    SET_VALUE(LW(1), PTR("c_hito_b"))
    SET_VALUE(LW(2), 75)
    SET_VALUE(LW(3), 30)
    SET_VALUE(LW(4), -385)
    SET_VALUE(LW(5), 270)
    SET_VALUE(LW(6), PTR(bom_24_tailor_talk))
    CALL_CHILD(c2_person)
    /* the village has one Bob-omb of hers and the shopkeeper's: she wears lavender */
    CALL(evt_npc_set_color, STRING("c2_tailor"), 235, 175, 255, 255)
    /* the mayor's house (what was General White's) */
    IF_INT_EQUAL(SLF(F_HIZZA), 0)
        SET_VALUE(LW(0), PTR("c2_mayor"))
        SET_VALUE(LW(1), PTR("c_bomson"))
        /* on the open floor before the bed (at z -170 he stood inside the table, unseen) */
        SET_VALUE(LW(2), 330)
        SET_VALUE(LW(3), 30)
        SET_VALUE(LW(4), -130)
        SET_VALUE(LW(5), 270)
        SET_VALUE(LW(6), PTR(bom_23_mayor_talk))
        CALL_CHILD(c2_person)
        CALL(evt_npc_set_tribe, STRING("c2_mayor"), STRING(SJ2_MAYOR))
        SET_VALUE(LW(0), PTR("c2_fuselle"))
        SET_VALUE(LW(1), PTR("c_sl_fuselle"))
        SET_VALUE(LW(2), 390)
        SET_VALUE(LW(3), 45)
        SET_VALUE(LW(4), -150)
        SET_VALUE(LW(6), PTR(bom_23_fuselle_talk))
        CALL_CHILD(c2_person)
        CALL(evt_item_entry, STRING("item_hearth"), ITEM_STAR_PIECE, 280, 30, -200, ITEM_ENTRY_FIXED, SLF(F_P23_PIECE), 0)
    END_IF
    IF_INT_EQUAL(SLF(F_JERRY), 0)
        CALL(evt_npc_entry, STRING("c2_jerry"), STRING("c_lp_bom"))
        CALL(evt_npc_set_tribe, STRING("c2_jerry"), STRING(SJ2_VILLAGER))
        CALL(evt_npc_set_position, STRING("c2_jerry"), 300, 0, -20)
        CALL(evt_npc_set_ry, STRING("c2_jerry"), 270)
        CALL(evt_npc_set_anim, STRING("c2_jerry"), STRING("S_1"))
        SPAWN_EVENT(bom_23_jerry_watch)
    END_IF

    SET_VALUE(LW(0), PTR(&b24_ie1))
    CALL_CHILD(evt_door_entry)
    SET_VALUE(LW(0), PTR(&b24_ie2))
    CALL_CHILD(evt_door_entry)
    SET_VALUE(LW(0), PTR(&b24_ie3))
    CALL_CHILD(evt_door_entry)
    /* the kid by Old Bertha */
    SET_VALUE(LW(0), PTR("c2_kid"))
    SET_VALUE(LW(1), PTR("c_bom_n"))
    SET_VALUE(LW(2), B24_KID_X)
    SET_VALUE(LW(3), 0)
    SET_VALUE(LW(4), B24_KID_Z)
    SET_VALUE(LW(5), 270)
    SET_VALUE(LW(6), PTR(bom_24_kid_talk))
    CALL_CHILD(c2_person)
    CALL(evt_npc_set_scale, STRING("c2_kid"), FLOAT(0.7f), FLOAT(0.7f), FLOAT(0.7f))
    IF_INT_EQUAL(SLF(F_HIZZA), 1)
        IF_INT_EQUAL(SLF(F_BERTHA), 0)
            /* (wide: Jerry goes off beside Luigi, who stands at her base's edge, 40 out) */
            CALL(evt_sl_point, PORT_SL_ABILITY_BLAST, 1, B24_CANNON_X, 0, B24_CANNON_Z, 100, 0, PTR(bom_24_bertha))
        END_IF
        IF_INT_EQUAL(SLF(F_BERTHA), 1)
            CALL(evt_item_entry, STRING("c2_shine2"), ITEM_SHINE_SPRITE, B24_SHINE_X, 0, B24_SHINE_Z, ITEM_ENTRY_STAY, SLF(F_P24_SHINE), 0)
        END_IF
    END_IF
    CALL(evt_mobj_brick, STRING("mobj_hidden"), -230, 60, 40, ITEM_STAR_PIECE, 11, 0, SLF(F_P24_PIECE))
    CALL(evt_mobj_save_blk, STRING("mobj_save"), -300, 65, 10, 0, 0)
    CALL(evt_mobj_kururing_floor, STRING("kururin1"), -30, 0, 115, STRING("S_wakuaaa"), 125, SLF(F_PANEL30))
    /* the ice wall across the tailor's lane */
    IF_INT_EQUAL(SLF(F_LANE), 0)
        CALL(evt_npc_entry, STRING("c2_lane_ice"), STRING("c_sl_ice_wall_large"))
        CALL(evt_npc_set_position, STRING("c2_lane_ice"), B24_WALL_X, 0, B24_WALL_Z)
        CALL(evt_npc_set_anim, STRING("c2_lane_ice"), STRING("S_1"))
        CALL(evt_sl_point, PORT_SL_ABILITY_BLAST, 0, B24_WALL_X, 0, B24_WALL_Z, 60, 0, PTR(bom_24_lane_blast))
        SPAWN_EVENT(bom_24_lane_watch)
    END_IF
    IF_INT_EQUAL(SLF(F_PROCESSION), 0)
        SPAWN_EVENT(bom_24_road_watch)
    ELSE
        IF_INT_EQUAL(SLF(F_HIZZA), 1)
            SPAWN_EVENT(bom_24_road_back)
        END_IF
    END_IF
    TOUR(bom_24_bero, "A_ie1_door1,A__ie2_door2,A_ie3_door3")
    RETURN
EVT_END()

/* ======================================================================
 * The Hollow (dou_20-dou_30)
 * ====================================================================== */

/* dou_20, the Hollow Mouth (dou_00): the guards at the inner gate */
static BeroEntry dou_20_bero[] = {
    {"w_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, NULL, NULL, 1, 1, 0, 0},
    {"e_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_21", "w_bero", 1, 1, 0, 0},
    {0},
};
static BeroEntry dou_20_bero_open[] = {
    {"w_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "bom_24", "sl_road", 1, 1, 0, 0},
    {"e_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_21", "w_bero", 1, 1, 0, 0},
    {0},
};

#define D20_ASIDE_X 100
#define D20_ASIDE_X2 40
#define D20_ASIDE_Z1 -70 /* both at the back: in front, one hid Luigi from the camera */
#define D20_ASIDE_Z2 -75

static EVT_BEGIN(dou_20_gate)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_seq_wait, 2)
    /* up the water to the foot of the steps, the guards in sight */
    CALL(evt_mario_mov_pos2, -20, -10, FLOAT(60.0f))
    CALL(evt_mario_set_dir, 90, 0, 0)
    WAIT_MS(300)
    CALL(evt_msg_print, 0, STRING("sl_c2_hollow_gate_00"), 0, STRING("c2_hg1"))
    CALL(evt_msg_print, 0, STRING("sl_c2_hollow_gate_01"), 0, STRING("mario"))
    CALL(evt_msg_print, 0, STRING("sl_c2_hollow_gate_02"), 0, STRING("c2_hg2"))
    CALL(evt_msg_print, 0, STRING("sl_c2_hollow_gate_03"), 0, STRING("c2_hg1"))
    CALL(evt_msg_print, 0, STRING("sl_c2_hollow_gate_04"), 0, STRING("c2_hg2"))
    CALL(evt_msg_print, 0, STRING("sl_c2_hollow_gate_05"), 0, STRING("c2_hg1"))
    CALL(evt_msg_print, 0, STRING("sl_c2_hollow_gate_06"), 0, STRING("c2_hg2"))
    CALL(evt_msg_print, 0, STRING("sl_c2_hollow_gate_07"), 0, STRING("mario"))
    CALL(evt_msg_print, 0, STRING("sl_c2_hollow_gate_08"), 0, STRING("c2_hg1"))
    CALL_CHILD(c2_cast_in)
    CALL(evt_msg_print, 0, STRING("sl_c2_hollow_gate_09"), 0, LW(11))
    CALL_CHILD(c2_cast_out)
    CALL(evt_msg_print, 0, STRING("sl_c2_hollow_gate_10"), 0, 0)
    /* they stand aside, and face the way in */
    CALL(evt_npc_move_position, STRING("c2_hg1"), D20_ASIDE_X, D20_ASIDE_Z1, 0, FLOAT(60.0f), 4)
    CALL(evt_npc_move_position, STRING("c2_hg2"), D20_ASIDE_X2, D20_ASIDE_Z2, 0, FLOAT(60.0f), 4)
    CALL(evt_npc_set_ry, STRING("c2_hg1"), 90)
    CALL(evt_npc_set_ry, STRING("c2_hg2"), 90)
    SET_VALUE(SLF(F_HOLLOW), 1)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* a Hissguard to talk to: the bride is greeted */
static EVT_BEGIN(c2_guard_greet)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_sub_random, 2, LW(1))
    SWITCH(LW(1))
        CASE_EQUAL(0)
        CALL(evt_msg_print, 0, STRING("sl_c2_guard_01"), 0, STRING("c2_hg1"))
        CASE_EQUAL(1)
        CALL(evt_msg_print, 0, STRING("sl_c2_guard_02"), 0, STRING("c2_hg1"))
        CASE_DEFAULT
        CALL(evt_msg_print, 0, STRING("sl_c2_guard_03"), 0, STRING("c2_hg1"))
    END_SWITCH
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* The grotto's water, as each borrowed room has it (rels/dou/src): fall in and the game's biter
 * has you, back to the last safe ground or to the point given. Without them Luigi was thrown to
 * the room's origin by the fall's fallback, and the biter's NPC wasn't there. */
static HitDamageReturn d22_water[] = {
    {"mod_01", {0.0f, -1000.0f, 0.0f}}, {"mod_02", {0.0f, -1000.0f, 0.0f}}, {"mod_03", {0.0f, -1000.0f, 0.0f}},
    {"mod_04", {0.0f, -1000.0f, 0.0f}}, {"mod_06", {556.0f, 50.0f, -90.0f}}, {"mod_08", {0.0f, -1000.0f, 0.0f}},
    {"Ship_Change", {0.0f, -1000.0f, 0.0f}}, {0},
};
static HitDamageReturn d24_water[] = {
    {"mod_01", {0.0f, -1000.0f, 0.0f}}, {"mod_02", {0.0f, -1000.0f, 0.0f}}, {"mod_03", {0.0f, -1000.0f, 0.0f}},
    {"mod_04", {0.0f, -1000.0f, 0.0f}}, {"mod_05", {0.0f, -1000.0f, 0.0f}}, {"mod_06", {230.0f, 50.0f, 50.0f}},
    {"mod_07", {0.0f, -1000.0f, 0.0f}}, {"mod_09", {0.0f, -1000.0f, 0.0f}}, {"mod_10", {0.0f, -1000.0f, 0.0f}},
    {"mod_11", {0.0f, -1000.0f, 0.0f}}, {"mod_12", {0.0f, -1000.0f, 0.0f}}, {"mod_13", {0.0f, -1000.0f, 0.0f}},
    {"mod_14", {0.0f, -1000.0f, 0.0f}}, {"mod_15", {0.0f, -1000.0f, 0.0f}}, {"mod_16", {0.0f, -1000.0f, 0.0f}},
    {"mod_17", {435.0f, 25.0f, 10.0f}}, {"Ship_Change", {0.0f, -1000.0f, 0.0f}}, {0},
};
static HitDamageReturn d25_water[] = {
    {"mod_01", {120.0f, 10.0f, 0.0f}}, {"mod_02", {-90.0f, 10.0f, -110.0f}}, {"mod_03", {-150.0f, 10.0f, 50.0f}}, {0},
};
static HitDamageReturn d28_water[] = {
    {"mod_01", {0.0f, -1000.0f, 0.0f}}, {"mod_02", {0.0f, -1000.0f, 0.0f}}, {"mod_03", {0.0f, -1000.0f, 0.0f}},
    {"mod_04", {0.0f, -1000.0f, 0.0f}}, {"Ship_Change", {0.0f, -1000.0f, 0.0f}}, {0},
};
/* (dou_09's, and dou_12's: the same four) */
static HitDamageReturn d26_water[] = {
    {"mod_01", {0.0f, -1000.0f, 0.0f}}, {"mod_02", {0.0f, -1000.0f, 0.0f}}, {"mod_03", {0.0f, -1000.0f, 0.0f}},
    {"mod_04", {0.0f, -1000.0f, 0.0f}}, {0},
};
static HitDamageReturn d27_water[] = {
    {"mod_01", {-700.0f, 50.0f, -90.0f}}, {"mod_03", {-130.0f, 50.0f, -90.0f}}, {"mod_04", {0.0f, -1000.0f, 0.0f}}, {0},
};
static HitDamageReturn d29_water[] = {
    {"mod_01", {725.0f, 50.0f, 90.0f}}, {"mod_02", {-650.0f, 200.0f, 0.0f}}, {"mod_03", {0.0f, -1000.0f, 0.0f}},
    {"mod_04", {-208.0f, 50.0f, -96.0f}}, {"mod_010", {0.0f, -1000.0f, 0.0f}}, {"Ship_Change_01", {0.0f, -1000.0f, 0.0f}},
    {"Ship_Change_02", {0.0f, -1000.0f, 0.0f}}, {"Ship_Change_03", {0.0f, -1000.0f, 0.0f}},
    {"Ship_Change_04", {0.0f, -1000.0f, 0.0f}}, {"a11_a_usiro", {160.0f, 37.0f, 0.0f}},
    {"a11_a_kaidana_01", {160.0f, 37.0f, 0.0f}}, {"a11_a_kaidanb_01", {160.0f, 37.0f, 0.0f}},
    {"dokan_1", {0.0f, -1000.0f, 0.0f}}, {"dokan_2", {0.0f, -1000.0f, 0.0f}}, {"dokan_3", {0.0f, -1000.0f, 0.0f}},
    {"dokan_7", {0.0f, -1000.0f, 0.0f}}, {0},
};

/* LW(0): the room's table */
static EVT_BEGIN(c2_water)
    CALL(evt_hit_damage_return_set, LW(0))
    CALL_CHILD(evt_gazigazi_entry)
    RETURN
EVT_END()

/* the Hollow's cold, the Pirate's Grotto music and its water */
static EVT_BEGIN(dou_room)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG5_DOU1"))
    CALL_CHILD(c2_cold)
    CALL(evt_map_playanim, STRING("nami"), 1, 0)
    CALL(evt_map_playanim, STRING("moku"), 1, 0)
    RETURN
EVT_END()

/* the guards, posted (while the disguise holds): LW(0) name, LW(1..3) place, LW(4) facing, LW(5) slot, LW(6) flag, LW(7) turn ms */
static EVT_BEGIN(c2_post)
    /* a guard beaten stays beaten: he isn't posted again */
    CALL(c2_flag_get, LW(6), LW(8))
    IF_INT_EQUAL(LW(8), 1)
        RETURN
    END_IF
    IF_INT_EQUAL(SLF(F_BULKHEAD), 0)
        CALL(evt_npc_entry, LW(0), STRING("c_heavy"))
        CALL(evt_npc_set_tribe, LW(0), STRING(SJ2_BULKY))
        CALL(evt_npc_set_position, LW(0), LW(1), LW(2), LW(3))
        CALL(evt_npc_set_ry, LW(0), LW(4))
        CALL(evt_npc_set_anim, LW(0), STRING("S_1"))
        CALL(evt_npc_change_interrupt, LW(0), 6, PTR(c2_guard_greet))
        CALL(evt_npc_set_battle_info, LW(0), 0)
        CALL(c2_guard, LW(5), LW(0), LW(6), LW(7))
    END_IF
    RETURN
EVT_END()

static EVT_BEGIN(dou_20_init)
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG5_DOU1"))
    IF_INT_EQUAL(SLF(F_HIZZA), 1)
        SET_VALUE(LW(0), PTR(dou_20_bero_open))
    ELSE
        SET_VALUE(LW(0), PTR(dou_20_bero))
    END_IF
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL_CHILD(dou_room)
    CALL(evt_mobj_save_blk, STRING("mobj_save"), 25, 85, -120, 0, 0)
    /* (the coin block over the lower step, y 40: at 60 it was a wall at Luigi's head) */
    CALL(evt_mobj_badgeblk, STRING("mobj_coin"), 150, 95, 40, ITEM_COIN, 0, SLF(F_D20_COIN), 0)
    /* the gate's two guards on the top step, before the way in (they stood inside
     * the rock past it, out of sight while they talked); stood aside afterwards,
     * facing the way in, so the steps up (a jump each) are behind them */
    SET_VALUE(LW(0), PTR("c2_hg1"))
    SET_VALUE(LW(2), 50)
    SET_VALUE(LW(5), 0)
    SET_VALUE(LW(6), F_GUARD_D20A)
    SET_VALUE(LW(7), 0)
    IF_INT_EQUAL(SLF(F_HOLLOW), 0)
        SET_VALUE(LW(1), 115)
        SET_VALUE(LW(3), -25)
        SET_VALUE(LW(4), 270)
    ELSE
        SET_VALUE(LW(1), D20_ASIDE_X)
        SET_VALUE(LW(3), D20_ASIDE_Z1)
        SET_VALUE(LW(4), 90)
    END_IF
    CALL_CHILD(c2_post)
    SET_VALUE(LW(0), PTR("c2_hg2"))
    SET_VALUE(LW(5), 1)
    SET_VALUE(LW(6), F_GUARD_D20B)
    IF_INT_EQUAL(SLF(F_HOLLOW), 0)
        SET_VALUE(LW(1), 130)
        SET_VALUE(LW(2), 40)
        SET_VALUE(LW(3), 30)
    ELSE
        SET_VALUE(LW(1), D20_ASIDE_X2)
        SET_VALUE(LW(2), 25)
        SET_VALUE(LW(3), D20_ASIDE_Z2)
    END_IF
    CALL_CHILD(c2_post)
    IF_INT_EQUAL(SLF(F_HOLLOW), 0)
        SPAWN_EVENT(dou_20_gate)
    END_IF
    TOUR(dou_20_bero, "")
    RETURN
EVT_END()

/* dou_21, Spring Passage (dou_01): the jump stands */
static BeroEntry dou_21_bero[] = {
    {"w_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_20", "e_bero", 1, 1, 0, 0},
    {"e_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_22", "w_bero_1", 1, 1, 0, 0},
    {0},
};

static EVT_BEGIN(dou_21_chest)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_item_entry, STRING("item_c2"), ITEM_MUSHROOM, 0, -1000, 0, ITEM_ENTRY_FIXED, -1, 0)
    CALL(evt_item_flag_on, STRING("item_c2"), ITEM_FLAG_NO_TOUCH)
    CALL(evt_mobj_wait_animation_end, STRING("box_c2"))
    CALL(evt_item_get_item, STRING("item_c2"))
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(dou_21_init)
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG5_DOU2"))
    CALL(evt_sl_enemy, STRING("c2f_ember21_1"), STRING(SJ2_EMBER), STRING(SJ2_EMBER), -100, 30, 40, 100, 50, STRING("BTLNO_DOU_02_01"))
    SET_VALUE(LW(0), PTR(dou_21_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL_CHILD(dou_room)
    CALL(evt_mobj_jumpstand_blue, 0, STRING("mobj_jump00"), 400, 92, 375, -21, 0, 0)
    CALL(evt_mobj_jumpstand_blue, 0, STRING("mobj_jump01"), 350, -45, 50, -15, 0, 0)
    /* on the bottom floor (it stood at x 400, outside the room), the guard by the way out */
    CALL(evt_mobj_itembox, STRING("box_c2"), -115, 50, -5, 0, 0, PTR(dou_21_chest), SLF(F_D21_CHEST))
    SET_VALUE(LW(0), PTR("c2_hg3"))
    SET_VALUE(LW(1), 90)
    SET_VALUE(LW(2), 0)
    SET_VALUE(LW(3), 40)
    SET_VALUE(LW(4), 270)
    SET_VALUE(LW(5), 0)
    SET_VALUE(LW(6), F_GUARD_D21)
    SET_VALUE(LW(7), 4000)
    CALL_CHILD(c2_post)
    TOUR(dou_21_bero, "")
    RETURN
EVT_END()

/* dou_22, the Spear Run (dou_02): Swim across the fast water */
static BeroEntry dou_22_bero[] = {
    {"w_bero_1", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_21", "e_bero", 1, 1, 0, 0},
    {"e_bero_1", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_23", "w_bero_1", 1, 1, 0, 0},
    {"e_bero_2", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_23", "w_bero_2", 1, 1, 0, 0},
    {0},
};

static EVT_BEGIN(dou_22_init)
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG5_DOU3"))
    CALL(evt_sl_enemy, STRING("c2f_ember22_1"), STRING(SJ2_EMBER), STRING(SJ2_EMBER), -250, 30, 40, 80, 40, STRING("BTLNO_DOU_02_01"))
    CALL(evt_sl_enemy, STRING("c2f_buzzy22_1"), STRING(SJ2_PARABUZZY), STRING(SJ2_PARABUZZY), 250, 60, 40, 80, 40, STRING("BTLNO_DOU_02_02"))
    SET_VALUE(LW(0), PTR(dou_22_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL_CHILD(dou_room)
    SET_VALUE(LW(0), PTR(d22_water))
    CALL_CHILD(c2_water)
    CALL(evt_mobj_badgeblk, STRING("mobj_item"), -25, 85, 100, ITEM_HONEY_SYRUP, 0, SLF(F_D22_ITEM), 0)
    TOUR(dou_22_bero, "a02_fune_ata1,a02_fune_ata2")
    RETURN
EVT_END()

/* dou_23, Lantern Walk (dou_12): the cracked wall's nook */
static BeroEntry dou_23_bero[] = {
    {"w_bero_1", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_22", "e_bero_1", 1, 1, 0, 0},
    {"w_bero_2", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_22", "e_bero_2", 1, 1, 0, 0},
    {"e_bero_1", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_24", "w_bero_1", 1, 1, 0, 0},
    {"e_bero_2", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_24", "w_bero_2", 1, 1, 0, 0},
    {0},
};

#define D23_WALL_X 0
#define D23_WALL_Y 50
#define D23_WALL_Z -135

static EVT_BEGIN(dou_23_nook)
    CALL(evt_npc_set_anim, STRING("c2_crack"), STRING("B_1"))
    CALL(evt_snd_sfxon, STRING("SFX_OFF_WALL_BREAK1"), 0)
    WAIT_MS(700)
    CALL(evt_npc_delete, STRING("c2_crack"))
    SET_VALUE(SLF(F_NOOK), 1)
    CALL(evt_item_entry, STRING("item_piece"), ITEM_STAR_PIECE, D23_WALL_X, D23_WALL_Y, D23_WALL_Z, ITEM_ENTRY_FIXED, SLF(F_D23_PIECE), 0)
    CALL(evt_mobj_badgeblk, STRING("mobj_coin"), D23_WALL_X + 50, D23_WALL_Y + 60, D23_WALL_Z + 30, ITEM_COIN, 0, SLF(F_D23_COIN), 0)
    RETURN
EVT_END()

static EVT_BEGIN(dou_23_init)
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG5_DOU3"))
    SET_VALUE(LW(0), PTR(dou_23_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL_CHILD(dou_room)
    SET_VALUE(LW(0), PTR(d26_water))
    CALL_CHILD(c2_water)
    IF_INT_EQUAL(SLF(F_NOOK), 0)
        CALL(evt_npc_entry, STRING("c2_crack"), STRING("c_sl_ice_wall"))
        CALL(evt_npc_set_position, STRING("c2_crack"), D23_WALL_X, D23_WALL_Y, D23_WALL_Z)
        CALL(evt_npc_set_anim, STRING("c2_crack"), STRING("S_1"))
        CALL(evt_sl_point, PORT_SL_ABILITY_BLAST, 0, D23_WALL_X, D23_WALL_Y, D23_WALL_Z, 60, 0, PTR(dou_23_nook))
    ELSE
        CALL(evt_item_entry, STRING("item_piece"), ITEM_STAR_PIECE, D23_WALL_X, D23_WALL_Y, D23_WALL_Z, ITEM_ENTRY_FIXED, SLF(F_D23_PIECE), 0)
        CALL(evt_mobj_badgeblk, STRING("mobj_coin"), D23_WALL_X + 50, D23_WALL_Y + 60, D23_WALL_Z + 30, ITEM_COIN, 0, SLF(F_D23_COIN), 0)
    END_IF
    TOUR(dou_23_bero, "")
    RETURN
EVT_END()

/* The doors Mario sails through in Boat Mode have no shore: Blooey carries Luigi into them, and
 * the room beyond sets him down (entrance "sl_ride", not in its table). */
#define C2_RIDE(name, x, y, z, map) \
    static EVT_BEGIN(name) \
        CALL(evt_sl_ability_done) \
        CALL(evt_mario_key_onoff, 0) \
        CALL(evt_party_stop, 0) \
        CALL(evt_msg_print, 0, STRING("sl_bl_swim"), 0, STRING("party")) \
        CALL(evt_party_set_pose, 0, STRING("F_1")) \
        BEGIN_INLINE_EVENT \
            CALL(evt_party_move_pos, 0, x, z, FLOAT(300.0f)) \
        END_INLINE_EVENT \
        CALL(evt_mario_jump_pos, x, y, z, 1800, 0, 30) \
        CALL(evt_bero_mapchange, STRING(map), STRING("sl_ride")) \
        RETURN \
    EVT_END()

/* dou_24, the Falls Gate (dou_03): the frozen winch and the water gate */
static BeroEntry dou_24_bero[] = {
    {"w_bero_1", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_23", "e_bero_1", 1, 1, 0, 0},
    {"w_bero_2", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_23", "e_bero_2", 1, 1, 0, 0},
    {"w_bero_3", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_25", "e_bero", 1, 1, 0, 0},
    {"e_bero_1", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, NULL, NULL, 1, 1, 0, 0},
    {"e_bero_2", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, NULL, NULL, 1, 1, 0, 0},
    {"e_bero_3", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, NULL, NULL, 1, 1, 0, 0},
    {"s_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_26", "sl_ride", 1, 1, 0, 0},
    {0},
};
static BeroEntry dou_24_bero_after[] = {
    {"w_bero_1", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_23", "e_bero_1", 1, 1, 0, 0},
    {"w_bero_2", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_23", "e_bero_2", 1, 1, 0, 0},
    {"w_bero_3", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_25", "e_bero", 1, 1, 0, 0},
    {"e_bero_1", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_28", "w_bero_2", 1, 1, 0, 0},
    {"e_bero_2", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_28", "w_bero_2", 1, 1, 0, 0},
    {"e_bero_3", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_28", "w_bero_3", 1, 1, 0, 0},
    {"s_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_26", "sl_ride", 1, 1, 0, 0},
    {0},
};

#define D24_WINCH_X 200
#define D24_WINCH_Z -26

/* the gate goes up (the borrowed map's own gate) */
static EVT_BEGIN(dou_24_gate_up)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_SINGLE, 0, STRING("S_reba"), MAPOBJ_FLAG_HIDE)
    CALL(evt_hitobj_onoff, STRING("A_reba"), 1, 1)
    CALL(evt_snd_sfxon, STRING("SFX_STG5_FLOODGATE_SHAKE1"), LW(10))
    CALL(evt_cam_shake, 4, FLOAT(0.01f), FLOAT(0.0f), 500)
    WAIT_MS(500)
    CALL(evt_snd_sfxoff, LW(10))
    CALL(evt_cam3d_evt_set, 98, 151, 563, 98, 56, 21, 500, 11)
    WAIT_MS(800)
    CALL(evt_mapobj_get_position, STRING("s03_mon"), LW(0), LW(1), LW(2))
    CALL(evt_snd_sfxon_3d, STRING("SFX_STG5_FLOODGATE_UP1"), LW(0), LW(1), LW(2), LW(9))
    CALL(evt_map_playanim, STRING("mae"), 1, 0)
    CALL(evt_map_playanim, STRING("mon"), 0, 0)
    CALL(evt_map_checkanim, STRING("mon"), LW(0), LW(1))
    WAIT_MS(LW(1))
    CALL(evt_map_pauseanim, 0, STRING("mae"))
    CALL(evt_snd_sfxoff, LW(9))
    CALL(evt_mapobj_get_position, STRING("s03_mon"), LW(0), LW(1), LW(2))
    CALL(evt_snd_sfxon_3d, STRING("SFX_STG5_FLOODGATE_UP2"), LW(0), LW(1), LW(2), 0)
    WAIT_MS(1200)
    CALL(evt_cam3d_evt_off, 300, 11)
    /* the way on, shut till now, as dou_03 has it */
    SET_VALUE(LW(0), STRING("s_bero"))
    CALL_CHILD(bero_case_switch_on)
    CALL(evt_hitobj_attr_onoff, 1, 1, STRING("s_bero"), 128)
    SET_VALUE(SLF(F_GATE_UP), 1)
    RETURN
EVT_END()

static EVT_BEGIN(dou_24_winch)
    CALL(evt_mario_key_onoff, 0)
    IF_INT_EQUAL(SLF(F_WINCH), 0)
        CALL(evt_msg_print, 0, STRING("sl_c2_winch_00"), 0, STRING("mario"))
    ELSE
        IF_INT_EQUAL(SLF(F_BOOTS), 0)
            /* the Upper Pool first: the boots come before Bulkhead, as the plan has them */
            CALL(evt_msg_print, 0, STRING("sl_c2_winch_02"), 0, STRING("mario"))
        ELSE
            IF_INT_EQUAL(SLF(F_GATE_UP), 0)
                CALL(evt_msg_print, 0, STRING("sl_c2_winch_01"), 0, STRING("mario"))
                CALL_CHILD(dou_24_gate_up)
            END_IF
        END_IF
    END_IF
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(dou_24_winch_blast)
    CALL(evt_npc_set_anim, STRING("c2_winch_ice"), STRING("B_1"))
    CALL(evt_snd_sfxon, STRING("SFX_OFF_WALL_BREAK1"), 0)
    WAIT_MS(700)
    CALL(evt_npc_delete, STRING("c2_winch_ice"))
    SET_VALUE(SLF(F_WINCH), 1)
    RETURN
EVT_END()

C2_RIDE(dou_24_ride_in, 100, 40, 130, "dou_26")

/* Swim at the bank by the gate: through it, once it's up */
static EVT_BEGIN(dou_24_gate_ride)
    IF_INT_EQUAL(SLF(F_GATE_UP), 0)
        CALL(evt_sl_ability_done)
        CALL(evt_mario_key_onoff, 0)
        CALL(evt_msg_print, 0, STRING("sl_c2_gate_shut"), 0, STRING("party"))
        CALL(evt_mario_key_onoff, 1)
        RETURN
    END_IF
    CALL_CHILD(dou_24_ride_in)
    RETURN
EVT_END()

static EVT_BEGIN(dou_24_init)
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG5_DOU4"))
    IF_INT_EQUAL(SLF(F_BULKHEAD), 1)
        SET_VALUE(LW(0), PTR(dou_24_bero_after))
    ELSE
        SET_VALUE(LW(0), PTR(dou_24_bero))
    END_IF
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL_CHILD(dou_room)
    SET_VALUE(LW(0), PTR(d24_water))
    CALL_CHILD(c2_water)
    CALL(evt_map_playanim, STRING("tubo"), 1, 0)
    CALL(evt_mobj_kururing_floor, STRING("kururin1"), 450, 275, -550, STRING("s03_iwadan2"), 125, SLF(F_PANEL25))
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("S_bin"), MAPOBJ_FLAG_HIDE)
    IF_INT_EQUAL(SLF(F_GATE_UP), 1)
        CALL(evt_mapobj_flag_onoff, MAPOBJ_SINGLE, 0, STRING("S_reba"), MAPOBJ_FLAG_HIDE)
        CALL(evt_hitobj_onoff, STRING("A_reba"), 1, 1)
        CALL(evt_map_playanim, STRING("mon_up"), 0, 0)
    ELSE
        CALL(evt_hitobj_onoff, STRING("on_off"), 1, 0)
        SET_VALUE(LW(0), STRING("s_bero"))
        CALL_CHILD(bero_case_switch_off)
        CALL(evt_hitobj_attr_onoff, 1, 0, STRING("s_bero"), 128)
        CALL(evt_mapobj_flag_onoff, MAPOBJ_SINGLE, 1, STRING("S_reba"), MAPOBJ_FLAG_HIDE)
        CALL(evt_hitobj_onoff, STRING("A_reba"), 1, 0)
        CALL(evt_run_case_evt, CASE_COND_FRONT_A, 1, STRING("A_makimaki"), 0, PTR(dou_24_winch), 0)
        IF_INT_EQUAL(SLF(F_WINCH), 0)
            CALL(evt_npc_entry, STRING("c2_winch_ice"), STRING("c_sl_ice_wall"))
            CALL(evt_npc_set_position, STRING("c2_winch_ice"), D24_WINCH_X, 50, D24_WINCH_Z + 25)
            CALL(evt_npc_set_anim, STRING("c2_winch_ice"), STRING("S_1"))
            CALL(evt_sl_point, PORT_SL_ABILITY_BLAST, 0, D24_WINCH_X, 50, D24_WINCH_Z + 25, 60, 0, PTR(dou_24_winch_blast))
        END_IF
    END_IF
    /* Blooey up the falls to the upper pool's ledge (dou_25's door): the only way up before Bulkhead */
    CALL(evt_sl_swim_point, 0, -450, 50, -100, -450, 275, -440)
    /* and over the gate's channel (or the barrel in it, by jumps), and through the gate */
    CALL(evt_sl_swim_point, 1, 35, 50, 10, 170, 50, 20)
    CALL(evt_sl_point, PORT_SL_ABILITY_SWIM, 2, 30, 25, 125, 60, 0, PTR(dou_24_gate_ride))
    /* and from the west shelf to the middle: past a jump's reach, Mario sails this water */
    CALL(evt_sl_swim_point, 3, -215, 17, -20, -125, 25, -20)
    /* back from the Bobbing Hall */
    CALL(evt_bero_get_entername, LW(0))
    IF_STR_EQUAL(LW(0), STRING("sl_ride"))
        CALL(evt_mario_set_pos, 30, 25, 115)
        CALL(evt_mario_set_dir, 270, 0, 0)
    END_IF
    /* on the east bank, far enough back that they don't fill the camera's foreground */
    SET_VALUE(LW(0), PTR("c2_hg4"))
    SET_VALUE(LW(1), 300)
    SET_VALUE(LW(2), 25)
    SET_VALUE(LW(3), 80)
    SET_VALUE(LW(4), 270)
    SET_VALUE(LW(5), 0)
    SET_VALUE(LW(6), F_GUARD_D24A)
    SET_VALUE(LW(7), 0)
    CALL_CHILD(c2_post)
    SET_VALUE(LW(0), PTR("c2_hg5"))
    SET_VALUE(LW(1), 370)
    SET_VALUE(LW(3), 15)
    SET_VALUE(LW(5), 1)
    SET_VALUE(LW(6), F_GUARD_D24B)
    CALL_CHILD(c2_post)
    TOUR(dou_24_bero, "A_makimaki,on_off,A_reba")
    RETURN
EVT_END()

/* dou_25, the Upper Pool (dou_04): the Super Boots */
static BeroEntry dou_25_bero[] = {
    {"e_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_24", "w_bero_3", 1, 1, 0, 0},
    {0},
};

static EVT_BEGIN(dou_25_boots)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_mobj_wait_animation_end, STRING("box_boots"))
    CALL(c2_give, ITEM_SUPER_BOOTS)
    CALL(evt_snd_bgmon, PSND_CH(1), STRING("BGM_FF_GET_ITEM1"))
    CALL(evt_mario_set_pose, STRING("M_I_2"))
    CALL(evt_msg_print, 0, STRING("sl_c2_boots_00"), 0, 0)
    CALL(evt_mario_set_pose, STRING("M_S_1"))
    CALL(evt_msg_print, 0, STRING("sl_c2_boots_01"), 0, STRING("mario"))
    SET_VALUE(SLF(F_BOOTS), 1)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(dou_25_badge)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_item_entry, STRING("item_c2"), ITEM_POWER_BOUNCE, 0, -1000, 0, ITEM_ENTRY_FIXED, -1, 0)
    CALL(evt_item_flag_on, STRING("item_c2"), ITEM_FLAG_NO_TOUCH)
    CALL(evt_mobj_wait_animation_end, STRING("box_c2"))
    CALL(evt_item_get_item, STRING("item_c2"))
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(dou_25_init)
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG5_DOU6"))
    SET_VALUE(LW(0), PTR(dou_25_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL_CHILD(dou_room)
    SET_VALUE(LW(0), PTR(d25_water))
    CALL_CHILD(c2_water)
    CALL(evt_mobj_itembox, STRING("box_boots"), -145, 10, -75, 1, 0, PTR(dou_25_boots), SLF(F_BOOTS))
    CALL(evt_mobj_itembox, STRING("box_c2"), 150, 10, -85, 0, 0, PTR(dou_25_badge), SLF(F_D25_BADGE))
    TOUR(dou_25_bero, "")
    RETURN
EVT_END()

/* dou_26, the Bobbing Hall (dou_09): Swim across; the side wall (Blast) to Sidewind Passage */
static BeroEntry dou_26_bero[] = {
    {"n_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_24", "sl_ride", 1, 1, 0, 0},
    {"e_bero_1", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_27", "w_bero_1", 1, 1, 0, 0},
    {"e_bero_2", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_27", "w_bero_2", 1, 1, 0, 0},
    {"w_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_30", "e_bero_1", 1, 1, 0, 0},
    {0},
};

/* the bomb wall to Sidewind Passage (the borrowed map's own, as Bobbery blows it) */
static EVT_BEGIN(dou_26_wall_blast)
    CALL(evt_mapobj_get_position, STRING("S_ki"), LW(0), LW(1), LW(2))
    CALL(evt_snd_sfxon_3d, STRING("SFX_OFF_WALL_BREAK1"), LW(0), LW(1), LW(2), 0)
    CALL(evt_eff, 0, STRING("kemuri_test"), 4, LW(0), LW(1), LW(2), FLOAT(2.0f), 0, 0, 0, 0, 0, 0, 0)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("S_ki"), MAPOBJ_FLAG_HIDE)
    CALL(evt_hitobj_onoff, STRING("A_ki1"), 1, 0)
    SET_VALUE(LW(0), STRING("w_bero"))
    CALL_CHILD(bero_case_switch_on)
    CALL(evt_hitobj_attr_onoff, 1, 1, STRING("w_bero"), 4)
    SET_VALUE(SLF(F_SIDEWALL), 1)
    RETURN
EVT_END()

static EVT_BEGIN(dou_26_alcove)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_item_entry, STRING("item_c2"), ITEM_PRETTY_LUCKY, 0, -1000, 0, ITEM_ENTRY_FIXED, -1, 0)
    CALL(evt_item_flag_on, STRING("item_c2"), ITEM_FLAG_NO_TOUCH)
    CALL(evt_mobj_wait_animation_end, STRING("box_c2"))
    CALL(evt_item_get_item, STRING("item_c2"))
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

C2_RIDE(dou_26_ride_back, 0, 40, -170, "dou_24")

static EVT_BEGIN(dou_26_init)
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG5_DOU11"))
    /* over the water east of the landing: at x 0 the first came down on Luigi as Blooey set him down */
    CALL(evt_sl_enemy, STRING("c2f_buzzy26_1"), STRING(SJ2_PARABUZZY), STRING(SJ2_PARABUZZY), 180, 80, 0, 60, 50, STRING("BTLNO_DOU_09_01"))
    CALL(evt_sl_enemy, STRING("c2f_buzzy26_2"), STRING(SJ2_PARABUZZY), STRING(SJ2_PARABUZZY), 330, 80, 0, 60, 50, STRING("BTLNO_DOU_09_01"))
    CALL(evt_sl_enemy, STRING("c2f_ember26_1"), STRING(SJ2_EMBER), STRING(SJ2_EMBER), -300, 30, 40, 80, 40, STRING("BTLNO_DOU_09_02"))
    SET_VALUE(LW(0), PTR(dou_26_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL_CHILD(dou_room)
    SET_VALUE(LW(0), PTR(d26_water))
    CALL_CHILD(c2_water)
    IF_INT_EQUAL(SLF(F_SIDEWALL), 0)
        SET_VALUE(LW(0), STRING("w_bero"))
        CALL_CHILD(bero_case_switch_off)
        CALL(evt_hitobj_attr_onoff, 1, 0, STRING("w_bero"), 4)
        CALL(c2_hit_pos, STRING("A_ki1"), LW(0), LW(1), LW(2))
        CALL(evt_sl_point, PORT_SL_ABILITY_BLAST, 0, LW(0), LW(1), LW(2), 90, 0, PTR(dou_26_wall_blast))
    ELSE
        CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("S_ki"), MAPOBJ_FLAG_HIDE)
        CALL(evt_hitobj_onoff, STRING("A_ki1"), 1, 0)
    END_IF
    /* set down on the west island; from its east tip to the far dock, from its west end back through the gate */
    CALL(evt_sl_swim_point, 0, -65, 25, 0, 500, 50, -60)
    CALL(evt_sl_point, PORT_SL_ABILITY_SWIM, 1, -210, 22, 0, 45, 0, PTR(dou_26_ride_back))
    CALL(evt_bero_get_entername, LW(0))
    IF_STR_EQUAL(LW(0), STRING("sl_ride"))
        CALL(evt_mario_set_pos, -90, 25, 0)
        CALL(evt_mario_set_dir, 90, 0, 0)
    END_IF
    CALL(evt_mobj_itembox, STRING("box_c2"), -175, 22, -20, 0, 0, PTR(dou_26_alcove), SLF(F_D26_BADGE))
    TOUR(dou_26_bero, "A_ki1,A_taru01,a09_fune_ata1")
    RETURN
EVT_END()

/* dou_27, the Wave Run (dou_10) */
static BeroEntry dou_27_bero[] = {
    {"w_bero_1", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_26", "e_bero_1", 1, 1, 0, 0},
    {"w_bero_2", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_26", "e_bero_2", 1, 1, 0, 0},
    {"n_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, NULL, NULL, 1, 1, 0, 0},
    {"e_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_28", "w_bero_1", 1, 1, 0, 0},
    {0},
};

/* Blooey carries Luigi down the Wave Run to the far door: no shore there to set him on (the dock
 * lies past the exit), so the ride ends in the door. */
static EVT_BEGIN(dou_27_ride)
    CALL(evt_sl_ability_done)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_party_stop, 0)
    CALL(evt_msg_print, 0, STRING("sl_bl_swim"), 0, STRING("party"))
    CALL(evt_party_set_pose, 0, STRING("F_1"))
    BEGIN_INLINE_EVENT
        CALL(evt_party_move_pos, 0, 1200, 0, FLOAT(400.0f))
    END_INLINE_EVENT
    CALL(evt_mario_jump_pos, 1235, 40, 0, 3400, 0, 30)
    CALL(evt_bero_mapchange, STRING("dou_28"), STRING("w_bero_1"))
    RETURN
EVT_END()

static EVT_BEGIN(dou_27_init)
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG5_DOU10"))
    CALL(evt_sl_enemy, STRING("c2f_ember27_1"), STRING(SJ2_EMBER), STRING(SJ2_EMBER), 0, 30, 60, 100, 40, STRING("BTLNO_DOU_09_02"))
    SET_VALUE(LW(0), PTR(dou_27_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL_CHILD(dou_room)
    SET_VALUE(LW(0), PTR(d27_water))
    CALL_CHILD(c2_water)
    CALL(evt_map_playanim, STRING("fune_02"), 1, 0)
    CALL(evt_mobj_badgeblk, STRING("mobj_coin1"), -430, 110, -80, ITEM_COIN, 0, SLF(F_D27_COIN1), 0)
    CALL(evt_mobj_badgeblk, STRING("mobj_coin2"), -300, 110, -80, ITEM_COIN, 0, SLF(F_D27_COIN2), 0)
    CALL(evt_mobj_badgeblk, STRING("mobj_coin3"), -170, 110, -80, ITEM_COIN, 0, SLF(F_D27_COIN3), 0)
    CALL(evt_sl_point, PORT_SL_ABILITY_SWIM, 0, -10, 50, -100, 70, 0, PTR(dou_27_ride))
    /* back from the Guard Room: set down at the walkway's end, as the ride started */
    CALL(evt_bero_get_entername, LW(0))
    IF_STR_EQUAL(LW(0), STRING("sl_back"))
        CALL(evt_mario_set_pos, -20, 50, -100)
        CALL(evt_mario_set_dir, 270, 0, 0)
    END_IF
    /* at the walkway's two ends, looking along it: the first coin block and the last in sight, the middle free */
    SET_VALUE(LW(0), PTR("c2_hg6"))
    SET_VALUE(LW(1), -560)
    SET_VALUE(LW(2), 50)
    SET_VALUE(LW(3), -140)
    SET_VALUE(LW(4), 90)
    SET_VALUE(LW(5), 0)
    SET_VALUE(LW(6), F_GUARD_D27A)
    SET_VALUE(LW(7), 0)
    CALL_CHILD(c2_post)
    SET_VALUE(LW(0), PTR("c2_hg7"))
    SET_VALUE(LW(1), -60)
    SET_VALUE(LW(4), 270)
    SET_VALUE(LW(5), 1)
    SET_VALUE(LW(6), F_GUARD_D27B)
    CALL_CHILD(c2_post)
    TOUR(dou_27_bero, "")
    RETURN
EVT_END()

/* dou_28, the Guard Room (dou_05): Sergeant Bulkhead */
static BeroEntry dou_28_bero[] = {
    {"w_bero_1", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_27", "sl_back", 1, 1, 0, 0},
    {"w_bero_2", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, NULL, NULL, 1, 1, 0, 0},
    {"w_bero_3", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, NULL, NULL, 1, 1, 0, 0},
    {"e_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_29", "sl_ride", 1, 1, 0, 0},
    {0},
};
static BeroEntry dou_28_bero_after[] = {
    {"w_bero_1", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_27", "sl_back", 1, 1, 0, 0},
    {"w_bero_2", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_24", "e_bero_2", 1, 1, 0, 0},
    {"w_bero_3", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_24", "e_bero_3", 1, 1, 0, 0},
    {"e_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_29", "sl_ride", 1, 1, 0, 0},
    {0},
};

#define D28_BRIDGE_X 100
#define D28_BRIDGE_Z 0

static EVT_BEGIN(dou_28_bulkhead)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_DANGER2"))
    CALL(evt_mario_set_dir_npc, STRING("c2_bulkhead"))
    CALL(evt_msg_print, 0, STRING("sl_c2_bulkhead_00"), 0, STRING("c2_bulkhead"))
    CALL(evt_msg_print, 0, STRING("sl_c2_bulkhead_01"), 0, STRING("c2_bulkhead"))
    /* the twirl: the veil lifts, the moustache in full view */
    CALL(evt_luigi_story_pose, STRING("TW_1"))
    WAIT_FRAMES(48)
    CALL(evt_luigi_story_pose, 0)
    CALL(evt_msg_print, 0, STRING("sl_c2_bulkhead_02"), 0, STRING("c2_bulkhead"))
    CALL(evt_msg_print, 0, STRING("sl_c2_bulkhead_03"), 0, STRING("mario"))
    CALL(evt_cam_shake, 4, FLOAT(0.02f), FLOAT(0.0f), 400)
    CALL(evt_msg_print, 0, STRING("sl_c2_bulkhead_04"), 0, STRING("c2_bulkhead"))
    CALL(evt_sl_c2_arm, 1)
    CALL(evt_mario_key_onoff, 1)
    CALL(evt_npc_set_battle_info, STRING("c2_bulkhead"), 10)
    CALL(evt_npc_battle_start, STRING("c2_bulkhead"))
    CALL(evt_npc_wait_battle_end)
    CALL(evt_sl_c2_arm, 0)
    CALL(evt_npc_get_battle_result, LW(0))
    IF_INT_NOT_EQUAL(LW(0), 1)
        RETURN
    END_IF
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_npc_delete, STRING("c2_bulkhead"))
    CALL(evt_npc_delete, STRING("c2_hga"))
    CALL(evt_npc_delete, STRING("c2_hgb"))
    SET_VALUE(SLF(F_BULKHEAD), 1)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG5_DOU1"))
    CALL(evt_msg_print, 0, STRING("sl_c2_bulkhead_05"), 0, STRING("party"))
    CALL(evt_mobj_badgeblk, STRING("mobj_item"), D28_BRIDGE_X, 60, D28_BRIDGE_Z, ITEM_SUPER_SHROOM, 0, SLF(F_D28_ITEM), 0)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(dou_28_watch)
    LOOP(0)
        CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
        IF_INT_GREATER(LW(0), D28_BRIDGE_X - 160)
            BREAK_LOOP
        END_IF
        WAIT_FRAMES(4)
    END_LOOP
    CALL_CHILD(dou_28_bulkhead)
    RETURN
EVT_END()

static EVT_BEGIN(dou_28_init)
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG5_DOU5"))
    IF_INT_EQUAL(SLF(F_BULKHEAD), 1)
        SET_VALUE(LW(0), PTR(dou_28_bero_after))
    ELSE
        SET_VALUE(LW(0), PTR(dou_28_bero))
    END_IF
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL_CHILD(dou_room)
    SET_VALUE(LW(0), PTR(d28_water))
    CALL_CHILD(c2_water)
    CALL(evt_map_playanim, STRING("tubo"), 1, 0)
    IF_INT_EQUAL(SLF(F_BULKHEAD), 0)
        CALL(evt_npc_entry, STRING("c2_bulkhead"), STRING("c_heavy"))
        CALL(evt_npc_set_tribe, STRING("c2_bulkhead"), STRING(SJ2_BULKY))
        CALL(evt_npc_set_position, STRING("c2_bulkhead"), D28_BRIDGE_X, 0, D28_BRIDGE_Z)
        CALL(evt_npc_set_scale, STRING("c2_bulkhead"), FLOAT(1.4f), FLOAT(1.4f), FLOAT(1.4f))
        /* (the sergeant's stripe: c_sl_bulkhead from the models work, when it lands) */
        CALL(c2_has_model, STRING("c_sl_bulkhead"), LW(0))
        IF_INT_EQUAL(LW(0), 1)
            CALL(evt_npc_delete, STRING("c2_bulkhead"))
            CALL(evt_npc_entry, STRING("c2_bulkhead"), STRING("c_sl_bulkhead"))
            CALL(evt_npc_set_tribe, STRING("c2_bulkhead"), STRING(SJ2_BULKY))
            CALL(evt_npc_set_position, STRING("c2_bulkhead"), D28_BRIDGE_X, 0, D28_BRIDGE_Z)
        END_IF
        CALL(evt_npc_set_ry, STRING("c2_bulkhead"), 270)
        CALL(evt_npc_set_anim, STRING("c2_bulkhead"), STRING("S_1"))
        CALL(evt_npc_entry, STRING("c2_hga"), STRING("c_heavy"))
        CALL(evt_npc_set_position, STRING("c2_hga"), D28_BRIDGE_X + 50, 0, D28_BRIDGE_Z - 40)
        CALL(evt_npc_entry, STRING("c2_hgb"), STRING("c_heavy"))
        CALL(evt_npc_set_position, STRING("c2_hgb"), D28_BRIDGE_X + 50, 0, D28_BRIDGE_Z + 40)
        CALL(evt_npc_set_anim, STRING("c2_hga"), STRING("S_1"))
        CALL(evt_npc_set_anim, STRING("c2_hgb"), STRING("S_1"))
        SPAWN_EVENT(dou_28_watch)
    ELSE
        CALL(evt_mobj_badgeblk, STRING("mobj_item"), D28_BRIDGE_X, 60, D28_BRIDGE_Z, ITEM_SUPER_SHROOM, 0, SLF(F_D28_ITEM), 0)
    END_IF
    TOUR(dou_28_bero, "mizu_atari")
    RETURN
EVT_END()

/* dou_29, the Grand Grotto (dou_11): the islands and the bomb wall to the Den */
static BeroEntry dou_29_bero[] = {
    {"w_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_28", "e_bero", 1, 1, 0, 0},
    {"e_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, NULL, NULL, 1, 1, 0, 0},
    {"n_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "gon_20", "w_bero", 1, 1, 0, 0},
    {0},
};

static EVT_BEGIN(dou_29_wall_blast)
    CALL(evt_mapobj_get_position, STRING("S_ana_before"), LW(0), LW(1), LW(2))
    CALL(evt_snd_sfxon_3d, STRING("SFX_OFF_WALL_BREAK1"), LW(0), LW(1), LW(2), 0)
    CALL(evt_eff, 0, STRING("kemuri_test"), 4, LW(0), LW(1), LW(2), FLOAT(2.5f), 0, 0, 0, 0, 0, 0, 0)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("S_ana_before1"), MAPOBJ_FLAG_HIDE)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("S_ana_before"), MAPOBJ_FLAG_HIDE)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("S_hibimae"), MAPOBJ_FLAG_HIDE)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 0, STRING("S_hibiato"), MAPOBJ_FLAG_HIDE)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 0, STRING("S_ana_after"), MAPOBJ_FLAG_HIDE)
    SET_VALUE(LW(0), STRING("n_bero"))
    CALL_CHILD(bero_case_switch_on)
    CALL(evt_hitobj_attr_onoff, 1, 1, STRING("n_bero"), 4)
    SET_VALUE(SLF(F_WALL29), 1)
    RETURN
EVT_END()

static EVT_BEGIN(dou_29_before)
    CALL(evt_mario_key_onoff, 0)
    CALL_CHILD(c2_cast_in)
    CALL(evt_msg_print, 0, STRING("sl_c2_den_00"), 0, LW(11))
    CALL(evt_msg_print, 0, STRING("sl_c2_den_01"), 0, STRING("mario"))
    CALL_CHILD(c2_cast_out)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(dou_29_watch)
    LOOP(0)
        CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
        IF_INT_GREATER(LW(0), 640)
            BREAK_LOOP
        END_IF
        WAIT_FRAMES(8)
    END_LOOP
    CALL_CHILD(dou_29_before)
    RETURN
EVT_END()

static EVT_BEGIN(dou_29_init)
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG5_DOU12"))
    SET_VALUE(LW(0), PTR(dou_29_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL_CHILD(dou_room)
    SET_VALUE(LW(0), PTR(d29_water))
    CALL_CHILD(c2_water)
    CALL(evt_snd_bgmoff, PSND_FADE_DIP_TO(60))
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("S_ana_after"), MAPOBJ_FLAG_HIDE)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("S_ana_before"), MAPOBJ_FLAG_HIDE)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("S_ana_before1"), MAPOBJ_FLAG_HIDE)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("S_hibiato"), MAPOBJ_FLAG_HIDE)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("S_hibimae"), MAPOBJ_FLAG_HIDE)
    IF_INT_EQUAL(SLF(F_WALL29), 0)
        CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 0, STRING("S_hibimae"), MAPOBJ_FLAG_HIDE)
        CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 0, STRING("S_ana_before"), MAPOBJ_FLAG_HIDE)
        CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 0, STRING("S_ana_before1"), MAPOBJ_FLAG_HIDE)
        SET_VALUE(LW(0), STRING("n_bero"))
        CALL_CHILD(bero_case_switch_off)
        CALL(evt_hitobj_attr_onoff, 1, 0, STRING("n_bero"), 4)
        CALL(c2_hit_pos, STRING("n_bero"), LW(0), LW(1), LW(2))
        /* (wide: from the shelf before the wall Jerry can't follow, and goes off on the ledge below) */
        CALL(evt_sl_point, PORT_SL_ABILITY_BLAST, 0, LW(0), LW(1), LW(2), 130, 0, PTR(dou_29_wall_blast))
        SPAWN_EVENT(dou_29_watch)
    ELSE
        CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 0, STRING("S_hibiato"), MAPOBJ_FLAG_HIDE)
        CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 0, STRING("S_ana_after"), MAPOBJ_FLAG_HIDE)
    END_IF
    /* the classic pair before the boss */
    CALL(evt_mobj_save_blk, STRING("mobj_save"), 700, 110, 60, 0, 0)
    CALL(evt_mobj_recovery_blk, STRING("mobj_heart"), 0, 790, 110, 60, 0, 0)
    /* (a block throws its item about 100 to the camera's left or right: at x 720 the west throw went over the
     * island's edge at 630 into the water; at 800 the floor runs 660-940 either way) */
    CALL(evt_mobj_brick, STRING("mobj_hidden"), 800, 110, -30, ITEM_STAR_PIECE, 11, 0, SLF(F_D29_PIECE))
    /* its west door has no shore (Mario sails in): the Guard Room's way on sets Luigi on the first island */
    CALL(evt_sl_swim_point, 0, -300, 50, 0, 680, 50, 0)
    CALL(evt_bero_get_entername, LW(0))
    IF_STR_EQUAL(LW(0), STRING("sl_ride"))
        CALL(evt_mario_set_pos, -300, 50, 0)
        CALL(evt_mario_set_dir, 90, 0, 0)
    END_IF
    TOUR(dou_29_bero, "n_bero")
    RETURN
EVT_END()

/* dou_30, Sidewind Passage (dou_13), optional */
static BeroEntry dou_30_bero[] = {
    {"w_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, NULL, NULL, 1, 1, 0, 0},
    {"e_bero_1", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_26", "w_bero", 1, 1, 0, 0},
    {0},
};

static EVT_BEGIN(dou_30_badge)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_item_entry, STRING("item_c2"), ITEM_SOFT_STOMP, 0, -1000, 0, ITEM_ENTRY_FIXED, -1, 0)
    CALL(evt_item_flag_on, STRING("item_c2"), ITEM_FLAG_NO_TOUCH)
    CALL(evt_mobj_wait_animation_end, STRING("box_c2"))
    CALL(evt_item_get_item, STRING("item_c2"))
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(dou_30_init)
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG5_DOU9"))
    CALL(evt_sl_enemy, STRING("c2f_buzzy30_1"), STRING(SJ2_PARABUZZY), STRING(SJ2_PARABUZZY), 0, 60, 0, 100, 40, STRING("BTLNO_DOU_09_01"))
    SET_VALUE(LW(0), PTR(dou_30_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL_CHILD(dou_room)
    CALL(evt_mobj_powerupblk, STRING("pblk_c2"), 100, 60, 0, 0, SLF(F_D30_SHINE), 0)
    CALL(evt_mobj_itembox, STRING("box_c2"), -400, 50, -10, 0, 0, PTR(dou_30_badge), SLF(F_D30_BADGE))
    TOUR(dou_30_bero, "")
    RETURN
EVT_END()

/* ======================================================================
 * gon_20, Hizza's Den (gon_11)
 * ====================================================================== */

static BeroEntry gon_20_bero[] = {
    {"w_bero", 0, 0, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "dou_29", "n_bero", 1, 1, 0, 0},
    {0},
};

#define G20_SNAKE_X 60
#define G20_SNAKE_Z -40
#define G20_CAGE_X 300
#define G20_CAGE_Z -150

static EVT_BEGIN(gon_20_after)
    CALL(evt_mario_key_onoff, 0)
    /* smoke clears; a golden curved plate rings on the floor */
    CALL(evt_npc_delete, STRING("c2_hizza"))
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_HAPPY1"))
    CALL(evt_item_entry, STRING("c2_piece"), ITEM_GOLD_RING, G20_SNAKE_X, 0, G20_SNAKE_Z, ITEM_ENTRY_FIXED_NO_TOUCH, -1, 0)
    CALL(evt_snd_sfxon, STRING("SFX_COIN_LEAPED1"), 0)
    WAIT_MS(500)
    CALL(evt_luigi_story_pose, STRING("FA_1"))
    WAIT_MS(300)
    CALL(evt_luigi_story_pose, 0)
    CALL_CHILD(c2_cast_in)
    CALL(evt_msg_print, 0, STRING("sl_c2_after_00"), 0, STRING("mario"))
    CALL(evt_msg_print, 0, STRING("sl_c2_after_01"), 0, LW(11))
    CALL(evt_msg_print, 0, STRING("sl_c2_after_02"), 0, STRING("mario"))
    CALL(evt_msg_print, 0, STRING("sl_c2_after_03"), 0, LW(10))
    CALL(evt_msg_print, 0, STRING("sl_c2_after_04"), 0, STRING("mario"))
    CALL_CHILD(c2_cast_out)
    CALL(evt_mario_mov_pos2, G20_SNAKE_X - 20, G20_SNAKE_Z, FLOAT(60.0f))
    CALL(evt_item_delete, STRING("c2_piece"))
    SET_VALUE(LW(0), PTR("sl_c2_after_05"))
    CALL_CHILD(c2_got)
    /* the brides' cage opens */
    CALL(c2_anim, STRING("c2_cage"), STRING("O_1"))
    WAIT_FRAMES(36)
    SET_VALUE(SLF(F_BRIDES), 1)
    CALL(evt_msg_print, 0, STRING("sl_c2_after_06"), 0, STRING("c2_bride1"))
    CALL(evt_msg_print, 0, STRING("sl_c2_after_07"), 0, STRING("c2_bride2"))
    CALL(evt_msg_print, 0, STRING("sl_c2_after_08"), 0, STRING("c2_bride3"))
    /* the brides are led home: night falls on the village */
    CALL(evt_fade_set_mapchange_type, 0, FADE_OUT_BLACK, 1000, FADE_IN_BLACK, 1000)
    CALL(evt_bero_mapchange, STRING("bom_23"), STRING("e_bero"))
    RETURN
EVT_END()

static EVT_BEGIN(gon_20_den)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_seq_wait, 2)
    WAIT_MS(500)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_DANGER3"))
    CALL(evt_mario_mov_pos2, G20_SNAKE_X - 130, G20_SNAKE_Z + 40, FLOAT(50.0f))
    CALL(evt_mario_set_dir, 90, 0, 0)
    /* framed for the cut snake: the tail's head stands about 138 up at the field's scale 2 */
    CALL(evt_cam3d_evt_set, G20_SNAKE_X - 50, 150, G20_SNAKE_Z + 520, G20_SNAKE_X - 50, 72, G20_SNAKE_Z, 800, 11)
    CALL(evt_npc_set_anim, STRING("c2_hizza"), STRING("U_1"))
    CALL(evt_cam_shake, 4, FLOAT(0.02f), FLOAT(0.0f), 1000)
    WAIT_MS(1000)
    CALL(evt_npc_set_anim, STRING("c2_hizza"), STRING("T_1"))
    CALL(evt_msg_print, 0, STRING("sl_c2_den_02"), 0, STRING("c2_hizza"))
    CALL(evt_npc_set_anim, STRING("c2_hizza"), STRING("T_2"))
    CALL(evt_msg_print, 0, STRING("sl_c2_den_03"), 0, STRING("c2_hizza"))
    CALL(evt_npc_set_anim, STRING("c2_hizza"), STRING("T_1"))
    CALL(evt_msg_print, 0, STRING("sl_c2_den_04"), 0, STRING("c2_hizza"))
    CALL(evt_npc_set_anim, STRING("c2_hizza"), STRING("T_2"))
    CALL(evt_msg_print, 0, STRING("sl_c2_den_05"), 0, STRING("c2_hizza"))
    CALL(evt_npc_set_anim, STRING("c2_hizza"), STRING("B_1"))
    CALL(evt_msg_print, 0, STRING("sl_c2_den_06"), 0, STRING("c2_hizza"))
    CALL(c2_anim, STRING("c2_cage"), STRING("H_1"))
    CALL(evt_msg_print, 0, STRING("sl_c2_den_07"), 0, STRING("mario"))
    /* Luigi faints, flat, the veil over his face */
    CALL(evt_luigi_story_pose, STRING("FA_1"))
    CALL(evt_snd_sfxon, STRING("SFX_MARIO_DAMAGE1"), 0)
    WAIT_FRAMES(50)
    CALL(evt_luigi_story_pose, STRING("FA_2"))
    CALL_CHILD(c2_cast_in)
    CALL(evt_msg_print, 0, STRING("sl_c2_den_08"), 0, LW(11))
    CALL(evt_msg_print, 0, STRING("sl_c2_den_09"), 0, LW(10))
    CALL_CHILD(c2_cast_out)
    CALL(evt_npc_set_anim, STRING("c2_hizza"), STRING("T_2"))
    CALL(evt_msg_print, 0, STRING("sl_c2_den_10"), 0, STRING("c2_hizza"))
    SET_VALUE(SLF(F_DEN), 1)
    CALL(evt_luigi_story_pose, 0)
    CALL(evt_cam3d_evt_off, 0, 11)
    /* the fight (superluigi_hizza_aurora.c) */
    CALL(evt_sl_c2_arm, 2)
    CALL(evt_mario_key_onoff, 1)
    CALL(evt_npc_set_battle_info, STRING("c2_hizza"), 20)
    CALL(evt_npc_battle_start, STRING("c2_hizza"))
    CALL(evt_npc_wait_battle_end)
    CALL(evt_sl_c2_arm, 0)
    CALL(evt_npc_get_battle_result, LW(0))
    IF_INT_NOT_EQUAL(LW(0), 1)
        RETURN
    END_IF
    SET_VALUE(SLF(F_HIZZA), 1)
    CALL_CHILD(gon_20_after)
    RETURN
EVT_END()

static EVT_BEGIN(gon_20_init)
    CALL(evt_snd_bgmoff, PSND_FADE_OUT)
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG1_GON4"))
    CALL(evt_map_set_blend, 0, 150, 160, 190, 255)
    SET_VALUE(LW(0), PTR(gon_20_bero))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    /* the heap of gowns, pie tins and cake dishes, and the six brides behind their bones */
    CALL(evt_item_entry, STRING("c2_heap1"), ITEM_MOUSSE_CAKE, G20_SNAKE_X - 30, 0, G20_SNAKE_Z - 30, ITEM_ENTRY_FIXED_NO_TOUCH, -1, 0)
    CALL(evt_item_entry, STRING("c2_heap2"), ITEM_COCONUT, G20_SNAKE_X + 20, 0, G20_SNAKE_Z - 40, ITEM_ENTRY_FIXED_NO_TOUCH, -1, 0)
    CALL(evt_item_entry, STRING("c2_heap3"), ITEM_SHROOM_CAKE, G20_SNAKE_X + 50, 0, G20_SNAKE_Z - 10, ITEM_ENTRY_FIXED_NO_TOUCH, -1, 0)
    SET_VALUE(LW(1), PTR("c_bom_n"))
    SET_VALUE(LW(3), 0)
    SET_VALUE(LW(5), 270)
    SET_VALUE(LW(6), 0)
    SET_VALUE(LW(0), PTR("c2_bride1"))
    SET_VALUE(LW(2), G20_CAGE_X)
    SET_VALUE(LW(4), G20_CAGE_Z)
    CALL_CHILD(c2_person)
    SET_VALUE(LW(0), PTR("c2_bride2"))
    SET_VALUE(LW(2), G20_CAGE_X + 30)
    SET_VALUE(LW(4), G20_CAGE_Z - 20)
    CALL_CHILD(c2_person)
    SET_VALUE(LW(0), PTR("c2_bride3"))
    SET_VALUE(LW(2), G20_CAGE_X + 60)
    SET_VALUE(LW(4), G20_CAGE_Z)
    CALL_CHILD(c2_person)
    SET_VALUE(LW(0), PTR("c2_bride4"))
    SET_VALUE(LW(2), G20_CAGE_X + 15)
    SET_VALUE(LW(4), G20_CAGE_Z + 25)
    CALL_CHILD(c2_person)
    SET_VALUE(LW(0), PTR("c2_bride5"))
    SET_VALUE(LW(2), G20_CAGE_X + 45)
    SET_VALUE(LW(4), G20_CAGE_Z + 25)
    CALL_CHILD(c2_person)
    SET_VALUE(LW(0), PTR("c2_bride6"))
    SET_VALUE(LW(2), G20_CAGE_X + 75)
    SET_VALUE(LW(4), G20_CAGE_Z + 25)
    CALL_CHILD(c2_person)
    CALL(c2_has_model, STRING("c_sl_cage"), LW(0))
    IF_INT_EQUAL(LW(0), 1)
        CALL(evt_npc_entry, STRING("c2_cage"), STRING("c_sl_cage"))
        CALL(evt_npc_set_position, STRING("c2_cage"), G20_CAGE_X + 35, 0, G20_CAGE_Z + 50)
        CALL(evt_npc_set_anim, STRING("c2_cage"), STRING("S_1"))
    END_IF
    IF_INT_EQUAL(SLF(F_HIZZA), 0)
        CALL(evt_npc_entry, STRING("c2_hizza"), STRING("c_sl_hizza_whole"))
        CALL(evt_npc_set_tribe, STRING("c2_hizza"), STRING(SJ2_BULKY))
        CALL(evt_npc_set_position, STRING("c2_hizza"), G20_SNAKE_X, 0, G20_SNAKE_Z)
        CALL(evt_npc_set_anim, STRING("c2_hizza"), STRING("N_1"))
        CALL(evt_npc_set_battle_info, STRING("c2_hizza"), 20)
        SPAWN_EVENT(gon_20_den)
    END_IF
    TOUR(gon_20_bero, "")
    RETURN
EVT_END()

/* ======================================================================
 * The chapter: its table, text and setup
 * ====================================================================== */

/* the Plumpbelly Pie: the game's Mousse Cake, renamed and stronger, while chapter 2 runs */
static void pieSwap(int on) {
    static short hpWas = -1, fpWas = -1;
    ItemData* d = &itemDataTable[ITEM_MOUSSE_CAKE];
    if (on && hpWas < 0) {
        hpWas = d->hp_restored, fpWas = d->fp_restored;
        d->hp_restored = 15, d->fp_restored = 10;
        portModsSetMessage(d->name, "Plumpbelly Pie");
        portModsSetMessage(d->description, "A Plumpbelly pie, still warm.\nRestores 15 HP and 10 FP.");
        portModsSetMessage(d->menu_description, "A Plumpbelly pie, still warm.\nRestores 15 HP and 10 FP.");
    } else if (!on && hpWas >= 0) {
        d->hp_restored = hpWas, d->fp_restored = fpWas;
        hpWas = fpWas = -1;
        portModsSetMessage(d->name, NULL);
        portModsSetMessage(d->description, NULL);
        portModsSetMessage(d->menu_description, NULL);
    }
}

/* each frame: the chapter's own swaps while it runs (the pie; the gown, from the save) */
static void chapterFrame(void) {
    static int was;
    const int on = portSuperLuigiChapter() == 2;
    if (on != was) {
        was = on;
        pieSwap(on);
        if (!on) {
            portLuigiSetCostume(NULL);
        }
    }
    if (on && gp != NULL) {
        /* in the gown from the reveal to the dawn */
        const int bride = swGet(SL_FLAG(2, F_GOWN)) && !swGet(SL_FLAG(2, F_VOICE));
        static int brideWas = -1;
        if (bride != brideWas) {
            brideWas = bride;
            portLuigiSetCostume(bride ? "c_luigi_bride" : NULL);
        }
    }
    guardFrame();
}

/* chapter:2@map tests: $TTYD_SL_C2_FLAGS=a,b,c sets those of our flags on entry (the story up to a point) */
static void testFlags(void) {
    static int done;
    const char* v = getenv("TTYD_SL_C2_FLAGS");
    if (done || v == NULL || gp == NULL || portSuperLuigiChapter() != 2) {
        return;
    }
    done = 1;
    while (*v) {
        const int n = atoi(v);
        swSet(SL_FLAG(2, 0) + n);
        printf("[superluigi] chapter 2 test: flag %d set\n", n);
        v = strchr(v, ',');
        if (v == NULL) {
            break;
        }
        v++;
    }
    fflush(stdout);
}

/* $TTYD_SL_C2_RUN=<scene>: that scene's script, once, 2 s into its room (tests) */
static const struct {
    const char* name;
    EvtWord* script;
} kScenes[] = {
    {"mayor", bom_23_mayor_talk}, {"jerry", bom_23_jerry}, {"lane", bom_24_lane_talk},
    {"tailor", bom_24_tailor_talk}, {"procession", bom_24_procession}, {"pearls", bom_20_pearls},
    {"feast", bom_23_feast}, {"proposal", bom_23_mayor_talk}, {"voice", bom_23_voice}, {"dawn", bom_20_dawn},
    {"gate", bom_22_gate_talk}, {"bulkhead", dou_28_bulkhead}, {"den", gon_20_den}, {"winch", dou_24_winch},
    {"hollow", dou_20_gate}, {NULL, NULL},
};
static void testRun(void) {
    static int frames;
    static char map[16];
    const char* v = getenv("TTYD_SL_C2_RUN");
    int i;
    if (v == NULL || gp == NULL || portSuperLuigiChapter() != 2 || frames < 0) {
        return;
    }
    if (strcmp(map, gp->currentMapName) != 0) {
        snprintf(map, sizeof(map), "%s", gp->currentMapName);
        frames = 0;
    }
    if (++frames == 60 && getenv("TTYD_SL_C2_POS") != NULL) {
        /* $TTYD_SL_C2_POS=x,z: Luigi put there first */
        MarioWork* m = marioGetPtr();
        const char* p = getenv("TTYD_SL_C2_POS");
        m->position.x = (f32)atof(p);
        if (strchr(p, ',') != NULL) {
            m->position.z = (f32)atof(strchr(p, ',') + 1);
        }
    }
    if (frames != 120) {
        return;
    }
    for (i = 0; kScenes[i].name != NULL; i++) {
        if (strcmp(kScenes[i].name, v) == 0) {
            evtEntry(kScenes[i].script, 0, 0);
            printf("[superluigi] chapter 2 test: scene %s\n", v);
            fflush(stdout);
            frames = -1;
        }
    }
}

static void frame(void) {
    testFlags();
    testRun();
    chapterFrame();
}

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

static void setup2(void) {
    portSlC2BattleRegister();
    portModHostAddHook(1, kSlot_marioStMain, (void*)frame);
}

static const short kItems2[] = {ITEM_MUSHROOM, ITEM_MUSHROOM, ITEM_MUSHROOM, ITEM_HONEY_SYRUP, ITEM_HONEY_SYRUP,
                                ITEM_FIRE_FLOWER, ITEM_SLEEPY_SHEEP, 0};
static const short kBadges2[] = {ITEM_POWER_SMASH, ITEM_CLOSE_CALL, 0};

static const PortSlMap kMaps2[] = {
    {"bom_20", bom_20_init, "bom_00", 1}, {"bom_21", bom_21_init, "bom_03", 1}, {"bom_22", bom_22_init, "bom_04", 1},
    {"bom_23", bom_23_init, "bom_01", 1}, {"bom_24", bom_24_init, "bom_02", 1}, {"dou_20", dou_20_init, "dou_00", 1},
    {"dou_21", dou_21_init, "dou_01", 1}, {"dou_22", dou_22_init, "dou_02", 1}, {"dou_23", dou_23_init, "dou_12", 1},
    {"dou_24", dou_24_init, "dou_03", 1}, {"dou_25", dou_25_init, "dou_04", 1}, {"dou_26", dou_26_init, "dou_09", 1},
    {"dou_27", dou_27_init, "dou_10", 1}, {"dou_28", dou_28_init, "dou_05", 1}, {"dou_29", dou_29_init, "dou_11", 1},
    {"dou_30", dou_30_init, "dou_13", 1}, {"gon_20", gon_20_init, "gon_11", 1}, {NULL, NULL, NULL, 0},
};

/* ch2.md, "Unit overrides (chapter 2)" */
static const PortSlUnitStats kStats2[] = {
    {UNIT_GOOMBA, 3, 1, 0, 0, 12},
    {UNIT_ICE_PUFF, 3, 2, 0, 0, 14},
    {UNIT_FROST_PIRANHA, 4, 2, 0, 0, 15},
    {UNIT_EMBER, 5, 2, 0, 0, 15},
    {UNIT_PARABUZZY, 4, 2, 0, 2, 16},
    {UNIT_BULKY_BOB_OMB, 6, 0, 0, 1, 18},
    {UNIT_BILL_BLASTER, 5, 0, 0, 2, 16},
    {UNIT_BULLET_BILL, 0, 2, 0, -1, 0},
    {0},
};


extern const char* const portSlText2[];

static const PortSlChapter kChapter2 = {
    "Plumpbelly Village", "bom_20", "dokan", "gor_luigi_talk_0002", PORT_SL_FRIEND_JERRY,
    6, 20, 10, 9, 1, 1, 80, kItems2, kBadges2, PORT_SL_BIT(PORT_SL_FRIEND_BLOOEY),
    kMaps2, portSlText2, kStats2, setup2, "sl_c2_diary",
};

const PortSlChapter* portSlChapter2(void) {
    return &kChapter2;
}

/* Our own text, all of it (docs/super_luigi/ch2.md). Luigi speaks as
 * "mario"; the friend out as "party". */
const char* const portSlText2[] = {
    /* 1. arrival */
    "sl_c2_arrive_00", "<dkey><wait 300></dkey>The Strudel Continent. Snow,\nmountains, and somewhere up\nthere, a compass piece.\n<k>",
    "sl_c2_arrive_01", "Phew! Who builds a harbour\ntunnel that comes out in a well?\nIn the SNOW?\n<k>",
    "sl_c2_arrive_02", "Brrrr! Blooey, it's freezing!\nYou don't even have a coat!\n<k>\n<p>\nYou don't even have... arms!\nExactly!\n<k>",
    "sl_c2_arrive_03", "Cold is fine. Cold is clean.\n<k>",
    "sl_c2_arrive_04", "The compass says the piece is\nthis way. Past those mountains.\n<k>\n<p>\nIn a place called \"Plumpbelly\nVillage\". With a name like that,\nI bet they've got soup.\n<k>\n<p>\nHot soup. Hot soup and a fire.\nLet's go!\n<k>",
    /* 2. the gate */
    "sl_c2_gate_00", "H-halt! Who goes there?! If\nyou're a snake, I'm warning you,\nI'm v-very explosive!\n<k>",
    "sl_c2_gate_01", "Do I look like a snake?\n<k>",
    "sl_c2_gate_02", "...You look like a man with a\nmoustache and a squid.\n<k>",
    "sl_c2_gate_03", "Blooper.\n<k>",
    "sl_c2_gate_04", "Sorry. A moustache and a\nBlooper. Go on in.\n<k>\n<p>\nMind the gloom. It's very\nthick today.\n<k>",
    "sl_c2_npc_gate_00", "Village is that way. Can't\nmiss it. It's the gloomy part.\n<k>",
    "sl_c2_npc_gate_10", "The snake's GONE! I'm not even\nshaking! Well, a little. From\nexcitement!\n<k>",
    /* 3. the square */
    "sl_c2_npc_elder_00", "Plumpbelly used to be the\njolliest place on the Strudel.\nFeasts every night!\n<k>\n<p>\nPies the size of cartwheels!\nAnd then the snake came.\n<k>",
    "sl_c2_npc_child_00", "Mom says I can't have dessert\nuntil the snake goes away.\n<k>\n<p>\nThat's not fair. I didn't even\nDO anything.\n<k>",
    "sl_c2_npc_cook_00", "Every month it wants a bride.\nAnd all our feasts.\n<k>\n<p>\nWe send it the pies, and the\nroasts, and a poor girl in a\ngown, and it eats the pies and\nkeeps the girl.\n<k>\n<p>\nWe're out of pies. We're almost\nout of girls.\n<k>",
    "sl_c2_npc_vil4_00", "Shh. Don't say the word \"snake\"\ntoo loud. Or \"pie\". It's\ntoo sad.\n<k>",
    "sl_c2_npc_vil5_00", "The mayor's house is the big\ndome. He hasn't eaten in days.\nThat's how you know it's bad.\n<k>",
    "sl_c2_npc_vil6_00", "Our champion went up the\nmountain last spring. He came\nback. He won't say how.\n<k>",
    "sl_c2_npc_elder_10", "You'd go up there? For us?\nThen you'll need old Wickworth.\nShe's in the east lane.\n<k>",
    "sl_c2_npc_child_10", "Are you REALLY going to wear\na dress? Can I watch?\n<k>",
    "sl_c2_npc_cook_10", "If you bring our feasts back,\nI'll bake you a pie the size of\na wagon wheel. Two wagon wheels!\n<k>",
    "sl_c2_npc_vil4_10", "A rescue squad! I said\n\"snake\" out loud just now and\nI didn't even flinch.\n<k>",
    "sl_c2_npc_vil5_10", "Bulky Bob-ombs guard the\nsnake's hole. Heavy fellows.\nNot clever. Very heavy.\n<k>",
    "sl_c2_npc_vil6_10", "Jerry's going with you? Then\nthe champion's fuse is lit\nagain. Good.\n<k>",
    "sl_c2_npc_elder_20", "Pies the size of cartwheels!\nI told you! Have a slice, young\nman. Have three!\n<k>",
    "sl_c2_npc_child_20", "I'm having dessert FIRST.\nBefore dinner. Nobody can stop\nme. It's a feast!\n<k>",
    "sl_c2_npc_cook_20", "I owe you a pie the size of two\nwagon wheels. It's in the oven.\nThe oven is the size of a barn.\n<k>",
    "sl_c2_npc_vil4_20", "SNAKE! SNAKE SNAKE SNAKE!\nHa! It doesn't scare me\nanymore!\n<k>",
    "sl_c2_npc_vil5_20", "The mayor ate a whole roast by\nhimself. That's how you know\nit's good.\n<k>",
    "sl_c2_npc_vil6_20", "Six brides home, and our\nchampion too. Best night\nPlumpbelly's ever had.\n<k>",
    /* 4. the mayor */
    "sl_c2_mayor_00", "Visitors? Now? Oh, forgive me,\nwe'd normally lay on a\ntwelve-course welcome.\n<k>\n<p>\nToday I can offer you a cracker.\nHalf a cracker. Fuselle, is\nthere half a cracker?\n<k>",
    "sl_c2_mayor_01", "That's OK! I'm Luigi. I'm\nlooking for a piece of the\nMarvelous Compass.\n<k>\n<p>\nHave you seen something like\nthis? Shiny, curved, sort of\ncompass-y?\n<k>",
    "sl_c2_mayor_02", "Shiny... curved... Oh! Oh,\nthat's on HIZZA! The snake!\n<k>\n<p>\nIt wears one on its collar,\nround its neck! Or round its\ntail.\n<k>\n<p>\nIt's hard to tell which end is\nwhich. That's rather the\nproblem.\n<k>",
    "sl_c2_mayor_03", "Hizza came down from the\nmountain a year ago. Two heads,\none at each end.\n<k>\n<p>\nIt demands a feast every month,\nand a bride to go with it, and\nif we don't... well.\n<k>\n<p>\nIt's a very large snake.\n<k>",
    "sl_c2_mayor_04", "And tomorrow... tomorrow the\nbride is to be my little\nFuselle.\n<k>",
    "sl_c2_mayor_05", "Father, please. I've said I'll\ngo. Someone has to.\n<k>\n<p>\nAnd I'm the one with the dress\nsize.\n<k>",
    "sl_c2_mayor_06", "<shake>My baby! My only daughter!\nMy best pie taster!</shake>\n<k>",
    "sl_c2_mayor_07", "Wait, wait, wait. You're sending\nHER into a giant snake's lair?\nTomorrow? On her own?\n<k>",
    "sl_c2_mayor_08", "It's the custom! The bride goes\nup the Bridal Road in her gown\nand veil...\n<k>\n<p>\n...and the snake's guards take\nher into the grotto, and...\nand we never see her again!\n<k>",
    "sl_c2_mayor_09", "Then...<wait 400> I'll go instead.\n<k>",
    "sl_c2_mayor_10", "What?\n<k>",
    "sl_c2_mayor_11", "I'll go! In her place! I'll go\nin there and get that snake and\nget your brides back...\n<k>\n<p>\n...and get the compass piece!\nI'm Super Luigi! It's what I do!\n<k>\n<p>\n...Mostly! Lately!\n<k>",
    "sl_c2_mayor_12", "But... the snake wants a BRIDE,\nMr. Luigi.\n<k>",
    "sl_c2_mayor_13", "...\n<k>",
    "sl_c2_mayor_14", "Oh.\n<k>",
    "sl_c2_mayor_15", "Ha.\n<k>",
    "sl_c2_mayor_16", "It would have to be a bride in\na gown. And a veil. The guards\ncheck.\n<k>",
    "sl_c2_mayor_17", "<scale 0.7>How... how thorough do they\ncheck?</scale>\n<k>",
    "sl_c2_mayor_18", "They're Bulky Bob-ombs. They\ncheck if it's wearing a veil.\n<k>",
    "sl_c2_mayor_19", "...Then I'll need a veil.\n<k>",
    "sl_c2_npc_mayor_10", "Madame Wickworth, in the east\nlane. Eleven gowns she's made\nfor that serpent. Eleven!\n<k>",
    "sl_c2_npc_mayor_11", "Oh, look at you. Look at you!\nIf your mother could see you.\nI'm assuming. Please be careful!\n<k>",
    "sl_c2_npc_mayor_20", "Eat, Mr. Luigi! Eat! There is\nalways more soup!\n<k>",
    "sl_c2_npc_fuselle_00", "Father's taking it hard. I'm\ntaking it... well, I'm taking it.\nSomebody has to.\n<k>",
    "sl_c2_npc_fuselle_10", "You'd really go in my place?\nNobody has ever done anything\nlike that for me.\n<k>",
    "sl_c2_npc_fuselle_20", "You came back! With all of\nthem! I knew you would.\nI mostly knew.\n<k>",
    /* 5. Jerry */
    "sl_c2_jerry_00", "So. You are the outsider who\nwould wear the veil.\n<k>",
    "sl_c2_jerry_01", "Er. Yes? Hi! Luigi.\n<k>",
    "sl_c2_jerry_02", "I am Jerry, champion of\nPlumpbelly. Last spring I climbed\nthe Bridal Road alone to face\nthe beast.\n<k>",
    "sl_c2_jerry_03", "Wow! And?\n<k>",
    "sl_c2_jerry_04", "...And I am here, and the beast\nis there, and we shall speak no\nmore of it.\n<k>",
    "sl_c2_jerry_05", "You think you can do what I\ncould not? A stranger, with a\nsquid?\n<k>",
    "sl_c2_jerry_06", "Blooper.\n<k>",
    "sl_c2_jerry_07", "With a Blooper. Forgive me.\n<k>",
    "sl_c2_jerry_08", "I don't think I can do it on my\nown, no. But you know the\nmountain. You know the snake.\n<k>\n<p>\nI know how to... how to...\nwear a veil, apparently.\n<k>\n<p>\nCome with us! We'll be a\nrescue squad!\n<k>",
    "sl_c2_jerry_09", "\"A rescue squad.\"\n<k>",
    "sl_c2_jerry_10", "...I like the sound of that.\nVery well, Sir Luigi.\n<k>\n<p>\nPlumpbelly's champion shall walk\nbehind the bride. At a respectful\ndistance.\n<k>\n<p>\nMainly so that I am not seen\nwith the bride.\n<k>",
    "sl_c2_jerry_11", "<system>\nJerry joined your party!\n<k>",
    "sl_c2_jerry_12", "The tailor's lane is east, past\nthe cannon. Follow me, Sir\nLuigi.\n<k>",
    /* 6. Blast */
    "sl_c2_blast_00", "The ploughs pile it up every\nwinter. Normally the children dig\nit away.\n<k>\n<p>\nThe children have not felt like\ndigging.\n<k>",
    "sl_c2_blast_01", "Stand back, Sir Luigi. And\nperhaps a little further back\nthan that.\n<k>",
    "sl_je_blast_help", "<system>\nBlast: press <icon PAD_X 0.6 0 22 0> and Jerry lights\nhis fuse. Walk him where you\nneed him.\n<k>\n<p>\nWhen the fuse runs down, he goes\noff. Cracked walls, packed ice\nand old fuses don't stand a\nchance.\n<k>\n<p>\nKeep Luigi out of the blast!\n<k>",
    "sl_c2_blast_02", "Ha! It has been too long.\n<k>",
    /* 7. the gown */
    "sl_c2_tailor_00", "A bride. For Hizza. Again.\nEleven gowns I've made for that\nserpent. Eleven!\n<k>\n<p>\nThe best work of my life, and it\neats them. Well, it eats the\ncake.\n<k>\n<p>\nThe gowns it keeps. Somewhere.\nProbably in a heap.\n<k>",
    "sl_c2_tailor_01", "Stand up straight. Arms out.\nHmm. Broad shoulders. Short legs.\nA moustache.\n<k>\n<p>\nThis will take every trick\nI know.\n<k>",
    "sl_c2_tailor_02", "Can you make it... I don't\nknow... less frilly?\n<k>",
    "sl_c2_tailor_03", "Young man, the frills are\nload-bearing.\n<k>",
    "sl_c2_tailor_04", "I need two things. Snowmoth\nSilk for the veil: the moths\nroost in the hollow pine on\nFrostcrust Path.\n<k>\n<p>\nAnd the Bridal Pearls, the\nvillage's own. The mayor dropped\nthem down the well in the pass,\nin a fit of grief.\n<k>\n<p>\nThat water's too cold for any\nBob-omb. Bring me both.\n<k>",
    "sl_c2_tailor_05", "Silk from the hollow pine, and\nthe pearls from the well.\nGo on. The frills won't wait.\n<k>",
    "sl_c2_silk_01", "<system>\nLuigi got the Snowmoth Silk!\n<k>",
    "sl_c2_silk_02", "So soft! And so... cold. Brr.\nThe tailor will love it.\n<k>",
    "sl_c2_pearls_00", "Found them. Also found a boot.\nKeeping the boot.\n<k>",
    "sl_c2_pearls_01", "<system>\nLuigi got the Bridal Pearls!\n<k>",
    "sl_c2_gown_00", "Silk. Pearls. Good. Behind the\ncurtain, young man. And hold\nyour breath.\n<k>",
    "sl_c2_gown_01", "...I have outdone myself.\n<k>",
    "sl_c2_gown_02", "Oh, Mr. Luigi. You look...\nyou look...\n<k>",
    "sl_c2_gown_03", "Please don't say it.\n<k>",
    "sl_c2_gown_04", "...radiant.\n<k>",
    "sl_c2_gown_05", "<wait 800>...I am never telling anybody\nabout this.\n<k>",
    "sl_c2_gown_06", "Nor I. On my honour. On my\nfuse.\n<k>",
    "sl_c2_gown_07", "It's FINE. It's FINE! It's for\nthe princess. It's for the\ncompass. It's for the village.\n<k>\n<p>\nLet's just... let's just go\nbefore I think about it.\n<k>",
    "sl_c2_npc_tailor_10", "Walk slow. Walk dainty. And\nwhatever you do, don't run in\nthat hem.\n<k>",
    "sl_c2_npc_tailor_20", "Eleven gowns, and the twelfth\ncame back. On a man. I need\nto sit down.\n<k>",
    "sl_c2_road_shut", "The Bridal Road... Not\nlike this. Not without a\nveil, anyway.\n<k>",
    /* 8. the procession */
    "sl_c2_procession_00", "For luck. And in case the\nsnake wants dessert first.\n<k>",
    "sl_c2_procession_01", "Come back to us, Mr. Luigi.\nIf you come back... if you bring\nback my brides and our feasts...\n<k>\n<p>\n...anything in Plumpbelly is\nyours!\n<k>",
    "sl_c2_procession_02", "Anything? Even soup?\n<k>",
    "sl_c2_procession_03", "Especially soup!\n<k>",
    "sl_c2_procession_04", "<system>\nLuigi got a Plumpbelly Pie!\n<k>",
    /* 9. the Hollow */
    "sl_c2_hollow_gate_00", "Halt! Who approaches the\nHollow of the Mighty Hizza?\n<k>",
    "sl_c2_hollow_gate_01", "Oh! Um! It's me! The... bride!\nTee-hee!\n<k>",
    "sl_c2_hollow_gate_02", "Veil?\n<k>",
    "sl_c2_hollow_gate_03", "Veil.\n<k>",
    "sl_c2_hollow_gate_04", "Gown?\n<k>",
    "sl_c2_hollow_gate_05", "Gown. A lot of gown.\n<k>",
    "sl_c2_hollow_gate_06", "...Is that a moustache?\n<k>",
    "sl_c2_hollow_gate_07", "It's... lace! Plumpbelly lace!\nVery fashionable this year!\n<k>",
    "sl_c2_hollow_gate_08", "Huh. Fashion. Go on in, bride.\nWalk slow. Walk dainty.\n<k>\n<p>\nBrides don't run in the Hollow.\nAnything that runs, we flatten.\n<k>",
    "sl_c2_hollow_gate_09", "<scale 0.7>Walk slowly, Sir Luigi. Bulky\nBob-ombs are not clever, but they\nare very, very heavy.</scale>\n<k>",
    "sl_c2_hollow_gate_10", "<system>\nIn the Hollow, walk past the\nguards. Running, jumping or a\nhammer where they can see you\nwill give the game away!\n<k>",
    "sl_c2_guard_01", "Evening, ma'am.\n<k>",
    "sl_c2_guard_02", "Evening, ma'am. Lovely gown.\n<k>",
    "sl_c2_guard_03", "Ma'am. Mind the puddles.\n<k>",
    "sl_c2_guard_04", "OI! BRIDES DON'T DO THAT!\n<k>",
    "sl_c2_guard_05", "OI! BRIDES DON'T DO THAT!\nGET 'ER!\n<k>",
    "sl_c2_guard_06", "OI! BRIDES DON'T DO THAT!\nThat's not dainty at all!\n<k>",
    "sl_c2_winch_00", "The winch is frozen solid.\nIt needs a good blast...\n<k>",
    "sl_c2_winch_01", "With boots like these... here\ngoes... nngh... HUP!\n<k>",
    "sl_c2_gate_shut", "Gate's down, man. Even I can't\nswim through that. Somebody's\ngot to crank it up.\n<k>",
    "sl_c2_winch_02", "Thawed out,but it won't turn.\nMy feet just slip on these rocks.\n<k>\n<p>\nAll that water comes from\nsomewhere up there, though.\nBlooey could swim up it...\n<k>",
    "sl_c2_boots_00", "<system>\nYou got the Super Boots!\n<k>\n<p>\nPress <icon PAD_A 0.6 0 22 0> in mid-air to Spin\nJump and smash through loose\nfloors and flip panels.\n<k>",
    "sl_c2_boots_01", "New boots! Oh, I wish they'd go\nwith the dress.\n<k>",
    "sl_c2_bulkhead_00", "Halt, bride! Sergeant Bulkhead,\nCaptain of the Hollow!\n<k>\n<p>\nNobody gets to the Master\nwithout the Sergeant's say-so!\n<k>",
    "sl_c2_bulkhead_01", "Let's have a look at you. Hmm.\nHmmmm. Turn around. Do a twirl.\n<k>",
    "sl_c2_bulkhead_02", "...That's a moustache.\n<k>",
    "sl_c2_bulkhead_03", "...Yes. Yes, it is.\n<k>",
    "sl_c2_bulkhead_04", "<shake>THAT'S NO BRIDE! THAT'S A MAN\nIN A FROCK! GET 'IM, LADS!</shake>\n<k>",
    "sl_c2_bulkhead_05", "The bridge is ours. And the\nguards won't be checking any\nmore veils tonight.\n<k>",
    "sl_c2_den_00", "Beyond this wall, the beast.\nLast time I stood here, I...\n<k>\n<p>\n...was very brave, for nearly a\nwhole minute.\n<k>",
    "sl_c2_den_01", "And this time you've got a team.\nAnd a bride! A very, VERY\nreluctant bride.\n<k>",
    /* 10. Hizza */
    "sl_c2_den_02", "Ssssso. The bride comesss at\nlassst. And she brought a pie.\nHow thoughtful.\n<k>",
    "sl_c2_den_03", "...It's not a bride, Hiss.\n<k>",
    "sl_c2_den_04", "Of course it'sss a bride.\nIt has a veil.\n<k>",
    "sl_c2_den_05", "It has a moustache.\n<k>",
    "sl_c2_den_06", "Brides can have moustachesss!\nDon't be ssso judgmental!\n<k>",
    "sl_c2_den_07", "T-t-two... two heads... one at\neach... at each END...\n<k>",
    "sl_c2_den_08", "He does that.\n<k>",
    "sl_c2_den_09", "I know.\n<k>",
    "sl_c2_den_10", "Good. Fainted brides are easier\nto swallow.\n<k>",
    "sl_c2_after_00", "Did we... is it... where's the\nsnake?\n<k>",
    "sl_c2_after_01", "Gone, Sir Luigi. You dodged.\nIt bit itself. Twice.\n<k>",
    "sl_c2_after_02", "I DID? I mean... I did! Yes!\nJust as I planned!\n<k>\n<p>\nThe ol' dodge-and-let-them-\nbonk-heads! A classic!\n<k>",
    "sl_c2_after_03", "You were screaming.\n<k>",
    "sl_c2_after_04", "Tactical screaming!\n<k>",
    "sl_c2_after_05", "<system>\nYou got a piece of the\nMarvelous Compass!\n<k>",
    "sl_c2_after_06", "You saved us!\n<k>",
    "sl_c2_after_07", "A bride saved the brides!\n<k>",
    "sl_c2_after_08", "Your gown is GORGEOUS, where\ndid you get it?\n<k>",
    /* 11. the feast */
    "sl_c2_feast_00", "A FEAST! A FEAST FOR OUR HERO!\nBring the cartwheel pies! Bring\nthe roast! Bring the soup!\n<k>\n<p>\nThere's SOUP, Mr. Luigi!\n<k>",
    "sl_c2_feast_01", "Soup...\n<k>",
    "sl_c2_proposal_00", "Mr. Luigi. Plumpbelly owes you\neverything. And I said anything\nin Plumpbelly would be yours.\nSo...\n<k>",
    "sl_c2_proposal_01", "Would you... marry my Fuselle?\n<k>",
    "sl_c2_proposal_02", "You did wear the dress for me,\nafter all.\n<k>",
    "sl_c2_proposal_03", "I... uh... marry? Me? I mean...\nshe's very... she's really\nvery...\n<k>\n<p>\nand the soup here is AMAZING...\n<o>",
    "sl_c2_proposal_04", "<select 0 1 0 40>\nI'd be honoured!\nLet me think about it!",
    "sl_c2_proposal_08", "I'd be... honoured. Yes.\nProbably. Is there more soup?\n<k>",
    "sl_c2_proposal_09", "Could I, uh... sleep on it?\nOn a full stomach?\n<k>",
    "sl_c2_proposal_05", "WONDERFUL! We'll have the\nwedding in the morning!\n<k>",
    "sl_c2_proposal_06", "<shake>*cough* In the MORNING?!</shake>\n<k>",
    "sl_c2_proposal_07", "Of course! Sleep on it! We'll\nhave the wedding in the morning!\n<k>",
    /* 12. the voice */
    "sl_c2_voice_00", "<system>\nLuigi set the piece into the\nMarvelous Compass!\n<k>",
    "sl_c2_voice_01", "<i>...Hello? Is someone... can\nyou hear me?</i>\n<k>",
    "sl_c2_voice_02", "<i>The wind here never stops. But\ntonight, for a moment, it sounded\nlike someone calling my name.</i>\n<k>",
    "sl_c2_voice_03", "<i>If it was you... please.\nThere's so little time.\nKeep coming.</i>\n<k>",
    "sl_c2_voice_04", "<system>\nThe needle swings east.\n<k>",
    "sl_c2_voice_05", "...Eclair. Princess Eclair. That\nwas her. That was HER voice.\n<k>",
    "sl_c2_voice_06", "And I nearly... in the morning\nI'm supposed to... oh no.\nOh no no no.\n<k>",
    "sl_c2_voice_07", "Sir Luigi. Plumpbelly's champion\nhas a suggestion. A strategic\none.\n<k>",
    "sl_c2_voice_08", "The early boat leaves before\ndawn.\n<k>",
    "sl_c2_voice_09", "Jerry, you're a genius.\n<k>",
    "sl_c2_voice_10", "I am aware.\n<k>",
    /* 13. before dawn */
    "sl_c2_note_00", "<system>\n\"Dear Mayor and Fuselle: thank\nyou for the soup. I'm sorry,\nI have a princess to save.\n<k>\n<p>\n\"You will find someone\nwonderful. Probably someone with\nfewer moustaches.\n<k>\n<p>\n\"Love, Luigi.\n\"P.S. The dress is on the chair.\"\n<k>",
    "sl_c2_dawn_00", "I too must go. The champion of\nPlumpbelly walks with the\nliberators, to the end.\n<k>",
    "sl_c2_dawn_01", "Really? Won't they miss you?\n<k>",
    "sl_c2_dawn_02", "They have pie now. They will\nbe fine.\n<k>",
    "sl_c2_dawn_03", "<system>\nThe compass glows, pointing\neast: Circuit Break Island.\n<k>",
    /* 14. the tag */
    "sl_c2_tag_00", "<scale 0.6>As Jerry tells it\n</scale><k>\n<p>\nThe tale of the Hollow, as it\ntruly happened.\n<k>",
    "sl_c2_tag_01", "The beast reared up, both heads.\nOur \"hero\" took one look and fell\nover like a sack of flour.\n<k>\n<p>\nIn a ball gown.\n<k>",
    "sl_c2_tag_02", "It fell to me. I lit my fuse.\nI leapt. I placed myself\nprecisely between the two\nheads...\n<k>\n<p>\n...and as they struck...\nI stepped aside.\n<k>",
    "sl_c2_tag_03", "They met in the middle.\nA champion's move. Taught to me\nby my grandfather.\n<k>\n<p>\nWho learned it from a goat.\n<k>",
    "sl_c2_tag_04", "When Sir Luigi awoke, he had a\ncompass piece in his hand and no\nidea how it got there.\n<k>\n<p>\nI let him keep the credit.\nA champion is generous.\n<k>",
    "sl_c2_tag_05", "...Also, he looked very nice in\nthe dress.\n<k>",
    "sl_c2_tag_06", "That is not part of the tale.\nStrike that.\n<k>",
    "sl_c2_diary", "Plumpbelly Village feasted again,\nits brides home and its snake\ngone in a puff of smoke.\n<k>\n<p>\nThe compass had its third part.\nLuigi left before the wedding,\nand Jerry went with him.\n<k>",
    /* bom_24's people */
    "sl_c2_npc_kid_00", "That's Old Bertha. She used to\nfire a pie salute at every feast!\n<k>\n<p>\nNow she just sits there. Being\na cannon. Sadly.\n<k>",
    "sl_c2_npc_kid_20", "A FEAST! Can Old Bertha fire\nagain? Somebody light her fuse!\nSomebody with a fuse!\n<k>",
    "sl_c2_bertha_00", "PIE SALUTE! Look, something\nshiny came out with the pie!\n<k>",
    "sl_c2_npc_inn_20", "For the bride! Our finest! It's\nbeen under the mattress for\nyears.\n<k>",
    /* the inn, the Full Belly */
    "sl_c2_yado_00", "Welcome to the Full Belly! A warm\nbed and breakfast, %d coins.\nStaying the night?\n<o>",
    "sl_c2_yado_01", "<p>\nYou look rested enough to me.\nStay anyway?\n<o>",
    "sl_c2_yado_02", "<p>\nSuit yourself! The door's always\nopen. Well, unlocked.\n<k>",
    "sl_c2_yado_03", "<p>\nSleep tight! Mind the snow on\nthe windowsill.\n<k>",
    "sl_c2_yado_04", "<p>\nOh dear, you're a little short.\nCome back when your purse is\nplumper!\n<k>",
    "sl_c2_yado_05", "Good morning! Breakfast is on the\ntable. Mind, it's hot!\n<k>",
    "sl_c2_yado_06", "<select 0 1 0 40>\nI'll stay\nNo thanks",
    "sl_c2_yado_07", "<p>\nOh, an Inn Coupon! Want to use\nit tonight?\n<o>",
    "sl_c2_yado_08", "<select 0 1 0 40>\nUse it\nKeep it",
    "sl_c2_yado_09", "<p>\nThat's your fiftieth night with\nus! You're practically family!\n<k>",
    "sl_c2_yado_10", "<p>\nAnd family gets a present.\n<k>",
    "sl_c2_yado_11", "<p>\nHere, from all of Plumpbelly!\n<k>",
    "sl_c2_yado_12", "<p>\nCome back any time!\n<k>",
    "sl_c2_yado_13", "<p>\nSleep well!\n<k>",
    /* the battles */
    "sl_un_hizza", "Hizza",
    "sl_un_hiss", "Hiss",
    "sl_un_zah", "Zah",
    "sl_un_bulkhead", "Sgt. Bulkhead",
    "sl_un_hissguard", "Hissguard",
    "sl_c2_bt_fainted", "Luigi fainted!\n<k>",
    "sl_c2_bt_wake_bl", "Wake up. Wake UP.\n<k>",
    "sl_c2_bt_wake_je", "Sir Luigi, the beast is right\nTHERE.\n<k>",
    "sl_c2_hizza_01", "Ssstop ssstealing my bitesss!\n<k>",
    "sl_c2_hizza_02", "You missed again.\n<k>",
    "sl_c2_hizza_03", "I didn't misss, he moved!\n<k>",
    "sl_c2_hizza_04", "That's what missing is.\n<k>",
    "sl_c2_bt_tele_hiss", "Ssssstay ssstill...\n<k>",
    "sl_c2_bt_tele_zah", "...Don't move.\n<k>",
    "sl_c2_bt_tele", "Both heads draw back and fix on\nLuigi from either side...\n<k>",
    "sl_c2_bt_crash", "The heads crashed into each\nother! The brides cheer!\n<k>",
    "sl_c2_bt_rage", "Hiss and Zah rear up in a\nrage!\n<k>",
    "sl_c2_bt_squeeze", "The coil tightens!\n<k>",
    "sl_c2_bt_hissbit", "Hiss bit Zah!\n<k>",
    "sl_c2_bt_zahbit", "Zah bit Hiss!\n<k>",
    "sl_c2_bt_brides_mush", "Here, bride! Catch!\n<k>",
    "sl_c2_bt_cling", "Hizza won't go down! Both heads\nrise for one last strike...\n<k>",
    "sl_c2_bt_brace", "Brace yourself! Wait for the\nheads... then lunge LEFT!",
    "sl_c2_bt_final", "Thisss time, no tricksss!\n<k>",
    "sl_c2_bt_lunge", "<icon STICK_LEFT 0.8 0 22 0> Lunge left!",
    "sl_c2_bt_light", "Light 'em up, lads!\n<k>",
    "sl_c2_bt_lit", "A Hissguard's fuse is lit!\nIt'll go off next turn!\n<k>",
    "sl_c2_bt_boom", "The lit Hissguard goes off!\n<k>",
    "sl_c2_bt_fusefuse", "One must fight fire with fire.\nOr in this case, fuse with fuse.\n<k>",
    NULL,
};
