/* The remake's field changes (port/include/port/field_remake.h): every one a
 * setting, off by default, so the field plays as the original until one is
 * turned on. */
#include <stdio.h>
#include "port/field_remake.h"
#include "port/settings.h"
#include "driver/npcdrv.h"
#include "mario/mario.h"
#include "mario/mario_pouch.h"
#include "eff/eff_small_star.h"
#include "pmario_sound.h"
#include "driver/casedrv.h"
#include "mario/mario_sbr.h"
#include "manager/evtmgr.h"
#include "evt/evt_cmd.h"
#include <math.h>
#include <string.h>
#include "system.h"
#include "party.h"
#include "mario/mario_party.h"
#include "mario/mariost.h"
#include "driver/camdrv.h"
#include "driver/itemdrv.h"
#include "driver/mapdrv.h"
#include "driver/effdrv.h"
#include "eff/eff_nokotarou.h"
#include "eff/eff_ripple.h"
#include "eff/n64/eff_ripple_n64.h"
#include "party/party_yoshi.h"
#include "mario/mario_motion.h"
#include "mario/mario_hit.h"
#include "battle/battle.h"
#include "battle/battle_unit.h"
#include "battle/battle_disp.h"
#include <dolphin/pad.h>

/* the field settings: key, default, range */
static const struct {
    const char* key;
    int def, min, max;
} kSettings[] = {
    {"remake_notice_range", 0, 0, 1},
    {"remake_projectile_hurt", 0, 0, 1},
    {"remake_pipe_a", 0, 0, 1},
    {"remake_field_hammer", 0, 0, 1},
    {"remake_koops_aim", 0, 0, 1},
    {"remake_plane_steady", 0, 0, 1},
    {"remake_yoshi", 0, 0, 1},
    {"remake_teeter", 0, 0, 1},
    {"remake_guard_stance", 0, 0, 1},
};

void portFieldRemakeDeclare(void) {
    static int declared;
    size_t i;
    if (declared) {
        return;
    }
    declared = 1;
    for (i = 0; i < sizeof(kSettings) / sizeof(kSettings[0]); i++) {
        portSettingsDeclare(kSettings[i].key, kSettings[i].def, kSettings[i].min, kSettings[i].max);
    }
}

static int setting(const char* key) {
    int* v;
    portFieldRemakeDeclare();
    v = portSettingsFind(key);
    return v != NULL ? *v : 0;
}

/* --- Shorter enemy notice range --- */

/* the search range a third shorter: the distance counts as this much more */
#define NOTICE_RANGE 0.65f

float portNoticeDistScale(void) {
    return setting("remake_notice_range") ? 1.0f / NOTICE_RANGE : 1.0f;
}

/* --- Field projectiles hurt --- */

int portFieldProjectileHit(struct NpcEntry* npc, unsigned kind) {
    MarioWork* mario = marioGetPtr();
    if (!setting("remake_projectile_hurt") || npc == NULL || mario == NULL) {
        return 0;
    }
    /* only touches (not Mario's own jump or hammer on it), only a thing an
     * enemy sent (a slave with an attack), only Mario himself (not Peach's or
     * Bowser's chapters), and never the last HP: then the battle starts */
    if ((kind != FBAT_HIT_CONTACT && kind != FBAT_HIT_ENEMY_FIRST) || npc->master == NULL || npc->attackMode == 0 ||
        mario->characterId != 0 || pouchGetHP() <= 1) {
        return 0;
    }
    pouchAddHP(-1);
    marioSetMutekiTime(1500); /* blinks, and nothing touches him meanwhile */
    psndSFXOn_3D("SFX_BTL_FLD_MARIO_DAMAGED1", &mario->position);
    effSmallStarEntry(3, 3, mario->position.x, mario->position.y + mario->wPlayerCollisionBox.y, mario->position.z,
                      0.0f, -1.0f, 0.0f);
    printf("[field] projectile %s: 1 HP\n", npc->description);
    fflush(stdout);
    return 1;
}

/* --- Pipes with A --- */

int portPipeByA(void) {
    return setting("remake_pipe_a");
}

