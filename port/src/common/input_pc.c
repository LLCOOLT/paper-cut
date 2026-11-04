/* Scripted controller input ($TTYD_INPUT=<file>), for tests and for replaying
 * a bug: drives controller 1 frame by frame, applied over the backend's own
 * read (src/sdk/DEMOPad.c calls portInputApply after PADRead).
 *
 * The script starts at the first frame, or, with the $TTYD_MAP warp, once the
 * map is reached (portInputStart). One command per line; '#' comments:
 *   wait N              no input for N frames
 *   hold KEYS N         hold KEYS for N frames; KEYS joined with '+':
 *                       a b x y z l r start, left right up down (main stick,
 *                       full tilt), dleft dright dup ddown (d-pad),
 *                       cleft cright cup cdown (C-stick)
 *   press KEYS          hold KEYS 2 frames, then release for 2
 *   stick X Y N         main stick at X, Y (-128..127) for N frames
 *   shot NAME           capture this frame as NAME.png (Aurora, $TTYD_SHOT_DIR)
 *   battle              start a battle with the nearest enemy NPC, as a map
 *                       script would (evt_npc_battle_start)
 *   approach [MAX] [hammer]  walk at the nearest field enemy until a battle
 *                       starts (at most MAX frames, default 1200); "hammer"
 *                       swings at it once in reach, for a First Strike
 *   act TYPE [WEAPON [TARGET]]  in a battle, the acting unit's command
 *                       picked through the menus (pickCommand): the act of
 *                       TYPE (0 Jump, 1 Hammer, 2 Items, 6 the partner's
 *                       moves; "1|0": the first that isn't greyed), the
 *                       weapon whose name holds WEAPON ("a|b" likewise; for
 *                       Items a number: the item of that ID), target N
 *                       (0, 1, ...: the Nth in the game's target order;
 *                       -1: the player's own unit, Mario's or Luigi's, for
 *                       a Shroom (any other negative but -2 means -1 too);
 *                       -2: the one highest up, a flier)
 *                       (none of WEAPON pickable, no FP: Tactics, Appeal)
 *   steer X Z [MAX] [HOW]  walk (or roll, or drive) to X, Z by the stick
 *                       alone, as a player steers, momentum allowed for; on
 *                       within 12 units (at most MAX frames, default 900,
 *                       then on). X Z may be a hit object's name and "-" (a
 *                       door). HOW: "hop": A when stuck against something;
 *                       "climb": A held through the hop, for steps; "walk":
 *                       a tiptoe's tilt, no run; "touch": straight at it at
 *                       the end, done once against it; "round": a step aside
 *                       when stuck; "run" / "hoprun": the stick full over,
 *                       no momentum taken off (hoprun: a full jump when
 *                       stuck); "mount": up onto a pipe at X, Z. A battle,
 *                       a new room or a scene taking control ends it
 *   goto HIT [N] / goto X Y Z  Mario put on a hit object (N units short of
 *                       it, towards the map's middle) or a position
 *   pause MS            as the window minimized for MS of real time
 *   wait_floor X1 Z1 X2 Z2  no input until both points have a floor under them
 *                       (a moving floor come round; at most 900 frames)
 *   hold_floor X1 Z1 X2 Z2  the same, steering back to where he stood at the
 *                       start meanwhile (a rolling tube coasts off the edge)
 *   collect [RANGE] [MAX]  walk to the items lying within RANGE (default
 *                       200) until none is left (at most MAX frames, 600)
 *   shot_every N [NAME] a capture every N frames while a battle is on
 *                       (NAME_<frame>), 0 stops it
 *   label NAME          a mark for the jumps below
 *   jump_if_field NAME / jump_if_battle NAME  on from the step after label
 *                       NAME if no battle is on / if one is (a battle's
 *                       turns, looped until it's over)
 *   jump_if_map MAP NAME  the same once in MAP
 *   jump_if_hp N NAME   the same while the player's HP (his battle unit's, in
 *                       a battle) is below N; jump_if_hp_below is another
 *                       name for it, the same command
 *   where [npcs|hits|hit:NAME]  Mario's position, the map, his pouch (and in
 *                       a battle the units' HP), to the log; npcs: every
 *                       NPC too; hits: the room's collision triangles to
 *                       hits_<map>.txt in $TTYD_SHOT_DIR; hit:NAME: that hit
 *                       object's position
 *   wait_ring [MAX]     in a battle, until an act ring is up (or the battle is
 *                       over), getting its lines through as act does (A now and
 *                       then, never over a guard cue; the stick at a level-up)
 *   jump_if_partner NAME  the same jump while the ring up is the partner's
 *                       (a knocked-out partner has no turn: the next ring is
 *                       Mario's, so a script mustn't count turns)
 *   hp N                set Mario's HP (pouchSetHP), e.g. 1 to reach a Game Over
 *   wait_seq N [MAX]    no input until the game sequence is N (seqdrv.h:
 *                       1 title, 2 field, 4 battle, 6 file select), at most
 *                       MAX frames (default 3600), else exit 5
 *   wait_map NAME [MAX] [b]  no input until NAME is the current map, as
 *                       wait_seq; "b": a battle also ends it (a field enemy
 *                       caught him on the way)
 *   press_until_seq KEYS N [MAX]  press KEYS every 30 frames until sequence N
 *                       (screens that ignore input during their intro)
 *   press_until_map KEYS NAME [MAX]  the same, until NAME is the current map
 *   press_until_story KEYS N [MAX]  the same, until the story (GSW 0) is N
 *   press_until_control KEYS [MAX]  the same, while Mario is held, until he
 *                       can be moved and has been for a second
 *   press_until_ring KEYS [MAX]  the same, until a battle's act ring is up and
 *                       waiting for a choice (or the battle is over)
 *   save [MAX]          the open file saved to the card, as a Save Block does
 *   checkpoint NAME [MAX]  the game saved to NAME.ckpt in $TTYD_CHECKPOINT_DIR
 *                       once Luigi can be moved (at most MAX frames, 1200);
 *                       $TTYD_CHECKPOINT_LOAD starts a later run from it (see
 *                       "checkpoints" below and checkpoint_pc.c)
 *   checkpoint_rooms on|off  a checkpoint at each room change, room_NNN_MAP
 *                       (on whenever $TTYD_CHECKPOINT_DIR is set)
 *   give ID             the item, key item or badge ID (pouchGetItem)
 *   wear ID             badge ID put on (given first if need be; BP not checked)
 *   join N / party N    partner N joins / joins and comes out (1 Goombella..7)
 *   story N / flag N    the story (GSW 0) set to N / switch GSWF N set
 *   audience N, coins N, sp N, fp N   those set (sp: star power, in 1/100 units)
 *   stp N               star points set (99: the next fight levels up)
 *   maxsp N, maxhp N    the maximums set (maxhp also fills HP)
 *   stars MASK          the special moves obtained (bit 0: Sweet Treat ...)
 *   swap                the Gameplay+ partner quick-swap control pressed
 *   travel N            Gameplay+ fast travel to hub N (0 Rogueport, 1 Petalburg ...)
 *   mobj_event NAME     run map object NAME's event, as hitting it would (a
 *                       switch's, a block's)
 *   talk_all [MAX]      talk to every NPC of the map in turn (not those parked
 *                       below the map), as A in front of one does (its talk script), A pressed every 20 frames
 *                       and B now and then to get through; each one at most
 *                       MAX frames (default 900); stops if the map changes
 *   mobj_all [MAX]      the same with every map object's event (switches,
 *                       blocks, chests)
 *   tap NX NY           a tap at NX, NY (0-1 across the picture) on a selection
 *                       list's rows (port/src/common/menu_tap_pc.c)
 *   hover NX NY         the mouse pointer moved to NX, NY (menus)
 *   back / wheel N      a right click / N notches of the wheel (up > 0), with a
 *                       menu up
 *   select N|list       open selection window type N (0-18; key items, items,
 *                       badges, partners, ...), or a window of a list of items
 *                       as a locked door's script opens; press keys to close it
 *   random N [SEED]     N frames of made-up play (random walking, A, B, X, ...;
 *                       the same for the same SEED, default 1), to shake out
 *                       crashes and stalls
 *   state save|load N   save state slot N (1-4) saved or loaded at the end of
 *                       this frame (Aurora, port/src/aurora/savestate_aurora.c)
 *   end                 exit (0) once any capture is written
 * Each command is logged as "[input] frame F: <line>".
 * $TTYD_AC_CUE_PRESS=1 presses A on the Action Command cue (timed jumps,
 * guards); $TTYD_AC_PLAY=1 does that and plays the hammer's and the stick
 * gauges' commands too (playStickCommands), for runs that fight through. */
#include "port/ac_cue.h"
#include "port/gameplus.h"
#include "port/menu_tap.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif
#include <dolphin/os.h>
#include "port/platform.h"
#include "data/mapdata.h"
#include "battle/battle.h"
#include "battle/battle_seq_command.h"
#include "driver/hitdrv.h"
#include "driver/swdrv.h"
#include "manager/cardmgr.h"
#include "driver/npcdrv.h"
#include "evt/evt_cmd.h"
#include "evt/evt_npc.h"
#include "manager/evtmgr.h"
#include "mario/mario.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "mario/mario_party.h"
#include "driver/itemdrv.h"
#include "data/item_data.h"
#include "driver/mobjdrv.h"
#include "party.h"
EventEntry* mobjRunEvent(MapObjectEntry* entry, EvtWord* eventCode); /* mobjdrv.c */
#include "driver/seqdrv.h"
#include "port/checkpoint.h"

#define MAX_LINES 8192 /* steps (a press is two); a longer script is cut, with a warning */

/* A step's kind: Step.frames, when it isn't a count of frames (>= 0: wait,
 * hold, press, stick, random). Each command has a value of its own: give a
 * new one the next free value, never one already here (two commands on one
 * value ran each other's code: select ran where's, state ran
 * press_until_ring's). */
enum {
    STEP_SHOT = -1,
    STEP_END = -2,
    STEP_BATTLE = -3,
    STEP_HP = -4,
    STEP_WAIT_SEQ = -5,             /* wait_seq, press_until_seq */
    STEP_WAIT_MAP = -6,             /* wait_map, press_until_map */
    STEP_PAUSE = -7,
    STEP_GOTO = -8,
    STEP_PRESS_UNTIL_CONTROL = -9,
    STEP_SAVE = -10,
    STEP_PRESS_UNTIL_STORY = -11,
    STEP_PRESS_UNTIL_RING = -12,
    STEP_SET = -13,                 /* give, wear, join, party, story, flag, coins, ... */
    STEP_WHERE = -14,
    STEP_MOBJ_EVENT = -15,
    STEP_TALK_ALL = -16,
    STEP_MOBJ_ALL = -17,
    STEP_TAP = -18,
    STEP_HOVER = -19,
    STEP_BACK = -20,
    STEP_WHEEL = -21,
    STEP_APPROACH = -23,
    STEP_LABEL = -24,
    STEP_JUMP_IF_FIELD = -25,
    STEP_JUMP_IF_BATTLE = -26,
    STEP_ACT = -28,
    STEP_COLLECT = -29,
    STEP_SHOT_EVERY = -30,
    STEP_STEER = -31,
    STEP_FLOOR = -32,               /* wait_floor, hold_floor */
    STEP_JUMP_IF_MAP = -33,
    STEP_JUMP_IF_HP = -34,          /* jump_if_hp, jump_if_hp_below (one command, two names) */
    STEP_JUMP_IF_PARTNER = -35,
    STEP_WAIT_RING = -36,
    STEP_SELECT = -37,
    STEP_STATE = -38,
    STEP_CHECKPOINT = -39,
    STEP_CHECKPOINT_ROOMS = -40,
};

/* the steps that run over frames (applyScript's wait, sWaitFrames counting) */
static BOOL isWaitStep(s32 kind) {
    switch (kind) {
        case STEP_WAIT_SEQ:
        case STEP_WAIT_MAP:
        case STEP_PRESS_UNTIL_CONTROL:
        case STEP_SAVE:
        case STEP_PRESS_UNTIL_STORY:
        case STEP_PRESS_UNTIL_RING:
        case STEP_APPROACH:
        case STEP_ACT:
        case STEP_COLLECT:
        case STEP_STEER:
        case STEP_FLOOR:
        case STEP_CHECKPOINT:
        case STEP_WAIT_RING:
            return TRUE;
        default:
            return FALSE;
    }
}

typedef struct Step {
    char text[96];
    u16 buttons;
    s8 stickX, stickY, substickX, substickY;
    u8 trigL, trigR;
    s32 frames; /* >= 0: frames of input; < 0: a STEP_ kind */
    char name[64];
    s32 waitMax; /* the most frames a waiting step takes; for some, a number argument */
    u32 seed;    /* random: its generator's seed (0: none, plain input) */
} Step;

