/* The action-command dojo's judging, its window over the battle, the kept
 * results and timing assist by command type (port/include/port/dojo.h,
 * docs/DOJO.md).
 *
 * Each judgement is read the way the game makes it: a timed-A command
 * (battleAcResult_TimingA) and a guard (BattleActionCommandCheckDefence)
 * look back over the pad record for a press within the last N frames, so
 * the press's place in that record says how far it was from the window; a
 * press that comes after the judgement (none in the record) is waited for a
 * few frames, to say how late it was. The Hammer (battleAcMain_StickKeepLeft)
 * is read from its own work as it lets go; a Stylish move from the frame
 * BattleAcrobatMain decided on against its window. Other commands (gauges,
 * mashing, aiming) say success or not, as their result does.
 *
 * All of it only reads the game, and draws (the font's random numbers kept
 * as they were, battle_popup_pc.c's way; time in battle frames, never the
 * clock), except timing assist, which is settings ("ac_assist_<type>") an
 * online host sends like the others. */
#include "port/file_pc.h"
#include "port/file_write.h"
#include "port/dojo.h"
#include "battle/battle.h"
#include "battle/battle_ac.h"
#include "battle/battle_event_default.h"
#include "battle/battle_pad.h"
#include "battle/battle_unit.h"
#include "battle/ac/ac_timing_a.h"
#include "driver/dispdrv.h"
#include "driver/msgdrv.h"
#include "driver/windowdrv.h"
#include "manager/fontmgr.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "port/gameplus.h"
#include "port/platform.h"
#include "port/settings.h"
#ifdef TTYD_BACKEND_AURORA
#include "port/tr.h"
#else
#define portTrGame(english) (english) /* (the headless build has no translations) */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

s32 battleAcResult_TimingA(BattleWork* work);
s32 battleAcMain_StickKeepLeft(BattleWork* work);
extern s32 ac_stickkeepleft_ok_frame[7]; /* ac_stick_keep_left.c */

#define BUTTON_A 0x100
#define BUTTON_B 0x200
#define LATE_WAIT 15     /* frames a press is waited for after a judgement with none */
#define SHOW_FRAMES 210  /* a verdict's time on screen */
#define HINT_FRAMES 300  /* the dojo's line at a battle's start */

/* --- the types --- */

static const char* const kKeys[PORT_DOJO_TYPES] = {
    "jump",   "hammer",  "special", "goombella", "koops", "flurrie",    "yoshi",
    "vivian", "bobbery", "mowz",    "guard",     "superguard", "stylish",
};
static const char* const kNames[PORT_DOJO_TYPES] = {
    "Jump",   "Hammer",  "Special moves", "Goombella", "Koops",      "Flurrie", "Yoshi",
    "Vivian", "Bobbery", "Ms. Mowz",      "Guard",     "Superguard", "Stylish",
};

const char* portDojoTypeKey(int type) {
    return type >= 0 && type < PORT_DOJO_TYPES ? kKeys[type] : "";
}

const char* portDojoTypeName(int type) {
    return type >= 0 && type < PORT_DOJO_TYPES ? kNames[type] : "";
}

int portDojoTypeGroup(int type) {
    return type == PORT_DOJO_GUARD || type == PORT_DOJO_SUPERGUARD ? 1 : 0;
}

/* the type's name in the game's text, for its window: the battle menu's and
 * the partners' own names; the port's for the guards and Stylish moves */
static const char* gameTypeName(int type) {
    static const char* const kMsg[PORT_DOJO_TYPES] = {
        "btl_cmd_act_jump", "btl_cmd_act_hammer", "btl_cmd_act_super_action", "name_party0", "name_party1",
        "name_party4", NULL, "name_party5", "name_party2", "name_party6", NULL, NULL, NULL,
    };
    const char* s;
    if (type == PORT_DOJO_YOSHI) {
        s = pouchGetYoshiName();
        return s != NULL && s[0] != '\0' ? s : "Yoshi";
    }
    if (type >= 0 && type < PORT_DOJO_TYPES && kMsg[type] != NULL && (s = msgSearch(kMsg[type])) != NULL) {
        return s;
    }
    return portTrGame(portDojoTypeName(type));
}

/* the command type of what unit does now (wp's command menu says Mario's;
 * running: the command set up is this one, whose kind says it too) */
static int typeOfUnit(BattleWork* wp, BattleUnit* u, int running) {
    if (u == NULL) {
        return -1;
    }
    switch (u->currentKind) {
        case UNIT_MARIO:
            if (running && wp->actionCommands.maincb == battleAcMain_TimingA) {
                return PORT_DOJO_JUMP;
            }
            if (running && wp->actionCommands.maincb == battleAcMain_StickKeepLeft) {
                return PORT_DOJO_HAMMER;
            }
            return wp->commandMenu.actType == 0 ? PORT_DOJO_JUMP
                   : wp->commandMenu.actType == 1 ? PORT_DOJO_HAMMER
                                                  : PORT_DOJO_SPECIAL;
        case UNIT_GOOMBELLA: return PORT_DOJO_GOOMBELLA;
        case UNIT_KOOPS: return PORT_DOJO_KOOPS;
        case UNIT_FLURRIE: return PORT_DOJO_FLURRIE;
        case UNIT_YOSHI: return PORT_DOJO_YOSHI;
        case UNIT_VIVIAN: return PORT_DOJO_VIVIAN;
        case UNIT_BOBBERY: return PORT_DOJO_BOBBERY;
        case UNIT_MS_MOWZ: return PORT_DOJO_MOWZ;
        default: return -1;
    }
}