int portPipeUnder(struct HitEntry* standOn) {
    CaseEntry* ce;
    if (standOn == NULL) {
        return 0;
    }
    ce = caseCheckHitObj(standOn);
    return ce != NULL && ce->conditionId == CASE_COND_STAND_ON_STICK_DOWN;
}

int portEvtPipeFaceFront(struct EventEntry* event, int isFirstCall) {
    MarioWork* mario = marioGetPtr();
    float diff;
    (void)event;
    (void)isFirstCall;
    if (!setting("remake_pipe_a") || mario == NULL) {
        return EVT_RETURN_DONE;
    }
    /* walking away from the camera shows his back (mario.c marioRearAnime:
     * within 70 degrees of the camera's yaw): mirror that across the screen's
     * left-right line, so he keeps his side and faces the camera */
    diff = revise360(mario->moveDirection - mario->targetYaw);
    if (diff >= 290.0f || diff <= 70.0f) {
        mario->moveDirection = revise360(mario->targetYaw + 180.0f - diff);
        mario->directionView = mario->moveDirection;
    }
    return EVT_RETURN_DONE;
}

/* --- Field hammer --- */

int portFieldHammer(void) {
    return setting("remake_field_hammer");
}

int portFieldHammerBackFrame(int frame) {
    /* the head is over Mario (npcdrv.c npcHammerPos: frames 0-3 straight up)
     * while it comes round from behind */
    return frame <= 4 && portFieldHammer();
}

/* the last swing's impact: where, and on which frame */
static struct {
    float x, y, z;
    unsigned frame;
} sImpact;
static unsigned sFrame = 1;

int portFieldHammerNear(float x, float y, float z, float r) {
    float dx = sImpact.x - x, dz = sImpact.z - z;
    if (sImpact.frame == 0 || sFrame - sImpact.frame > 1 || fabsf(sImpact.y - y) > 40.0f) {
        return 0;
    }
    return dx * dx + dz * dz <= r * r;
}

/* NPCs and the partner flinching: a squash and back over FLINCH frames */
#define FLINCH 16
#define FLINCH_MAX 8
static struct {
    NpcEntry* npc; /* or NULL for the partner */
    char name[32]; /* the NPC's, to know it is still the same one */
    Vec scale;     /* as it was */
    int t;         /* frames left, 0 free */
} sFlinch[FLINCH_MAX];

static Vec* flinchScale(int i) {
    PartyEntry* party;
    if (sFlinch[i].npc != NULL) {
        NpcEntry* npc = sFlinch[i].npc;
        if (!(npc->flags & NPC_FLAG_ACTIVE) || strncmp(npc->description, sFlinch[i].name, sizeof(sFlinch[i].name))) {
            return NULL;
        }
        return &npc->scale;
    }
    party = partyGetPtr(marioGetPartyId());
    return party != NULL ? &party->scale : NULL;
}

static void flinch(NpcEntry* npc, Vec* pos) {
    int i, slot = -1;
    Vec* scale;
    for (i = 0; i < FLINCH_MAX; i++) {
        if (sFlinch[i].t > 0 && sFlinch[i].npc == npc) {
            return; /* already */
        }
        if (sFlinch[i].t <= 0 && slot < 0) {
            slot = i;
        }
    }
    if (slot < 0) {
        return;
    }
    sFlinch[slot].npc = npc;
    if (npc != NULL) {
        strncpy(sFlinch[slot].name, npc->description, sizeof(sFlinch[slot].name));
    }
    scale = flinchScale(slot);
    if (scale == NULL) {
        return;
    }
    sFlinch[slot].scale = *scale;
    sFlinch[slot].t = FLINCH;
    psndSFXOn_3D("SFX_BTL_FLD_ENEMY_DAMAGED1", pos);
    effSmallStarEntry(3, 2, pos->x, pos->y + 25.0f, pos->z, 0.0f, -1.0f, 0.0f);
}

static void koopsAimFrame(void);

/* the cockroaches that have paid their coin this stay in Rogueport (bit: room 0 gor_01, 1 gor_02, times two) */
static unsigned sRoachesPaid;

