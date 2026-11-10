/* Dungeon master (port/include/port/dungeon_master.h, docs/DUNGEON_MASTER.md):
 * another player plays the enemies' side in the story's battles.
 *
 * Each enemy's turn is its attack event (BattleUnit.attackEventCode, started
 * in battle_seq.c's act sequence). At the event's first user function, a look
 * ahead copies the event (and the events waiting on it) into scratch entries
 * and runs them on: the flow and the arithmetic by the game's own handlers
 * (portEvtLookStep, evtmgr_cmd.c), the battle's getters for real on the
 * scratch entry, everything else skipped (its variables marked unknown).
 * Every choice the script makes by chance (evt_sub_random,
 * btlevtcmd_GetRandomValue, btlevtcmd_DrawLots, btlevtcmd_EnemyItemUseCheck)
 * is followed each way it can go, to the move the way ends on (the weapon it
 * declares, pays for or hits with; a call for help). Those are the moves the
 * player is offered, each with the choices that lead to it.
 *
 * The player's pick then stands in for the script's chances, in the order the
 * script asks for them (each checked against the command the look ahead saw
 * ask), and their target for the one btlevtcmd_ChoiceSamplingEnemy would
 * weigh; the rest of the script runs as the game's. When the look ahead finds
 * a single way (a boss's set opening, a phase it keeps to), the script's move
 * stands and the player is told so; where it can't see (a C function of the
 * enemy's own deciding), "Enemy's choice" leaves it to the script.
 *
 * The wind-up: once the attack is declared and the enemy has moved up, the
 * first wait before its hit is held while the player holds A (up to HOLD_MAX
 * frames, once a turn), so Mario's player has to read the strike; the strike
 * itself plays at the game's speed after it.
 *
 * Everything here reads the frame's controllers as played (online: the
 * session's; replays: the recording's) and the battle's own state, in the
 * game's frame, so every game of an online session decides the same; the
 * drawing keeps the game's random numbers as they were. */
#include "port/dungeon_master.h"
#include "port/hook_list.h"
#include "port/statesync.h"
#include "port/mod_host.h"
#include "port/platform.h"
#include "port/replay.h"
#include "port/settings.h"
#include "port/tr.h"
#include "port/versus.h"
#include "battle/battle.h"
#include "battle/battle_disp.h"
#include "battle/battle_event_cmd.h"
#include "battle/battle_status_effect.h"
#include "battle/battle_sub.h"
#include "battle/battle_unit.h"
#include "data/item_data.h"
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "driver/icondrv.h"
#include "driver/msgdrv.h"
#include "driver/windowdrv.h"
#include "evt/evt_cmd.h"
#include "evt/evt_sub.h"
#include "icon_ids.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "manager/fontmgr.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "pmario_sound.h"
#include "system.h"

#include <dolphin/gx.h>
#include <dolphin/pad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

extern PADStatus Pad[PAD_MAX_CONTROLLERS]; /* DEMOPad.c: the frame's controllers (online and replays: as played) */
s32 portStatusIconId(s32 statusType);      /* battle_status_icon.c */

int gPortDmLive;

#define STICK_UP 0x1000 /* DEMOPad's stick directions (gp->dirs) */
#define STICK_DOWN 0x2000
#define STICK_LEFT 0x4000
#define STICK_RIGHT 0x8000

#define MAX_OPTS 10     /* moves offered (an enemy's own are fewer) */
#define MAX_CHOICES 12  /* chances on the way to one */
#define MAX_TARGETS 12
#define SIM_DEPTH 8     /* events deep (the attack event's children) */
#define SIM_STEPS 4000  /* commands a way may take */
#define SIM_RUNS 1500   /* ways looked at, a turn */
#define MAX_CONSTS 32
#define HOLD_MAX 45     /* frames the wind-up can be held (0.75 s) */
#define BANNER_FRAMES 150

/* --- the setting and the player --- */

static int* sSetting;

int portDmPlayer(void) {
    if (sSetting == NULL) {
        sSetting = portSettingsDeclare("dungeon_master", 0, 0, 3);
    }
    return *sSetting;
}

/* a shared online battle: the dungeon master's seat's player's place among those who came, -1 when that
 * player isn't in the battle (every game works it out alike: the battle's places, the session's seats) */
static int dmPlace(void) {
    return portStateSyncSeatPlace(portDmPlayer() + 1);
}

/* the controller the dungeon master's choices are read from: its seat's (portDmPlayer) on one computer;
 * in a shared battle the channel of the character its place plays (the battle's controllers cross by
 * character: that channel is its place's controller, which on its own game is this computer's), -1 none */
static int dmChannel(void) {
    if (portStateSyncSharedBattle()) {
        const int place = dmPlace();
        return place >= 0 ? portStateSyncBattleCharacterOf(place) : -1;
    }
    return portDmPlayer();
}

/* the dungeon master's controller is there: online, its seat's player in the shared battle (never
 * another place in its stead); a replay's own; else a controller in that port ($TTYD_P2_PRESS's for
 * player 2, tests) */
static int padHere(int player) {
    if (player < 1 || player >= PAD_MAX_CONTROLLERS) {
        return 0;
    }
    if (portStateSyncSharedBattle()) {
        /* "sync what matters" online: a shared battle's players (each their own game; the battle's
           controllers cross, port/include/port/statesync.h). By the seat's player, not the seat number:
           the places are those who came, in order */
        return dmPlace() >= 0;
    }
    if (portReplayActive()) {
        return portReplayPadConnected(player);
    }
    /* (whatever is in the port: a controller, the keyboard a seat swap took there, a test's scripted one) */
    return portInputHasAny(player);
}

/* the dungeon master's controller's buttons this frame (gp's held, new or repeating), 0 with none */
static u32 dmButtons(const u32* buttons) {
    const int c = dmChannel();
    return c >= 0 && c < PAD_MAX_CONTROLLERS ? buttons[c] : 0;
}

/* the battle has a dungeon master: the setting, a story battle (not a versus fight) */
static int battleOn(void) {
    const int p = portDmPlayer();
    return p > 0 && gp != NULL && gp->inBattle && _battleWorkPointer != NULL && !portVersusOn();
}

/* gPortDmLive: 1 on; 2 off but a held call's first run still owed (sOwed below: evtmgr_cmd.c's hooks
   still call in for it) */
#define DM_ON 1
#define DM_OWED 2

/* player: a controller channel (in a shared battle a character's, as second_player_pc.c's owners are;
   crowd_seats_pc.c passes its place's character) */
int portDmPlaysEnemies(int player) {
    return gPortDmLive == DM_ON && player > 0 && player == dmChannel() && padHere(portDmPlayer());
}

/* --- the moves --- */

enum { OPT_WEAPON, OPT_SPAWN, OPT_OTHER, OPT_AI };

typedef struct Choice {
    EvtWord* pc;  /* the command that asks (the look ahead saw it) */
    EvtWord value;
} Choice;

typedef struct Target {
    s16 unit, part;
    Vec pos; /* the cursor's point (BattleWorkTarget's) */
} Target;

typedef struct Option {
    int kind;
    BattleWeapon* weapon;
    void* key;               /* what makes two ways one move: the weapon, or the script it ends in */
    Choice choices[MAX_CHOICES];
    int nChoices;
    double share;            /* how often the script's own chances come here (0..1) */
    EvtWord* choicePc;       /* the btlevtcmd_ChoiceSamplingEnemy that picks its target, or NULL */
    Target targets[MAX_TARGETS];
    int nTargets;
} Option;

enum { PH_IDLE, PH_MENU, PH_TARGET, PH_RUN, PH_LIVE };

static struct {
    int evtId;               /* the attack event decided (its eventId), 0 none */
    int unitId;
    int phase;
    EventEntry* menuEntry;   /* the entry held at the decision */
    int realPending;         /* its call hasn't run yet */
    Option opts[MAX_OPTS + 1];
    int nOpts;               /* with "Enemy's choice" last */
    int scripted;            /* the look ahead found one way only */
    double unknown;          /* the share it couldn't follow */
    int cursor, top;
    int picked;              /* the move chosen (an index), -1 none */
    int aiAll;               /* the enemy's own choice: its move and targets the script's */
    int forcedAt;            /* the next of its choices */
    int target;              /* the target chosen (an index into its targets), -1 none */
    int choiceDone;          /* its target choice was made */
    /* a target asked for as the script runs (a second hit, or a turn the script decides) */
    EventEntry* liveEntry;
    Target live[MAX_TARGETS];
    int nLive;
    BattleWeapon* liveWeapon;
    /* the wind-up */
    int armed, moved, hit, held, holdFrames;
    int frames;              /* frames in the menu */
    const char* banner;      /* a word for the player (English, translated as drawn) */
    int bannerAge;
    char bannerName[64];     /* the move it is about (its name first), or "" */
    int noPadTold;
} sDm;

/* the call held for the menu (portDmUserFunc returned BLOCK on its first call, so the function itself hasn't
   run yet): kept past resetTurn and the dungeon master going, so its first run (isFirstCall) still comes */
static struct {
    EventEntry* entry;
    void* pc;
    UserFunction fn;
    s32 id; /* its event's id: kept while that event lives (never timed: a frame with the event manager
               stopped, statusWinMain still running, would have given up on it and lost its first run) */
} sOwed;

static void resetTurn(void) {
    sDm.evtId = 0;
    sDm.unitId = -1;
    sDm.phase = PH_IDLE;
    sDm.menuEntry = NULL;
    sDm.realPending = 0;
    sDm.nOpts = 0;
    sDm.picked = -1;
    sDm.aiAll = 0;
    sDm.forcedAt = 0;
    sDm.target = -1;
    sDm.choiceDone = 0;
    sDm.liveEntry = NULL;
    sDm.nLive = 0;
    sDm.armed = sDm.moved = sDm.hit = sDm.held = sDm.holdFrames = 0;
}

static void say(const char* english) {
    sDm.banner = english;
    sDm.bannerAge = 0;
    sDm.bannerName[0] = 0;
    printf("[dm] %s\n", english);
    fflush(stdout);
}

/* --- the look ahead --- */

typedef struct SimFrame {
    EventEntry e;
    u32 taint;   /* LW(n) not known (bit n) */
    int lfTaint; /* LF not known */
    u32 derived; /* LW(n) holds the chance being probed (bit n) */
    EvtWord* source[16]; /* the call that left LW(n) unknown (its command) */
} SimFrame;

enum { R_NEED, R_DONE, R_END, R_UNKNOWN, R_RETRY };

/* Calls of the enemy's own (a C function of its file: kamec_get_live_num) whose
 * answer the script branches on: tried for real on the scratch entry, and
 * kept only when the battle is as it was after it (a getter); else put back
 * and left unknown. By their command, for the turn. */
#define MAX_TRUST 8
static EvtWord* sTrust[MAX_TRUST];
static int sTrustOk[MAX_TRUST]; /* 1 a getter, 0 not yet tried, -1 not one */
static int sTrustN;

static int trustIndex(EvtWord* pc) {
    int i;
    for (i = 0; i < sTrustN; i++) {
        if (sTrust[i] == pc) {
            return i;
        }
    }
    return -1;
}

/* the battle as a call found it, to tell a getter from anything else */
static u8* sSave;
static size_t sSaveSize;

static size_t battleBytes(void) {
    return sizeof(BattleWork) + sizeof(BattleUnit) * BATTLE_UNIT_MAX;
}

static void battleSave(u8* out) {
    BattleWork* wp = _battleWorkPointer;
    int i;
    memcpy(out, wp, sizeof(BattleWork));
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        BattleUnit* u = wp->units[i];
        if (u != NULL) {
            memcpy(out + sizeof(BattleWork) + sizeof(BattleUnit) * (size_t)i, u, sizeof(BattleUnit));
        }
    }
}