/* --- settings --- */

static int* sAssist[PORT_DOJO_TYPES]; /* "ac_assist_<key>": 0 off .. 3 most */
static int* sFeedback;                /* "dojo_feedback": the judging's window in every battle */

void portDojoDeclare(void) {
    int i;
    if (sFeedback != NULL) {
        return;
    }
    for (i = 0; i < PORT_DOJO_TYPES; i++) {
        char key[48];
        snprintf(key, sizeof(key), "ac_assist_%s", kKeys[i]);
        sAssist[i] = portSettingsDeclare(key, 0, 0, 3);
    }
    sFeedback = portSettingsDeclare("dojo_feedback", 0, 0, 1);
}

static int assistLevel(int type) {
    portDojoDeclare();
    if (type < 0 || type >= PORT_DOJO_TYPES ||
        portGameplus(portDojoTypeGroup(type) ? "ac_guard_assist" : "ac_attack_assist") != 3) {
        return 0;
    }
    return *sAssist[type];
}

int portDojoAssistAttack(BattleUnit* unit, int difficulty) {
    const int level = assistLevel(typeOfUnit(_battleWorkPointer, unit, 0));
    return difficulty > level ? difficulty - level : 0; /* one Simplifier's step a level (3: the "Wider windows" choice) */
}

void portDojoAssistGuard(int* normal, int* superguard) {
    const int assist = portGameplus("ac_guard_assist");
    if (assist == 1) { /* Wider windows */
        *normal *= 2;
        *superguard += 2;
    } else if (assist == 3) { /* by type: up to the same, a third of it a level */
        *normal += *normal * assistLevel(PORT_DOJO_GUARD) / 3;
        *superguard += assistLevel(PORT_DOJO_SUPERGUARD);
    }
}

/* --- running --- */

static int sRunning;
static unsigned sFrame; /* battle frames seen (the windows' clock) */

void portDojoSetRunning(int on) {
    sRunning = on;
}

int portDojoRunning(void) {
    return sRunning;
}

int portDojoWanted(void) {
    portDojoDeclare();
    return sRunning || *sFeedback;
}

/* --- results --- */

static PortDojoStats sLife[PORT_DOJO_TYPES], sSession[PORT_DOJO_TYPES];
static int sLifeRead, sLifeDirty;

static void statsPath(char* out, size_t size) {
    snprintf(out, size, "%s/dojo.txt", portSettingsUserPath());
}

static void readLife(void) {
    char path[1024], line[160];
    FILE* f;
    if (sLifeRead) {
        return;
    }
    sLifeRead = 1;
    statsPath(path, sizeof(path));
    f = portFopen(path, "r");
    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        char key[32];
        PortDojoStats s;
        int i;
        memset(&s, 0, sizeof(s));
        if (sscanf(line, "%31s %d %d %d %d %d %f", key, &s.attempts, &s.hits, &s.measured, &s.early, &s.late,
                   &s.offsetSum) < 3) {
            continue;
        }
        for (i = 0; i < PORT_DOJO_TYPES; i++) {
            if (strcmp(key, kKeys[i]) == 0) {
                sLife[i] = s;
            }
        }
    }
    fclose(f);
}

static void writeLife(void) {
    char path[1024];
    FILE* f;
    int i;
    if (!sLifeDirty) {
        return;
    }
    sLifeDirty = 0;
    statsPath(path, sizeof(path));
    f = portFileWriteOpen(path, "w");
    if (f == NULL) {
        return;
    }
    fprintf(f, "# Paper Cut's action-command dojo: type attempts hits measured early late offset-sum (frames)\n");
    for (i = 0; i < PORT_DOJO_TYPES; i++) {
        const PortDojoStats* s = &sLife[i];
        fprintf(f, "%s %d %d %d %d %d %.1f\n", kKeys[i], s->attempts, s->hits, s->measured, s->early, s->late,
                (double)s->offsetSum);
    }
    portFileWriteClose(f, path);
}

void portDojoGetStats(int type, int session, PortDojoStats* out) {
    readLife();
    memset(out, 0, sizeof(*out));
    if (type >= 0 && type < PORT_DOJO_TYPES) {
        *out = session ? sSession[type] : sLife[type];
    }
}

void portDojoResetStats(void) {
    readLife();
    memset(sLife, 0, sizeof(sLife));
    memset(sSession, 0, sizeof(sSession));
    sLifeDirty = 1;
    writeLife();
}

int portDojoSuggest(int type) {
    PortDojoStats s;
    int pct;
    portDojoGetStats(type, 0, &s);
    if (s.attempts < 5) {
        return -1;
    }
    pct = s.hits * 100 / s.attempts;
    return pct >= 85 ? 0 : pct >= 65 ? 1 : pct >= 45 ? 2 : 3;
}