void portFieldRemakeFrame(void) {
    int i;
    sFrame++;
    koopsAimFrame();
    if (gp != NULL && strncmp(gp->currentMapName, "gor_", 4) != 0) {
        sRoachesPaid = 0; /* (out of Rogueport, the title, another file: they pay again next time) */
    }
    for (i = 0; i < FLINCH_MAX; i++) {
        Vec* scale;
        float k;
        if (sFlinch[i].t <= 0) {
            continue;
        }
        sFlinch[i].t--;
        scale = flinchScale(i);
        if (scale == NULL) {
            sFlinch[i].t = 0; /* gone (a map change) */
            continue;
        }
        if (sFlinch[i].t == 0 || gp->inBattle) {
            *scale = sFlinch[i].scale;
            sFlinch[i].t = 0;
            continue;
        }
        /* squashed down and out, then back */
        k = sinf(3.14159265f * (float)(FLINCH - sFlinch[i].t) / (float)FLINCH);
        scale->x = sFlinch[i].scale.x * (1.0f + 0.2f * k);
        scale->y = sFlinch[i].scale.y * (1.0f - 0.3f * k);
        scale->z = sFlinch[i].scale.z * (1.0f + 0.2f * k);
    }
}

/* Rogueport's cockroaches (gor_01, gor_02: map objects a script walks about). Squashed whenever the hammer
 * finds one (back when the room is entered again), but each pays its coin once a stay in Rogueport: paid at
 * every entry, walking between the plaza and the east side was an endless farm of two coins a trip. Out of
 * Rogueport and back, they pay again (a small thank-you, not a living). */
static void cockroaches(float x, float y, float z) {
    static const char* const kGoki[] = {"S_goki_01", "S_goki_02"};
    size_t i;
    const int room = strcmp(gp->currentMapName, "gor_02") == 0;
    if (strcmp(gp->currentMapName, "gor_01") != 0 && !room) {
        return;
    }
    for (i = 0; i < sizeof(kGoki) / sizeof(kGoki[0]); i++) {
        MapObject* obj = mapGetMapObj(kGoki[i]);
        Vec pos;
        float dx, dz;
        if (obj == NULL || (obj->flags & MAPOBJ_FLAG_HIDE)) {
            continue;
        }
        mapObjGetPos(kGoki[i], &pos);
        dx = pos.x - x;
        dz = pos.z - z;
        if (dx * dx + dz * dz > 35.0f * 35.0f || fabsf(pos.y - y) > 40.0f) {
            continue;
        }
        /* squashed: gone until the room is entered again, and its coin the first time this stay */
        mapObjFlagOn(kGoki[i], MAPOBJ_FLAG_HIDE);
        psndSFXOn_3D("SFX_BTL_FLD_ENEMY_DAMAGED1", &pos);
        effSmallStarEntry(3, 2, pos.x, pos.y + 5.0f, pos.z, 0.0f, -1.0f, 0.0f);
        if (!(sRoachesPaid & (1u << (room * 2 + (int)i)))) {
            sRoachesPaid |= 1u << (room * 2 + (int)i);
            itemEntry(pos.x, pos.y + 10.0f, pos.z, NULL, ITEM_COIN, ITEM_ENTRY_BOUNCE, -1, NULL);
            printf("[field] hammer: %s, a coin\n", kGoki[i]);
        } else {
            printf("[field] hammer: %s (its coin paid this stay)\n", kGoki[i]);
        }
        fflush(stdout);
    }
}