/* random N [SEED]: N frames of made-up play, the same for the same seed */
static u32 sRand;
static s32 sRandHold;
static PADStatus sRandPad;

static u32 nextRand(void) {
    sRand = sRand * 1664525u + 1013904223u;
    return sRand >> 8;
}

/* A new input every 6-30 frames: mostly walking or running somewhere, often A
 * (talk, check, jump), sometimes B, X, Y, Z, R, L, the C-stick or the d-pad,
 * rarely START; never the GameCube's reset combination (START + B + X). */
static void randomInput(PADStatus* out) {
    if (sRandHold <= 0) {
        static const u16 kButtons[] = {PAD_BUTTON_A, PAD_BUTTON_A, PAD_BUTTON_A, PAD_BUTTON_B, PAD_BUTTON_X,
                                       PAD_BUTTON_Y, PAD_TRIGGER_Z, PAD_TRIGGER_R, PAD_TRIGGER_L, PAD_BUTTON_LEFT,
                                       PAD_BUTTON_RIGHT, PAD_BUTTON_UP, PAD_BUTTON_DOWN};
        const u32 r = nextRand() % 100;
        memset(&sRandPad, 0, sizeof(sRandPad));
        if (r < 70) {
            const u32 a = nextRand() % 360;
            const f32 tilt = nextRand() % 3 == 0 ? 60.0f : 120.0f;
            sRandPad.stickX = (s8)(tilt * cosf(a * 0.017453292f));
            sRandPad.stickY = (s8)(tilt * sinf(a * 0.017453292f));
        }
        if (nextRand() % 100 < 45) {
            sRandPad.button |= kButtons[nextRand() % (sizeof(kButtons) / sizeof(kButtons[0]))];
        }
        if (nextRand() % 400 == 0) { /* about once a minute: mostly field play */
            sRandPad.button |= PAD_BUTTON_START;
            sRandPad.button &= ~(PAD_BUTTON_B | PAD_BUTTON_X);
        }
        if (nextRand() % 100 < 5) {
            sRandPad.substickX = (s8)((s32)(nextRand() % 255) - 127);
        }
        sRandHold = 6 + (s32)(nextRand() % 25);
    }
    sRandHold--;
    *out = sRandPad;
    out->err = PAD_ERR_NONE;
    out->triggerLeft = (sRandPad.button & PAD_TRIGGER_L) ? 255 : 0;
    out->triggerRight = (sRandPad.button & PAD_TRIGGER_R) ? 255 : 0;
    out->analogA = (sRandPad.button & PAD_BUTTON_A) ? 255 : 0;
    out->analogB = (sRandPad.button & PAD_BUTTON_B) ? 255 : 0;
}

static s32 sWaitFrames = -1; /* frames spent in the current wait_seq / wait_map, -1: none */

/* talk_all / mobj_all: the item being run, -1 when none */
MapObjectWork* portMobjGetWork(void); /* mobjdrv.c */
EventEntry* mobjRunEvent(MapObjectEntry* entry, EvtWord* eventCode);
static s32 sSweepKind; /* 0 none, 1 talk, 2 mobj */
static s32 sSweepIndex, sSweepEvt, sSweepFrames, sSweepMax, sSweepDone;
static NpcEntry* sSweepNpc;
static char sSweepMap[32];

static Step sSteps[MAX_LINES];
static s32 sCount, sCur = -1, sLeft;
static int sState = -1; /* -1 unread, 0 off, 1 loaded, 2 running, 3 ended */
static u32 sFrame;
static s32 sShotEvery; /* shot_every N NAME: a capture every N frames in battles (0: off) */
static char sShotEveryName[48];

static BOOL parseKeys(Step* s, char* keys) {
    char* k;
    for (k = strtok(keys, "+"); k != NULL; k = strtok(NULL, "+")) {
        static const struct { const char* name; u16 button; s8 sx, sy, cx, cy; u8 l, r; } kKeys[] = {
            {"a", PAD_BUTTON_A}, {"b", PAD_BUTTON_B}, {"x", PAD_BUTTON_X}, {"y", PAD_BUTTON_Y},
            {"z", PAD_TRIGGER_Z}, {"l", PAD_TRIGGER_L, 0, 0, 0, 0, 255, 0}, {"r", PAD_TRIGGER_R, 0, 0, 0, 0, 0, 255},
            {"start", PAD_BUTTON_START},
            {"left", 0, -127, 0}, {"right", 0, 127, 0}, {"up", 0, 0, 127}, {"down", 0, 0, -127},
            {"dleft", PAD_BUTTON_LEFT}, {"dright", PAD_BUTTON_RIGHT}, {"dup", PAD_BUTTON_UP},
            {"ddown", PAD_BUTTON_DOWN},
            {"cleft", 0, 0, 0, -127, 0}, {"cright", 0, 0, 0, 127, 0}, {"cup", 0, 0, 0, 0, 127},
            {"cdown", 0, 0, 0, 0, -127},
        };
        size_t i;
        for (i = 0; i < sizeof(kKeys) / sizeof(kKeys[0]); i++) {
            if (strcmp(k, kKeys[i].name) == 0) {
                s->buttons |= kKeys[i].button;
                if (kKeys[i].sx) s->stickX = kKeys[i].sx;
                if (kKeys[i].sy) s->stickY = kKeys[i].sy;
                if (kKeys[i].cx) s->substickX = kKeys[i].cx;
                if (kKeys[i].cy) s->substickY = kKeys[i].cy;
                if (kKeys[i].l) s->trigL = kKeys[i].l;
                if (kKeys[i].r) s->trigR = kKeys[i].r;
                break;
            }
        }
        if (i == sizeof(kKeys) / sizeof(kKeys[0])) {
            return FALSE;
        }
    }
    return TRUE;
}

/* port/src/aurora/savestate_aurora.c defines it; the headless build has no save states */
__attribute__((weak)) void portSaveStateRequest(int slot, int load) {
    (void)slot;
    (void)load;
}

