/* "!" prompts over Mario: is the thing a prompt points at there to act on (port/include/port/prompt_check.h).
 * Also the prompt log ($TTYD_PROMPT_LOG) and the room audit ($TTYD_PROMPT_AUDIT) the tests use. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "port/prompt_check.h"
#include "port/field_remake.h"
#include "port/remake_maps.h"
#include "port/qol.h"
#include "port/platform.h"
#include "system.h"
#include "driver/casedrv.h"
#include "driver/hitdrv.h"
#include "driver/mapdrv.h"
#include "driver/mobjdrv.h"
#include "driver/npcdrv.h"
#include "evt/evt_bero.h"
#include "mario/mario.h"
#include "mario/mariost.h"
#include "manager/evtmgr.h"
#include "driver/seqdrv.h"
#include "win/win_main.h"

/* the bero entry whose exit case this is (the room's entrance table), or NULL */
static BeroEntry* beroOf(const CaseEntry* ce) {
    s32 i;
    for (i = 0; i < 16; i++) {
        if (BeroINFOARR[i] != NULL && BeroINFOARR[i]->hitName != NULL &&
            strcmp(BeroINFOARR[i]->hitName, ce->hitObjName) == 0) {
            return BeroINFOARR[i];
        }
    }
    return NULL;
}

const char* portPromptMissing(HitEntry* hit, const CaseEntry* ce) {
    HitEntry* h;
    if (hit == NULL) {
        return "no collision";
    }
    if (ce == NULL || !(ce->flags & CASE_ENTRY_FLAG_IN_USE)) {
        return "no case";
    }
    /* the collision itself, or one it hangs from, switched off or deleted */
    for (h = hit; h != NULL; h = h->parent) {
        if (h->flags & (HIT_FLAG_OFF | HIT_FLAG_DELETED)) {
            return "collision off";
        }
    }
    if (hitGetAttr(hit) & HIT_ATTR_MOBJ) {
        /* a map object's (a remake pipe, a block): the object there and drawn */
        MapObjectEntry* mobj = hit->mobj;
        if (mobj == NULL || !(mobj->flags & MOBJ_FLAG_USED)) {
            return "object gone";
        }
        if (mobj->flags & MOBJ_FLAG_INVISIBLE) {
            return "object hidden";
        }
        return NULL;
    }
    /* a model's: the model it moves with shown */
    for (h = hit; h != NULL; h = h->parent) {
        if ((h->flags & HIT_FLAG_BOUND) && h->mapObj != NULL && (h->mapObj->flags & MAPOBJ_FLAG_HIDE)) {
            return "model hidden";
        }
    }
    if (ce->conditionId == CASE_COND_STAND_ON_STICK_DOWN && ce->evtCode == bero_case_exec_pipe) {
        /* an entrance table's pipe: still that table's (the room's scripts may have switched it off) */
        if (beroOf(ce) == NULL) {
            return "pipe not in the room's table";
        }
    }
    return NULL;
}

const char* portPromptCaseByA(HitEntry* hit, const CaseEntry* ce) {
    const MarioWork* mario = marioGetPtr();
    const HitEntry* at;
    if (hit == NULL || ce == NULL || mario == NULL || gp == NULL) {
        return "no case";
    }
    /* where casedrv.c's caseMain looks for each A condition */
    switch (ce->conditionId) {
        case CASE_COND_FRONT_A:
            at = mario->hitobjFront;
            break;
        case CASE_COND_PUSH_A:
            at = mario->hitobjPush;
            break;
        case CASE_COND_STAND_ON_A:
        case CASE_COND_STAND_ON_STICK_DOWN: /* (a pipe, by A: remake_pipe_a) */
            at = mario->hitobjStandOn;
            break;
        default:
            return "not an A case";
    }
    if (at == NULL || strcmp(hitGetName((HitEntry*)at), ce->hitObjName) != 0) {
        return "not where A looks"; /* (a wall in reach but not yet pushed, a case on a floor beside his) */
    }
    /* caseMain's own gate */
    if (marioStGetSystemLevel() != 0 || winCheck() || gp->inBattle || N_fbatPreventMarioEventChk() ||
        !marioCaseEventValidChk()) {
        return "case events held";
    }
    return NULL;
}

