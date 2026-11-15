/* Restorations: the game side (port/include/port/restorations.h). The
 * settings rows and the chalk outline's disc file are in
 * port/settings/features/restorations.cpp. */
#include "port/restorations.h"
#include "port/settings.h"
#include "data/npc_data.h"
#include "driver/animdrv.h"
#include "driver/msgdrv.h"
#include "driver/npcdrv.h"
#include "port/luigi.h"
#include "evt/evt_cmd.h"
#include "evt/evt_nannpc.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mariost.h"
#include "version.h"
#ifdef PORT_MODS
#include "port/mods.h"
#endif

#include <stdio.h>
#include <string.h>

extern const char* language_dir[]; /* msgdrv.c: each language's message folder */

static const char* const kKeys[PORT_RESTORE_COUNT] = {"rs_chalk_outline", "rs_bunny_boo", "rs_jp_flashes",
                                                      "rs_xnaut_crowd"};

const char* portRestoreKey(int id) {
    return id >= 0 && id < PORT_RESTORE_COUNT ? kKeys[id] : "";
}

int portRestoreOn(int id) {
#if VERSION <= VERSION_JP
    if (id == PORT_RESTORE_JP_FLASHES) {
        /* the JP release's own strobes: Less flashing tames them to the US release's, as on US and EU */
        const int* less = portSettingsFind("less_flashing");
        return less == NULL || *less == 0;
    }
    return 1;
#else
    int* v;
    if (id < 0 || id >= PORT_RESTORE_COUNT) {
        return 0;
    }
    v = portSettingsDeclare(kKeys[id], 0, 0, 1);
    if (id == PORT_RESTORE_JP_FLASHES && v != NULL && *v != 0) {
        /* Less flashing (rules_pc.c) wins over the restored strobes: they are screen fades, which its effect
         * dimming doesn't reach */
        const int* less = portSettingsFind("less_flashing");
        return less == NULL || *less == 0;
    }
    return v != NULL && *v != 0;
#endif
}

int portEvtRestore(struct EventEntry* event, int isFirstCall) {
    (void)isFirstCall;
    evtSetValue(event, event->args[1], portRestoreOn(evtGetValue(event, event->args[0])));
    return EVT_RETURN_DONE;
}

int portEvtRestorePick(struct EventEntry* event, int isFirstCall) {
    const int id = evtGetValue(event, event->args[0]);
    const int on = portRestoreOn(id);
    const int value = evtGetValue(event, event->args[on ? 2 : 1]);
    (void)isFirstCall;
    printf("[restore] %s: %s (%d)\n", portRestoreKey(id), on ? "on" : "off", value);
    fflush(stdout);
    evtSetValue(event, event->args[3], value);
    return EVT_RETURN_DONE;
}

int portEvtNannpcCount(struct EventEntry* event, int isFirstCall) {
    (void)isFirstCall;
    evtSetValue(event, event->args[0], nanNPCWork != NULL ? nanNPCWork->num : 0);
    return EVT_RETURN_DONE;
}

/* --- the Parlor's Boo ----------------------------------------------------------- */

#if VERSION > VERSION_JP
/* the Japanese model, still on the US and EU discs, and the US one */
static const char* const kBunnyJp[9] = {"c_bteresa", "BTS_S_1", "BTS_Z_1", "BTS_S_1", "BTS_T_1",
                                        "BTS_W_1",   "BTS_R_1", "BTS_Z_1", "BTS_Z_1"};
static const char* const kBunnyUs[9] = {"c_bteresa2", "S_1", "Z_1", "S_1", "T_1", "W_1", "R_1", "Z_1", "Z_1"};
#endif

void portRestoreCreditNpc(const char** model, const char** anim) {
#if VERSION > VERSION_JP
    if (*model != NULL && strcmp(*model, kBunnyUs[0]) == 0 && portRestoreOn(PORT_RESTORE_BUNNY_BOO)) {
        *model = kBunnyJp[0];
        *anim = kBunnyJp[5];
    }
#else
    (void)model;
    (void)anim;
#endif
}

#if VERSION > VERSION_JP
static int sPosePending; /* the setting changed: the Boo already in the room still has the other model */

/* The Boo in the room now (the tribe's NPCs) given the model the setting wants, in place: the pose
 * swapped as npcRecoveryFiledNpc re-creates one after a battle (its turn and facing kept), her animation
 * the same one of the other model's. Only in the field (in a battle the field's poses are released, and
 * come back with the model they had: done once the battle is over). */