static void load(const char* path) {
    FILE* f = fopen(path, "r");
    char line[128];
    int n = 0;
    if (f == NULL) {
        fprintf(stderr, "TTYD_INPUT: cannot open %s\n", path);
        exit(2);
    }
    while (fgets(line, sizeof(line), f) && sCount < MAX_LINES - 1) {
        char cmd[32] = "", a1[64] = "", a2[16] = "", a3[16] = "", a4[16] = "";
        char* hash = strchr(line, '#');
        Step* s = &sSteps[sCount];
        n++;
        if (hash) *hash = '\0';
        if (sscanf(line, "%31s %63s %15s %15s %15s", cmd, a1, a2, a3, a4) < 1) {
            continue;
        }
        memset(s, 0, sizeof(*s));
        snprintf(s->text, sizeof(s->text), "%s %s %s %s", cmd, a1, a2, a3);
        if (strcmp(cmd, "wait") == 0) {
            s->frames = atoi(a1);
        } else if (strcmp(cmd, "hold") == 0) {
            s->frames = atoi(a2);
            if (!parseKeys(s, a1)) goto bad;
        } else if (strcmp(cmd, "press") == 0) {
            Step* rel = &sSteps[sCount + 1];
            s->frames = 2;
            if (!parseKeys(s, a1)) goto bad;
            memset(rel, 0, sizeof(*rel));
            snprintf(rel->text, sizeof(rel->text), "(release)");
            rel->frames = 2;
            sCount++;
        } else if (strcmp(cmd, "stick") == 0) {
            s->stickX = (s8)atoi(a1);
            s->stickY = (s8)atoi(a2);
            s->frames = atoi(a3);
        } else if (strcmp(cmd, "shot") == 0) {
            s->frames = STEP_SHOT;
            snprintf(s->name, sizeof(s->name), "%s", a1);
        } else if (strcmp(cmd, "battle") == 0) {
            s->frames = STEP_BATTLE;
            snprintf(s->name, sizeof(s->name), "%s", a1); /* optional: an entry of the area's table */
        } else if (strcmp(cmd, "wait_seq") == 0 || strcmp(cmd, "wait_map") == 0) {
            s->frames = cmd[5] == 's' ? STEP_WAIT_SEQ : STEP_WAIT_MAP;
            snprintf(s->name, sizeof(s->name), "%s", a1);
            s->waitMax = a2[0] ? atoi(a2) : 3600;
            s->stickX = a3[0] == 'b'; /* (wait_map's battle flag: no stick is applied by a wait) */
        } else if (strcmp(cmd, "press_until_seq") == 0 || strcmp(cmd, "press_until_map") == 0) {
            /* a wait_seq / wait_map that also presses KEYS every 30 frames */
            s->frames = strcmp(cmd, "press_until_seq") == 0 ? STEP_WAIT_SEQ : STEP_WAIT_MAP;
            snprintf(s->name, sizeof(s->name), "%s", a2);
            s->waitMax = a3[0] ? atoi(a3) : 3600;
            if (!parseKeys(s, a1)) goto bad;
        } else if (strcmp(cmd, "save") == 0) {
            /* save: the open file, here and now, as a Save Block does
             * (cardWrite); waits until the card is done. A checkpoint: a later
             * run resumes with load_file.txt */
            s->frames = STEP_SAVE;
            s->waitMax = a1[0] ? atoi(a1) : 1200;
        } else if (strcmp(cmd, "press_until_story") == 0) {
            /* press KEYS every 30 frames until the story (GSW 0) reaches N */
            s->frames = STEP_PRESS_UNTIL_STORY;
            snprintf(s->name, sizeof(s->name), "%s", a2);
            s->waitMax = a3[0] ? atoi(a3) : 3600;
            if (!parseKeys(s, a1)) goto bad;
        } else if (strcmp(cmd, "press_until_ring") == 0) {
            /* press KEYS every 30 frames (messages) until a battle's act ring is
             * up and waiting (or the battle is over) */
            s->frames = STEP_PRESS_UNTIL_RING;
            s->waitMax = a2[0] ? atoi(a2) : 3600;
            if (!parseKeys(s, a1)) goto bad;
        } else if (strcmp(cmd, "press_until_control") == 0) {
            /* press KEYS every 30 frames until Mario can be moved (cutscenes
             * and messages over) and has been for a second */
            s->frames = STEP_PRESS_UNTIL_CONTROL;
            s->waitMax = a2[0] ? atoi(a2) : 3600;
            if (!parseKeys(s, a1)) goto bad;
        } else if (strcmp(cmd, "label") == 0 || strcmp(cmd, "jump_if_field") == 0 ||
                   strcmp(cmd, "jump_if_battle") == 0) {
            /* label NAME; jump_if_field / jump_if_battle NAME: on to the step
             * after the label if no battle is on / one is (a battle's turns,
             * looped until it's over) */
            s->frames = cmd[0] == 'l' ? STEP_LABEL : cmd[8] == 'f' ? STEP_JUMP_IF_FIELD : STEP_JUMP_IF_BATTLE;
            snprintf(s->name, sizeof(s->name), "%s", a1);
        } else if (strcmp(cmd, "jump_if_map") == 0 || strcmp(cmd, "jump_if_hp") == 0 ||
                   strcmp(cmd, "jump_if_hp_below") == 0) {
            /* jump_if_map MAP NAME: on to the step after label NAME once in that room;
             * jump_if_hp (or jump_if_hp_below) N NAME: the same while Mario's HP is below N
             * (in a battle his unit's HP: a player reaching for a Mushroom) */
            s->frames = cmd[8] == 'm' ? STEP_JUMP_IF_MAP : STEP_JUMP_IF_HP;
            snprintf(s->name, sizeof(s->name), "%s %s", a1, a2);
        } else if (strcmp(cmd, "jump_if_partner") == 0) {
            s->frames = STEP_JUMP_IF_PARTNER;
            snprintf(s->name, sizeof(s->name), "%s", a1);
        } else if (strcmp(cmd, "wait_ring") == 0) {
            s->frames = STEP_WAIT_RING;
            s->waitMax = a1[0] ? atoi(a1) : 6000;
        } else if (strcmp(cmd, "act") == 0) {
            /* act TYPE [WEAPON [TARGET]]: the acting unit's command picked in
             * the battle menus, by the stick and A as a player picks it */
            s->frames = STEP_ACT;
            snprintf(s->name, sizeof(s->name), "%s %s %s", a1, a2[0] ? a2 : "-", a3[0] ? a3 : "0");
            s->waitMax = 6000; /* (a boss's end can take the turn: its scene, then the level-up) */
        } else if (strcmp(cmd, "shot_every") == 0) {
            s->frames = STEP_SHOT_EVERY;
            s->waitMax = atoi(a1);
            snprintf(s->name, sizeof(s->name), "%s", a2[0] ? a2 : "every");
        } else if (strcmp(cmd, "collect") == 0) {
            /* walk to the items lying about within RANGE (a badge out of its
             * block, a battle's coins) until none is left, or MAX frames */
            s->frames = STEP_COLLECT;
            s->waitMax = a2[0] ? atoi(a2) : 600;
            snprintf(s->name, sizeof(s->name), "%s", a1[0] ? a1 : "200");
        } else if (strcmp(cmd, "steer") == 0) {
            /* steer X Z [MAX] [hop]: the stick toward a point (steerTo) */
            s->frames = STEP_STEER;
            s->waitMax = a3[0] ? atoi(a3) : 900;
            snprintf(s->name, sizeof(s->name), "%s %s %s", a1, a2, a4);
        } else if (strcmp(cmd, "wait_floor") == 0 || strcmp(cmd, "hold_floor") == 0) {
            /* wait_floor X1 Z1 X2 Z2: no input until both points have a floor under them;
             * hold_floor: the same, steering back to where he stands (the tube coasts) */
            s->frames = STEP_FLOOR;
            s->stickX = strcmp(cmd, "hold_floor") == 0;
            s->waitMax = 900;
            snprintf(s->name, sizeof(s->name), "%s %s %s %s", a1, a2, a3, a4);
        } else if (strcmp(cmd, "approach") == 0) {
            /* walk at the nearest field enemy until a battle starts */
            s->frames = STEP_APPROACH;
            s->waitMax = a1[0] ? atoi(a1) : 1200;
            snprintf(s->name, sizeof(s->name), "%s", a2);
        } else if (strcmp(cmd, "where") == 0) {
            s->frames = STEP_WHERE;
            snprintf(s->name, sizeof(s->name), "%s", a1); /* "npcs": every NPC too */
        } else if (strcmp(cmd, "hp") == 0) {
            s->frames = STEP_HP;
            s->stickX = 0;
            snprintf(s->name, sizeof(s->name), "%s", a1);
        } else if (strcmp(cmd, "goto") == 0) {
            /* goto HIT: Mario onto a hit object (an exit's zone, a door), as
             * evt_mario_set_pos does; the script then walks or presses into it */
            s->frames = STEP_GOTO;
            if (a3[0]) { /* goto X Y Z: a position */
                snprintf(s->name, sizeof(s->name), "%s %s %s", a1, a2, a3);
            } else {
                snprintf(s->name, sizeof(s->name), "%s %s", a1, a2);
            }
        } else if (strcmp(cmd, "pause") == 0) {
            s->frames = STEP_PAUSE;
            snprintf(s->name, sizeof(s->name), "%s", a1);
        } else if (strcmp(cmd, "give") == 0 || strcmp(cmd, "wear") == 0 || strcmp(cmd, "join") == 0 ||
                   strcmp(cmd, "party") == 0 ||
                   strcmp(cmd, "story") == 0 || strcmp(cmd, "flag") == 0 || strcmp(cmd, "audience") == 0 ||
                   strcmp(cmd, "coins") == 0 || strcmp(cmd, "stp") == 0 || strcmp(cmd, "sp") == 0 ||
                   strcmp(cmd, "fp") == 0 ||
                   strcmp(cmd, "maxsp") == 0 || strcmp(cmd, "maxhp") == 0 || strcmp(cmd, "stars") == 0 ||
                   strcmp(cmd, "swap") == 0 || strcmp(cmd, "travel") == 0) {
            /* state set-up for a test, at once */
            s->frames = STEP_SET;
            snprintf(s->name, sizeof(s->name), "%s", cmd);
            s->waitMax = (s32)strtol(a1, NULL, 0);
        } else if (strcmp(cmd, "random") == 0) {
            s->frames = atoi(a1);
            s->seed = a2[0] ? (u32)strtoul(a2, NULL, 0) : 1u;
            if (s->seed == 0) {
                s->seed = 1;
            }
        } else if (strcmp(cmd, "talk_all") == 0 || strcmp(cmd, "mobj_all") == 0) {
            s->frames = cmd[0] == 't' ? STEP_TALK_ALL : STEP_MOBJ_ALL;
            s->waitMax = a1[0] ? atoi(a1) : 900;
        } else if (strcmp(cmd, "tap") == 0 || strcmp(cmd, "hover") == 0) {
            s->frames = cmd[0] == 't' ? STEP_TAP : STEP_HOVER;
            snprintf(s->name, sizeof(s->name), "%s %s", a1, a2);
        } else if (strcmp(cmd, "back") == 0) {
            s->frames = STEP_BACK;
        } else if (strcmp(cmd, "wheel") == 0) {
            s->frames = STEP_WHEEL;
            snprintf(s->name, sizeof(s->name), "%s", a1);
        } else if (strcmp(cmd, "mobj_event") == 0) {
            s->frames = STEP_MOBJ_EVENT;
            snprintf(s->name, sizeof(s->name), "%s", a1);
        } else if (strcmp(cmd, "select") == 0) {
            s->frames = STEP_SELECT;
            s->waitMax = strcmp(a1, "list") == 0 ? -1 : atoi(a1);
        } else if (strcmp(cmd, "state") == 0) {
            s->frames = STEP_STATE;
            s->waitMax = atoi(a2) - 1;
            s->stickX = strcmp(a1, "load") == 0;
            if (strcmp(a1, "load") != 0 && strcmp(a1, "save") != 0) goto bad;
        } else if (strcmp(cmd, "checkpoint") == 0) {
            /* checkpoint NAME [MAX]: the game saved to NAME.ckpt once Luigi can be moved (at most
             * MAX frames, default 1200; then on without it) */
            s->frames = STEP_CHECKPOINT;
            snprintf(s->name, sizeof(s->name), "%s", a1);
            s->waitMax = a2[0] ? atoi(a2) : 1200;
            if (!a1[0]) goto bad;
        } else if (strcmp(cmd, "checkpoint_rooms") == 0) {
            /* checkpoint_rooms on|off: a checkpoint at every room change (on by default) */
            s->frames = STEP_CHECKPOINT_ROOMS;
            s->waitMax = strcmp(a1, "off") != 0;
        } else if (strcmp(cmd, "end") == 0) {
            s->frames = STEP_END;
        } else {
            goto bad;
        }
        sCount++;
        continue;
    bad:
        fprintf(stderr, "TTYD_INPUT: %s:%d: cannot parse '%s'\n", path, n, line);
        exit(2);
    }
    if (!feof(f)) {
        fprintf(stderr, "TTYD_INPUT: %s: more than %d steps, the rest is ignored\n", path, MAX_LINES - 1);
        printf("[input] WARNING: %s: more than %d steps, the rest is ignored\n", path, MAX_LINES - 1);
    }
    fclose(f);
}

/* pause MS: as the window being minimized for MS of real time (portPause);
 * reports how far the game clock moved meanwhile (should be ~0). */
static void pauseFor(int ms) {
    OSTime before;
    portPause();
    before = OSGetTime();
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    {
        struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000};
        nanosleep(&ts, NULL);
    }
#endif
    printf("[input] paused %d ms; game clock moved %lld us\n", ms,
           (long long)OSTicksToMicroseconds(OSGetTime() - before));
    fflush(stdout);
    portResume();
}

static char sBattleNpc[32];

/* select N|list: a selection window (winmgr.c's select_desc_tbl type N,
 * or a list of items, as a door or chest asking for a key passes) */
USER_FUNC(evt_win_item_select);
EVT_BEGIN(input_select_evt)
    CALL(evt_win_item_select, 1, LW(0), LW(1), LW(2))
    RETURN
EVT_END()
static s32 sSelectList[] = {0x001F, 0x0021, 0x0001, -1}; /* key items' ids; any list will do */

EVT_BEGIN(input_battle_evt)
    CALL(evt_npc_battle_start, PTR(sBattleNpc), 0)
    RETURN
EVT_END()

/* battle N: entry N of the current area's battle table (relSetBtlAddr),
 * given to the nearest NPC, as a script's evt_npc_set_battle_info does for a
 * scripted fight: every enemy formation can be fought from one map of its
 * area. FALSE (logged) if there is no such entry. */
static BOOL setBattleEntry(NpcEntry* npc, s32 n) {
    char area[4] = {gp->currentMapName[0], gp->currentMapName[1], gp->currentMapName[2], '\0'};
    AreaData* data = areaDataPtr(area);
    NpcSetupBattleInfo* tbl = data != NULL ? (NpcSetupBattleInfo*)data->btlSetupTbl : NULL;
    s32 count = 0;
    while (tbl != NULL && tbl[count].name != NULL) {
        count++;
    }
    if (n < 0 || n >= count) {
        printf("[input] battle: area %s has no battle %d (%d entries)\n", area, (int)n, (int)count);
        fflush(stdout);
        return FALSE;
    }
    npcSetBattleInfo(npc, n);
    printf("[input] battle %d of %s (%d entries)\n", (int)n, area, (int)count);
    return TRUE;
}

/* every NPC entry with a name, to the log */
static void listNpcs(void) {
    NpcWork* wp = npcGetWorkPtr();
    s32 i;
    for (i = 0; i < wp->total; i++) {
        const NpcEntry* npc = &wp->entries[i];
        if (npc->description[0] != '\0') {
            printf("[input]   npc '%s' (flags %x) at %.0f %.0f %.0f, group %p, defeat script %p\n",
                   npc->description, (unsigned)npc->flags, npc->position.x, npc->position.y,
                   npc->position.z, (void*)npc->battleInfo.groupSetup, (void*)npc->deadEvent);
        }
    }
}

/* approach: the stick toward the nearest live enemy NPC (as the field battle
 * check sees one: a battle group and a defeat script), as a player walks at
 * it, until it touches and the battle starts. With "hammer", B is pressed
 * once it's within a hammer's reach (a First Strike, if it lands). The camera
 * looks down -z: up on the stick is -z. */
ItemWork* portItemGetWork(void); /* src/driver/itemdrv.c */

/* the stick for a walk in the world's direction (dx, dz, not normalised), at TILT: the game turns
 * the stick by the camera's yaw (marioGetStick), and a room's camera zones can turn it (tou_04's
 * halls: up is -x in the east one) */
static void stickWorld(PADStatus* pad, f32 dx, f32 dz, f32 tilt) {
    const f32 len = sqrtf(dx * dx + dz * dz);
    f32 yaw = marioGetPtr()->targetYaw * 3.14159265f / 180.0f, c, s;
    if (len < 0.001f) {
        return;
    }
    dx /= len;
    dz /= len;
    c = cosf(yaw);
    s = sinf(yaw);
    pad[0].stickX = (s8)((dx * c + dz * s) * tilt);
    pad[0].stickY = (s8)((dx * s - dz * c) * tilt);
}

/* collect: the stick toward the nearest item that can be picked up within
 * range; 1 once there is none */
static int collectItems(PADStatus* pad, f32 range, s32 frame) {
    ItemWork* wp = portItemGetWork();
    MarioWork* mario = marioGetPtr();
    const ItemEntry* best = NULL;
    f32 bestDist = range * range, dx = 0.0f, dz = 0.0f, len;
    s32 i;
    for (i = 0; i < wp->count; i++) {
        const ItemEntry* it = &wp->entries[i];
        f32 ex, ez;
        if (!(it->flags & ITEM_FLAG_USED) || (it->flags & ITEM_FLAG_NO_TOUCH)) {
            continue;
        }
        ex = it->position.x - mario->position.x;
        ez = it->position.z - mario->position.z;
        if (ex * ex + ez * ez < bestDist) {
            bestDist = ex * ex + ez * ez;
            best = it;
            dx = ex;
            dz = ez;
        }
    }
    if (best == NULL) {
        return 1;
    }
    len = sqrtf(bestDist);
    if (frame % 60 == 0) {
        printf("[input] collect: item %d at %.0f %.0f %.0f, %.0f away\n", (int)best->itemId, best->position.x,
               best->position.y, best->position.z, len);
        fflush(stdout);
    }
    if (len > 4.0f) {
        stickWorld(pad, dx, dz, 90.0f);
    }
    return 0;
}

/* steer: the stick toward X, Z, with the speed it already has taken off
 * (the tube's and the kart's momentum), full tilt; 1 once within 12 units */
