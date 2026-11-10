/* Idle players in the audience (docs/PLAYERS.md, "In the audience"; port/include/port/crowd.h).
 *
 * In co-op (one computer or a shared online battle), a player with no character in the fight
 * (players 3 and 4 whose companion isn't the partner fighting, player 2 when the partner is
 * player 3's or 4's, anyone with no partner out) gets a Toad of their own at the front of the
 * crowd, tinted and tagged in their colour ("2P", "3P", "4P"):
 * - the stick moves it a little left or right;
 * - A cheer, B boo, X clap, Y jump, D-pad up wave, D-pad down laugh: the crowd's own animations,
 *   a speech bubble (effFukidashi) and a crowd sound; no effect on play, at most one each 40 frames;
 * - hold L and aim with the stick (Mario, the partner...), let go: a Mushroom (5 HP) for the one
 *   aimed at, or Honey Syrup (5 FP) when their HP is full; hold R likewise: a rock at an enemy
 *   (1 damage, never the last HP). One throw a turn, three a battle, only while a command is being
 *   chosen (never during an action, so never in an action command's timing).
 *
 * The member is a free seat of the game's own audience (BattleAudience_Entry) marked
 * MEMBER_FLAG_REMOVED (not counted: the crowd's size and Star Power stay as they were) and
 * MEMBER_FLAG_EVT_CTRL (the crowd's own behaviour leaves it alone; its hops are still the game's).
 * Nothing here takes the game's random numbers (the font's and effects' draws are put back).
 *
 * Online (a shared battle, port/include/port/statesync.h, layer 3): the battle's player 1's game
 * (A) decides. The idle player's game sends a request (REQ_*) to A; A checks it (the same rules)
 * and sends the event to every game (EMOTE, THROW, then LAND with what the throw did when it lands
 * on A's screen), and every game, the requester's too, plays it from A's event. The HP or FP a
 * throw gives or takes is A's, applied by A at its landing: it is in A's next step's snapshot; the
 * others apply LAND's amount when it comes. Units go by the battle's unit ids, never pointers. */
#include "port/achievements.h"
#include "port/crowd.h"
#include "port/brothers.h"
#include "port/dungeon_master.h"
#include "port/platform.h"
#include "port/players.h"
#include "port/settings.h"
#include "port/statesync.h"
#include "port/versus.h"
#include "port/view.h"
#include "battle/battle.h"
#include "battle/battle_audience.h"
#include "battle/battle_audience_kind_data.h"
#include "battle/battle_unit.h"
#include "data/item_data.h"
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "driver/icondrv.h"
#include "eff/eff_damage_star.h"
#include "eff/eff_fukidashi.h"
#include "eff/eff_hit.h"
#include "eff/eff_recovery.h"
#include "manager/fontmgr.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "pmario_sound.h"
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <dolphin/pad.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern PADStatus Pad[PAD_MAX_CONTROLLERS]; /* DEMOPad.c: the frame's controllers (online and replays: as played) */
#include "port/tips.h"

#ifndef WIRE_SIZE
#define WIRE_SIZE(T, n) _Static_assert(sizeof(T) == (n), #T " is " #n " bytes on the wire")
#endif

#define PLACES 4
#define THROWS_PER_BATTLE 3
#define EMOTE_GAP 40       /* frames between a player's emotes */
#define HOLD_FRAMES 12     /* the item held up before it flies */
#define SEAT_RANGE 50.0f   /* how far the stick moves the seat each way */
#define SEAT_Z 112.0f      /* just before the crowd's first row (z 115) */
#define ACT_CHOOSING 0x6000002 /* battle_seq.c btlseqAct: a command being chosen */

enum { EMOTE_CHEER, EMOTE_BOO, EMOTE_CLAP, EMOTE_JUMP, EMOTE_WAVE, EMOTE_LAUGH, EMOTES };
enum { THROW_HP, THROW_FP, THROW_ROCK };
enum { AIM_NONE, AIM_GIFT, AIM_ROCK };

/* the audience kinds' animation slots (battle_audience_kind_data.c) */
enum {
    ANIM_WAIT_FRONT = 0,
    ANIM_WAIT_BACK = 1,
    ANIM_JUMP_FRONT = 2,
    ANIM_JUMP_BACK = 3,
    ANIM_RAISE_BACK = 4,
    ANIM_ITEMON_BACK = 5,
    ANIM_ITEMTHROW_BACK = 6,
    ANIM_CONFUSE_BACK = 13,
};

/* ---- the battle message (statesync_battle_pc.c B_CROWD on SS_R_BATTLE) ---- */
#define B_CROWD 40
enum {
    OP_REQ_EMOTE = 1, /* a: the emote */
    OP_REQ_THROW = 2, /* a: 0 a gift, 1 a rock; b: the target's unit id */
    OP_REQ_POS = 3,   /* a: the seat's x offset, tenths */
    OP_EMOTE = 17,    /* A: a: the emote */
    OP_THROW = 18,    /* A: a: the item, b: the target's unit id, d: THROW_HP/FP/ROCK */
    OP_LAND = 19,     /* A: a: THROW_*, b: the target's unit id, c: the HP or FP given or taken */
    OP_POS = 20,      /* A: a: the seat's x offset, tenths */
    OP_DENY = 21,     /* A: the request can't be done now */
};