int portDojoApplySuggestions(void) {
    int i, changed = 0;
    portDojoDeclare();
    for (i = 0; i < PORT_DOJO_TYPES; i++) {
        const int s = portDojoSuggest(i);
        char key[48], value[8];
        if (s < 0 || s == *sAssist[i]) {
            continue;
        }
        snprintf(key, sizeof(key), "ac_assist_%s", kKeys[i]);
        snprintf(value, sizeof(value), "%d", s);
        portSettingsSetKey(key, value);
        changed++;
    }
    portSettingsSetKey("ac_attack_assist", "3");
    portSettingsSetKey("ac_guard_assist", "3");
    portSettingsSave();
    return changed;
}

static void addTo(PortDojoStats* s, int hit, int measured, float offset) {
    s->attempts++;
    s->hits += hit ? 1 : 0;
    if (measured) {
        s->measured++;
        s->offsetSum += offset;
        s->early += offset < 0.0f && !hit ? 1 : 0;
        s->late += offset > 0.0f && !hit ? 1 : 0;
    }
}

/* --- verdicts --- */

enum {
    V_HIT,      /* in the window */
    V_EARLY,    /* frames before it */
    V_LATE,     /* frames after it */
    V_NONE,     /* no press at all */
    V_MASH,     /* pressed more than once */
    V_NO_SUPER, /* B on an attack that can't be Superguarded */
    V_LONG,     /* the Hammer held too long */
    V_DONE,     /* a command without frames to tell: done */
    V_MISSED,   /* ...or not */
};

typedef struct Verdict {
    int type, kind;
    int window;      /* frames */
    int superWindow; /* a guard's: the last frames of window that Superguard (0 none) */
    int press;       /* where it was, in frames from the window's first (-99: none) */
    int frames;      /* how early or late */
    char button;     /* 'A', 'B' or 0 */
    unsigned shownAt;
} Verdict;

static Verdict sVerdict;
static int sHaveVerdict;

/* window frames, the press at t frames from its first */
static void verdict(int type, int window, int superWindow, int t, char button, int kind) {
    Verdict* v = &sVerdict;
    const int hit = kind == V_HIT || kind == V_DONE;
    const int measured = kind == V_HIT || kind == V_EARLY || kind == V_LATE || kind == V_LONG;
    /* frames outside the window, as the verdict says (below 0 early; 0 in time) */
    const int outside = kind == V_EARLY ? t : kind == V_LATE || kind == V_LONG ? t - window + 1 : 0;
    const float offset = (float)outside;
    memset(v, 0, sizeof(*v));
    v->type = type;
    v->kind = kind;
    v->window = window;
    v->superWindow = superWindow;
    v->press = measured ? t : -99;
    v->frames = kind == V_EARLY ? -t : kind == V_LATE || kind == V_LONG ? t - window + 1 : 0;
    v->button = button;
    v->shownAt = sFrame;
    sHaveVerdict = 1;
    if (kind == V_NO_SUPER) {
        return; /* not a try at anything this attack allows */
    }
    readLife();
    addTo(&sSession[type], hit, measured, offset);
    if (sRunning) {
        addTo(&sLife[type], hit, measured, offset);
        sLifeDirty = 1;
        writeLife();
    }
    printf("[dojo] %s: %s %d (press at %d of a %d-frame window)\n", kKeys[type],
           kind == V_HIT ? "in time" : kind == V_EARLY ? "early" : kind == V_LATE ? "late" : kind == V_LONG ? "too long"
           : kind == V_NONE ? "no press" : kind == V_MASH ? "mashed" : kind == V_DONE ? "done" : "missed",
           v->frames, t, window);
    fflush(stdout);
}

/* a press within or before the window: in time or early */
static void timed(int type, int window, int superWindow, int t, char button) {
    verdict(type, window, superWindow, t, button, t >= window ? V_LATE : t >= 0 ? V_HIT : V_EARLY);
}

/* --- a press waited for after the judgement --- */

typedef struct Pending {
    int on;
    int type, window, superWindow, superOk;
    int guard;                /* 1: a guard's (A guards, B Superguards); 0 a timed A */
    const BattleWorkPad* pad; /* the record it was judged from */
    int waited;
} Pending;
static Pending sPending;

static void pendingFrame(void) {
    Pending* p = &sPending;
    u32 now;
    if (!p->on) {
        return;
    }
    p->waited++;
    now = p->pad->buttonsPressedHistory[0];
    if (p->guard && (now & BUTTON_B)) {
        p->on = 0;
        if (!p->superOk) {
            verdict(PORT_DOJO_SUPERGUARD, p->superWindow, p->superWindow, 0, 'B', V_NO_SUPER);
        } else {
            verdict(PORT_DOJO_SUPERGUARD, p->superWindow, p->superWindow, p->superWindow - 1 + p->waited, 'B', V_LATE);
        }
    } else if (now & BUTTON_A) {
        p->on = 0;
        verdict(p->type, p->window, p->superWindow, p->window - 1 + p->waited, 'A', V_LATE);
    } else if (p->waited >= LATE_WAIT) {
        p->on = 0;
        verdict(p->type, p->window, p->superWindow, -99, 0, V_NONE);
    }
}