/* the units are the same ones (none made or removed): only then can the saved ones be compared and put
   back (a removed one's saved pointer is freed memory, a made one isn't in the save) */
static int unitsKept(const u8* saved) {
    return memcmp(((const BattleWork*)saved)->units, _battleWorkPointer->units, sizeof(_battleWorkPointer->units)) == 0;
}

/* 1 when the battle is as saved; else it is put back as saved (the units the same ones: unitsKept) */
static int battleSame(const u8* saved) {
    BattleWork* wp = _battleWorkPointer;
    int i, same = memcmp(saved, wp, sizeof(BattleWork)) == 0;
    for (i = 0; i < BATTLE_UNIT_MAX && same; i++) {
        BattleUnit* u = ((const BattleWork*)saved)->units[i];
        if (u != NULL && memcmp(saved + sizeof(BattleWork) + sizeof(BattleUnit) * (size_t)i, u, sizeof(BattleUnit)) != 0) {
            same = 0;
        }
    }
    if (!same) {
        memcpy(wp, saved, sizeof(BattleWork));
        for (i = 0; i < BATTLE_UNIT_MAX; i++) {
            BattleUnit* u = wp->units[i];
            if (u != NULL) {
                memcpy(u, saved + sizeof(BattleWork) + sizeof(BattleUnit) * (size_t)i, sizeof(BattleUnit));
            }
        }
    }
    return same;
}

static int liveEvents(void) {
    const EventWork* w = evtGetWork();
    int i, n = 0;
    for (i = 0; i < w->count; i++) {
        n += (w->entries[i].flags & EVENT_ACTIVE) != 0;
    }
    return n;
}

/* the enemy calls found to do more than the battle's numbers can put back (make or remove a unit, start
   an event): never tried again, any turn or battle (by the function) */
#define MAX_UNSAFE 16
static UserFunction sUnsafe[MAX_UNSAFE];
static int sUnsafeN;
static int sLookAheadSpoilt; /* this turn's look ahead stopped: the battle isn't as it was */

static int unsafeFn(UserFunction fn) {
    int i;
    for (i = 0; i < sUnsafeN; i++) {
        if (sUnsafe[i] == fn) {
            return 1;
        }
    }
    return 0;
}

/* an enemy's own call, for real on the scratch entry: 1 when it only read the battle. What it changes is
   put back: the battle's work and units, the global words and flags. One that made or removed a unit
   can't be put back safely (the units as saved may be freed memory): the battle is left as the call left
   it, the look ahead stops for the turn, and the function is never tried again. One that started an
   event is put back, and never tried again either. */
static int trustedCall(EventEntry* e, UserFunction fn) {
    static EvtWord sGw[EVT_GW_COUNT];
    static s32 sGf[EVT_GF_COUNT / 32];
    EventWork* w = evtGetWork();
    int r, events, same;
    if (unsafeFn(fn) || sLookAheadSpoilt) {
        return 0;
    }
    if (sSave == NULL) {
        sSaveSize = battleBytes();
        sSave = (u8*)malloc(sSaveSize);
        if (sSave == NULL) {
            return 0;
        }
    }
    battleSave(sSave);
    memcpy(sGw, w->gwData, sizeof(sGw));
    memcpy(sGf, w->gfData, sizeof(sGf));
    events = liveEvents();
    e->blocked = 1;
    r = fn(e, TRUE);
    memcpy(w->gwData, sGw, sizeof(sGw));
    memcpy(w->gfData, sGf, sizeof(sGf));
    if (!unitsKept(sSave)) {
        sLookAheadSpoilt = 1;
        if (sUnsafeN < MAX_UNSAFE) {
            sUnsafe[sUnsafeN++] = fn;
        }
        printf("[dm] look ahead: an enemy call made or removed a unit; stopped for this turn\n");
        fflush(stdout);
        return 0;
    }
    same = battleSame(sSave);
    if (liveEvents() != events) {
        if (sUnsafeN < MAX_UNSAFE) {
            sUnsafe[sUnsafeN++] = fn;
        }
        printf("[dm] look ahead: an enemy call started an event; not tried again\n");
        fflush(stdout);
        return 0;
    }
    return same && r == EVT_RETURN_DONE;
}

typedef struct SimOut {
    /* R_NEED: a chance */
    EvtWord* pc;
    int needKind;            /* 0 a number below range, 1 a draw by weights, 2 an item or not */
    int range;
    int weights[16];
    EvtWord itemScript;
    /* R_DONE */
    int kind;
    BattleWeapon* weapon;
    void* key;
    EvtWord* choicePc;
    Target targets[MAX_TARGETS];
    int nTargets;
    /* the probe's numbers the chance was compared with */
    int consts[MAX_CONSTS];
    int nConsts;
    int opaque;
} SimOut;

static SimFrame sStart[SIM_DEPTH];
static int sStartDepth;
static SimFrame sFr[SIM_DEPTH];
static int sRuns;

static int isLw(EvtWord w) {
    return w > EVTDAT_GW_MAX && w <= EVTDAT_LW_MAX;
}
static int lwIndex(EvtWord w) {
    return (int)(w + EVTDAT_LW_BASE);
}
static int isLf(EvtWord w) {
    return w > EVTDAT_GF_MAX && w <= EVTDAT_LF_MAX;
}
/* a reference to something a call could write outside the entry (UW, UF, GW, GF, the save's flags and bytes) */
static int isOutside(EvtWord w) {
    return w > EVTDAT_FLOAT_MAX && w <= EVTDAT_GW_MAX && !isLf(w);
}
static int tainted(const SimFrame* f, EvtWord w) {
    if (isLw(w)) {
        return (f->taint >> lwIndex(w)) & 1;
    }
    if (isLf(w)) {
        return f->lfTaint;
    }
    return 0;
}
static int derived(const SimFrame* f, EvtWord w) {
    return isLw(w) && ((f->derived >> lwIndex(w)) & 1);
}
static void markOut(SimFrame* f, EvtWord w, int unknown);

/* a branch reads an unknown: retry the way with the call that left it so tried for real, if it can be */
static int unknownAt(int line, const EventEntry* e);
static int unknownBranch(SimFrame* f, EvtWord w) {
    EvtWord* src = isLw(w) ? f->source[lwIndex(w)] : NULL;
    const int i = src != NULL ? trustIndex(src) : -1;
    if (src != NULL && i < 0 && sTrustN < MAX_TRUST) {
        sTrust[sTrustN] = src;
        sTrustOk[sTrustN++] = 0;
        return R_RETRY;
    }
    return unknownAt(__LINE__, &f->e);
}

static void markOut(SimFrame* f, EvtWord w, int unknown) {
    if (isLw(w)) {
        const u32 bit = 1u << lwIndex(w);
        f->taint = unknown ? f->taint | bit : f->taint & ~bit;
        f->derived &= ~bit;
    } else if (isLf(w) && unknown) {
        f->lfTaint = 1;
    }
}

/* the battle's getters: they read the game and write their arguments only.
 * unit: the first argument is a unit (checked before the call) */
typedef struct Getter {
    UserFunction fn;
    int unit;
} Getter;
static const Getter kGetters[] = {
    {btlevtcmd_CheckActStatus, 1},
    {btlevtcmd_CheckAttribute, 1},
    {btlevtcmd_CheckPartsAttribute, 1},
    {btlevtcmd_CheckPartsCounterAttribute, 1},
    {btlevtcmd_CheckStatus, 1},
    {btlevtcmd_CheckToken, 1},
    {btlevtcmd_CheckUnitFlag, 1},
    {btlevtcmd_GetBodyId, 1},
    {btlevtcmd_GetEnemyBelong, 1},
    {btlevtcmd_GetFriendBelong, 1},
    {btlevtcmd_GetFaceDirection, 1},
    {btlevtcmd_GetFp, 1},
    {btlevtcmd_GetMaxFp, 1},
    {btlevtcmd_GetHp, 1},
    {btlevtcmd_GetMaxHp, 1},
    {btlevtcmd_GetHeight, 1},
    {btlevtcmd_GetWidth, 1},
    {btlevtcmd_GetHomePos, 1},
    {btlevtcmd_GetPos, 1},
    {btlevtcmd_GetPosFloat, 1},
    {btlevtcmd_GetRotate, 1},
    {btlevtcmd_GetScale, 1},
    {btlevtcmd_GetStatusMg, 1},
    {btlevtcmd_GetUnitKind, 1},
    {btlevtcmd_GetUnitWork, 1},
    {btlevtcmd_GetUnitWorkFloat, 1},
    {btlevtcmd_GetOverTurnCount, 1},
    {btlevtcmd_GetProtectId, 1},
    {btlevtcmd_GetMarioId, 1},
    {btlevtcmd_GetPartyId, 1},
    {btlevtcmd_check_battleflag, 0},
    {btlevtcmd_GetFirstAttackTarget, 0},
    {btlevtcmd_get_turn, 0},
    {btlevtcmd_GetSelectEnemy, 0},
    {btlevtcmd_CheckPhase, 0},
    {btlevtcmd_GetStageSize, 0},
    {btlevtcmd_CheckSpace, 0},
};

static const Getter* getter(UserFunction fn) {
    size_t i;
    for (i = 0; i < sizeof(kGetters) / sizeof(kGetters[0]); i++) {
        if (kGetters[i].fn == fn) {
            return &kGetters[i];
        }
    }
    return NULL;
}

static int moveFn(UserFunction fn) {
    return fn == btlevtcmd_MovePosition || fn == btlevtcmd_JumpPosition || fn == btlevtcmd_DivePosition ||
           fn == btlevtcmd_FallPosition || fn == btlevtcmd_EllipseMovePosition || fn == btlevtcmd_JumpPartsPosition;
}

static int choiceFn(UserFunction fn) {
    return fn == evt_sub_random || fn == btlevtcmd_GetRandomValue || fn == btlevtcmd_DrawLots ||
           fn == btlevtcmd_EnemyItemUseCheck;
}

static BattleUnit* unitById(s32 id) {
    if (id < 0 || id >= BATTLE_UNIT_MAX) {
        return NULL;
    }
    return BattleGetUnitPtr(_battleWorkPointer, id);
}

/* the targets now sampled (btlevtcmd_SamplingEnemy's), as the menu offers them */
static int sampledTargets(Target* out) {
    const BattleWorkWeaponTargets* t = &_battleWorkPointer->weaponTargets;
    int i, n = 0;
    for (i = 0; i < t->numTargets && n < MAX_TARGETS; i++) {
        const BattleWorkTarget* w = &t->targets[t->targetIndices[i]];
        out[n].unit = w->unitIdx;
        out[n].part = w->partIdx;
        out[n].pos = (Vec){(f32)w->posX, (f32)w->posY, (f32)w->posZ};
        n++;
    }
    return n;
}

/* the held item's own script, when the enemy has one it could use in battle (0: none) */
static EvtWord itemScriptOf(BattleUnit* unit) {
    const s32 item = unit != NULL ? unit->heldItem : 0;
    BattleWeapon* w;
    if (item <= 0 || item >= ITEM_TYPE_MAX) {
        return 0;
    }
    w = itemDataTable[item].battle_params;
    return w != NULL && w->attackEvtScript != NULL ? (EvtWord)w->attackEvtScript : 0;
}