typedef struct CrowdMsg {
    uint8_t kind;  /* B_CROWD */
    uint8_t place; /* the sender's battle place */
    uint8_t op;
    uint8_t seat; /* the idle player's battle place (1 to 3) */
    uint32_t id;  /* the battle's */
    uint32_t seq;
    int16_t a, b, c, d;
} CrowdMsg;
WIRE_SIZE(CrowdMsg, 20);

typedef struct Flight {
    int on;
    int item;   /* the item thrown */
    int target; /* the unit's id */
    int kind;   /* THROW_* */
    int hold;   /* frames held up before it flies */
    int frames; /* frames of flight left */
    int phase;  /* 0 held, 1 flying, 2 bouncing off, 3 lying (fading) */
    int fade;
    Vec pos, vel, acc;
    float rot;
} Flight;

typedef struct Seat {
    int member;  /* the audience seat, -1 none */
    float x;     /* the stick's offset (the seat's own player's, or the latest from A) */
    float drawX; /* eased toward x */
    int emote, emoteT;
    int sfx, sfxLeft;
    int deny;
    int lastEmote;
    int aim, aimUnit, stickHeld;
    uint16_t prev;
    int used, lastTurn;
    float sentX;
    int posAt;
    Flight f;
} Seat;

static struct {
    Seat s[PLACES];
    int frame;
    uint32_t seq;
    int ready;
} C;
static int sNoticed;

/* a place's player's colour (the Player colours setting's, port/include/port/players.h) */
static GXColor placeColour(int p) {
    const unsigned v = portPlayerColour(p);
    return (GXColor){(u8)(v >> 24), (u8)(v >> 16), (u8)(v >> 8), 0xFF};
}
static const char* const kTag[PLACES] = {"1P", "2P", "3P", "4P"};
static const float kHomeX[PLACES] = {0.0f, -120.0f, -40.0f, 40.0f};


static void say(int p, const char* what, int a, int b) {
    printf("[crowd] %s %s %d %d\n", kTag[p], what, a, b); /* (the tests read these) */
    fflush(stdout);
}

/* ---- the setting and who is idle ---- */

static int settingOn(void) {
    static int* sOn;
    if (sOn == NULL) {
        sOn = portSettingsDeclare("audience_players", 1, 0, 1);
    }
    return *sOn;
}

int portCrowdSeatsOn(void) {
    return settingOn() && !portVersusOn() && (portStateSyncSharedBattle() || portCoopPlayers() > 1);
}

static int shared(void) {
    return portStateSyncSharedBattle();
}

static int deciding(void) {
    return !shared() || portStateSyncBattleIsA();
}

/* place p (1 to 3) is in the battle with no character in it */
static int idle(BattleWork* wp, int p) {
    BattleUnit* partner;
    /* (the character place p plays: itself, but a guest in seat 1 plays Mario, Go.roles, and the host the
       partner; the dungeon master's is by that character's channel, dungeon_master_aurora.c) */
    const int c = portStateSyncBattleCharacterOf(p);
    if (!portCoopPlayerHere(p) || c == 0 || portDmPlaysEnemies(c)) {
        return 0; /* (the place that plays Mario is never idle: its presses are Mario's menu's) */
    }
    if (c == 1 && portBrothersOn()) {
        return 0; /* (player 2 plays Luigi) */
    }
    partner = BattleGetPartyPtr(wp);
    /* (the owner is a character: its place plays it, a guest in seat 1 playing Mario) */
    return partner == NULL || portStateSyncBattlePlaceOf(portSecondPlayerPartnerOwner(partner)) != p;
}

/* this game reads place p's controller (its own player's, in a shared battle) */
static int mine(int p) {
    return shared() ? portStateSyncBattleSelf() == p : 1;
}

static int usable(void) {
    BattleWork* wp = _battleWorkPointer;
    BattleAudience* aud = BattleAudienceBaseGetPtr();
    return wp != NULL && !(aud->flags & (AUDIENCE_FLAG_FROZEN | AUDIENCE_FLAG_HIDDEN)) &&
           !(wp->flags & BATTLE_FLAG_WIDE_STAGE);
}

/* a throw may start now: a command is being chosen (no action runs, so no action command) */
static int throwTime(void) {
    BattleWork* wp = _battleWorkPointer;
    return usable() && BattleGetSeq(wp, SEQ_UNKNOWN) == 2 && BattleGetSeq(wp, SEQ_ACT) == ACT_CHOOSING;
}

/* ---- targets ---- */