static void waitLate(int type, int window, int superWindow, int superOk, int guard, const BattleWorkPad* pad) {
    if (sPending.on) {
        sPending.waited = LATE_WAIT; /* a new judgement: the one before had no press */
        sPending.pad = pad;
        pendingFrame();
    }
    sPending.on = 1;
    sPending.type = type;
    sPending.window = window;
    sPending.superWindow = superWindow;
    sPending.superOk = superOk;
    sPending.guard = guard;
    sPending.pad = pad;
    sPending.waited = 0;
}

/* the newest press of mask in the record, frames ago (from), or -1 */
static int newest(const BattleWorkPad* pad, u32 mask, int from) {
    int i;
    for (i = from; i < 30; i++) {
        if (pad->buttonsPressedHistory[i] & mask) {
            return i;
        }
    }
    return -1;
}

static int presses(const BattleWorkPad* pad, u32 mask, int frames) {
    int i, n = 0;
    for (i = 0; i < frames && i < 30; i++) {
        n += (pad->buttonsPressedHistory[i] & mask) != 0;
    }
    return n;
}

/* --- timed A (jumps, Headbonk) --- */

static int sTimingWindow; /* before the result: its window (a bounce narrows it after) */
static int sTimingSkip;   /* played by itself: nothing to judge */

void portDojoBeforeResult(BattleWork* wp) {
    BattleActions* ac = &wp->actionCommands;
    sTimingWindow = 0;
    if (!portDojoWanted() || ac->resultcb != battleAcResult_TimingA || ac->acUnit == NULL) {
        return;
    }
    sTimingSkip = ac->acUnit->badgesEquipped.AutoCommand || (wp->flags & BATTLE_FLAG_LECTURE_JUMP) ||
                  (ac->acUnit->tokenFlags & TOKEN_FLAG_CONFUSED_ACTION) || ac->acUnit->alliance != 0;
    sTimingWindow = battleAcTimingA_GetSuccessFrame(wp);
}

/* --- the generic: other commands, done or not --- */

static int sGenericDone; /* this command's result counted */

void portDojoAfterResult(BattleWork* wp) {
    BattleActions* ac = &wp->actionCommands;
    if (!portDojoWanted() || ac->acUnit == NULL || ac->acUnit->alliance != 0) {
        return;
    }
    if (ac->resultcb == battleAcResult_TimingA) {
        const BattleWorkPad* pad = &wp->padWork[0];
        const int w = sTimingWindow, type = typeOfUnit(wp, ac->acUnit, 1);
        const int out = ac->outputParams[0];
        int i;
        if (sTimingSkip || w <= 0 || type < 0) {
            return;
        }
        if (out == 3) {
            i = newest(pad, BUTTON_A, 0);
            timed(type, w, 0, i >= 0 && i < w ? w - 1 - i : 0, 'A');
        } else if (out == 1 && presses(pad, BUTTON_A, 20) >= 2) {
            verdict(type, w, 0, -99, 'A', V_MASH);
        } else if ((i = newest(pad, BUTTON_A, 0)) >= 0) {
            timed(type, w, 0, w - 1 - i, 'A'); /* (out 1: one press, before the window) */
        } else if (out == 2) {
            waitLate(type, w, 0, 0, 0, pad);
        }
        return;
    }
    if (ac->maincb == battleAcMain_StickKeepLeft || (ac->result & 1) || sGenericDone) {
        return; /* the Hammer: as it lets go (hammerFrame); or not decided yet */
    }
    if (!ac->acUnit->badgesEquipped.AutoCommand && !(ac->acUnit->tokenFlags & TOKEN_FLAG_CONFUSED_ACTION)) {
        const int type = typeOfUnit(wp, ac->acUnit, 1);
        if (type >= 0) {
            sGenericDone = 1;
            verdict(type, 0, 0, -99, 0, (ac->result & 2) ? V_DONE : V_MISSED);
        }
    }
}

/* --- the Hammer: hold the stick left, let go after the last light --- */

typedef struct SklView {
    s32 timer, idx; /* ac_stick_keep_left.c's SklWork */
} SklView;
static int sSklOut; /* outputParams[2] last frame */

static void hammerFrame(BattleWork* wp) {
    BattleActions* ac = &wp->actionCommands;
    const SklView* w = (const SklView*)ac->work;
    const int lights = ac->params[0], per = ac->params[1], last = lights + 1;
    int ok, window, type, out;
    if (ac->maincb != battleAcMain_StickKeepLeft || ac->acUnit == NULL || ac->acUnit->alliance != 0) {
        sSklOut = 0;
        return;
    }
    out = ac->outputParams[2];
    if (ac->acState < 1000) {
        sSklOut = 0;
        return;
    }
    if (out == sSklOut || out == 0) {
        sSklOut = out;
        return;
    }
    sSklOut = out;
    if (ac->acUnit->badgesEquipped.AutoCommand || (ac->acFlags & 2) || per <= 0 ||
        (type = typeOfUnit(wp, ac->acUnit, 1)) < 0) {
        return;
    }
    ok = ac_stickkeepleft_ok_frame[BattleActionCommandGetDifficulty(wp)];
    window = ok < per ? ok : per;
    if (out == 3) {
        timed(type, window, 0, per - w->timer - 1, 0);
    } else if (out == 1 && w->idx != last) {
        const int toWindow = (w->timer + 1) + (last - w->idx - 1) * (per + 1) + 1;
        verdict(type, window, 0, -toWindow, 0, V_EARLY);
    } else if (out == 1) {
        verdict(type, window, 0, per - w->timer - 1, 0, V_LATE);
    } else if (out == 2) {
        verdict(type, window, 0, per, 0, V_LONG);
    }
}