static int steerTo(PADStatus* pad, const char* spec, s32 frame) {
    static Vec prev;
    static s32 still, climbHold;
    static char map[16];
    MarioWork* mario = marioGetPtr();
    f32 tx = 0.0f, tz = 0.0f, dx, dz, len, vx, vy, vz, ax, az, al;
    char how[16] = "";
    /* the point may be a hit object's name: steer HIT - [MAX] [how], an exit's zone or a door as a player walks to it */
    if (sscanf(spec, "%f %f %15s", &tx, &tz, how) < 2) {
        char hit[64] = "", z[16] = "";
        Vec pos = {0.0f, 0.0f, 0.0f};
        sscanf(spec, "%63s %15s %15s", hit, z, how);
        hitObjGetPos(hit, &pos);
        tx = pos.x;
        tz = pos.z;
    }
    if (frame == 0) {
        prev = mario->position;
        still = 0;
        snprintf(map, sizeof(map), "%s", gp != NULL ? gp->currentMapName : "");
    }
    /* a door taken or a battle begun ends the walk: on to what the script does about it */
    if (seqGetSeq() == SEQ_BATTLE || (gp != NULL && strcmp(map, gp->currentMapName) != 0)) {
        printf("[input] steer: %s, on\n", seqGetSeq() == SEQ_BATTLE ? "a battle" : "a new room");
        fflush(stdout);
        return 1;
    }
    vx = mario->position.x - prev.x;
    vz = mario->position.z - prev.z;
    vy = mario->position.y - prev.y;
    prev = mario->position;
    dx = tx - mario->position.x;
    dz = tz - mario->position.z;
    len = sqrtf(dx * dx + dz * dz);
    if (strcmp(how, "mount") == 0) {
        /* mount: up onto a pipe at X, Z, as a player does: run at it, a full jump near it, the stick held
         * toward its middle in the air; 1 once he stands within 6 of the middle, above the ground around it
         * (landed: at a jump's top he's still too, and the stick pressed then walks him off the pipe) */
        static f32 ground;
        if (frame == 0) {
            ground = mario->position.y;
            climbHold = 0;
        }
        if (len < 6.0f && mario->position.y > ground + 10.0f && vx * vx + vz * vz < 0.01f && fabsf(vy) < 0.01f) {
            return 1;
        }
        if (len >= 2.0f) {
            const f32 tilt = len < 12.0f ? 50.0f : 127.0f;
            pad[0].stickX = (s8)(dx / len * tilt);
            pad[0].stickY = (s8)(-dz / len * tilt);
        }
        still = vx * vx + vz * vz < 0.1f ? still + 1 : 0;
        /* near it, or stopped against a step on the way: a jump (a fresh press each time) */
        if ((len < 45.0f || still >= 6) && frame % 45 == 0 && mario->position.y <= ground + 1.0f) {
            climbHold = 20;
        }
        if (climbHold > 0) {
            pad[0].button = PAD_BUTTON_A;
            climbHold--;
        }
        return 0;
    }
    if (len < 12.0f) {
        return 1;
    }
    /* touch: straight at it, as a player pushes at a door (a slant leaves him not quite facing it, and A does nothing) */
    if (strcmp(how, "touch") == 0 && len < 60.0f) { /* the last stretch */
        if (fabsf(dx) < fabsf(dz)) {
            dx = 0.0f;
        } else {
            dz = 0.0f;
        }
        len = sqrtf(dx * dx + dz * dz);
    }
    if (strcmp(how, "run") == 0 || strcmp(how, "hoprun") == 0) {
        /* run: as a player walks there, the stick full over toward it (hoprun: and A when he's stuck) */
        stickWorld(pad, dx, dz, 127.0f);
        still = vx * vx + vz * vz < 0.1f ? still + 1 : 0;
        if (frame == 0) {
            climbHold = 0;
        }
        if (how[0] == 'h' && still >= 6 && frame > 10) {
            climbHold = 20; /* a full jump: a tap's stops short of a pipe's rim */
            still = -30;
        }
        if (climbHold > 0) {
            pad[0].button = PAD_BUTTON_A;
            climbHold--;
        }
        if (frame % 120 == 0) {
            printf("[input] steer: at %.0f %.0f %.0f, %.0f to go\n", mario->position.x, mario->position.y,
                   mario->position.z, len);
            fflush(stdout);
        }
        return 0;
    }
    ax = dx / len * 3.0f - vx;
    az = dz / len * 3.0f - vz;
    al = sqrtf(ax * ax + az * az);
    if (al > 0.001f) {
        /* walk: a tiptoe's tilt, no run (past chapter 2's guards) */
        stickWorld(pad, ax, az, strcmp(how, "walk") == 0 ? 45.0f : 127.0f);
    }
    still = vx * vx + vz * vz < 0.1f ? still + 1 : 0;
    /* touch: done once he's walked up against something (a door, whose hit is in a wall) */
    if (strcmp(how, "touch") == 0 && still >= 10 && frame > 20) {
        return 1;
    }
    /* round: stuck against an NPC or a post, a player steps aside; each time the other way */
    if (strcmp(how, "round") == 0) {
        static s32 sideLeft;
        static f32 side = 1.0f;
        if (frame == 0) {
            sideLeft = 0;
        }
        if (still >= 20 && frame > 10) {
            sideLeft = 30;
            side = -side;
            still = 0;
        }
        if (sideLeft > 0) {
            sideLeft--;
            stickWorld(pad, dz / len * side, -dx / len * side, 127.0f);
        }
    }
    if (strcmp(how, "hop") == 0 && still >= 6 && frame > 10) {
        pad[0].button = PAD_BUTTON_A;
        still = -20;
    }
    /* climb: A held through the hop (the tube's full hop tops out at 25, a tap's at 14), for steps */
    if (frame == 0) {
        climbHold = 0;
    }
    if (strcmp(how, "climb") == 0 && still >= 2 && climbHold == 0 && frame > 10) {
        climbHold = 20;
    }
    if (climbHold > 0) {
        pad[0].button = PAD_BUTTON_A;
        climbHold--;
    }
    if (frame % 60 == 0) {
        printf("[input] steer: at %.0f %.0f %.0f, %.0f to go\n", mario->position.x, mario->position.y,
               mario->position.z, len);
        fflush(stdout);
    }
    return 0;
}

static void approachEnemy(PADStatus* pad, const char* how, s32 frame) {
    NpcWork* wp = npcGetWorkPtr();
    MarioWork* mario = marioGetPtr();
    NpcEntry* best = NULL;
    f32 bestDist = 1e30f, dx = 0.0f, dz = 0.0f, len;
    s32 i;
    for (i = 0; i < wp->total; i++) {
        NpcEntry* npc = &wp->entries[i];
        f32 ex, ez;
        if (!(npc->flags & 1) || npc->battleInfo.groupSetup == NULL || npc->deadEvent == NULL ||
            npc->position.y < -500.0f) {
            continue;
        }
        ex = npc->position.x - mario->position.x;
        ez = npc->position.z - mario->position.z;
        if (ex * ex + ez * ez < bestDist) {
            bestDist = ex * ex + ez * ez;
            best = npc;
            dx = ex;
            dz = ez;
        }
    }
    if (best == NULL) {
        if (frame % 300 == 0) {
            printf("[input] approach: no enemy on this map\n");
            listNpcs();
            fflush(stdout);
        }
        return;
    }
    len = sqrtf(bestDist);
    if (frame % 60 == 0) {
        printf("[input] approach: '%s' at %.0f %.0f %.0f, %.0f away\n", best->description, best->position.x,
               best->position.y, best->position.z, len);
        fflush(stdout);
    }
    if (strcmp(how, "hammer") == 0 && len < 45.0f) {
        if (frame % 20 < 2) {
            pad[0].button = PAD_BUTTON_B;
        }
        dx = dx > 0.0f ? 1.0f : -1.0f; /* face it, no closer */
        pad[0].stickX = (s8)(dx * 40.0f);
        return;
    }
    if (len > 0.0f) {
        stickWorld(pad, dx, dz, 127.0f);
    }
}

/* act TYPE [WEAPON [TARGET]]: a battle command picked through the game's own
 * menus (battle_seq_command.c), a press at a time, as a player would: the act
 * ring turned (the stick right) to the act of type TYPE (0 Jump, 1 Hammer, 2
 * Items, 4 Special, 5 Tactics, 6 the partner's moves), its list moved (the
 * stick down) to the weapon whose name holds WEAPON ("-": the first), the
 * target moved to the TARGET'th (-1: Mario, -2: the highest up), and A at each. An act the acting unit lacks
 * (6 on Mario's ring, 0 on the partner's) falls to its first. Returns 1 once
 * the pick is made (or no battle is on). */
static int sActPicked, sActWait;
static int sActFallback; /* none of WEAPON could be picked: Tactics, Appeal (Defend) instead */
static int sActEmptyType = -1; /* the act (Jump, Hammer) whose list had nothing in reach: the other first */