static BattleUnit* unitAt(int id) {
    BattleWork* wp = _battleWorkPointer;
    if (wp == NULL || id < 0 || id >= BATTLE_UNIT_MAX) {
        return NULL;
    }
    return wp->units[id];
}

/* a unit a throw may go to: Mario's side (gifts) or the enemies (rocks), standing, targetable */
static int targetable(int id, int rock) {
    BattleUnit* unit = unitAt(id);
    BattleUnitPart* part;
    if (unit == NULL || unit->currentHp <= 0 || unit->alliance != (rock ? 1 : 0)) {
        return 0;
    }
    if (rock && unit->currentKind >= UNIT_UNUSED_TREE && unit->currentKind < UNIT_UNUSED_TESTNPC) {
        return 0;
    }
    part = BattleGetUnitPartsPtr(id, BtlUnit_GetBodyPartsId(unit));
    return part != NULL && !(part->attributes & PART_ATTR_NOT_TARGETABLE);
}

/* the next target from id in that direction (the units' order; -1 none) */
static int nextTarget(int id, int dir, int rock) {
    int i, k = id < 0 ? (dir > 0 ? -1 : 0) : id;
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        k = (k + dir + BATTLE_UNIT_MAX) % BATTLE_UNIT_MAX;
        if (targetable(k, rock)) {
            return k;
        }
    }
    return -1;
}

static void unitTop(BattleUnit* unit, Vec* out) {
    BtlUnit_GetPos(unit, &out->x, &out->y, &out->z);
    out->y += unit->sizeMultiplier * (f32)BtlUnit_GetHeight(unit) + 5.0f;
}

static void unitHit(BattleUnit* unit, Vec* out) {
    BattleUnitPart* part = BtlUnit_GetPartsPtr(unit, BtlUnit_GetBodyPartsId(unit));
    BtlUnit_GetHitPos(unit, part, &out->x, &out->y, &out->z);
}

/* ---- messages ---- */

static void crowdSend(int toA, int op, int seat, int a, int b, int c, int d) {
    CrowdMsg m;
    const int self = portStateSyncBattleSelf();
    memset(&m, 0, sizeof(m));
    m.kind = B_CROWD;
    m.place = (uint8_t)(self >= 0 ? self : 0xFF);
    m.op = (uint8_t)op;
    m.seat = (uint8_t)seat;
    m.id = portStateSyncBattleId();
    m.seq = ++C.seq;
    m.a = (int16_t)a;
    m.b = (int16_t)b;
    m.c = (int16_t)c;
    m.d = (int16_t)d;
    portStateSyncCrowdSend(toA, &m, sizeof(m));
}

/* ---- the member ---- */

static BattleAudienceMember* member(int p) {
    return C.s[p].member >= 0 ? BattleAudienceGetPtr(C.s[p].member) : NULL;
}

static void place(int p) {
    Seat* s = &C.s[p];
    BattleAudienceMember* m;
    int i, idx = -1;
    /* a free seat from the back of the list (the crowd fills from its own picks; ours isn't counted) */
    for (i = AUDIENCE_MEMBER_MAX - 1; i >= 0; i--) {
        if (!(BattleAudienceGetPtr(i)->flags & (MEMBER_FLAG_ACTIVE | MEMBER_FLAG_WIDE_TAIL))) {
            idx = i;
            break;
        }
    }
    if (idx < 0 || !BattleAudience_Entry(idx, AUDIENCE_TOAD, 0)) {
        return; /* (a full house: no seat this battle) */
    }
    m = BattleAudienceGetPtr(idx);
    m->flags |= MEMBER_FLAG_REMOVED | MEMBER_FLAG_EVT_CTRL;
    m->flags &= ~MEMBER_FLAG_CENTER_SEAT;
    m->position.x = m->homePosition.x = kHomeX[p] + s->x;
    m->position.z = m->homePosition.z = SEAT_Z;
    m->angle = 270.0f;
    BattleAudience_SetAnim(idx, ANIM_WAIT_BACK, TRUE);
    s->member = idx;
    s->drawX = s->x;
    say(p, "seated", idx, 0);
    if (mine(p) && !sNoticed) {
        sNoticed = 1;
        portTipOffer(PORT_TIP_AUDIENCE); /* (once per install: the buttons, tips_aurora.c) */
    }
}

static void unplace(int p) {
    Seat* s = &C.s[p];
    if (s->member >= 0) {
        BattleAudienceMember* m = member(p);
        if ((m->flags & MEMBER_FLAG_EVT_CTRL) && (m->flags & MEMBER_FLAG_REMOVED)) {
            BattleAudience_Delete(s->member);
        }
        s->member = -1;
    }
    s->aim = AIM_NONE;
    s->emoteT = 0;
}

/* a crowd sound at the seat, stopped after frames (some of them are loops) */
static void sound(int p, const char* name, int frames) {
    Seat* s = &C.s[p];
    BattleAudienceMember* m = member(p);
    Vec pos;
    if (m == NULL) {
        return;
    }
    if (s->sfxLeft > 0) {
        psndSFXOff(s->sfx);
    }
    pos = m->position;
    s->sfx = psndSFXOn_3D(name, &pos);
    s->sfxLeft = frames;
}