/* --- the log --- */

static int envOn(const char* name) {
    const char* v = getenv(name);
    return v != NULL && v[0] != '\0' && strcmp(v, "0") != 0;
}

static const char* const kKinds[] = {"-", "front", "push", "pipe", "chest", "npc"};

/* a name for the log: the game's Shift-JIS bytes as hex (\xNN), the rest as is */
static const char* ascii(const char* s) {
    static char buf[2][96];
    static int which;
    char* out = buf[which ^= 1];
    size_t n = 0;
    for (; s != NULL && *s != '\0' && n + 5 < sizeof(buf[0]); s++) {
        const unsigned char c = (unsigned char)*s;
        if (c >= 0x20 && c < 0x7F) {
            out[n++] = (char)c;
        } else {
            n += (size_t)snprintf(out + n, sizeof(buf[0]) - n, "\\x%02x", c);
        }
    }
    out[n] = '\0';
    return out;
}

static void hitBox(HitEntry* hit, Vec* lo, Vec* hi) {
    s32 k, n;
    lo->x = lo->y = lo->z = 1e9f;
    hi->x = hi->y = hi->z = -1e9f;
    for (k = 0; k < hit->vectorCount; k++) {
        const Vec* v[3] = {&hit->vectors[k].v0, &hit->vectors[k].v1, &hit->vectors[k].v2};
        for (n = 0; n < 3; n++) {
            lo->x = v[n]->x < lo->x ? v[n]->x : lo->x, hi->x = v[n]->x > hi->x ? v[n]->x : hi->x;
            lo->y = v[n]->y < lo->y ? v[n]->y : lo->y, hi->y = v[n]->y > hi->y ? v[n]->y : hi->y;
            lo->z = v[n]->z < lo->z ? v[n]->z : lo->z, hi->z = v[n]->z > hi->z ? v[n]->z : hi->z;
        }
    }
}

static int sShownKind; /* what put up the prompt showing now (PORT_PROMPT_*, NONE when none) */

void portPromptFrame(int shown, int kind, HitEntry* hit) {
    static int sLog = -1, sWas;
    static HitEntry* sHit;
    static int sKind;
    MarioWork* mario = marioGetPtr();
    sShownKind = shown ? kind : PORT_PROMPT_NONE;
    if (sLog < 0) {
        sLog = envOn("TTYD_PROMPT_LOG") || envOn("TTYD_PROMPT_AUDIT");
    }
    if (!sLog) {
        return;
    }
    if (shown && (!sWas || hit != sHit || kind != sKind)) {
        const int mobjHit = hit != NULL && (hitGetAttr(hit) & HIT_ATTR_MOBJ);
        const CaseEntry* ce = hit != NULL && !mobjHit ? caseCheckHitObj(hit) : NULL;
        const char* why = NULL;
        if (mobjHit) {
            /* (a map object's own hint: no case) */
            const MapObjectEntry* mobj = hit->mobj;
            why = mobj == NULL || !(mobj->flags & MOBJ_FLAG_USED) ? "object gone"
                  : (mobj->flags & MOBJ_FLAG_INVISIBLE)           ? "object hidden"
                                                                  : NULL;
        } else if (kind == PORT_PROMPT_FRONT || kind == PORT_PROMPT_PUSH || kind == PORT_PROMPT_PIPE) {
            why = portPromptMissing(hit, ce);
            if (why == NULL && ce != NULL && !(ce->flags & CASE_ENTRY_FLAG_BALLOON)) {
                why = portPromptCaseByA(hit, ce); /* (one Paper Cut adds: A fires it now) */
            }
        }
        if (kind == PORT_PROMPT_NPC) {
            /* (the NPC A talks to: which, where, how far) */
            const NpcEntry* npc = fbatNpcTalkCheck();
            const float dx = npc != NULL ? npc->position.x - mario->position.x : 0.0f;
            const float dz = npc != NULL ? npc->position.z - mario->position.z : 0.0f;
            printf("[prompt] shown in %s at %.0f %.0f %.0f: npc '%s' at %.0f %.0f %.0f, %.0f away: %s\n",
                   gp->currentMapName, mario->position.x, mario->position.y, mario->position.z,
                   npc != NULL ? ascii(npc->description) : "-", npc != NULL ? npc->position.x : 0.0f,
                   npc != NULL ? npc->position.y : 0.0f, npc != NULL ? npc->position.z : 0.0f,
                   sqrtf(dx * dx + dz * dz), npc != NULL ? "ok" : "STRAY, no NPC A talks to");
        } else {
            printf("[prompt] shown in %s at %.0f %.0f %.0f: %s, hit '%s', case %d%s: %s%s\n", gp->currentMapName,
                   mario->position.x, mario->position.y, mario->position.z, kKinds[kind < 6 ? kind : 0],
                   hit != NULL ? hitGetName(hit) : "-", ce != NULL ? (int)ce->conditionId : -1,
                   ce != NULL && (ce->flags & CASE_ENTRY_FLAG_BALLOON) ? " (the game's)" : "",
                   why != NULL ? "STRAY, " : "ok", why != NULL ? why : "");
        }
        fflush(stdout);
    }
    sWas = shown;
    sHit = shown ? hit : NULL;
    sKind = shown ? kind : 0;
}

