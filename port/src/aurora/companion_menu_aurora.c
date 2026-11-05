/* The second screen's Start menu: its frame (port/include/port/companion_menu.h,
 * docs/SECOND_SCREEN_MENU.md).
 *
 * Once per game frame, after the game's frame is out and the next one has
 * begun (vi_aurora.c), and while the game's own Start menu is closed, the
 * game's menu code runs once more on a WindowWork of this file's:
 * winRootMain with the second screen's input, then its draws (winRootDisp,
 * the window manager's windows) through a display list of its own, into the
 * frame buffer, which is copied out (GXCopyTex, clearing it as it was) and
 * read back a frame or two later for the presenter.
 *
 * Around it, the game is put back exactly as it was:
 * - its data and bss (the save states' sections, a couple of MB) copied
 *   before and after; the Start menu's own statics are the block swapped for
 *   the second screen's copy (port/companion_section.h), the decoders' kept
 *   (they know the second screen's files from then on);
 * - the audio thread held, the random numbers put back;
 * - its allocations, files, effects, window slots and animation work its own
 *   (the gPortCompanionMenu hooks, port/src/common/companion_menu_pc.c);
 * - nothing reaches the sound (pmario_sound.c) or the game's pad (system.c).
 * $TTYD_COMPANION_AUDIT (Windows) lists what still changes (interp_audit.c).
 *
 * What the menu would change becomes a tap of the second screen
 * (port/companion.h), which the game acts on as input: an item chosen for
 * Mario or a partner, a partner switch. */
#include "port/companion_menu.h"
#include "port/companion.h"
#include "port/platform.h"
#include "port/widescreen.h"
#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>
#include <dolphin/mtx.h>
#include <demo/DEMOPad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "data/item_data.h"
#include "driver/animdrv.h"
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "driver/seqdrv.h"
#include "driver/windowdrv.h"
#include "manager/filemgr.h"
#include "manager/winmgr.h"
#include "mario/mario.h"
#include "mario/mario_party.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "sdk/DEMOInit.h"
#include "system.h"
#include "version.h"
#include "win/win_main.h"
#include "win/win_party.h"
#include "win/win_root.h"
#include "win/win_badge.h"
#include "win/win_item.h"
#include "win/win_log.h"
#include "win/win_mario.h"

size_t portSnapSection(int which, char** start);    /* snapshot_bounds_pc.c */
size_t portCompanionBlock(int which, char** start); /* snapshot_bounds_pc.c */
void portWinSetPtr(WindowWork* w);                  /* win_main.c */
void portAnimWorkInit(AnimationWork* w);            /* animdrv.c */
AnimationWork* portAnimSwapWork(AnimationWork* w);
void portWinMgrWorkInit(WinMgrWork* w);             /* winmgr.c */
WinMgrWork* portWinMgrSwapWork(WinMgrWork* w);
void portCompanionAuditBegin(void);                 /* interp_audit.c: $TTYD_COMPANION_AUDIT */
void portCompanionAuditEnd(void);
void portShotWriteNamed(const char* name, const u8* rgb, u32 w, u32 h); /* shot_aurora.c */
extern s32 entry_n; /* dispdrv.c: the draw list */
extern DispEntry** pSortWork;
extern DispEntry* pDispWork;

#define W PORT_COMPANION_MENU_WIDTH
#define H PORT_COMPANION_MENU_HEIGHT
#define PICTURE_BYTES (W * H * 4)
#define DISP_ENTRIES 256

/* --- the lock: the game thread draws, a presenter's thread reads and taps --- */

static volatile char sLock;

static void lock(void) {
    while (__atomic_test_and_set(&sLock, __ATOMIC_ACQUIRE)) {
    }
}

static void unlock(void) {
    __atomic_clear(&sLock, __ATOMIC_RELEASE);
}

static unsigned char* sPicture; /* RGBA, under the lock */
static unsigned sSerial;        /* under the lock */
static volatile int sReady;     /* a picture can come (readback works) */
static volatile int sFailed;    /* readback unavailable: the presenter's own panels */

