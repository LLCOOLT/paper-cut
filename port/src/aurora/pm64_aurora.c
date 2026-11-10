/* The Paper Mario 64 partners' game side on Aurora (port/include/port/pm64.h,
 * docs/PM64_LINK.md): the frame hook (their field abilities and joining) and
 * the battle moves' names and descriptions from the linked ROM. The
 * partners' state, draw and FP costs are port/src/common/pm64_partner_pc.c. */
#include "port/hook_list.h"
#include "port/mod_host.h"
#include "port/pm64.h"
#include "port/settings.h"
#include "port/tr.h"
#include "port/versions.h"
#include "driver/seqdrv.h"
#include "evt/evt_cmd.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mario.h"
#include "mario/mariost.h"
#include "party.h"
#include <stdio.h>
#include <string.h>

/* the host moves of the partners out now as PM64's (the ROM's text), the
 * rest the game's own; set again when that changes */
static void moveTexts(void) {
    static unsigned sSig = ~0u;
    static char sName[64][48], sDesc[64][160];
    const int n = portPm64MoveCountMapped();
    static char sHave[64];
    unsigned sig = (portPm64Generation() * 8u + (unsigned)(portPm64TextLanguage() + 1)) * 0x9E3779B1u;
    int i;
    for (i = 0; i < n && i < 64; i++) {
        sig = sig * 31u + (unsigned)portPm64MoveWorn(i);
    }
    if (sig == sSig) {
        return;
    }
    sSig = sig;
    /* (the ROM's text only in the game's language: portPm64MoveText; else the game's own stays) */
    for (i = 0; i < n && i < 64; i++) {
        sHave[i] = (char)(portPm64MoveWorn(i) && portPm64MoveText(i, sName[i], sizeof(sName[i]), sDesc[i], sizeof(sDesc[i])));
    }
    /* the TTYD texts back first (a tag two partners share is then set by the one out) */
    for (i = 0; i < n && i < 64; i++) {
        if (!sHave[i]) {
            if (portPm64MoveTag(i, 0) != NULL) {
                portModsSetMessage(portPm64MoveTag(i, 0), NULL);
            }
            if (portPm64MoveTag(i, 1) != NULL) {
                portModsSetMessage(portPm64MoveTag(i, 1), NULL);
            }
        }
    }
    for (i = 0; i < n && i < 64; i++) {
        if (sHave[i]) {
            if (portPm64MoveTag(i, 0) != NULL) {
                portModsSetMessage(portPm64MoveTag(i, 0), sName[i]);
            }
            if (portPm64MoveTag(i, 1) != NULL && sDesc[i][0] != '\0') {
                portModsSetMessage(portPm64MoveTag(i, 1), sDesc[i]);
            }
        }
    }
}

/* --- the frame */

static void frameHook(void) {
    if (gp == NULL) {
        return;
    }
    portPm64BattleCosts();
    portPm64FieldFrame();
    moveTexts();
}

/* --- Bow's Outta Sight in the field: Vivian's Veil (party_vivian.c vivian_use) with PM64's look. The shadow's
 * paper ("p_bibi": Mario and the partner sinking into the floor) and Mario's sunk pose are left out while Bow
 * is out, and its draw offsets with them; Mario crouches, see-through, with Bow over him (pm64_field_pc.c). */

static int bowOut(void) {
    return portPm64PartnerOut() == PM64_PARTNER_BOW;
}

static int bowNoPaper(BOOL* r, const char** name) {
    if (bowOut() && *name != NULL && strcmp(*name, "p_bibi") == 0) {
        *r = TRUE;
        return 1;
    }
    return 0;
}

static int bowNoPaperAnim(const char** name) {
    return bowOut() && *name != NULL && strcmp(*name, "PM_B_1") == 0;
}

/* PM64's world_bow UseAbility (OUTTA_SIGHT_GATHER) puts Mario in ANIM_Mario1_Crouch once she has gathered onto
 * him; TTYD's closest pose is the crouch before a spring (M_P_1), which holds its last, crouched frame. */