static int pickCommand(PADStatus* pad, const char* spec) {
    BattleWork* bw = _battleWorkPointer;
    BattleCommand* cmd;
    BattleUnit* unit;
    BattleCursor* cursor = NULL;
    char weapon[32] = "-";
    int type = 0, target = 0, i, want = -1;
    if (bw == NULL || seqGetSeq() != SEQ_BATTLE) {
        return 1;
    }
    cmd = &bw->commandMenu;
    {
        /* TYPE may be "1|0": read as a number, its "|0" was taken for WEAPON, and an empty
         * alternative of that matched the list's first entry whatever WEAPON said */
        char types[16] = "0";
        sscanf(spec, "%15s %31s %d", types, weapon, &target);
        type = atoi(types);
    }
    if (sActFallback) {
        type = 5;
        snprintf(weapon, sizeof(weapon), "6");
    }
    if (sActWait > 0) {
        sActWait--;
        return 0;
    }
    if (!(bw->dispFlags & BATTLE_DISP_FLAG_COMMAND_MENU)) {
        /* waiting for the menu: a press now and then gets a battle's own lines through (a
         * boss's mid-battle scenes: they waited for ever), as a player reading them would */
        static int sTick;
        if (!sActPicked && ++sTick % 40 == 0 && gPortAcCueState == 0) { /* (a press over a guard cue is a mash) */
            pad[0].button = PAD_BUTTON_A;
        }
        /* (the turn asked for was the battle's last, and it's won: a level-up's prompt waits for the stick) */
        if (sTick % 40 == 20 && BattleGetSeq(bw, SEQ_END) == 0x7000011) {
            pad[0].stickX = 127;
        }
        return sActPicked; /* the menus closed after the target's A: picked */
    }
    unit = BattleGetUnitPtr(bw, bw->activeUnitId);
    if (unit == NULL) {
        return 0;
    }
    sActWait = 12; /* a press, then a beat for the menu */
    if (cmd->state == 2) {
        cursor = &cmd->cursor[unit->currentKind == UNIT_MARIO ? 0 : 1];
        {
            /* TYPE may be "1|0": the first of them whose act can be picked now (a
             * Hammer with nothing in reach is greyed: on to the Jump); the fallback
             * (Tactics) takes no alternatives */
            const char* t = sActFallback ? NULL : spec;
            if (sActEmptyType >= 0 && !sActFallback) {
                for (i = 0; want < 0 && i < cursor->count; i++) {
                    if ((cmd->act[i].type == 0 || cmd->act[i].type == 1) && cmd->act[i].type != sActEmptyType && cmd->act[i].enabled) {
                        want = i;
                    }
                }
                t = NULL;
            }
            const char* typeEnd = strchr(spec, ' ');
            if (typeEnd == NULL) {
                typeEnd = spec + strlen(spec);
            }
            while (want < 0 && t != NULL && *t) {
                const int ty = atoi(t);
                for (i = 0; i < cursor->count; i++) {
                    if (cmd->act[i].type == ty && cmd->act[i].enabled) {
                        want = i;
                    }
                }
                t = strchr(t, '|');
                t = t != NULL && t < typeEnd ? t + 1 : NULL;
            }
            /* TYPE greyed (a forced command): the act that can be picked; it took the next
             * step's A for ever on the greyed one */
            for (i = 0; want < 0 && i < cursor->count; i++) {
                if (cmd->act[i].type == type && cmd->act[i].enabled) {
                    want = i;
                }
            }
            /* (a script's act can fall on the other unit's turn, after a First Strike: Luigi
             * asked for a partner's move swings; a jump onto spikes is no help) */
            for (i = 0; want < 0 && i < cursor->count; i++) {
                if (cmd->act[i].type == 1 && cmd->act[i].enabled) {
                    want = i;
                }
            }
            for (i = 0; want < 0 && i < cursor->count; i++) {
                if (cmd->act[i].enabled) {
                    want = i;
                }
            }
            for (i = 0; want < 0 && i < cursor->count; i++) {
                if (cmd->act[i].type == type) {
                    want = i;
                }
            }
        }
        if (want < 0) {
            want = 0;
        }
        if (cursor->absPos != want) {
            pad[0].stickX = 127;
        } else {
            printf("[input] act: ring %d (type %d)\n", want, (int)cmd->act[want].type);
            fflush(stdout);
            pad[0].button = PAD_BUTTON_A;
        }
        return 0;
    }
    if (cmd->state == 7) {
        static const int kCursor[11] = {2, 3, 11, 12, 7, -1, 4, 5, 6, -1, 10};
        const int t = cmd->actType;
        int c = t >= 0 && t < 11 ? kCursor[t] : -1;
        if (t == 5) {
            c = unit->currentKind == UNIT_MARIO ? 8 : 9;
        }
        if (c < 0) {
            pad[0].button = PAD_BUTTON_A;
            return 0;
        }
        cursor = &cmd->cursor[c];
        want = 0;
        if (type == 6 && unit->currentKind == UNIT_MARIO) {
            snprintf(weapon, sizeof(weapon), "-"); /* the partner's move asked for on Luigi's turn */
        }
        if (strcmp(weapon, "-") != 0) {
            /* "a|b": the first of them that can be picked now (not greyed) */
            char* alt = weapon;
            int found = 0;
            while (alt != NULL && !found) {
                char* bar = strchr(alt, '|');
                if (bar != NULL) {
                    *bar = '\0';
                }
                for (i = 0; i < cursor->count; i++) {
                    /* a move by its weapon's name; an item (whose weapon has none) by the item's,
                     * "in_kinoko", or by its ID when WEAPON is a number */
                    const int byId = isdigit((unsigned char)alt[0]) && type == 2;
                    const char* nm = cmd->weapon[i].weapon != NULL ? cmd->weapon[i].weapon->name : NULL;
                    if (nm == NULL && cmd->weapon[i].itemId > 0 && cmd->weapon[i].itemId < ITEM_TYPE_MAX) {
                        nm = itemDataTable[cmd->weapon[i].itemId].name;
                    }
                    if (cmd->weapon[i].enabled &&
                        (byId ? cmd->weapon[i].itemId == atoi(alt) : nm != NULL && strstr(nm, alt) != NULL)) {
                        want = i;
                        found = 1;
                        break;
                    }
                }
                alt = bar != NULL ? bar + 1 : NULL;
            }
            if (!found) {
                /* nothing of WEAPON can be picked now (no FP for it): back
                 * to the ring and Appeal instead, as a player would */
                printf("[input] act: none of %s can be picked: Appeal\n", weapon);
                fflush(stdout);
                sActFallback = 1;
                pad[0].button = PAD_BUTTON_B;
                return 0;
            }
        }
        if (!cmd->weapon[want].enabled) {
            /* greyed: the first entry that isn't, else back out of the list and Defend */
            for (i = 0; i < cursor->count && !cmd->weapon[i].enabled; i++) {
            }
            if (i < cursor->count) {
                want = i;
            } else {
                if (sActEmptyType < 0 && (cmd->actType == 0 || cmd->actType == 1)) {
                    /* nothing in its reach (a Hammer at fliers): as a player would, the other one */
                    sActEmptyType = cmd->actType;
                    pad[0].button = PAD_BUTTON_B;
                    return 0;
                }
                printf("[input] act: nothing to pick in the list, Defend instead\n");
                fflush(stdout);
                sActFallback = 1;
                pad[0].button = PAD_BUTTON_B;
                return 0;
            }
        }
        if (cursor->absPos != want) {
            pad[0].stickY = -127;
        } else {
            printf("[input] act: list %d (%s)\n", want,
                   cmd->weapon[want].weapon != NULL && cmd->weapon[want].weapon->name != NULL
                       ? cmd->weapon[want].weapon->name
                   : cmd->weapon[want].itemId > 0 && cmd->weapon[want].itemId < ITEM_TYPE_MAX
                       ? itemDataTable[cmd->weapon[want].itemId].name /* an item: its name */
                       : "?");
            fflush(stdout);
            pad[0].button = PAD_BUTTON_A;
        }
        return 0;
    }
    if (cmd->state == 21) {
        /* Tactics: WEAPON is the operation's type (3 Run Away, 5 Appeal, 6 Defend, 0 Change Partner);
         * a move's name here (the ring only offered Tactics, a forced command): Defend */
        const int op = sActFallback ? 5 : isdigit((unsigned char)weapon[0]) ? atoi(weapon) : 6;
        cursor = &cmd->cursor[unit->currentKind == UNIT_MARIO ? 8 : 9];
        want = -1;
        for (i = 0; i < cursor->count; i++) {
            if (cmd->operation[i].type == op) {
                want = i;
            }
        }
        for (i = 0; want < 0 && i < cursor->count; i++) {
            if (cmd->operation[i].type == 6) { /* no Appeal without a Star Power: Defend */
                want = i;
            }
        }
        if (want < 0) {
            want = 0;
        }
        if (cursor->absPos != want) {
            pad[0].stickY = -127;
        } else {
            printf("[input] act: tactic %d (type %d)\n", want, (int)cmd->operation[want].type);
            fflush(stdout);
            pad[0].button = PAD_BUTTON_A;
            sActPicked = 1; /* a tactic needs no target */
            sActWait = 4;
        }
        return 0;
    }
    if (cmd->state == 11) {
        BattleWorkWeaponTargets* targets = &bw->weaponTargets;
        if (target == -2) {
            /* -2: the one highest up (a flier: a Lakitu over his Spinies) */
            int best = 0;
            for (i = 1; i < targets->numTargets; i++) {
                if (targets->targets[targets->targetIndices[i]].posY > targets->targets[targets->targetIndices[best]].posY) {
                    best = i;
                }
            }
            target = best;
        } else if (target < 0) {
            /* -1: Mario himself (a Shroom for him, not the partner beside him) */
            target = 0;
            for (i = 0; i < targets->numTargets; i++) {
                const BattleWorkTarget* tw = &targets->targets[targets->targetIndices[i]];
                const BattleUnit* tu = BattleGetUnitPtr(bw, tw->unitIdx);
                /* (by his unit, not his kind: Luigi's is his own, not UNIT_MARIO) */
                if (tu != NULL && tu == BattleGetMarioPtr(bw)) {
                    target = i;
                    break;
                }
            }
        }
        if (target >= targets->numTargets) {
            target = 0;
        }
        if (targets->currentTarget != target) {
            pad[0].stickX = 127;
        } else {
            {
                const BattleUnit* tu =
                    BattleGetUnitPtr(bw, targets->targets[targets->targetIndices[target]].unitIdx);
                printf("[input] act: target %d of %d (kind %d)\n", target, (int)targets->numTargets,
                       tu != NULL ? (int)tu->currentKind : -1);
            }
            fflush(stdout);
            pad[0].button = PAD_BUTTON_A;
            sActPicked = 1;
            sActWait = 4;
        }
        return 0;
    }
    sActWait = 0;
    return 0;
}

/* The nearest live NPC with a battle group (an enemy), as a map script names
 * it; with entry >= 0 the nearest live NPC, which gets that battle. */
static void startBattle(s32 entry) {
    NpcWork* wp = npcGetWorkPtr();
    MarioWork* mario = marioGetPtr();
    NpcEntry* best = NULL;
    f32 bestDist = 1e30f;
    s32 i;
    for (i = 0; i < wp->total; i++) {
        NpcEntry* npc = &wp->entries[i];
        f32 dx, dz;
        /* as the field battle check (npcdrv.c): a battle group and a defeat
         * script; story NPCs with only a group would fight out of context */
        if (!(npc->flags & 1) || (entry < 0 && (npc->battleInfo.groupSetup == NULL || npc->deadEvent == NULL))) {
            continue;
        }
        dx = npc->position.x - mario->position.x;
        dz = npc->position.z - mario->position.z;
        if (dx * dx + dz * dz < bestDist) {
            bestDist = dx * dx + dz * dz;
            best = npc;
        }
    }
    if (best == NULL) {
        printf("[input] battle: no %s on this map\n", entry < 0 ? "enemy NPC" : "NPC");
        fflush(stdout);
        return;
    }
    if (entry >= 0 && !setBattleEntry(best, entry)) {
        _Exit(6);
    }
    snprintf(sBattleNpc, sizeof(sBattleNpc), "%s", best->description);
    printf("[input] battle with '%s'\n", sBattleNpc);
    fflush(stdout);
    evtEntry(input_battle_evt, 0, 0);
}

/* --- checkpoints (port/src/common/checkpoint_pc.c) ---
 * With $TTYD_CHECKPOINT_DIR, the game is saved to a file at every room change
 * (room_NNN_<map>.ckpt, made the frame the map change starts) and at each
 * `checkpoint NAME` step (NAME.ckpt, made where Luigi stands). With
 * $TTYD_CHECKPOINT_LOAD=<file>, the run starts from one: the script waits
 * until the title is up, the checkpoint's game is loaded and its room
 * entered, and the script goes on from where it was when the checkpoint was
 * made: a room change's at the same step, with the same frames left in it
 * and the same frame count, so the run goes on as it did; a `checkpoint
 * NAME`'s at the step after it, once Luigi can be moved in the reloaded
 * room. The script may have been changed since: a room change's step is
 * found by its text, a checkpoint's by its name; $TTYD_CHECKPOINT_AT=LABEL
 * starts at the step after that label instead. */
extern const char* next_p0; /* seqdrv.c: the map and entrance of the sequence being changed to */
extern const char* next_p1;
static int sRoomsOn = -1; /* checkpoint_rooms: -1 not set (on if $TTYD_CHECKPOINT_DIR is) */
static int sRoomCount;
static int sResume;       /* 1: a checkpoint to load before the script runs; 2: loaded, waiting for control */
static s32 sResumeReady;

static void driverState(char* out, size_t size, s32 step, s32 left, s32 waitFrames) {
    const Step* st = step >= 0 && step < sCount ? &sSteps[step] : NULL;
    char text[sizeof(st->text)];
    size_t n;
    snprintf(text, sizeof(text), "%s", st != NULL ? st->text : "");
    for (n = strlen(text); n > 0 && text[n - 1] == ' '; n--) {
        text[n - 1] = '\0';
    }
    snprintf(out, size,
             "script=%s\nstep=%d\nstep_kind=%d\nstep_name=%s\nstep_text=%s\nleft=%d\nwait_frames=%d\nframe=%u\n"
             "rooms=%d\nroom_count=%d\nshot_every=%d\nshot_every_name=%s\n",
             getenv("TTYD_INPUT") != NULL ? getenv("TTYD_INPUT") : "", (int)step, st != NULL ? (int)st->frames : 0,
             st != NULL ? st->name : "", text, (int)left, (int)waitFrames, (unsigned)sFrame, sRoomsOn, sRoomCount,
             (int)sShotEvery, sShotEveryName);
}

/* the start of a frame: a map change about to begin from the field, a checkpoint of the room it goes to */
static void roomCheckpoint(void) {
    char name[64], state[1024];
    if (sRoomsOn < 0) {
        const char* dir = getenv("TTYD_CHECKPOINT_DIR");
        sRoomsOn = dir != NULL && *dir != '\0';
    }
    if (!sRoomsOn || gp == NULL || seqGetSeq() != SEQ_GAME || seqGetNextSeq() != SEQ_MAP_CHANGE || next_p0 == NULL) {
        return;
    }
    snprintf(name, sizeof(name), "room_%03d_%s", ++sRoomCount, next_p0);
    /* the driver as it stands before this frame's input (sFrame is this frame's) */
    sFrame--;
    driverState(state, sizeof(state), sCur, sLeft, sWaitFrames);
    sFrame++;
    portCheckpointSave(name, next_p0, next_p1, state);
}

static s32 findStep(s32 kind, const char* name) {
    s32 i;
    for (i = 0; i < sCount; i++) {
        if (sSteps[i].frames == kind && strcmp(sSteps[i].name, name) == 0) {
            return i;
        }
    }
    return -2;
}