/* the presenter's input, queued */
#define QUEUE 16
typedef struct Input {
    int button; /* 0: a tap */
    float x, y;
} Input;
static Input sQueue[QUEUE];
static int sQueueHead, sQueueCount;

void portCompanionMenuTap(float nx, float ny) {
    lock();
    if (sQueueCount < QUEUE) {
        Input* in = &sQueue[(sQueueHead + sQueueCount++) % QUEUE];
        in->button = 0;
        in->x = nx;
        in->y = ny;
    }
    unlock();
}

void portCompanionMenuButton(int button) {
    lock();
    if (sQueueCount < QUEUE && button > 0) {
        Input* in = &sQueue[(sQueueHead + sQueueCount++) % QUEUE];
        in->button = button;
        in->x = in->y = 0.0f;
    }
    unlock();
}

static int nextInput(Input* out) {
    int got = 0;
    lock();
    if (sQueueCount > 0) {
        *out = sQueue[sQueueHead];
        sQueueHead = (sQueueHead + 1) % QUEUE;
        sQueueCount--;
        got = 1;
    }
    unlock();
    return got;
}

unsigned portCompanionMenuPicture(unsigned char* rgba, unsigned* seen) {
    unsigned serial;
    lock();
    serial = sSerial;
    if (serial != 0 && serial != *seen && rgba != NULL) {
        memcpy(rgba, sPicture, PICTURE_BYTES);
        *seen = serial;
    }
    unlock();
    return serial;
}

int portCompanionMenuReady(void) {
    return !sFailed && sReady;
}

/* --- the second screen's menu's own state --- */

typedef struct Block {
    char* at;   /* the game's block */
    size_t size;
    unsigned char* mine;  /* the second screen's copy */
    unsigned char* saved; /* the game's, during a frame (or the sections' copy) */
} Block;

static int sSetUp;      /* 1 set up, -1 can't be */
static Block sSnap[2];  /* data, bss */
static Block sMenu[2];  /* the menu's statics: data, bss */
static Block sKeep[2];  /* the decoders' */
static WindowWork sWork;
static AnimationWork sAnim;
static WinMgrWork sWinMgr;
static DispEntry sDisp[DISP_ENTRIES];
static DispEntry* sSort[DISP_ENTRIES];
static int sOpen;       /* the menu is open (else it opens at the next frame) */
static int sTabSel;     /* the tab it opens at */
static int sBusyFrames; /* frames since the game's own menu closed */
static int sDump;       /* $TTYD_COMPANION_DUMP: its input in the log */
static unsigned sSig;   /* signature() when it opened */
static int sOpenedOnce;

/* the input this frame */
static unsigned sKeys[8];

static int blockSetUp(Block* b, int which, int menu) {
    b->size = menu ? portCompanionBlock(which, &b->at) : portSnapSection(which, &b->at);
    if (b->size == 0) {
        return 0;
    }
    b->mine = (unsigned char*)malloc(b->size);
    b->saved = (unsigned char*)malloc(b->size);
    return b->mine != NULL && b->saved != NULL;
}

/* The menu's statics as the program started (its first frame starts from
 * them): its data as the image holds it, copied before main; its bss zero. */
static unsigned char* sMenuPristine;
static size_t sMenuPristineSize;

/* Before main: the menu's data as the image holds it. */
__attribute__((constructor)) static void portCompanionMenuEarly(void) {
    char* at;
    const size_t size = portCompanionBlock(0, &at);
    if (size > 0 && sMenuPristine == NULL) {
        sMenuPristine = (unsigned char*)malloc(size);
        if (sMenuPristine != NULL) {
            memcpy(sMenuPristine, at, size);
            sMenuPristineSize = size;
        }
    }
}