static void bubble(int p, int type) {
    BattleAudienceMember* m = member(p);
    if (m != NULL) {
        effFukidashiEntry(type, 0, NULL, m->position.x + 8.0f, m->position.y + 48.0f, m->position.z, 10.0f, 0.0f, 50);
    }
}

static void startEmote(int p, int e) {
    static const int kBubble[EMOTES] = {0, 5, 4, 2, 6, 6}; /* "!", scribble, dots, swirl, bounce, bounce */
    static const char* const kSound[EMOTES] = {
        "SFX_AUDIENCE_WHISTLE1", "SFX_AUDIENCE_BOO_ENEMY1", "SFX_AUDIENCE_CLAPPING1",
        "SFX_AUDIENCE_CALL_ALLY1", "SFX_AUDIENCE_WHISTLE2", "SFX_AUDIENCE_CHEER_ALLY1"};
    Seat* s = &C.s[p];
    const u32 seed = portRandState();
    if (e < 0 || e >= EMOTES || s->member < 0) {
        return;
    }
    s->emote = e;
    s->emoteT = 60;
    say(p, "emote", e, 0);
    bubble(p, kBubble[e]);
    sound(p, kSound[e], 50);
    portRandSetState(seed);
}

/* every game: an emote or throw refused (a shake) */
static void startDeny(int p) {
    C.s[p].deny = 14;
    say(p, "denied", 0, 0);
}

/* ---- throws ---- */

static void startThrow(int p, int item, int target, int kind) {
    Seat* s = &C.s[p];
    Flight* f = &s->f;
    BattleWork* wp = _battleWorkPointer;
    memset(f, 0, sizeof(*f));
    f->on = 1;
    f->item = item;
    f->target = target;
    f->kind = kind;
    f->hold = HOLD_FRAMES;
    say(p, "throws", item, target);
    s->used++;
    s->lastTurn = wp != NULL ? wp->turnCount : 0;
    s->emoteT = 0;
}

/* what a throw does when it lands (A's game, or one game alone); amount: what A's did (-1 decide here) */
static int applyThrow(int kind, int target, int amount) {
    BattleUnit* unit = unitAt(target);
    const u32 seed = portRandState();
    Vec at;
    int n = 0;
    if (unit == NULL || unit->currentHp <= 0) {
        return 0;
    }
    switch (kind) {
        case THROW_HP:
            n = amount >= 0 ? amount : itemDataTable[ITEM_MUSHROOM].hp_restored;
            BtlUnit_RecoverHp(unit, n);
            unitTop(unit, &at);
            effRecoveryEntry(0, n, at.x, at.y, at.z);
            break;
        case THROW_FP: {
            BattleUnit* mario = BattleGetMarioPtr(_battleWorkPointer);
            n = amount >= 0 ? amount : itemDataTable[ITEM_HONEY_SYRUP].fp_restored;
            if (mario != NULL) {
                BtlUnit_RecoverFp(mario, n); /* (the party's FP: Mario's) */
            }
            unitTop(unit, &at);
            effRecoveryEntry(1, n, at.x, at.y, at.z);
            break;
        }
        case THROW_ROCK:
            /* one damage, never the last HP: no defeat happens outside an action */
            n = amount >= 0 ? amount : 1;
            if (n > unit->currentHp - 1) {
                n = unit->currentHp - 1;
            }
            if (n < 0) {
                n = 0;
            }
            unit->currentHp = (s16)(unit->currentHp - n);
            unitHit(unit, &at);
            effHitEntry(0, 3, at.x, at.y, at.z, 1.0f);
            if (n > 0) {
                effDamageStarEntry(0, n, at.x, at.y + 10.0f, at.z, 10.0f, -30.0f);
            }
            psndSFXOn_3D("SFX_BTL_DAMAGED_PLIABLE1", &at);
            break;
        default:
            break;
    }
    portRandSetState(seed);
    return n;
}

/* the deciding game (A, or the only one): may player p throw at target now? Then it does. */
static void decideThrow(int p, int rock, int target) {
    Seat* s = &C.s[p];
    BattleWork* wp = _battleWorkPointer;
    BattleUnit* unit = unitAt(target);
    BattleUnit* mario;
    int item, kind;
    if (wp == NULL || s->member < 0 || s->f.on || s->used >= THROWS_PER_BATTLE || s->lastTurn == wp->turnCount ||
        !throwTime() || !targetable(target, rock) || unit == NULL) {
        goto no;
    }
    if (rock) {
        item = ITEM_AUDIENCE_ROCK;
        kind = THROW_ROCK;
    } else {
        mario = BattleGetMarioPtr(wp);
        if (unit->currentHp < unit->maxHp) {
            item = ITEM_MUSHROOM;
            kind = THROW_HP;
        } else if (mario != NULL && mario->currentFp < mario->maxFp) {
            item = ITEM_HONEY_SYRUP;
            kind = THROW_FP;
        } else {
            goto no; /* (nobody needs it) */
        }
    }
    startThrow(p, item, target, kind);
    if (shared()) {
        crowdSend(0, OP_THROW, p, item, target, 0, kind);
    }
    return;
no:
    startDeny(p);
    if (shared()) {
        crowdSend(0, OP_DENY, p, 0, 0, 0, 0);
    }
}