/* --- the room audit ($TTYD_PROMPT_AUDIT) --- */

#define AUDIT_SPOTS 64
static struct {
    int state;     /* 0 waiting for a free Mario, 1 probing, 2 done */
    int wait;      /* frames */
    int n, at;     /* the floor spots to stand on, the one now */
    char spot[AUDIT_SPOTS][32];
    Vec home;      /* where Mario was */
    int shown;     /* the prompt came up on the spot now */
    char standOn[32];
    unsigned settings; /* the prompt and room settings audited with */
} sA;

static int auditOn(void) {
    static int on = -1;
    if (on < 0) {
        on = envOn("TTYD_PROMPT_AUDIT");
    }
    return on;
}

static int free_(void) {
    return seqGetSeq() == SEQ_GAME && !seqCheckSeq() && marioStGetSystemLevel() == 0 && marioChkKey() &&
           marioChkCtrl();
}

/* would a prompt show at this case's spot (Mario in front of, on it) by the settings, the game's flag aside? */
static int promptCase(const CaseEntry* ce) {
    const int flagged = (ce->flags & CASE_ENTRY_FLAG_BALLOON) != 0 || portQol("talk_prompts");
    switch (ce->conditionId) {
        case CASE_COND_FRONT_A:
        case CASE_COND_PUSH_A:
        case CASE_COND_STAND_ON_A:
            return flagged;
        case CASE_COND_STAND_ON_STICK_DOWN:
            return portPipeByA();
        default:
            return 0;
    }
}

static void auditList(void) {
    s32 c;
    sA.n = sA.at = 0;
    for (c = 0; c < 64; c++) {
        const CaseEntry* ce = caseIdToPtr(c);
        HitEntry* hit;
        const char* why;
        Vec lo, hi;
        if (ce == NULL || !promptCase(ce)) {
            continue;
        }
        hit = hitNameToPtr(ce->hitObjName);
        why = portPromptMissing(hit, ce);
        if (hit != NULL) {
            hitBox(hit, &lo, &hi);
        } else {
            lo.x = lo.y = lo.z = hi.x = hi.y = hi.z = 0.0f;
        }
        /* ok: a prompt there; no prompt: what it acts on isn't there now, so none shows (the game's own flagged
         * ones show as the game shows them); LEFTOVER: a case of a thing Paper Cut placed and took away (a map
         * edit's, MOBJ_me...), left behind */
        printf("[prompt] spot %s '%s' case %d%s at %.0f %.0f %.0f: %s%s\n", gp->currentMapName, ce->hitObjName,
               (int)ce->conditionId, (ce->flags & CASE_ENTRY_FLAG_BALLOON) ? " (the game's)" : "",
               (lo.x + hi.x) / 2, hi.y, (lo.z + hi.z) / 2,
               why == NULL                                                    ? "ok"
               : hit == NULL && strncmp(ce->hitObjName, "MOBJ_me", 7) == 0    ? "LEFTOVER, "
               : (ce->flags & CASE_ENTRY_FLAG_BALLOON)                        ? "the game's own, "
                                                                              : "no prompt, ",
               why != NULL ? why : "");
        if (hit != NULL && hit->vectorCount > 0 && sA.n < AUDIT_SPOTS &&
            (ce->conditionId == CASE_COND_STAND_ON_STICK_DOWN || ce->conditionId == CASE_COND_STAND_ON_A)) {
            snprintf(sA.spot[sA.n++], sizeof(sA.spot[0]), "%s", ce->hitObjName);
        }
    }
    fflush(stdout);
}