/* --- guards --- */

void portDojoGuardJudged(BattleUnit* unit, BattleWeapon* weapon, int normal, int superguard, int mash,
                         const BattleWorkPad* pad, int result) {
    const int superOk = weapon != NULL && weapon->superguardsAllowed && unit != NULL && unit->currentHp > 0;
    int a, b;
    if (!portDojoWanted() || pad == NULL || unit == NULL || unit->alliance != 0 || unit->badgesEquipped.AutoCommand ||
        _battleWorkPointer->actionCommands.countdown > 0) {
        return;
    }
    if (sPending.on) {
        sPending.waited = LATE_WAIT; /* the hit before: no press after all */
        pendingFrame();
    }
    a = newest(pad, BUTTON_A, 0);
    b = newest(pad, BUTTON_B, 0);
    if (result == 5) {
        timed(PORT_DOJO_SUPERGUARD, superguard, superguard, b >= 0 ? superguard - 1 - b : 0, 'B');
    } else if (result == 4 && a >= 0 && a < normal) {
        timed(PORT_DOJO_GUARD, normal, superOk ? superguard : 0, normal - 1 - a, 'A');
    } else if (result == 4) {
        return; /* guarded for the player (Accessibility's automatic guards) */
    } else if (presses(pad, BUTTON_A, mash) + presses(pad, BUTTON_B, mash) >= 2) { /* (as the game counts them) */
        verdict(b >= 0 && (a < 0 || b < a) ? PORT_DOJO_SUPERGUARD : PORT_DOJO_GUARD, b >= 0 && (a < 0 || b < a) ? superguard : normal,
                0, -99, 0, V_MASH);
    } else if (b >= 0 && (a < 0 || b < a)) {
        if (!superOk) {
            verdict(PORT_DOJO_SUPERGUARD, superguard, superguard, 0, 'B', V_NO_SUPER);
        } else {
            timed(PORT_DOJO_SUPERGUARD, superguard, superguard, superguard - 1 - b, 'B');
        }
    } else if (a >= 0) {
        timed(PORT_DOJO_GUARD, normal, superOk ? superguard : 0, normal - 1 - a, 'A');
    } else {
        waitLate(PORT_DOJO_GUARD, normal, superOk ? superguard : 0, superOk, 1, pad);
    }
}

/* --- Stylish moves --- */

static int sStylishArmed, sStylishStart, sStylishEnd, sStylishEndFrame;

void portDojoStylishStart(BattleWork* wp, int* windowStart, int* windowEnd, int endFrame) {
    const int level = assistLevel(PORT_DOJO_STYLISH);
    (void)wp;
    if (level > 0) {
        *windowStart = *windowStart > level ? *windowStart - level : 0;
        *windowEnd = *windowEnd + level < endFrame ? *windowEnd + level : endFrame;
    }
    sStylishArmed = 1;
    sStylishStart = *windowStart;
    sStylishEnd = *windowEnd;
    sStylishEndFrame = endFrame;
}

void portDojoStylishFrame(BattleWork* wp) {
    const int r = wp->stylishResult;
    const int window = sStylishEnd - sStylishStart, c = wp->stylishCurFrame;
    BattleUnit* unit;
    if (!sStylishArmed || r == 0) {
        return;
    }
    sStylishArmed = 0;
    unit = BattleGetUnitPtr(wp, wp->stylishUnitId);
    if (!portDojoWanted() || window <= 0 || unit == NULL || unit->alliance != 0 || unit->badgesEquipped.AutoCommand) {
        return;
    }
    if (r == 2) {
        timed(PORT_DOJO_STYLISH, window, 0, c - sStylishStart - 1, 'A');
    } else if (r == -1) {
        timed(PORT_DOJO_STYLISH, window, 0, c <= 1 ? -sStylishStart - 1 : c - sStylishStart - 1, 'A');
    } else if (r == -2) {
        timed(PORT_DOJO_STYLISH, window, 0, c - sStylishStart - 1, 'A');
    } else if (sRunning) {
        /* no press: shown, not counted (most moves can be done without one) */
        sVerdict.type = PORT_DOJO_STYLISH;
        sVerdict.kind = V_NONE;
        sVerdict.window = window;
        sVerdict.superWindow = 0;
        sVerdict.press = -99;
        sVerdict.frames = 0;
        sVerdict.button = 0;
        sVerdict.shownAt = sFrame;
        sHaveVerdict = 1;
    }
}

/* --- the dojo's dummy and refills (set by dojo_aurora.c) --- */

static const void* sDummyKind;  /* the dummy's unit kind (a copy of the fighter's) */
static EvtWord* sDummyAttack;   /* its own attack script */
static int sAttacks;            /* PORT_DOJO_DUMMY_* */
static unsigned sBattleFrames;  /* frames since the battle's first command menu (0: not yet) */