static void decideEmote(int p, int e) {
    Seat* s = &C.s[p];
    if (s->member < 0 || C.frame - s->lastEmote < EMOTE_GAP || s->f.on || e < 0 || e >= EMOTES) {
        return;
    }
    s->lastEmote = C.frame;
    startEmote(p, e);
    if (shared()) {
        crowdSend(0, OP_EMOTE, p, e, 0, 0, 0);
    }
}

static void requestEmote(int p, int e) {
    if (deciding()) {
        decideEmote(p, e);
    } else {
        crowdSend(1, OP_REQ_EMOTE, p, e, 0, 0, 0);
    }
}

static void requestThrow(int p, int rock, int target) {
    if (deciding()) {
        decideThrow(p, rock, target);
    } else {
        crowdSend(1, OP_REQ_THROW, p, rock, target, 0, 0);
    }
}

static void flightFrame(int p) {
    Seat* s = &C.s[p];
    Flight* f = &s->f;
    BattleAudienceMember* m = member(p);
    BattleUnit* unit;
    if (!f->on) {
        return;
    }
    switch (f->phase) {
        case 0: /* held up */
            if (m != NULL) {
                BattleAudienceKind* kd = &audience_kind[m->type];
                f->pos.x = m->position.x + kd->itemOffsetX;
                f->pos.y = m->position.y + m->hopY + kd->itemOffsetY + 6.0f;
                f->pos.z = m->position.z + 1.0f;
            }
            if (--f->hold > 0) {
                break;
            }
            unit = unitAt(f->target);
            if (unit == NULL) {
                f->on = 0;
                break;
            }
            {
                Vec to;
                float n, dx;
                const u32 seed = portRandState();
                unitHit(unit, &to);
                dx = to.x - f->pos.x;
                n = 24.0f + (float)fabs(dx) / 20.0f;
                f->vel.x = dx / n;
                f->vel.y = 7.0f;
                f->acc.y = 2.0f * ((to.y - f->pos.y) - f->vel.y * n) / (n * n);
                f->vel.z = (to.z - f->pos.z) / n;
                f->frames = (int)n;
                f->phase = 1;
                sound(p, "SFX_AUDIENCE_THROW_OBJECT1", 40);
                portRandSetState(seed);
            }
            break;
        case 1: /* flying */
            f->pos.x += f->vel.x;
            f->pos.y += f->vel.y;
            f->pos.z += f->vel.z;
            f->vel.y += f->acc.y;
            if (f->kind == THROW_ROCK) {
                f->rot += 18.0f;
            }
            if (--f->frames > 0) {
                break;
            }
            /* landed: the deciding game does what it does (the others take it from LAND) */
            if (deciding()) {
                const int n = applyThrow(f->kind, f->target, -1);
                say(p, f->kind == THROW_ROCK ? "landed-damage" : f->kind == THROW_FP ? "landed-fp" : "landed-hp", n,
                    f->target);
                portAchievementCounterAdd("crowd_throws", 1); /* (each game present counts the party's) */
                if (shared()) {
                    crowdSend(0, OP_LAND, p, f->kind, f->target, n, 0);
                }
            }
            if (f->kind == THROW_ROCK) {
                f->vel.x *= -0.2f;
                f->vel.y = 3.0f;
                f->vel.z = 0.0f;
                f->acc.y = -0.4f;
                f->phase = 2;
            } else {
                f->on = 0; /* (taken: the recovery's numbers show) */
            }
            break;
        case 2: /* the rock bounces off */
            f->pos.x += f->vel.x;
            f->pos.y += f->vel.y;
            f->vel.y += f->acc.y;
            f->rot += 9.0f;
            if (f->pos.y <= 0.0f) {
                f->pos.y = 0.0f;
                f->phase = 3;
                f->fade = 40;
            }
            break;
        default:
            if (--f->fade <= 0) {
                f->on = 0;
            }
            break;
    }
}

/* ---- each frame ---- */

