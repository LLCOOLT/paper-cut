/* Restorations: the game side (port/include/port/restorations.h). The
 * settings rows and the chalk outline's disc file are in
 * port/settings/features/restorations.cpp. */
#include "port/restorations.h"
#include "port/settings.h"
#include "data/npc_data.h"
#include "driver/msgdrv.h"
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
    if (sBunny == NULL || on == sWas) {
        return;
    }
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