static void auditPut(const char* name) {
    MarioWork* mario = marioGetPtr();
    HitEntry* hit = hitNameToPtr(name);
    Vec lo, hi;
    if (hit == NULL) {
        return;
    }
    hitBox(hit, &lo, &hi);
    mario->position.x = (lo.x + hi.x) / 2;
    mario->position.y = hi.y + 5.0f;
    mario->position.z = (lo.z + hi.z) / 2;
}

int portPromptAuditBusy(void) {
    return auditOn() && sA.state != 2;
}

void portPromptAuditFrame(void) {
    MarioWork* mario;
    if (!auditOn() || gp == NULL) {
        return;
    }
    if (seqGetSeq() == SEQ_MAP_CHANGE || seqGetSeq() != SEQ_GAME) {
        if (seqGetSeq() == SEQ_MAP_CHANGE) {
            memset(&sA, 0, sizeof(sA)); /* a new room: audited once Mario is free in it */
        }
        return;
    }
    if (gp->inBattle) {
        return;
    }
    mario = marioGetPtr();
    {
        /* a setting that changes the room's things (or its prompts) changed: the room audited again */
        unsigned m = (portQol("talk_prompts") ? 1u : 0u) | (portPipeByA() ? 2u : 0u);
        int i;
        for (i = 0; i < PORT_RM_COUNT; i++) {
            m |= portRemakeMap(i) ? 4u << i : 0u;
        }
        if (sA.state == 2 && m != sA.settings) {
            printf("[prompt] room %s: settings changed, audited again\n", gp->currentMapName);
            sA.state = 0;
            sA.wait = 0;
        }
        sA.settings = m;
    }
    switch (sA.state) {
        case 0:
            if (!free_()) {
                sA.wait = 0;
                return;
            }
            if (++sA.wait < 45) {
                return;
            }
            auditList();
            sA.home = mario->position;
            sA.state = 1;
            sA.wait = -1;
            return;
        case 1:
            if (sA.at >= sA.n) {
                mario->position = sA.home;
                printf("[prompt] room %s: %d floor spots stood on\n", gp->currentMapName, sA.n);
                fflush(stdout);
                sA.state = 2;
                return;
            }
            if (!free_()) {
                return; /* (something fired: let it end) */
            }
            if (sA.wait < 0) {
                auditPut(sA.spot[sA.at]);
                sA.wait = 0;
                sA.shown = 0;
                sA.standOn[0] = '\0';
                return;
            }
            if ((mario->dispFlags & MARIO_DISP_FLAG_BALLOON_ON) &&
                (sShownKind == PORT_PROMPT_PIPE || sShownKind == PORT_PROMPT_PUSH)) {
                sA.shown = 1;
            }
            if (mario->hitobjStandOn != NULL) {
                snprintf(sA.standOn, sizeof(sA.standOn), "%s", hitGetName(mario->hitobjStandOn));
            }
            if (++sA.wait == 15 && sA.shown && envOn("TTYD_PROMPT_AUDIT_SHOTS")) {
                char shot[64];
                snprintf(shot, sizeof(shot), "prompt_%s_%s", gp->currentMapName, sA.spot[sA.at]);
                portShotRequest(shot); /* (to look at: is the thing there) */
            }
            if (sA.wait < 20) {
                return;
            }
            printf("[prompt] stood %s '%s' (on '%s'): %s\n", gp->currentMapName, sA.spot[sA.at], sA.standOn,
                   sA.shown ? "shown" : "none");
            fflush(stdout);
            sA.at++;
            sA.wait = -1;
            return;
        default:
            return;
    }
}