/* a label of the frame's script, from its start (GOTO) */
static EvtWord* findLabel(EvtWord* start, EvtWord id) {
    EvtWord* cmd = start;
    int guard;
    for (guard = 0; cmd != NULL && guard < 20000; guard++) {
        const int op = (u8)*cmd;
        const int n = (int)(*cmd >> 16);
        if (op == OPCODE_END_SCRIPT) {
            return NULL;
        }
        if (op == OPCODE_LABEL && n >= 1 && cmd[1] == id) {
            return cmd + 1 + n;
        }
        cmd += 1 + n;
    }
    return NULL;
}

/* past an inline or brother block (its commands run beside the script, not in it) */
static EvtWord* skipBlock(EvtWord* cmd) {
    int depth = 0, guard;
    for (guard = 0; guard < 20000; guard++) {
        const int op = (u8)*cmd;
        const int n = (int)(*cmd >> 16);
        cmd += 1 + n;
        if (op == OPCODE_INLINE_EVENT || op == OPCODE_INLINE_EVENT_ID || op == OPCODE_BROTHER_EVENT ||
            op == OPCODE_BROTHER_EVENT_ID) {
            depth++;
        } else if (op == OPCODE_END_INLINE || op == OPCODE_END_BROTHER) {
            if (depth == 0) {
                return cmd;
            }
            depth--;
        } else if (op == OPCODE_END_SCRIPT) {
            return NULL;
        }
    }
    return NULL;
}

static void addConst(SimOut* out, int v) {
    int i;
    for (i = 0; i < out->nConsts; i++) {
        if (out->consts[i] == v) {
            return;
        }
    }
    if (out->nConsts < MAX_CONSTS) {
        out->consts[out->nConsts++] = v;
    } else {
        out->opaque = 1;
    }
}

/* why the first way the look ahead couldn't follow stopped (the log's) */
static int sWhyLine;
static EvtWord* sWhyPc;

static int unknownAt(int line, const EventEntry* e) {
    if (sWhyLine == 0) {
        sWhyLine = line;
        sWhyPc = e != NULL ? (EvtWord*)e->currCommand : NULL;
    }
    return R_UNKNOWN;
}

/* One way through, from the snapshot, with these chances (the last, probe: its
 * numbers compared are collected). */
static int simRun(const Choice* forced, int nForced, int probe, SimOut* out) {
    int depth = sStartDepth, used = 0, steps;
    BattleWeapon* sampled = NULL;
    EvtWord* choicePc = NULL;
    BattleWeapon* choiceWeapon = NULL;
    Target choiceTargets[MAX_TARGETS];
    int nChoiceTargets = 0;
    int switchDerived[8];
    memset(out, 0, sizeof(*out));
    memset(switchDerived, 0, sizeof(switchDerived));
    memcpy(sFr, sStart, sizeof(SimFrame) * (size_t)sStartDepth);
    sRuns++;
    if (sLookAheadSpoilt) {
        return unknownAt(__LINE__, NULL); /* (the battle changed under it: trustedCall) */
    }
    for (steps = 0; steps < SIM_STEPS; steps++) {
        SimFrame* f = &sFr[depth - 1];
        EventEntry* e = &f->e;
        EvtWord* cmd = (EvtWord*)e->nextCommand;
        const int op = (u8)*cmd;
        const int n = (int)(*cmd >> 16);
        EvtWord* a = cmd + 1;
        int r, k;
        /* what the command reads that isn't known, and the probe's comparisons */
        if ((op >= OPCODE_IF_STR_EQUAL && op <= OPCODE_IF_NOT_FLAG) || op == OPCODE_SWITCH ||
            (op >= OPCODE_CASE_EQUAL && op <= OPCODE_CASE_BETWEEN && op != OPCODE_CASE_ETC && op != OPCODE_CASE_END)) {
            int anyDerived = 0;
            const int sd = e->switchDepth >= 0 && e->switchDepth < 8 ? e->switchDepth : 0;
            for (k = 0; k < n && k < 2; k++) {
                if (tainted(f, a[k])) {
                    return unknownBranch(f, a[k]);
                }
                anyDerived |= derived(f, a[k]);
            }
            if (op == OPCODE_SWITCH) {
                switchDerived[e->switchDepth + 1 >= 0 && e->switchDepth + 1 < 8 ? e->switchDepth + 1 : 0] = anyDerived;
            } else if (op >= OPCODE_CASE_EQUAL) {
                if (switchDerived[sd] && op != OPCODE_CASE_FLAG) {
                    for (k = 0; k < n && k < 2; k++) {
                        addConst(out, (int)evtGetValue(e, a[k]));
                    }
                } else if (switchDerived[sd]) {
                    out->opaque = 1;
                }
            } else if (anyDerived) {
                if (op == OPCODE_IF_FLAG || op == OPCODE_IF_NOT_FLAG || op < OPCODE_IF_EQUAL) {
                    out->opaque = 1;
                } else {
                    for (k = 0; k < 2; k++) {
                        if (!derived(f, a[k])) {
                            addConst(out, (int)evtGetValue(e, a[k]));
                        }
                    }
                }
            }
        } else if (op >= OPCODE_SET && op <= OPCODE_DIVF && n >= 2) {
            const int src = tainted(f, a[1]), dst = tainted(f, a[0]);
            const int plain = op == OPCODE_SET || op == OPCODE_SETF;
            if (derived(f, a[1]) || (!plain && derived(f, a[0]))) {
                if (plain && isLw(a[0])) {
                    f->derived |= 1u << lwIndex(a[0]);
                    f->taint &= ~(1u << lwIndex(a[0]));
                    goto step; /* a copy of the chance: still compared as it */
                }
                out->opaque = 1;
            }
            if (isLw(a[0])) {
                f->derived &= ~(1u << lwIndex(a[0]));
            }
            {
                /* (an unknown copied or worked on stays unknown, from the same call) */
                EvtWord* from = src && isLw(a[1]) ? f->source[lwIndex(a[1])]
                                : isLw(a[0])      ? f->source[lwIndex(a[0])]
                                                  : NULL;
                markOut(f, a[0], op == OPCODE_SETI ? 0 : plain ? src : (src || dst));
                if (isLw(a[0]) && tainted(f, a[0])) {
                    f->source[lwIndex(a[0])] = from;
                }
            }
        }
    step:
        r = portEvtLookStep(e);
        if (r == -1) {
            continue;
        }
        switch (r) {
            case OPCODE_USER_FUNC: {
                UserFunction fn = (UserFunction)evtGetValue(e, e->args[0]);
                EvtWord* args = e->args + 1;
                const int argc = e->params - 1;
                EvtWord* pc = (EvtWord*)e->currCommand;
                const Getter* g;
                e->args = args;
                e->params = (u8)argc;
                if (choiceFn(fn)) {
                    EvtWord dst;
                    if (fn == btlevtcmd_DrawLots) {
                        dst = args[0];
                    } else {
                        dst = args[1];
                    }
                    if (used < nForced) {
                        if (forced[used].pc != pc) {
                            return unknownAt(__LINE__, e);
                        }
                        if (isLw(dst) || isLf(dst)) {
                            evtSetValue(e, dst, forced[used].value);
                        }
                        markOut(f, dst, 0);
                        if (used == probe && isLw(dst)) {
                            f->derived |= 1u << lwIndex(dst);
                        }
                        used++;
                        continue;
                    }
                    for (k = 0; k < argc; k++) {
                        if (k != (fn == btlevtcmd_DrawLots ? 0 : 1) && tainted(f, args[k])) {
                            return unknownAt(__LINE__, e);
                        }
                    }
                    out->pc = pc;
                    if (fn == evt_sub_random) {
                        out->needKind = 0;
                        out->range = (int)evtGetValue(e, args[0]) + 1;
                    } else if (fn == btlevtcmd_GetRandomValue) {
                        out->needKind = 0;
                        out->range = (int)evtGetValue(e, args[0]);
                    } else if (fn == btlevtcmd_DrawLots) {
                        int c = (int)evtGetValue(e, args[1]);
                        out->needKind = 1;
                        if (c > 16) {
                            c = 16;
                        }
                        out->range = c;
                        for (k = 0; k < c && k + 2 < argc; k++) {
                            out->weights[k] = (int)evtGetValue(e, args[2 + k]);
                        }
                    } else {
                        out->needKind = 2;
                        out->itemScript = itemScriptOf(unitById(BattleTransID(e, (s32)evtGetValue(e, args[0]))));
                        out->range = out->itemScript != 0 ? 2 : 1;
                    }
                    if (out->range < 1) {
                        out->range = 1;
                    }
                    return R_NEED;
                }
                if (fn == btlevtcmd_SamplingEnemy) {
                    for (k = 0; k < argc; k++) {
                        /* (one written outside the event, GW/GF/UW, would stay written: the real call only
                           with LW/LF) */
                        if (tainted(f, args[k]) || (k > 0 && isOutside(args[k]))) {
                            return unknownAt(__LINE__, e);
                        }
                    }
                    if (unitById(BattleTransID(e, (s32)evtGetValue(e, args[0]))) == NULL) {
                        return unknownAt(__LINE__, e);
                    }
                    fn(e, TRUE);
                    sampled = (BattleWeapon*)evtGetValue(e, args[2]);
                    continue;
                }
                if (fn == btlevtcmd_ChoiceSamplingEnemy) {
                    const BattleWorkWeaponTargets* t = &_battleWorkPointer->weaponTargets;
                    if (argc < 3 || isOutside(args[1]) || isOutside(args[2])) {
                        return unknownAt(__LINE__, e); /* (its answer written outside the event: not here) */
                    }
                    choicePc = pc;
                    choiceWeapon = (BattleWeapon*)evtGetValue(e, args[0]);
                    nChoiceTargets = sampledTargets(choiceTargets);
                    /* (as the AI's first candidate: the script only asks whether there is one) */
                    if (t->numTargets > 0) {
                        const BattleWorkTarget* w = &t->targets[t->targetIndices[0]];
                        evtSetValue(e, args[1], w->unitIdx);
                        evtSetValue(e, args[2], w->partIdx);
                    } else {
                        evtSetValue(e, args[1], -1);
                        evtSetValue(e, args[2], 0);
                    }
                    markOut(f, args[1], 0);
                    markOut(f, args[2], 0);
                    continue;
                }
                if (fn == btlevtcmd_PayWeaponCost || fn == btlevtcmd_WeaponAftereffect || fn == btlevtcmd_CheckDamage ||
                    fn == btlevtcmd_PreCheckDamage || fn == btlevtcmd_AttackDeclare || fn == btlevtcmd_AttackDeclareAll ||
                    fn == btlevtcmd_SpawnUnit) {
                    BattleWeapon* w = NULL;
                    if (fn == btlevtcmd_PayWeaponCost) {
                        w = (BattleWeapon*)evtGetValue(e, args[1]);
                    } else if (fn == btlevtcmd_WeaponAftereffect) {
                        w = (BattleWeapon*)evtGetValue(e, args[0]);
                    } else if (fn == btlevtcmd_CheckDamage || fn == btlevtcmd_PreCheckDamage) {
                        w = (BattleWeapon*)evtGetValue(e, args[3]);
                    } else if (fn != btlevtcmd_SpawnUnit) {
                        w = choiceWeapon != NULL ? choiceWeapon : sampled;
                    }
                    if (fn == btlevtcmd_SpawnUnit) {
                        out->kind = OPT_SPAWN;
                        out->key = e->restartFrom;
                    } else if (w != NULL) {
                        out->kind = OPT_WEAPON;
                        out->weapon = w;
                        out->key = w;
                    } else {
                        out->kind = OPT_OTHER;
                        out->key = e->restartFrom;
                    }
                    if (w != NULL && choicePc != NULL && choiceWeapon == w) {
                        out->choicePc = choicePc;
                        memcpy(out->targets, choiceTargets, sizeof(choiceTargets));
                        out->nTargets = nChoiceTargets;
                    }
                    return R_DONE;
                }
                g = getter(fn);
                if (g != NULL) {
                    int ok = 1;
                    for (k = 0; k < argc; k++) {
                        if (tainted(f, args[k]) || isOutside(args[k])) {
                            ok = 0;
                        }
                    }
                    if (ok && g->unit && unitById(BattleTransID(e, (s32)evtGetValue(e, args[0]))) == NULL) {
                        ok = 0;
                    }
                    if (ok) {
                        e->blocked = 1;
                        if (fn(e, TRUE) == EVT_RETURN_DONE) {
                            for (k = 0; k < argc; k++) {
                                markOut(f, args[k], 0); /* (an argument it read stays known; one it wrote is) */
                            }
                            continue;
                        }
                    }
                }
                /* an enemy's own call whose answer a branch reads: for real, if it only reads */
                {
                    const int t = trustIndex(pc);
                    if (t >= 0 && sTrustOk[t] >= 0) {
                        int ok = 1;
                        for (k = 0; k < argc; k++) {
                            if (tainted(f, args[k]) || isOutside(args[k])) {
                                ok = 0;
                            }
                        }
                        if (ok && trustedCall(e, fn)) {
                            sTrustOk[t] = 1;
                            for (k = 0; k < argc; k++) {
                                markOut(f, args[k], 0);
                            }
                            continue;
                        }
                        sTrustOk[t] = -1;
                    }
                }
                /* anything else: skipped, what it might have written unknown */
                for (k = 0; k < argc; k++) {
                    markOut(f, args[k], 1);
                    if (isLw(args[k])) {
                        f->source[lwIndex(args[k])] = pc;
                    }
                }
                continue;
            }
            case OPCODE_RUN_CHILD_EVENT: {
                EvtWord* script = (EvtWord*)evtGetValue(e, e->args[0]);
                SimFrame* c;
                if (tainted(f, e->args[0]) || script == NULL || depth >= SIM_DEPTH) {
                    return unknownAt(__LINE__, e);
                }
                c = &sFr[depth++];
                memcpy(c, f, sizeof(*c));
                c->e.nextCommand = script;
                c->e.restartFrom = script;
                c->e.currCommand = script;
                c->e.loopDepth = -1;
                c->e.switchDepth = -1;
                continue;
            }
            case OPCODE_END_EVENT:
            case OPCODE_END_SCRIPT:
                if (depth <= 1) {
                    out->kind = OPT_OTHER;
                    out->key = e->restartFrom;
                    return R_END;
                }
                {
                    SimFrame* p = &sFr[depth - 2];
                    memcpy(p->e.lwData, e->lwData, sizeof(e->lwData));
                    memcpy(p->e.lfData, e->lfData, sizeof(e->lfData));
                    p->taint = f->taint;
                    p->lfTaint = f->lfTaint;
                    p->derived = f->derived;
                    memcpy(p->source, f->source, sizeof(f->source));
                    depth--;
                }
                continue;
            case OPCODE_GOTO: {
                EvtWord* to;
                if (tainted(f, e->args[0])) {
                    return unknownAt(__LINE__, e);
                }
                to = findLabel((EvtWord*)e->restartFrom, evtGetValue(e, e->args[0]));
                if (to == NULL) {
                    return unknownAt(__LINE__, e);
                }
                e->nextCommand = to;
                continue;
            }
            case OPCODE_INLINE_EVENT:
            case OPCODE_INLINE_EVENT_ID:
            case OPCODE_BROTHER_EVENT:
            case OPCODE_BROTHER_EVENT_ID: {
                EvtWord* past = skipBlock((EvtWord*)e->nextCommand);
                if (past == NULL) {
                    return unknownAt(__LINE__, e);
                }
                if (r == OPCODE_INLINE_EVENT_ID || r == OPCODE_BROTHER_EVENT_ID) {
                    markOut(f, e->args[0], 1);
                }
                e->nextCommand = past;
                continue;
            }
            case OPCODE_RUN_EVENT_ID:
            case OPCODE_CHECK_EVENT:
                markOut(f, e->args[1], 1);
                continue;
            case OPCODE_WAIT_FRAME:
            case OPCODE_WAIT_MSEC:
            case OPCODE_HALT:
            case OPCODE_RUN_EVENT:
            case OPCODE_DELETE_EVENT:
            case OPCODE_SET_PRI:
            case OPCODE_SET_SPD:
            case OPCODE_SET_TYPE:
            case OPCODE_STOP_ALL:
            case OPCODE_START_ALL:
            case OPCODE_STOP_OTHER:
            case OPCODE_START_OTHER:
            case OPCODE_STOP_ID:
            case OPCODE_START_ID:
                continue;
            default:
                if (r >= OPCODE_SET && r <= OPCODE_SET_MSEC_FROM_FRAME) {
                    continue; /* (a write outside the entry: skipped) */
                }
                return unknownAt(__LINE__, e);
        }
    }
    return unknownAt(__LINE__, NULL); /* (too long a way) */
}