/* the driver put back as the checkpoint has it */
static void restoreDriver(const char* t, int inPlace) {
    char v[128], text[96];
    const char* at = getenv("TTYD_CHECKPOINT_AT");
    const s32 step = portCheckpointValue(t, "step", v, sizeof(v)) != NULL ? atoi(v) : -1;
    const s32 kind = portCheckpointValue(t, "step_kind", v, sizeof(v)) != NULL ? atoi(v) : 0;
    char name[64] = "";
    s32 cur = -2;
    portCheckpointValue(t, "step_name", name, sizeof(name));
    if (portCheckpointValue(t, "step_text", text, sizeof(text)) == NULL) {
        text[0] = '\0';
    }
    sLeft = 0;
    sWaitFrames = -1;
    if (at != NULL && *at) {
        cur = findStep(STEP_LABEL, at);
    } else if (kind == STEP_CHECKPOINT) {
        cur = findStep(STEP_CHECKPOINT, name);
    } else if (step < 0) {
        cur = -1; /* before the first step */
    } else {
        /* the step it was on, by its text: the same index if it's still there, else the nearest with that text */
        s32 i, best = -2;
        for (i = 0; i < sCount; i++) {
            char mine[96];
            size_t n;
            snprintf(mine, sizeof(mine), "%s", sSteps[i].text);
            for (n = strlen(mine); n > 0 && mine[n - 1] == ' '; n--) {
                mine[n - 1] = '\0';
            }
            if (strcmp(mine, text) == 0 && (best < 0 || abs(i - step) < abs(best - step))) {
                best = i;
            }
        }
        cur = best;
        if (cur >= 0) {
            sLeft = portCheckpointValue(t, "left", v, sizeof(v)) != NULL ? atoi(v) : 0;
            sWaitFrames = portCheckpointValue(t, "wait_frames", v, sizeof(v)) != NULL ? atoi(v) : -1;
        }
    }
    if (cur == -2) {
        printf("[input] FAIL: checkpoint: step '%s' (%s) not in this script; name a label with "
               "TTYD_CHECKPOINT_AT\n", at != NULL && *at ? at : kind == STEP_CHECKPOINT ? name : text,
               at != NULL && *at ? "label" : kind == STEP_CHECKPOINT ? "checkpoint" : "step");
        fflush(stdout);
        _Exit(5);
    }
    sCur = cur;
    sFrame = portCheckpointValue(t, "frame", v, sizeof(v)) != NULL ? (u32)strtoul(v, NULL, 10) : sFrame;
    sRoomCount = portCheckpointValue(t, "room_count", v, sizeof(v)) != NULL ? atoi(v) : 0;
    if (portCheckpointValue(t, "rooms", v, sizeof(v)) != NULL) {
        sRoomsOn = atoi(v) != 0 && getenv("TTYD_CHECKPOINT_DIR") != NULL;
    }
    sShotEvery = portCheckpointValue(t, "shot_every", v, sizeof(v)) != NULL ? atoi(v) : 0;
    portCheckpointValue(t, "shot_every_name", sShotEveryName, sizeof(sShotEveryName));
    sSweepKind = 0;
    sActPicked = sActFallback = sActWait = 0;
    sActEmptyType = -1;
    sState = 2;
    sResume = inPlace ? 2 : 0;
    sResumeReady = 0;
    printf("[input] checkpoint: on from step %d (%s), frame %u\n", (int)sCur + (sLeft <= 0 && sWaitFrames < 0),
           sCur >= 0 && sCur < sCount ? sSteps[sCur].text : "the start", (unsigned)sFrame);
    fflush(stdout);
}

BOOL portInputActive(void) {
    if (sState < 0) {
        const char* path = getenv("TTYD_INPUT");
        sState = 0;
        if (path != NULL && *path) {
            load(path);
            sState = 1;
            sResume = portCheckpointPending(NULL) != NULL;
        }
    }
    return sState > 0;
}

/* The map warp calls this once the map is reached; without the warp the
 * script starts on the first frame. */
void portInputStart(void) {
    if (portInputActive() && sState == 1) {
        sState = 2;
    }
}