void portDojoSetDummyKind(const void* kind) {
    sDummyKind = kind;
    sDummyAttack = NULL;
}

void portDojoSetAttacks(int attacks) {
    sAttacks = attacks;
}

int portDojoAttacks(void) {
    return sAttacks;
}

/* Defend chosen this turn by Mario or the partner */
static int defendChosen(BattleWork* wp) {
    BattleUnit* m = BattleGetMarioPtr(wp);
    BattleUnit* p = BattleGetPartyPtr(wp);
    return (m != NULL && m->attackEventCode == btldefaultevt_Defend) ||
           (p != NULL && p->attackEventCode == btldefaultevt_DefendParty);
}

static void dojoUnits(BattleWork* wp) {
    int i;
    PouchData* pouch = pouchGetPtr();
    const int menu = (wp->flags & BATTLE_FLAG_COMMAND_INPUT) != 0;
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        BattleUnit* u = wp->units[i];
        if (u == NULL) {
            continue;
        }
        if (u->alliance == 1 && sDummyKind != NULL && u->kindParams == sDummyKind) {
            /* never falls: its HP back after each hit */
            if (u->currentHp > 0 && u->currentHp < u->maxHp) {
                u->currentHp = u->maxHp;
            }
            /* attacks when asked: its own script, or the game's "does nothing this turn" */
            if (u->attackEventCode != btldefaultevt_Wait) {
                sDummyAttack = u->attackEventCode;
            }
            if (!(u->tokenFlags & TOKEN_FLAG_CURRENTLY_ACTING) && sDummyAttack != NULL) {
                const int attack = sAttacks == PORT_DOJO_DUMMY_EVERY_TURN ||
                                   (sAttacks == PORT_DOJO_DUMMY_ON_DEFEND && defendChosen(wp));
                u->attackEventCode = attack ? sDummyAttack : btldefaultevt_Wait;
            }
        } else if (u->alliance == 0 && menu && u->currentHp > 0) {
            /* Mario's side: full again for each turn's command */
            u->currentHp = u->maxHp;
            if (u->currentKind == UNIT_MARIO) {
                u->currentFp = u->maxFp;
            }
        }
    }
    if (menu && pouch != NULL) {
        pouch->currentSP = pouch->maximumSP;
    }
}

/* --- the window over the battle --- */

static const GXColor kInk = {0x20, 0x20, 0x20, 0xFF};
static const GXColor kHit = {0x18, 0x90, 0x30, 0xFF};
static const GXColor kEarly = {0xD0, 0x70, 0x10, 0xFF};
static const GXColor kLate = {0x20, 0x60, 0xC8, 0xFF};
static const GXColor kMiss = {0xC0, 0x20, 0x20, 0xFF};

static GXColor withAlpha(GXColor c, u8 a) {
    c.a = (u8)(c.a * a / 0xFF);
    return c;
}

static void text(f32 x, f32 y, f32 scale, GXColor color, const char* s) {
    FontDrawStart();
    FontDrawScale(scale);
    FontDrawColor(color);
    FontDrawString(x, y, s);
}

static void textCentered(f32 cx, f32 y, f32 scale, GXColor color, const char* s) {
    text(cx - (f32)FontGetMessageWidth(s) * scale * 0.5f, y, scale, color, s);
}

/* a row of frame cells: [from, to] in frames from the window's first, the
 * window's own in colour (a guard's Superguard frames at its end in gold), the press marked */
static void frameBar(f32 cx, f32 y, int window, int superWindow, int press, char button, u8 alpha, int now) {
    const f32 cell = 11.0f, step = 13.0f;
    int from = -3, to = window + 2, t;
    f32 x0;
    if (press != -99 && press < from) {
        from = press;
    }
    if (press != -99 && press > to) {
        to = press;
    }
    if (now != -99 && now < from) {
        from = now;
    }
    if (to - from > 28) { /* far off: the press at the bar's end */
        if (press != -99 && press < 0) {
            from = to - 28;
        } else {
            to = from + 28;
        }
    }
    x0 = cx - (f32)(to - from + 1) * step * 0.5f;
    for (t = from; t <= to; t++) {
        const int inWindow = t >= 0 && t < window;
        const int super = inWindow && superWindow > 0 && t >= window - superWindow;
        const int pressed = press != -99 && (t == press || (t == from && press < from) || (t == to && press > to));
        GXColor c = super ? (GXColor){0xF0, 0xC0, 0x30, 0xFF} : inWindow ? (GXColor){0x70, 0xB0, 0xF0, 0xFF}
                                                                     : (GXColor){0xD8, 0xD0, 0xC0, 0xFF};
        if (button == 0 && inWindow && superWindow == 0) {
            c = (GXColor){0x80, 0xD0, 0x80, 0xFF}; /* an attack's window */
        }
        if (now != -99 && t < now) {
            c.r = (u8)(c.r * 3 / 5);
            c.g = (u8)(c.g * 3 / 5);
            c.b = (u8)(c.b * 3 / 5);
        }
        windowDispGX_Waku_col(0, withAlpha(c, alpha), x0 + (f32)(t - from) * step, y, cell, cell, 3.0f);
        if (pressed) {
            const f32 px = x0 + (f32)(t - from) * step;
            windowDispGX_Waku_col(0, withAlpha(kMiss, alpha), px + 1.0f, y + 5.0f, cell - 2.0f, 4.0f, 1.0f);
            if (button != 0) {
                char b[2] = {button, 0};
                textCentered(px + cell * 0.5f, y + 22.0f, 0.55f, withAlpha(kInk, alpha), b);
            }
        }
        if (now != -99 && t == now) {
            windowDispGX_Waku_col(0, withAlpha(kInk, alpha), x0 + (f32)(t - from) * step + 3.0f, y - cell - 2.0f,
                                  cell - 6.0f, 4.0f, 1.0f);
        }
    }
}

