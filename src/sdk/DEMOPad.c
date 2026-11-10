// DEMOPad: the SDK demo-library pad reader (button edges, repeat, stick directions) used by makeKey.
#include <demo/DEMOPad.h>

#ifdef TARGET_PC
#include "port/assist.h"
#include "port/gameplus.h"
#include "port/toggles.h"
#include "port/platform.h"
#include "port/ping.h"
#include "port/replay.h"
#include "port/view.h"
#include "port/story_test.h"
#include "port/statesync.h"
#include "port/map_edits.h"
#include "port/player_menu.h"
#include "port/menu_buttons.h"
#ifdef TTYD_BACKEND_AURORA
void portKeyboardActivate(void);              /* port/settings/settings_ui.cpp */
void portKeyboardApplyPad(PADStatus* pad); /* port/settings/settings_ui.cpp */
void portWhatsNewPad(PADStatus* pads);     /* port/include/port/whats_new.h */
void portPartyPad(PADStatus* pads);        /* port/include/port/party.h */
#endif
#endif
#define DEMO_STICK_THRESHOLD 48

DEMOPadStatus DemoPad[PAD_MAX_CONTROLLERS];
u32 DemoNumValidPads;
PADStatus Pad[PAD_MAX_CONTROLLERS];
/** Bit of each controller port, used by DEMOPadRead to reset ports with no controller attached. */
u32 PadChanMask[PAD_MAX_CONTROLLERS] = {PAD_CHAN0_BIT, PAD_CHAN1_BIT, PAD_CHAN2_BIT, PAD_CHAN3_BIT};

/**
 * Updates one demo pad from a PAD read: stick and C-stick directions past 48,
 * newly pressed and released buttons and directions, and stick deltas.
 * On a transfer error the edges are cleared and the last state kept.
 */
static void DEMOPadCopy(PADStatus* pad, DEMOPadStatus* dmpad) {
    u16 dirs;

    if (pad->err != PAD_ERR_TRANSFER) {
        dirs = 0;
        if (pad->stickX < -DEMO_STICK_THRESHOLD) {
            dirs |= DEMO_STICK_LEFT;
        }
        if (pad->stickX > DEMO_STICK_THRESHOLD) {
            dirs |= DEMO_STICK_RIGHT;
        }
        if (pad->stickY < -DEMO_STICK_THRESHOLD) {
            dirs |= DEMO_STICK_DOWN;
        }
        if (pad->stickY > DEMO_STICK_THRESHOLD) {
            dirs |= DEMO_STICK_UP;
        }
        if (pad->substickX < -DEMO_STICK_THRESHOLD) {
            dirs |= DEMO_SUBSTICK_LEFT;
        }
        if (pad->substickX > DEMO_STICK_THRESHOLD) {
            dirs |= DEMO_SUBSTICK_RIGHT;
        }
        if (pad->substickY < -DEMO_STICK_THRESHOLD) {
            dirs |= DEMO_SUBSTICK_DOWN;
        }
        if (pad->substickY > DEMO_STICK_THRESHOLD) {
            dirs |= DEMO_SUBSTICK_UP;
        }

        dmpad->dirsNew = (dmpad->dirs ^ dirs) & dirs;
        dmpad->dirsReleased = (dmpad->dirs ^ dirs) & dmpad->dirs;
        dmpad->dirs = dirs;

        dmpad->buttonDown = (dmpad->pst.button ^ pad->button) & pad->button;
        dmpad->buttonUp = (dmpad->pst.button ^ pad->button) & dmpad->pst.button;

        dmpad->stickDeltaX = (s16)(pad->stickX - dmpad->pst.stickX);
        dmpad->stickDeltaY = (s16)(pad->stickY - dmpad->pst.stickY);
        dmpad->substickDeltaX = (s16)(pad->substickX - dmpad->pst.substickX);
        dmpad->substickDeltaY = (s16)(pad->substickY - dmpad->pst.substickY);

        dmpad->pst = *pad;
    } else {
        dmpad->dirsNew = dmpad->dirsReleased = 0;
        dmpad->buttonDown = dmpad->buttonUp = 0;
        dmpad->stickDeltaX = dmpad->stickDeltaY = 0;
        dmpad->substickDeltaX = dmpad->substickDeltaY = 0;
    }
}