static void input(int p) {
    Seat* s = &C.s[p];
    /* (Pad[n] is character n's controller in a shared battle: place p's is the character it plays) */
    const int ch = portStateSyncBattleCharacterOf(p);
    const PADStatus* pad = &Pad[ch >= 0 && ch < PAD_MAX_CONTROLLERS ? ch : p];
    const uint16_t b = pad->err == PAD_ERR_NONE ? pad->button : 0;
    const uint16_t pressed = (uint16_t)(b & ~s->prev);
    const int sx = pad->err == PAD_ERR_NONE ? pad->stickX : 0;
    s->prev = b;
    if (!usable()) {
        s->aim = AIM_NONE;
        return;
    }
    if (s->aim == AIM_NONE) {
        if (pressed & PAD_TRIGGER_L) {
            s->aim = AIM_GIFT;
        } else if (pressed & PAD_TRIGGER_R) {
            s->aim = AIM_ROCK;
        }
        if (s->aim != AIM_NONE) {
            s->aimUnit = nextTarget(-1, 1, s->aim == AIM_ROCK);
            s->stickHeld = 1;
            if (s->aimUnit < 0) {
                s->aim = AIM_NONE;
            }
            return;
        }
        if (pressed & PAD_BUTTON_A) {
            requestEmote(p, EMOTE_CHEER);
        } else if (pressed & PAD_BUTTON_B) {
            requestEmote(p, EMOTE_BOO);
        } else if (pressed & PAD_BUTTON_X) {
            requestEmote(p, EMOTE_CLAP);
        } else if (pressed & PAD_BUTTON_Y) {
            requestEmote(p, EMOTE_JUMP);
        } else if (pressed & PAD_BUTTON_UP) {
            requestEmote(p, EMOTE_WAVE);
        } else if (pressed & PAD_BUTTON_DOWN) {
            requestEmote(p, EMOTE_LAUGH);
        }
        if (sx > 20 || sx < -20) {
            s->x += (float)sx / 80.0f * 1.2f;
            if (s->x > SEAT_RANGE) {
                s->x = SEAT_RANGE;
            }
            if (s->x < -SEAT_RANGE) {
                s->x = -SEAT_RANGE;
            }
        }
        return;
    }
    /* aiming: the stick picks, letting go throws, Z puts it away */
    {
        const int rock = s->aim == AIM_ROCK;
        const uint16_t trigger = rock ? PAD_TRIGGER_R : PAD_TRIGGER_L;
        if (!targetable(s->aimUnit, rock)) {
            s->aimUnit = nextTarget(s->aimUnit, 1, rock);
            if (s->aimUnit < 0) {
                s->aim = AIM_NONE;
                return;
            }
        }
        if (sx > -30 && sx < 30) {
            s->stickHeld = 0;
        } else if (!s->stickHeld) {
            const int to = nextTarget(s->aimUnit, sx > 0 ? 1 : -1, rock);
            s->stickHeld = 1;
            if (to >= 0) {
                s->aimUnit = to;
            }
        }
        if (pressed & PAD_TRIGGER_Z) {
            s->aim = AIM_NONE;
        } else if (!(b & trigger)) {
            s->aim = AIM_NONE;
            requestThrow(p, rock, s->aimUnit);
        }
    }
}

static void animate(int p) {
    Seat* s = &C.s[p];
    BattleAudienceMember* m = member(p);
    const int idx = s->member;
    int anim = ANIM_WAIT_BACK;
    if (m == NULL) {
        return;
    }
    s->drawX += (s->x - s->drawX) * 0.3f;
    m->position.x = m->homePosition.x = kHomeX[p] + s->drawX;
    m->dispOffset = (Vec){0.0f, 0.0f, 0.0f};
    m->rotation.x = m->rotation.z = 0.0f;
    m->angle = 270.0f;
    /* the player's colour, a little (the members' colour is added to their picture) */
    m->colorR = (u8)(placeColour(p).r / 8);
    m->colorG = (u8)(placeColour(p).g / 8);
    m->colorB = (u8)(placeColour(p).b / 8);
    m->colorA = 0xFF;
    if (s->sfxLeft > 0 && --s->sfxLeft == 0) {
        psndSFXOff(s->sfx);
    }
    if (s->deny > 0) {
        s->deny--;
        anim = ANIM_CONFUSE_BACK;
        m->dispOffset.x = (s->deny & 2) ? 3.0f : -3.0f;
    } else if (s->f.on && s->f.phase == 0) {
        anim = ANIM_ITEMON_BACK;
    } else if (s->f.on && s->f.phase == 1) {
        anim = ANIM_ITEMTHROW_BACK;
    } else if (s->aim != AIM_NONE) {
        anim = ANIM_ITEMON_BACK;
    } else if (s->emoteT > 0) {
        const int t = 60 - s->emoteT;
        s->emoteT--;
        switch (s->emote) {
            case EMOTE_CHEER: /* arms up, bobbing (the crowd's joy, status 4) */
                anim = ANIM_RAISE_BACK;
                m->dispOffset.y = 10.0f * (float)(t % 15 < 8 ? t % 15 : 15 - t % 15) / 8.0f;
                break;
            case EMOTE_BOO: /* arms up, shaking (the crowd's booing, status 9) */
                anim = ANIM_RAISE_BACK;
                m->dispOffset.x = (t & 4) ? 2.0f : -2.0f;
                break;
            case EMOTE_CLAP:
                anim = (t / 6) & 1 ? ANIM_RAISE_BACK : ANIM_WAIT_BACK;
                break;
            case EMOTE_JUMP: /* two hops, spinning (the crowd's big cheer, status 6) */
                anim = ANIM_JUMP_BACK;
                if (t == 0 || t == 30) {
                    m->hopSpeed = 4.4f;
                    m->hopAccel = -0.3f;
                }
                if (m->hopAccel != 0.0f) {
                    m->rotation.y += 24.0f;
                }
                break;
            case EMOTE_WAVE: /* facing the camera, swaying */
                anim = ANIM_WAIT_FRONT;
                m->angle = (t / 10) & 1 ? 90.0f : 270.0f;
                break;
            case EMOTE_LAUGH: /* facing the camera, little hops */
                anim = ANIM_JUMP_FRONT;
                if (t % 15 == 0 && t < 50) {
                    m->hopSpeed = 2.0f;
                    m->hopAccel = -0.35f;
                }
                break;
            default:
                break;
        }
    }
    if (m->hopAccel == 0.0f) {
        m->rotation.y = 0.0f; /* (a spin only while a jump's hop lasts) */
    }
    BattleAudience_SetAnim(idx, anim, FALSE);
}