static void applyScript(PADStatus* pad) {
    const Step* s;
    if (!portInputActive()) {
        return;
    }
    sFrame++;
    if (sResume == 1) {
        /* a checkpoint to start from: nothing until it's loaded */
        int inPlace = 0;
        const char* t = portCheckpointPending(&inPlace);
        if (!portCheckpointResumeFrame()) {
            return;
        }
        restoreDriver(t, inPlace);
        if (!inPlace) {
            sFrame++; /* (it was saved as it stood before that frame) */
            goto run; /* this frame is the one the checkpoint was made on: on as it went then */
        }
    }
    if (sResume == 2) {
        /* a here-and-now checkpoint: the room reloaded, Luigi free to move for a moment */
        sResumeReady = seqGetSeq() == SEQ_GAME && gp != NULL && !gp->inBattle && marioChkKey() && marioChkCtrl()
                           ? sResumeReady + 1
                           : 0;
        if (sResumeReady < 30) {
            return;
        }
        sResume = 0;
        printf("[input] frame %u: checkpoint loaded, on\n", (unsigned)sFrame);
        fflush(stdout);
    }
    if (sState == 1 && getenv("TTYD_MAP") == NULL) {
        sState = 2;
    }
    if (sState != 2) {
        return;
    }
    roomCheckpoint();
run:
    if (sSweepKind != 0) {
        /* talk_all / mobj_all */
        memset(&pad[0], 0, sizeof(pad[0]));
        pad[0].err = PAD_ERR_NONE;
        if (gp == NULL || strncmp(gp->currentMapName, sSweepMap, sizeof(sSweepMap)) != 0) {
            printf("[input] %s: the map changed, stopping (%d run)\n", sSweepKind == 1 ? "talk_all" : "mobj_all",
                   (int)sSweepDone);
            fflush(stdout);
            sSweepKind = 0;
            return;
        }
        if (sSweepEvt >= 0) {
            const BOOL running = evtCheckID(sSweepEvt);
            if (running && ++sSweepFrames <= sSweepMax) {
                if (sSweepFrames % 20 < 2) {
                    pad[0].button = PAD_BUTTON_A;
                } else if (sSweepFrames % 90 >= 45 && sSweepFrames % 90 < 47) {
                    pad[0].button = PAD_BUTTON_B;
                }
                return;
            }
            if (running) {
                printf("[input] %s %d: still running after %d frames\n", sSweepKind == 1 ? "talk" : "mobj",
                       (int)sSweepIndex, (int)sSweepMax);
                evtDeleteID(sSweepEvt);
            }
            if (sSweepKind == 1 && sSweepNpc != NULL && (sSweepNpc->flags & 1)) {
                evtStartID(sSweepNpc->regularEvtId);
            }
            marioKeyOn();
            partyKeyOn();
            sSweepEvt = -1;
            sSweepDone++;
            sSweepIndex++;
            return; /* a frame between items */
        }
        for (;;) {
            EventEntry* e = NULL;
            if (sSweepKind == 1) {
                NpcWork* wp = npcGetWorkPtr();
                NpcEntry* npc;
                if (sSweepIndex >= wp->total) {
                    break;
                }
                npc = &wp->entries[sSweepIndex];
                /* parked out of reach (scripts put unused NPCs at y -1000):
                 * a player cannot talk to them, and their scripts may need
                 * set-up that only comes with placing them */
                if (!(npc->flags & 1) || npc->talkEvt == NULL || npc->position.y < -500.0f) {
                    sSweepIndex++;
                    continue;
                }
                printf("[input] talk %d: '%s'\n", (int)sSweepIndex, npc->description);
                evtStopID(npc->regularEvtId);
                marioKeyOff();
                partyKeyOff();
                e = evtEntry(npc->talkEvt, 0, 0);
                e->wNpcEventType = 6;
                e->ownerNPC = npc;
                sSweepNpc = npc;
            } else {
                MapObjectWork* wp = portMobjGetWork();
                MapObjectEntry* mobj;
                if (sSweepIndex >= wp->count) {
                    break;
                }
                mobj = &wp->entries[sSweepIndex];
                if (!(mobj->flags & 1) || mobj->event == 0) {
                    sSweepIndex++;
                    continue;
                }
                printf("[input] mobj %d: '%s'\n", (int)sSweepIndex, mobj->name);
                e = mobjRunEvent(mobj, (EvtWord*)mobj->event);
            }
            fflush(stdout);
            if (e == NULL) {
                sSweepIndex++;
                continue;
            }
            sSweepEvt = e->eventId;
            sSweepFrames = 0;
            return;
        }
        printf("[input] %s: %d run\n", sSweepKind == 1 ? "talk_all" : "mobj_all", (int)sSweepDone);
        fflush(stdout);
        sSweepKind = 0;
    }
    if (sShotEvery > 0 && sFrame % (u32)sShotEvery == 0 && seqGetSeq() == SEQ_BATTLE) {
        /* shot_every: a capture every N frames of a battle, through the waits */
        char name[96];
        snprintf(name, sizeof(name), "%s_%06u", sShotEveryName, (unsigned)sFrame);
        portShotRequest(name);
    }
    if (sWaitFrames >= 0) {
        /* wait_seq / wait_map: neutral input until the condition holds */
        const Step* w = &sSteps[sCur];
        static s32 sControlFrames;
        BOOL met;
        if (w->frames == STEP_PRESS_UNTIL_CONTROL) {
            sControlFrames = seqGetSeq() == SEQ_GAME && marioChkKey() && marioChkCtrl() ? sControlFrames + 1 : 0;
            /* (a field enemy caught him: on too, for jump_if_battle: this waited out its limit in a fight) */
            met = sControlFrames >= 60 || (seqGetSeq() == SEQ_BATTLE && (w->substickX == 0 && w->buttons != 0));
        } else if (w->frames == STEP_PRESS_UNTIL_RING) {
            static s32 sRingFrames;
            BattleWork* bw = _battleWorkPointer;
            const BOOL up = bw != NULL && seqGetSeq() == SEQ_BATTLE && bw->commandMenu.state == 2 &&
                            (bw->dispFlags & BATTLE_DISP_FLAG_COMMAND_MENU);
            sRingFrames = up ? sRingFrames + 1 : 0;
            met = sRingFrames >= 15 || (sWaitFrames > 60 && seqGetSeq() != SEQ_BATTLE);
            if (sRingFrames > 0 && !met) { /* the ring is coming up: no more presses */
                ++sWaitFrames;
                memset(&pad[0], 0, sizeof(pad[0]));
                pad[0].err = PAD_ERR_NONE;
                return;
            }
        } else if (w->frames == STEP_APPROACH) {
            met = seqGetSeq() == SEQ_BATTLE;
            if (!met && sWaitFrames >= w->waitMax) {
                /* out of reach (a ledge, the far side of lava): on without it */
                printf("[input] approach: no battle in %d frames, on\n", (int)w->waitMax);
                fflush(stdout);
                met = 1;
            }
        } else if (w->frames == STEP_COLLECT) {
            memset(&pad[0], 0, sizeof(pad[0]));
            pad[0].err = PAD_ERR_NONE;
            met = !(seqGetSeq() == SEQ_GAME && marioChkKey()) ? 0 : collectItems(pad, (f32)atof(w->name), sWaitFrames);
            if (!met && ++sWaitFrames > w->waitMax) {
                printf("[input] collect: still items after %d frames, on\n", (int)w->waitMax);
                met = 1;
            }
            if (!met) {
                return;
            }
        } else if (w->frames == STEP_STEER) {
            memset(&pad[0], 0, sizeof(pad[0]));
            pad[0].err = PAD_ERR_NONE;
            /* a battle or a map change took him: on, and a script's next step sees it */
            met = seqGetSeq() != SEQ_GAME && sWaitFrames > 0 ? 1 : steerTo(pad, w->name, sWaitFrames);
            {
                /* a scene took the walk over: no use pushing the stick through it */
                static s32 sNoControl;
                sNoControl = seqGetSeq() == SEQ_GAME && marioChkCtrl() && marioChkKey() ? 0 : sNoControl + 1;
                if (!met && sNoControl > 30) {
                    printf("[input] steer: %s: control lost, on\n", w->name);
                    fflush(stdout);
                    sNoControl = 0;
                    met = 1;
                }
            }
            if (!met && ++sWaitFrames > w->waitMax) {
                printf("[input] steer: %s not reached in %d frames, on\n", w->name, (int)w->waitMax);
                fflush(stdout);
                met = 1;
            }
            if (!met) {
                return;
            }
        } else if (w->frames == STEP_FLOOR) {
            f32 p[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            s32 i;
            memset(&pad[0], 0, sizeof(pad[0]));
            pad[0].err = PAD_ERR_NONE;
            sscanf(w->name, "%f %f %f %f", &p[0], &p[1], &p[2], &p[3]);
            met = 1;
            for (i = 0; i < 2; i++) {
                HitCheckQuery q;
                memset(&q, 0, sizeof(q));
                q.targetPos = (Vec){p[i * 2], 600.0f, p[i * 2 + 1]};
                q.targetDistance = 1000.0f;
                q.targetDir = (Vec){0.0f, -1.0f, 0.0f};
                if (hitCheckVecFilter(&q, NULL) == NULL) {
                    met = 0;
                }
            }
            if (!met && ++sWaitFrames > w->waitMax) {
                printf("[input] wait_floor: %s not seen in %d frames, on\n", w->name, (int)w->waitMax);
                fflush(stdout);
                met = 1;
            }
            if (!met) {
                if (w->stickX) {
                    static Vec holdAt;
                    char spot[48];
                    if (sWaitFrames == 1) {
                        holdAt = marioGetPtr()->position;
                    }
                    snprintf(spot, sizeof(spot), "%.1f %.1f", holdAt.x, holdAt.z);
                    steerTo(pad, spot, sWaitFrames - 1);
                }
                return;
            }
        } else if (w->frames == STEP_WAIT_RING) {
            /* wait_ring: as act waits for its menu, without picking anything */
            static s32 sUpFrames;
            BattleWork* bw = _battleWorkPointer;
            const BOOL inBattle = bw != NULL && seqGetSeq() == SEQ_BATTLE;
            const BOOL up = inBattle && bw->commandMenu.state == 2 && (bw->dispFlags & BATTLE_DISP_FLAG_COMMAND_MENU);
            memset(&pad[0], 0, sizeof(pad[0]));
            pad[0].err = PAD_ERR_NONE;
            sUpFrames = up ? sUpFrames + 1 : 0;
            /* (over: out of the battle sequence; its work isn't there yet while it loads) */
            met = sUpFrames >= 15 || (seqGetSeq() != SEQ_BATTLE && sWaitFrames > 0);
            if (!met && inBattle && !up) {
                if (sWaitFrames % 40 == 39 && gPortAcCueState == 0) { /* (a press over a guard cue is a mash) */
                    pad[0].button = PAD_BUTTON_A;
                }
                if (sWaitFrames % 40 == 19 && BattleGetSeq(bw, SEQ_END) == 0x7000011) {
                    pad[0].stickX = 127; /* the level-up's prompt */
                }
            }
            if (!met && ++sWaitFrames > w->waitMax) {
                printf("[input] wait_ring: no ring in %d frames, on\n", (int)w->waitMax);
                fflush(stdout);
                met = 1;
            }
            if (!met) {
                return;
            }
            sUpFrames = 0;
        } else if (w->frames == STEP_ACT) {
            memset(&pad[0], 0, sizeof(pad[0]));
            pad[0].err = PAD_ERR_NONE;
            met = pickCommand(pad, w->name);
            if (!met) {
                if (++sWaitFrames > w->waitMax) {
                    printf("[input] FAIL: %s not picked in %d frames\n", w->text, (int)w->waitMax);
                    fflush(stdout);
                    _Exit(5);
                }
                return;
            }
        } else if (w->frames == STEP_PRESS_UNTIL_STORY) {
            met = swByteGet(0) >= atoi(w->name);
        } else if (w->frames == STEP_SAVE) {
            met = sWaitFrames > 0 && !cardIsExec(); /* save: the card is done */
        } else if (w->frames == STEP_CHECKPOINT) {
            /* checkpoint NAME: once Luigi can be moved (no scene, battle or map change on), saved here */
            static s32 sFree;
            memset(&pad[0], 0, sizeof(pad[0]));
            pad[0].err = PAD_ERR_NONE;
            sFree = seqGetSeq() == SEQ_GAME && gp != NULL && !gp->inBattle && !seqCheckSeq() && marioChkKey() &&
                            marioChkCtrl()
                        ? sFree + 1
                        : 0;
            met = sFree >= 2;
            if (met) {
                char state[1024];
                driverState(state, sizeof(state), sCur, 0, -1);
                portCheckpointSave(w->name, NULL, NULL, state);
            } else if (++sWaitFrames > w->waitMax) {
                printf("[input] checkpoint %s: Luigi not free to move in %d frames, on without it\n", w->name,
                       (int)w->waitMax);
                fflush(stdout);
                met = 1;
            }
            if (!met) {
                return;
            }
            sFree = 0;
        } else {
            met = w->frames == STEP_WAIT_SEQ ? (s32)seqGetSeq() == atoi(w->name)
                                  : gp != NULL && strcmp(gp->currentMapName, w->name) == 0;
            /* wait_map MAP N b: a field enemy caught him on the way out: on, for jump_if_battle */
            met = met || (w->frames == STEP_WAIT_MAP && w->stickX == 1 && seqGetSeq() == SEQ_BATTLE);
        }
        if (!met) {
            if (++sWaitFrames > w->waitMax) {
                printf("[input] FAIL: %s not reached in %d frames (sequence %d, map '%s')\n", w->text,
                       (int)w->waitMax, (int)seqGetSeq(), gp != NULL ? gp->currentMapName : "");
                if (_battleWorkPointer != NULL && seqGetSeq() == SEQ_BATTLE) {
                    const BattleWork* bw = _battleWorkPointer;
                    printf("[input]   battle: menu state %d, menu shown %d, acting unit %d, turn %d\n",
                           (int)bw->commandMenu.state, (bw->dispFlags & BATTLE_DISP_FLAG_COMMAND_MENU) != 0,
                           (int)bw->activeUnitId, (int)bw->turnCount);
                }
                fflush(stdout);
                _Exit(5);
            }
            memset(&pad[0], 0, sizeof(pad[0]));
            pad[0].err = PAD_ERR_NONE;
            if (w->frames == STEP_APPROACH) {
                if (seqGetSeq() == SEQ_GAME && marioChkKey()) {
                    approachEnemy(pad, w->name, sWaitFrames);
                }
                return;
            }
            /* press_until_seq: a 2-frame press every 30. press_until_control only while Mario is held:
             * pressed with him free, A talked to the one he stood by again, for ever */
            if (sWaitFrames % 30 < 2 &&
                !(w->frames == STEP_PRESS_UNTIL_CONTROL && (sControlFrames > 0 ||
                                      (seqGetSeq() == SEQ_GAME && marioChkKey() && marioChkCtrl())))) {
                pad[0].button = w->buttons;
                pad[0].stickX = w->stickX;
                pad[0].stickY = w->stickY;
            }
            /* press_until_ring with the battle won on a level-up: its prompt waits for the stick (as in act) */
            if (w->frames == STEP_PRESS_UNTIL_RING && sWaitFrames % 30 == 15 && seqGetSeq() == SEQ_BATTLE && _battleWorkPointer != NULL &&
                BattleGetSeq(_battleWorkPointer, SEQ_END) == 0x7000011) {
                pad[0].stickX = 127;
            }
            return;
        }
        printf("[input] frame %u: reached after %d frames\n", (unsigned)sFrame, (int)sWaitFrames);
        fflush(stdout);
        sWaitFrames = -1;
    }
    while (sLeft <= 0) {
        if (sCur >= 0 && sSteps[sCur].frames == STEP_END) {
            /* end: wait for a pending capture, then leave */
            if (!portShotPending()) {
                printf("[input] end\n");
                fflush(stdout);
                _Exit(0);
            }
            return;
        }
        if (++sCur >= sCount) {
            sState = 3;
            return;
        }
        s = &sSteps[sCur];
        printf("[input] frame %u: %s\n", (unsigned)sFrame, s->text);
        fflush(stdout);
        if (s->frames == STEP_SHOT) {
            /* with the map warp (map_sweep --input), named <map>__<NAME> */
            const char* map = getenv("TTYD_MAP");
            char name[128];
            if (map != NULL && *map) {
                /* the map's name without the entrance ("eki_02,s_bero") */
                snprintf(name, sizeof(name), "%.*s__%s", (int)strcspn(map, ","), map, s->name);
            } else {
                snprintf(name, sizeof(name), "%s", s->name);
            }
            portShotRequest(name);
            continue;
        }
        if (s->frames == STEP_SHOT_EVERY) {
            sShotEvery = s->waitMax;
            snprintf(sShotEveryName, sizeof(sShotEveryName), "%s", s->name);
            continue;
        }
        if (s->frames == STEP_LABEL) {
            continue;
        }
        if (s->frames == STEP_JUMP_IF_FIELD || s->frames == STEP_JUMP_IF_BATTLE || s->frames == STEP_JUMP_IF_MAP ||
            s->frames == STEP_JUMP_IF_HP || s->frames == STEP_JUMP_IF_PARTNER) {
            char map[32] = "", target[64];
            BOOL jump;
            snprintf(target, sizeof(target), "%s", s->name);
            if (s->frames == STEP_JUMP_IF_MAP || s->frames == STEP_JUMP_IF_HP) {
                sscanf(s->name, "%31s %63s", map, target);
            }
            if (s->frames == STEP_JUMP_IF_PARTNER) {
                /* jump_if_partner: the ring up is the partner's */
                BattleWork* bw = _battleWorkPointer;
                BattleUnit* u = seqGetSeq() == SEQ_BATTLE && bw != NULL ? BattleGetUnitPtr(bw, bw->activeUnitId) : NULL;
                jump = u != NULL && u->currentKind != UNIT_MARIO;
                printf("[input] jump_if_partner: %s\n", jump ? "the partner's turn: on" : "no");
                fflush(stdout);
            } else if (s->frames == STEP_JUMP_IF_HP) {
                /* in a battle his unit's HP (the pouch's is written back at its end) */
                BattleUnit* u = seqGetSeq() == SEQ_BATTLE && _battleWorkPointer != NULL
                                    ? BattleGetMarioPtr(_battleWorkPointer)
                                    : NULL;
                const int hp = u != NULL ? u->currentHp : pouchGetPtr()->currentHP;
                jump = hp < atoi(map);
                printf("[input] jump_if_hp: HP %d, %s\n", hp, jump ? "below: on" : "no");
                fflush(stdout);
            } else {
                jump = s->frames == STEP_JUMP_IF_MAP ? gp != NULL && strcmp(gp->currentMapName, map) == 0
                                        : (seqGetSeq() == SEQ_BATTLE) == (s->frames == STEP_JUMP_IF_BATTLE);
            }
            if (jump) {
                s32 i;
                for (i = 0; i < sCount; i++) {
                    if (sSteps[i].frames == STEP_LABEL && strcmp(sSteps[i].name, target) == 0) {
                        sCur = i;
                        break;
                    }
                }
                if (i == sCount) {
                    printf("[input] FAIL: no label %s\n", s->name);
                    fflush(stdout);
                    _Exit(5);
                }
            }
            continue;
        }
        if (s->frames == STEP_BATTLE) {
            /* battle [N]; N may also come from $TTYD_BATTLE_SETUP (map_sweep --battles) */
            const char* n = s->name[0] ? s->name : getenv("TTYD_BATTLE_SETUP");
            startBattle(n != NULL && *n ? atoi(n) : -1);
            continue;
        }
        if (s->frames == STEP_SAVE) {
            printf("[input] save: slot %d, map '%s', story %d\n", (int)gp->saveSlot, gp->currentMapName,
                   (int)swByteGet(0));
            fflush(stdout);
            cardWrite(gp->saveSlot);
        }
        if (s->frames == STEP_CHECKPOINT_ROOMS) {
            sRoomsOn = s->waitMax && getenv("TTYD_CHECKPOINT_DIR") != NULL;
            continue;
        }
        if (isWaitStep(s->frames)) {
            sWaitFrames = 0;
            sActPicked = 0;
            sActFallback = 0;
            sActEmptyType = -1;
            sActWait = 0;
            memset(&pad[0], 0, sizeof(pad[0]));
            pad[0].err = PAD_ERR_NONE;
            return;
        }
        if (s->frames == STEP_GOTO) {
            Vec pos;
            if (sscanf(s->name, "%f %f %f", &pos.x, &pos.y, &pos.z) != 3) {
                char hit[64] = "";
                float in = 0.0f;
                sscanf(s->name, "%63s %f", hit, &in);
                hitObjGetPos(hit, &pos);
                if (in > 0.0f) { /* goto HIT N: N units short of it, towards the map's middle */
                    const float len = sqrtf(pos.x * pos.x + pos.z * pos.z);
                    if (len > in) {
                        pos.x -= pos.x / len * in;
                        pos.z -= pos.z / len * in;
                    }
                }
            }
            marioGetPtr()->position = pos;
            continue;
        }
        if (s->frames == STEP_PAUSE) {
            pauseFor(atoi(s->name));
            continue;
        }
        if (s->frames == STEP_WHERE) { /* where: Mario's position and the map to the log */
            const MarioWork* m = marioGetPtr();
            const PouchData* pouch = pouchGetPtr();
            printf("[input] where: %s %.0f %.0f %.0f (motion %d, system level %d, field battle mode %d, key %d, "
                   "control %d); level %d, HP %d/%d, FP %d/%d, BP %d, SP %d, coins %d\n",
                   gp != NULL ? gp->currentMapName : "", m->position.x, m->position.y, m->position.z,
                   (int)m->currMotionId, (int)marioStGetSystemLevel(),
                   gp != NULL && gp->fbatData != NULL ? (int)gp->fbatData->mode : -1, (int)marioChkKey(),
                   (int)marioChkCtrl(), (int)pouch->level, (int)pouch->currentHP, (int)pouch->maximumHP,
                   (int)pouch->currentFP, (int)pouch->maximumFP, (int)pouch->maximumBP, (int)pouch->starPoints,
                   (int)pouch->coins);
            {
                int k;
                printf("[input]   star pieces %d; items", (int)pouch->starPieceCount);
                for (k = 0; k < MAX_HELD_ITEMS && pouch->heldItems[k] != 0; k++) {
                    printf(" %d", (int)pouch->heldItems[k]);
                }
                printf("; badges");
                for (k = 0; k < MAX_BADGES && pouch->badges[k] != 0; k++) {
                    printf(" %d", (int)pouch->badges[k]);
                }
                printf("; worn");
                for (k = 0; k < MAX_BADGES && pouch->equippedBadges[k] != 0; k++) {
                    printf(" %d", (int)pouch->equippedBadges[k]);
                }
                printf("\n");
            }
            if (_battleWorkPointer != NULL && seqGetSeq() == SEQ_BATTLE) {
                /* in a battle the pouch's HP is the one before it: the units' own */
                BattleWork* bw = _battleWorkPointer;
                BattleUnit* u;
                int k;
                printf("[input]   battle turn %d:", (int)bw->turnCount);
                for (k = 0; k < BATTLE_UNIT_MAX; k++) {
                    if ((u = bw->units[k]) != NULL && u->currentKind != 0) {
                        printf(" unit %d (kind %d) %d/%d;", k, (int)u->currentKind, (int)u->currentHp, (int)u->maxHp);
                    }
                }
                printf("\n");
            }
            if (strcmp(s->name, "hits") == 0) { /* where hits: the room's collision triangles to the log's folder, to plan a walk */
                extern HitEntry* hitActiveListTop;
                char path[512];
                const char* dir = getenv("TTYD_SHOT_DIR");
                FILE* out;
                snprintf(path, sizeof(path), "%s/hits_%s.txt", dir != NULL ? dir : ".", gp->currentMapName);
                if ((out = fopen(path, "w")) != NULL) {
                    const HitEntry* h;
                    for (h = hitActiveListTop; h != NULL; h = h->nextActive) {
                        int k;
                        for (k = 0; k < h->vectorCount; k++) {
                            const HitVector* v = &h->vectors[k];
                            fprintf(out, "%s %x %x %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.2f %.2f %.2f\n", hitGetName((HitEntry*)h),
                                    (unsigned)h->flags, (unsigned)h->attributes, v->v0.x, v->v0.y, v->v0.z, v->v1.x, v->v1.y,
                                    v->v1.z, v->v2.x, v->v2.y, v->v2.z, v->normal.x, v->normal.y, v->normal.z);
                        }
                    }
                    fclose(out);
                }
            }
            {
                /* what a press of A would act on */
                const MarioWork* f = marioGetPtr();
                printf("[input]   front %s, talk %s, dir %.0f\n", f->hitobjFront != NULL ? hitGetName(f->hitobjFront) : "-",
                       fbatNpcTalkCheck() != NULL ? "yes" : "no", f->moveDirection);
            }
            if (strncmp(s->name, "hit:", 4) == 0) { /* where hit:NAME: a hit object's position (a door's), to walk to */
                Vec hitPos = {0.0f, 0.0f, 0.0f};
                hitObjGetPos(s->name + 4, &hitPos);
                printf("[input]   hit %s at %.0f %.0f %.0f\n", s->name + 4, hitPos.x, hitPos.y, hitPos.z);
            }
            if (strcmp(s->name, "npcs") == 0) {
                listNpcs();
            }
            fflush(stdout);
            continue;
        }
        if (s->frames == STEP_HP) {
            pouchSetHP((s16)atoi(s->name)); /* e.g. 1, so the next hit ends in a Game Over */
            continue;
        }
        if (s->frames == STEP_SET) {
            const s32 v = s->waitMax;
            if (strcmp(s->name, "give") == 0) {
                pouchGetItem((ItemType)v);
            } else if (strcmp(s->name, "wear") == 0) {
                /* the badge given if he hasn't it, put on (BP not checked), its effect applied */
                if (!pouchEquipBadgeID(v)) {
                    pouchGetItem((ItemType)v);
                    pouchEquipBadgeID(v);
                }
                pouchReviseMarioParam();
                pouchRevisePartyParam();
            } else if (strcmp(s->name, "join") == 0) {
                partyJoin((PartyId)v);
            } else if (strcmp(s->name, "party") == 0) {
                partyJoin((PartyId)v);
                marioPartyKill();
                marioPartyEntry((PartyId)v);
            } else if (strcmp(s->name, "story") == 0) {
                swByteSet(0, v);
            } else if (strcmp(s->name, "flag") == 0) {
                swSet(v);
            } else if (strcmp(s->name, "audience") == 0) {
                pouchSetAudienceNum((f32)v);
            } else if (strcmp(s->name, "coins") == 0) {
                pouchSetCoin(v);
            } else if (strcmp(s->name, "stp") == 0) {
                pouchGetPtr()->starPoints = (s16)v;
            } else if (strcmp(s->name, "sp") == 0) {
                pouchSetAP((s16)v);
            } else if (strcmp(s->name, "fp") == 0) {
                pouchSetFP((s16)v);
            } else if (strcmp(s->name, "maxsp") == 0) {
                pouchGetPtr()->maximumSP = (s16)v;
            } else if (strcmp(s->name, "maxhp") == 0) {
                pouchSetMaxHP((s16)v);
                pouchSetHP((s16)v);
            } else if (strcmp(s->name, "stars") == 0) {
                pouchGetPtr()->starPowersObtained = (u16)v;
            } else if (strcmp(s->name, "swap") == 0) {
                portQuickSwapPress();
            } else if (strcmp(s->name, "travel") == 0) {
                portFastTravelRequest(v);
            }
            continue;
        }
        if (s->frames == STEP_TALK_ALL || s->frames == STEP_MOBJ_ALL) {
            sSweepKind = s->frames == STEP_TALK_ALL ? 1 : 2;
            sSweepIndex = 0;
            sSweepEvt = -1;
            sSweepDone = 0;
            sSweepMax = s->waitMax;
            sSweepNpc = NULL;
            snprintf(sSweepMap, sizeof(sSweepMap), "%s", gp != NULL ? gp->currentMapName : "");
            return; /* runs from the next frame */
        }
        if (s->frames == STEP_TAP) {
            float nx = 0.5f, ny = 0.5f;
            sscanf(s->name, "%f %f", &nx, &ny);
            portMenuTap(nx, ny); /* as the touch overlay does with a list up */
            continue;
        }
        if (s->frames == STEP_HOVER) {
            float nx = 0.5f, ny = 0.5f;
            sscanf(s->name, "%f %f", &nx, &ny);
            portMenuHover(nx, ny); /* as the mouse moving there */
            continue;
        }
        if (s->frames == STEP_BACK) {
            portMenuBack(); /* as a right click */
            continue;
        }
        if (s->frames == STEP_WHEEL) {
            portMenuWheel(atoi(s->name)); /* as the wheel's notches, up > 0 */
            continue;
        }
        if (s->frames == STEP_MOBJ_EVENT) {
            MapObjectEntry* mobj = mobjNameToPtrNoAssert(s->name);
            if (mobj == NULL || mobj->event == 0) {
                printf("[input] mobj_event: no map object '%s' with an event\n", s->name);
            } else {
                mobjRunEvent(mobj, (EvtWord*)mobj->event);
            }
            fflush(stdout);
            continue;
        }
        if (s->frames == STEP_SELECT) {
            EventEntry* e = evtEntry(input_select_evt, 0, 0);
            e->lwData[0] = s->waitMax < 0 ? (EvtWord)sSelectList : (EvtWord)s->waitMax;
            continue;
        }
        if (s->frames == STEP_STATE) {
            portSaveStateRequest(s->waitMax, s->stickX);
            continue;
        }
        if (s->frames == STEP_END) {
            return;
        }
        sLeft = s->frames;
        if (s->seed != 0) {
            sRand = s->seed;
            sRandHold = 0;
        }
    }
    s = &sSteps[sCur];
    if (s->seed != 0) {
        randomInput(&pad[0]);
        sLeft--;
        return;
    }
    pad[0].err = PAD_ERR_NONE;
    pad[0].button = s->buttons;
    pad[0].stickX = s->stickX;
    pad[0].stickY = s->stickY;
    pad[0].substickX = s->substickX;
    pad[0].substickY = s->substickY;
    pad[0].triggerLeft = s->trigL;
    pad[0].triggerRight = s->trigR;
    pad[0].analogA = (s->buttons & PAD_BUTTON_A) ? 255 : 0;
    pad[0].analogB = (s->buttons & PAD_BUTTON_B) ? 255 : 0;
    sLeft--;
}

s32 battleAcMain_StickKeepLeft(BattleWork* work); /* src/battle/ac/ac_stick_keep_left.c */
s32 battleAcMain_GaugeTiming(BattleWork* work);   /* src/battle/ac/ac_gauge_timing.c */

/* $TTYD_AC_PLAY=1 (tests that play battles through): the stick-held commands
 * played as a player would, the stick held left from the command's start and
 * let go on its mark. The hammer's (ac_stick_keep_left.c): let go once the
 * last, big light is on (its work's light count, SklWork.idx at 4, reaches
 * params[0] + 1). A gauge held on the stick (ac_gauge_timing.c, type 2:
 * Koops's Shell Toss, Blooey's Torpedo and Toss): let go once the marker is
 * in the zone (GaugeWork.inRange at 0x10), on its second pass, not at the
 * start where it sits in the middle. */
static void playStickCommands(PADStatus* pad) {
    static int sHeld;
    BattleWork* bw = _battleWorkPointer;
    BattleActions* ac;
    int hold = 0;
    if (bw == NULL || seqGetSeq() != SEQ_BATTLE) {
        sHeld = 0;
        return;
    }
    ac = &bw->actionCommands;
    if (ac->acState >= 100 && ac->acState <= 1001) {
        if (ac->maincb == battleAcMain_StickKeepLeft) {
            s32 idx;
            memcpy(&idx, &ac->work[4], sizeof(idx));
            hold = idx < ac->params[0] + 1;
        } else if (ac->maincb == battleAcMain_GaugeTiming && ac->params[0] == 2) {
            s32 inRange;
            memcpy(&inRange, &ac->work[0x10], sizeof(inRange));
            hold = !(inRange && sHeld > 30);
        }
    }
    if (hold) {
        if (sHeld++ == 0) {
            printf("[acplay] frame %u: holding the stick left\n", (unsigned)sFrame);
            fflush(stdout);
        }
        pad[0].err = PAD_ERR_NONE;
        pad[0].stickX = -127;
    } else if (sHeld > 0) {
        printf("[acplay] frame %u: let go after %d frames\n", (unsigned)sFrame, sHeld);
        fflush(stdout);
        sHeld = 0;
        pad[0].stickX = 0;
    }
}

/* $TTYD_AC_CUE_PRESS=1 (tests of the Action Command cue): A pressed for two
 * frames each time the cue comes on (port/src/common/ac_cue_pc.c);
 * $TTYD_AC_PLAY=1 also plays the stick-held commands (above) */
void portInputApply(PADStatus* pad) {
    static int sPrevCue, sHold;
    static int sCuePress = -1, sPlay = -1;
    applyScript(pad);
    portMenuApplyPad(pad); /* the mouse's B and wheel in menus */
    if (sCuePress < 0) {
        sPlay = getenv("TTYD_AC_PLAY") != NULL;
        sCuePress = sPlay || getenv("TTYD_AC_CUE_PRESS") != NULL;
    }
    if (sPlay) {
        playStickCommands(pad);
    }
    if (!sCuePress) {
        return;
    }
    /* as a player following the cue: at its ideal moment */
    if (gPortAcCueState != 0 && gPortAcCueLead <= 0 && sPrevCue == 0) {
        printf("[accue] frame %u: cue %d at its mark, pressing A\n", (unsigned)sFrame, gPortAcCueState);
        fflush(stdout);
        sHold = 2;
    }
    sPrevCue = gPortAcCueState != 0 && gPortAcCueLead <= 0;
    if (sHold > 0) {
        sHold--;
        pad[0].err = PAD_ERR_NONE;
        if (gPortAcCueState == 2 && getenv("TTYD_SL6_SGCUE") != NULL) {
            pad[0].button |= PAD_BUTTON_B; /* a Superguard's press */
        } else {
            pad[0].button |= PAD_BUTTON_A;
            pad[0].analogA = 255;
        }
    }
}