void portFieldHammerImpact(void) {
    MarioWork* mario = marioGetPtr();
    NpcWork* wp;
    PartyEntry* party;
    float reach, angle, x, y, z;
    int i;
    if (!portFieldHammer() || mario == NULL || gp->inBattle) {
        return;
    }
    /* the head where it lands (as npcdrv.c fbatHitCheck aims the swing) */
    reach = 1.5f * mario->wPlayerCollisionBox.x;
    angle = reviseAngle(mario->displayDirection - 90.0f + camGetPtr(CAMERA_3D)->viewYaw);
    x = mario->position.x + sinf(angle * 3.14159265f / 180.0f) * reach;
    z = mario->position.z + cosf(angle * 3.14159265f / 180.0f) * reach;
    y = mario->position.y;
    sImpact.x = x;
    sImpact.y = y;
    sImpact.z = z;
    sImpact.frame = sFrame;

    /* friendly NPCs with no hit event of their own (those run it: npcdrv.c,
     * field battle mode 3) */
    wp = npcGetWorkPtr();
    for (i = 0; wp != NULL && i < wp->total; i++) {
        NpcEntry* npc = &wp->entries[i];
        float dx, dz, r;
        if (!(npc->flags & NPC_FLAG_ACTIVE) || (npc->flags & (NPC_FLAG_ENEMY | NPC_FLAG_INTANGIBLE)) ||
            (npc->reactionFlags & 2)) {
            continue;
        }
        dx = npc->position.x - x;
        dz = npc->position.z - z;
        r = 0.5f * npc->width + 12.0f;
        if (dx * dx + dz * dz > r * r || y + 40.0f < npc->position.y || y > npc->position.y + npc->height) {
            continue;
        }
        flinch(npc, &npc->position);
    }
    party = partyGetPtr(marioGetPartyId());
    if (party != NULL) {
        float dx = party->position.x - x, dz = party->position.z - z;
        if (dx * dx + dz * dz <= 22.0f * 22.0f && fabsf(party->position.y - y) < 40.0f) {
            flinch(NULL, &party->position);
        }
    }
    cockroaches(x, y, z);
}

/* --- Koops' aim --- */

#define KOOPS_REACH 120.0f /* party_nokonoko.c: the shell slows to a stop 120 out */

static EffectEntry* sAim;
static void* sAimWork;   /* its work, to know the effect is still ours */
static unsigned sAimSeen; /* the frame portKoopsAim last kept it */
/* the mark on the ground where it stops: the game's ripple (as Vivian's
 * shadow ring), in white, held at one size and kept on while the aim is
 * (its life topped up, its strength raised), and a fainter one pulsing out
 * from it every KOOPS_RING_EVERY frames; named for the log's proof they were
 * drawn (eff_ripple_n64.c, port/include/port/vfx_trace.h). Draw only. */
static EffectEntry* sRing;
static void* sRingWork;
static unsigned sRingNext;
#define KOOPS_RING_EVERY 12
#define KOOPS_RING_SIZE 3.0f /* (its 16-unit quad: about twice the shell's width) */
#define KOOPS_RING_ALPHA 255 /* (the ripple eases it to 120 each frame: about 214 drawn) */

static int ringOurs(void) {
    return sRing != NULL && sRing->flags != 0 && sRing->userdata == sRingWork;
}

static void aimOff(void) {
    if (sAim != NULL && sAim->userdata == sAimWork) {
        effSoftDelete(sAim);
    }
    sAim = NULL;
    sAimWork = NULL;
    if (ringOurs()) {
        EffRippleN64Work* w = (EffRippleN64Work*)sRingWork;
        w->lifetime = w->lifetime > 16 ? 16 : w->lifetime; /* (it fades out) */
    }
    sRing = NULL;
    sRingWork = NULL;
}

static void ringAt(float x, float y, float z) {
    EffRippleN64Work* w;
    if (!ringOurs()) {
        sRing = effRippleEntry(0, x, y, z);
        sRingWork = sRing != NULL ? sRing->userdata : NULL;
        if (sRing == NULL) {
            return;
        }
        effSetName(sRing, "pc_koops_aim");
    }
    w = (EffRippleN64Work*)sRingWork;
    w->position.x = x;
    w->position.y = y;
    w->position.z = z;
    w->scale = KOOPS_RING_SIZE;
    w->lifetime = 40;
    w->alpha = KOOPS_RING_ALPHA;
    if ((int)(sFrame - sRingNext) >= 0) {
        EffectEntry* pulse = effRippleEntry(0, x, y, z);
        if (pulse != NULL) {
            effSetName(pulse, "pc_koops_aim");
            ((EffRippleN64Work*)pulse->userdata)->scale = KOOPS_RING_SIZE;
        }
        sRingNext = sFrame + KOOPS_RING_EVERY;
    }
}

static void koopsAimFrame(void) {
    /* Koops put away, swapped or a map change: nothing kept it last frame */
    if (sAim != NULL && sFrame - sAimSeen > 2) {
        aimOff();
    }
}

