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
 *   stick X Y N [KEYS]  main stick at X, Y (-128..127) for N frames, KEYS held too
 *   shot NAME           capture this frame as NAME.png (Aurora, $TTYD_SHOT_DIR)
 *   setting KEY VALUE   set a declared setting (portSettingsFind) for the rest of the run, not
 *                       saved: one run captures a look off, then on ("[input] setting KEY = V")
 *   battle              start a battle with the nearest enemy NPC, as a map
 *                       script would (evt_npc_battle_start); none on the map
 *                       ends the test (exit 6)
 *   hp N                set Mario's HP (pouchSetHP), e.g. 1 to reach a Game Over
 *   walk_to HIT [MAX]   the stick towards hit object HIT (an exit's zone) until
 *                       the map changes, jumping when stuck; at most MAX frames
 *   walk_to X Z [MAX]   the same towards point X, Z of the map, until Mario is
 *                       there, the map changes or a scene takes him over (a
 *                       trigger crossed on the way)
 *   walk_to HIT stop [MAX]  the same towards hit object HIT, stopping there
 *   walk_to npc NAME|N [MAX]  the same after an NPC (its name in the map's scripts,
 *                       UTF-8, or its index in the NPC table) wherever it goes
 *   walk_to item NAME [MAX]  the same into a field item (`where items`' name) until it is
 *                       picked up (gone; one not there counts as picked up)
 *   fight [MAX]         the same into the nearest enemy NPC (as `battle` picks it) until the
 *                       battle its touch starts is over and it is beaten ("[input] frame F:
 *                       fight enemy: beaten after N frames"): a field battle, its defeat
 *                       script run (unlike `battle`); no enemy ends the test (exit 6)
 *   wait_seq N [MAX]    no input until the game sequence is N (seqdrv.h:
 *                       1 title, 2 field, 4 battle, 6 file select), at most
 *                       MAX frames (default 3600), else exit 5
 *   wait_map NAME [MAX] no input until NAME is the current map, as wait_seq
 *   wait_y Y [MAX]      no input until Mario is at height Y or above (riding a lift)
 *   wait_vfx NAME [MAX] no input until an effect named NAME (its type: GonbabaBreath, Nokotarou)
 *                       was drawn with something showing (port/include/port/vfx_trace.h), as wait_seq
 *                       (default MAX 600): a capture of the effect right after it, whenever it came
 *   wait_bindings [MAX] no input until no key or button is being asked for (Controls > Bindings, the
 *                       Start menu's Bindings page: a test's own answer, $TTYD_REBIND_TEST), as wait_seq
 *   wait_peer WHAT [MAX] online tests: no input until another game in the session shows WHAT: "card" (a
 *                       player's card open, its player message's), "paused" (the host's world held by a
 *                       Start menu), "here" (linked), "battle", "field", "room" (in this one's room),
 *                       "coop" (a co-op guest), "free" (in the field, keys on), "busy" (a player not
 *                       free: title, loading, battle, scene, menu), "map=NAME"; a count after it ("here3") asks that many
 *                       players; "!card" / "!paused" ... none shows it; as wait_seq. Two games' scripts
 *                       meet so, however far apart they started (launches are staggered under load)
 *   wait_hit HIT x|y|z >=|<= V  no input until hit object HIT's centre is there (a
 *                       moving platform come alongside), at most 3600 frames
 *   press_until_seq KEYS N [MAX]  press KEYS every 30 frames until sequence N
 *   press_until_control KEYS [MAX]  the same until Mario has had control a second; the keys
 *                       are pressed only while he has none (next to an NPC, A would talk again)
 *   press_until_map KEYS NAME [MAX]  the same, until NAME is the current map
 *                       (screens that ignore input during their intro)
 *   give ID             the item, key item or badge ID (pouchGetItem)
 *   join N / party N    partner N joins / joins and comes out (1 Goombella..7)
 *   story N / flag N    the story (GSW 0) set to N / switch GSWF N set
 *   audience N, coins N, sp N, fp N   those set (sp: star power, in 1/100 units)
 *   maxsp N, maxhp N, maxfp N   the maximums set (maxhp and maxfp also fill them)
 *   gsw N V             saved byte GSW N set to V (story values, area progress)
 *   level N             Mario's level set to N
 *   stars MASK          the special moves obtained (bit 0: Sweet Treat ...)
 *   swap                the next partner out, in the partner menu's order (the quick-swap control,
 *                       on whether or not Enhancements has it on); `party N` for a given one
 *   companion item N [T] / companion partner N
 *                       a tap on the second screen (port/include/port/companion.h):
 *                       the bag's item N used on Mario (T 1: the partner), or
 *                       a switch to partner N
 *   companion tap NX NY / companion button a|b|x|y|z|up|down|left|right|l|r / companion shot NAME
 *                       a tap at NX, NY (0-1) on the second screen's Start menu,
 *                       or one of its buttons (port/include/port/companion_menu.h)
 *   travel N            Enhancements fast travel to hub N (0 Rogueport, 1 Petalburg ...)
 *   mobj_event NAME     run map object NAME's event, as hitting it would (a
 *                       switch's, a block's)
 *   talk NAME|N [MAX]   talk to that NPC as A in front of it does (only where the map blocks the
 *                       real way: across a counter); A and B as talk_all presses them
 *   talk_all [MAX]      talk to every NPC of the map in turn (not those parked
 *                       below the map), as A in front of one does (its talk script), A pressed every 20 frames
 *                       and B now and then to get through; each one at most
 *                       MAX frames (default 900); stops if the map changes
 *   mobj_all [MAX]      the same with every map object's event (switches,
 *                       blocks, chests)
 *   tattle_all [MAX]    Goombella's field Tattle (her own script) of every NPC talk_all
 *                       would talk to, then of the map; she must be out
 *   tap NX NY           a tap at NX, NY (0-1 across the picture) on a selection
 *                       list's rows (port/src/common/menu_tap_pc.c)
 *   hover NX NY         the mouse pointer moved to NX, NY (menus)
 *   back / wheel N      a right click / N notches of the wheel (up > 0), with a
 *                       menu up
 *   shadow STEP         STEP in $TTYD_INPUT_SHADOW games only (online tests: the
 *                       other players' own tap, hover, back or wheel)
 *   select N|list       open selection window type N (0-18; key items, items,
 *                       badges, partners, ...), or a window of a list of items
 *                       as a locked door's script opens; press keys to close it
 *   random N [SEED]     N frames of made-up play (walking, jumps, the hammer,
 *                       partner abilities, the menus and back out; the same
 *                       for the same SEED, default 1), to shake out crashes
 *                       and stalls (tools/port/monkey.py; randAction below)
 *   state save|load N   save state slot N (1-4) saved or loaded at the end of
 *                       this frame (Aurora, port/src/aurora/savestate_aurora.c)
 *   where [npcs|hits|mobjs]  print the map, Mario's place, the story, the sequence, control;
 *                       npcs: each NPC of the map too (index, name, x y z); hits: each hit
 *                       object (centre, box; exits and cases first, marked *; `where hits NAME`: those
 *                       named with NAME in it, with their triangles); mobjs: each map object;
 *                       items: each item lying in the field (name, item, mode, place)
 *   where flag|gsw NAME|N ...  saved flags (GSWF) or bytes (GSW) by name (evt_story.h) or number
 *                       (a long name: one a line)
 *   where have N ...    how many of item N (mario_pouch.h's ItemType) the pouch holds (pouchCheckItem)
 *   where floor [STEP [BELOW]]  a top-down text map of the floor's height, STEP units a cell (the
 *                       highest floor under height BELOW: one storey of a tower)
 *   where words         every saved flag set and saved byte not 0 (GSWF, LSWF, GSW, LSW)
 *   where windows       the game's windows still open (message, select, item lists): how many, then
 *                       each one's slot, type, state and alpha
 *   dev STEP            a device flow's step (port/src/common/flows_pc.c): a virtual controller plugged in
 *                       or pressed, a keyboard key, a touch control, a wait or an expectation, with
 *                       controller 1 left to those devices (never written by the script) meanwhile
 *   pad N KEYS [FRAMES] controller N (2-4) plugged in (from now on), KEYS held on it FRAMES (default 2;
 *                       KEYS "none": only plugged in); the script goes on at once (co-op players' presses)
 *   unplug N [FRAMES] / plug N   controller N pulled out (for FRAMES, else until plug N) / back in
 *   online leave / online join CODE|ADDRESS   as the Start menu's Leave the room / Join (online tests)
 *   together PICK / together seat S W   the Multiplayer page's pick (versus, raid, bossrush, quiz, cards,
 *                       bingo, dm2-dm4, together) / Who's who swap of seats S and W, as its button (Aurora)
 *   key NAME [FRAMES]   a keyboard key (SDL's name: Escape, Return...) held FRAMES (default 2), as the
 *                       window's own key event (Aurora: the pages' and menus' keys)
 *   press_until_pick KEYS N [MAX]  KEYS every 30 frames until the room's pick is N (1 the adventure,
 *                       10-16 a party game; port/together.h portTogetherPicked), as press_until_seq
 *   where coop [MAX]    an "[mp] where:" line: every player's character, menus, control, and "settled"
 *                       or "NOT SETTLED: why" (someone mid-move; port/players.h's portCoopWhere), once
 *                       everyone is settled or MAX frames on (default 0: now); never fails by itself
 *   end                 exit (0) once any capture is written
 * Each command is logged as "[input] frame F: <line>". */
#include "port/file_pc.h"
#include "port/ac_cue.h"
#include "port/bindings.h"
#include "port/brothers.h"
#include "port/companion.h"
#include "port/companion_menu.h"
#include "port/gameplus.h"
#include "port/menu_tap.h"
#include "port/practice.h"
#include "port/practice_more.h"
#include "port/practice_tools.h"
#include "port/practice_jump.h"
#include "port/postgame_extra.h"
#include "port/settings.h"
#include "port/statesync.h"
#include "port/players.h"
#include "port/story_test.h"
#include "port/together.h"
#ifdef PORT_TOUCH_OVERLAY
#include "port/touch_overlay.h"
#endif
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/camdrv.h" /* (before windows.h: its 'near' macro) */
#include "driver/windowdrv.h" /* where windows */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif
#include <dolphin/os.h>
#include "port/platform.h"
#include "data/mapdata.h"
#include "driver/casedrv.h"
#include "driver/hitdrv.h"
#include "driver/mapdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_story.h"
#include "manager/cardmgr.h"
#include "driver/npcdrv.h"
#include "evt/evt_bero.h"
#include "evt/evt_cmd.h"
#include "evt/evt_npc.h"
#include "manager/evtmgr.h"
#include "mario/mario.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "mario/mario_party.h"
#include "driver/mobjdrv.h"
#include "driver/itemdrv.h"
ItemWork* portItemGetWork(void);
#include "party.h"
#include "pmario_sound.h"
EventEntry* mobjRunEvent(MapObjectEntry* entry, EvtWord* eventCode); /* mobjdrv.c */
#include "driver/seqdrv.h"
#include "port/vfx_trace.h"

#define MAX_LINES 4096

/* the device flows' steps (flows_pc.c) */
int portFlowsStep(const char* args);
int portFlowsStepDone(void);
void portFlowsFrame(void);

typedef struct Step {
    char text[96];
    u16 buttons;
    s8 stickX, stickY, substickX, substickY;
    u8 trigL, trigR;
    s32 frames; /* -1: shot, -2: end, -3: battle, -4: hp, -5: wait_seq, -6: wait_map, ... (load) */
    char name[160]; /* (where gsw|flag|have: its whole list) */
    s32 waitMax; /* wait_seq / wait_map */
    u32 seed;    /* random: its generator's seed (0: none, plain input) */
} Step;

/* the script's own controllers 2-4 (pad N KEYS, unplug N, plug N): each plugged in from its first "pad" (or
 * there all along with its $TTYD_PN_PRESS), its keys held for so many frames, pulled out for so many (-1: until
 * "plug") */
static struct {
    int plugged;
    int pulled;
    u16 buttons;
    s8 stickX, stickY, substickX, substickY;
    u8 trigL, trigR;
    int left;
} sScriptPad[4];
static int sCoopWherePending; /* where coop: printed at the end of this frame's input (portInputApply) */
static int sCoopSettledSeen;  /* where coop MAX: everyone was settled at the end of the last frame's input */
#ifdef TTYD_BACKEND_AURORA
static char sKeyName[32]; /* key NAME FRAMES: the key held, and the frames until it is let go */
static int sKeyLeft;
static int sKeyFresh; /* (the frame it went down: the frames held count from the next, so FRAMES 1 is one read) */
#endif

static void scriptPadStep(const Step* s) {
    const int c = s->waitMax; /* 1-3: controller 2-4 */
    if (c < 1 || c > 3) {
        return;
    }
    if (s->frames == -32) {
        sScriptPad[c].plugged = 1;
        sScriptPad[c].buttons = s->buttons;
        sScriptPad[c].stickX = s->stickX;
        sScriptPad[c].stickY = s->stickY;
        sScriptPad[c].substickX = s->substickX;
        sScriptPad[c].substickY = s->substickY;
        sScriptPad[c].trigL = s->trigL;
        sScriptPad[c].trigR = s->trigR;
        sScriptPad[c].left = (int)s->seed;
    } else if (s->frames == -33) {
        sScriptPad[c].pulled = s->seed > 0 ? (int)s->seed : -1;
        sScriptPad[c].left = 0;
    } else {
        sScriptPad[c].pulled = 0;
        sScriptPad[c].plugged = 1;
    }
}

/* random N [SEED]: N frames of made-up play, the same for the same seed.
 * Play is a run of "actions" drawn from one generator, weighted towards what a
 * player does: walking and running, jumps (A, held for height), checks and
 * talks (A taps), the hammer (B), the partner's ability (X, held), Mario's
 * paper modes (Y), spins and charges (Z, L, R), the camera (C-stick), and the
 * menus: Start or a D-pad direction (the item, partner-swap, badge and Mario
 * menus), a few moves and choices there, then B until they're closed. Never
 * the GameCube's reset combination (START + B + X): an action holds one
 * button at most. $TTYD_RANDOM_LOG=1 logs each action ("[random] #I frame F:
 * ..."), and $TTYD_RANDOM_SKIP=I,J-K plays those actions as no input for as
 * long (the generator still draws them, so every other action stays as it
 * was): what tools/port/monkey.py shrinks a failing seed with. */
static u32 sRand;
static PADStatus sRandPad;
static u32 sFrame;

#define RAND_SEGS 48
typedef struct RandSeg {
    u16 button;
    s8 sx, sy, cx, cy;
    s32 frames;
} RandSeg;
static RandSeg sRandSeg[RAND_SEGS];
static s32 sRandSegCount, sRandSegCur, sRandSegLeft;
static s32 sRandAction;   /* actions drawn since the seed was set */
static BOOL sRandMuted;   /* $TTYD_RANDOM_SKIP has the action being played */
static int sRandLog = -1; /* $TTYD_RANDOM_LOG */

static u32 nextRand(void) {
    sRand = sRand * 1664525u + 1013904223u;
    return sRand >> 8;
}

static s32 randRange(s32 lo, s32 hi) { /* lo..hi inclusive */
    return lo + (s32)(nextRand() % (u32)(hi - lo + 1));
}

static void randSeg(u16 button, s32 sx, s32 sy, s32 frames) {
    if (sRandSegCount < RAND_SEGS && frames > 0) {
        RandSeg* g = &sRandSeg[sRandSegCount++];
        memset(g, 0, sizeof(*g));
        g->button = button;
        g->sx = (s8)sx;
        g->sy = (s8)sy;
        g->frames = frames;
    }
}

/* a stick direction: any angle, mostly at full tilt (a run), sometimes half (a walk) */
static void randStick(s32* sx, s32* sy) {
    const u32 a = nextRand() % 360;
    const f32 tilt = nextRand() % 4 == 0 ? 60.0f : 120.0f;
    *sx = (s32)(tilt * cosf(a * 0.017453292f));
    *sy = (s32)(tilt * sinf(a * 0.017453292f));
}

static BOOL randSkipped(s32 action) {
    const char* p = getenv("TTYD_RANDOM_SKIP");
    while (p != NULL && *p != '\0') {
        char* end;
        long lo = strtol(p, &end, 10), hi = lo;
        if (end == p) {
            break;
        }
        if (*end == '-') {
            hi = strtol(end + 1, &end, 10);
        }
        if (action >= lo && action <= hi) {
            return TRUE;
        }
        if (*end != ',') {
            break;
        }
        p = end + 1;
    }
    return FALSE;
}

/* the next action's segments */
static void randAction(void) {
    static const struct { const char* name; s32 weight; } kActions[] = {
        {"walk", 24}, {"jump", 11}, {"a", 12}, {"hammer", 8}, {"partner", 9}, {"paper", 2}, {"spin", 4},
        {"camera", 2}, {"idle", 4}, {"mash", 5}, {"start menu", 6}, {"quick menu", 7}, {"running jump", 6},
    };
    s32 total = 0, r, k, i, n, sx, sy;
    sRandSegCount = 0;
    sRandSegCur = 0;
    for (k = 0; k < (s32)(sizeof(kActions) / sizeof(kActions[0])); k++) {
        total += kActions[k].weight;
    }
    r = (s32)(nextRand() % (u32)total);
    for (k = 0; r >= kActions[k].weight; k++) {
        r -= kActions[k].weight;
    }
    switch (k) {
    case 0: /* walk or run somewhere */
        randStick(&sx, &sy);
        randSeg(0, sx, sy, randRange(15, 100));
        break;
    case 1: /* jumps on the spot or running, A held 2-20 frames for the height */
        randStick(&sx, &sy);
        if (nextRand() % 3 == 0) {
            sx = sy = 0;
        }
        n = randRange(1, 3);
        for (i = 0; i < n; i++) {
            randSeg(0, sx, sy, randRange(4, 20));
            randSeg(PAD_BUTTON_A, sx, sy, randRange(2, 20));
        }
        randSeg(0, sx, sy, randRange(10, 30));
        break;
    case 2: /* A taps: check, talk, read on */
        n = randRange(1, 4);
        for (i = 0; i < n; i++) {
            randSeg(PAD_BUTTON_A, 0, 0, 3);
            randSeg(0, 0, 0, randRange(4, 20));
        }
        break;
    case 3: /* the hammer, standing or on the move */
        randStick(&sx, &sy);
        if (nextRand() % 2 == 0) {
            sx = sy = 0;
        }
        randSeg(0, sx, sy, randRange(2, 10));
        randSeg(PAD_BUTTON_B, sx, sy, randRange(3, 8));
        randSeg(0, sx, sy, randRange(15, 40));
        break;
    case 4: /* the partner's ability: X, perhaps held (Koops' shell, Flurrie, Yoshi, Bobbery), moving */
        randStick(&sx, &sy);
        randSeg(PAD_BUTTON_X, 0, 0, randRange(3, 8));
        randSeg(nextRand() % 2 ? PAD_BUTTON_X : 0, sx, sy, randRange(10, 70));
        if (nextRand() % 3 == 0) {
            randSeg(PAD_BUTTON_A, sx, sy, 3); /* Yoshi's flutter, a jump off */
        }
        randSeg(0, sx, sy, randRange(10, 40));
        break;
    case 5: /* Mario's paper modes (Y, on their panels) */
        randSeg(PAD_BUTTON_Y, 0, 0, randRange(3, 40));
        randStick(&sx, &sy);
        randSeg(0, sx, sy, randRange(20, 60));
        break;
    case 6: /* Z, L or R held a while */
        n = (s32)(nextRand() % 3);
        randStick(&sx, &sy);
        if (nextRand() % 2 == 0) {
            sx = sy = 0;
        }
        randSeg(n == 0 ? PAD_TRIGGER_Z : n == 1 ? PAD_TRIGGER_L : PAD_TRIGGER_R, sx, sy, randRange(5, 60));
        randSeg(0, 0, 0, randRange(5, 20));
        break;
    case 7: /* the camera */
        randSeg(0, 0, 0, randRange(10, 40));
        sRandSeg[0].cx = (s8)(nextRand() % 2 ? 127 : -127);
        sRandSeg[0].cy = (s8)((s32)(nextRand() % 255) - 127);
        break;
    case 8: /* nothing pressed */
        randSeg(0, 0, 0, randRange(10, 60));
        break;
    case 9: /* A mashed (a cutscene, a message, an action command) */
        n = randRange(5, 20);
        for (i = 0; i < n; i++) {
            randSeg(PAD_BUTTON_A, 0, 0, 2);
            randSeg(0, 0, 0, randRange(2, 6));
        }
        break;
    case 10:   /* the Start menu: tabs and rows, perhaps a choice, then B out */
    case 11: { /* a D-pad menu: items, partner swap, badges, Mario */
        static const u16 kDpad[] = {PAD_BUTTON_UP, PAD_BUTTON_DOWN, PAD_BUTTON_LEFT, PAD_BUTTON_RIGHT};
        static const u16 kMove[] = {PAD_BUTTON_UP, PAD_BUTTON_DOWN, PAD_BUTTON_LEFT, PAD_BUTTON_RIGHT, PAD_BUTTON_UP,
                                    PAD_BUTTON_DOWN, PAD_TRIGGER_L, PAD_TRIGGER_R, PAD_TRIGGER_Z};
        randSeg(k == 10 ? PAD_BUTTON_START : kDpad[nextRand() % 4], 0, 0, 3);
        randSeg(0, 0, 0, randRange(20, 40));
        n = randRange(1, 8);
        for (i = 0; i < n; i++) {
            const u32 m = nextRand() % 10;
            if (m < 7) {
                randSeg(kMove[nextRand() % (sizeof(kMove) / sizeof(kMove[0]))], 0, 0, 3);
            } else if (m < 9) {
                randSeg(0, 0, nextRand() % 2 ? 100 : -100, randRange(3, 12)); /* the stick through a list */
            } else {
                randSeg(PAD_BUTTON_A, 0, 0, 3); /* a choice: an item used, a partner out, a badge on */
            }
            randSeg(0, 0, 0, randRange(5, 20));
        }
        n = randRange(3, 6);
        for (i = 0; i < n; i++) {
            randSeg(PAD_BUTTON_B, 0, 0, 3);
            randSeg(0, 0, 0, randRange(8, 16));
        }
        break;
    }
    default: /* a running jump over something, then on */
        randStick(&sx, &sy);
        randSeg(0, sx, sy, randRange(10, 30));
        randSeg(PAD_BUTTON_A, sx, sy, randRange(10, 20));
        randSeg(0, sx, sy, randRange(10, 40));
        break;
    }
    sRandMuted = randSkipped(sRandAction);
    if (sRandLog < 0) {
        const char* v = getenv("TTYD_RANDOM_LOG");
        sRandLog = v != NULL && v[0] != '0';
    }
    if (sRandLog) {
        s32 len = 0;
        for (i = 0; i < sRandSegCount; i++) {
            len += sRandSeg[i].frames;
        }
        printf("[random] #%d frame %u: %s, %d frames%s\n", (int)sRandAction, (unsigned)sFrame, kActions[k].name,
               (int)len, sRandMuted ? " (skipped)" : "");
        fflush(stdout);
    }
    sRandAction++;
    sRandSegLeft = sRandSeg[0].frames;
}

static void randomStart(u32 seed) {
    sRand = seed;
    sRandSegCount = sRandSegCur = sRandSegLeft = 0;
    sRandAction = 0;
}

static void randomInput(PADStatus* out) {
    const RandSeg* g;
    while (sRandSegCur >= sRandSegCount || sRandSegLeft <= 0) {
        if (sRandSegCur < sRandSegCount && ++sRandSegCur < sRandSegCount) {
            sRandSegLeft = sRandSeg[sRandSegCur].frames;
        } else {
            randAction();
        }
    }
    g = &sRandSeg[sRandSegCur];
    sRandSegLeft--;
    memset(&sRandPad, 0, sizeof(sRandPad));
    if (!sRandMuted) {
        sRandPad.button = g->button;
        sRandPad.stickX = g->sx;
        sRandPad.stickY = g->sy;
        sRandPad.substickX = g->cx;
        sRandPad.substickY = g->cy;
    }
    *out = sRandPad;
    out->err = PAD_ERR_NONE;
    out->triggerLeft = (sRandPad.button & PAD_TRIGGER_L) ? 255 : 0;
    out->triggerRight = (sRandPad.button & PAD_TRIGGER_R) ? 255 : 0;
    out->analogA = (sRandPad.button & PAD_BUTTON_A) ? 255 : 0;
    out->analogB = (sRandPad.button & PAD_BUTTON_B) ? 255 : 0;
}

static s32 sWaitFrames = -1; /* frames spent in the current wait_seq / wait_map, -1: none */
static s32 sWalkFrames = -1; /* walk_to: frames spent, -1: none */
static s32 sWalkStuck, sWalkDetour, sWalkTries, sWalkScene;
/* walk_to: a map change ran during the walk (a door into the same map, e.g. las_08's looping corridor) */
static BOOL sWalkReload;
/* walk_to: a battle ran during the walk (walk_to npc after an enemy: it is gone after it) */
static BOOL sWalkBattle;
static s32 sFightNpc = -1; /* fight: the enemy's index in the NPC table */
static Vec sWalkPrev;
static char sWalkMap[16];

/* talk_all / mobj_all: the item being run, -1 when none */
MapObjectWork* portMobjGetWork(void); /* mobjdrv.c */
EventEntry* mobjRunEvent(MapObjectEntry* entry, EvtWord* eventCode);
static s32 sSweepKind; /* 0 none, 1 talk, 2 mobj, 3 Goombella's Tattle */
extern EvtWord kurio_msg_evt[]; /* party_kuribo.c: her field Tattle */
static s32 sSweepIndex, sSweepEvt, sSweepFrames, sSweepMax, sSweepDone;
static s32 sSweepOne = -1; /* talk NAME: only that NPC (its index), -1: every one */
static NpcEntry* sSweepNpc;
static char sSweepMap[32];

static Step sSteps[MAX_LINES];
static s32 sCount, sCur = -1, sLeft;
static int sState = -1; /* -1 unread, 0 off, 1 loaded, 2 running, 3 ended */

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
            {"none", 0}, /* (press_until_story none N: a wait for the story, nothing pressed) */
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

/* port/src/aurora/rewind_aurora.c: the Rewind control held for frames (tests) */
__attribute__((weak)) void portRewindTest(int frames) {
    (void)frames;
}

/* One whole line of the script, however long (a long comment, a malformed
 * line): the first size-1 characters, the rest read past. A line cut by
 * fgets used to come back as more lines, each "cannot parse". */
static BOOL readLine(FILE* f, char* line, int size) {
    int len;
    if (fgets(line, size, f) == NULL) {
        return FALSE;
    }
    len = (int)strlen(line);
    if (len > 0 && line[len - 1] != '\n') {
        int c;
        while ((c = fgetc(f)) != EOF && c != '\n') {
        }
    }
    return TRUE;
}

/* A test script that cannot run: quit at once, as the other test exits do
 * (_Exit, no teardown). exit() ran the C++ static destructors while Aurora's
 * threads were still up, and the pipeline cache's writer thread, still
 * joinable, aborted the game (std::terminate) instead of exit code 2. */
__attribute__((noreturn)) static void loadFail(void) {
    fflush(stdout);
    fflush(stderr);
    portTestExit(2);
}

static void load(const char* path) {
    FILE* f = portFopen(path, "r");
    char line[512];
    int n = 0;
    if (f == NULL) {
        fprintf(stderr, "TTYD_INPUT: cannot open %s\n", path);
        loadFail();
    }
    while (sCount < MAX_LINES - 1 && readLine(f, line, sizeof(line))) {
        char cmd[32] = "", a1[64] = "", a2[64] = "", a3[16] = "", a4[32] = "";
        char* hash = strchr(line, '#');
        Step* s = &sSteps[sCount];
        n++;
        if (hash) *hash = '\0';
        if (strncmp(line, "shadow ", 7) == 0) {
            /* shadow STEP: the step in $TTYD_INPUT_SHADOW games only (online tests: the other
             * players' own mouse in a menu, tap, hover, back, wheel: no frames of their own, so the
             * script's timing is the same in every game) */
            static int sShadowLoad = -1;
            if (sShadowLoad < 0) {
                sShadowLoad = getenv("TTYD_INPUT_SHADOW") != NULL;
            }
            if (!sShadowLoad) {
                continue;
            }
            memmove(line, line + 7, strlen(line + 7) + 1);
        }
        if (sscanf(line, "%31s %63s %63s %15s %31s", cmd, a1, a2, a3, a4) < 1) {
            continue;
        }
        memset(s, 0, sizeof(*s));
        snprintf(s->text, sizeof(s->text), "%s %s %s %s %s", cmd, a1, a2, a3, a4);
        if (strcmp(cmd, "wait") == 0) {
            s->frames = atoi(a1);
        } else if (strcmp(cmd, "dev") == 0) {
            /* dev ...: a device flow's step (flows_pc.c), controller 1 left to the devices meanwhile */
            const char* rest = strstr(line, "dev") + 3;
            while (*rest == ' ' || *rest == '\t') {
                rest++;
            }
            s->frames = -40;
            snprintf(s->name, sizeof(s->name), "%.*s", (int)strcspn(rest, "\r\n"), rest);
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
            /* stick X Y N [KEYS]: KEYS' buttons held too (a running jump: stick 127 0 20 a) */
            if (a4[0] && !parseKeys(s, a4)) goto bad;
            s->stickX = (s8)atoi(a1);
            s->stickY = (s8)atoi(a2);
            s->frames = atoi(a3);
        } else if (strcmp(cmd, "shot") == 0) {
            s->frames = -1;
            snprintf(s->name, sizeof(s->name), "%s", a1);
        } else if (strcmp(cmd, "setting") == 0) {
            s->frames = -41;
            snprintf(s->name, sizeof(s->name), "%s %s", a1, a2);
        } else if (strcmp(cmd, "battle") == 0) {
            s->frames = -3;
            snprintf(s->name, sizeof(s->name), "%s", a1); /* optional: an entry of the area's table */
        } else if (strcmp(cmd, "wait_seq") == 0 || strcmp(cmd, "wait_map") == 0) {
            s->frames = cmd[5] == 's' ? -5 : -6;
            snprintf(s->name, sizeof(s->name), "%s", a1);
            s->waitMax = a2[0] ? atoi(a2) : 3600;
        } else if (strcmp(cmd, "press_until_seq") == 0 || strcmp(cmd, "press_until_map") == 0) {
            /* a wait_seq / wait_map that also presses KEYS every 30 frames */
            s->frames = strcmp(cmd, "press_until_seq") == 0 ? -5 : -6;
            snprintf(s->name, sizeof(s->name), "%s", a2);
            s->waitMax = a3[0] ? atoi(a3) : 3600;
            if (!parseKeys(s, a1)) goto bad;
        } else if (strcmp(cmd, "save") == 0) {
            /* save: the open file, here and now, as a Save Block does
             * (cardWrite); waits until the card is done. A checkpoint: a later
             * run resumes with load_file.txt */
            s->frames = -10;
            s->waitMax = a1[0] ? atoi(a1) : 1200;
        } else if (strcmp(cmd, "wait_hit") == 0) {
            /* wait_hit HIT x|y|z >=|<= V: no input until hit object HIT's centre is
             * there (a moving platform come alongside), at most 3600 frames */
            s->frames = -27;
            snprintf(s->name, sizeof(s->name), "%s %s %s %s", a1, a2, a3, a4);
            s->waitMax = 3600;
        } else if (strcmp(cmd, "wait_bindings") == 0) {
            s->frames = -31;
            s->waitMax = a1[0] ? atoi(a1) : 3600;
        } else if (strcmp(cmd, "wait_peer") == 0 && a1[0]) {
            /* wait_peer WHAT [MAX]: no input until another game shows WHAT (portStateSyncPeerShows) */
            s->frames = -29;
            snprintf(s->name, sizeof(s->name), "%s", a1);
            s->waitMax = a2[0] ? atoi(a2) : 3600;
        } else if (strcmp(cmd, "wait_y") == 0) {
            /* wait_y Y [MAX]: no input until Mario is at height Y or above (a lift) */
            s->frames = -26;
            snprintf(s->name, sizeof(s->name), "%s", a1);
            s->waitMax = a2[0] ? atoi(a2) : 3600;
        } else if (strcmp(cmd, "wait_vfx") == 0 && a1[0]) {
            /* wait_vfx NAME [MAX]: no input until effect NAME was drawn showing something */
            s->frames = -30;
            snprintf(s->name, sizeof(s->name), "%s", a1);
            s->waitMax = a2[0] ? atoi(a2) : 600;
        } else if (strcmp(cmd, "press_until_story") == 0) {
            /* press KEYS every 30 frames until the story (GSW 0) reaches N */
            s->frames = -11;
            snprintf(s->name, sizeof(s->name), "%s", a2);
            s->waitMax = a3[0] ? atoi(a3) : 3600;
            if (!parseKeys(s, a1)) goto bad;
        } else if (strcmp(cmd, "press_until_control") == 0) {
            /* press KEYS every 30 frames until Mario can be moved (cutscenes
             * and messages over) and has been for a second */
            s->frames = -9;
            s->waitMax = a2[0] ? atoi(a2) : 3600;
            s->seed = strcmp(a3, "quiet") == 0; /* (accepted: every press_until_control is quiet now) */
            if (!parseKeys(s, a1)) goto bad;
        } else if (strcmp(cmd, "hp") == 0) {
            s->frames = -4;
            s->stickX = 0;
            snprintf(s->name, sizeof(s->name), "%s", a1);
        } else if (strcmp(cmd, "goto") == 0) {
            /* goto HIT: Mario onto a hit object (an exit's zone, a door), as
             * evt_mario_set_pos does; the script then walks or presses into it */
            s->frames = -8;
            if (a3[0]) { /* goto X Y Z: a position */
                snprintf(s->name, sizeof(s->name), "%s %s %s", a1, a2, a3);
            } else {
                snprintf(s->name, sizeof(s->name), "%s", a1);
            }
        } else if (strcmp(cmd, "pause") == 0) {
            s->frames = -7;
            snprintf(s->name, sizeof(s->name), "%s", a1);
        } else if (strcmp(cmd, "give") == 0 || strcmp(cmd, "join") == 0 || strcmp(cmd, "party") == 0 ||
                   strcmp(cmd, "story") == 0 || strcmp(cmd, "flag") == 0 || strcmp(cmd, "audience") == 0 ||
                   strcmp(cmd, "coins") == 0 || strcmp(cmd, "sp") == 0 || strcmp(cmd, "fp") == 0 ||
                   strcmp(cmd, "maxsp") == 0 || strcmp(cmd, "maxhp") == 0 || strcmp(cmd, "stars") == 0 ||
                   strcmp(cmd, "swap") == 0 || strcmp(cmd, "travel") == 0 || strcmp(cmd, "level") == 0 ||
                   strcmp(cmd, "gsw") == 0 || strcmp(cmd, "maxfp") == 0 || strcmp(cmd, "drop") == 0 ||
                   strcmp(cmd, "maxbp") == 0 || strcmp(cmd, "rank") == 0) {
            /* state set-up for a test, at once */
            s->frames = -13;
            snprintf(s->name, sizeof(s->name), "%s", cmd);
            s->waitMax = (s32)strtol(a1, NULL, 0);
            s->seed = (u32)strtol(a2, NULL, 0); /* gsw: the value */
        } else if (strcmp(cmd, "sfx") == 0 && a1[0]) {
            /* sfx NAME: the game plays sound effect NAME (psndSFXOn, as a script's evt_snd_sfxon does: the pick's
             * voices and sounds apply), for the audio captures; sfx NAME DX DY DZ: from that far from Mario
             * (psndSFXOn_3D: its pan, and its caption's side) */
            s->frames = -13;
            if (a4[0]) {
                snprintf(s->name, sizeof(s->name), "sfx:%s@%s,%s,%s", a1, a2, a3, a4);
            } else {
                snprintf(s->name, sizeof(s->name), "sfx:%s", a1);
            }
        } else if (strcmp(cmd, "practice") == 0) {
            /* practice wear BADGE | practice partner P hp|maxhp|rank VALUE | practice stat NAME VALUE |
             * practice show | practice items | practice warp MAP [ENTRANCE] | practice jump POINT (Aurora):
             * the Practice tab's own calls (practice_more.h), as its buttons make them */
            s->frames = -28;
            snprintf(s->name, sizeof(s->name), "%s %s %s %s", a1, a2, a3, a4);
        } else if (strcmp(cmd, "companion") == 0) {
            s->frames = -13;
            snprintf(s->name, sizeof(s->name), "companion_%s %s %s", a1, a2, a3);
            s->waitMax = (s32)strtol(a2, NULL, 0);
            s->seed = (u32)strtoul(a3, NULL, 0);
        } else if (strcmp(cmd, "random") == 0) {
            s->frames = atoi(a1);
            s->seed = a2[0] ? (u32)strtoul(a2, NULL, 0) : 1u;
            if (s->seed == 0) {
                s->seed = 1;
            }
        } else if (strcmp(cmd, "talk") == 0 && a1[0]) {
            /* talk NAME|INDEX [MAX]: that NPC's talk, as A in front of it starts it
             * (only where the map blocks the real thing: across a counter) */
            s->frames = -16;
            snprintf(s->name, sizeof(s->name), "%s", a1);
            s->waitMax = a2[0] ? atoi(a2) : 900;
        } else if (strcmp(cmd, "talk_all") == 0 || strcmp(cmd, "mobj_all") == 0 || strcmp(cmd, "tattle_all") == 0) {
            s->frames = cmd[0] == 'm' ? -17 : -16;
            s->waitMax = a1[0] ? atoi(a1) : 900;
            if (cmd[1] == 'a' && cmd[2] == 't') {
                snprintf(s->name, sizeof(s->name), "%s", "*tattle"); /* (talk_all's own step, Goombella's Tattle) */
            }
        } else if (strcmp(cmd, "tap") == 0 || strcmp(cmd, "hover") == 0) {
            s->frames = cmd[0] == 't' ? -18 : -19;
            snprintf(s->name, sizeof(s->name), "%s %s", a1, a2);
        } else if (strcmp(cmd, "back") == 0) {
            s->frames = -20;
        } else if (strcmp(cmd, "wheel") == 0) {
            s->frames = -21;
            snprintf(s->name, sizeof(s->name), "%s", a1);
        } else if (strcmp(cmd, "walk_to") == 0) {
            /* walk_to HIT [MAX]: the stick towards a hit object (an exit's
             * zone) until the map changes, jumping when stuck; at most MAX
             * frames (default 1800), then "[input] walk_to: not reached" */
            char* end1;
            char* end2;
            s->frames = -23;
            (void)strtod(a1, &end1);
            (void)strtod(a2, &end2);
            if (a2[0] && *end1 == '\0' && *end2 == '\0') {
                /* walk_to X Z [MAX]: to that point of the map, until Mario is
                 * there (or the map changes) */
                snprintf(s->name, sizeof(s->name), "%s %s", a1, a2);
                s->stickX = 1;
                s->waitMax = a3[0] ? atoi(a3) : 1800;
            } else if (strcmp(a1, "npc") == 0 && a2[0]) {
                /* walk_to npc NAME|INDEX [MAX]: after an NPC wherever it goes,
                 * until Mario is next to it (the name as the map's scripts
                 * give it, UTF-8, or its index in the NPC table) */
                snprintf(s->name, sizeof(s->name), "%s", a2);
                s->stickX = 2;
                s->waitMax = a3[0] ? atoi(a3) : 1800;
            } else if (strcmp(a1, "item") == 0 && a2[0]) {
                /* walk_to item NAME [MAX]: into a field item (its name as `where items` gives it) until it is
                 * picked up (gone); one that isn't there is taken as picked up already */
                snprintf(s->name, sizeof(s->name), "%s", a2);
                s->stickX = 4;
                s->waitMax = a3[0] ? atoi(a3) : 1800;
            } else if (strcmp(a2, "stop") == 0) {
                /* walk_to HIT stop [MAX]: to the hit object, stopping there
                 * (a door, an NPC's place: then press A) */
                snprintf(s->name, sizeof(s->name), "%s", a1);
                s->stickX = 1;
                s->waitMax = a3[0] ? atoi(a3) : 1800;
            } else {
                snprintf(s->name, sizeof(s->name), "%s", a1);
                s->waitMax = a2[0] ? atoi(a2) : 1800;
            }
        } else if (strcmp(cmd, "fight") == 0) {
            /* fight [MAX]: walk_to npc into the nearest enemy until the touch's battle is over and it is
             * beaten (a field battle, the enemy's own defeat script run: the Pit's pipe rises after it) */
            snprintf(s->name, sizeof(s->name), "enemy");
            s->frames = -23;
            s->stickX = 3;
            s->waitMax = a1[0] ? atoi(a1) : 1800;
        } else if (strcmp(cmd, "mobj_event") == 0) {
            s->frames = -15;
            snprintf(s->name, sizeof(s->name), "%s", a1);
        } else if (strcmp(cmd, "select") == 0) {
            s->frames = -14;
            s->waitMax = strcmp(a1, "list") == 0 ? -1 : atoi(a1);
        } else if (strcmp(cmd, "rewind") == 0) {
            /* rewind N: the Rewind control held for the next N frames */
            s->frames = -22;
            s->waitMax = atoi(a1);
        } else if (strcmp(cmd, "state") == 0) {
            s->frames = -24;
            s->waitMax = atoi(a2) - 1;
            s->stickX = strcmp(a1, "load") == 0;
            if (strcmp(a1, "load") != 0 && strcmp(a1, "save") != 0) goto bad;
        } else if (strcmp(cmd, "where") == 0) {
            s->frames = -25;
            /* where npcs / hits / mobjs: each NPC, hit object or map object too */
            s->stickX = strcmp(a1, "npcs") == 0 ? 1 : strcmp(a1, "hits") == 0 ? 2 : strcmp(a1, "mobjs") == 0 ? 3
                      : strcmp(a1, "flag") == 0 ? 4 : strcmp(a1, "gsw") == 0 ? 5 : strcmp(a1, "floor") == 0 ? 6
                      : strcmp(a1, "items") == 0 ? 7 : strcmp(a1, "words") == 0 ? 8
                      : strcmp(a1, "have") == 0 ? 9 : strcmp(a1, "windows") == 0 ? 10 : 0;
            /* where flag|gsw|have: every name or number to the line's end (from the line itself: the
               split above keeps three, and a3 only 15 characters, so a longer list or name was cut) */
            {
                const char* rest = line;
                size_t len;
                int k;
                for (k = 0; k < 2; k++) { /* past "where" and its kind */
                    rest += strspn(rest, " \t");
                    rest += strcspn(rest, " \t\r\n");
                }
                rest += strspn(rest, " \t");
                len = strcspn(rest, "\r\n");
                if (len >= sizeof(s->name)) {
                    goto bad; /* (a list too long for one step: split it over two where lines) */
                }
                snprintf(s->name, sizeof(s->name), "%.*s", (int)len, rest);
            }
            if (strcmp(a1, "coop") == 0) {
                /* where coop [MAX]: once everyone is settled, at most MAX frames on (default 0: now) */
                s->frames = -35;
                s->waitMax = atoi(a2);
            }
        } else if (strcmp(cmd, "pad") == 0 && atoi(a1) >= 2 && atoi(a1) <= 4 && a2[0]) {
            /* pad N KEYS [FRAMES]: controller N (2-4) plugged in, KEYS held on it for FRAMES (default 2); the
             * script goes on at once (a co-op player beside player 1; "wait" for the press to land) */
            s->frames = -32;
            s->waitMax = atoi(a1) - 1;
            s->seed = a3[0] ? (u32)atoi(a3) : 2u;
            if (strcmp(a2, "none") != 0 && !parseKeys(s, a2)) goto bad;
        } else if (strcmp(cmd, "online") == 0 && (strcmp(a1, "leave") == 0 || (strcmp(a1, "join") == 0 && a2[0]))) {
            /* online leave / online join CODE|ADDRESS: as the Start menu's Leave the room and Join (headless has
             * no Multiplayer page; $TTYD_SYNC_LEAVE_AT and _MENU_JOIN_AT at a step of the script, not a frame) */
            s->frames = -36;
            s->stickX = strcmp(a1, "join") == 0;
            snprintf(s->name, sizeof(s->name), "%s", a2);
        } else if (strcmp(cmd, "press_until_pick") == 0 && a2[0]) {
            /* press_until_pick KEYS N [MAX]: KEYS every 30 frames until the room's pick (port/together.h's
             * portTogetherPicked: 1 the adventure, 10-16 a party game) is N (Aurora) */
            s->frames = -38;
            snprintf(s->name, sizeof(s->name), "%s", a2);
            s->waitMax = a3[0] ? atoi(a3) : 3600;
            if (!parseKeys(s, a1)) goto bad;
        } else if (strcmp(cmd, "key") == 0 && a1[0]) {
            /* key NAME [FRAMES]: a keyboard key (SDL's name: Escape, Return, Backspace...) held FRAMES (default 2),
             * as the window's own key event (Aurora: the pages and menus' keys); the script goes on at once */
            s->frames = -39;
            snprintf(s->name, sizeof(s->name), "%s", a1);
            s->waitMax = a2[0] ? atoi(a2) : 2;
        } else if (strcmp(cmd, "together") == 0 && a1[0]) {
            /* together PICK / together seat S W: the Multiplayer page's pick (versus, raid, bossrush, quiz, cards,
             * bingo, dm2..dm4, together) or Who's who swap, as its button (Aurora: port/together.h) */
            s->frames = -37;
            snprintf(s->name, sizeof(s->name), "%s %s %s", a1, a2, a3);
        } else if ((strcmp(cmd, "unplug") == 0 || strcmp(cmd, "plug") == 0) && atoi(a1) >= 2 && atoi(a1) <= 4) {
            /* unplug N [FRAMES]: controller N pulled out (for FRAMES, else until "plug N"); plug N: back */
            s->frames = strcmp(cmd, "unplug") == 0 ? -33 : -34;
            s->waitMax = atoi(a1) - 1;
            s->seed = a2[0] ? (u32)atoi(a2) : 0u;
        } else if (strcmp(cmd, "end") == 0) {
            s->frames = -2;
        } else {
            goto bad;
        }
        sCount++;
        continue;
    bad:
        line[strcspn(line, "\r\n")] = '\0';
        fprintf(stderr, "TTYD_INPUT: %s:%d: cannot parse '%.80s%s'\n", path, n, line, strlen(line) > 80 ? "..." : "");
        loadFail();
    }
    if (sCount >= MAX_LINES - 1 && readLine(f, line, sizeof(line))) {
        /* the steps after this would have been dropped without a word */
        fprintf(stderr, "TTYD_INPUT: %s: more than %d steps\n", path, MAX_LINES - 1);
        loadFail();
    }
    fclose(f);
}

/* pause MS: as the window being minimized for MS of real time (portPause);
 * reports how far the game clock moved meanwhile (should be ~0). */
static void pauseFor(int ms) {
    OSTime before;
    portPause();
    before = (OSTime)portGameClockPeek();
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    {
        struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000};
        nanosleep(&ts, NULL);
    }
#endif
    printf("[input] paused %d ms; game clock moved %lld us\n", ms,
           (long long)OSTicksToMicroseconds((OSTime)portGameClockPeek() - before));
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
    CALL(evt_npc_wait_battle_end) /* as a map script does: Mario gets his control back after it */
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

/* fight: the index of the nearest live enemy NPC (as startBattle picks it), or -1 */
static s32 nearestEnemy(void) {
    NpcWork* wp = npcGetWorkPtr();
    MarioWork* mario = marioGetPtr();
    s32 i, best = -1;
    f32 bestDist = 1e30f;
    for (i = 0; i < wp->total; i++) {
        NpcEntry* npc = &wp->entries[i];
        f32 dx, dz;
        if (!(npc->flags & NPC_FLAG_USED) || npc->battleInfo.groupSetup == NULL || npc->deadEvent == NULL ||
            npc->position.y < -500.0f) {
            continue;
        }
        dx = npc->position.x - mario->position.x;
        dz = npc->position.z - mario->position.z;
        if (dx * dx + dz * dz < bestDist) {
            bestDist = dx * dx + dz * dz;
            best = i;
        }
    }
    return best;
}

/* The nearest live NPC with a battle group (an enemy), as a map script names
 * it; with entry >= 0 the nearest live NPC, which gets that battle. */
static void startBattle(s32 entry) {
    NpcWork* wp = npcGetWorkPtr();
    MarioWork* mario = marioGetPtr();
    NpcEntry* best = NULL;
    f32 bestDist = 1e30f;
    s32 i;
    for (i = 0; i < wp->total; i++) { /* (count is how many are used: they sit anywhere below total) */
        NpcEntry* npc = &wp->entries[i];
        f32 dx, dz;
        /* as the field battle check (npcdrv.c): a battle group and a defeat
         * script; story NPCs with only a group would fight out of context */
        if (!(npc->flags & NPC_FLAG_USED) ||
            (entry < 0 && (npc->battleInfo.groupSetup == NULL || npc->deadEvent == NULL))) {
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
        /* a test's failure, not a step passed over: 135 PM64 battle scenarios on tik_15 (no field enemy) passed
         * without ever fighting */
        printf("[input] battle: no %s on this map (use `battle N` on a map with an NPC)\n",
               entry < 0 ? "enemy NPC" : "NPC");
        fflush(stdout);
        portTestExit(6);
        return;
    }
    if (entry >= 0 && !setBattleEntry(best, entry)) {
        portTestExit(6);
    }
    snprintf(sBattleNpc, sizeof(sBattleNpc), "%s", best->description);
    printf("[input] battle with '%s'\n", sBattleNpc);
    fflush(stdout);
    evtEntry(input_battle_evt, 0, 0);
}

BOOL portInputActive(void) {
    if (sState < 0) {
        const char* path = getenv("TTYD_INPUT");
        sState = 0;
        if (path != NULL && *path) {
            load(path);
            sState = 1;
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

/* An NPC (in the map, not parked below it) within dist of Mario. */
static BOOL npcNear(const MarioWork* m, f32 dist) {
    NpcWork* nw = npcGetWorkPtr();
    s32 i;
    for (i = 0; i < nw->total; i++) {
        const NpcEntry* npc = &nw->entries[i];
        const f32 dx = npc->position.x - m->position.x, dz = npc->position.z - m->position.z;
        if ((npc->flags & NPC_FLAG_USED) && npc->position.y > -500.0f && dx * dx + dz * dz < dist * dist) {
            return TRUE;
        }
    }
    return FALSE;
}

/* where hits [NAME]: each hit object of the map, its centre and box; those with an
 * exit (a bero) or a case (A in front, standing on, ...) first, marked. With NAME, only
 * those whose name has NAME in it, each with its triangles (bars, walls with openings). */
static void whereHitList(BOOL marked, const char* only) {
    MapWork* mw = mapGetWork();
    s32 i, j, k, c;
    for (i = 0; i < mw->entries->count; i++) {
        MapEntry* map = &mw->entries[i];
        HitEntry* hit = map->hitEntries;
        for (j = 0; j < map->hitNumJoints; j++, hit++) {
            const char* name = hit->joint != NULL ? hit->joint->name : NULL;
            const char* bero = NULL;
            char cases[64] = "";
            Vec lo = {1e9f, 1e9f, 1e9f}, hi = {-1e9f, -1e9f, -1e9f}, centre;
            if (name == NULL || (hit->flags & HIT_FLAG_DELETED) || (only[0] && strstr(name, only) == NULL)) {
                continue;
            }
            for (k = 0; k < (s32)(sizeof(BeroINFOARR) / sizeof(BeroINFOARR[0])); k++) {
                if (BeroINFOARR[k] != NULL && BeroINFOARR[k]->hitName != NULL &&
                    strcmp(BeroINFOARR[k]->hitName, name) == 0) {
                    bero = BeroINFOARR[k]->nextMapName != NULL ? BeroINFOARR[k]->nextMapName : "-";
                }
            }
            for (c = 0; c < 64; c++) {
                const CaseEntry* ce = caseIdToPtr(c);
                if (ce != NULL && strcmp(ce->hitObjName, name) == 0 && strlen(cases) < sizeof(cases) - 8) {
                    snprintf(cases + strlen(cases), sizeof(cases) - strlen(cases), "%s%d", cases[0] ? "," : "",
                             (int)(ce->conditionId & 0xFF));
                }
            }
            if ((bero != NULL || cases[0]) != marked) {
                continue;
            }
            for (k = 0; k < hit->vectorCount; k++) {
                const Vec* v[3] = {&hit->vectors[k].v0, &hit->vectors[k].v1, &hit->vectors[k].v2};
                s32 n;
                for (n = 0; n < 3; n++) {
                    lo.x = v[n]->x < lo.x ? v[n]->x : lo.x, hi.x = v[n]->x > hi.x ? v[n]->x : hi.x;
                    lo.y = v[n]->y < lo.y ? v[n]->y : lo.y, hi.y = v[n]->y > hi.y ? v[n]->y : hi.y;
                    lo.z = v[n]->z < lo.z ? v[n]->z : lo.z, hi.z = v[n]->z > hi.z ? v[n]->z : hi.z;
                }
            }
            hitObjGetPos(name, &centre);
            printf("[input] where hit%s '%s' at %.0f %.0f %.0f", marked ? " *" : "", name, centre.x, centre.y,
                   centre.z);
            if (hit->vectorCount > 0) {
                printf(", box %.0f..%.0f %.0f..%.0f %.0f..%.0f, %d tris", lo.x, hi.x, lo.y, hi.y, lo.z, hi.z,
                       (int)hit->vectorCount);
            }
            if (bero != NULL) {
                printf(", exit to %s", bero);
            }
            if (cases[0]) {
                printf(", case %s", cases); /* CaseCondition (casedrv.h): 9 A in front (doors), 4/6 pipes, ... */
            }
            if (hitGetAttr(hit) != 0) {
                printf(", attr 0x%x", (unsigned)hitGetAttr(hit)); /* HIT_ATTR_* (hitdrv.h): 0x2000 a paper slit */
            }
            printf("%s\n", (hit->flags & HIT_FLAG_OFF) ? ", off" : "");
            if (only[0]) {
                /* each triangle, to see what a wall ray meets where (bars: a paper slit and its solid twin) */
                for (k = 0; k < hit->vectorCount; k++) {
                    const HitVector* hv = &hit->vectors[k];
                    printf("[input]   tri %d: %.3f %.3f %.3f / %.3f %.3f %.3f / %.3f %.3f %.3f, normal %.3f %.3f %.3f\n",
                           (int)k, hv->v0.x, hv->v0.y, hv->v0.z, hv->v1.x, hv->v1.y, hv->v1.z, hv->v2.x, hv->v2.y,
                           hv->v2.z, hv->normal.x, hv->normal.y, hv->normal.z);
                }
                printf("[input]   cull mode %d\n", (int)hit->joint->drawMode->cullMode);
            }
        }
    }
}

static void whereHits(const char* only) {
    whereHitList(TRUE, only);
    whereHitList(FALSE, only);
}

/* where floor [STEP [BELOW]]: a top-down map of the highest surface at each cell
 * (a ray cast down, from height BELOW: a tower's one storey), one character a cell: its height band, ' ' nothing
 * (a pit, outside the map), M Mario. Rows run from -z (up the screen in the
 * field camera) to +z, columns from -x to +x. */
static void whereFloor(s32 step, f32 below) {
    static const char kBands[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    MapWork* mw = mapGetWork();
    const MarioWork* m = marioGetPtr();
    Vec lo = {1e9f, 0.0f, 1e9f}, hi = {-1e9f, 0.0f, -1e9f};
    f32 ylo = 1e9f;
    s32 i, j, k, cols, rows, r, c;
    static f32 sHeights[100 * 100];
    for (i = 0; i < mw->entries->count; i++) {
        MapEntry* map = &mw->entries[i];
        HitEntry* hit = map->hitEntries;
        for (j = 0; j < map->hitNumJoints; j++, hit++) {
            if (hit->flags & (HIT_FLAG_DELETED | HIT_FLAG_OFF)) {
                continue;
            }
            for (k = 0; k < hit->vectorCount; k++) {
                const Vec* v[3] = {&hit->vectors[k].v0, &hit->vectors[k].v1, &hit->vectors[k].v2};
                s32 n;
                for (n = 0; n < 3; n++) {
                    lo.x = v[n]->x < lo.x ? v[n]->x : lo.x, hi.x = v[n]->x > hi.x ? v[n]->x : hi.x;
                    lo.z = v[n]->z < lo.z ? v[n]->z : lo.z, hi.z = v[n]->z > hi.z ? v[n]->z : hi.z;
                }
            }
        }
    }
    if (lo.x > hi.x) {
        printf("[input] where floor: no hit objects\n");
        return;
    }
    if (step < 5) {
        step = 25;
    }
    while ((hi.x - lo.x) / step >= 100 || (hi.z - lo.z) / step >= 100) {
        step *= 2;
    }
    cols = (s32)((hi.x - lo.x) / step) + 1;
    rows = (s32)((hi.z - lo.z) / step) + 1;
    for (r = 0; r < rows; r++) {
        for (c = 0; c < cols; c++) {
            f32 x, y, z, dist = below + 3000.0f, nx, ny, nz;
            const f32 px = lo.x + c * step + step * 0.5f, pz = lo.z + r * step + step * 0.5f;
            sHeights[r * cols + c] = hitCheckFilter(NULL, &x, &y, &z, &dist, &nx, &ny, &nz, px, below, pz, 0.0f,
                                                    -1.0f, 0.0f) != NULL
                                         ? y
                                         : -1e9f;
            if (sHeights[r * cols + c] > -1e8f && sHeights[r * cols + c] < ylo) {
                ylo = sHeights[r * cols + c];
            }
        }
    }
    if (ylo < below - 875.0f) {
        /* the bands (36 of them) end at BELOW: a high storey's floors in range, what lies below them '0' */
        ylo = below - 875.0f;
    }
    printf("[input] where floor: x %.0f..%.0f (columns), z %.0f..%.0f (rows), %d units a cell; height bands of 25 "
           "from y %.0f: 0 = %.0f, 1 = %.0f, ..., ' ' none, M Mario\n",
           lo.x, hi.x, lo.z, hi.z, (int)step, ylo, ylo, ylo + 25.0f);
    for (r = 0; r < rows; r++) {
        char line[104];
        for (c = 0; c < cols; c++) {
            const f32 h = sHeights[r * cols + c];
            const f32 cx = lo.x + c * step, cz = lo.z + r * step;
            if (m->position.x >= cx && m->position.x < cx + step && m->position.z >= cz && m->position.z < cz + step) {
                line[c] = 'M';
            } else if (h < -1e8f) {
                line[c] = ' ';
            } else {
                s32 band = (s32)((h - ylo) / 25.0f);
                line[c] = kBands[band < 0 ? 0 : band > 35 ? 35 : band];
            }
        }
        line[cols] = '\0';
        printf("[input] floor z %5.0f |%s|\n", lo.z + r * step, line);
    }
}

/* where words: every saved word that isn't 0, one kind a line (GSWF and LSWF
 * by number, GSW and LSW as number=value): two saves compared line by line */
static void whereWords(void) {
    static const char* const kKinds[] = {"gswf", "lswf"};
    char line[4096];
    size_t n;
    s32 i, k;
    for (k = 0; k < 2; k++) {
        const u32* f = k == 0 ? gp->gswf : gp->lswf;
        const s32 count = k == 0 ? 0x100 * 32 : 0x10 * 32;
        n = (size_t)snprintf(line, sizeof(line), "[input] where words %s:", kKinds[k]);
        for (i = 0; i < count; i++) {
            if (((f[i / 32] >> (i % 32)) & 1) && n < sizeof(line) - 16) {
                n += (size_t)snprintf(line + n, sizeof(line) - n, " %d", (int)i);
            }
        }
        printf("%s\n", line);
    }
    for (k = 0; k < 2; k++) {
        const u8* w = k == 0 ? gp->gsw : gp->lsw;
        const s32 count = k == 0 ? 0x800 : 0x400;
        n = (size_t)snprintf(line, sizeof(line), "[input] where words %s: 0=%d", k == 0 ? "gsw" : "lsw",
                             k == 0 ? (int)gp->gsw0 : (int)w[0]);
        for (i = 1; i < count; i++) {
            if (w[i] != 0 && n < sizeof(line) - 16) {
                n += (size_t)snprintf(line + n, sizeof(line) - n, " %d=%d", (int)i, (int)w[i]);
            }
        }
        printf("%s\n", line);
    }
    fflush(stdout);
}

/* where mobjs: each map object (switches, blocks, chests, save blocks) */
static void whereMobjs(void) {
    MapObjectWork* wp = portMobjGetWork();
    s32 i;
    for (i = 0; i < wp->count; i++) {
        const MapObjectEntry* m = &wp->entries[i];
        if (m->flags & MOBJ_FLAG_USED) {
            printf("[input] where mobj %d: '%s' (%s) at %.0f %.0f %.0f%s, state %d, timer %d\n", (int)i, m->name,
                   m->animName, m->position.x, m->position.y, m->position.z, m->event != 0 ? ", event" : "",
                   (int)m->state, (int)m->timer);
        }
    }
}

/* where items: each item lying in the field (coins, hearts, keys, badges) */
static void whereItems(void) {
    ItemWork* wp = portItemGetWork();
    s32 i;
    for (i = 0; i < wp->count; i++) {
        const ItemEntry* it = &wp->entries[i];
        if (it->flags & ITEM_FLAG_USED) {
            printf("[input] where item %d: '%s' (item %d, mode %d, status 0x%x) at %.0f %.0f %.0f\n", (int)i, it->name,
                   (int)it->itemId, (int)it->mode, (unsigned)it->status, it->position.x, it->position.y,
                   it->position.z);
        }
    }
}

/* talk_all / mobj_all cutting a script off at its frame cap: the script and
 * every inline event it started (a brother's spawner chain leads to it; the
 * game's own delete leaves those running: an inn's paper loop froze Mario) */
static void sweepCut(s32 id) {
    EventWork* ew = evtGetWork();
    EventEntry* root = NULL;
    s32 i;
    for (i = 0; i < ew->count; i++) {
        if ((ew->entries[i].flags & 1) && ew->entries[i].eventId == id) {
            root = &ew->entries[i];
        }
    }
    for (i = 0; root != NULL && i < ew->count; i++) {
        EventEntry* e = &ew->entries[i];
        EventEntry* up;
        s32 depth = 0;
        if (!(e->flags & 1) || e == root) {
            continue;
        }
        for (up = e; up != NULL && up != root && depth < 64; depth++) {
            up = up->brotherEvent != NULL ? up->brotherEvent : up->waitingEvent;
        }
        if (up == root) {
            evtDeleteID(e->eventId);
        }
    }
    evtDeleteID(id);
}

/* The stick for walking along world direction (dx, dz) at tilt: by the field
 * camera's facing (eye to target), as the stick moves Mario. The usual camera
 * looks down -z (stick right +x, up -z): then exactly the old sums. */
static void walkStick(PADStatus* pad, float dx, float dz, float tilt) {
    const CameraEntry* cam = camGetPtr(CAMERA_3D);
    float fx = 0.0f, fz = -1.0f, fl;
    if (cam != NULL) {
        fx = cam->cameraAt.x - cam->cameraPos.x;
        fz = cam->cameraAt.z - cam->cameraPos.z;
    }
    fl = sqrtf(fx * fx + fz * fz);
    if (fl < 1e-3f || (fabsf(fx) < 0.02f * fl && fz < 0.0f)) {
        pad->stickX = (s8)(dx * tilt);
        pad->stickY = (s8)(-dz * tilt);
        return;
    }
    fx /= fl;
    fz /= fl;
    /* up = forward (fx, fz); right = (-fz, fx) */
    pad->stickX = (s8)((dx * -fz + dz * fx) * tilt);
    pad->stickY = (s8)((dx * fx + dz * fz) * tilt);
}

/* walk_to HIT: where hit object HIT is; else, for an exit whose bero table
 * gives its place, there. FALSE: no such hit object. */
static BOOL walkHitPos(const char* name, Vec* to) {
    s32 i;
    if (hitNameToPtr(name) != NULL) {
        hitObjGetPos(name, to);
        return TRUE;
    }
    for (i = 0; i < (s32)(sizeof(BeroINFOARR) / sizeof(BeroINFOARR[0])); i++) {
        const BeroEntry* b = BeroINFOARR[i];
        if (b != NULL && b->hitName != NULL && strcmp(b->hitName, name) == 0 && b->centerPosition != BERO_POS_AUTO) {
            to->x = (f32)b->centerPosition;
            to->y = (f32)b->centerPositionY;
            to->z = (f32)b->centerPositionZ;
            return TRUE;
        }
    }
    return FALSE;
}

static const char* sweepName(void) {
    return sSweepKind == 1 ? "talk_all" : sSweepKind == 2 ? "mobj_all" : "tattle_all";
}

static void applyScript(PADStatus* pad) {
    const Step* s;
    if (!portInputActive()) {
        return;
    }
    sFrame++;
    if (sState == 1 && getenv("TTYD_MAP") == NULL) {
        sState = 2;
    }
    if (sState != 2) {
        return;
    }
    if (sSweepKind != 0) {
        /* talk_all / mobj_all */
        memset(&pad[0], 0, sizeof(pad[0]));
        pad[0].err = PAD_ERR_NONE;
        /* a talk that changes the map (a ride, a door): stop at once, while the map is still loading,
         * without the key-on below: the map change reset Mario's and the partners' key locks already,
         * and one more key-on left them at -1, locked in the new room */
        if (gp == NULL || strncmp(gp->currentMapName, sSweepMap, sizeof(sSweepMap)) != 0 ||
            seqGetSeq() == SEQ_MAP_CHANGE) {
            printf("[input] %s: the map changed, stopping (%d run)\n", sweepName(),
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
                printf("[input] %s %d: still running after %d frames\n", sSweepKind == 2 ? "mobj" : sSweepKind == 3 ? "tattle" : "talk",
                       (int)sSweepIndex, (int)sSweepMax);
                sweepCut(sSweepEvt);
            }
            if (sSweepKind != 2 && sSweepNpc != NULL && (sSweepNpc->flags & NPC_FLAG_USED)) {
                evtStartID(sSweepNpc->regularEvtId);
            }
            marioKeyOn();
            partyKeyOn();
            sSweepEvt = -1;
            sSweepDone++;
            sSweepIndex++;
            if (sSweepOne >= 0) {
                sSweepIndex = 1 << 30; /* talk NAME: that one only */
            }
            return; /* a frame between items */
        }
        for (;;) {
            EventEntry* e = NULL;
            if (sSweepKind == 3 && sSweepIndex == npcGetWorkPtr()->total) {
                /* last, the map's own Tattle (no NPC: msg_kuri_map, or the block or sign in front) */
                printf("[input] tattle: the map\n");
                marioKeyOff();
                partyKeyOff();
                e = evtEntryType(kurio_msg_evt, 0, 0, 26);
                marioStSystemLevel(1);
                e->lwData[0] = 0;
                sSweepNpc = NULL;
            } else if (sSweepKind == 1 || sSweepKind == 3) {
                NpcWork* wp = npcGetWorkPtr();
                NpcEntry* npc;
                if (sSweepIndex >= wp->total) {
                    if (sSweepKind == 3 && sSweepIndex == wp->total) {
                        continue; /* (the map's, above) */
                    }
                    break;
                }
                npc = &wp->entries[sSweepIndex];
                /* parked out of reach (scripts put unused NPCs at y -1000):
                 * a player cannot talk to them, and their scripts may need
                 * set-up that only comes with placing them */
                if (!(npc->flags & NPC_FLAG_USED) || npc->talkEvt == NULL || npc->position.y < -500.0f) {
                    sSweepIndex++;
                    continue;
                }
                printf("[input] %s %d: '%s'\n", sSweepKind == 3 ? "tattle" : "talk", (int)sSweepIndex, npc->description);
                evtStopID(npc->regularEvtId);
                marioKeyOff();
                partyKeyOff();
                if (sSweepKind == 3) {
                    /* as her ability does for the NPC in front of Mario (kuribo_use) */
                    e = evtEntryType(kurio_msg_evt, 0, 0, 26);
                    marioStSystemLevel(1);
                    e->lwData[0] = (EvtWord)npc;
                } else {
                    e = evtEntry(npc->talkEvt, 0, 0);
                    e->wNpcEventType = 6;
                    e->ownerNPC = npc;
                }
                sSweepNpc = npc;
            } else {
                MapObjectWork* wp = portMobjGetWork();
                MapObjectEntry* mobj;
                if (sSweepIndex >= wp->count) {
                    break;
                }
                mobj = &wp->entries[sSweepIndex];
                if (!(mobj->flags & MOBJ_FLAG_USED) || mobj->event == 0) {
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
        printf("[input] %s: %d run\n", sweepName(), (int)sSweepDone);
        fflush(stdout);
        sSweepKind = 0;
    }
    if (sWalkFrames >= 0) {
        /* walk_to: the stick towards the hit object until the map changes,
         * or towards the point until Mario is there */
        const Step* w = &sSteps[sCur];
        const BOOL point = w->stickX != 0;
        MarioWork* m = marioGetPtr();
        Vec to = {0.0f, 0.0f, 0.0f};
        float dx, dz, len, reach = 10.0f;
        if (gp == NULL || strcmp(gp->currentMapName, sWalkMap) != 0) {
            memset(&pad[0], 0, sizeof(pad[0]));
            pad[0].err = PAD_ERR_NONE;
            printf("[input] frame %u: walk_to: reached (map %s) after %d frames\n", (unsigned)sFrame,
                   gp != NULL ? gp->currentMapName : "", (int)sWalkFrames);
            fflush(stdout);
            sWalkFrames = -1;
            return;
        }
        if (seqGetSeq() == SEQ_MAP_CHANGE) {
            sWalkReload = TRUE;
        }
        if (seqGetSeq() != SEQ_GAME) {
            /* a battle on the way (an enemy touched): the pad left to it (and
             * to the battle hooks), the frames not counted; the walk goes on
             * after it */
            sWalkBattle = sWalkBattle || seqGetSeq() == SEQ_BATTLE;
            return;
        }
        if (sWalkReload) {
            /* an exit back into the same map: the walk ends there too */
            memset(&pad[0], 0, sizeof(pad[0]));
            pad[0].err = PAD_ERR_NONE;
            printf("[input] frame %u: walk_to: reached (map %s, reloaded) after %d frames\n", (unsigned)sFrame,
                   gp->currentMapName, (int)sWalkFrames);
            fflush(stdout);
            sWalkFrames = -1;
            return;
        }
        if (w->stickX == 2 || w->stickX == 3) {
            /* walk_to npc / fight: where the NPC is now */
            NpcWork* nw = npcGetWorkPtr();
            char* end = NULL;
            const long index = w->stickX == 3 ? sFightNpc : strtol(w->name, &end, 10);
            NpcEntry* npc = w->stickX == 3 || *end == '\0'
                                ? (index >= 0 && index < nw->total ? &nw->entries[index] : NULL)
                                : npcNameToPtr_NoAssert(w->name);
            if (w->stickX == 3 && sWalkBattle &&
                (npc == NULL || !(npc->flags & NPC_FLAG_USED) || (npc->jumpFlags & 4))) {
                /* fight: the enemy beaten in the battle the touch started (its dead flag, or deleted by its
                   defeat script) */
                memset(&pad[0], 0, sizeof(pad[0]));
                pad[0].err = PAD_ERR_NONE;
                printf("[input] frame %u: fight %s: beaten after %d frames\n", (unsigned)sFrame, w->name,
                       (int)sWalkFrames);
                fflush(stdout);
                sWalkFrames = -1;
                return;
            }
            if (npc == NULL || !(npc->flags & NPC_FLAG_USED)) {
                printf("[input] FAIL: walk_to npc %s: no such NPC on map '%s'\n", w->name, gp->currentMapName);
                fflush(stdout);
                portTestExit(5);
            }
            if (npc->position.y < -500.0f) {
                /* gone under the floor (a scene parks NPCs at 0 -1000 0): nothing to walk to any more */
                memset(&pad[0], 0, sizeof(pad[0]));
                pad[0].err = PAD_ERR_NONE;
                printf("[input] frame %u: walk_to npc %s: it went under the floor (y %.0f) after %d frames\n",
                       (unsigned)sFrame, w->name, npc->position.y, (int)sWalkFrames);
                fflush(stdout);
                sWalkFrames = -1;
                return;
            }
            to = npc->position;
            reach = w->stickX == 3 ? -1.0f : 25.0f; /* (fight: into it, until the battle) */
        } else if (w->stickX == 4) {
            /* walk_to item: where the item is now, into it until it is gone */
            ItemWork* iw = portItemGetWork();
            const ItemEntry* it = NULL;
            s32 i;
            for (i = 0; i < iw->count; i++) {
                if ((iw->entries[i].flags & ITEM_FLAG_USED) && strcmp(iw->entries[i].name, w->name) == 0) {
                    it = &iw->entries[i];
                    break;
                }
            }
            if (it == NULL) {
                memset(&pad[0], 0, sizeof(pad[0]));
                pad[0].err = PAD_ERR_NONE;
                printf("[input] frame %u: walk_to item %s: picked up (gone) after %d frames, Mario at %.0f %.0f\n",
                       (unsigned)sFrame, w->name, (int)sWalkFrames, m->position.x, m->position.z);
                fflush(stdout);
                sWalkFrames = -1;
                return;
            }
            to = it->position;
            reach = -1.0f;
        } else if (!point || sscanf(w->name, "%f %f", &to.x, &to.z) != 2) {
            if (!walkHitPos(w->name, &to)) {
                printf("[input] FAIL: walk_to %s: no such hit object on map '%s'\n", w->name, gp->currentMapName);
                fflush(stdout);
                portTestExit(5);
            }
            /* a hit object may stand in a wall (a door): on it, or up against
             * the wall close to it (stopped there), facing it */
            reach = 8.0f;
        }
        memset(&pad[0], 0, sizeof(pad[0]));
        pad[0].err = PAD_ERR_NONE;
        dx = to.x - m->position.x;
        dz = to.z - m->position.z;
        len = sqrtf(dx * dx + dz * dz);
        if (w->stickX == 2 && len >= reach && len < 80.0f && sWalkStuck > 0) {
            /* walk_to npc blocked close by (a counter between): done, facing it (the
             * stick has pushed towards it), so A talks across */
            printf("[input] frame %u: walk_to npc %s: blocked at %.0f units, facing\n", (unsigned)sFrame, w->name, len);
            fflush(stdout);
            sWalkFrames = -1;
            return;
        }
        if (point && (len < reach || (reach == 8.0f && len < 60.0f && sWalkStuck > 0))) {
            printf("[input] frame %u: walk_to: reached (%.0f %.0f) after %d frames\n", (unsigned)sFrame, m->position.x,
                   m->position.z, (int)sWalkFrames);
            fflush(stdout);
            sWalkFrames = -1;
            return;
        }
        if (point && sWalkFrames > 0 && !(seqGetSeq() == SEQ_GAME && marioChkKey())) {
            /* a scene took Mario on the way (a trigger crossed): the walk's job is done */
            if (++sWalkScene >= 30 && w->stickX != 3) { /* (fight: the battle's start, then the battle) */
                printf("[input] frame %u: walk_to: a scene took over at %.0f %.0f after %d frames\n", (unsigned)sFrame,
                       m->position.x, m->position.z, (int)sWalkFrames);
                fflush(stdout);
                sWalkFrames = -1;
                return;
            }
            sWalkFrames++;
            return;
        }
        if (++sWalkFrames > w->waitMax) {
            printf("[input] frame %u: walk_to %s: not reached in %d frames (Mario at %.0f %.0f %.0f, it at %.0f %.0f "
                   "%.0f)\n",
                   (unsigned)sFrame, w->name, (int)w->waitMax, m->position.x, m->position.y, m->position.z, to.x, to.y,
                   to.z);
            fflush(stdout);
            sWalkFrames = -1;
            return;
        }
        if (seqGetSeq() == SEQ_GAME && marioChkKey()) {
            /* near a point: slower, so Mario stops on it rather than past it */
            const float tilt = point && len < 60.0f ? 40.0f + len : 100.0f;
            sWalkScene = 0;
            if (len > 1.0f) {
                walkStick(&pad[0], dx / len, dz / len, tilt);
            }
            /* stuck for a moment: jump (for a step up); stuck longer: walk
             * along the wall, one side then the other, a longer way each time */
            if (sWalkFrames % 20 == 0) {
                const float moved = fabsf(m->position.x - sWalkPrev.x) + fabsf(m->position.z - sWalkPrev.z);
                sWalkStuck = moved < 6.0f ? sWalkStuck + 1 : 0;
                sWalkPrev = m->position;
                if (sWalkStuck >= 2 && sWalkDetour <= 0) {
                    sWalkTries++;
                    sWalkDetour = 60 * (1 + sWalkTries / 2);
                    sWalkStuck = 0;
                }
            }
            if (sWalkStuck > 0 && sWalkFrames % 20 < 3 && !npcNear(m, 80.0f)) {
                pad[0].button = PAD_BUTTON_A; /* (not by an NPC: A would talk to it) */
            }
            if (sWalkDetour > 0 && len > 1.0f) {
                const float side = sWalkTries % 2 ? 1.0f : -1.0f;
                sWalkDetour--;
                /* along the wall: the direction turned a right angle */
                walkStick(&pad[0], -dz / len * side, dx / len * side, 100.0f);
            }
        }
        return;
    }
    if (sWaitFrames >= 0) {
        /* wait_seq / wait_map: neutral input until the condition holds */
        const Step* w = &sSteps[sCur];
        static s32 sControlFrames;
        BOOL met;
        if (w->frames == -9) {
            sControlFrames = seqGetSeq() == SEQ_GAME && marioChkKey() && marioChkCtrl() ? sControlFrames + 1 : 0;
            met = sControlFrames >= 60;
        } else if (w->frames == -27) {
            char hit[64], axis[4], op[4];
            float v = 0.0f;
            Vec pos = {0.0f, 0.0f, 0.0f};
            met = FALSE;
            if (sscanf(w->name, "%63s %3s %3s %f", hit, axis, op, &v) == 4 && walkHitPos(hit, &pos)) {
                const float c = axis[0] == 'x' ? pos.x : axis[0] == 'y' ? pos.y : pos.z;
                met = op[0] == '>' ? c >= v : c <= v;
            }
        } else if (w->frames == -29) {
            met = portStateSyncPeerShows(w->name); /* wait_peer */
        } else if (w->frames == -38) {
#ifdef TTYD_BACKEND_AURORA
            met = portTogetherPicked() == atoi(w->name); /* press_until_pick */
#else
            met = TRUE; /* (headless: no party games) */
#endif
        } else if (w->frames == -35) {
            /* where coop [MAX]: everyone settled (as the end of the last frame's input found), or MAX frames
               gone; never a failure here: the line says NOT SETTLED and the run goes on */
            met = sCoopSettledSeen || sWaitFrames >= w->waitMax;
            if (met) {
                sCoopWherePending = 1;
            }
        } else if (w->frames == -31) {
#ifdef TTYD_BACKEND_AURORA
            met = !portBindingsBusy(); /* wait_bindings */
#else
            met = TRUE; /* (headless: no Bindings page, so nothing is ever asked) */
#endif
        } else if (w->frames == -26) {
            met = marioGetPtr()->position.y >= (f32)atof(w->name); /* wait_y */
        } else if (w->frames == -30) {
            met = portVfxShownLately(w->name); /* wait_vfx */
        } else if (w->frames == -11) {
            met = swByteGet(0) >= atoi(w->name);
        } else if (w->frames == -10) {
            met = sWaitFrames > 0 && !cardIsExec(); /* save: the card is done */
        } else {
            met = w->frames == -5 ? (s32)seqGetSeq() == atoi(w->name)
                                  : gp != NULL && strcmp(gp->currentMapName, w->name) == 0;
        }
        if (!met) {
            if (++sWaitFrames > w->waitMax) {
                printf("[input] FAIL: %s not reached in %d frames (sequence %d, map '%s')\n", w->text,
                       (int)w->waitMax, (int)seqGetSeq(), gp != NULL ? gp->currentMapName : "");
                fflush(stdout);
                portSettingsDumpNow(); /* tests: $TTYD_SETTINGS_DUMP, a failed run's too */
                portTestExit(5);
            }
            memset(&pad[0], 0, sizeof(pad[0]));
            pad[0].err = PAD_ERR_NONE;
            /* press_until_seq: a 2-frame press every 30. press_until_control presses only while Mario has
             * no control: next to an NPC, a press once his control was back started the talk again the
             * moment it ended, so the 60 frames of control never came (gor_01's Bob-omb, gor_02's Frankly) */
            if (sWaitFrames % 30 < 2 && (w->frames != -9 || sControlFrames == 0)) {
                pad[0].button = w->buttons;
                pad[0].stickX = w->stickX;
                pad[0].stickY = w->stickY;
            }
            return;
        }
        printf("[input] frame %u: reached after %d frames\n", (unsigned)sFrame, (int)sWaitFrames);
        fflush(stdout);
        sWaitFrames = -1;
    }
    while (sLeft <= 0) {
        if (sCur >= 0 && sSteps[sCur].frames == -2) {
            /* end: wait for a pending capture, then leave (the fingerprint file ends here: the
               capture takes a few frames more or less on Aurora, its readback in real time) */
            portTestEnding();
            if (!portShotPending()) {
                printf("[input] end\n");
                fflush(stdout);
                if (getenv("TTYD_SETTINGS_SAVE_AT_END") != NULL) {
                    portSettingsSave(); /* tests: what any save writes then (a run's own values left out) */
                }
                portSettingsDumpNow(); /* tests: $TTYD_SETTINGS_DUMP (tools/port/settings_fuzz.py) */
                portTestExit(0);
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
        if (s->frames == -1) {
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
        if (s->frames == -32 || s->frames == -33 || s->frames == -34) {
            scriptPadStep(s); /* pad N KEYS / unplug N / plug N: at once, the script goes on */
            continue;
        }
        if (s->frames == -39) {
#ifdef TTYD_BACKEND_AURORA
            if (sKeyLeft > 0) {
                portStoryTestKey(sKeyName, 0); /* (one at a time: the last one let go first) */
            }
            snprintf(sKeyName, sizeof(sKeyName), "%s", s->name);
            sKeyLeft = s->waitMax > 0 ? s->waitMax : 1;
            sKeyFresh = 1;
            printf("[input] key %s down: %s\n", s->name, portStoryTestKey(s->name, 1) ? "sent" : "no such key");
#else
            printf("[input] key %s: no keyboard on headless\n", s->name);
#endif
            fflush(stdout);
            continue;
        }
        if (s->frames == -37) {
#ifdef TTYD_BACKEND_AURORA
            const char* why = NULL;
            const int ok = portTogetherTest(s->name, &why);
            printf("[input] together %s: %s\n", s->name, ok ? "done" : why != NULL ? why : "refused");
#else
            printf("[input] together %s: refused (the Multiplayer page is Aurora's)\n", s->name);
#endif
            fflush(stdout);
            continue;
        }
        if (s->frames == -36) {
            if (s->stickX) {
                char text[64];
                const char* port = getenv("TTYD_SYNC_PORT");
                /* (an address with no port: the session's, as $TTYD_SYNC_MENU_JOIN_AT's) */
                if (strcmp(s->name, "port") == 0 && port != NULL) {
                    snprintf(text, sizeof(text), "[::1]:%s", port);
                } else {
                    snprintf(text, sizeof(text), "%s", s->name);
                }
                printf("[input] online join %s: %s\n", text, portStateSyncJoin(text) ? "on" : portStateSyncNote());
            } else {
                printf("[input] online leave\n");
                portStateSyncLeave();
            }
            fflush(stdout);
            continue;
        }
        if (s->frames == -3) {
            /* battle [N]; N may also come from $TTYD_BATTLE_SETUP (map_sweep --battles) */
            const char* n = s->name[0] ? s->name : getenv("TTYD_BATTLE_SETUP");
            startBattle(n != NULL && *n ? atoi(n) : -1);
            continue;
        }
        if (s->frames == -41) {
            /* setting KEY VALUE: at once, the script goes on */
            char key[64];
            int value = 0, *at = NULL;
            if (sscanf(s->name, "%63s %d", key, &value) == 2) {
                at = portSettingsFind(key);
            }
            if (at != NULL) {
                *at = value;
                printf("[input] setting %s = %d\n", key, value);
            } else {
                printf("[input] setting %s: no such declared setting\n", s->name);
            }
            fflush(stdout);
            continue;
        }
        if (s->frames == -40) {
            /* dev ...: its frames with controller 1 the devices' (below) */
            sLeft = portFlowsStep(s->name);
            if (sLeft <= 0) {
                continue;
            }
            break;
        }
        if (s->frames == -10) {
            printf("[input] save: slot %d, map '%s', story %d\n", (int)gp->saveSlot, gp->currentMapName,
                   (int)swByteGet(0));
            fflush(stdout);
            /* a Save Block's kind of save (evt_memcard_save's LW(0) 0): loaded,
             * it resumes here. The new file's first save left 23 ("start the
             * prologue"), and a save before the first Save Block loaded as a
             * new game */
            swByteSet(GSW_SAVE_RESUME_KIND, 0);
            cardWrite(gp->saveSlot);
        }
        if (s->frames == -5 || s->frames == -6 || s->frames == -9 || s->frames == -10 || s->frames == -11 || s->frames == -26 ||
            s->frames == -27 || s->frames == -29 || s->frames == -30 || s->frames == -31 || s->frames == -35 ||
            s->frames == -38) {
            sWaitFrames = 0;
            sCoopSettledSeen = 0;
            memset(&pad[0], 0, sizeof(pad[0]));
            pad[0].err = PAD_ERR_NONE;
            return;
        }
        if (s->frames == -23) {
            snprintf(sWalkMap, sizeof(sWalkMap), "%s", gp != NULL ? gp->currentMapName : "");
            sWalkFrames = 0;
            sWalkStuck = 0;
            sWalkDetour = 0;
            sWalkTries = 0;
            sWalkScene = 0;
            sWalkReload = FALSE;
            sWalkBattle = FALSE;
            sWalkPrev = marioGetPtr()->position;
            if (s->stickX == 3) {
                sFightNpc = nearestEnemy();
                if (sFightNpc < 0) {
                    printf("[input] fight: no enemy NPC on this map\n");
                    fflush(stdout);
                    portTestExit(6);
                }
            }
            memset(&pad[0], 0, sizeof(pad[0]));
            pad[0].err = PAD_ERR_NONE;
            return;
        }
        if (s->frames == -8) {
            Vec pos;
            if (sscanf(s->name, "%f %f %f", &pos.x, &pos.y, &pos.z) != 3) {
                hitObjGetPos(s->name, &pos);
            }
            marioGetPtr()->position = pos;
            continue;
        }
        if (s->frames == -7) {
            pauseFor(atoi(s->name));
            continue;
        }
        if (s->frames == -4) {
            pouchSetHP((s16)atoi(s->name)); /* e.g. 1, so the next hit ends in a Game Over */
            continue;
        }
        if (s->frames == -13) {
            const s32 v = s->waitMax;
            if (strncmp(s->name, "sfx:", 4) == 0) {
                char* at = strchr(s->name + 4, '@');
                if (at != NULL && marioGetPtr() != NULL) {
                    Vec pos = marioGetPtr()->position;
                    float d[3] = {0.0f, 0.0f, 0.0f};
                    *at = '\0';
                    sscanf(at + 1, "%f,%f,%f", &d[0], &d[1], &d[2]);
                    pos.x += d[0];
                    pos.y += d[1];
                    pos.z += d[2];
                    printf("[input] sfx %s at %.0f %.0f %.0f: %d\n", s->name + 4, pos.x, pos.y, pos.z,
                           (int)psndSFXOn_3D(s->name + 4, &pos));
                } else {
                    printf("[input] sfx %s: %d\n", s->name + 4, (int)psndSFXOn(s->name + 4));
                }
                fflush(stdout);
            } else if (strcmp(s->name, "give") == 0) {
                pouchGetItem((ItemType)v);
            } else if (strcmp(s->name, "drop") == 0) {
                /* drop ITEM: the item lies at Mario's feet and is picked up as one on the field is (an upgrade's
                 * important item ceremony and all: #32's repro) */
                MarioWork* mario = marioGetPtr();
                if (mario != NULL) {
                    itemEntry(mario->position.x, mario->position.y, mario->position.z, NULL, v, ITEM_ENTRY_STAY, -1,
                              NULL);
                    printf("[input] drop 0x%x at Mario\n", (unsigned)v);
                    fflush(stdout);
                }
            } else if (strcmp(s->name, "join") == 0) {
                partyJoin((PartyId)v);
            } else if (strcmp(s->name, "party") == 0) {
                if (v > 0 && v < MAIN_PARTY_MEMBER_MAX) {
                    partyJoin((PartyId)v); /* (a follower, 8 and up, joins nothing: the pouch has the seven) */
                }
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
            } else if (strcmp(s->name, "sp") == 0) {
                pouchSetAP((s16)v);
            } else if (strcmp(s->name, "fp") == 0) {
                pouchSetFP((s16)v);
            } else if (strcmp(s->name, "maxsp") == 0) {
                pouchGetPtr()->maximumSP = (s16)v;
            } else if (strcmp(s->name, "maxhp") == 0) {
                pouchSetMaxHP((s16)v);
                pouchSetHP((s16)v);
            } else if (strcmp(s->name, "gsw") == 0) {
                swByteSet(v, (s32)s->seed);
            } else if (strcmp(s->name, "maxfp") == 0) {
                pouchGetPtr()->baseMaxFP = (s16)v; /* (as a level-up raises it) */
                pouchSetMaxFP((s16)v);
                pouchSetFP((s16)v);
            } else if (strcmp(s->name, "level") == 0) {
                pouchGetPtr()->level = (s16)v;
            } else if (strcmp(s->name, "maxbp") == 0) {
                /* maxbp N: N BP, all of it free (worn badges' costs taken off) */
                PouchData* pouch = pouchGetPtr();
                pouch->availableBP = (s16)(pouch->availableBP + v - pouch->maximumBP);
                pouch->maximumBP = (s16)v;
            } else if (strcmp(s->name, "rank") == 0 && v >= 0 && v <= 2) {
                /* rank N: every partner at that rank (0 Normal, 1 Super, 2 Ultra), HP to the rank's, as Merlon */
                s32 i;
                for (i = 1; i < 8; i++) {
                    PouchPartyData* pd = &pouchGetPtr()->partyData[i];
                    pd->HPLevel = pd->attackLevel = pd->techLevel = (s16)v;
                }
                pouchRevisePartyParam();
                for (i = 1; i < 8; i++) {
                    pouchGetPtr()->partyData[i].currentHP = pouchGetPtr()->partyData[i].maximumHP;
                }
            } else if (strcmp(s->name, "stars") == 0) {
                pouchGetPtr()->starPowersObtained = (u16)v;
            } else if (strcmp(s->name, "swap") == 0) {
                portQuickSwapPressCoop(); /* (as the second player's: on whether or not the Enhancements quick-swap is) */
            } else if (strncmp(s->name, "companion_", 10) == 0) {
                const char* what = s->name + 10;
                float x = 0.0f, y = 0.0f;
                if (strncmp(what, "tap ", 4) == 0 && sscanf(what + 4, "%f %f", &x, &y) == 2) {
                    portCompanionMenuTap(x, y); /* the second screen's Start menu (companion_menu.h) */
                } else if (strncmp(what, "shot ", 5) == 0) {
                    char name[48];
                    if (sscanf(what + 5, "%47s", name) == 1) {
                        portCompanionMenuShot(name);
                    }
                } else if (strncmp(what, "button ", 7) == 0) {
                    static const char* const kNames[] = {"", "a", "b", "up", "down", "left", "right", "l", "r", "x", "y", "z"};
                    int k;
                    for (k = 1; k < (int)(sizeof(kNames) / sizeof(kNames[0])); k++) {
                        if (strncmp(what + 7, kNames[k], strlen(kNames[k])) == 0 && (what[7 + strlen(kNames[k])] == ' ' || what[7 + strlen(kNames[k])] == 0)) {
                            portCompanionMenuButton(k);
                        }
                    }
                } else {
                    portCompanionTap(what[0] == 'i' ? PORT_COMPANION_TAP_ITEM : PORT_COMPANION_TAP_PARTNER, v, (int)s->seed);
                }
            } else if (strcmp(s->name, "travel") == 0) {
                portFastTravelRequest(v);
            }
            continue;
        }
        if (s->frames == -16 || s->frames == -17) {
            sSweepKind = s->frames == -16 ? (strcmp(s->name, "*tattle") == 0 ? 3 : 1) : 2;
            sSweepIndex = 0;
            sSweepOne = -1;
            if (s->name[0] && sSweepKind == 1) {
                /* talk NAME|INDEX */
                NpcWork* nw = npcGetWorkPtr();
                char* end;
                const long index = strtol(s->name, &end, 10);
                NpcEntry* npc = *end == '\0' ? (index >= 0 && index < nw->total ? &nw->entries[index] : NULL)
                                             : npcNameToPtr_NoAssert(s->name);
                if (npc == NULL || !(npc->flags & NPC_FLAG_USED) || npc->talkEvt == NULL) {
                    printf("[input] FAIL: talk %s: no such NPC with a talk on map '%s'\n", s->name,
                           gp != NULL ? gp->currentMapName : "");
                    fflush(stdout);
                    portTestExit(5);
                }
                sSweepOne = (s32)(npc - nw->entries);
                sSweepIndex = sSweepOne;
            }
            sSweepEvt = -1;
            sSweepDone = 0;
            sSweepMax = s->waitMax;
            sSweepNpc = NULL;
            snprintf(sSweepMap, sizeof(sSweepMap), "%s", gp != NULL ? gp->currentMapName : "");
            return; /* runs from the next frame */
        }
        if (s->frames == -18) {
            float nx = 0.5f, ny = 0.5f;
            sscanf(s->name, "%f %f", &nx, &ny);
            portMenuTap(nx, ny); /* as the touch overlay does with a list up */
            continue;
        }
        if (s->frames == -19) {
            float nx = 0.5f, ny = 0.5f;
            sscanf(s->name, "%f %f", &nx, &ny);
            portMenuHover(nx, ny); /* as the mouse moving there */
            continue;
        }
        if (s->frames == -20) {
            portMenuBack(); /* as a right click */
            continue;
        }
        if (s->frames == -22) {
            portRewindTest(s->waitMax); /* held from now; the next steps go on meanwhile (wait, shot) */
            continue;
        }
        if (s->frames == -21) {
            portMenuWheel(atoi(s->name)); /* as the wheel's notches, up > 0 */
            continue;
        }
        if (s->frames == -15) {
            MapObjectEntry* mobj = mobjNameToPtrNoAssert(s->name);
            if (mobj == NULL || mobj->event == 0) {
                printf("[input] mobj_event: no map object '%s' with an event\n", s->name);
            } else {
                mobjRunEvent(mobj, (EvtWord*)mobj->event);
            }
            fflush(stdout);
            continue;
        }
        if (s->frames == -14) {
            EventEntry* e = evtEntry(input_select_evt, 0, 0);
            e->lwData[0] = s->waitMax < 0 ? (EvtWord)sSelectList : (EvtWord)s->waitMax;
            continue;
        }
        if (s->frames == -24) {
            portSaveStateRequest(s->waitMax, s->stickX);
            continue;
        }
        if (s->frames == -28) {
            char what[16] = "", b1[32] = "", b2[32] = "", b3[16] = "";
            int ok = 0;
            sscanf(s->name, "%15s %31s %31s %15s", what, b1, b2, b3);
            if (strcmp(what, "wear") == 0) {
                ok = portPracticeGiveBadgeWorn((int)strtol(b1, NULL, 0));
            } else if (strcmp(what, "partner") == 0) {
                const int p = atoi(b1);
                const int field = strcmp(b2, "hp") == 0      ? PORT_PARTY_HP
                                  : strcmp(b2, "maxhp") == 0 ? PORT_PARTY_MAX_HP
                                                             : PORT_PARTY_RANK;
                portPracticeSetParty(p, field, atoi(b3));
                ok = portPracticeParty(p, field) >= 0;
                printf("[input] practice partner %d %s: now %d\n", p, b2, portPracticeParty(p, field));
            } else if (strcmp(what, "stat") == 0) {
                /* practice stat NAME VALUE: the Practice tab's Stats (practice_tools.h) */
                const int which = portPracticeStatByName(b1);
                if (which >= 0) {
                    portPracticeSetStat(which, atoi(b2));
                    ok = portPracticeCanEdit();
                    printf("[input] practice stat %s: now %d\n", b1, portPracticeStat(which));
                }
            } else if (strcmp(what, "show") == 0) {
                portPracticeShow();
                ok = 1;
            } else if (strcmp(what, "items") == 0) {
                portPracticeItemsCheck(); /* the Items and Badges lists' entries, as the page lists them */
                ok = 1;
            } else if (strcmp(what, "warp") == 0) {
                ok = portRoomWarpRequest(b1, b2[0] ? b2 : NULL);
            }
#ifdef TTYD_BACKEND_AURORA
            else {
                /* the post-game page's buttons (postgame_extra.h): practice exhibition N | champion |
                 * nighttail | mover */
                const char* why = "";
                if (strcmp(what, "exhibition") == 0) {
                    ok = portExhibitionStart(atoi(b1), &why);
                } else if (strcmp(what, "champion") == 0) {
                    ok = portChampionStart(&why);
                } else if (strcmp(what, "nighttail") == 0) {
                    ok = portNighttailStart(&why);
                } else if (strcmp(what, "gallery") == 0) {
                    ok = portGalleryOpen(atoi(b1));
                    why = "not open";
                    if (ok) {
                        portGalleryOpenPage(atoi(b1));
                    }
                } else if (strcmp(what, "mover") == 0) {
                    ok = portPitMoverGo(&why);
                } else if (strcmp(what, "jump") == 0) {
                    /* practice jump POINT: the Practice tab's Jump to chapter (practice_jump.h; 0 the Prologue's
                     * end, 1-8 the chapters, 9 before the Shadow Queen, 10 postgame), asked in the game's
                     * window first as its button asks: the script answers it (A on Yes) */
                    ok = portPracticeJumpAsk(atoi(b1), &why);
                }
                if (!ok) {
                    printf("[input] practice %s: %s\n", what, portPracticeRefusalEnglish(why));
                }
            }
#endif
            printf("[input] practice %s: %s\n", s->name, ok ? "done" : "refused");
            fflush(stdout);
            continue;
        }
        if (s->frames == -25) {
            /* where: the game's state now, for writing scripts */
            static const char* const kPartners[] = {"none",   "Goombella", "Koops",  "Bobbery",
                                                    "Yoshi",  "Flurrie",   "Vivian", "Ms. Mowz"};
            const MarioWork* m = marioGetPtr();
            const PartyEntry* party = partyGetPtr(marioGetPartyId());
            const s32 partner = party != NULL ? (s32)party->currentMemberId : 0;
            printf("[input] where: map '%s', Mario at %.0f %.0f %.0f, story %d, sequence %d, control %d, coins %d, "
                   "HP %d, partner %d (%s), motion %d, pose %s\n",
                   gp != NULL ? gp->currentMapName : "", m->position.x, m->position.y, m->position.z,
                   (int)swByteGet(0), (int)seqGetSeq(), (int)(marioChkKey() && marioChkCtrl()),
                   (int)pouchGetPtr()->coins, (int)pouchGetPtr()->currentHP, (int)partner,
                   partner >= 0 && partner < 8 ? kPartners[partner] : "other", (int)m->currMotionId,
                   m->animName != NULL ? m->animName : "-");
            if (party != NULL && partner > 0) {
                /* where the partner is and what it is doing (its motion, partyChgMot: Koops's shell out ...) */
                printf("[input] where partner: at %.0f %.0f %.0f, motion %d, use motion %d\n", party->position.x,
                       party->position.y, party->position.z, (int)party->motionId, (int)party->useMotionId);
            }
            {
                /* Brothers mode's Luigi (brothers.h) */
                const struct PartyEntry* luigi = portBrothersFieldLuigi();
                if (luigi != NULL) {
                    printf("[input] where Luigi: at %.0f %.0f %.0f, facing %.0f\n", luigi->position.x,
                           luigi->position.y, luigi->position.z, luigi->moveDir);
                }
            }
            /* his speed, air time and screen position (marioChkInScreen: off it, a fall loses its speed) */
            printf("[input] where move: speed %.2f (walk %.2f, dash %.2f), dir %.0f, air %d, vy %.2f, screen %d %d\n",
                   m->baseSpeed, m->baseWalkSpeed, m->baseDashSpeed, m->directionView, (int)m->airTimer,
                   m->jumpVelocityY, (int)m->screenX, (int)m->screenY);
            if (m->characterId == 2) {
                /* Bowser's stages: the HUD's score, coins and time left (kpa.rel keeps the time in GSW 1222),
                   with the game clock, to check the timer's rate against game time */
                printf("[input] where Bowser: score %u, coins %d, time %d, lives %d, game ms %lld\n",
                       (unsigned)pouchGetKpaScore(), (int)pouchGetKpaCoin(), (int)swByteGet(1222),
                       (int)swByteGet(1221), (long long)OSTicksToMilliseconds(gp->renderTime));
            }
            if (m->hitobjWall != NULL || m->hitobjStandOn != NULL) {
                /* what he stands on and the last wall he walked into (a route that stalls: which one) */
                printf("[input] where hits: on '%s', wall '%s'\n",
                       m->hitobjStandOn != NULL && m->hitobjStandOn->joint != NULL ? m->hitobjStandOn->joint->name : "-",
                       m->hitobjWall != NULL && m->hitobjWall->joint != NULL ? m->hitobjWall->joint->name : "-");
            }
            if (m->flags & MARIO_FLAG_PAPER_MODE) {
                /* Paper Mode: the wall he pushes on (a slit's: attr 0x2000) and mot_slit's state */
                HitEntry* push = m->hitobjPush;
                printf("[input] where paper: pushing '%s' (attr 0x%x), slit state 0x%x, facing %.0f, stick %.0f\n",
                       push != NULL && push->joint != NULL ? push->joint->name : "-",
                       push != NULL ? (unsigned)hitGetAttr(push) : 0u, (unsigned)m->slitState,
                       m->directionView, m->controlStickAngle);
            }
            if (s->stickX == 4 || s->stickX == 5) {
                /* where flag|gsw NAME|N ...: saved flags (GSWF) or bytes (GSW) */
                char names[sizeof(s->name)];
                char* tok;
                snprintf(names, sizeof(names), "%s", s->name);
                for (tok = strtok(names, " "); tok != NULL; tok = strtok(NULL, " ")) {
                    const PortNamedValue* table = s->stickX == 4 ? portGswfNames : portGswNames;
                    const int count = s->stickX == 4 ? portGswfNamesCount : portGswNamesCount;
                    char* end;
                    long id = strtol(tok, &end, 0);
                    const char* name = *end == '\0' ? NULL : tok;
                    int k;
                    for (k = 0; k < count; k++) {
                        if (name != NULL ? strcmp(table[k].name, name) == 0 : table[k].value == id) {
                            id = table[k].value;
                            name = table[k].name;
                            break;
                        }
                    }
                    if (name != NULL && k == count) {
                        printf("[input] where %s: no such name '%s'\n", s->stickX == 4 ? "flag" : "gsw", tok);
                        continue;
                    }
                    printf("[input] where %s %s (%ld) = %d\n", s->stickX == 4 ? "flag" : "gsw", name != NULL ? name : "-",
                           id, s->stickX == 4 ? (int)swGet((s32)id) : (int)swByteGet((s32)id));
                }
            } else if (s->stickX == 6) {
                int step = 0;
                float below = 3000.0f;
                sscanf(s->name, "%d %f", &step, &below);
                whereFloor(step, below);
            } else if (s->stickX == 2) {
                char only[64] = "";
                if (sscanf(s->name, "%63s", only) != 1) {
                    only[0] = '\0';
                }
                whereHits(only);
            } else if (s->stickX == 3) {
                whereMobjs();
            } else if (s->stickX == 7) {
                whereItems();
            } else if (s->stickX == 8) {
                whereWords();
            } else if (s->stickX == 9) {
                /* where have N ...: the pouch's count of each item */
                char ids[sizeof(s->name)];
                char* tok;
                snprintf(ids, sizeof(ids), "%s", s->name);
                for (tok = strtok(ids, " "); tok != NULL; tok = strtok(NULL, " ")) {
                    const long id = strtol(tok, NULL, 0);
                    printf("[input] where have %ld = %d\n", id, id > 0 ? (int)pouchCheckItem((u32)id) : 0);
                }
            } else if (s->stickX == 10) {
                /* where windows: the game's windows (windowdrv.c's slots) still in use */
                int i, open = 0;
                for (i = 0; i < PORT_WINDOW_SLOTS; i++) {
                    open += (windowGetPointer(i)->flags & WINDOW_FLAG_USED) != 0;
                }
                printf("[input] where windows: %d open\n", open);
                for (i = 0; i < PORT_WINDOW_SLOTS; i++) {
                    const WindowEntry* w = windowGetPointer(i);
                    if (w->flags & WINDOW_FLAG_USED) {
                        printf("[input] where window %d: type %d, state %d, alpha %d\n", i, (int)w->type,
                               (int)w->state[0] << 8 | w->state[1], (int)w->alpha);
                    }
                }
            } else if (s->stickX) {
                /* where npcs: each NPC of the map (its name as the game has it,
                 * Shift-JIS: full_run.py turns the log into UTF-8) */
                NpcWork* nw = npcGetWorkPtr();
                s32 i;
                for (i = 0; i < nw->total; i++) {
                    const NpcEntry* npc = &nw->entries[i];
                    if (npc->flags & NPC_FLAG_USED) {
                        /* (its talk reach, npcdrv.c's talk check: width; and its flags) */
                        printf("[input] where npc %d: '%s' at %.0f %.0f %.0f, width %.0f height %.0f, flags 0x%x\n",
                               (int)i, npc->description, npc->position.x, npc->position.y, npc->position.z,
                               npc->width, npc->height, (unsigned)npc->flags);
                    }
                }
            }
            fflush(stdout);
            continue;
        }
        if (s->frames == -2) {
            return;
        }
        sLeft = s->frames;
        if (s->seed != 0) {
            randomStart(s->seed);
        }
    }
    s = &sSteps[sCur];
    if (s->frames == -40) {
        /* a dev step: controller 1 is what the devices give (a controller, the keyboard, the touch controls) */
        sLeft = portFlowsStepDone() ? 0 : sLeft - 1;
        return;
    }
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

/* $TTYD_P2_PRESS=KEYS@F[+N][,...] (tests of the second player,
 * second_player_pc.c): KEYS (a b x y z l r start, dup ddown the D-pad, and left right up down for the
 * main stick, joined with '+': about half way once clamped, all the way with "full", as "right+full", N% of the way with "tiltN") held on controller 2 for N frames
 * (default 2) from frame F of the script (counted from its first step, as its
 * waits are: past a $TTYD_MAP warp). $TTYD_P3_PRESS and $TTYD_P4_PRESS:
 * the same on controllers 3 and 4, which are there (plugged in) for the
 * whole script, so their companions come (docs/PLAYERS.md). A seat swap takes a scripted controller
 * to its new port (portInputSwapPorts): its presses land there; its log line keeps its own number.
 * KEYS@ac, @acstart, @zone in place of @F: timed to that player's action command, not a frame (below).
 * "mash" among the KEYS: pressed every other frame while held (a mash command's presses: "a+mash@acstart+90").
 * $TTYD_P1_PRESS: the same on controller 1, this computer's own player's (online tests: a guest's own action
 * commands and guards, timed on its own screen; a shared battle's cues are this game's player's on port 0). */
static const char* const kPressEnv[PAD_MAX_CONTROLLERS] = {"TTYD_P1_PRESS", "TTYD_P2_PRESS", "TTYD_P3_PRESS",
                                                           "TTYD_P4_PRESS"};
static int sPressAt[PAD_MAX_CONTROLLERS] = {0, 1, 2, 3}; /* the port each scripted controller is in now */

/* a scripted controller ($TTYD_P2_PRESS...) is in that port now: its number (1-3), 0 none */
static int pressIn(int port) {
    int src;
    for (src = 1; src < PAD_MAX_CONTROLLERS; src++) {
        if (sPressAt[src] == port && (getenv(kPressEnv[src]) != NULL || sScriptPad[src].plugged)) {
            return src;
        }
    }
    return 0;
}

/* --- KEYS@TRIGGER: a press timed to an action command instead of a frame (tests, media captures) ---
 *
 *   KEYS@ac[~D][+N]       at the ideal moment of the next timed press that controller's player makes (a duo
 *                         move's mark, a stomp's window, a guard's), D frames later (~-2: two early), held N
 *   KEYS@acstart[~D][+N]  D frames after a gauge, stick or mash command of that player's starts
 *   KEYS@zone[~D][+N]     as that command's gauge or marker reaches where it succeeds
 *   KEYS@A..B             held from trigger A until trigger B (Koops's Shell Toss: r@acstart..zone)
 *
 * Every occurrence fires again (each duo move, each turn's command). The commands report their state each
 * battle frame (portPressCue: duo_pc.c, ac_cue_pc.c), only while one of these waits for it. */
#define PRESS_ENTRIES 16
#define PRESS_UNTIL_MAX 600
static int sPadReads;
static int sCueAt[PAD_MAX_CONTROLLERS][PORT_PRESS_CUE_KINDS];    /* the read it was reported after, +1 (0 never) */
static int sCueIdeal[PAD_MAX_CONTROLLERS][PORT_PRESS_CUE_KINDS]; /* toIdeal as reported */
static int sCueSerial[PAD_MAX_CONTROLLERS][PORT_PRESS_CUE_KINDS];
static int sCueBegan[PAD_MAX_CONTROLLERS][PORT_PRESS_CUE_KINDS]; /* the read its occurrence began after */
static int sFired[PAD_MAX_CONTROLLERS][PRESS_ENTRIES];           /* the occurrence an entry fired on (serial) */
static int sHeld[PAD_MAX_CONTROLLERS][PRESS_ENTRIES];            /* frames left (-1: until its second trigger) */
static int sHeldFrom[PAD_MAX_CONTROLLERS][PRESS_ENTRIES];        /* the read it fired on */

int portPressCueWanted(void) {
    static int sWanted = -1;
    if (sWanted < 0) {
        int src;
        sWanted = 0;
        for (src = 0; src < PAD_MAX_CONTROLLERS; src++) {
            const char* v = getenv(kPressEnv[src]);
            if (v != NULL && (strstr(v, "@ac") != NULL || strstr(v, "@zone") != NULL)) {
                sWanted = 1;
            }
        }
    }
    return sWanted;
}

void portPressCue(int port, int kind, int toIdeal) {
    int same;
    if (port < 0 || port >= PAD_MAX_CONTROLLERS || kind < 0 || kind >= PORT_PRESS_CUE_KINDS) {
        return;
    }
    /* reported after the last read already (another unit's, the same frame), or after the one before: the same
       occurrence, unless a timed press's count went back up (the next one) */
    same = sCueAt[port][kind] == sPadReads + 1 || sCueAt[port][kind] == sPadReads;
    if (!same || (kind == PORT_PRESS_CUE_AC && toIdeal > sCueIdeal[port][kind] + 1)) {
        sCueSerial[port][kind]++;
        sCueBegan[port][kind] = sPadReads;
    } else if (sCueAt[port][kind] == sPadReads + 1 && toIdeal > sCueIdeal[port][kind]) {
        return; /* (two in one frame: the nearer press counts) */
    }
    sCueAt[port][kind] = sPadReads + 1;
    sCueIdeal[port][kind] = toIdeal;
}

/* the trigger's name at s: its kind, *n moved past it; -1 none */
static int pressTrigger(const char* s, int* n) {
    if (strncmp(s, "acstart", 7) == 0) {
        *n += 7;
        return PORT_PRESS_CUE_START;
    }
    if (strncmp(s, "ac", 2) == 0) {
        *n += 2;
        return PORT_PRESS_CUE_AC;
    }
    if (strncmp(s, "zone", 4) == 0) {
        *n += 4;
        return PORT_PRESS_CUE_ZONE;
    }
    return -1;
}

/* this read (after its increment): the cue reported after the last one */
static int cueLive(int to, int kind) {
    return sCueAt[to][kind] == sPadReads;
}

/* An entry with a trigger, for the scripted controller src in port to: its keys held this read */
static int pressOnCue(int src, int to, int entry, int trig, int until, int off, unsigned len, const char* keys) {
    int* held = &sHeld[src][entry];
    if (*held > 0) {
        --*held;
        return 1;
    }
    if (*held < 0) {
        /* until the second trigger, the command's end (its first one gone) or the cap */
        if ((cueLive(to, until) && sCueBegan[to][until] >= sHeldFrom[src][entry]) ||
            (trig != PORT_PRESS_CUE_AC && !cueLive(to, trig)) || sPadReads - sHeldFrom[src][entry] > PRESS_UNTIL_MAX) {
            printf("[input] frame %u: controller %d lets go of %s\n", (unsigned)sFrame, src + 1, keys);
            fflush(stdout);
            *held = 0;
            return 0;
        }
        return 1;
    }
    if (!cueLive(to, trig) || sFired[src][entry] == sCueSerial[to][trig]) {
        return 0;
    }
    if (trig == PORT_PRESS_CUE_AC ? sCueIdeal[to][trig] - 1 > -off : sPadReads - 1 - sCueBegan[to][trig] < off) {
        return 0; /* not yet */
    }
    sFired[src][entry] = sCueSerial[to][trig];
    sHeldFrom[src][entry] = sPadReads;
    *held = until >= 0 ? -1 : (int)len - 1;
    printf("[input] frame %u: controller %d %s on its %s (frames to the ideal press %d)%s\n", (unsigned)sFrame,
           src + 1, keys, trig == PORT_PRESS_CUE_AC ? "action command" : trig == PORT_PRESS_CUE_START ? "command's start" : "command's zone",
           trig == PORT_PRESS_CUE_AC ? sCueIdeal[to][trig] - 1 : 0, until >= 0 ? ", held" : "");
    fflush(stdout);
    return 1;
}

static void applyPress(PADStatus* pad, int port) {
    static const char* sList[PAD_MAX_CONTROLLERS];
    static int sRead[PAD_MAX_CONTROLLERS];
    static u32 sClocks[PAD_MAX_CONTROLLERS];
    const int to = sPressAt[port];
    const char* p;
    u32 sClock;
    int entry;
    if (port == 1) {
        sPadReads++; /* (once a read: the cues reported since are this read's) */
    }
    if (!sRead[port]) {
        sRead[port] = 1;
        sList[port] = getenv(kPressEnv[port]);
    }
    if (sState == 2 && sScriptPad[port].plugged && !sScriptPad[port].pulled) {
        /* the script's own presses on this controller (pad N KEYS) */
        if (pad[to].err != PAD_ERR_NONE) {
            memset(&pad[to], 0, sizeof(pad[to])); /* a controller there, nothing pressed */
        }
        if (sScriptPad[port].left > 0) {
            sScriptPad[port].left--;
            pad[to].button |= sScriptPad[port].buttons;
            pad[to].stickX = sScriptPad[port].stickX;
            pad[to].stickY = sScriptPad[port].stickY;
            pad[to].substickX = sScriptPad[port].substickX;
            pad[to].substickY = sScriptPad[port].substickY;
            pad[to].triggerLeft = sScriptPad[port].trigL;
            pad[to].triggerRight = sScriptPad[port].trigR;
        }
    }
    if (sList[port] == NULL || sState != 2) {
        return;
    }
    sClock = ++sClocks[port];
    if ((port >= 2 || to >= 2) && pad[to].err != PAD_ERR_NONE) {
        memset(&pad[to], 0, sizeof(pad[to])); /* a controller there, nothing pressed */
    }
    for (p = sList[port], entry = 0; p != NULL && *p != '\0'; entry++) {
        char keys[64], key[16];
        unsigned at = 0, len = 2;
        u16 bits = 0;
        int sx = 0, sy = 0, n = 0, trig = -1, until = -1, off = 0, full = 0, mash = 0;
        const char* k;
        if (sscanf(p, "%63[^@]@%n", keys, &n) != 1 || n == 0) {
            break;
        }
        if (p[n] >= '0' && p[n] <= '9') {
            char* end;
            at = (unsigned)strtoul(p + n, &end, 10);
            n = (int)(end - p);
        } else if ((trig = pressTrigger(p + n, &n)) < 0) {
            break;
        } else {
            if (p[n] == '~') {
                char* end;
                off = (int)strtol(p + n + 1, &end, 10);
                n = (int)(end - p);
            }
            if (p[n] == '.' && p[n + 1] == '.') {
                int m = n + 2;
                if ((until = pressTrigger(p + m, &m)) < 0) {
                    break;
                }
                n = m;
            }
        }
        if (p[n] == '+') {
            len = (unsigned)atoi(p + n + 1);
        }
        for (k = keys; *k != '\0';) {
            int kn = 0;
            if (sscanf(k, "%15[^+]%n", key, &kn) != 1) {
                break;
            }
            bits |= !strcmp(key, "a") ? PAD_BUTTON_A : !strcmp(key, "b") ? PAD_BUTTON_B : !strcmp(key, "x") ? PAD_BUTTON_X
                    : !strcmp(key, "y") ? PAD_BUTTON_Y : !strcmp(key, "z") ? PAD_TRIGGER_Z : !strcmp(key, "l") ? PAD_TRIGGER_L
                    : !strcmp(key, "r") ? PAD_TRIGGER_R : !strcmp(key, "dup") ? PAD_BUTTON_UP
                    : !strcmp(key, "ddown") ? PAD_BUTTON_DOWN : !strcmp(key, "start") ? PAD_BUTTON_START : 0;
            sx += !strcmp(key, "right") ? 70 : !strcmp(key, "left") ? -70 : 0;
            sy += !strcmp(key, "up") ? 70 : !strcmp(key, "down") ? -70 : 0;
            mash |= !strcmp(key, "mash");
            if (!strcmp(key, "full")) {
                full = 100;
            } else if (!strncmp(key, "tilt", 4)) {
                full = atoi(key + 4);
            }
            k += kn;
            k += *k == '+';
        }
        if (full > 0) {
            /* "full": the stick pushed all the way (127, as a controller's full tilt reads; 70 is about half once
               PADClamp takes its dead zone off); "tiltN": N% of that, a stick whose full tilt reads short (a
               handheld's built-in one) */
            const int v = 127 * (full > 100 ? 100 : full) / 100;
            sx = sx > 0 ? v : sx < 0 ? -v : 0;
            sy = sy > 0 ? v : sy < 0 ? -v : 0;
        }
        if (trig >= 0) {
            if (entry < PRESS_ENTRIES && pressOnCue(port, to, entry, trig, until, off, len, keys)) {
                pad[to].err = PAD_ERR_NONE;
                pad[to].button |= mash && !(sPadReads & 1) ? 0 : bits; /* (mash: every other frame) */
                pad[to].stickX = (s8)sx;
                pad[to].stickY = (s8)sy;
            }
        } else if (sClock >= at && sClock < at + len) {
            pad[to].err = PAD_ERR_NONE;
            pad[to].button |= mash && !(sClock & 1) ? 0 : bits;
            pad[to].stickX = (s8)sx;
            pad[to].stickY = (s8)sy;
            if (sClock == at) {
                printf("[input] frame %u: controller %d %s for %u\n", (unsigned)sFrame, port + 1, keys, len);
                fflush(stdout);
            }
        }
        p = strchr(p, ',');
        p = p != NULL ? p + 1 : NULL;
    }
}

/* $TTYD_PAD_UNPLUG=C@F[+N][,...] (tests of a controller disconnecting, second_player_pc.c): controller C
 * (1-4) pulled out from frame F of the script (counted as $TTYD_P2_PRESS's) for N frames, none: for good
 * (controller 1: until the hold's Start, $TTYD_PAD_LOST_START). Its port reads no controller. */
static u32 sUnplugClock;
static int sReplugged[PAD_MAX_CONTROLLERS];

int portInputUnplugged(int port) {
    static const char* sList;
    static int sRead;
    const char* p;
    if (!sRead) {
        sRead = 1;
        sList = getenv("TTYD_PAD_UNPLUG");
    }
    if (port >= 1 && port < PAD_MAX_CONTROLLERS && sScriptPad[port].pulled) {
        return 1; /* the script's "unplug N" */
    }
    if (sList == NULL || port < 0 || port >= PAD_MAX_CONTROLLERS || sReplugged[port]) {
        return 0;
    }
    for (p = sList; p != NULL && *p != '\0';) {
        unsigned c = 0, at = 0, len = 0;
        int n = 0;
        if (sscanf(p, "%u@%u%n", &c, &at, &n) != 2) {
            break;
        }
        if (p[n] == '+') {
            len = (unsigned)atoi(p + n + 1);
        }
        if ((int)c == port + 1 && sUnplugClock >= at && (len == 0 || sUnplugClock < at + len)) {
            return 1;
        }
        p = strchr(p, ',');
        p = p != NULL ? p + 1 : NULL;
    }
    return 0;
}

/* the hold's Start came (second_player_pc.c): that controller is back for good */
void portInputReplug(int port) {
    if (port >= 0 && port < PAD_MAX_CONTROLLERS) {
        sReplugged[port] = 1;
    }
}

/* --- what is in each controller port (port/include/port/platform.h): a seat swap moves all of it --- */

static int sHome; /* the port with this computer's keyboard and touch controls (0 until a swap) */
static unsigned sSwaps;

int portInputHomePort(void) {
    return sHome;
}

unsigned portInputSwaps(void) {
    return sSwaps;
}

int portInputVirtualAt(int port) {
    if (port < 0 || port >= PAD_MAX_CONTROLLERS) {
        return 0;
    }
#ifdef TTYD_BACKEND_AURORA
    if (port == sHome) {
        return 1; /* (the keyboard, the touch screen) */
    }
#endif
    return pressIn(port) != 0;
}

int portInputHasAny(int port) {
    if (port < 0 || port >= PAD_MAX_CONTROLLERS) {
        return 0;
    }
#ifdef TTYD_BACKEND_AURORA
    if (PADGetIndexForPort((u32)port) >= 0) {
        return 1;
    }
#endif
    return portInputVirtualAt(port);
}

#ifdef TTYD_BACKEND_AURORA
/* the keyboard's state in a port: on, its keys and its stick's keys */
typedef struct {
    int on;
    PADKeyButtonBinding keys[PAD_BUTTON_COUNT];
    PADKeyAxisBinding axes[PAD_AXIS_COUNT];
} KeyState;

static void keysGet(u32 port, KeyState* k) {
    u32 n = 0;
    const PADKeyButtonBinding* keys = PADGetKeyButtonBindings(port, &n);
    const PADKeyAxisBinding* axes;
    memset(k, 0, sizeof(*k));
    k->on = keys != NULL;
    if (keys != NULL) {
        memcpy(k->keys, keys, sizeof(k->keys[0]) * (n < PAD_BUTTON_COUNT ? n : PAD_BUTTON_COUNT));
    }
    n = 0;
    axes = PADGetKeyAxisBindings(port, &n);
    if (axes != NULL) {
        memcpy(k->axes, axes, sizeof(k->axes[0]) * (n < PAD_AXIS_COUNT ? n : PAD_AXIS_COUNT));
    }
}

static void keysSet(u32 port, KeyState* k) {
    if (!k->on) {
        PADClearKeyBindings(port);
        return;
    }
    PADSetKeyboardActive(port, TRUE);
    PADSetKeyButtonBindings(port, k->keys);
    PADSetKeyAxisBindings(port, k->axes);
}

/* the keyboard from port a to port b (whatever was on b to a) */
static void keysSwap(int a, int b) {
    KeyState ka, kb;
    keysGet((u32)a, &ka);
    keysGet((u32)b, &kb);
    if (!ka.on && !kb.on) {
        return;
    }
    keysSet((u32)a, &kb);
    keysSet((u32)b, &ka);
}
#endif

/* everything in ports a and b trades places; homeToo 0: the keyboard and touch controls only from a to b */
static void swapPorts(int a, int b, int homeToo) {
    int src;
    if (a < 0 || b < 0 || a >= PAD_MAX_CONTROLLERS || b >= PAD_MAX_CONTROLLERS || a == b) {
        return;
    }
#ifdef TTYD_BACKEND_AURORA
    {
        /* the controllers (their device index) */
        const s32 ia = PADGetIndexForPort((u32)a);
        const s32 ib = PADGetIndexForPort((u32)b);
        PADClearPort((u32)a);
        PADClearPort((u32)b);
        if (ia >= 0) {
            PADSetPortForIndex((u32)ia, (u32)b);
        }
        if (ib >= 0) {
            PADSetPortForIndex((u32)ib, (u32)a);
        }
    }
    /* the keyboard and the touch controls: this computer's own, where its player sits */
    if (sHome == a || (homeToo && sHome == b)) {
        const int to = sHome == a ? b : a;
        keysSwap(sHome, to);
#ifdef PORT_TOUCH_OVERLAY
        portTouchSetPort(to);
#endif
        sHome = to;
    }
    PADSerializeMappings();
#endif
    for (src = 1; src < PAD_MAX_CONTROLLERS; src++) {
        /* the scripted test controllers */
        if (sPressAt[src] == a) {
            sPressAt[src] = b;
        } else if (sPressAt[src] == b) {
            sPressAt[src] = a;
        }
    }
    sSwaps++;
}

void portInputSwapPorts(int a, int b) {
    swapPorts(a, b, 1);
}

void portInputTakePort(int from, int to) {
    swapPorts(from, to, 0);
}

/* a controller's hold (second_player_pc.c's: player 1's controller gone): the scripted controllers' presses,
   as each frame's (portInputApply's are the game's frames, held then) */
void portInputApplyPresses(PADStatus* pad) {
    applyPress(pad, 1);
    applyPress(pad, 2);
    applyPress(pad, 3);
}

static void applyUnplug(PADStatus* pad) {
    int port;
    if (sState == 2) {
        sUnplugClock++;
        for (port = 1; port < PAD_MAX_CONTROLLERS; port++) {
            if (sScriptPad[port].pulled > 0 && --sScriptPad[port].pulled == 0) {
                printf("[input] frame %u: controller %d plugged back in\n", (unsigned)sFrame, port + 1);
                fflush(stdout);
            }
        }
    }
    for (port = 0; port < PAD_MAX_CONTROLLERS; port++) {
        if (portInputUnplugged(port)) {
            memset(&pad[port], 0, sizeof(pad[port]));
            pad[port].err = PAD_ERR_NO_CONTROLLER;
        }
    }
}

/* $TTYD_AC_CUE_PRESS=1 (tests of the Action Command cue): A pressed for two
 * frames each time the cue comes on (port/src/common/ac_cue_pc.c) */
void portInputApply(PADStatus* pad) {
    static int sPrevCue, sHold;
    static int sCuePress = -1;
    static int sHapticsPress;
    static int sShadow = -1;
    applyScript(pad);
    portFlowsFrame(); /* (a dev step's press let go, its "until", the motion watch: flows_pc.c) */
    if (sShadow < 0) {
        sShadow = getenv("TTYD_INPUT_SHADOW") != NULL;
    }
    if (sShadow) {
        /* $TTYD_INPUT_SHADOW (online tests): the script's steps run, its controller stays still. Online
         * co-op's other games run the host's script this way: its game-changing steps (save, give,
         * story, battle, ...) and the waits they hang on happen in every game of the session at the
         * same frame, as the game's own would, and only the host's presses are played */
        const s8 err = pad[0].err;
        memset(&pad[0], 0, sizeof(pad[0]));
        pad[0].err = err;
    }
    applyPress(pad, 1);
    applyPress(pad, 2);
    applyPress(pad, 3);
    applyPress(pad, 0); /* ($TTYD_P1_PRESS: after controller 2's, which counts the read its cues are of) */
    applyUnplug(pad); /* (after: a pulled controller presses nothing) */
    portMenuApplyPad(pad); /* the mouse's B and wheel in menus */
#ifdef TTYD_BACKEND_AURORA
    if (portBindingsAskedFromSecondScreen()) { /* a key or button being chosen there: not the game's */
        int port;
        for (port = 0; port < PAD_CHANMAX; port++) {
            pad[port].button = 0;
            pad[port].stickX = pad[port].stickY = pad[port].substickX = pad[port].substickY = 0;
            pad[port].triggerLeft = pad[port].triggerRight = 0;
        }
    }
#endif
    portCompanionApplyPad(pad); /* a tap on the second screen (companion_pc.c) */
#ifdef TTYD_BACKEND_AURORA
    if (sKeyFresh) {
        sKeyFresh = 0; /* (down this frame: the game reads it before any frame is counted) */
    } else if (sKeyLeft > 0 && --sKeyLeft == 0) {
        portStoryTestKey(sKeyName, 0); /* key NAME FRAMES: let go */
    }
#endif
    if (sCoopWherePending) {
        /* where coop: once this frame's controllers are all read (PADRead's fresh ones say "no controller" for
           a test's scripted ports until their presses are applied above) */
        sCoopWherePending = 0;
        portCoopWhere(1);
    } else if (sWaitFrames >= 0 && sCur >= 0 && sCur < sCount && sSteps[sCur].frames == -35) {
        sCoopSettledSeen = portCoopWhere(0); /* (where coop MAX waiting: settled yet?) */
    }
    if (sCuePress < 0) {
        const char* h = getenv("TTYD_HAPTICS_PRESS");
        sCuePress = getenv("TTYD_AC_CUE_PRESS") != NULL;
        sHapticsPress = h == NULL ? -1000 : strcmp(h, "last") == 0 ? 1000 : atoi(h);
    }
    if (sHapticsPress > -1000) {
        /* $TTYD_HAPTICS_PRESS=K (tests of controller haptics, ac_cue_pc.c):
         * A pressed for one frame, K frames into the window the haptic cue
         * announces (0 its first frame, "last" its last; -1 the frame before
         * it). The last update counted toJudge frames from its frame to the
         * judgement; this frame's press is judged in toJudge - 1. */
        static int sPressed, sPrevToJudge = -1;
        const int toJudge = gPortAcCueToJudge;
        const int k = sHapticsPress == 1000 ? gPortAcCueWindow - 1 : sHapticsPress;
        if (toJudge < 0 || toJudge > sPrevToJudge) {
            sPressed = 0; /* none, or the next judgement */
        }
        sPrevToJudge = toJudge;
        if (toJudge >= 0 && !sPressed && toJudge - 1 == gPortAcCueWindow - 1 - k) {
            printf("[haptics] frame %u: pressing A, %d frames into the %d-frame window (judged in %d)\n",
                   (unsigned)sFrame, k, gPortAcCueWindow, toJudge - 1);
            fflush(stdout);
            sPressed = 1;
            pad[0].err = PAD_ERR_NONE;
            pad[0].button |= PAD_BUTTON_A;
            /* (analogA left as it is: after the second screen's tap it carries the marks, 0xC0 and up,
               and 255 would read as one: player_menu_pc.c markBusy, ping_pc.c) */
        }
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
        pad[0].button |= PAD_BUTTON_A; /* (analogA left alone: see the haptics press above) */
    }
}