void portCrowdSeatsReset(void) {
    int p;
    memset(&C, 0, sizeof(C));
    for (p = 0; p < PLACES; p++) {
        C.s[p].member = -1;
        C.s[p].lastTurn = -1;
        C.s[p].lastEmote = -EMOTE_GAP;
    }
    C.ready = 1;
}

void portCrowdSeatsFrame(void) {
    BattleWork* wp = _battleWorkPointer;
    const int on = wp != NULL && portCrowdSeatsOn() && !(wp->flags & BATTLE_FLAG_WIDE_STAGE);
    int p;
    if (!C.ready) {
        portCrowdSeatsReset();
    }
    C.frame++;
    for (p = 1; p < PLACES; p++) {
        Seat* s = &C.s[p];
        BattleAudienceMember* m;
        if (!on || !idle(wp, p)) {
            unplace(p);
            s->f.on = 0;
            continue;
        }
        m = member(p);
        if (m != NULL && (!(m->flags & MEMBER_FLAG_ACTIVE) || !(m->flags & MEMBER_FLAG_EVT_CTRL))) {
            s->member = -1; /* (the battle's own scripts took the seat back) */
        }
        if (s->member < 0) {
            place(p);
        }
        if (mine(p)) {
            input(p);
            if (shared() && !deciding() && C.frame - s->posAt >= 6 && fabsf(s->x - s->sentX) > 0.5f) {
                s->sentX = s->x;
                s->posAt = C.frame;
                crowdSend(1, OP_REQ_POS, p, (int)(s->x * 10.0f), 0, 0, 0);
            } else if (shared() && deciding() && C.frame - s->posAt >= 6 && fabsf(s->x - s->sentX) > 0.5f) {
                s->sentX = s->x;
                s->posAt = C.frame;
                crowdSend(0, OP_POS, p, (int)(s->x * 10.0f), 0, 0, 0);
            }
        }
        animate(p);
        flightFrame(p);
    }
}

/* ---- from the other games (statesync_battle_pc.c, a B_CROWD message: from is its battle place) ---- */

void portCrowdSeatsNet(int from, const void* data, size_t n) {
    CrowdMsg m;
    int p;
    if (n < sizeof(m) || !shared() || _battleWorkPointer == NULL) {
        return;
    }
    memcpy(&m, data, sizeof(m));
    p = m.seat;
    if (m.kind != B_CROWD || m.id != portStateSyncBattleId() || p < 1 || p >= PLACES) {
        return;
    }
    if (!C.ready) {
        portCrowdSeatsReset();
    }
    if (portStateSyncBattleIsA()) {
        if (from != p || !portCrowdSeatsOn() || !idle(_battleWorkPointer, p)) {
            return; /* (only a seat's own player asks for it) */
        }
        switch (m.op) {
            case OP_REQ_EMOTE:
                decideEmote(p, m.a);
                break;
            case OP_REQ_THROW:
                decideThrow(p, m.a != 0, m.b);
                break;
            case OP_REQ_POS: {
                float x = (float)m.a / 10.0f;
                C.s[p].x = x > SEAT_RANGE ? SEAT_RANGE : x < -SEAT_RANGE ? -SEAT_RANGE : x;
                crowdSend(0, OP_POS, p, (int)(C.s[p].x * 10.0f), 0, 0, 0);
                break;
            }
            default:
                break;
        }
        return;
    }
    if (from != 0) {
        return; /* (the events are player 1's game's) */
    }
    switch (m.op) {
        case OP_EMOTE:
            startEmote(p, m.a);
            break;
        case OP_THROW:
            if (m.d >= THROW_HP && m.d <= THROW_ROCK) {
                startThrow(p, m.a, m.b, m.d);
            }
            break;
        case OP_LAND:
            applyThrow(m.a, m.b, m.c);
            say(p, "landed-from-player-1", m.c, m.b);
            portAchievementCounterAdd("crowd_throws", 1);
            break;
        case OP_POS:
            if (portStateSyncBattleSelf() != p) {
                float x = (float)m.a / 10.0f;
                C.s[p].x = x > SEAT_RANGE ? SEAT_RANGE : x < -SEAT_RANGE ? -SEAT_RANGE : x;
            }
            break;
        case OP_DENY:
            startDeny(p);
            break;
        default:
            break;
    }
}