/* the moves found so far, while looking */
static void addOption(const SimOut* out, const Choice* path, int n, double share) {
    int i;
    Option* o;
    for (i = 0; i < sDm.nOpts; i++) {
        if (sDm.opts[i].kind == out->kind && sDm.opts[i].key == out->key) {
            sDm.opts[i].share += share;
            return;
        }
    }
    if (sDm.nOpts >= MAX_OPTS) {
        sDm.unknown += share;
        return;
    }
    o = &sDm.opts[sDm.nOpts++];
    memset(o, 0, sizeof(*o));
    o->kind = out->kind;
    o->weapon = out->weapon;
    o->key = out->key;
    o->nChoices = n;
    memcpy(o->choices, path, sizeof(Choice) * (size_t)n);
    o->share = share;
    o->choicePc = out->choicePc;
    o->nTargets = out->nTargets;
    memcpy(o->targets, out->targets, sizeof(out->targets));
}

static int cmpInt(const void* a, const void* b) {
    const int x = *(const int*)a, y = *(const int*)b;
    return x < y ? -1 : x > y;
}

static void explore(Choice* path, int n, double share);

/* the values a chance of range numbers can take that the script tells apart,
 * each with how many of the numbers it stands for */
static int chanceValues(Choice* path, int n, const SimOut* need, int* values, int* counts, int max) {
    const int range = need->range;
    int consts[MAX_CONSTS * 4], nc = 0, i, k, rounds, opaque = 0;
    int probed[64], nProbed = 0;
    SimOut o;
    if (range <= 4) {
        for (i = 0; i < range; i++) {
            values[i] = i;
            counts[i] = 1;
        }
        return range;
    }
    /* probe: which numbers does the way compare it with? */
    for (rounds = 0; rounds < 6; rounds++) {
        int cand[2 + MAX_CONSTS * 4 * 2], ncand = 0, added = 0; /* (the ends, then two per number) */
        cand[ncand++] = 0;
        cand[ncand++] = range - 1;
        for (i = 0; i < nc; i++) {
            const int c = consts[i];
            if (c >= 0 && c < range) {
                cand[ncand++] = c;
            }
            if (c + 1 >= 0 && c + 1 < range) {
                cand[ncand++] = c + 1;
            }
        }
        for (i = 0; i < ncand; i++) {
            int seen = 0;
            for (k = 0; k < nProbed; k++) {
                seen |= probed[k] == cand[i];
            }
            if (seen || nProbed >= 64 || sRuns >= SIM_RUNS) {
                continue;
            }
            probed[nProbed++] = cand[i];
            path[n].pc = need->pc;
            path[n].value = cand[i];
            while (simRun(path, n + 1, n, &o) == R_RETRY) {
            }
            opaque |= o.opaque;
            for (k = 0; k < o.nConsts; k++) {
                int j, dup = 0;
                for (j = 0; j < nc; j++) {
                    dup |= consts[j] == o.consts[k];
                }
                if (!dup && nc < MAX_CONSTS * 4) {
                    consts[nc++] = o.consts[k];
                    added = 1;
                }
            }
        }
        if (!added || opaque) {
            break;
        }
    }
    if (opaque) {
        /* every number (or an even spread of them) */
        const int count = range < max ? range : max;
        for (i = 0; i < count; i++) {
            values[i] = (int)((long long)i * range / count);
            counts[i] = (int)((long long)(i + 1) * range / count) - values[i];
        }
        return count;
    }
    /* the numbers compared split the range: each constant is a class, and so is each gap between them */
    {
        int cuts[MAX_CONSTS * 4 + 2], ncut = 0, out = 0;
        for (i = 0; i < nc; i++) {
            if (consts[i] >= 0 && consts[i] < range) {
                cuts[ncut++] = consts[i];
            }
        }
        qsort(cuts, (size_t)ncut, sizeof(int), cmpInt);
        {
            int lo = 0;
            for (i = 0; i <= ncut && out < max; i++) {
                const int c = i < ncut ? cuts[i] : range;
                if (i > 0 && i < ncut && cuts[i] == cuts[i - 1]) {
                    continue;
                }
                if (c > lo) {
                    values[out] = lo; /* the gap below c */
                    counts[out++] = c - lo;
                }
                if (i < ncut && out < max) {
                    values[out] = c; /* c itself */
                    counts[out++] = 1;
                    lo = c + 1;
                }
            }
        }
        return out;
    }
}

static void explore(Choice* path, int n, double share) {
    SimOut out;
    int r;
    if (sRuns >= SIM_RUNS || n >= MAX_CHOICES) {
        sDm.unknown += share;
        return;
    }
    r = simRun(path, n, -1, &out);
    while (r == R_RETRY) {
        r = simRun(path, n, -1, &out); /* (with an enemy's own call tried for real) */
    }
    if (r == R_NEED) {
        int values[128], counts[128], count = 0, total = 0, i;
        if (out.needKind == 1) {
            for (i = 0; i < out.range; i++) {
                values[i] = i;
                counts[i] = out.weights[i] > 0 ? out.weights[i] : 0;
            }
            count = out.range;
        } else if (out.needKind == 2) {
            values[0] = 0;
            counts[0] = 1;
            count = 1;
            if (out.itemScript != 0) {
                values[1] = 1; /* (its item: the AI's own call, by chance; offered as a move of its own) */
                counts[1] = 0;
                count = 2;
            }
        } else {
            count = chanceValues(path, n, &out, values, counts, 128);
        }
        for (i = 0; i < count; i++) {
            total += counts[i];
        }
        for (i = 0; i < count; i++) {
            const EvtWord v = out.needKind == 2 && values[i] ? out.itemScript : values[i];
            int k, again = 0;
            /* the same chance asked again (a pick that found nothing to do, tried anew): only the
             * numbers above the last pick there, so each retry is followed once, not in every order */
            for (k = 0; k < n; k++) {
                if (path[k].pc == out.pc && v <= path[k].value) {
                    again = 1;
                }
            }
            if (again) {
                continue;
            }
            path[n].pc = out.pc;
            path[n].value = v;
            explore(path, n + 1, total > 0 ? share * counts[i] / total : 0.0);
        }
        return;
    }
    if (r == R_DONE || r == R_END) {
        if (r == R_END && n == 0) {
            sDm.unknown += 0.0; /* (no move at all: nothing to offer) */
            return;
        }
        addOption(&out, path, n, share);
        return;
    }
    sDm.unknown += share;
}