static int setUp(void) {
    int i;
    if (sSetUp != 0) {
        return sSetUp > 0;
    }
    sSetUp = -1;
    for (i = 0; i < 2; i++) {
        if (!blockSetUp(&sSnap[i], i, 0) || !blockSetUp(&sMenu[i], i, 1)) {
            printf("[companion] menu: no sections to run it in (off)\n");
            fflush(stdout);
            return 0;
        }
        blockSetUp(&sKeep[i], i + 2, 1);
    }
    if (sMenuPristine == NULL || sMenuPristineSize != sMenu[0].size) {
        printf("[companion] menu: its statics were not kept at start (off)\n");
        fflush(stdout);
        return 0;
    }
    memcpy(sMenu[0].mine, sMenuPristine, sMenu[0].size);
    memset(sMenu[1].mine, 0, sMenu[1].size);
    sSetUp = 1;
    return 1;
}

/* --- entering and leaving its frame --- */

static u32 sRand;
/* the virtual clock ($TTYD_FAST, online): the menu's code reads it (the log's map capture, the
   paper poses' draws: sysWaitDrawSync), each read 1 us on, and the second screen is one player's
   alone; os_time_aurora.c is outside the sections, so put back here */
static s64 sClock;

/* What the menu's code writes in the game's heaps, put back after (each one
 * found with $TTYD_COMPANION_AUDIT): the pouch (its getters cache the boots'
 * and hammer's levels, which battles read), the message windows' textures
 * (GXLoadTexObj notes the texture map in them). */
#define KEEP_MAX 8
static struct {
    void* at;
    size_t size;
    unsigned char* copy;
    size_t cap;
} sKept[KEEP_MAX];
static int sKeptCount;

static void keep(void* at, size_t size) {
    if (at == NULL || size == 0 || sKeptCount == KEEP_MAX) {
        return;
    }
    if (sKept[sKeptCount].cap < size) {
        free(sKept[sKeptCount].copy);
        sKept[sKeptCount].copy = (unsigned char*)malloc(size);
        sKept[sKeptCount].cap = sKept[sKeptCount].copy != NULL ? size : 0;
        if (sKept[sKeptCount].copy == NULL) {
            return;
        }
    }
    sKept[sKeptCount].at = at;
    sKept[sKeptCount].size = size;
    memcpy(sKept[sKeptCount].copy, at, size);
    sKeptCount++;
}

static void keptBack(void) {
    while (sKeptCount > 0) {
        sKeptCount--;
        memcpy(sKept[sKeptCount].at, sKept[sKeptCount].copy, sKept[sKeptCount].size);
    }
}

static void enter(void) {
    int i;
    salPcLock();
    sRand = portRandState();
    sClock = portFastClockGet();
    keep(pouchGetPtr(), sizeof(PouchData));
    if (wakuTexObj != NULL && winTpl != NULL) {
        keep(wakuTexObj, sizeof(GXTexObj) * winTpl->numDescriptors);
    }
    for (i = 0; i < 2; i++) {
        memcpy(sSnap[i].saved, sSnap[i].at, sSnap[i].size);
#if !defined(_WIN32)
        /* elsewhere the menu's block is outside the sections */
        memcpy(sMenu[i].saved, sMenu[i].at, sMenu[i].size);
#endif
        memcpy(sMenu[i].at, sMenu[i].mine, sMenu[i].size);
    }
    gPortCompanionMenu = 1;
    portWinSetPtr(&sWork);
    portAnimSwapWork(&sAnim);
    portWinMgrSwapWork(&sWinMgr);
    pDispWork = sDisp;
    pSortWork = sSort;
    entry_n = 0;
}

static void leave(void) {
    int i;
    portCompanionFrameEnd();
    gPortCompanionMenu = 0;
    for (i = 0; i < 2; i++) {
        memcpy(sMenu[i].mine, sMenu[i].at, sMenu[i].size);
#if defined(_WIN32)
        /* the decoders' block is inside the sections: kept through the copy back */
        if (sKeep[i].size > 0) {
            memcpy(sKeep[i].saved, sKeep[i].at, sKeep[i].size);
        }
        memcpy(sSnap[i].at, sSnap[i].saved, sSnap[i].size);
        if (sKeep[i].size > 0) {
            memcpy(sKeep[i].at, sKeep[i].saved, sKeep[i].size);
        }
#else
        memcpy(sSnap[i].at, sSnap[i].saved, sSnap[i].size);
        memcpy(sMenu[i].at, sMenu[i].saved, sMenu[i].size);
#endif
    }
    keptBack();
    portRandSetState(sRand);
    portFastClockSet(sClock);
    salPcUnlock();
}