/* ---- drawing ---- */

static void tag(const Vec* top, const char* text, GXColor color, float scale) {
    Vec at = *top, screen;
    float w;
    getScreenPoint(&at, &screen);
    if (portViewMirrored()) {
        screen.x = -screen.x;
    }
    FontDrawStart_alpha(color.a);
    FontDrawEdge();
    FontDrawColor(color);
    FontDrawScale(scale);
    w = FontGetMessageWidth(text) * scale;
    FontDrawString(screen.x - w * 0.5f, screen.y + 18.0f, text);
}

/* each seat's tag (dim when it has no throw now), and the target being aimed at */
static void tagsDisp(CameraId camId, void* param) {
    BattleWork* wp = _battleWorkPointer;
    const u32 seed = portRandState(); /* (the font draws with the game's random numbers) */
    int p;
    (void)camId;
    (void)param;
    for (p = 1; p < PLACES; p++) {
        Seat* s = &C.s[p];
        BattleAudienceMember* m = member(p);
        GXColor color = placeColour(p);
        Vec top;
        if (m == NULL) {
            continue;
        }
        top = m->position;
        top.y += 52.0f + m->hopY;
        if (wp != NULL && (s->used >= THROWS_PER_BATTLE || s->lastTurn == wp->turnCount)) {
            color.a = 0x90;
        }
        tag(&top, kTag[p], color, 0.7f);
        if (s->aim != AIM_NONE && mine(p)) {
            BattleUnit* unit = unitAt(s->aimUnit);
            if (unit != NULL) {
                unitTop(unit, &top);
                top.y += 14.0f + 3.0f * (float)sin(C.frame * 0.25f);
                color = placeColour(p);
                tag(&top, kTag[p], color, 0.8f);
            }
        }
    }
    portRandSetState(seed);
}

static void gxInit(void) {
    GXSetCullMode(GX_CULL_NONE);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_VTX, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_CLAMP, GX_AF_NONE);
    GXSetNumTexGens(1);
    GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
    GXSetNumTevStages(1);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_RASC, GX_CC_TEXC, GX_CC_ZERO);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_RASA, GX_CA_TEXA, GX_CA_ZERO);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
}

static void icon(int item, const Vec* pos, float rot) {
    Mtx mtx, r;
    PSMTXTrans(mtx, 0.0f, -20.0f, 0.0f);
    PSMTXRotRad(r, 'z', 0.017453292f * rot);
    PSMTXConcat(r, mtx, mtx);
    PSMTXTransApply(mtx, mtx, 0.0f, 20.0f, 0.0f);
    PSMTXTransApply(mtx, mtx, pos->x, pos->y, pos->z);
    iconDispGx2(mtx, 0, itemDataTable[item].icon_id);
}

/* the items held up and in flight (as the crowd's own, BattleAudienceDispItem); one being aimed */
static void itemsDisp(CameraId camId, void* param) {
    int p;
    (void)camId;
    (void)param;
    gxInit();
    for (p = 1; p < PLACES; p++) {
        Seat* s = &C.s[p];
        BattleAudienceMember* m = member(p);
        if (s->f.on && (s->f.phase != 3 || (s->f.fade & 3) <= 1 || s->f.fade > 20)) {
            icon(s->f.item, &s->f.pos, s->f.rot);
        } else if (!s->f.on && s->aim != AIM_NONE && m != NULL && mine(p)) {
            BattleAudienceKind* kd = &audience_kind[m->type];
            Vec at = m->position;
            at.x += kd->itemOffsetX;
            at.y += m->hopY + kd->itemOffsetY + 6.0f;
            at.z += 1.0f;
            icon(s->aim == AIM_ROCK ? ITEM_AUDIENCE_ROCK : ITEM_MUSHROOM, &at, 0.0f);
        }
    }
}

void portCrowdSeatsDisp(void) {
    int p, any = 0;
    if (!C.ready || (BattleAudienceBaseGetPtr()->flags & AUDIENCE_FLAG_HIDDEN)) {
        return;
    }
    for (p = 1; p < PLACES; p++) {
        any |= C.s[p].member >= 0;
    }
    if (any) {
        dispEntry(CAMERA_3D, 0.0f, 1, itemsDisp, NULL);
        dispEntry(CAMERA_2D, 498.0f, 0, tagsDisp, NULL);
    }
}