/* the event and the ones waiting on it, from the root, as the look ahead's start */
static int snapshot(EventEntry* entry) {
    EventEntry* chain[SIM_DEPTH];
    int n = 0, i;
    EventEntry* e;
    for (e = entry; e != NULL && n < SIM_DEPTH; e = e->waitingEvent) {
        chain[n++] = e;
    }
    if (e != NULL) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        SimFrame* f = &sStart[i];
        memcpy(&f->e, chain[n - 1 - i], sizeof(EventEntry));
        f->taint = 0;
        f->lfTaint = 0;
        f->derived = 0;
    }
    /* the event at its call (the look ahead runs it as a command) */
    sStart[n - 1].e.nextCommand = entry->currCommand;
    sStartDepth = n;
    return 1;
}

/* the look ahead for the unit's turn: the moves (sDm.opts) */
static void lookAhead(EventEntry* entry) {
    Choice path[MAX_CHOICES];
    static BattleWorkWeaponTargets sKept;
    const u32 seed = portRandState();
    sDm.nOpts = 0;
    sDm.unknown = 0.0;
    sRuns = 0;
    sWhyLine = 0;
    sTrustN = 0;
    sLookAheadSpoilt = 0;
    if (!snapshot(entry)) {
        return;
    }
    memcpy(&sKept, &_battleWorkPointer->weaponTargets, sizeof(sKept));
    explore(path, 0, 1.0);
    memcpy(&_battleWorkPointer->weaponTargets, &sKept, sizeof(sKept));
    portRandSetState(seed); /* (nothing it calls takes one; kept all the same) */
    printf("[dm] look ahead: %d move(s) in %d way(s), %.0f%% unseen\n", sDm.nOpts, sRuns, sDm.unknown * 100.0);
    if (sWhyLine != 0) {
        /* (where: dungeon_master_aurora.c's line; the command, its opcode and first word) */
        printf("[dm]   unseen from line %d, command %08x %llx\n", sWhyLine, sWhyPc != NULL ? (unsigned)sWhyPc[0] : 0u,
               sWhyPc != NULL ? (unsigned long long)sWhyPc[1] : 0ull);
    }
    {
        int i, k;
        for (i = 0; i < sDm.nOpts; i++) {
            const Option* o = &sDm.opts[i];
            printf("[dm]   move %d: kind %d, %d chance(s), %.0f%%, targets", i, o->kind, o->nChoices, o->share * 100.0);
            for (k = 0; k < o->nTargets; k++) {
                BattleUnit* t = unitById(o->targets[k].unit);
                printf(" %s/%d", t != NULL ? msgSearch(t->kindParams->name) : "?", o->targets[k].part);
            }
            printf("\n");
        }
    }
    fflush(stdout);
}

/* the script (or a child it calls by name) takes a chance somewhere: it has
 * more than one move to choose from, in some turn */
static int hasChance(EvtWord* script, int depth) {
    EvtWord* cmd = script;
    int guard;
    if (script == NULL || depth > 4) {
        return 0;
    }
    for (guard = 0; guard < 8000; guard++) {
        const int op = (u8)*cmd;
        const int n = (int)(*cmd >> 16);
        if (op == OPCODE_END_SCRIPT) {
            return 0;
        }
        if (op == OPCODE_USER_FUNC && n >= 1) {
            const UserFunction fn = (UserFunction)cmd[1];
            if (fn == evt_sub_random || fn == btlevtcmd_GetRandomValue || fn == btlevtcmd_DrawLots) {
                return 1;
            }
        } else if (op == OPCODE_RUN_CHILD_EVENT && n >= 1 && cmd[1] > 0x10000 && (EvtWord*)cmd[1] != script) {
            if (hasChance((EvtWord*)cmd[1], depth + 1)) {
                return 1;
            }
        }
        cmd += 1 + n;
    }
    return 0;
}

/* --- names --- */

static int opposing(const BattleWeapon* w) {
    return !(w->targetClassFlags & (TARGET_CLASS_NO_OPPOSING_ALLIANCE | TARGET_CLASS_ONLY_SELF));
}

/* a move's name (English: translated as drawn), from what its weapon does */
static const char* baseName(const Option* o) {
    const BattleWeapon* w = o->weapon;
    if (o->kind == OPT_AI) {
        return "Enemy's choice";
    }
    if (o->kind == OPT_SPAWN) {
        return "Call for help";
    }
    if (o->kind == OPT_OTHER || w == NULL) {
        return "Other move";
    }
    if (!opposing(w)) {
        if (w->chargeStrength > 0) return "Charge";
        if (w->atkChangeChance && w->atkChangeStrength > 0) return "Power up";
        if (w->defChangeChance && w->defChangeStrength > 0) return "Defense up";
        if (w->invisibleChance) return "Vanish";
        if (w->dodgyChance) return "Dodgy";
        if (w->fastChance) return "Speed up";
        if (w->hpRegenTime > 0) return "Regenerate";
        if (w->electricChance) return "Electrify";
        if (w->sizeChangeChance && w->sizeChangeStrength > 0) return "Grow";
        return "Support";
    }
    if (w->sleepChance) return "Sleep attack";
    if (w->stopChance) return "Stop attack";
    if (w->dizzyChance) return "Dizzy attack";
    if (w->poisonChance) return "Poison attack";
    if (w->confuseChance) return "Confusion attack";
    if (w->freezeChance || w->element == ELEMENT_ICE) return "Ice attack";
    if (w->burnChance || w->element == ELEMENT_FIRE) return "Fire attack";
    if (w->electricChance || w->element == ELEMENT_ELECTRIC) return "Shock attack";
    if (w->element == ELEMENT_EXPLOSION) return "Explosion";
    if (w->sizeChangeChance && w->sizeChangeStrength < 0) return "Shrinking attack";
    if (w->defChangeChance && w->defChangeStrength < 0) return "Softening attack";
    if (w->atkChangeChance && w->atkChangeStrength < 0) return "Weakening attack";
    if (w->slowChance) return "Slowing attack";
    if (w->ohkoChance) return "Deadly attack";
    if (w->hpDamageFn == NULL && w->fpDamageFn != NULL) return "FP attack";
    return "Basic attack";
}

/* the name as shown: an item's own (the game's text), else the move's in the menu's language, numbered */
static int optionDamage(const Option* o, int target);

static void optionName(int i, char* out, size_t size) {
    const Option* o = &sDm.opts[i];
    if (o->kind == OPT_WEAPON && o->weapon != NULL && o->weapon->itemId > 0 && o->weapon->itemId < ITEM_TYPE_MAX) {
        snprintf(out, size, "%s", msgSearch(itemDataTable[o->weapon->itemId].name));
        return;
    }
    {
        const char* base = baseName(o);
        int k, number = 0, more = 0;
        for (k = 0; k < sDm.nOpts; k++) {
            const Option* q = &sDm.opts[k];
            const int itemQ = q->kind == OPT_WEAPON && q->weapon != NULL && q->weapon->itemId > 0;
            if (q->kind != OPT_AI && !itemQ && strcmp(baseName(q), base) == 0) {
                more++;
                if (k <= i) {
                    number++;
                }
            }
        }
        if (more == 2 && strcmp(base, "Basic attack") == 0) {
            /* two plain attacks of different power: the basic one and the strong one */
            int other = -1, mine = optionDamage(o, -1);
            for (k = 0; k < sDm.nOpts; k++) {
                const Option* q = &sDm.opts[k];
                const int itemQ = q->kind == OPT_WEAPON && q->weapon != NULL && q->weapon->itemId > 0;
                if (k != i && q->kind != OPT_AI && !itemQ && strcmp(baseName(q), base) == 0) {
                    other = optionDamage(q, -1);
                }
            }
            if (mine != other) {
                snprintf(out, size, "%s", portTrGame(mine > other ? "Strong attack" : base));
                return;
            }
        }
        if (more > 1) {
            snprintf(out, size, "%s %d", portTrGame(base), number);
        } else {
            snprintf(out, size, "%s", portTrGame(base));
        }
    }
}

/* the damage the move would do to the target before guards (its power, the
 * attacker's attack changes and charge, less the target's defence) */
static int damageTo(BattleUnit* self, const BattleWeapon* w, s16 unitId, s16 partId) {
    BattleUnit* t = unitById(unitId);
    BattleUnitPart* part;
    int power, def = 0;
    if (w == NULL || w->hpDamageFn == NULL || t == NULL || self == NULL) {
        return -1;
    }
    part = BtlUnit_GetPartsPtr(t, partId);
    if (part == NULL) {
        return -1;
    }
    power = w->hpDamageFn(self, (BattleWeapon*)w, t, part);
    if (w->special & SPECIAL_STATUS_BUFFABLE) {
        power += self->atkChangeStrength + self->chargeStrength;
    }
    if (part->defense != NULL && w->element < 5) {
        def = part->defense->defenses[w->element];
    }
    if (def >= 99) {
        return 0;
    }
    def += t->defChangeStrength;
    if (t->statusFlags & STATUS_FLAG_DEFENDING) {
        def += 1;
    }
    power -= def;
    return power < 0 ? 0 : power;
}

/* the move's damage as the menu shows it: to the target chosen, else the one the AI prefers (the first) */
static int optionDamage(const Option* o, int target) {
    BattleUnit* self = unitById(sDm.unitId);
    if (o->kind != OPT_WEAPON || o->weapon == NULL) {
        return -1;
    }
    if (o->nTargets > 0) {
        const Target* t = &o->targets[target >= 0 && target < o->nTargets ? target : 0];
        return damageTo(self, o->weapon, t->unit, t->part);
    }
    {
        BattleUnit* mario = BattleGetMarioPtr(_battleWorkPointer);
        if (mario != NULL && opposing(o->weapon)) {
            return damageTo(self, o->weapon, (s16)mario->unitId, (s16)BtlUnit_GetBodyPartsId(mario));
        }
    }
    return -1;
}

static int single(const Option* o) {
    return o->kind == OPT_WEAPON && o->weapon != NULL && !(o->weapon->targetClassFlags & TARGET_CLASS_MULTIPLE) &&
           o->choicePc != NULL && o->nTargets > 1;
}

/* --- the player's controller --- */

/* the player's presses since the menu last looked (gathered each frame by
 * frameHook: the script's call isn't run every frame) */
static struct {
    u32 trg, rep, dir;
} sIn;
static u32 trg(void) {
    return sIn.trg;
}
static u32 rep(void) {
    return sIn.rep;
}
static u32 dirRep(void) {
    return sIn.dir;
}
static void inputSeen(void) {
    memset(&sIn, 0, sizeof(sIn));
}
static int up(void) {
    return (rep() & PAD_BUTTON_UP) || (dirRep() & STICK_UP);
}
static int down(void) {
    return (rep() & PAD_BUTTON_DOWN) || (dirRep() & STICK_DOWN);
}
static int left(void) {
    return (rep() & (PAD_BUTTON_LEFT | PAD_TRIGGER_L)) || (dirRep() & STICK_LEFT); /* (L and R step through the targets too) */
}
static int right(void) {
    return (rep() & (PAD_BUTTON_RIGHT | PAD_TRIGGER_R)) || (dirRep() & STICK_RIGHT);
}

/* --- the turn --- */

/* the unit whose turn the event is (an enemy's attack event, or one it waits on), else NULL */
static BattleUnit* turnUnit(EventEntry* entry) {
    BattleWork* wp = _battleWorkPointer;
    EventEntry* root = entry;
    BattleUnit* unit;
    int guard = 0;
    if (entry->unitId < 0 || entry->unitId >= BATTLE_UNIT_MAX || entry->unitId != wp->activeUnitId) {
        return NULL;
    }
    unit = BattleGetUnitPtr(wp, entry->unitId);
    if (unit == NULL || unit->alliance != 1 || unit->attackEventId == 0) {
        return NULL;
    }
    while (root->waitingEvent != NULL && guard++ < 32) {
        root = root->waitingEvent;
    }
    if (root->eventId != unit->attackEventId) {
        return NULL;
    }
    return unit;
}