void portKoopsAim(struct PartyEntry* party) {
    const int m = party->useMotionId;
    EffNokotarouWork* work;
    float x, z, left;
    /* out (10-12) or held spinning (13, 20, 21), the player's own throw */
    if (!setting("remake_koops_aim") || (party->flags & PARTY_FLAG_IS_EVT_USE) ||
        !((m >= 10 && m <= 13) || m == 20 || m == 21)) {
        aimOff();
        return;
    }
    x = party->position.x;
    z = party->position.z;
    left = m <= 12 ? KOOPS_REACH - party->moveDistance : 0.0f;
    if (left > 0.0f) {
        movePos(&x, &z, left, party->targetDir);
    }
    if (sAim == NULL || sAim->userdata != sAimWork) {
        sAim = effNokotarouEntry(0, 0, x, party->position.y, z, 1.5f);
        sAimWork = sAim != NULL ? sAim->userdata : NULL;
        if (sAim == NULL) {
            return;
        }
        /* (the streak grows in over 32 frames, longer than the shell takes to get there: from part way) */
        ((EffNokotarouWork*)sAimWork)->scale = 0.6f;
        sRingNext = sFrame;
        printf("[koops-aim] marker at %.0f %.0f\n", x, z);
        fflush(stdout);
    }
    ringAt(x, party->position.y + 1.5f, z); /* (1.5 up, as Vivian's: over the floor) */
    sAimSeen = sFrame;
    work = (EffNokotarouWork*)sAimWork;
    /* the ring where the shell stops, stretched back to it */
    work->start = party->position;
    work->position.x = x;
    work->position.y = party->position.y;
    work->position.z = z;
    work->size = 1.5f;
}

/* --- Steadier Paper Plane --- */

#define PLANE_PANEL 120.0f /* about a plane panel's width */

static struct {
    int on;
    float x, y, z;
} sGlide;

void portPlaneGlideStart(void) {
    MarioWork* mario = marioGetPtr();
    sGlide.on = setting("remake_plane_steady") && mario != NULL;
    if (sGlide.on) {
        sGlide.x = mario->position.x;
        sGlide.y = mario->position.y;
        sGlide.z = mario->position.z;
    }
}

void portPlaneSteady(void) {
    MarioWork* mario = marioGetPtr();
    float dx, dz;
    if (!sGlide.on || mario == NULL) {
        return;
    }
    dx = mario->position.x - sGlide.x;
    dz = mario->position.z - sGlide.z;
    if (dx * dx + dz * dz >= PLANE_PANEL * PLANE_PANEL) {
        sGlide.on = 0; /* clear of the panel: the glide as the game flies it */
        return;
    }
    if (mario->position.y < sGlide.y) {
        mario->position.y = sGlide.y;
    }
}

/* --- Yoshi Kid --- */

int portYoshi(void) {
    return setting("remake_yoshi");
}

int portYoshiFlutterFrames(void) {
    /* a longer flutter (the original's 68 frames of air) */
    return portYoshi() ? 96 : 68;
}

int portYoshiTalk(void) {
    return portYoshi() && yoshiGetStatus() == 1;
}

int portYoshiActionAhead(void) {
    MarioWork* mario = marioGetPtr();
    HitEntry* ahead[2];
    int i;
    if (!portYoshiTalk() || mario == NULL) {
        return 0;
    }
    if (fbatNpcTalkCheck() != NULL) {
        return 1;
    }
    ahead[0] = mario->hitobjFrontA;
    ahead[1] = mario->hitobjFront;
    for (i = 0; i < 2; i++) {
        CaseEntry* ce = ahead[i] != NULL ? caseCheckHitObj(ahead[i]) : NULL;
        if (ce != NULL && (ce->conditionId == CASE_COND_FRONT_A || ce->conditionId == CASE_COND_PUSH_A)) {
            return 1;
        }
    }
    return 0;
}

static int sJumpstandRide;

void portYoshiJumpstandStart(void) {
    sJumpstandRide = portYoshi() && yoshiGetStatus() != 0;
}

int portYoshiJumpstandLand(void) {
    PartyEntry* party;
    if (!sJumpstandRide) {
        return 0;
    }
    sJumpstandRide = 0;
    party = partyGetPtr(marioGetPartyId());
    if (party == NULL || party->currentMemberId != PARTY_MEMBER_YOSHI) {
        return 0;
    }
    marioChgMot(MARIO_MOTION_YOSHI);
    marioRideYoshi();
    return 1;
}