static int bowStanding(const char** name) {
    if (bowOut() && *name != NULL && strcmp(*name, "M_B_3") == 0) {
        *name = "M_P_1";
    }
    return 0;
}

static int bowNoPartyPaper(BOOL* r, PartyEntry** party, const char** name) {
    (void)party;
    if (bowOut() && *name != NULL && strcmp(*name, "p_bibi") == 0) {
        *r = TRUE;
        return 1;
    }
    return 0;
}

static int bowNoPartyPaperAnim(PartyEntry** party, const char** name) {
    (void)party;
    return bowOut() && *name != NULL && strcmp(*name, "PM_B_1") == 0;
}

static void bowVeil(PartyEntry* party) {
    MarioWork* m = marioGetPtr();
    if (!bowOut() || party == NULL || m == NULL) {
        return;
    }
    /* (Veil's offsets lift the two as the paper sinks them: without the paper they stay where they are) */
    m->dispOfs2.y = 0.0f;
    party->dispOfs.y = 0.0f;
    /* hidden from the moment the shadow would have opened until the rise out begins */
    portPm64BowVeilSet((party->flags & PARTY_FLAG_IS_BEING_USED) && party->useMotionId >= 4 && party->useMotionId < 22);
}

/* --- Watt (on Ms. Mowz): the partner button picks her up and puts her down, in place of Ms. Mowz's sniff
 * (pm64_field_pc.c) */

static int wattUse(PartyEntry** party) {
    if (portPm64PartnerOut() != PM64_PARTNER_WATT || *party == NULL) {
        return 0;
    }
    portPm64WattUse(*party);
    return 1;
}

static void wattFollow(PartyEntry* party) {
    portPm64WattFollow(party);
}

/* Lakilester's own field ability instead of Flurrie's Gale Force (pm64_field_pc.c) */
static int lakilesterUse(PartyEntry** party) {
    return portPm64LakilesterUse(*party);
}

/* Bombette back from her blast facing where Mario does (pm64_field_pc.c) */
static void bombetteUsed(PartyEntry* party) {
    portPm64BombetteUsed(party);
}

/* X refused where the PM64 partner out refuses it (pm64_field_pc.c: Parakarry, Sushie): the buzzer, no use */
static int fieldRefuse(BOOL* r) {
    if (portPm64FieldRefuse()) {
        *r = FALSE;
        return 1;
    }
    return 0;
}

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

void portPm64Init(void) {
    static int sDone;
    if (sDone) {
        return;
    }
    sDone = 1;
    portModHostAddHook(1, kSlot_marioStMain, (void*)frameHook);
    portModHostAddHook(0, kSlot_marioPaperOn, (void*)bowNoPaper);
    portModHostAddHook(0, kSlot_marioChgPaper, (void*)bowNoPaperAnim);
    portModHostAddHook(0, kSlot_marioChgPose, (void*)bowStanding);
    portModHostAddHook(0, kSlot_partyPaperOn, (void*)bowNoPartyPaper);
    portModHostAddHook(0, kSlot_partyChgPaper, (void*)bowNoPartyPaperAnim);
    portModHostAddHook(1, kSlot_vivian_use, (void*)bowVeil);
    portModHostAddHook(0, kSlot_chuchu_use, (void*)wattUse);
    portModHostAddHook(1, kSlot_chuchu_move, (void*)wattFollow);
    portModHostAddHook(0, kSlot_cloud_use, (void*)lakilesterUse);
    portModHostAddHook(1, kSlot_bomhei_use, (void*)bombetteUsed);
    portModHostAddHook(0, kSlot_partyDoWork, (void*)fieldRefuse);
    portModHostAddHook(0, kSlot_marioChgPose, (void*)portPm64RidePose); /* Mario still on Sushie's back */
}

void portPm64Frame(void) {
    frameHook();
}