static void sfx(int ok) {
    psndSFXOn(ok ? (const char*)0x9d8 : (const char*)0x9d9);
}

/* the move's choices from here, its target, and the script on */
static void commit(int pick, int target) {
    sDm.picked = pick;
    sDm.target = target;
    sDm.forcedAt = 0;
    sDm.choiceDone = 0;
    sDm.phase = PH_RUN;
    if (pick >= 0 && sDm.opts[pick].kind != OPT_AI) {
        char name[64];
        optionName(pick, name, sizeof(name));
        printf("[dm] %s: %s, target %s\n", msgSearch(unitById(sDm.unitId)->kindParams->name), name,
               target >= 0 && unitById(sDm.opts[pick].targets[target].unit) != NULL
                   ? msgSearch(unitById(sDm.opts[pick].targets[target].unit)->kindParams->name)
                   : "the script's");
    } else {
        sDm.picked = -1;
        sDm.aiAll = 1;
        printf("[dm] the enemy's own choice\n");
    }
    fflush(stdout);
}

/* the menu, a frame: 1 once a move (and its target) is chosen */
static int menuFrame(void) {
    const int count = sDm.nOpts;
    Option* o;
    sDm.frames++;
    if (sDm.phase == PH_MENU) {
        if (up() && sDm.cursor > 0) {
            sDm.cursor--;
            psndSFXOn("SFX_BTL_CURSOR_MOVE1");
        } else if (down() && sDm.cursor < count - 1) {
            sDm.cursor++;
            psndSFXOn("SFX_BTL_CURSOR_MOVE1");
        }
        if (sDm.cursor < sDm.top) {
            sDm.top = sDm.cursor;
        } else if (sDm.cursor >= sDm.top + 6) {
            sDm.top = sDm.cursor - 5;
        }
        if (trg() & PAD_BUTTON_Y) {
            sfx(1);
            commit(-1, -1); /* Y: the enemy's own choice, at once */
            return 1;
        }
        if (trg() & PAD_BUTTON_A) {
            o = &sDm.opts[sDm.cursor];
            sfx(1);
            if (o->kind == OPT_AI) {
                commit(-1, -1);
                return 1;
            }
            if (single(o)) {
                sDm.phase = PH_TARGET;
                sDm.target = 0;
                return 0;
            }
            commit(sDm.cursor, -1);
            return 1;
        }
        return 0;
    }
    /* PH_TARGET */
    o = &sDm.opts[sDm.cursor];
    if ((left() || up()) && sDm.target > 0) {
        sDm.target--;
        psndSFXOn("SFX_BTL_CURSOR_MOVE2");
    } else if ((right() || down()) && sDm.target < o->nTargets - 1) {
        sDm.target++;
        psndSFXOn("SFX_BTL_CURSOR_MOVE2");
    }
    if (trg() & PAD_BUTTON_B) {
        sfx(0);
        sDm.phase = sDm.scripted ? PH_TARGET : PH_MENU;
        if (sDm.scripted) {
            commit(-1, -1); /* (the script's move: B leaves its target to it) */
            return 1;
        }
        return 0;
    }
    if (trg() & PAD_BUTTON_A) {
        sfx(1);
        commit(sDm.cursor, sDm.target);
        return 1;
    }
    return 0;
}

/* a target asked for as the script runs, a frame: 1 once chosen (outputs set) */
static int liveFrame(EventEntry* entry) {
    EvtWord* args = entry->args;
    if ((left() || up()) && sDm.target > 0) {
        sDm.target--;
        psndSFXOn("SFX_BTL_CURSOR_MOVE2");
    } else if ((right() || down()) && sDm.target < sDm.nLive - 1) {
        sDm.target++;
        psndSFXOn("SFX_BTL_CURSOR_MOVE2");
    }
    if (trg() & PAD_BUTTON_B) {
        sfx(0);
        return -1; /* (the AI's) */
    }
    if (trg() & PAD_BUTTON_A) {
        sfx(1);
        evtSetValue(entry, args[1], sDm.live[sDm.target].unit);
        evtSetValue(entry, args[2], sDm.live[sDm.target].part);
        return 1;
    }
    return 0;
}

/* a shared online battle (port/include/port/statesync.h): the dungeon master's game picks, from its
 * player's controller; every other game takes that pick (or the enemy's own choice, when it never
 * comes this step) */
static int menuFrameShared(void) {
    /* (its seat's player's place in the battle, not its seat: places follow those who came) */
    const int p = dmPlace();
    if (p < 0) {
        commit(-1, -1); /* (its player isn't in the battle: the enemy's own choice, alike in every game) */
        return 1;
    }
    if (portStateSyncBattleRemote(p)) {
        int a = -1, b = -1, c = 1;
        const int r = portStateSyncBattleTake(0, p, &a, &b, &c);
        if (r == 0) {
            return 0;
        }
        if (r == 1 && !c && a >= 0 && a < sDm.nOpts) {
            commit(a, b >= 0 && b < sDm.opts[a].nTargets ? b : -1);
        } else {
            commit(-1, -1);
        }
        return 1;
    }
    if (!menuFrame()) {
        return 0;
    }
    portStateSyncBattlePut(0, sDm.picked, sDm.target, sDm.aiAll);
    return 1;
}

static int liveFrameShared(EventEntry* entry) {
    const int p = dmPlace();
    int r;
    if (p < 0) {
        return -1; /* (its player isn't in the battle: the AI's pick, alike in every game) */
    }
    if (portStateSyncBattleRemote(p)) {
        int a = -1, b = -1, c = 0;
        r = portStateSyncBattleTake(1, p, &a, &b, &c);
        if (r == 0) {
            return 0;
        }
        if (r == 1 && a > 0 && b >= 0 && b < sDm.nLive) {
            sDm.target = b;
            evtSetValue(entry, entry->args[1], sDm.live[b].unit);
            evtSetValue(entry, entry->args[2], sDm.live[b].part);
            return 1;
        }
        return -1;
    }
    r = liveFrame(entry);
    if (r != 0) {
        portStateSyncBattlePut(1, r, sDm.target, 0);
    }
    return r;
}

/* a call of the turn's script, as it runs (the choice made): 1 handled */
static int runCall(EventEntry* entry, int first, int* result) {
    UserFunction fn = entry->userfunc;
    EvtWord* args = entry->args;
    EvtWord* pc = (EvtWord*)entry->currCommand;
    if (!first) {
        if (sDm.phase == PH_LIVE && entry == sDm.liveEntry) {
            const int r = liveFrameShared(entry);
            inputSeen();
            if (r == 0) {
                *result = EVT_RETURN_BLOCK;
                return 1;
            }
            sDm.phase = PH_RUN;
            sDm.liveEntry = NULL;
            if (r < 0) {
                *result = entry->userfunc(entry, TRUE); /* (B: the AI's pick) */
                return 1;
            }
            *result = EVT_RETURN_DONE;
            return 1;
        }
        return 0;
    }
    if (choiceFn(fn) && sDm.picked >= 0) {
        Option* o = &sDm.opts[sDm.picked];
        if (sDm.forcedAt < o->nChoices) {
            const Choice* c = &o->choices[sDm.forcedAt];
            if (c->pc == pc) {
                evtSetValue(entry, fn == btlevtcmd_DrawLots ? args[0] : args[1], c->value);
                sDm.forcedAt++;
                *result = EVT_RETURN_DONE;
                return 1;
            }
            printf("[dm] the script went another way: the rest is its own\n");
            fflush(stdout);
            sDm.forcedAt = o->nChoices;
        }
        return 0;
    }
    if (fn == btlevtcmd_ChoiceSamplingEnemy) {
        BattleWeapon* w = (BattleWeapon*)evtGetValue(entry, args[0]);
        const BattleWorkWeaponTargets* t = &_battleWorkPointer->weaponTargets;
        if (sDm.aiAll) {
            return 0; /* (the enemy's own choice: its targets too) */
        }
        if (sDm.picked >= 0 && !sDm.choiceDone) {
            Option* o = &sDm.opts[sDm.picked];
            if (pc != o->choicePc) {
                return 0; /* (a choice before the move's own: the AI's) */
            }
            sDm.choiceDone = 1;
            if (sDm.target >= 0) {
                const Target* want = &o->targets[sDm.target];
                int i;
                for (i = 0; i < t->numTargets; i++) {
                    const BattleWorkTarget* w2 = &t->targets[t->targetIndices[i]];
                    if (w2->unitIdx == want->unit && w2->partIdx == want->part) {
                        evtSetValue(entry, args[1], want->unit);
                        evtSetValue(entry, args[2], want->part);
                        *result = EVT_RETURN_DONE;
                        return 1;
                    }
                }
            }
            return 0; /* (one target, or it isn't there now: the game's) */
        }
        /* a target asked for as the script runs: a second hit, or a turn whose move the script decided */
        if (w != NULL && !(w->targetClassFlags & TARGET_CLASS_MULTIPLE) && t->numTargets > 1) {
            sDm.nLive = sampledTargets(sDm.live);
            sDm.liveWeapon = w;
            sDm.liveEntry = entry;
            sDm.target = 0;
            sDm.phase = PH_LIVE;
            *result = EVT_RETURN_BLOCK;
            return 1;
        }
        return 0;
    }
    if (fn == btlevtcmd_AttackDeclare || fn == btlevtcmd_AttackDeclareAll) {
        sDm.armed = 1;
    } else if (sDm.armed && moveFn(fn)) {
        sDm.moved = 1;
    } else if (fn == btlevtcmd_PreCheckDamage || fn == btlevtcmd_CheckDamage) {
        sDm.hit = 1;
    }
    return 0;
}

int portDmUserFunc(EventEntry* entry, int first, int* result) {
    BattleUnit* unit;
    if (sOwed.entry != NULL && entry == sOwed.entry) {
        if (first) {
            sOwed.entry = NULL; /* (a new call on that entry: the held one is gone) */
        } else if (!(sDm.realPending && entry == sDm.menuEntry) && entry->currCommand == sOwed.pc &&
                   entry->userfunc == sOwed.fn) {
            /* the dungeon master went (the pad, the turn over, a guest leaving) with its call held: the call's
               first run, owed since the hold, not a later one on a function that never began */
            sOwed.entry = NULL;
            if (gPortDmLive == DM_OWED) {
                gPortDmLive = 0;
            }
            *result = entry->userfunc(entry, TRUE);
            return 1;
        }
    }
    if (gPortDmLive != DM_ON || _battleWorkPointer == NULL) {
        return 0;
    }
    unit = turnUnit(entry);
    if (unit == NULL) {
        return 0;
    }
    if (sDm.evtId != unit->attackEventId) {
        if (!first) {
            return 0; /* (a call that began before the turn was seen) */
        }
        resetTurn();
        sDm.evtId = unit->attackEventId;
        sDm.unitId = unit->unitId;
        if (unit->tokenFlags & TOKEN_FLAG_CONFUSED_ACTION) {
            say("Confused: it acts on its own.");
            sDm.phase = PH_RUN;
            sDm.aiAll = 1;
            return runCall(entry, first, result);
        }
        lookAhead(entry);
        sDm.cursor = 0;
        sDm.top = 0;
        sDm.frames = 0;
        /* one move, where its script could take others by chance (a set opening, a phase): the script's */
        sDm.scripted = sDm.nOpts == 1 && sDm.unknown < 0.001 && hasChance((EvtWord*)unit->attackEventCode, 0);
        if (sDm.nOpts == 0) {
            say("Its script decides this turn.");
            sDm.phase = PH_RUN; /* (its targets may still be asked for) */
            return runCall(entry, first, result);
        }
        if (sDm.scripted) {
            if (!single(&sDm.opts[0])) {
                say("Its script picks this move this turn.");
                optionName(0, sDm.bannerName, sizeof(sDm.bannerName));
                commit(0, -1);
                return runCall(entry, first, result);
            }
            sDm.cursor = 0;
            sDm.phase = PH_TARGET; /* (its move is the script's; its target the player's) */
            sDm.target = 0;
        } else {
            Option* ai = &sDm.opts[sDm.nOpts++];
            memset(ai, 0, sizeof(*ai));
            ai->kind = OPT_AI;
            ai->share = sDm.unknown;
            sDm.phase = PH_MENU;
        }
        sDm.menuEntry = entry;
        sDm.realPending = 1;
        sOwed.entry = entry;
        sOwed.pc = entry->currCommand;
        sOwed.fn = entry->userfunc;
        sOwed.id = entry->eventId;
        *result = EVT_RETURN_BLOCK;
        return 1;
    }
    if ((sDm.phase == PH_MENU || sDm.phase == PH_TARGET) && entry == sDm.menuEntry) {
        const int chosen = menuFrameShared();
        inputSeen();
        if (!chosen) {
            *result = EVT_RETURN_BLOCK;
            return 1;
        }
    }
    if (sDm.realPending && entry == sDm.menuEntry && sDm.phase >= PH_RUN) {
        sDm.realPending = 0;
        sOwed.entry = NULL;
        if (!runCall(entry, 1, result)) {
            *result = entry->userfunc(entry, TRUE);
        }
        return 1;
    }
    return runCall(entry, first, result);
}