/* --- the menu --- */

/* the partner menu's target list (win_main.c case 200, win_item.c state 300):
 * the partner with Mario first, then the others */
static int targetParty(int useTarget) {
    WindowPartyDt* list[7];
    int i, n = 0, party;
    if (useTarget <= 0) {
        return 0;
    }
    party = !marioBgmodeChk() ? marioGetParty() : marioGetPtr()->prevPartyMemberId[0];
    for (i = 0; i < 7; i++) {
        if (party == winPartyDt[i].partyId) {
            list[n++] = &winPartyDt[i];
        }
    }
    for (i = 0; i < 7; i++) {
#if VERSION >= VERSION_DEMO_EU
        if (party != winPartyDt[i].partyId && (pouchGetPtr()->partyData[winPartyDt[i].partyId].flags & POUCH_PARTY_FLAG_JOINED) &&
            !(pouchGetPtr()->partyData[winPartyDt[i].partyId].flags & POUCH_PARTY_FLAG_AWAY)) {
#else
        if (party != winPartyDt[i].partyId) {
#endif
            list[n++] = &winPartyDt[i];
        }
    }
    return useTarget - 1 < n ? list[useTarget - 1]->partyId : -1;
}

static void open(void) {
#if VERSION >= VERSION_US
    static const char* const kLang[] = {"jp", "us", "ge", "fr", "sp", "it"};
#else
    static const char* const kLang[] = {"jp", "us"};
#endif
    const u32 lang = (u32)gp->language < sizeof(kLang) / sizeof(kLang[0]) ? (u32)gp->language : 1;
    memset(&sWork, 0, sizeof(sWork));
    sWork.res = fileAllocf(4, WIN_DIR "%s/win.tpl", getMarioStDvdRoot(), kLang[lang]);
    if (sWork.res == NULL) {
        return;
    }
    if (marioGetPtr()->flags & MARIO_FLAG_IS_8_BIT) {
        sWork.flags |= WIN_FLAG_8BIT_MARIO;
    }
    sWork.menuSel = -1; /* at the tab row */
    sWork.tabSel = sTabSel;
    sOpen = 1;
}

/* The menu closed (winRootMain's answer): what it chose becomes a tap. */
static void closed(int ret) {
    printf("[companion] menu closed (%d): partner %d (%d of %d), item %d (slot %d, target %d)\n", ret,
           sWork.party.switchRequested, sWork.party.cursor, sWork.party.openCursor, sWork.item.useItem,
           sWork.item.useIdx, sWork.item.useTarget);
    fflush(stdout);
    if (ret == -2 && sWork.party.switchRequested && sWork.party.cursor != sWork.party.openCursor) {
        portCompanionTap(PORT_COMPANION_TAP_PARTNER, sWork.party.partyIds[sWork.party.cursor], 0);
    } else if (ret == -3 && sWork.item.useItem != ITEM_NULL && sWork.item.useItem != ITEM_RAGGED_DIARY) {
        const int party = targetParty(sWork.item.useTarget);
        if (party >= 0) {
            portCompanionTap(PORT_COMPANION_TAP_ITEM_ON, sWork.item.useIdx, party);
        }
    }
    sOpen = 0;
}

/* What the menu's lists are made from: the pouch, the map. */
static unsigned signature(void) {
    const unsigned char* p = (const unsigned char*)pouchGetPtr();
    unsigned h = 2166136261u;
    size_t i;
    for (i = 0; i < sizeof(PouchData); i++) {
        h = (h ^ p[i]) * 16777619u;
    }
    for (i = 0; i < sizeof(gp->currentMapName) && gp->currentMapName[i] != 0; i++) {
        h = (h ^ (unsigned char)gp->currentMapName[i]) * 16777619u;
    }
    return h;
}

/* Opened again: its opening (the tabs dropping in, the page sliding)
 * run through at once, without input, so it looks as it was. */
static void settle(void) {
    static const unsigned kNone[8];
    int i;
    portCompanionMenuSetKeys(kNone);
    portCompanionMenuSetPoint(0, 0.0f, 0.0f);
    for (i = 0; i < 48 && sOpen; i++) {
        sWork.buttonTrg = sWork.buttonRep = sWork.dirTrg = sWork.dirRep = 0;
        if (winRootMain(&sWork) < 0) {
            sOpen = 0;
        }
        entry_n = 0; /* its draw requests (none are drawn) */
    }
}

/* This frame's input for the menu: one of the presenter's at a time, as a
 * press (and its repeat), the way winMain fills them from the pad. */
static void input(void) {
    static const unsigned kPad[] = {0, PAD_BUTTON_A, PAD_BUTTON_B, PAD_BUTTON_UP, PAD_BUTTON_DOWN,
                                    PAD_BUTTON_LEFT, PAD_BUTTON_RIGHT, PAD_TRIGGER_L, PAD_TRIGGER_R, PAD_BUTTON_X,
                                    PAD_BUTTON_Y, PAD_TRIGGER_Z};
    static const unsigned kDir[] = {0, 0, 0, DEMO_STICK_UP, DEMO_STICK_DOWN, DEMO_STICK_LEFT, DEMO_STICK_RIGHT,
                                    0, 0, 0, 0, 0};
    Input in;
    float gx = 0.0f, gy = 0.0f;
    int kind = 0;
    memset(sKeys, 0, sizeof(sKeys));
    if (nextInput(&in)) {
        if (in.button == 0) {
            kind = 1;
            gx = (in.x - 0.5f) * 608.0f;
            gy = (0.5f - in.y) * 480.0f;
        } else if (in.button < (int)(sizeof(kPad) / sizeof(kPad[0]))) {
            sKeys[0] = sKeys[1] = sKeys[2] = kPad[in.button];
            sKeys[3] = sKeys[4] = sKeys[5] = kDir[in.button];
        }
    }
    /* B on the tab row would close it: the second screen's menu stays open */
    if (sWork.active == 10) {
        sKeys[1] &= ~(PAD_BUTTON_B | PAD_BUTTON_START);
    }
    sWork.buttonTrg = (s32)sKeys[1];
    sWork.buttonRep = (s32)sKeys[2];
    sWork.dirTrg = (s32)sKeys[4];
    sWork.dirRep = (s32)sKeys[5];
    portCompanionMenuSetKeys(sKeys);
    portCompanionMenuSetPoint(kind, gx, gy);
}

static void winDisp(CameraId camId, void* param) {
    (void)param;
    winRootDisp(camId, &sWork);
}

/* --- the picture --- */

static AuroraReadbackStream sStream;
static u8 sCopyId; /* GXCopyTex's dest: only an identifier for Aurora */
static u64 sKey;
static u8* sTiles;
static int sInFlight;
static char sShotName[64];

void portCompanionMenuShot(const char* name) {
    snprintf(sShotName, sizeof(sShotName), "%s", name);
}

static void untile(const u8* tex, u8* rgba) {
    u32 tx, ty, i;
    for (ty = 0; ty < H / 4; ty++) {
        for (tx = 0; tx < W / 4; tx++) {
            const u8* t = tex + (ty * (W / 4) + tx) * 64;
            for (i = 0; i < 16; i++) {
                const u32 x = tx * 4 + (i & 3), y = ty * 4 + (i >> 2);
                u8* o = rgba + (y * W + x) * 4;
                o[0] = t[i * 2 + 1];
                o[1] = t[32 + i * 2];
                o[2] = t[32 + i * 2 + 1];
                o[3] = 0xFF;
            }
        }
    }
}

/* the last copy, read back */
static void collect(void) {
    AuroraReadbackDest dest;
    if (!sInFlight) {
        return;
    }
    if (sTiles == NULL) {
        sTiles = (u8*)malloc(PICTURE_BYTES);
    }
    dest.key = sKey;
    dest.dest = sTiles;
    dest.size = PICTURE_BYTES;
    dest.found = GX_FALSE;
    if (AuroraReadbackGet(sStream, &dest, 1) && dest.found) {
        lock();
        if (sPicture == NULL) {
            sPicture = (unsigned char*)malloc(PICTURE_BYTES);
        }
        untile(sTiles, sPicture);
        sSerial++;
        unlock();
        sInFlight = 0;
        if (sShotName[0] != 0) { /* tests: the picture as a file */
            u8* rgb = (u8*)malloc(W * H * 3);
            int i;
            for (i = 0; rgb != NULL && i < W * H; i++) {
                memcpy(rgb + i * 3, sPicture + i * 4, 3);
            }
            if (rgb != NULL) {
                portShotWriteNamed(sShotName, rgb, W, H);
            }
            free(rgb);
            sShotName[0] = 0;
        }
    } else if (++sInFlight > 30) {
        sInFlight = 0; /* lost: ask again */
    }
}

/* A plain background for the menu: the game draws its own over a capture of
 * the field, which the second screen does without. */
static void background(void) {
    Mtx m;
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_VTX, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetNumTexGens(0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, (GXColor){0, 0, 0, 0});
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    PSMTXCopy(camGetPtr(CAMERA_2D)->view, m);
    GXLoadPosMtxImm(m, GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition3f32(-400.0f, 300.0f, 0.0f);
    GXColor4u8(0x3A, 0x2C, 0x5C, 0xFF);
    GXPosition3f32(400.0f, 300.0f, 0.0f);
    GXColor4u8(0x3A, 0x2C, 0x5C, 0xFF);
    GXPosition3f32(400.0f, -300.0f, 0.0f);
    GXColor4u8(0x16, 0x12, 0x2A, 0xFF);
    GXPosition3f32(-400.0f, -300.0f, 0.0f);
    GXColor4u8(0x16, 0x12, 0x2A, 0xFF);
    GXEnd();
}

/* The menu's draws, as camDraw does for each camera (no widescreen: the
 * picture is the game's 4:3), then copied out, the frame buffer cleared as
 * the frame began. */
static void draw(void) {
    GXRenderModeObj* rmode = DEMOGetRenderModeObj();
    AuroraReadbackCopySrc src;
    /* the cameras' widescreen squeeze, left loaded by the game's frame: not
       here (the picture is 4:3), and the game's again after */
    const f32 scaleX = portWidescreenScaleX();
    int id;
    GXSetProjectionScaleX(1.0f);
    dispEntry(CAMERA_2D, 250.0f, 0, winDisp, NULL);
    winMgrMain(); /* the item list's target windows (its own slots) */
    dispSort();
    GXSetViewport(0.0f, 0.0f, (f32)rmode->fbWidth, (f32)rmode->efbHeight, 0.0f, 1.0f);
    GXSetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
    GXInvalidateVtxCache();
    GXInvalidateTexAll();
    {
        CameraEntry* cam = camGetPtr(CAMERA_2D);
        GXSetProjection(cam->projection, cam->projType);
        background();
    }
    for (id = 0; id < CAMERA_MAX; id++) {
        CameraEntry* camera = camGetPtr((CameraId)id);
        int k, any = 0;
        for (k = 0; k < entry_n && !any; k++) {
            any = pSortWork[k]->cameraId == id;
        }
        if (!any) {
            continue;
        }
        camSetCurNo((CameraId)id);
        GXSetViewport(camera->viewport[0], camera->viewport[1], camera->viewport[2], camera->viewport[3],
                      camera->viewport[4], camera->viewport[5]);
        GXSetScissor(camera->scissor[0], camera->scissor[1], camera->scissor[2], camera->scissor[3]);
        GXSetProjection(camera->projection, camera->projType);
        dispDraw((CameraId)id);
    }
    GXSetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    GXSetColorUpdate(GX_TRUE);
    GXSetAlphaUpdate(GX_TRUE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);
    GXSetViewport(0.0f, 0.0f, (f32)rmode->fbWidth, (f32)rmode->efbHeight, 0.0f, 1.0f);
    GXSetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
    GXSetProjectionScaleX(scaleX);
    if (sStream == 0) {
        sStream = AuroraReadbackCreateStream();
    }
    GXSetTexCopySrc(0, 0, W, H);
    GXSetTexCopyDst(W, H, GX_TF_RGBA8, GX_FALSE);
    GXCopyTex(&sCopyId, GX_TRUE);
    if (sInFlight) {
        return; /* the last is still on its way */
    }
    src.key = ++sKey;
    src.src = &sCopyId;
    if (!AuroraReadbackCaptureCopies(sStream, &src, 1)) {
        if (!sFailed) {
            printf("[companion] menu: no readback on this GPU (or with MSAA): the panels instead\n");
            fflush(stdout);
        }
        sFailed = 1;
        return;
    }
    sInFlight = 1;
}

/* --- the frame --- */

static int wanted(void) {
    static int sForced = -1;
    if (sForced < 0) {
        sForced = getenv("TTYD_COMPANION_MENU") != NULL ? atoi(getenv("TTYD_COMPANION_MENU")) : -1;
    }
    if (sForced == 0 || sFailed) {
        return 0;
    }
    return portCompanionShowing();
}

/* The game lets it run now: the field (Mario himself), the game's own menu
 * closed (and closed a moment: their pictures never mix), no map change. */
static int allowed(void) {
    WindowWork* game;
    MarioWork* mario;
    if (gp == NULL || pouchGetPtr() == NULL || (mario = marioGetPtr()) == NULL || seqCheckSeq()) {
        return 0;
    }
    if (seqGetSeq() != SEQ_GAME || mario->characterId != 0) {
        return 0;
    }
    game = winGetPtr();
    if (game == NULL || game->menuState != 0) {
        sBusyFrames = 0;
        return 0;
    }
    return ++sBusyFrames > 2;
}

void portCompanionMenuFrame(void) {
    static int sTabEnv = -2;
    int ret;
    collect();
    if (!wanted() || !allowed() || !setUp()) {
        return;
    }
    if (sTabEnv == -2) { /* tests: the tab it opens at; its input in the log */
        sDump = getenv("TTYD_COMPANION_DUMP") != NULL;
        const char* v = getenv("TTYD_COMPANION_TAB");
        sTabEnv = v != NULL ? atoi(v) : -1;
        if (sTabEnv >= 0) {
            sTabSel = sTabEnv;
        }
    }
    portCompanionAuditBegin();
    enter();
    if (sAnim.poses == NULL) {
        portAnimWorkInit(&sAnim);
        portWinMgrWorkInit(&sWinMgr);
    }
    {
        /* the game's pouch or map changed: the menu's lists are made when it
         * opens, so it opens again (at the tab row, where nothing is lost) */
        const unsigned sig = signature();
        if (sOpen && sig != sSig && sWork.active == 10 && sWork.menu[0].state == 100) {
            winMarioExit(&sWork);
            winPartyExit(&sWork);
            winItemExit(&sWork);
            winBadgeExit(&sWork);
            winLogExit(&sWork);
            sTabSel = sWork.tabSel;
            sOpen = 0;
        }
        if (!sOpen) {
            open();
            sSig = sig;
            if (sOpen && sOpenedOnce) {
                settle();
            }
            sOpenedOnce = 1;
        }
    }
    if (sOpen) {
        input();
        ret = winRootMain(&sWork);
        if (sDump && (sKeys[1] != 0 || sWork.buttonTrg != 0)) { /* tests */
            printf("[companion] menu input %x: active %d tab %d, partner %d/%d mode %d\n", sKeys[1], sWork.active,
                   sWork.tabSel, sWork.party.cursor, sWork.party.count, sWork.party.mode);
            fflush(stdout);
        }
        if (ret < 0) {
            sTabSel = sWork.tabSel;
            closed(ret);
        }
        draw();
    }
    leave();
    portCompanionAuditEnd();
    sReady = 1;
}