/** Reads all four controllers, counts the connected ones, updates DemoPad and resets any unplugged channel. */
void DEMOPadRead(void) {
    s32 i;
    u32 ResetReq = 0;

    PADRead(Pad);
#ifdef TARGET_PC /* $TTYD_INPUT: scripted controller 1 (port/src/common/input_pc.c) */
    portInputApply(Pad);
    portStoryTestPad(Pad); /* tests: the story sweep's text advance (port/include/port/story_test.h) */
#ifdef TTYD_BACKEND_AURORA
    portKeyboardApplyPad(Pad); /* the stick's second keys, the arrows by default */
#endif
    portMenuLayerFrame(); /* a menu up: its marks this frame (port/include/port/menu_buttons.h) */
#ifdef TTYD_BACKEND_AURORA
    portMenuPadLayer(Pad); /* and the controllers' B backs out in it, before the input is recorded or sent */
#endif
    portViewPad(Pad); /* the free camera's C-stick, mirror mode's controls (this player's own) */
    portAssistPad(Pad); /* hold instead of mashing (port/include/port/assist.h), before it's sent */
    portPartnerWheelPad(Pad); /* the partner wheel's stick, not Mario's walk (gameplus.h) */
    portPlayerMenuPad(Pad);  /* players 2-4's own Start menu: its input, their emotes (player_menu_pc.c) */
    portPingPad(Pad);        /* co-op pings: this game's own, in its players' pads (ping_pc.c) */
#ifdef TTYD_BACKEND_AURORA
    portWhatsNewPad(Pad); /* the what's new window up: its keys, and nothing for the game (whats_new_aurora.c) */
#endif
    portMapEditsPad(Pad); /* the map editor open: its keys and buttons, nothing for the game (map_edits_pc.c) */
    portStateSyncApplyPad(Pad); /* online play: a shared battle's lockstep (statesync_pc.c) */
    portReplayApplyPad(Pad); /* replays: the input recorded, or played back (replay_pc.c) */
    portPingTakePad(Pad);    /* everyone's pings taken off: the game gets the input it would without them */
    portPlayerMenuTakePad(Pad); /* the same for the players' emotes (player_menu_pc.c) */
    portChaosPad(Pad);       /* chaos mode's flipped controls (after the exchange: every game of a session alike; port/toggles.h) */
#ifdef TTYD_BACKEND_AURORA
    portPartyPad(Pad); /* a party game up (the quiz, a card duel): its players' input, nothing for the game */
#endif
#endif
    PADClamp(Pad);
    DemoNumValidPads = 0;

    for (i = 0; i < PAD_MAX_CONTROLLERS; i++) {
        if (Pad[i].err == PAD_ERR_NONE || Pad[i].err == PAD_ERR_TRANSFER) {
            DemoNumValidPads++;
        } else if (Pad[i].err == PAD_ERR_NO_CONTROLLER) {
            ResetReq |= PadChanMask[i];
        }
        DEMOPadCopy(&Pad[i], &DemoPad[i]);
    }

    if (ResetReq) {
        PADReset(ResetReq);
    }
}

/** Initialises the PAD library and zeroes all four DemoPad entries. */
void DEMOPadInit(void) {
    s32 i;

    PADInit();
#if defined(TARGET_PC) && defined(TTYD_BACKEND_AURORA)
    portKeyboardActivate(); /* the keyboard plays too (port/settings/settings_ui.cpp) */
#endif
    for (i = 0; i < PAD_MAX_CONTROLLERS; i++) {
        DemoPad[i].pst.button = 0;
        DemoPad[i].pst.stickX = 0;
        DemoPad[i].pst.stickY = 0;
        DemoPad[i].pst.substickX = 0;
        DemoPad[i].pst.substickY = 0;
        DemoPad[i].pst.triggerLeft = 0;
        DemoPad[i].pst.triggerRight = 0;
        DemoPad[i].pst.analogA = 0;
        DemoPad[i].pst.analogB = 0;
        DemoPad[i].pst.err = 0;
        DemoPad[i].buttonDown = 0;
        DemoPad[i].buttonUp = 0;
        DemoPad[i].dirs = 0;
        DemoPad[i].dirsNew = 0;
        DemoPad[i].dirsReleased = 0;
        DemoPad[i].stickDeltaX = 0;
        DemoPad[i].stickDeltaY = 0;
        DemoPad[i].substickDeltaX = 0;
        DemoPad[i].substickDeltaY = 0;
    }
}