int portDmHold(EventEntry* entry) {
    BattleUnit* unit;
    if (gPortDmLive != DM_ON || _battleWorkPointer == NULL || !sDm.armed || !sDm.moved || sDm.hit || sDm.held) {
        return 0;
    }
    unit = turnUnit(entry);
    if (unit == NULL || unit->attackEventId != sDm.evtId) {
        return 0;
    }
    if (sDm.picked >= 0) {
        const Option* o = &sDm.opts[sDm.picked];
        if (o->kind != OPT_WEAPON || o->weapon == NULL || o->weapon->hpDamageFn == NULL || !opposing(o->weapon)) {
            return 0;
        }
    }
    if ((dmButtons(gp->button) & PAD_BUTTON_A) && sDm.holdFrames < HOLD_MAX) {
        sDm.holdFrames++;
        return 1;
    }
    sDm.held = 1; /* (let go, or held out: the strike, once a turn) */
    if (sDm.holdFrames > 0) {
        printf("[dm] wind-up held %d frame(s)\n", sDm.holdFrames);
        fflush(stdout);
    }
    return 0;
}

/* --- drawing (the game's windows, font and icons) --- */

static void quad(f32 x, f32 y, f32 w, f32 h, GXColor c) {
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GXSetZCompLoc(GX_TRUE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
    GXSetNumChans(0);
    GXSetNumTevStages(1);
    GXSetTevColor(GX_TEVREG0, c);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C0);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_A0);
    GXSetNumTexGens(0);
    GXLoadPosMtxImm(camGetPtr(CAMERA_2D)->view, 0);
    GXSetCurrentMtx(0);
    GXSetCullMode(GX_CULL_NONE);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition3f32(x, y, 0.0f);
    GXPosition3f32(x, y - h, 0.0f);
    GXPosition3f32(x + w, y - h, 0.0f);
    GXPosition3f32(x + w, y, 0.0f);
}

/* text from its top left, fitted into room */
static void text(f32 x, f32 y, f32 scale, f32 room, GXColor c, const char* t) {
    const f32 full = (f32)FontGetMessageWidth(t);
    const f32 s = full * scale > room && full > 0.0f ? room / full : scale;
    FontDrawStart();
    FontDrawColor(c);
    FontDrawScaleVec((Vec){s, scale, 1.0f});
    FontDrawString(x, y, t);
}

/* a line at the bottom of the screen, white with the font's edge (over the stage) */
static void hint(const char* t) {
    const f32 full = (f32)FontGetMessageWidth(t);
    const f32 s = full * 0.6f > 520.0f && full > 0.0f ? 520.0f / full : 0.6f;
    FontDrawStart();
    FontDrawEdge();
    FontDrawColor((GXColor){0xFF, 0xFF, 0xFF, 0xFF});
    FontDrawScaleVec((Vec){s, 0.6f, 1.0f});
    FontDrawString(-full * s * 0.5f, -186.0f, t);
    FontDrawEdgeOff();
}

static void textRight(f32 x, f32 y, f32 scale, GXColor c, const char* t) {
    text(x - (f32)FontGetMessageWidth(t) * scale, y, scale, 999.0f, c, t);
}

static const GXColor kBlack = {0x00, 0x00, 0x00, 0xFF};
static const GXColor kGrey = {0x70, 0x70, 0x70, 0xFF};
static const GXColor kBlue = {0x30, 0x50, 0xC0, 0xFF};  /* the "2P" tag's (second_player_pc.c), on white */
static const GXColor kRed = {0xC0, 0x20, 0x20, 0xFF};
static const GXColor kPanel = {0xFF, 0xFF, 0xFF, 0xCF}; /* the battle menus' help window's */

/* the player's tag, as co-op's ("2P", "3P", "4P") */
static void playerTag(char* out, size_t size) {
    snprintf(out, size, "%dP", portDmPlayer() + 1);
}

/* the statuses a weapon can cause, for the help line */
static void statusList(const BattleWeapon* w, char* out, size_t size) {
    static const struct {
        int off;
        const char* name;
    } kStatus[] = {
        {0, "Sleep"},    {1, "Stopped"},       {2, "Dizzy"},  {3, "Poison"}, {4, "Confusion"}, {5, "Shock"},
        {6, "Burn"},     {7, "Freeze"},     {8, "Shrink"}, {9, "Soft"},   {10, "Weak"},     {11, "Slow"},
        {12, "Fright"},  {13, "Gust"},      {14, "Knockout"},
    };
    int on[15], i;
    size_t len = 0;
    memset(on, 0, sizeof(on));
    on[0] = w->sleepChance > 0;
    on[1] = w->stopChance > 0;
    on[2] = w->dizzyChance > 0;
    on[3] = w->poisonChance > 0;
    on[4] = w->confuseChance > 0;
    on[5] = w->electricChance > 0;
    on[6] = w->burnChance > 0;
    on[7] = w->freezeChance > 0;
    on[8] = w->sizeChangeChance > 0 && w->sizeChangeStrength < 0;
    on[9] = w->defChangeChance > 0 && w->defChangeStrength < 0;
    on[10] = w->atkChangeChance > 0 && w->atkChangeStrength < 0;
    on[11] = w->slowChance > 0;
    on[12] = w->frightChance > 0;
    on[13] = w->galeForceChance > 0;
    on[14] = w->ohkoChance > 0;
    out[0] = '\0';
    for (i = 0; i < 15; i++) {
        if (on[i] && len + 2 < size) {
            snprintf(out + len, size - len, "%s%s", len ? ", " : "", portTrGame(kStatus[i].name));
            len = strlen(out);
        }
    }
}

/* the help window under the list: what the move does */
static void helpWindow(const Option* o, int target) {
    char l1[160], l2[160], st[96];
    const f32 x = -200.0f, y = -128.0f;
    l1[0] = l2[0] = '\0';
    if (o->kind == OPT_AI) {
        snprintf(l1, sizeof(l1), "%s", portTrGame("The enemy picks its move and target as it would."));
    } else if (o->kind == OPT_SPAWN) {
        snprintf(l1, sizeof(l1), "%s", portTrGame("It calls another enemy into the battle."));
    } else if (o->kind == OPT_OTHER || o->weapon == NULL) {
        snprintf(l1, sizeof(l1), "%s", portTrGame("A move of its own that doesn't attack."));
    } else {
        const int dmg = optionDamage(o, target);
        statusList(o->weapon, st, sizeof(st));
        if (dmg >= 0) {
            snprintf(l1, sizeof(l1), "%s %d%s%s", portTrGame("Damage"), dmg,
                     (o->weapon->targetClassFlags & TARGET_CLASS_MULTIPLE) ? " - " : "",
                     (o->weapon->targetClassFlags & TARGET_CLASS_MULTIPLE) ? portTrGame("Hits all of them") : "");
        } else if (!opposing(o->weapon)) {
            snprintf(l1, sizeof(l1), "%s", portTrGame("On its own side"));
        } else {
            snprintf(l1, sizeof(l1), "%s", portTrGame("No damage"));
        }
        if (st[0] != '\0') {
            const size_t len = strlen(l1);
            snprintf(l1 + len, sizeof(l1) - len, " - %s", st);
        }
    }
    if (sDm.scripted) {
        snprintf(l2, sizeof(l2), "%s", portTrGame("Its script picks this move this turn."));
    } else if (o->kind != OPT_AI && o->share >= 0.005 && sDm.nOpts > 2) { /* (more than one move of its own) */
        snprintf(l2, sizeof(l2), "%s %d%%", portTrGame("The enemy's own pick:"), (int)(o->share * 100.0 + 0.5));
    }
    windowDispGX_Waku_col(0, kPanel, x, y, 400.0f, 50.0f, 10.0f);
    text(x + 16.0f, y - 6.0f, 0.7f, 368.0f, kBlack, l1);
    if (l2[0] != '\0') {
        text(x + 16.0f, y - 26.0f, 0.6f, 368.0f, kGrey, l2);
    }
}

/* the game's target cursor and name box (BattleCommandDisplay_TargetSelectMenuDisp's) */
static void targetCursor(const Target* t) {
    BattleUnit* unit = unitById(t->unit);
    BattleUnitPart* part = unit != NULL ? BtlUnit_GetPartsPtr(unit, t->part) : NULL;
    Vec pos = t->pos, screen, icon;
    const char* name;
    f32 w;
    if (unit == NULL || part == NULL) {
        return;
    }
    btlGetScreenPoint(&pos, &screen);
    icon = (Vec){screen.x, screen.y, 0.0f};
    iconDispGx(&icon, (unit->attributeFlags & UNIT_ATTR_CEILING) ? ICON_FLAG_2D | ICON_FLAG_FLIP_Y : ICON_DISP_2D, ICON_1EF,
               1.0f);
    name = unit->currentKind == UNIT_YOSHI ? pouchGetYoshiName() : msgSearch(part->partName);
    w = (f32)FontGetMessageWidth(name);
    windowDispGX_Waku_col(0, kPanel, -240.0f, 86.0f, 16.0f + w, 32.0f, 10.0f);
    FontDrawStart();
    FontDrawColorIDX(0);
    FontDrawScale(1.0f);
    FontDrawString(-232.0f, 83.0f, name);
}