static void bunnyRepose(NpcTribeInfo* tribe, int on) {
    const char* const* to = on ? kBunnyJp : kBunnyUs;
    const char* const* from = on ? kBunnyUs : kBunnyJp;
    NpcWork* wp;
    NpcEntry* npc;
    int i, j;
    if (gp == NULL || gp->inBattle || (wp = portNpcFieldWork()) == NULL) {
        return;
    }
    sPosePending = 0;
    for (i = 0, npc = wp->entries; i < wp->total; i++, npc++) {
        AnimationPose* pose;
        const char* anim = to[1];
        u32 flags;
        f32 pera, rot, flip, target;
        s32 id;
        if (!(npc->flags & NPC_FLAG_USED) || npc->tribe != tribe || npc->poseId < 0 ||
            strcmp(animPoseGetAnimBaseDataPtr(npc->poseId)->fileName, to[0]) == 0 ||
            (pose = animPoseGetAnimPosePtr(npc->poseId))->paperId != -1) {
            continue;
        }
        for (j = 1; j < 9; j++) {
            if (strcmp(animPoseGetCurrentAnim(npc->poseId)->name, from[j]) == 0) {
                anim = to[j];
                break;
            }
        }
        id = animPoseEntry(to[0], 0);
        if (id < 0) {
            continue; /* (not loaded: she keeps the model she has) */
        }
        flags = pose->flags;
        pera = pose->peraRotY;
        rot = pose->rotationY;
        flip = pose->scaleZFlip;
        target = pose->peraTargetRotY;
        portLuigiRelease(npc->poseId);
        npc->poseId = id;
        animPoseSetAnim(id, anim, TRUE);
        pose = animPoseGetAnimPosePtr(id);
        pose->flags = flags;
        pose->peraRotY = pera;
        pose->rotationY = rot;
        pose->scaleZFlip = flip;
        pose->peraTargetRotY = target;
        snprintf(npc->currentAnim, sizeof(npc->currentAnim), "%s", anim);
        snprintf(npc->stayAnim, sizeof(npc->stayAnim), "%s", to[3]);
        snprintf(npc->talkAnim, sizeof(npc->talkAnim), "%s", to[4]);
        printf("[restore] rs_bunny_boo: %s in the room now (%s)\n", to[0], anim);
        fflush(stdout);
    }
}
#endif

void portRestorationsApply(void) {
#if VERSION > VERSION_JP
    static NpcTribeInfo* sBunny;
    static int sSearched, sWas = -1;
    const int on = portRestoreOn(PORT_RESTORE_BUNNY_BOO);
    if (!sSearched) {
        NpcTribeInfo* t;
        sSearched = 1;
        for (t = npcTribe; t->nameJp != NULL; t++) {
            if (t->modelName != NULL && strcmp(t->modelName, kBunnyUs[0]) == 0) {
                sBunny = t;
                break;
            }
        }
    }
    if (sBunny != NULL && on == sWas && sPosePending) {
        bunnyRepose(sBunny, on);
    }
    if (sBunny == NULL || on == sWas) {
        return;
    }
    sPosePending = sWas >= 0; /* (a change while playing: the Boo already in the room too) */
    sWas = on;
    printf("[restore] rs_bunny_boo: %s\n", on ? kBunnyJp[0] : kBunnyUs[0]);
    {
        const char* const* m = on ? kBunnyJp : kBunnyUs;
        sBunny->modelName = m[0];
        sBunny->initialAnim = m[1];
        sBunny->stopAnimation = m[2];
        sBunny->stayAnim = m[3];
        sBunny->talkAnim = m[4];
        sBunny->walkAnimation = m[5];
        sBunny->runAnimation = m[6];
        sBunny->damageAnimation = m[7];
        sBunny->confuseAnimation = m[8];
    }
    if (sPosePending) {
        bunnyRepose(sBunny, on);
    }
#endif
}

/* --- Doopliss's name ------------------------------------------------------------- */

int portEvtNameAnswer(struct EventEntry* event, int isFirstCall) {
    EvtWord answer = evtGetValue(event, event->args[0]);
    (void)isFirstCall;
#ifdef PORT_MODS
    {
        const char* pack = portModMsgSearch("pc_doopliss_name", language_dir[gp->language]);
        if (pack != NULL && pack[0] != '\0') {
            answer = (EvtWord)pack;
        }
    }
#endif
    evtSetValue(event, event->args[1], answer);
    return EVT_RETURN_DONE;
}