/* --- Ledge teetering --- */

/* the pose: Mario's flailing one (thrown up by spikes, mot_damage.c); every
 * Play as pick has its own of it (docs/PLAY_AS.md, the parity check) */
#define TEETER_POSE "M_D_6"

static int sTeetering;

/* standing with the floor gone just past his front foot */
static int atLedge(MarioWork* mario) {
    const float ahead = 0.5f * mario->wPlayerCollisionBox.x + 8.0f;
    float x = mario->position.x, z = mario->position.z, groundY;
    HitEntry* floor;
    if (mario->hitobjStandOn == NULL || mario->frontWallDist < ahead + 4.0f) {
        return 0; /* in the air, or a wall in front (the floor beyond it isn't his edge) */
    }
    movePos(&x, &z, ahead, mario->moveDirection);
    floor = searchUnder2(&groundY, x, mario->position.y, z);
    return floor == NULL || groundY < mario->position.y - 30.0f;
}

int portLedgeTeeter(void) {
    MarioWork* mario = marioGetPtr();
    const int teeter = setting("remake_teeter") && mario != NULL && mario->currSubMotionId == 0 &&
                       mario->controlStickSensitivity == 0.0f && !(mario->flags & MARIO_FLAG_PAPER_MODE) &&
                       atLedge(mario);
    if (teeter) {
        if (!sTeetering || strcmp(mario->animName, TEETER_POSE) != 0) {
            marioChgPose(TEETER_POSE);
        }
        mario->multiTimer = 0; /* no dozing off on the brink */
        sTeetering = 1;
        return 1;
    }
    if (sTeetering) {
        sTeetering = 0;
        if (mario != NULL && strcmp(mario->animName, TEETER_POSE) == 0) {
            marioChgPose("M_S_1");
        }
    }
    return 0;
}

/* --- Guard and Superguard stances --- */

/* Mario's battle poses for them (b_mario; every Play as pick has its own):
 * the Defend command's brace, and the arms thrust out of using an item */
#define GUARD_POSE "M_B_2"
#define SUPERGUARD_POSE "M_C_4"
#define STANCE_FRAMES 14

void* portAcCueDefender(void); /* ac_cue_pc.c */

static struct {
    BattleUnit* unit;
    const char* pose;
    char saved[0x40];
    u32 savedFlags;
    int t;
} sStance;

void portGuardStanceUpdate(BattleWork* wp) {
    BattleUnit* unit;
    BattleUnitPart* part;
    u32 trg;
    if (sStance.t > 0 && --sStance.t == 0) {
        /* back to the pose it had, unless something else changed it meanwhile (a hit) */
        part = sStance.unit != NULL ? BtlUnit_GetPartsPtr(sStance.unit, BtlUnit_GetBodyPartsId(sStance.unit)) : NULL;
        if (part != NULL && strcmp(part->poseName, sStance.pose) == 0) {
            strcpy(part->poseName, sStance.saved);
            part->animFlags = sStance.savedFlags | PART_ANIM_CHANGE;
        }
        sStance.unit = NULL;
    }
    if (wp == NULL || !setting("remake_guard_stance")) {
        return;
    }
    trg = wp->actionCommands.pad.buttonsPressedHistory[0];
    if (!(trg & (PAD_BUTTON_A | PAD_BUTTON_B))) {
        return;
    }
    /* only while an enemy's attack on Mario is coming (not a menu's A) */
    unit = (BattleUnit*)portAcCueDefender();
    if (unit == NULL || unit != BattleGetMarioPtr(wp) || unit->currentHp <= 0) {
        return;
    }
    part = BtlUnit_GetPartsPtr(unit, BtlUnit_GetBodyPartsId(unit));
    if (part == NULL) {
        return;
    }
    if (sStance.t == 0) {
        strncpy(sStance.saved, part->poseName, sizeof(sStance.saved) - 1);
        sStance.savedFlags = part->animFlags;
    }
    sStance.unit = unit;
    sStance.pose = (trg & PAD_BUTTON_B) ? SUPERGUARD_POSE : GUARD_POSE;
    sStance.t = STANCE_FRAMES;
    btlDispChangeAnime(part, sStance.pose, 0);
}