/* the move list: the enemy's name over it, a row a move, the hand at the cursor */
static void menuDraw(void) {
    BattleUnit* unit = unitById(sDm.unitId);
    const f32 x = -100.0f, y = 120.0f, w = 250.0f, rowH = 24.0f; /* (left of the enemies' panel) */
    const int rows = sDm.nOpts - sDm.top < 6 ? sDm.nOpts - sDm.top : 6;
    char tag[8], name[64], num[16];
    int i;
    if (unit == NULL) {
        return;
    }
    if (sDm.phase == PH_MENU) {
        windowDispGX_Waku_col(0, kPanel, x, y, w, 34.0f + rows * rowH + 8.0f, 10.0f);
        playerTag(tag, sizeof(tag));
        text(x + 12.0f, y - 6.0f, 0.65f, 30.0f, kBlue, tag);
        text(x + 44.0f, y - 6.0f, 0.75f, w - 56.0f, kBlack, msgSearch(unit->kindParams->name));
        for (i = 0; i < rows; i++) {
            const int k = sDm.top + i;
            const Option* o = &sDm.opts[k];
            const f32 ry = y - 34.0f - i * rowH;
            const GXColor c = k == sDm.cursor ? kBlack : kGrey;
            int dmg;
            optionName(k, name, sizeof(name));
            if (o->kind == OPT_WEAPON && o->weapon != NULL && o->weapon->itemId > 0 && o->weapon->itemId < ITEM_TYPE_MAX) {
                iconDispGx(&(Vec){x + 30.0f, ry - 10.0f, 0.0f}, ICON_DISP_2D, itemDataTable[o->weapon->itemId].icon_id,
                           0.45f);
            }
            text(x + 44.0f, ry, 0.7f, w - 100.0f, o->kind == OPT_AI ? kBlue : c, name);
            dmg = optionDamage(o, -1);
            if (dmg >= 0) {
                snprintf(num, sizeof(num), "%d", dmg);
                textRight(x + w - 14.0f, ry, 0.7f, c, num);
            }
        }
        /* the hand at the cursor (the game's list cursor) and the arrows when there are more */
        {
            Vec hand = {x + 18.0f, y - 54.0f - (sDm.cursor - sDm.top) * rowH, 0.0f};
            iconDispGx(&hand, ICON_FLAG_2D | ICON_FLAG_FLIP_X, ICON_1F0, 0.8f);
            if (sDm.top > 0) {
                iconDispGx(&(Vec){x + w - 20.0f, y - 22.0f, 0.0f}, ICON_DISP_2D, ICON_1B5, 0.6f);
            }
            if (sDm.top + 6 < sDm.nOpts) {
                iconDispGx(&(Vec){x + w - 20.0f, y - 34.0f - 6 * rowH, 0.0f}, ICON_DISP_2D, ICON_1B6, 0.6f);
            }
        }
        helpWindow(&sDm.opts[sDm.cursor], -1);
        hint(portTrGame("{A}: this move   {Y}: the enemy's choice"));
    } else if (sDm.phase == PH_TARGET) {
        const Option* o = &sDm.opts[sDm.cursor];
        windowDispGX_Waku_col(0, kPanel, x, y, w, 64.0f, 10.0f);
        playerTag(tag, sizeof(tag));
        text(x + 12.0f, y - 6.0f, 0.65f, 30.0f, kBlue, tag);
        text(x + 44.0f, y - 6.0f, 0.75f, w - 56.0f, kBlack, msgSearch(unit->kindParams->name));
        optionName(sDm.cursor, name, sizeof(name));
        text(x + 44.0f, y - 32.0f, 0.7f, w - 100.0f, kBlack, name);
        {
            const int dmg = optionDamage(o, sDm.target);
            if (dmg >= 0) {
                snprintf(num, sizeof(num), "%d", dmg);
                textRight(x + w - 14.0f, y - 32.0f, 0.7f, kBlack, num);
            }
        }
        if (sDm.target >= 0 && sDm.target < o->nTargets) {
            targetCursor(&o->targets[sDm.target]);
        }
        helpWindow(o, sDm.target);
        hint(portTrGame(sDm.scripted ? "Choose a target   {B}: the enemy's choice" : "Choose a target   {B}: back"));
    } else if (sDm.phase == PH_LIVE) {
        if (sDm.target >= 0 && sDm.target < sDm.nLive) {
            targetCursor(&sDm.live[sDm.target]);
            if (sDm.liveWeapon != NULL) {
                BattleUnit* self = unitById(sDm.unitId);
                const int dmg = damageTo(self, sDm.liveWeapon, sDm.live[sDm.target].unit, sDm.live[sDm.target].part);
                if (dmg >= 0) {
                    snprintf(num, sizeof(num), "%s %d", portTrGame("Damage"), dmg);
                    windowDispGX_Waku_col(0, kPanel, x, y, w, 40.0f, 10.0f);
                    playerTag(tag, sizeof(tag));
                    text(x + 12.0f, y - 6.0f, 0.65f, 30.0f, kBlue, tag);
                    text(x + 44.0f, y - 6.0f, 0.7f, w - 56.0f, kBlack, num);
                }
            }
        }
        hint(portTrGame("Choose a target   {B}: the enemy's choice"));
    }
}

/* each enemy's HP and statuses, at the right (the dungeon master's side) */
static void hudDraw(void) {
    BattleWork* wp = _battleWorkPointer;
    const f32 x = 168.0f, w = 130.0f;
    f32 y = 158.0f;
    BattleUnit* list[8];
    int n = 0, i, s;
    char tag[16], hp[24];
    for (i = 0; i < BATTLE_UNIT_MAX && n < 8; i++) {
        BattleUnit* u = BattleGetUnitPtr(wp, i);
        if (u == NULL || u->alliance != 1 || u->kindParams == NULL || u->currentHp <= 0 ||
            (u->attributeFlags & (UNIT_ATTR_HIDDEN | UNIT_ATTR_IGNORE_FOR_CLEAR)) || u->currentKind >= UNIT_UNUSED_TREE) {
            continue;
        }
        list[n++] = u;
    }
    if (n == 0) {
        return;
    }
    windowDispGX_Waku_col(0, (GXColor){0xFF, 0xFF, 0xFF, 0xA0}, x, y, w, 22.0f + n * 30.0f, 8.0f);
    snprintf(tag, sizeof(tag), "%dP", portDmPlayer() + 1);
    text(x + 8.0f, y - 4.0f, 0.55f, 24.0f, kBlue, tag);
    text(x + 32.0f, y - 4.0f, 0.55f, w - 40.0f, kBlack, portTrGame("Enemies"));
    for (i = 0; i < n; i++) {
        BattleUnit* u = list[i];
        const f32 ry = y - 22.0f - i * 30.0f;
        const int acting = u->unitId == wp->activeUnitId && sDm.evtId != 0;
        const f32 frac = u->maxHp > 0 ? (f32)u->currentHp / (f32)u->maxHp : 0.0f;
        int icons = 0;
        text(x + 8.0f, ry, 0.55f, 74.0f, acting ? kRed : kBlack, msgSearch(u->kindParams->name));
        snprintf(hp, sizeof(hp), "%d/%d", u->currentHp, u->maxHp);
        textRight(x + w - 8.0f, ry, 0.55f, kBlack, hp);
        quad(x + 8.0f, ry - 16.0f, w - 16.0f, 4.0f, (GXColor){0x40, 0x40, 0x40, 0xC0});
        quad(x + 8.0f, ry - 16.0f, (w - 16.0f) * (frac > 1.0f ? 1.0f : frac), 4.0f,
             frac <= 0.25f ? (GXColor){0xE0, 0x40, 0x30, 0xFF} : (GXColor){0x50, 0xC0, 0x40, 0xFF});
        for (s = 0; s < STATUS_INVALID && icons < 6; s++) {
            const s32 id = portStatusIconId(s);
            if (id >= 0 && BtlUnit_CheckStatus(u, (StatusEffectType)s)) {
                iconDispGx(&(Vec){x + 14.0f + icons * 13.0f, ry - 24.0f, 0.0f}, ICON_DISP_2D, (u16)id, 0.35f);
                icons++;
            }
        }
    }
}

static void bannerDraw(void) {
    char line[160];
    const char* t;
    f32 w, k = 0.7f;
    u8 a;
    if (sDm.banner == NULL || sDm.bannerAge >= BANNER_FRAMES) {
        return;
    }
    t = portTrGame(sDm.banner);
    if (sDm.bannerName[0] != 0) {
        snprintf(line, sizeof(line), "%s: %s", sDm.bannerName, t);
        t = line;
    }
    a = BANNER_FRAMES - sDm.bannerAge < 30 ? (u8)(0xFF * (BANNER_FRAMES - sDm.bannerAge) / 30) : 0xFF;
    w = (f32)FontGetMessageWidth(t) * k;
    if (w > 520.0f) {
        k *= 520.0f / w;
        w = 520.0f;
    }
    FontDrawStart_alpha(a);
    FontDrawEdge();
    FontDrawColor((GXColor){0xFF, 0xFF, 0xFF, a});
    FontDrawScale(k);
    FontDrawString(-w * 0.5f, -150.0f, t);
}

static void dmDisp(CameraId camId, void* param) {
    const u32 seed = portRandState(); /* the font takes the game's random numbers: kept (second_player_pc.c's tagDisp) */
    (void)camId;
    (void)param;
    if (gPortDmLive != DM_ON || _battleWorkPointer == NULL) {
        return;
    }
    hudDraw();
    if (sDm.phase == PH_MENU || sDm.phase == PH_TARGET || sDm.phase == PH_LIVE) {
        menuDraw();
    } else if (sDm.phase == PH_RUN && sDm.armed && !sDm.hit && !sDm.held) {
        hint(portTrGame("Hold {A}: the wind-up"));
    }
    bannerDraw();
    portRandSetState(seed);
}

/* each frame (statusWinMain's, in battle and out): the battle's dungeon master on or off, and the drawing */
static void frameHook(void) {
    const int on = battleOn();
    if (sOwed.entry != NULL &&
        (!(sOwed.entry->flags & EVENT_ACTIVE) || sOwed.entry->eventId != sOwed.id || !evtCheckID(sOwed.id))) {
        sOwed.entry = NULL; /* (not called again: its event is gone) */
    }
    if (!on) {
        sOwed.entry = NULL; /* (the battle's events are gone with it) */
        if (gPortDmLive) {
            gPortDmLive = 0;
            resetTurn();
            sDm.banner = NULL;
            sDm.noPadTold = 0;
        }
        return;
    }
    if (!padHere(portDmPlayer())) {
        if (!sDm.noPadTold) {
            sDm.noPadTold = 1;
            say("Dungeon master: no controller for its player, so the enemies play themselves.");
        }
        resetTurn();
        gPortDmLive = sOwed.entry != NULL ? DM_OWED : 0; /* (the hooks still call in for the held call) */
    } else {
        gPortDmLive = DM_ON;
    }
    if (sDm.banner != NULL && sDm.bannerAge < BANNER_FRAMES) {
        sDm.bannerAge++;
    }
    /* a turn whose event is over: done */
    if (sDm.evtId != 0 && _battleWorkPointer != NULL) {
        BattleUnit* u = unitById(sDm.unitId);
        if (u == NULL || u->attackEventId != sDm.evtId || _battleWorkPointer->activeUnitId != sDm.unitId) {
            resetTurn();
        }
    }
    if (sDm.phase == PH_MENU || sDm.phase == PH_TARGET || sDm.phase == PH_LIVE) {
        /* (its own controller: in a shared battle the channel its place's character crosses on, which on the
           dungeon master's own game is this computer's pad, not the seat number's) */
        sIn.trg |= dmButtons(gp->buttonNew);
        sIn.rep |= dmButtons(gp->buttonRepeat);
        sIn.dir |= dmButtons(gp->dirsRepeat);
    } else {
        inputSeen();
    }
    dispEntry(CAMERA_2D, 905.0f, 1, dmDisp, NULL);
}

/* a save state or rewind put back (portSnapMetaPut): the turn kept here is outside the state, so it goes
   (its held entry, options and pointers were the other moment's) */
void portDmStateLoaded(void) {
    resetTurn();
    sOwed.entry = NULL;
    if (gPortDmLive == DM_OWED) {
        gPortDmLive = 0;
    }
}

void portDmRegister(void) {
    portDmPlayer();
    resetTurn();
    portModHostAddHook(1, kSlot_statusWinMain, (void*)frameHook);
}