static void verdictText(const Verdict* v, char* out, size_t size) {
    switch (v->kind) {
        case V_HIT: snprintf(out, size, "%s", portTrGame("Perfect!")); break;
        case V_EARLY:
            if (v->frames == 1) {
                snprintf(out, size, "%s", portTrGame("1 frame early"));
            } else {
                snprintf(out, size, portTrGame("%d frames early"), v->frames);
            }
            break;
        case V_LATE:
            if (v->frames == 1) {
                snprintf(out, size, "%s", portTrGame("1 frame late"));
            } else {
                snprintf(out, size, portTrGame("%d frames late"), v->frames);
            }
            break;
        case V_LONG: snprintf(out, size, portTrGame("Held too long (%d frames late)"), v->frames); break;
        case V_NONE: snprintf(out, size, "%s", portTrGame("No press")); break;
        case V_MASH: snprintf(out, size, "%s", portTrGame("Too many presses")); break;
        case V_NO_SUPER: snprintf(out, size, "%s", portTrGame("This attack can't be Superguarded")); break;
        case V_DONE: snprintf(out, size, "%s", portTrGame("Success")); break;
        default: snprintf(out, size, "%s", portTrGame("Missed")); break;
    }
}

static GXColor verdictColor(const Verdict* v) {
    switch (v->kind) {
        case V_HIT:
        case V_DONE: return kHit;
        case V_EARLY: return kEarly;
        case V_LATE:
        case V_LONG: return kLate;
        default: return kMiss;
    }
}

static void tallyText(int type, char* out, size_t size) {
    const PortDojoStats* s = &sSession[type];
    char avg[64] = "";
    if (s->measured > 0) {
        const float a = s->offsetSum / (float)s->measured;
        if (a <= -0.5f) {
            snprintf(avg, sizeof(avg), portTrGame("%.1f frames early on average"), (double)-a);
        } else if (a >= 0.5f) {
            snprintf(avg, sizeof(avg), portTrGame("%.1f frames late on average"), (double)a);
        } else {
            snprintf(avg, sizeof(avg), "%s", portTrGame("on time on average"));
        }
    }
    snprintf(out, size, portTrGame("%d of %d"), s->hits, s->attempts);
    if (avg[0] != '\0') {
        const size_t n = strlen(out);
        snprintf(out + n, size - n, "  /  %s", avg);
    }
}

/* the live bar: a Stylish check running, or a guard or timed A the Action
 * Command cue can see coming (ac_cue_pc.c) */
extern int gPortAcCueToJudge, gPortAcCueWindow, gPortAcCueKind;

static void liveBar(BattleWork* wp, u8 alpha) {
    if (sStylishArmed && wp->stylishResult == 0 && sStylishEnd > sStylishStart) {
        const int window = sStylishEnd - sStylishStart;
        textCentered(0.0f, -82.0f, 0.6f, withAlpha(kInk, alpha), portTrGame("Stylish"));
        frameBar(0.0f, -100.0f, window, 0, -99, 'A', alpha, wp->stylishCurFrame - sStylishStart - 1);
    } else if (gPortAcCueToJudge >= 0 && gPortAcCueWindow > 0 && gPortAcCueToJudge <= 40) {
        const int window = gPortAcCueWindow;
        int sg = 0;
        if (gPortAcCueKind == 2) {
            int n = 8, s = 3; /* (the Superguard's share of it, as judged with no badges: speed aside) */
            portDojoAssistGuard(&n, &s);
            sg = s < window ? s : window;
        }
        textCentered(0.0f, -82.0f, 0.6f, withAlpha(kInk, alpha), portTrGame(gPortAcCueKind == 2 ? "Guard" : "Timed press"));
        frameBar(0.0f, -100.0f, window, sg, -99, gPortAcCueKind == 2 ? 'A' : 0, alpha, window - 1 - gPortAcCueToJudge);
    }
}

static void disp(CameraId cam, void* param) {
    BattleWork* wp = (BattleWork*)param;
    const u32 seed = portRandState(); /* the font's random numbers: as they were */
    char line[160], tally[160];
    (void)cam;
    if (sHaveVerdict) {
        const Verdict* v = &sVerdict;
        const unsigned age = sFrame - v->shownAt;
        const u8 alpha = age < SHOW_FRAMES - 30 ? 0xFF : (u8)(0xFF * (SHOW_FRAMES - age) / 30);
        const f32 w = 420.0f, h = 92.0f, x0 = -w * 0.5f, y0 = -118.0f;
        char head[96];
        windowDispGX_Waku_col(0, (GXColor){0xFF, 0xFF, 0xFF, (u8)(0xF0 * alpha / 0xFF)}, x0, y0, w, h, 10.0f);
        snprintf(head, sizeof(head), "%s:", gameTypeName(v->type));
        verdictText(v, line, sizeof(line));
        {
            const f32 hw = (f32)FontGetMessageWidth(head) * 0.8f, lw = (f32)FontGetMessageWidth(line) * 0.8f;
            const f32 x = -(hw + 10.0f + lw) * 0.5f;
            text(x, y0 - 8.0f, 0.8f, withAlpha(kInk, alpha), head);
            text(x + hw + 10.0f, y0 - 8.0f, 0.8f, withAlpha(verdictColor(v), alpha), line);
        }
        if (v->window > 0 && v->kind != V_NO_SUPER && v->kind != V_DONE && v->kind != V_MISSED) {
            frameBar(0.0f, y0 - 46.0f, v->window, v->superWindow, v->press, v->button, alpha, -99);
        }
        if (v->kind == V_HIT && v->window > 0) {
            snprintf(line, sizeof(line), portTrGame("frame %d of %d"), v->press + 1, v->window);
        } else { /* (the bar shows the window's frames) */
            line[0] = '\0';
        }
        /* the tally once this kind has a try counted ("0 of 0" read as nothing counted: a Stylish move left
           without a press is shown, not counted) */
        if (sSession[v->type].attempts > 0) {
            tallyText(v->type, tally, sizeof(tally));
        } else {
            tally[0] = '\0';
        }
        if (tally[0] == '\0') {
            /* (the frame line alone, or nothing) */
        } else if (line[0] != '\0') {
            const size_t n = strlen(line);
            snprintf(line + n, sizeof(line) - n, "  /  %s", tally);
        } else {
            snprintf(line, sizeof(line), "%s", tally);
        }
        textCentered(0.0f, y0 - 66.0f, 0.55f, withAlpha((GXColor){0x50, 0x50, 0x50, 0xFF}, alpha), line);
    }
    liveBar(wp, 0xFF);
    if (sRunning && sBattleFrames > 0 && sBattleFrames < HINT_FRAMES) {
        const u8 alpha = sBattleFrames < HINT_FRAMES - 30 ? 0xFF : (u8)(0xFF * (HINT_FRAMES - sBattleFrames) / 30);
        const char* hint = portTrGame(sAttacks == PORT_DOJO_DUMMY_EVERY_TURN ? "Dojo: the dummy attacks every turn. Run Away to leave."
                                      : sAttacks == PORT_DOJO_DUMMY_ON_DEFEND
                                          ? "Dojo: choose Defend and the dummy attacks. Run Away to leave."
                                          : "Dojo: the dummy never attacks. Run Away to leave.");
        const f32 w = (f32)FontGetMessageWidth(hint) * 0.55f + 32.0f;
        windowDispGX_Waku_col(0, (GXColor){0xFF, 0xFF, 0xFF, (u8)(0xF0 * alpha / 0xFF)}, -w * 0.5f, 134.0f, w, 30.0f, 8.0f);
        textCentered(0.0f, 127.0f, 0.55f, withAlpha(kInk, alpha), hint);
    }
    portRandSetState(seed);
}

void portDojoBattleStart(void) {
    sBattleFrames = 0;
    sHaveVerdict = 0;
    sPending.on = 0;
    sStylishArmed = 0;
    sSklOut = 0;
    sDummyAttack = NULL;
}

void portDojoBattleFrame(BattleWork* wp) {
    BattleActions* ac = &wp->actionCommands;
    if (sBattleFrames > 0 || (wp->flags & BATTLE_FLAG_COMMAND_INPUT)) {
        sBattleFrames++;
    }
    if (sRunning) {
        dojoUnits(wp);
    }
    if (!portDojoWanted()) {
        sHaveVerdict = 0;
        sPending.on = 0;
        return;
    }
    sFrame++;
    if (ac->result & 1) {
        sGenericDone = 0;
    }
    pendingFrame();
    hammerFrame(wp);
    portDojoStylishFrame(wp); /* (decided last frame, in btlevtcmd_ACRStart) */
    if (sHaveVerdict && (sFrame - sVerdict.shownAt >= SHOW_FRAMES ||
                         ((wp->flags & BATTLE_FLAG_COMMAND_INPUT) && sFrame - sVerdict.shownAt >= 60 &&
                          sVerdict.shownAt + SHOW_FRAMES - 30 > sFrame))) {
        if (sFrame - sVerdict.shownAt >= SHOW_FRAMES) {
            sHaveVerdict = 0;
        } else {
            sVerdict.shownAt = sFrame + 30 - SHOW_FRAMES; /* the menu is up: fade it out now */
        }
    }
    dispEntry(CAMERA_2D, 905.0f, 1, disp, wp);
}

/* the dojo started: its results since */
void portDojoSessionStart(void) {
    memset(sSession, 0, sizeof(sSession));
    readLife();
}
